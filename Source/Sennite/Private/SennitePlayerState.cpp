// Fill out your copyright notice in the Description page of Project Settings.

#include "SennitePlayerState.h"
#include "SenniteCharacter.h"
#include "SenniteHeroes.h"
#include "SenniteMenu.h"
#include "SennitePublisher.h"
#include "Engine/GameInstance.h"
#include "Net/UnrealNetwork.h"

ASennitePlayerState::ASennitePlayerState() {
	InBuildMode = false;
	BuildMode = FString("None");
	HoldingWeapon = true; //when spawned, player is holding pickaxe
	HoldingHealingItem = false;
	AimedIn = false;
	EquippedWeapons.Add(0); //pickaxe
	EquippedWeaponsAmmunition.Add(0); // pickaxe
	EquippedWeaponsAmmunition.Add(0); // assault rifle
	EquippedWeaponsAmmunition.Add(0); // shotgun
	EquippedWeaponsClips.Add(0); // pickaxe
	EquippedWeaponsClips.Add(0); // assault rifle
	EquippedWeaponsClips.Add(0); // shotgun
	MaterialCounts.Add(0); // wood
	MaterialCounts.Add(0); // stone
	MaterialCounts.Add(0); // steel
	CurrentWeapon = 0;
	CurrentHealingItem = -1;
	HealingItemCounts.Add(0);
	HealingItemCounts.Add(0);
	JustShotRifle = false;
	JustShotShotgun = false;
	JustSwungPickaxe = false;
	JustUsedHealingItem = false;
	JustReloadedRifle = false;
	JustReloadedShotgun = false;
	KillCount = 0;
}

void ASennitePlayerState::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASennitePlayerState, InBuildMode);
	DOREPLIFETIME(ASennitePlayerState, BuildMode);
	DOREPLIFETIME(ASennitePlayerState, HoldingWeapon);
	DOREPLIFETIME(ASennitePlayerState, HoldingHealingItem);
	DOREPLIFETIME(ASennitePlayerState, EquippedWeapons);
	DOREPLIFETIME(ASennitePlayerState, EquippedWeaponsAmmunition);
	DOREPLIFETIME(ASennitePlayerState, EquippedWeaponsClips);
	DOREPLIFETIME(ASennitePlayerState, MaterialCounts);
	DOREPLIFETIME(ASennitePlayerState, CurrentWeapon);
	DOREPLIFETIME(ASennitePlayerState, CurrentHealingItem);
	DOREPLIFETIME(ASennitePlayerState, HealingItemCounts);
	DOREPLIFETIME(ASennitePlayerState, JustShotRifle);
	DOREPLIFETIME(ASennitePlayerState, JustShotShotgun);
	DOREPLIFETIME(ASennitePlayerState, JustSwungPickaxe);
	DOREPLIFETIME(ASennitePlayerState, JustUsedHealingItem);
	DOREPLIFETIME(ASennitePlayerState, JustReloadedRifle);
	DOREPLIFETIME(ASennitePlayerState, JustReloadedShotgun);
	DOREPLIFETIME(ASennitePlayerState, KillCount);
	DOREPLIFETIME(ASennitePlayerState, FactionId);
	DOREPLIFETIME(ASennitePlayerState, Wallet);
	DOREPLIFETIME(ASennitePlayerState, HeroId);
}

void ASennitePlayerState::ServerChooseHero_Implementation(FName Id, const FString& Pass) {
	// A player changes hero in the lobby, where nobody has a body yet; in play a change would have every
	// machine load and put on another hero, so it is not taken there. Nor more than twice a second.
	const UWorld* World = GetWorld();
	if (World == nullptr || World->GetGameState<ASenniteLobbyState>() == nullptr || World->GetRealTimeSeconds() < NextHeroChoice) {
		return;
	}
	NextHeroChoice = World->GetRealTimeSeconds() + 0.5;
	ClaimHero(Id, Pass);
}

void ASennitePlayerState::ClaimHero(FName Id, const FString& Pass) {
	if (!HasAuthority()) {
		return;
	}
	const FSenniteHero* Hero = USenniteHeroes::Find(Id);
	if (HeroId.IsNone()) {
		SetHero(USenniteHeroes::FindOrDefault(NAME_None).Id);
	}
	if (Hero == nullptr) {
		return;
	}
	if (Hero->bFree) {
		// a later choice: whatever is still being checked no longer counts
		++ClaimSeq;
		SetHero(Id);
		return;
	}
	if (bClaimChecking) {
		// one check at a time for a player; a claim made meanwhile is not taken
		return;
	}
	const UGameInstance* Game = GetGameInstance();
	USennitePublisher* Shop = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
	if (Shop == nullptr || !Shop->IsOn() || !USennitePublisher::IsHeroPass(Pass)) {
		UE_LOG(LogTemp, Warning, TEXT("%s asked to be %s, a hero bought in the shop, with no pass this server can check: refused"), *GetPlayerName(), *Id.ToString());
		return;
	}
	TWeakObjectPtr<ASennitePlayerState> Self(this);
	bClaimChecking = true;
	const int32 Seq = ++ClaimSeq;
	Shop->CheckHeroPass(Pass, [Self, Id, Seq](bool bOk, const TArray<FString>& Owned) {
		ASennitePlayerState* State = Self.Get();
		if (State == nullptr) {
			return;
		}
		State->bClaimChecking = false;
		if (Seq != State->ClaimSeq) {
			// the player chose again while this was checked: the later choice stands
			return;
		}
		if (bOk && Owned.Contains(Id.ToString())) {
			State->SetHero(Id);
		}
		else {
			UE_LOG(LogTemp, Warning, TEXT("%s asked to be %s; the publisher's server does not say their wallet owns it: refused"), *State->GetPlayerName(), *Id.ToString());
		}
	});
}

void ASennitePlayerState::SetHero(FName Id) {
	if (!HasAuthority() || USenniteHeroes::Find(Id) == nullptr || Id == HeroId) {
		return;
	}
	HeroId = Id;
	// a player state goes out about once a second: a new hero is shown to everyone now
	ForceNetUpdate();
	if (ASenniteCharacter* Hero = GetPawn<ASenniteCharacter>()) {
		Hero->SetHero(Id);
	}
}

void ASennitePlayerState::CopyProperties(APlayerState* PlayerState) {
	Super::CopyProperties(PlayerState);
	if (ASennitePlayerState* Next = Cast<ASennitePlayerState>(PlayerState)) {
		Next->HeroId = HeroId;
	}
}

void ASennitePlayerState::StartOver() {
	InBuildMode = false;
	BuildMode = FString("None");
	HoldingWeapon = true;
	HoldingHealingItem = false;
	AimedIn = false;
	EquippedWeapons.Reset();
	EquippedWeapons.Add(0);
	for (int& Rounds : EquippedWeaponsAmmunition) {
		Rounds = 0;
	}
	for (int& Rounds : EquippedWeaponsClips) {
		Rounds = 0;
	}
	for (int& Count : HealingItemCounts) {
		Count = 0;
	}
	CurrentWeapon = 0;
	CurrentHealingItem = -1;
	JustShotRifle = false;
	JustShotShotgun = false;
	JustSwungPickaxe = false;
	JustUsedHealingItem = false;
	JustReloadedRifle = false;
	JustReloadedShotgun = false;
}

void ASennitePlayerState::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);
	ASenniteCharacter* SenniteCharacter = Cast<ASenniteCharacter>(GetPawn());
	if (SenniteCharacter) {

	}
}