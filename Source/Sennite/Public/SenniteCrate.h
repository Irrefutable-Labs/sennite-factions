// A weapon crate: where a hero on an arena gets weapons.
//
// A hero starts with a pickaxe. Crates stand by where heroes start on each base of an arena. A hero
// beside a closed crate opens it (the OpenCrate key): the crate lays a rifle and a shotgun on the
// ground in front of it and a pack of bandages and potions at its ends, to be picked up by walking
// over them, and gives the hero who opened it rounds for both weapons. A while later it is closed
// and full again, and what nobody took is gone.
//
// A crate is the long green ammunition crate of the Fab pack "Wooden Ammo Crates Pack", brought in by
// Tools/import_crate.py. The model is one piece with no lid of its own, so an opened crate looks as
// a closed one does: what shows it is open is the weapons lying in front of it. What it holds, how
// near a hero must be and how long it takes to fill are set in DefaultGame.ini.

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

	/** The crate's model. */
	UPROPERTY(Config)
	FSoftObjectPath Model;

	/** How long a crate is along its long side. The model is scaled evenly to it. */
	UPROPERTY(Config)
	float Across = 150.0f;

	UPROPERTY(Replicated)
	bool bOpen = false;

	/** A hero opens it. On the server. False when it is open already or the hero is not beside it. */
	bool Open(ASenniteCharacter* By);

	/** The crate nearest a place, no farther than Within; with bClosed, among the closed ones. */
	static ASenniteCrate* Nearest(const UWorld* World, const FVector& From, float Within, bool bClosed);

	/**
	 * Stand crates by each start of an arena, clear of what is built there (each a place and how far
	 * around it is taken up). On the server.
	 */
	static void Stock(UWorld* World, const TArray<FTransform>& Starts, const TArray<TPair<FVector, float>>& Built);

	virtual void PostInitializeComponents() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void Restock();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

	/** How big the crate is as it stands: across, front to back, and high. From the model and Across. */
	FVector Size = FVector::ZeroVector;

	/** What it laid out when it was last opened. On the server. */
	TArray<TWeakObjectPtr<AActor>> Laid;
};
