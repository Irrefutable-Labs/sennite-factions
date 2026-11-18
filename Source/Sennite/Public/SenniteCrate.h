// A weapon crate: where a hero on an arena gets weapons.
//
// A hero starts with a pickaxe. Crates stand by where heroes start on each base of an arena. What a
// crate holds is inside it: nothing lies about to be walked over. A hero beside a closed crate opens
// it (the OpenCrate key) and has what it held: the rifle and the shotgun, if the hero has none yet,
// rounds for both, a pack of bandages and potions. The first new weapon is in the hero's hand at once
// and the HUD says what the crate gave. A while later the crate is closed and full again.
//
// A hero that falls leaves a crate where it fell, holding the weapons it had. Whoever opens it has
// them and the crate is gone; unopened it goes after LeftSeconds. Heroes walk through it, so it
// never shuts a bridge.
//
// A crate is the long green ammunition crate of the Fab pack "Wooden Ammo Crates Pack", brought in by
// Tools/import_crate.py. The model is one piece with no lid of its own, so an opened crate looks as
// a closed one does. What it holds, how near a hero must be and how long it takes to fill are set in
// DefaultGame.ini.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SenniteCrate.generated.h"

class ASenniteCharacter;
class UStaticMeshComponent;

UCLASS(Config = Game)
class SENNITE_API ASenniteCrate : public AActor
{
	GENERATED_BODY()

public:
	ASenniteCrate();

	/** How many crates stand on each base of an arena. */
	UPROPERTY(Config)
	int32 PerBase = 2;

	/** How far from where a base's heroes start its crates stand. */
	UPROPERTY(Config)
	float FromStart = 500.0f;

	/** How near a hero must be to open a crate. */
	UPROPERTY(Config)
	float Reach = 250.0f;

	/** How long after it is opened a crate is closed and full again. */
	UPROPERTY(Config)
	float RestockSeconds = 45.0f;

	/** The rounds a crate gives the hero who opens it. */
	UPROPERTY(Config)
	int32 RifleRounds = 90;

	UPROPERTY(Config)
	int32 ShotgunShells = 20;

	/** How long the crate a fallen hero leaves stays if nobody opens it. */
	UPROPERTY(Config)
	float LeftSeconds = 60.0f;

	/** The crate's model. */
	UPROPERTY(Config)
	FSoftObjectPath Model;

	/** How long a crate is along its long side. The model is scaled evenly to it. */
	UPROPERTY(Config)
	float Across = 150.0f;

	UPROPERTY(Replicated)
	bool bOpen = false;

	/** Left by a fallen hero: heroes walk through it, and it is gone once opened. */
	UPROPERTY(Replicated)
	bool bLeft = false;

	/** What it holds, on the server. A crate of an arena holds everything; one a hero left, the weapons that hero had. */
	bool bRifle = true;
	bool bShotgun = true;
	int32 Rounds = 0;
	int32 Shells = 0;
	int32 Bandages = 0;
	int32 Potions = 0;

	/**
	 * A hero opens it and has what it held. On the server. False, and it stays closed and full, when it
	 * is open already, the hero is not beside it, or the hero is busy (building, firing, reloading,
	 * healing).
	 */
	bool Open(ASenniteCharacter* By);

	/** The crate a fallen hero leaves on the ground under where it fell, holding these weapons. On the server; none where there is no ground under it. */
	static ASenniteCrate* Leave(UWorld* World, const AActor* Fallen, bool bHadRifle, bool bHadShotgun);

	/** The crate nearest a place, no farther than Within; with bClosed, among the closed ones. */
	static ASenniteCrate* Nearest(const UWorld* World, const FVector& From, float Within, bool bClosed);

	/**
	 * Stand crates by each start of an arena, clear of what is built there (each a place and how far
	 * around it is taken up). On the server.
	 */
	static void Stock(UWorld* World, const TArray<FTransform>& Starts, const TArray<TPair<FVector, float>>& Built);

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void Restock();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

	/** How big the crate is as it stands: across, front to back, and high. From the model and Across. */
	FVector Size = FVector::ZeroVector;
};
