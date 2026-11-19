// What the game sounds like. Every sound has a name ("step.grass.walk", "ui.click"), and the list in
// Config/DefaultGame.ini says which recordings are that sound (several takes of a step, so no two in a
// row are the same), how loud, and how far it carries. The recordings are brought into /Game/Sounds by
// Tools/cut_sounds.py and Tools/import_sounds.py.
//
// Much of what is heard is worked out here, on each machine, from what that machine sees, so nothing
// more is sent over the network for it: a hero's steps from how far it has walked and what it stands
// on, its jump and landing from when it leaves and meets the ground, its hurt and its fall from its
// health, a shot or a blow landing from where the bullet ended, a piece being built or broken, a thing
// picked up, the storm, the match starting and ending, the wallet's D-bucks arriving, and the air of
// the place. What only the game's own code knows (a reload, a swing, a button pressed) asks for its
// sound by name with Play or PlayAt.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SenniteSounds.generated.h"

class ASenniteCharacter;
class UAudioComponent;
class USoundBase;
class USoundAttenuation;

/** One sound by name: its recordings, and how it is heard. */
USTRUCT()
struct FSenniteSound
{
	GENERATED_BODY()

	UPROPERTY(Config)
	FName Name;

	/** The recordings: one is played, never the same one twice running where there are several. */
	UPROPERTY(Config)
	TArray<FSoftObjectPath> Takes;

	UPROPERTY(Config)
	float Volume = 1.0f;

	/** How far it carries when played at a place: Near, Far or Shot. Played with no place it is heard the same everywhere. */
	UPROPERTY(Config)
	FName Carries = TEXT("Near");
};

UCLASS(Config = Game)
class SENNITE_API USenniteSounds : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Play a sound to this machine's player, the same wherever they are: a button, a hit marker. */
	static void Play(const UObject* In, FName Name);

	/** Play a sound at a place in the world: nearer is louder. */
	static void PlayAt(const UObject* In, FName Name, const FVector& Where);

	/** A hero's own voice (Kind: hit, jump, land, pain), for the heroes that have a human one. It says nothing when the hero attacks or shoots. */
	static void Voice(const ASenniteCharacter* Hero, FName Kind);

	/**
	 * The sound of a menu's button, from the action it does: every menu's buttons, the in-game menu's
	 * too. Each kind of button has its own: one of the first screen or the in-game menu (ui.press); a
	 * section of the store (ui.tab); the arrows that turn to the next hero, skin or gun (ui.turn); one of
	 * a row along the bottom (ui.pick); back (ui.back); a thing done, as choose, buy, swap, start
	 * (ui.confirm); a digit of a keypad (ui.key); a setting a step less or more (ui.step).
	 */
	static FName ButtonSound(FName Action);

	/** Make it heard, once. */
	static void Button(const UObject* In, FName Action);

	/** A sound's recordings, for the checks. */
	static TArray<FSoftObjectPath> Takes(FName Name);

	/** How many recordings a sound names, and how many of them load, for the checks. */
	static int32 TakesNamed(FName Name);
	static int32 TakesThatLoad(FName Name);

	/** Every sound's name, for the checks. */
	static TArray<FName> Names();

	/** How many sounds have been asked for in this world since it began, by name: the checks read what was heard. */
	static int32 Heard(const UObject* In, FName Name);

	/** The same, for every sound whose name begins so ("step." for a step on any ground). */
	static int32 HeardBeginning(const UObject* In, const FString& With);

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UPROPERTY(Config)
	TArray<FSenniteSound> Sounds;

	UPROPERTY(Config)
	FSoftObjectPath NearFalloff;

	UPROPERTY(Config)
	FSoftObjectPath FarFalloff;

	UPROPERTY(Config)
	FSoftObjectPath ShotFalloff;

	/** The heroes that speak with the woman's voice, and with the man's. The others (a robot, a beast) have none here. */
	UPROPERTY(Config)
	TArray<FName> WomensVoices;

	UPROPERTY(Config)
	TArray<FName> MensVoices;

	/** How far a hero walks, and runs, between two steps heard, in cm. */
	UPROPERTY(Config)
	float WalkStride = 140.0f;

	UPROPERTY(Config)
	float RunStride = 210.0f;

	/** The air of a map (by the map's name) and of every other one: a sound that goes round while the map is open. None is set. */
	UPROPERTY(Config)
	TMap<FString, FName> Ambience;

	UPROPERTY(Config)
	FName AmbienceElsewhere;

	UPROPERTY(Config)
	float AmbienceVolume = 0.5f;

private:
	/** What this machine last saw of a hero, to hear what changed. */
	struct FSeen
	{
		FVector At = FVector::ZeroVector;
		float Walked = 0.0f;
		float Health = 0.0f;
		bool bInAir = false;
		float Fell = 0.0f;
		TWeakObjectPtr<AActor> Held;
		bool bFirst = true;
	};

	static const FSenniteSound* Find(FName Name);
	static USoundBase* Take(const FSenniteSound& Sound);
	USoundAttenuation* Falloff(FName Carries);
	void Count(FName Name);

	void HearHero(ASenniteCharacter* Hero, FSeen& Seen, float DeltaTime);
	FName GroundUnder(const ASenniteCharacter* Hero) const;
	void Spawned(AActor* Actor);
	void Destroyed(AActor* Actor);
	void HearStorm(float DeltaTime);
	void HearMatch();
	void HearWallet();
	void Air(FName Name, TObjectPtr<UAudioComponent>& Loop, float Volume);

	TMap<TWeakObjectPtr<ASenniteCharacter>, FSeen> Heroes;
	TMap<FName, int32> Counts;
	/** Pieces just spawned: heard as built once this machine knows they are not a preview. */
	TArray<TPair<TWeakObjectPtr<AActor>, double>> NewPieces;
	TArray<TPair<FVector, double>> BuiltLately;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> Loaded;

	UPROPERTY()
	TObjectPtr<UAudioComponent> AirLoop;

	UPROPERTY()
	TObjectPtr<UAudioComponent> StormLoop;

	FDelegateHandle SpawnedHandle;
	FDelegateHandle DestroyedHandle;
	bool bInStorm = false;
	bool bStormSeen = false;
	uint8 MatchState = 0;
	bool bMatchSeen = false;
	int32 DBucks = 0;
	int32 Owned = 0;
	bool bConnected = false;
	bool bWalletSeen = false;
};
