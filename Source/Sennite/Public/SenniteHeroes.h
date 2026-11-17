// The heroes a player can be, and what each one is to the game: its mesh, its animation Blueprint, its
// firing animation, where its feet are, and how its hands hold a weapon and a healing item. Listed in
// DefaultGame.ini under [/Script/Sennite.SenniteHeroes]; the hand facts are measured on each hero
// (Tools/check_hero_hands.sh), never guessed.
//
// A hero marked free is anyone's. Every other hero is bought with D-bucks in the publisher's shop, as
// a skin is, and is the player's while their wallet proves it owns it.
//
// The player's choice is kept on their machine (USenniteProfile) and sent with every login to a
// server (USenniteLocalPlayer), so the hero is right from the first frame it appears; a change made
// in the lobby goes to the server by ASennitePlayerState::ServerChooseHero. Each player's hero is
// replicated with their player state and their character.

#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/SaveGame.h"
#include "SenniteHeroes.generated.h"

class USkeletalMesh;
class UAnimMontage;

/** One hero a player can be. */
USTRUCT()
struct FSenniteHero
{
	GENERATED_BODY()

	/** What the game calls the hero: in the login address, the player state, the shop and the save. */
	UPROPERTY()
	FName Id;

	/** The hero's name, as players read it. */
	UPROPERTY()
	FString Name;

	/** Anyone may play this hero. Any other is bought with D-bucks in the publisher's shop. */
	UPROPERTY()
	bool bFree = false;

	/** The skeletal mesh, with the materials it ships with. */
	UPROPERTY()
	FSoftObjectPath Mesh;

	/** The hero's own animation Blueprint class. */
	UPROPERTY()
	FSoftClassPath AnimClass;

	/** The hero's own firing animation, played for every shot. None for a hero that ships with none. */
	UPROPERTY()
	FSoftObjectPath FireMontage;

	/** How far below the middle of the character's capsule the mesh's origin goes: its feet, for a mesh whose origin is at its feet.
	 * Every hero has the same capsule (ASenniteCharacter's), so building and being hit are the same for all. */
	UPROPERTY()
	float MeshDrop = 97.0f;

	/** The bone a held weapon is fixed to; how the weapon is turned against it (muzzle +X, top +Z); and where the palm is from it. */
	UPROPERTY()
	FName WeaponBone;

	UPROPERTY()
	FRotator GripRotation = FRotator::ZeroRotator;

	UPROPERTY()
	FVector PalmOffset = FVector::ZeroVector;

	/** The bones of a weapon that is part of the hero's own mesh; hidden while the hero holds a game weapon. */
	UPROPERTY()
	TArray<FName> BuiltInWeaponBones;

	/** The bone a held healing item is fixed to, and where the palm is from it. */
	UPROPERTY()
	FName ItemBone;

	UPROPERTY()
	FVector ItemPalmOffset = FVector::ZeroVector;
};

/** The list of heroes. */
UCLASS(config=Game)
class SENNITE_API USenniteHeroes : public UObject
{
	GENERATED_BODY()

public:
	/** Every hero, in the order the character select shows them. */
	UPROPERTY(Config)
	TArray<FSenniteHero> Heroes;

	/** The hero a player is who has chosen none, or chose one the list no longer has. A free hero. */
	UPROPERTY(Config)
	FName DefaultHero;

	/** The hero of that id; null if the list has none. */
	static const FSenniteHero* Find(FName Id);

	/** The hero of that id, or the default hero. */
	static const FSenniteHero& FindOrDefault(FName Id);

	static const TArray<FSenniteHero>& All() { return GetDefault<USenniteHeroes>()->Heroes; }

	/** Whether this machine's player may play the hero: it is free, or their wallet proved it owns it. */
	static bool MayPlay(const UObject* WorldContext, FName Id);
};

/** What this machine keeps of its player between runs: the hero they chose and the address they last joined. */
UCLASS()
class SENNITE_API USenniteProfile : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName Hero;

	UPROPERTY()
	FString LastAddress;

	/** This machine's profile, read once from its save slot ("Profile", or -ProfileSlot= on the command line). */
	static USenniteProfile& Get();

	/** Write it back to its save slot. */
	void Save();

	/** The hero this player will be: the one chosen, if they may play it; otherwise the default hero. */
	FName PlayingHero(const UObject* WorldContext) const;

private:
	static FString Slot();
};

/** This machine's player, telling every server it logs in to which hero they are (Hero=<id>). */
UCLASS()
class SENNITE_API USenniteLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	virtual FString GetGameLoginOptions() const override;
};
