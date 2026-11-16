// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "SennitePlayerState.generated.h"

class ASenniteCharacter;

/**
 * 
 */
UCLASS()
class SENNITE_API ASennitePlayerState : public APlayerState
{
	GENERATED_BODY()
	
public:
	ASennitePlayerState();

	UPROPERTY(Replicated)
	bool InBuildMode;

	UPROPERTY(Replicated)
	FString BuildMode;

	UPROPERTY(Replicated)
	bool HoldingWeapon;

	UPROPERTY(Replicated)
	bool HoldingHealingItem;

	UPROPERTY(Replicated)
	bool AimedIn;

	UPROPERTY(Replicated)
	TArray<int> EquippedWeapons; //0 for pickaxe, 1 for assault rifle, 2 for shotgun

	UPROPERTY(Replicated)
	TArray<int> EquippedWeaponsAmmunition; //index 1 holds ammo for assault rifle, index 2 holds ammo for shotgun

	//below will be used primarily for keeping track of how many bullets in a clip while switching between guns and building
	UPROPERTY(Replicated)
	TArray<int> EquippedWeaponsClips; //index 1 holds current magazine clip count for assault rifle, index 2 holds current magazine clip count for shotgun

	UPROPERTY(Replicated)
	TArray<int> MaterialCounts; //index 0 holds count for wood, index 1 holds count for stone, index 2 holds count for steel

	UPROPERTY(Replicated)
	TArray<int> HealingItemCounts; //index 0 holds count for bandages, index 1 holds count for potions

	UPROPERTY(Replicated)
	int CurrentWeapon; //0 for pickaxe, 1 for assault rifle, 2 for shotgun, -1 for non weapons like bandages

	UPROPERTY(Replicated)
	int CurrentHealingItem; //0 for bandage, 1 for potion, -1 for non healing type items

	UPROPERTY(Replicated)
	int KillCount;

	/** In a match: the faction this player's wallet is a member of, as the publisher's server said. Empty until it has, and outside a match. */
	UPROPERTY(Replicated)
	FString FactionId;

	/** In a match: the wallet's DSM identity, as people read it. */
	UPROPERTY(Replicated)
	FString Wallet;

	/** Which hero this player is (USenniteHeroes): what they logged in with (Hero=), or chose in the lobby since. */
	UPROPERTY(Replicated)
	FName HeroId;

	/**
	 * This player chooses a hero. Their game lets them choose only a hero that is free or that their
	 * wallet proved it owns; the server checks the hero is on the list, and dresses their character.
	 */
	UFUNCTION(Server, Reliable)
	void ServerChooseHero(FName Id);

	/** On the server: this player is this hero from now on, if the list has it. */
	void SetHero(FName Id);

	/** What a player keeps from one map to the next when the server travels: their hero. */
	virtual void CopyProperties(APlayerState* PlayerState) override;

	/** On the server: back to what a hero starts with. The weapons, rounds and healing items picked up are gone. */
	void StartOver();

	UPROPERTY(Replicated)
	bool JustShotRifle; // used to prevent player from spamming rifle

	UPROPERTY(Replicated)
	bool JustShotShotgun; // used to prevent player from spamming shotgun

	UPROPERTY(Replicated)
	bool JustSwungPickaxe; //used to prevent player from spamming pickaxe

	UPROPERTY(Replicated)
	bool JustUsedHealingItem; //used to prevent player from spamming healing items and doing other things while reloading 

	UPROPERTY(Replicated)
	bool JustReloadedRifle; //used to prevent player from doing other things while reloading

	UPROPERTY(Replicated)
	bool JustReloadedShotgun; //used to prevent player from doing other things while reloading

	virtual bool IsSupportedForNetworking() const override
	{
		return true;
	}

	virtual void Tick(float DeltaSeconds) override;
protected:

private:

};
