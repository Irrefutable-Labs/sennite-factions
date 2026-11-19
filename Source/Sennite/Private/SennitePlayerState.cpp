// Fill out your copyright notice in the Description page of Project Settings.

#include "SennitePlayerState.h"
#include "SenniteCharacter.h"
#include "SenniteHeroes.h"
#include "SenniteMenu.h"
#include "SennitePublisher.h"
#include "Engine/GameInstance.h"
#include "Net/UnrealNetwork.h"

ASennitePlayerState::ASennitePlayerState() {
	PrimaryActorTick.bCanEverTick = true;
	InBuildMode = false;
	BuildMode = FString("None");
	HoldingWeapon = true; //when spawned, player is holding pickaxe
	HoldingHealingItem = false;
	AimedIn = false;
	EquippedWeapons.Add(0); //pickaxe
	EquippedWeaponsAmmunition.Add(0); // pickaxe
	EquippedWeaponsAmmunition.Add(0); // assault rifle
	EquippedWeaponsAmmunition.Add(0); // shotgun
	EquippedWeaponsAmmunition.Add(0); // energy rifle
	EquippedWeaponsAmmunition.Add(0); // sniper
	EquippedWeaponsClips.Add(0); // pickaxe
	EquippedWeaponsClips.Add(0); // assault rifle
	EquippedWeaponsClips.Add(0); // shotgun
	EquippedWeaponsClips.Add(0); // energy rifle
	EquippedWeaponsClips.Add(0); // sniper
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
	DOREPLIFETIME_CONDITION(ASennitePlayerState, HeroAudience, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ASennitePlayerState, GunsOwned, COND_OwnerOnly);
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

void ASennitePlayerState::ServerClaimStartingHero_Implementation(FName Id, const FString& Pass) {
	if (bStartingClaimTaken) {
		return;
	}
	bStartingClaimTaken = true;
	ClaimHero(Id, Pass);
}

void ASennitePlayerState::ServerClaimGuns_Implementation(const FString& Pass) {
	if (bGunClaimTaken) {
		return;
	}
	bGunClaimTaken = true;
	const UGameInstance* Game = GetGameInstance();
	USennitePublisher* Shop = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
	if (Shop == nullptr || !Shop->IsOn() || !USennitePublisher::IsHeroPass(Pass)) {
		UE_LOG(LogTemp, Warning, TEXT("%s showed no pass this server can check for guns bought in the store: they have the free ones"), *GetPlayerName());
		return;
	}
	TWeakObjectPtr<ASennitePlayerState> Self(this);
	Shop->CheckHeroPass(Pass, [Self](bool bOk, const TArray<FString>& Owned, const FString& SigningKey, const FString& PassWallet) {
		ASennitePlayerState* State = Self.Get();
		if (State == nullptr) {
			return;
		}
		// the pass must be this player's wallet's, as for a hero: the match ticket's, or the one their first pass named
		const bool bTheirs = bOk && (State->Wallet.IsEmpty() || State->Wallet == PassWallet) && (State->HeroWallet.IsEmpty() || State->HeroWallet == SigningKey);
		if (!bTheirs) {
			UE_LOG(LogTemp, Warning, TEXT("%s showed a pass for bought guns that is not their wallet's: refused"), *State->GetPlayerName());
			return;
		}
		if (State->HeroWallet.IsEmpty()) {
			State->HeroWallet = SigningKey;
		}
		// THE STORE'S CONTRACT: what the wallet proved it owns names a bought gun by its ShopId, in the same list as its heroes
		State->GunsOwned = ASenniteCharacter::BoughtGunsAmong(Owned);
		UE_LOG(LogTemp, Display, TEXT("%s owns bought guns %d"), *State->GetPlayerName(), State->GunsOwned);
	});
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
	Shop->CheckHeroPass(Pass, [Self, Id, Seq](bool bOk, const TArray<FString>& Owned, const FString& SigningKey, const FString& PassWallet) {
		ASennitePlayerState* State = Self.Get();
		if (State == nullptr) {
			return;
		}
		State->bClaimChecking = false;
		if (Seq != State->ClaimSeq) {
			// the player chose again while this was checked: the later choice stands
			return;
		}
		// the pass must be this player's wallet's: the match ticket's, or the one their first pass named
		const bool bTheirs = bOk && (State->Wallet.IsEmpty() || State->Wallet == PassWallet) && (State->HeroWallet.IsEmpty() || State->HeroWallet == SigningKey);
		if (bTheirs && State->HeroWallet.IsEmpty()) {
			State->HeroWallet = SigningKey;
		}
		if (bTheirs && Owned.Contains(Id.ToString())) {
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
		// what the wallet proved to this server stays proved across a change of map
		Next->GunsOwned = GunsOwned;
		Next->bGunClaimTaken = bGunClaimTaken;
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
	const UGameInstance* Game = GetGameInstance();
	USennitePublisher* Shop = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
	// the server tells each player what to ask a hero pass for, once it knows what it is called
	if (HasAuthority() && HeroAudience.IsEmpty() && Shop != nullptr && USennitePublisher::IsAudience(Shop->ServerAudience())) {
		HeroAudience = Shop->ServerAudience();
	}
	// the player's own game: a hero bought in the shop is claimed once in, with a pass made out to this server
	const APlayerController* Owner = Cast<APlayerController>(GetOwner());
	if (!bStartingClaimAsked && Owner != nullptr && Owner->IsLocalController() && Shop != nullptr && USennitePublisher::IsAudience(HeroAudience)) {
		const FName Want = USenniteProfile::Get().PlayingHero(this);
		const FSenniteHero* Hero = USenniteHeroes::Find(Want);
		if (Hero == nullptr || Hero->bFree || Want == HeroId) {
			bStartingClaimAsked = true;
		}
		else if (Shop->bConnected) {
			bStartingClaimAsked = true;
			TWeakObjectPtr<ASennitePlayerState> Self(this);
			Shop->AskHeroPass(HeroAudience, [Self, Want](const FString& Pass) {
				if (ASennitePlayerState* State = Self.Get()) {
					State->ServerClaimStartingHero(Want, Pass);
				}
			});
		}
	}
	// the player's own game: guns bought in the store are claimed once in, the same way
	if (!bGunClaimAsked && Owner != nullptr && Owner->IsLocalController() && Shop != nullptr && Shop->bConnected && USennitePublisher::IsAudience(HeroAudience)) {
		// THE STORE'S CONTRACT: the wallet's owned list names a bought gun by its ShopId, with its heroes.
		// Asked for only once the wallet has proved it owns one: until then there is nothing to show.
		if (ASenniteCharacter::BoughtGunsAmong(Shop->OwnedHeroes) != 0) {
			bGunClaimAsked = true;
			TWeakObjectPtr<ASennitePlayerState> Self(this);
			Shop->AskHeroPass(HeroAudience, [Self](const FString& Pass) {
				if (ASennitePlayerState* State = Self.Get()) {
					State->ServerClaimGuns(Pass);
				}
			});
		}
	}
}