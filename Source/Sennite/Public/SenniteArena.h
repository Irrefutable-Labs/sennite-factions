// The match arena: the factions' bases, floating, joined by connectors.
//
// A match does not take place on the persistent map. It lays the bases of the factions playing close
// together and fills the gaps between them with connectors, chosen and shaped from a seed. Every
// machine in the match is given the same seed and builds the same arena from it; nothing of the
// arena outlasts the match.
//
// There are six kinds of connector, each a different way across:
//   Floating  pieces to jump between, some with cover to shoot from
//   TreeTop   a boardwalk between huts, high above the bases
//   Canyon    a floor to walk through between two rims that can be held from above
//   Traffic   an open deck that hovering craft cross without stopping
//   Tunnel    a passage through a solid mass
//   Bridges   narrow spans over the drop
//
// FArenaPlan is the arena as numbers, made from the seed alone (MakePlan); ASenniteArena builds a
// plan in the world. The shapes are plain blocks, tinted by kind, until each kind has its own scenery.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SenniteArena.generated.h"

class APlayerStart;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

UENUM()
enum class EConnectorKind : uint8
{
	Floating,
	TreeTop,
	Canyon,
	Traffic,
	Tunnel,
	Bridges,
	Count UMETA(Hidden)
};

/** What a block is, for its colour. */
enum class EArenaTint : uint8 { Base, Floating, Boardwalk, Trunk, Rock, Deck, Craft, TunnelRock, Bridge, Cover, Marking, Hidden, Count };

/** A piece of scenery: seen, never stood on or bumped into. The blocks under it are what a player meets. */
enum class EArenaScenery : uint8 { Cliff, RockyGround, Island, RockA, RockB, RockC, RockD, RockE, RockF, Count };

/** One piece of scenery, in the world. */
struct FArenaProp
{
	EArenaScenery Scenery = EArenaScenery::Cliff;
	FTransform Transform = FTransform::Identity;
	/**
	 * When set, the transform's scale is not a scale but the size the piece is to be, in cm, and its
	 * place is where the middle of the piece is to be; the arena works out the scale from the mesh.
	 */
	bool bFit = false;
};

/** How big an arena is made. */
struct FArenaSizes
{
	float BaseRadius = 12000.0f;
	float ConnectorLength = 24000.0f;
	float ConnectorWidth = 12000.0f;
};

/** One block of the arena: a box, in the world. */
struct FArenaBox
{
	FVector Centre = FVector::ZeroVector;
	FVector Size = FVector::OneVector;
	FQuat Rotation = FQuat::Identity;
	EArenaTint Tint = EArenaTint::Base;
};

/** One hovering craft: it flies its lane end to end, again and again. */
struct FArenaCraft
{
	FVector LaneStart = FVector::ZeroVector;
	FVector LaneDirection = FVector::ForwardVector;
	float LaneLength = 0.0f;
	float Speed = 0.0f;
	/** How far along the lane it is when the match's clock reads nothing. */
	float Offset = 0.0f;
	FVector Size = FVector::OneVector;
	FQuat Rotation = FQuat::Identity;
	int32 Connector = 0;
	int32 Lane = 0;
};

/** One point of the way across a connector. */
struct FArenaStep
{
	FVector Point = FVector::ZeroVector;
	/** The way from this point to the next is a jump. */
	bool bJump = false;
	/** The way from this point to the next crosses this traffic lane (or none: -1). */
	int32 Lane = INDEX_NONE;
};

/** A connector: its kind, where it starts and which way it runs, and a way across it. */
struct FArenaConnector
{
	EConnectorKind Kind = EConnectorKind::Bridges;
	int32 FromBase = 0;
	int32 ToBase = 0;
	FTransform Frame = FTransform::Identity;
	TArray<FArenaStep> Route;
};

/** The whole arena, as numbers. The same seed and number of bases always give the same plan. */
struct FArenaPlan
{
	int32 Seed = 0;
	TArray<FVector> Bases;
	TArray<FArenaConnector> Connectors;
	TArray<FArenaBox> Boxes;
	TArray<FArenaCraft> Craft;
	TArray<FArenaProp> Props;
	/** A number that differs when anything in the plan differs. */
	uint32 Hash = 0;

	/** The kinds of the connectors, in order, as text: "Canyon Traffic Bridges". */
	FString KindsText() const;
};

UCLASS(config=Game)
class SENNITE_API ASenniteArena : public AActor
{
	GENERATED_BODY()

public:
	ASenniteArena();

	/**
	 * A base is a round plot this far from its middle to its edge: room for a faction's base and
	 * ground around it. Set in DefaultGame.ini.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float BaseRadius = 12000.0f;

	/** A connector is this long between two bases, and this wide. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float ConnectorLength = 24000.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float ConnectorWidth = 12000.0f;

	/** The sizes in the settings. */
	static FArenaSizes Sizes();

	/**
	 * The arena for a seed and a number of bases, at the sizes in the settings or at sizes given.
	 * ForcedKind, when it names a kind, makes every connector that kind, for looking at one kind on its own.
	 */
	static FArenaPlan MakePlan(int32 Seed, int32 BaseCount, int32 ForcedKind = INDEX_NONE);
	static FArenaPlan MakePlan(int32 Seed, int32 BaseCount, int32 ForcedKind, const FArenaSizes& Size);

	static const TCHAR* KindName(EConnectorKind Kind);

	/** The plan this arena built, on this machine. */
	const FArenaPlan& GetPlan() const { return Plan; }
	bool IsBuilt() const { return bBuilt; }

	/** The server's plan hash, for a machine that joined to hold its own arena against. */
	uint32 GetServerHash() const { return static_cast<uint32>(ServerHash); }

	/** The start points, one to a base, in the order of the bases. On the server. */
	const TArray<TObjectPtr<APlayerStart>>& GetStarts() const { return StartActors; }

	/** Where a craft is at a moment of the match's clock. */
	static FVector CraftLocation(const FArenaCraft& Craft, double MatchSeconds);

	/** The match's clock, the same on every machine. */
	double MatchSeconds() const;

	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaTime) override;

	/** The seed, when the map's address does not give one (?Seed=). Negative: a different one every match. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	int32 DefaultSeed = -1;

	/** How many bases, when the map's address does not say (?Bases=). Set in DefaultGame.ini. */
	UPROPERTY(Config, EditAnywhere, Category = "Arena")
	int32 DefaultBases = 2;

	/** The unit cube and unit cylinder the blocks and bases are shaped from, and what they are drawn with. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	FSoftObjectPath BlockShape;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	FSoftObjectPath BaseShape;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	FSoftObjectPath BlockMaterial;

	/** The scenery meshes, in the order of EArenaScenery: the cliff, the rocky ground, the island, six rocks. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	TArray<FSoftObjectPath> SceneryMeshes;

	/** How wide the rock under a base is, as a share of the base's width. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float IslandWidth = 0.96f;

	/** What a craft takes from a player it runs into. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float CraftDamage = 20.0f;

protected:
	/** What the server chose, sent to every machine. Negative until chosen. */
	UPROPERTY(ReplicatedUsing = OnRep_Arena)
	int32 MatchSeed = -1;

	UPROPERTY(Replicated)
	int32 MatchBases = 0;

	UPROPERTY(Replicated)
	int32 MatchForcedKind = INDEX_NONE;

	UPROPERTY(Replicated)
	int64 ServerHash = 0;

	UFUNCTION()
	void OnRep_Arena();

private:
	/** Make the plan for what the server chose and put it in the world. */
	void Build();

	FArenaPlan Plan;
	bool bBuilt = false;

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> BlockSets;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> CraftBodies;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Tints;

	UPROPERTY()
	TArray<TObjectPtr<APlayerStart>> StartActors;

	/** When each player was last run into by a craft, so one pass of a craft is one hit. */
	TMap<TWeakObjectPtr<AActor>, double> LastCraftHit;
};
