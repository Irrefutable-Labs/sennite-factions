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
	 * This player chooses a hero, in the lobby, showing a hero pass made out to this server for a hero bought
	 * in the shop. The server takes the choice only in a lobby, not more than twice a second, and only as
	 * ClaimHero does.
	 */
	UFUNCTION(Server, Reliable)
	void ServerChooseHero(FName Id, const FString& Pass);

	/**
	 * Once in, a player whose chosen hero was bought in the shop claims it, with a hero pass made out to this
	 * server (they log in as the default hero). Taken once for each player, anywhere, as ClaimHero does.
	 */
	UFUNCTION(Server, Reliable)
	void ServerClaimStartingHero(FName Id, const FString& Pass);

	/** What this server is called as the audience of a hero pass: what the player's game asks a pass for. Sent to this player alone. */
	UPROPERTY(Replicated)
	FString HeroAudience;

	/** On the server: the wallet (signing key) the first hero pass this player showed spoke for. Every later pass must be that wallet's. */
	FString HeroWallet;

	/**
	 * The guns and blades bought in the store that this player's wallet proved it owns: a bit for each weapon slot.
	 * Set on the server with SetGunsOwned, from what the publisher's server says of a pass the player showed; sent to
	 * the player alone. What is bought is the player's at once, no crate needed; a crate gives the free guns, and
	 * rounds for every gun the player has (ASenniteCharacter::TakeFromCrate).
	 */
	UPROPERTY(Replicated)
	int32 GunsOwned = 0;

	/** What the wallet owns, set on the server: each owned gun or blade is among what the player holds at once, a gun with a full magazine. */
	void SetGunsOwned(int32 Owned);

	/** Each gun or blade the wallet owns that the player does not have yet: among what they hold, a gun with a full magazine. On the server: once owned, and again after a fall. */
	void ArmOwned();

	/**
	 * Once in, a player whose wallet owns guns bought in the store shows a pass made out to this server.
	 * Taken once for each player; the server asks the publisher's server what the pass speaks for, as it
	 * does for a hero, and the player's own game is never taken at its word.
	 */
	UFUNCTION(Server, Reliable)
	void ServerClaimGuns(const FString& Pass);

	/** Whether the claim of guns was taken (on the server), or asked for (on the player's game). */
	bool bGunClaimTaken = false;
	bool bGunClaimAsked = false;

	/** Whether the starting claim was taken (on the server), or asked for (on the player's game). */
	bool bStartingClaimTaken = false;
	bool bStartingClaimAsked = false;

	/**
	 * On the server: this player claims a hero. A free hero is theirs at once. A hero bought in the shop is
	 * theirs only once the publisher's server, asked by this server with a pass made out to it, says the
	 * wallet the pass speaks for proved it owns it, and that wallet is this player's: in a match, the wallet
	 * their ticket named; elsewhere, the wallet of the first pass they showed. Until then, or if not, the
	 * player stays the hero they are (the default hero, if none yet). The player's own game is never taken
	 * at its word for it.
	 */
	void ClaimHero(FName Id, const FString& Pass);

	/** When, on the server's clock, this player's next choice of hero is taken. */
	double NextHeroChoice = 0.0;

	/** On the server: whether a claim to a paid hero is being checked with the publisher's server, and which claim is the latest. */
	bool bClaimChecking = false;
	int32 ClaimSeq = 0;

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
