// Mining: where a player's building materials come from in play (owner, 2026-11-22).
//
// Deposits of wood, stone and steel stand on the land between the bases of an arena. Each holds so many
// units; both sides take from the same ones. A player has one mining robot a match. It is not out until
// the player sends it: the player aims at a deposit and sends the robot (the SendRobot key; ROBOT on the
// touch wheel), and it flies there and takes a unit every so often until the deposit is empty or the
// player sends it to another. Once it is out it stays out, whatever becomes of its player's hero. Anyone
// it is not safe from can shoot it down; a robot shot down is gone for the rest of the match.
//
// What a robot takes is its player's to build with in the match at once (the three materials on the HUD).
// In a match the publisher's match server runs, it is also counted for the player's wallet: when the match
// is over the match server reports what each wallet's robot took, and the publisher's account gives it
// (ASenniteMatch, Server/src/publisher/matches.ts). What such an arena's deposits hold is the publisher's
// to say (world/matches.json), and is the most a match can pay out in mining. An arena with no publisher
// holds what DefaultGame.ini says, and what is mined there is for that game alone.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SenniteMining.generated.h"

class ASenniteCharacter;
class ASennitePlayerState;
class UStaticMeshComponent;

/** A deposit of one material, holding so many units. */
UCLASS(Config = Game)
class SENNITE_API ASenniteDeposit : public AActor
{
	GENERATED_BODY()

public:
	ASenniteDeposit();

	/** How many materials there are: wood, stone, steel, in the order a player's material counts keep them. */
	static constexpr int32 Kinds = 3;

	/** A material by the publisher's symbol for it: WOOD, STONE, STEEL. */
	static const TCHAR* NameOf(int32 InMaterial);
	/** The material a publisher's symbol names, or INDEX_NONE. */
	static int32 MaterialOf(const FString& Symbol);

	/** A deposit's model, for each material. */
	UPROPERTY(Config)
	TArray<FSoftObjectPath> Models;

	/** How far across a deposit of each material is. Its model is scaled evenly to it. */
	UPROPERTY(Config)
	TArray<float> Across;

	/** How many deposits of each material stand on each base's side of the arena. */
	UPROPERTY(Config)
	int32 PerBase = 1;

	/** What the deposits of an arena with no publisher hold of each material, between them. */
	UPROPERTY(Config)
	TArray<int32> Holds;

	/**
	 * How far along the way from a base's edge to the arena's middle each material's deposits stand, from
	 * 0 (the base's edge) to 1 (the middle): what is worth more stands farther out, nearer the other side.
	 */
	UPROPERTY(Config)
	TArray<float> Way;

	/** How far apart, across that way, the deposits of one base's side stand. */
	UPROPERTY(Config)
	float Apart = 2400.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Material)
	int32 Material = 0;

	/** How many units it holds now. An empty deposit is gone from the arena. */
	UPROPERTY(ReplicatedUsing = OnRep_Left)
	int32 Left = 0;

	/** How many units it held when it was stood. */
	UPROPERTY(Replicated)
	int32 Held = 0;

	/** Take up to Units from it, on the server. How many were taken. */
	int32 Take(int32 Units);

	/** How high it stands above the ground it is on. */
	float Height() const { return Size.Z; }

	/**
	 * Stand an arena's deposits, on the server: PerBase of each material on each base's side, holding
	 * Totals[material] between them, shared out evenly. A material with nothing to hold has no deposit.
	 * How many were stood.
	 */
	static int32 Stock(UWorld* World, const TArray<int32>& Totals);

	/**
	 * The deposit a player aims at: of those that hold something and are no farther than Range, the one
	 * nearest the line from From along Toward, and no farther from that line than Off. None when the
	 * player aims at none.
	 */
	static ASenniteDeposit* Aimed(const UWorld* World, const FVector& From, const FVector& Toward, float Range, float Off);

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void OnRep_Material();
	UFUNCTION()
	void OnRep_Left();

	/** Wear the material's model, at its size, standing on the ground at the actor's place. */
	void Dress();
	/** An empty deposit is not seen and stops nothing. */
	void Show();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

	/** How big it is as it stands. From the model and Across. */
	FVector Size = FVector::ZeroVector;
};

/** A player's mining robot. */
UCLASS(Config = Game)
class SENNITE_API ASenniteMiningRobot : public AActor
{
	GENERATED_BODY()

public:
	ASenniteMiningRobot();

	/** The robot's model. */
	UPROPERTY(Config)
	FSoftObjectPath Model;

	/** How far across the robot is. Its model is scaled evenly to it. */
	UPROPERTY(Config)
	float Across = 140.0f;

	/** How fast it flies. */
	UPROPERTY(Config)
	float Speed = 1200.0f;

	/** How far above a deposit's top it hangs while it mines. */
	UPROPERTY(Config)
	float Above = 220.0f;

	/** How high above the land it flies on its way. */
	UPROPERTY(Config)
	float Cruise = 900.0f;

	/** How long it takes to take one unit. */
	UPROPERTY(Config)
	float UnitSeconds = 1.5f;

	/** How much it takes to shoot it down. */
	UPROPERTY(Config)
	int32 StartHealth = 60;

	/** How far off a deposit a player can send it to, and how near the player's aim has to pass the deposit. */
	UPROPERTY(Config)
	float SendRange = 30000.0f;
	UPROPERTY(Config)
	float SendOff = 900.0f;

	UPROPERTY(Replicated)
	int32 Health = 0;

	/** The deposit it was sent to. */
	UPROPERTY(Replicated)
	TObjectPtr<ASenniteDeposit> Target;

	/** It is at its deposit and taking from it. */
	UPROPERTY(Replicated)
	bool bMining = false;

	/** Whose it is. */
	UPROPERTY(Replicated)
	TObjectPtr<ASennitePlayerState> Miner;

	/**
	 * Take a player's robot out, by the hero, on the server. None when the player has one out already,
	 * or theirs was shot down this match.
	 */
	static ASenniteMiningRobot* Launch(ASenniteCharacter* By);

	/** Send it to a deposit, on the server. */
	void SendTo(ASenniteDeposit* Deposit);

	/**
	 * A shot lands on it, on the server. It always stops the shot. It counts when the shooter is not its
	 * own player and, in a match, is of a faction that can harm its player's.
	 */
	void TakeHit(float Damage, ASenniteCharacter* Shooter);

	/** What the HUD says of a player's robot: not out, on its way, mining, idle, or down. */
	static FString Status(const ASennitePlayerState* Of);

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** Where it hangs to mine its deposit. */
	FVector Over(const ASenniteDeposit* Deposit) const;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

	/** How long it has been at the unit it is taking. */
	float Taking = 0.0f;
};
