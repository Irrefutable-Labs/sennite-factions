// Fill out your copyright notice in the Description page of Project Settings.

#include "SennitePlayerState.h"
#include "SenniteCharacter.h"
#include "SenniteHeroes.h"
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

void ASennitePlayerState::ServerChooseHero_Implementation(FName Id) {
	SetHero(Id);
}

void ASennitePlayerState::SetHero(FName Id) {
	if (!HasAuthority() || USenniteHeroes::Find(Id) == nullptr) {
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