// The match arena: one piece of land, made for the match from the plots of the factions playing.
//
// A match does not take place on the persistent map. It sets the plots of the factions playing
// close together on one piece of land and fills the ground between them with connectors, chosen and
// shaped from a seed. Every machine in the match is given the same seed and builds the same arena
// from it; nothing of the arena outlasts the match.
//
// The land is ground to walk on: hills, from the seed, everywhere but where a faction's buildings
// stand (a level pad in the middle of each plot) and where a connector meets a plot. Its shore goes
// down under the water of a lake, and nobody comes back from the water. Nothing of it floats.
//
// There are six kinds of connector, each a different way across the ground between two plots:
//   Floating  a gorge with pieces hanging over it to jump between, some with cover to shoot from
//   TreeTop   a forest with a boardwalk above it, between huts on tall trunks
//   Canyon    a floor to walk through between two rims that can be held from above
//   Traffic   a road that hovering craft cross without stopping
//   Tunnel    a passage through a mass of rock
//   Bridges   a river with arched bridges over it
//
// FArenaPlan is the arena as numbers, made from the seed alone (MakePlan); GroundHeight is its
// land; ASenniteArena builds a plan in the world. What stands on the land is plain blocks, tinted by
// kind, until each kind has its own scenery.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SenniteArena.generated.h"

class APlayerStart;
class UProceduralMeshComponent;
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
enum class EArenaTint : uint8 { Floating, Boardwalk, Trunk, Rock, Deck, Craft, TunnelRock, Bridge, Cover, Marking, Hidden, Count };

/** A piece of scenery: seen, never stood on or bumped into. The blocks under it are what a player meets. */
enum class EArenaScenery : uint8
{
	Cliff, RockyGround, RockA, RockB, RockC, RockD, RockE, RockF,
	// four trees, then seven bushes: what stands round the rim of a base
	TreeA, TreeB, TreeC, TreeD,
	BushA, BushB, BushC, BushD, BushE, BushF, BushG,
	// The dressing of the plots: six rocks of cover and two crags (each plot's landmark), solid by their
	// own shapes, and the steel frame, over blocks; all of them stop heroes and shots. Then three
	// knee-high stones and a patch of stony ground, seen and nothing else. Each has a set of its own, so
	// it is drawn, and met, as it needs.
	CoverRock02, CoverRock04, CoverRock05, CoverRock06, CoverRock07, CoverRock08,
	CragRock03, CragRock11,
	SteelFrame,
	Stone01, Stone10, Stone12,
	StonyGround,
	Count
};

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
	/** The land between two plots: this long from one plot to the next, and this wide. */
	float ConnectorLength = 48000.0f;
	float ConnectorWidth = 24000.0f;
	/**
	 * A built crossing (bridges, a tunnel, a road of traffic, floating pieces, a tree-top walk) is this
	 * long and this wide, in the middle of the land between its two plots; a kind that is land itself
	 * fills that land.
	 */
	float PieceLength = 24000.0f;
	float PieceWidth = 12000.0f;
	/** How many trees and bushes the seed tries to stand round the rim of each base. */
	int32 TreesPerBase = 14;
	int32 BushesPerBase = 24;
	/** The level ground in the middle of a plot, where a faction's buildings stand: this far out from its middle. */
	float PadRadius = 5000.0f;
	/** How far below a plot's level ground the water lies. */
	float WaterDepth = 2000.0f;
	/** How high the land's hills rise above a plot's level ground, about. */
	float HillHeight = 900.0f;
	/**
	 * The dressing of each plot, off its pad: groups of cover rocks on its land; cover rocks along
	 * each way out, beside the straight way to it; outcrops, big rocks on its high ground; crags, its
	 * landmark; steel frames; small stones; and patches of stony ground.
	 */
	int32 CoverPerBase = 10;
	int32 CoverPerWay = 4;
	int32 OutcropsPerBase = 3;
	int32 CragsPerBase = 1;
	int32 FramesPerBase = 1;
	int32 StonesPerBase = 24;
	int32 PatchesPerBase = 6;
	/**
	 * The land past each plot's rim, out to the shore: rocks of the plot's style (some outcrops, some
	 * with a low rock beside), patches of stony ground by them, and groves of trees with bushes.
	 */
	int32 OuterRocksPerBase = 8;
	int32 OuterPatchesPerBase = 4;
	int32 GrovesPerBase = 4;
};

/** One block of the arena: a box, in the world. */
struct FArenaBox
{
	FVector Centre = FVector::ZeroVector;
	FVector Size = FVector::OneVector;
	FQuat Rotation = FQuat::Identity;
	EArenaTint Tint = EArenaTint::Rock;
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

/** A point of a way cut through land: where, along and across from its connector's frame; how high its floor lies there over the plots' level ground; and how far the floor reaches either side. */
struct FArenaTrailPoint
{
	FVector2D At = FVector2D::ZeroVector;
	float Floor = 0.0f;
	float Half = 0.0f;
};

/** A way cut through land, point to point: a gorge, a side valley, a trail. */
struct FArenaTrail
{
	TArray<FArenaTrailPoint> Points;
};

/**
 * A connector: its kind, where what it builds starts and which way it runs, and a way across it. What
 * it builds is Length long and Width wide from its frame; the land between its two plots begins Lead
 * before that, at the first plot's edge, and ends Lead after it. A kind that is land itself (a
 * canyon) fills that land: its ground stands Rise over the plots' level ground, with the ways in
 * Trails cut through it, the first of them the way across.
 */
struct FArenaConnector
{
	EConnectorKind Kind = EConnectorKind::Bridges;
	int32 FromBase = 0;
	int32 ToBase = 0;
	FTransform Frame = FTransform::Identity;
	float Length = 0.0f;
	float Width = 0.0f;
	float Lead = 0.0f;
	float Rise = 0.0f;
	FVector2D GrainAt = FVector2D::ZeroVector;
	TArray<FArenaTrail> Trails;
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
	/** What the land is made from: the sizes, how far a connector starts from its plot's middle, and where the seed set the hills and the coast. */
	FArenaSizes Size;
	float Reach = 0.0f;
	FVector2D HillsAt = FVector2D::ZeroVector;
	FVector2D RipplesAt = FVector2D::ZeroVector;
	FVector2D CoastAt = FVector2D::ZeroVector;
	/**
	 * Each plot's look, in the order of the bases: which rocks its cover is made of (0 blocky, 1 slabs,
	 * 2 rubble), and how dry its grass is, from 0 (lush) to 1. Two plots a connector joins never share
	 * a style.
	 */
	TArray<int32> Styles;
	TArray<float> Dryness;
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
	 * A plot is this far from its middle to its edge: room for a faction's base and ground around
	 * it. Set in DefaultGame.ini.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float BaseRadius = 12000.0f;

	/** The land between two plots is this long from one to the next, and this wide. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float ConnectorLength = 48000.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float ConnectorWidth = 24000.0f;

	/**
	 * How many trees and bushes the seed tries to stand round the rim of each base, clear of every
	 * way out of it. A tree is scenery over a trunk block, which is what a hero or a shot meets; a
	 * bush is scenery alone. Set in DefaultGame.ini.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 TreesPerBase = 14;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 BushesPerBase = 24;

	/**
	 * The dressing of each plot, off its pad, from a random stream of its own: groups of cover rocks on
	 * its land; cover rocks along each way out of it, either side of the straight way to the connector;
	 * a crag on the high ground at its back, its landmark; a steel frame; small stones at the feet of
	 * the rocks; and patches of stony ground. Rocks and crags are solid by their own shapes and the frame
	 * stands over blocks, so all of them stop heroes and shots; the stones and patches are only seen. Set
	 * in DefaultGame.ini.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 CoverPerBase = 10;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 CoverPerWay = 4;

	/** Big rocks, two heroes tall and more, on the high ground of each plot off its ways. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 OutcropsPerBase = 3;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 CragsPerBase = 1;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 FramesPerBase = 1;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 StonesPerBase = 24;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 PatchesPerBase = 6;

	/** The land past each plot's rim: rocks, patches of stony ground, groves of trees with bushes. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 OuterRocksPerBase = 8;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 OuterPatchesPerBase = 4;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	int32 GrovesPerBase = 4;

	/** The level ground in the middle of a plot, where a faction's buildings stand. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float PadRadius = 5000.0f;

	/** How far below a plot's level ground the water of the lake lies. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float WaterDepth = 2000.0f;

	/** How high the land's hills rise above a plot's level ground, about. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float HillHeight = 900.0f;

	/** The sizes in the settings. */
	static FArenaSizes Sizes();

	/**
	 * How high the land of a plan is at a place: the same on every machine, from the plan alone.
	 * Level (0) on each plot's pad; the hills elsewhere on the land; a connector's own ground between
	 * plots; and down under the water past the shore.
	 */
	static double GroundHeight(const FArenaPlan& Plan, double X, double Y);

	/** How far a place is outside the land's outline, the plots and the connectors: nothing inside it. */
	static double OutsideLand(const FArenaPlan& Plan, double X, double Y);

	/**
	 * What the ground is painted with at a place, from the plan alone: red is gravel (the paths from
	 * each pad out to its ways and the yard round each steel frame), green is dirt (under trees and
	 * round the plots' rocks), blue is the ground of a connector that is land itself (a canyon's sand
	 * and rock), alpha is how dry the grass of the nearest plot is. The ground's material draws grass,
	 * dirt and gravel by it; the material of a land connector's own ground draws its ground by blue.
	 */
	static FLinearColor GroundPaint(const FArenaPlan& Plan, double X, double Y);

	/**
	 * The size of a dressing mesh as imported, in cm (Saved/import_rocks.txt, Saved/import_scenery.txt):
	 * the plan sizes each piece in its mesh's own proportions, and the blocks under it from that. Zero
	 * for a piece of scenery that is not dressing.
	 */
	static FVector ShapeOf(EArenaScenery Kind);

	/** How wide the shore is: past the land's outline the ground goes down under the water over this far. */
	static constexpr double ShoreWidth = 4500.0;

	/** How far past its outline, the plots and the connectors, the land's coast wanders at most. */
	static constexpr double CoastReach = 6000.0;

	/** How far apart the points of the ground's mesh are. */
	static constexpr double GroundCell = 500.0;

	/** The ground as this machine built it: what heroes stand on. */
	UProceduralMeshComponent* GetGround() const { return Ground; }

	/**
	 * The arena for a seed and a number of bases, at the sizes in the settings or at sizes given.
	 * ForcedKind, when it names a kind, makes every connector that kind, for looking at one kind on its own.
	 */
	static FArenaPlan MakePlan(int32 Seed, int32 BaseCount, int32 ForcedKind = INDEX_NONE);
	static FArenaPlan MakePlan(int32 Seed, int32 BaseCount, int32 ForcedKind, const FArenaSizes& Size);

	static const TCHAR* KindName(EConnectorKind Kind);

	/** A kind that is land itself, shaped ground a hero walks through, not a crossing built on level ground. */
	static bool IsLand(EConnectorKind Kind);

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

	/** The unit cube the blocks are shaped from, and what they are drawn with. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	FSoftObjectPath BlockShape;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	FSoftObjectPath BlockMaterial;

	/** What the land is drawn with. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	FSoftObjectPath BaseGround;

	/** What the ground of a canyon is drawn with: the land's own material with sand and rock where the ground's paint is blue. Empty: the land's. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	FSoftObjectPath CanyonGround;

	/** What the traffic deck and the bridge spans are drawn with. Empty: their plain tints. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	FSoftObjectPath RoadSurface;

	/**
	 * The scenery meshes, in the order of EArenaScenery: the cliff, the rocky ground, six rocks, four
	 * trees, seven bushes, then the plots' dressing (six cover rocks, two crags, the steel frame, three
	 * stones, stony ground). Set in DefaultGame.ini.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	TArray<FSoftObjectPath> SceneryMeshes;

	/** What a craft takes from a player it runs into. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float CraftDamage = 20.0f;

	/**
	 * The country round the arena, and its water: mountains and a lake, seen and nothing else. Each
	 * mesh has the place the arena stands at its origin and its lake's surface at its own height 0
	 * (Tools/import_land.py). It is set with its lake WaterDepth below the plots' level ground, so
	 * the arena's land rises out of its lake, and made LandScale times its size, so the lake's open
	 * water is wider than the widest arena. Empty: round the arena is haze. Set in DefaultGame.ini.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	FSoftObjectPath Land;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	FSoftObjectPath LandWater;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Arena")
	float LandScale = 1.5f;

	/** The land as this machine drew it: none on a machine with no screen. */
	const TArray<TObjectPtr<UStaticMeshComponent>>& GetLand() const { return LandParts; }

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
	TArray<TObjectPtr<UStaticMeshComponent>> LandParts;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Ground;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Tints;

	UPROPERTY()
	TArray<TObjectPtr<APlayerStart>> StartActors;

	/** When each player was last run into by a craft, so one pass of a craft is one hit. */
	TMap<TWeakObjectPtr<AActor>, double> LastCraftHit;
};
