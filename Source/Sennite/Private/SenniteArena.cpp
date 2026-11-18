#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "SenniteCrate.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerStart.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"

namespace
{
	// connectors' floors sit a hair under the bases', so two surfaces never share a plane
	constexpr float FloorTop = -2.0f;

	/** Lays one connector's blocks, in the connector's own space: x from its near base to its far one, y across, z up from the bases' floor. */
	struct FBuilder
	{
		FArenaPlan& Plan;
		FArenaConnector& Connector;
		FRandomStream& Rand;
		int32 ConnectorIndex;
		/** How long the connector is between its two bases, and how wide. */
		float Length;
		float Width;
		/** How high the bed of a gorge is: under the water. What stands in a gorge stands on it. */
		float Bed;

		float R(float Low, float High) { return Rand.FRandRange(Low, High); }
		bool Chance(float Odds) { return Rand.FRand() < Odds; }
		float Side() { return Chance(0.5f) ? -1.0f : 1.0f; }

		void Box(const FVector& Centre, const FVector& Size, EArenaTint Tint, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			FArenaBox Block;
			Block.Centre = Connector.Frame.TransformPosition(Centre);
			Block.Size = Size;
			Block.Rotation = Connector.Frame.GetRotation() * Rotation.Quaternion();
			Block.Tint = Tint;
			Plan.Boxes.Add(Block);
		}
		/** A flat slab whose top is at Top. */
		void Slab(float X0, float X1, float Y0, float Y1, float Top, float Thick, EArenaTint Tint)
		{
			Box(FVector((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, Top - Thick * 0.5f), FVector(X1 - X0, Y1 - Y0, Thick), Tint);
		}
		/** A ramp along x whose top runs from (X0, Z0) to (X1, Z1), centred on Y. */
		void Ramp(float X0, float Z0, float X1, float Z1, float Y, float Wide, EArenaTint Tint, float Thick = 30.0f)
		{
			const float Run = X1 - X0;
			const float Rise = Z1 - Z0;
			const FRotator Tilt(FMath::RadiansToDegrees(FMath::Atan2(Rise, Run)), 0.0f, 0.0f);
			const FVector Middle((X0 + X1) * 0.5f, Y, (Z0 + Z1) * 0.5f);
			Box(Middle - Tilt.RotateVector(FVector::UpVector) * (Thick * 0.5f), FVector(FMath::Sqrt(Run * Run + Rise * Rise), Wide, Thick), Tint, Tilt);
		}
		/** A piece of scenery, placed like a block. */
		void Prop(EArenaScenery Scenery, const FVector& Location, const FRotator& Rotation, const FVector& Scale)
		{
			FArenaProp Piece;
			Piece.Scenery = Scenery;
			Piece.Transform = FTransform(Connector.Frame.GetRotation() * Rotation.Quaternion(), Connector.Frame.TransformPosition(Location), Scale);
			Plan.Props.Add(Piece);
		}
		/** A rock, one of the six, turned any way round, filling a box of the given size about a middle. */
		void Rock(const FVector& Middle, const FVector& Size)
		{
			FArenaProp Piece;
			Piece.Scenery = static_cast<EArenaScenery>(static_cast<int32>(EArenaScenery::RockA) + Rand.RandRange(0, 5));
			Piece.Transform = FTransform(Connector.Frame.GetRotation() * FRotator(0.0f, Chance(0.5f) ? 180.0f : 0.0f, 0.0f).Quaternion(), Connector.Frame.TransformPosition(Middle), Size);
			Piece.bFit = true;
			Plan.Props.Add(Piece);
		}
		/** The next point of the way across. */
		void Step(const FVector& Point, bool bJump = false, int32 Lane = INDEX_NONE)
		{
			FArenaStep Next;
			Next.Point = Connector.Frame.TransformPosition(Point);
			Next.bJump = bJump;
			Next.Lane = Lane;
			Connector.Route.Add(Next);
		}

		void Floating();
		void TreeTop();
		void Canyon();
		void Traffic();
		void Tunnel();
		void Bridges();
	};

	// A gorge, and pieces hanging over it to jump between, in several lanes: some small, some broad
	// enough to fight on, some carrying a wall to shoot from behind. The one crossing that floats.
	void FBuilder::Floating()
	{
		const float Half = Width * 0.5f;
		Slab(-400.0f, 220.0f, -Half, Half, FloorTop, 60.0f, EArenaTint::Floating);
		float FarEdge = Length - 220.0f;
		Step(FVector(-100.0f, 0.0f, 0.0f));
		const int32 Lanes = FMath::Clamp(FMath::RoundToInt(Width / 2400.0f), 2, 6);
		const int32 Way = Lanes / 2;
		for (int32 Lane = 0; Lane < Lanes; ++Lane) {
			const bool bWay = Lane == Way;
			const float LaneY = -Half + Width * (Lane + 0.5f) / Lanes;
			const float Stray = Width / Lanes * 0.3f;
			float X = 220.0f;
			float Y = LaneY;
			float Z = 0.0f;
			if (bWay) {
				Step(FVector(195.0f, Y, 0.0f), true);
			}
			while (X < Length - 2000.0f) {
				// A jump at a walk carries 2.8 to 3.7 m, less going up and more going down. Every gap, shift
				// and step is inside what that reaches, with room to land.
				const float Gap = R(130.0f, 190.0f);
				const float Long = Chance(0.3f) ? R(900.0f, 1500.0f) : R(380.0f, 700.0f);
				const float Wide = FMath::Max(R(380.0f, 900.0f), Long * 0.5f);
				Y = FMath::Clamp(Y + R(-120.0f, 120.0f), LaneY - Stray, LaneY + Stray);
				// the last few pieces come back down to the far base, and no piece is more than a jump's
				// rise above the one before
				// (the lowest a piece sits is three rises under the base, and the way home is at least three pieces)
				const bool bHome = X > Length - 8000.0f;
				Z = FMath::Min(Z + 70.0f, FMath::Clamp(Z + R(-90.0f, 70.0f), bHome ? -40.0f : -200.0f, bHome ? 60.0f : 700.0f));
				const float X0 = X + Gap;
				const float X1 = X0 + Long;
				Slab(X0, X1, Y - Wide * 0.5f, Y + Wide * 0.5f, Z, 60.0f, EArenaTint::Floating);
				// the rock the piece is the top of: a little wider than it, hanging under it
				const float Deep = FMath::Clamp(FMath::Max(Long, Wide) * R(0.5f, 0.8f), 260.0f, 1000.0f);
				Rock(FVector((X0 + X1) * 0.5f, Y, Z - 60.0f - Deep * 0.45f), FVector(Long * 1.12f, Wide * 1.12f, Deep));
				if (Long > 800.0f || Chance(0.3f)) {
					Box(FVector((X0 + X1) * 0.5f, Y + Wide * 0.34f, Z + 80.0f), FVector(Long * 0.45f, 30.0f, 160.0f), EArenaTint::Cover);
				}
				if (bWay) {
					Step(FVector(X0 + 70.0f, Y, Z));
					Step(FVector(X1 - 25.0f, Y, Z), true);
				}
				X = X1;
			}
			const float Landing = X + R(130.0f, 190.0f);
			FarEdge = FMath::Min(FarEdge, Landing);
			if (bWay) {
				Step(FVector(Landing + 90.0f, Y, 0.0f));
			}
		}
		Slab(FarEdge, Length + 400.0f, -Half, Half, FloorTop, 60.0f, EArenaTint::Floating);
		Step(FVector(Length + 100.0f, 0.0f, 0.0f));
	}

	// A boardwalk between huts on tall trunks, over a forest that stands on the ground between the
	// plots: a ramp up from each plot, planks from hut to hut. The ground under it can be walked too.
	void FBuilder::TreeTop()
	{
		const float High = R(900.0f, 1300.0f);
		const float Run = High / FMath::Tan(FMath::DegreesToRadians(27.0f));
		const float Spread = Width * 0.36f;
		const float NearY = R(-Spread * 0.5f, Spread * 0.5f);
		const float FarY = R(-Spread * 0.5f, Spread * 0.5f);
		TArray<FVector> Nodes;

		Slab(-400.0f, 60.0f, NearY - 400.0f, NearY + 400.0f, FloorTop, 60.0f, EArenaTint::Boardwalk);
		Ramp(0.0f, 0.0f, Run, High, NearY, 420.0f, EArenaTint::Boardwalk);
		Slab(Run, Run + 500.0f, NearY - 300.0f, NearY + 300.0f, High, 30.0f, EArenaTint::Boardwalk);
		Nodes.Add(FVector(Run + 250.0f, NearY, High));

		// a hut every 18 m or so, wandering across the width
		const float First = Run + 1500.0f;
		const float Last = Length - Run - 1500.0f;
		const int32 Huts = FMath::Max(3, FMath::RoundToInt((Last - First) / 1800.0f) + 1);
		// The walk wanders, but never turns so sharply that a plank meets a hut's wall: at most Turn to
		// the side from one hut to the next. The first hut is straight on from the near ramp, and the
		// last few come round to be straight on to the far one.
		const float Turn = (Last - First) / (Huts - 1) * 0.55f;
		float Y = NearY;
		for (int32 Hut = 0; Hut < Huts; ++Hut) {
			const float X = FMath::Lerp(First, Last, Hut / float(Huts - 1));
			if (Hut > 0) {
				const float Left = Turn * (Huts - 1 - Hut);
				Y = FMath::Clamp(FMath::Clamp(Y + R(-Turn, Turn), -Spread, Spread), FMath::Max(FarY - Left, Y - Turn), FMath::Min(FarY + Left, Y + Turn));
			}
			const float Deck = R(450.0f, 700.0f);
			Slab(X - Deck, X + Deck, Y - Deck, Y + Deck, High, 40.0f, EArenaTint::Boardwalk);
			Box(FVector(X, Y, High - 40.0f - 2500.0f), FVector(260.0f, 260.0f, 5000.0f), EArenaTint::Trunk);
			// the hut: a wall on each side of the way through, and a roof
			Box(FVector(X, Y - Deck + 60.0f, High + 140.0f), FVector(Deck * 1.5f, 24.0f, 280.0f), EArenaTint::Cover);
			Box(FVector(X, Y + Deck - 60.0f, High + 140.0f), FVector(Deck * 1.5f, 24.0f, 280.0f), EArenaTint::Cover);
			Box(FVector(X, Y, High + 295.0f), FVector(Deck * 1.7f, Deck * 2.0f, 30.0f), EArenaTint::Boardwalk);
			Nodes.Add(FVector(X, Y, High));
		}

		Slab(Length - Run - 500.0f, Length - Run, FarY - 300.0f, FarY + 300.0f, High, 30.0f, EArenaTint::Boardwalk);
		Nodes.Add(FVector(Length - Run - 250.0f, FarY, High));
		Ramp(Length - Run, High, Length, 0.0f, FarY, 420.0f, EArenaTint::Boardwalk);
		Slab(Length - 60.0f, Length + 400.0f, FarY - 400.0f, FarY + 400.0f, FloorTop, 60.0f, EArenaTint::Boardwalk);

		// planks from each stop to the next, a hair under the platforms they join, on a post
		for (int32 Node = 0; Node + 1 < Nodes.Num(); ++Node) {
			const FVector Span = Nodes[Node + 1] - Nodes[Node];
			const FVector Middle = (Nodes[Node] + Nodes[Node + 1]) * 0.5f;
			Box(FVector(Middle.X, Middle.Y, High - 4.0f - 15.0f), FVector(Span.Size2D(), 260.0f, 30.0f), EArenaTint::Boardwalk, FRotator(0.0f, Span.Rotation().Yaw, 0.0f));
			Box(FVector(Middle.X, Middle.Y, High - 34.0f - 2500.0f), FVector(110.0f, 110.0f, 5000.0f), EArenaTint::Trunk);
		}

		Step(FVector(-100.0f, NearY, 0.0f));
		for (const FVector& Node : Nodes) {
			Step(Node);
		}
		Step(FVector(Length + 100.0f, FarY, 0.0f));
	}

	// A winding floor between two walls of cliff; a ramp at each end of each wall lets it be held from
	// above. The cliffs are scenery over hidden blocks: the blocks are the walls and the ground on top.
	void FBuilder::Canyon()
	{
		const float Half = Width * 0.5f;
		Slab(-400.0f, Length + 400.0f, -Half, Half, FloorTop, 100.0f, EArenaTint::Rock);
		// the cliff is 17.5 m high as it comes; the walls are about that
		const float RimTop = R(1300.0f, 1600.0f);
		const float Mouth = RimTop / FMath::Tan(FMath::DegreesToRadians(27.0f));
		for (const float Rim : { -1.0f, 1.0f }) {
			const float Y = Rim * (Half - 500.0f);
			Ramp(40.0f, 0.0f, Mouth, RimTop, Y, 700.0f, EArenaTint::Rock);
			Ramp(Length - Mouth, RimTop, Length - 40.0f, 0.0f, Y, 700.0f, EArenaTint::Rock);
		}
		Step(FVector(-100.0f, 0.0f, 0.0f));
		const int32 Reaches = FMath::Max(4, FMath::RoundToInt((Length - 2.0f * Mouth) / 1500.0f));
		const float Reach = (Length - 2.0f * Mouth) / Reaches;
		const float Drift = Width * 0.12f;
		float Middle = R(-Drift * 0.5f, Drift * 0.5f);
		for (int32 Each = 0; Each < Reaches; ++Each) {
			// the floor shifts a little from one reach to the next, always leaving a way through
			Middle = FMath::Clamp(Middle + R(-300.0f, 300.0f), -Drift, Drift);
			const float Open = R(650.0f, 950.0f);
			const float X0 = Mouth + Each * Reach;
			const float X1 = X0 + Reach;
			Slab(X0, X1, -Half, Middle - Open, RimTop, RimTop - FloorTop, EArenaTint::Hidden);
			Slab(X0, X1, Middle + Open, Half, RimTop, RimTop - FloorTop, EArenaTint::Hidden);
			const float Against = Side();
			Box(FVector(R(X0 + 200.0f, X1 - 200.0f), Middle + Against * (Open - 160.0f), 100.0f), FVector(R(200.0f, 320.0f), R(180.0f, 280.0f), 200.0f), EArenaTint::Cover);
			Step(FVector((X0 + X1) * 0.5f, Middle - Against * 150.0f, 0.0f));
			// rocky ground along the foot of each wall
			for (const float Wall : { -1.0f, 1.0f }) {
				Prop(EArenaScenery::RockyGround, FVector(R(X0 + 100.0f, X1 - 100.0f), Middle + Wall * (Open - R(40.0f, 200.0f)), FloorTop), FRotator(0.0f, R(0.0f, 360.0f), 0.0f), FVector(R(1.2f, 2.2f)));
			}
		}
		// The cliffs: each is 60.6 m long and 23.7 m deep as it comes. They stand end to end along each
		// wall, near their own size, from past the outer edge in to the nearest the floor comes, a
		// little higher than the ground on top; each shows a different face.
		const float Inner = Drift + 700.0f;
		const float Outer = Half + 200.0f;
		const int32 Cliffs = FMath::Max(1, FMath::RoundToInt((Length - 2.0f * Mouth) / 5600.0f));
		const float Run = (Length - 2.0f * Mouth + 600.0f) / Cliffs;
		for (const float Rim : { -1.0f, 1.0f }) {
			for (int32 Each = 0; Each < Cliffs; ++Each) {
				const float X = Mouth - 300.0f + (Each + 0.5f) * Run;
				Prop(EArenaScenery::Cliff, FVector(X, Rim * (Inner + Outer) * 0.5f, FloorTop), FRotator(0.0f, (Rim > 0.0f ? 180.0f : 0.0f) + (Chance(0.5f) ? 180.0f : 0.0f), 0.0f),
					FVector(Run * 1.08f / 6057.0f, (Outer - Inner) / 2367.0f, (RimTop + 250.0f) / 1718.0f));
			}
		}
		// and rocky ground across the two mouths
		for (int32 Each = 0; Each < 12; ++Each) {
			const float X = Each % 2 == 0 ? R(150.0f, Mouth - 200.0f) : R(Length - Mouth + 200.0f, Length - 150.0f);
			Prop(EArenaScenery::RockyGround, FVector(X, Side() * R(600.0f, Half - 1300.0f), FloorTop), FRotator(0.0f, R(0.0f, 360.0f), 0.0f), FVector(R(1.5f, 3.0f)));
		}
		Step(FVector(Length + 100.0f, 0.0f, 0.0f));
	}

	// A road across the land that hovering craft cross, lane after lane, without stopping.
	void FBuilder::Traffic()
	{
		const float Half = Width * 0.5f;
		Slab(-400.0f, Length + 400.0f, -Half, Half, FloorTop, 100.0f, EArenaTint::Deck);
		Step(FVector(-100.0f, 0.0f, 0.0f));
		const float First = 1500.0f;
		const float Last = Length - 1500.0f;
		const int32 Lanes = FMath::Max(5, FMath::RoundToInt((Last - First) / 1500.0f) + 1);
		const float LaneLength = Width + 2000.0f;
		const bool bFlip = Chance(0.5f);
		for (int32 Lane = 0; Lane < Lanes; ++Lane) {
			const float X = FMath::Lerp(First, Last, Lane / float(Lanes - 1));
			const float Way = ((Lane % 2 == 0) != bFlip) ? 1.0f : -1.0f;
			// fast enough to be hard to cross, with a gap between craft a sprint fits through
			const float Speed = R(800.0f, 1300.0f);
			const int32 Count = FMath::Max(2, FMath::RoundToInt(LaneLength / R(1900.0f, 2700.0f)));
			Slab(X - 175.0f, X - 150.0f, -Half, Half, FloorTop + 3.0f, 3.0f, EArenaTint::Marking);
			Slab(X + 150.0f, X + 175.0f, -Half, Half, FloorTop + 3.0f, 3.0f, EArenaTint::Marking);
			for (int32 Each = 0; Each < Count; ++Each) {
				FArenaCraft Craft;
				Craft.LaneStart = Connector.Frame.TransformPosition(FVector(X, -Way * LaneLength * 0.5f, 115.0f));
				Craft.LaneDirection = Connector.Frame.TransformVectorNoScale(FVector(0.0f, Way, 0.0f));
				Craft.LaneLength = LaneLength;
				Craft.Speed = Speed;
				Craft.Offset = LaneLength * Each / Count + R(0.0f, 150.0f);
				Craft.Size = FVector(210.0f, 430.0f, 130.0f);
				Craft.Rotation = Connector.Frame.GetRotation();
				Craft.Connector = ConnectorIndex;
				Craft.Lane = Lane;
				Plan.Craft.Add(Craft);
			}
			// cover between the lanes, off the straight way across
			if (Lane + 1 < Lanes) {
				const float Between = X + (Last - First) / (Lanes - 1) * 0.5f;
				for (int32 Wall = 0; Wall < 3; ++Wall) {
					Box(FVector(Between, Side() * R(500.0f, Half - 400.0f), 80.0f), FVector(R(120.0f, 300.0f), R(300.0f, 700.0f), 160.0f), EArenaTint::Cover);
				}
			}
			// stop short of the lane, and cross when it is clear
			Step(FVector(X - 320.0f, 0.0f, 0.0f), false, Lane);
			Step(FVector(X + 320.0f, 0.0f, 0.0f));
		}
		Step(FVector(Length + 100.0f, 0.0f, 0.0f));
	}

	// A mass of rock standing on the land with one passage through it, which turns.
	void FBuilder::Tunnel()
	{
		const float Cell = 960.0f;
		const int32 Across = FMath::Max(3, FMath::RoundToInt(Width / Cell));
		const int32 Along = FMath::Max(3, FMath::RoundToInt(Length / Cell));
		const float CellX = Length / Along;
		const float CellY = Width / Across;
		const float Top = 1500.0f;
		const float Clear = 520.0f;
		Slab(-500.0f, Length + 500.0f, -Width * 0.5f, Width * 0.5f, FloorTop, 100.0f, EArenaTint::TunnelRock);

		TArray<bool> Open;
		Open.Init(false, Across * Along);
		const auto CellCentre = [&](int32 Column, int32 Row) { return FVector((Column + 0.5f) * CellX, (Row + 0.5f) * CellY - Width * 0.5f, 0.0f); };
		// the mouths are in the middle rows, in front of the bases
		const int32 Low = Across / 2 - 1;
		const int32 High = Across / 2;
		int32 Row = Rand.RandRange(Low, High);
		Step(CellCentre(0, Row) - FVector(CellX * 0.5f + 150.0f, 0.0f, 0.0f));
		for (int32 Column = 0; Column < Along; ++Column) {
			Open[Column * Across + Row] = true;
			Step(CellCentre(Column, Row));
			const bool bHome = Column >= Along - 1 - FMath::Abs(Row - (Row < Low ? Low : High));
			if (Column >= 1 && Column < Along - 1 && (bHome ? Row < Low || Row > High : Chance(0.45f))) {
				// turn: away as the seed says, or back toward the far mouth when it is near
				const int32 Turned = bHome ? Row + (Row < Low ? 1 : -1) : FMath::Clamp(Row + (Chance(0.5f) ? -1 : 1), 0, Across - 1);
				if (Turned != Row) {
					Row = Turned;
					Open[Column * Across + Row] = true;
					Step(CellCentre(Column, Row));
				}
			}
		}
		Step(CellCentre(Along - 1, Row) + FVector(CellX * 0.5f + 150.0f, 0.0f, 0.0f));

		for (int32 Column = 0; Column < Along; ++Column) {
			for (int32 Each = 0; Each < Across; ++Each) {
				const FVector Centre = CellCentre(Column, Each);
				if (Open[Column * Across + Each]) {
					// the passage's roof
					Box(FVector(Centre.X, Centre.Y, (Clear + Top) * 0.5f), FVector(CellX, CellY, Top - Clear), EArenaTint::TunnelRock);
				}
				else {
					Box(FVector(Centre.X, Centre.Y, (FloorTop + Top) * 0.5f), FVector(CellX, CellY, Top - FloorTop), EArenaTint::TunnelRock);
				}
			}
		}
	}

	// A river gorge with arched bridges over it: always one that is whole, the others a higher arch
	// or one broken at a gap to jump. Every bridge rises from each bank to a crown over the middle of
	// the river and stands on piers that go down to the river's bed.
	void FBuilder::Bridges()
	{
		const float Half = Width * 0.5f;
		Slab(-400.0f, 150.0f, -Half, Half, FloorTop, 60.0f, EArenaTint::Bridge);
		Slab(Length - 150.0f, Length + 400.0f, -Half, Half, FloorTop, 60.0f, EArenaTint::Bridge);
		const int32 Places = FMath::Clamp(FMath::RoundToInt(Width / 2000.0f), 3, 7);
		TArray<float> Lines;
		for (int32 Place = 0; Place < Places; ++Place) {
			Lines.Add(-Half + Width * (Place + 0.5f) / Places);
		}
		for (int32 Each = Lines.Num() - 1; Each > 0; --Each) {
			Lines.Swap(Each, Rand.RandRange(0, Each));
		}
		const int32 Count = Rand.RandRange(FMath::Max(2, Places / 2), Places);
		const float X0 = 150.0f;
		const float X1 = Length - 150.0f;
		const float Middle = (X0 + X1) * 0.5f;
		const float HalfSpan = (X1 - X0) * 0.5f;
		const int32 Pieces = 16;
		for (int32 Each = 0; Each < Count; ++Each) {
			const float Y = Lines[Each];
			const float Wide = R(300.0f, 600.0f);
			// the first is the sure way; the others are one of three sorts
			const int32 Sort = Each == 0 ? 0 : Rand.RandRange(0, 2);
			const float Rise = Sort == 1 ? R(1400.0f, 2000.0f) : R(700.0f, 1100.0f);
			const float Break = R(Length * 0.35f, Length * 0.65f);
			const float Gap = Sort == 2 ? R(190.0f, 250.0f) : 0.0f;
			// how high the deck is at a place along it: an arch, level with the banks at its ends
			const auto DeckAt = [Middle, HalfSpan, Rise](float X) {
				const float Out = (X - Middle) / HalfSpan;
				return Rise * (1.0f - Out * Out);
			};
			const auto Deck = [this, &DeckAt, Y, Wide](float From, float To) {
				if (To - From > 30.0f) {
					Ramp(From, DeckAt(From), To, DeckAt(To), Y, Wide, EArenaTint::Bridge, 40.0f);
				}
			};
			for (int32 Piece = 0; Piece < Pieces; ++Piece) {
				const float From = FMath::Lerp(X0, X1, Piece / float(Pieces));
				const float To = FMath::Lerp(X0, X1, (Piece + 1) / float(Pieces));
				if (Gap <= 0.0f || To <= Break - Gap * 0.5f || From >= Break + Gap * 0.5f) {
					Deck(From, To);
				}
				else {
					// the broken one: its deck stops each side of the gap
					Deck(From, Break - Gap * 0.5f);
					Deck(Break + Gap * 0.5f, To);
				}
			}
			// piers under it, down to the bed of the river
			for (const float Out : { -0.6f, -0.2f, 0.2f, 0.6f }) {
				const float X = Middle + Out * HalfSpan;
				if (Gap <= 0.0f || FMath::Abs(X - Break) > Gap * 0.5f + 150.0f) {
					const float Top = DeckAt(X) - 40.0f;
					Box(FVector(X, Y, (Top + Bed) * 0.5f), FVector(160.0f, Wide * 0.6f, Top - Bed), EArenaTint::Bridge);
				}
			}
			if (Sort != 1) {
				// low walls down alternate sides: something to stand behind
				int32 Post = 0;
				for (float X = X0 + 900.0f; X < X1 - 600.0f; X += 1300.0f, ++Post) {
					const float Long = R(120.0f, 260.0f);
					Box(FVector(X, Y + (Post % 2 == 0 ? 1.0f : -1.0f) * (Wide * 0.5f - 15.0f), DeckAt(X) + 60.0f), FVector(Long, 30.0f, 120.0f), EArenaTint::Cover);
				}
			}
			if (Each == 0) {
				Step(FVector(-100.0f, Y, 0.0f));
				Step(FVector(Middle, Y, Rise));
				Step(FVector(Length + 100.0f, Y, 0.0f));
			}
		}
	}

	double Smooth(double Low, double High, double Value)
	{
		return FMath::SmoothStep(Low, High, Value);
	}

	/** Where a place is in a connector's strip, which runs from one plot's edge to the other's and is the connector's width across; and how far outside it. */
	struct FStripPlace
	{
		double Along = 0.0;
		double Across = 0.0;
		double Outside = 0.0;
	};

	FStripPlace InStrip(const FArenaPlan& Plan, const FArenaConnector& Connector, double X, double Y)
	{
		const FVector Local = Connector.Frame.InverseTransformPosition(FVector(X, Y, 0.0));
		const double Neck = Plan.Reach - Plan.Size.BaseRadius + 400.0;
		const double OutAlong = FMath::Max3(-Neck - Local.X, Local.X - (Plan.Size.ConnectorLength + Neck), 0.0);
		const double OutAcross = FMath::Max(FMath::Abs(Local.Y) - Plan.Size.ConnectorWidth * 0.5, 0.0);
		FStripPlace Place;
		Place.Along = Local.X;
		Place.Across = Local.Y;
		Place.Outside = FMath::Sqrt(OutAlong * OutAlong + OutAcross * OutAcross);
		return Place;
	}

	/** A way over a gorge: the land stops at each end of it and the ground between goes down under the water. */
	bool IsGorge(EConnectorKind Kind)
	{
		return Kind == EConnectorKind::Floating || Kind == EConnectorKind::Bridges;
	}

	/** How far along a way over a gorge the land of a mouth reaches, from that end: past what the connector lays there to stand on. */
	double MouthOf(EConnectorKind Kind, bool bFar)
	{
		// A river has banks: 50 m of level ground under each end of its bridges, then the slope to the
		// water. The gorge the floating pieces hang over is the whole way across, but for where they
		// are stood on: the far landing may begin as much as 18.7 m short of the far plot.
		if (Kind == EConnectorKind::Bridges) {
			return 5000.0;
		}
		return bFar ? 2300.0 : 600.0;
	}

	/** How far the hills take to rise from level ground: long enough that the rise is a slope to walk, not a wall. */
	constexpr double LevelEase = 4500.0;

	/** How far under a plot's level ground the bed of the lake, and of a gorge, lies. */
	double BedOf(const FArenaSizes& Size)
	{
		return -(Size.WaterDepth + 800.0);
	}

	/** A thing the plots' dressing stood up, for what comes after to keep clear of: where, how far it reaches, and how far a tree's trunk keeps from it. */
	struct FTaken
	{
		FVector2D At = FVector2D::ZeroVector;
		double Reach = 0.0;
		double TreeRoom = 400.0;
	};

	/** The rocks a plot's cover is made of, by its style (FArenaPlan::Styles): blocky, slabs, rubble. */
	constexpr EArenaScenery TallRocks[3][2] = {
		{ EArenaScenery::CoverRock02, EArenaScenery::CoverRock04 },
		{ EArenaScenery::CoverRock06, EArenaScenery::CoverRock07 },
		{ EArenaScenery::CoverRock07, EArenaScenery::CoverRock04 },
	};
	constexpr EArenaScenery LowRocks[3][2] = {
		{ EArenaScenery::CoverRock08, EArenaScenery::CoverRock08 },
		{ EArenaScenery::CoverRock05, EArenaScenery::CoverRock05 },
		{ EArenaScenery::CoverRock05, EArenaScenery::CoverRock08 },
	};
	constexpr EArenaScenery SmallStones[3] = { EArenaScenery::Stone10, EArenaScenery::Stone01, EArenaScenery::Stone12 };
	/** How dry each style's grass is, about: the blocky plots lush, the slabs dry, the rubble between. */
	constexpr float StyleDryness[3] = { 0.12f, 0.80f, 0.45f };

	/**
	 * The steel frame (Construct.obj, imported as SM_Frame, measured in it): a slab 12.7 cm thick;
	 * twelve columns 20.3 cm square on a grid of three across its 8.9 m side and four along its 13.2 m
	 * side, from the slab to beams that run along every row and column of them, 3.63 m to 3.83 m up.
	 * The 25 cm footings round the columns' feet are under a step and have no block of their own.
	 */
	constexpr float FrameColumnsX[3] = { -426.72f, 0.0f, 426.72f };
	constexpr float FrameColumnsY[4] = { -640.08f, -213.36f, 213.36f, 640.08f };
	constexpr float FrameSteel = 20.32f;
	constexpr float FrameSlab = 12.7f;
	constexpr float FrameBeamUnder = 362.79f;
	constexpr float FrameBeamTop = 383.11f;

	/**
	 * Heights of the pieces, in cm. Tall cover is sized by how high its top stands over the lowest ground
	 * at its foot once it is sunk: a hero is 1.92 m tall and cannot crouch, so 2.05 m and more hides one
	 * wholly. Low rocks stand about waist high: something to shoot over and partial cover, not a place to
	 * hide. Crags and outcrops are sized by their whole height; stones are stepped over.
	 */
	constexpr double TallClearLow = 205.0, TallClearHigh = 250.0;
	constexpr double LowLow = 100.0, LowHigh = 120.0;
	constexpr double CragLow = 1000.0, CragHigh = 1300.0;
	constexpr double OutcropLow = 400.0, OutcropHigh = 550.0;
	constexpr double StoneLow = 28.0, StoneHigh = 45.0;
	/** How much of its height a rock is sunk into the ground under its lowest point: rocks are rounded underneath. */
	constexpr double RockSink = 0.15;
	constexpr double CragSink = 0.12;

	/** A tall rock's height for its top to stand Clear over the lowest ground at its foot. */
	double TallFor(double Clear)
	{
		return Clear / (1.0 - RockSink);
	}

	/** How far from a straight way a hero walks across a plot anything solid keeps; how far from where a match may start heroes, or stand a crate. */
	constexpr double WalkedRoom = 900.0;
	constexpr double StartRoom = 300.0;
	/** Past this far from a plot's middle, inside a way out, is where heroes come off a connector: nothing solid there. */
	constexpr double ApronFrom = 9600.0;

	/** Where the ground's mesh lies: its first point, and how many points across and down, a point every GroundCell. */
	struct FGroundGrid
	{
		FVector2D Corner = FVector2D::ZeroVector;
		int32 Across = 0;
		int32 Down = 0;
	};

	/** The ground mesh's grid for a plan: over every plot and connector, past the outline as far as the coast wanders, then the shore, and a cell more. */
	FGroundGrid GridOf(const FArenaPlan& Plan)
	{
		const FArenaSizes& Size = Plan.Size;
		FBox2D Reaches(ForceInit);
		for (const FVector& Base : Plan.Bases) {
			Reaches += FVector2D(Base) - FVector2D(Size.BaseRadius);
			Reaches += FVector2D(Base) + FVector2D(Size.BaseRadius);
		}
		for (const FArenaConnector& Connector : Plan.Connectors) {
			const double Neck = Plan.Reach - Size.BaseRadius + 400.0;
			for (const double Along : { -Neck, Size.ConnectorLength + Neck }) {
				for (const double Across : { -Size.ConnectorWidth * 0.5, Size.ConnectorWidth * 0.5 }) {
					Reaches += FVector2D(Connector.Frame.TransformPosition(FVector(Along, Across, 0.0)));
				}
			}
		}
		const double Past = ASenniteArena::CoastReach + ASenniteArena::ShoreWidth + ASenniteArena::GroundCell;
		FGroundGrid Grid;
		Grid.Corner = Reaches.Min - FVector2D(Past);
		Grid.Across = FMath::CeilToInt32((Reaches.Max.X - Reaches.Min.X + 2.0 * Past) / ASenniteArena::GroundCell) + 1;
		Grid.Down = FMath::CeilToInt32((Reaches.Max.Y - Reaches.Min.Y + 2.0 * Past) / ASenniteArena::GroundCell) + 1;
		return Grid;
	}

	/**
	 * The ground as the arena draws it and heroes stand on it: GroundHeight at the points of its mesh, a
	 * point every GroundCell, and flat triangles between, wound as Build winds them. On a crease it lies
	 * below GroundHeight by as much as half a metre, so what stands on the ground is set by this. The
	 * height at each point of the mesh is worked out once.
	 */
	struct FDrawnGround
	{
		const FArenaPlan& Plan;
		FGroundGrid Grid;
		TMap<FIntPoint, double> Known;

		explicit FDrawnGround(const FArenaPlan& InPlan) : Plan(InPlan), Grid(GridOf(InPlan)) {}

		double Point(int32 Column, int32 Row)
		{
			if (const double* Height = Known.Find(FIntPoint(Column, Row))) {
				return *Height;
			}
			const double Height = ASenniteArena::GroundHeight(Plan, Grid.Corner.X + Column * ASenniteArena::GroundCell, Grid.Corner.Y + Row * ASenniteArena::GroundCell);
			Known.Add(FIntPoint(Column, Row), Height);
			return Height;
		}

		double Height(double X, double Y)
		{
			const double U = (X - Grid.Corner.X) / ASenniteArena::GroundCell;
			const double V = (Y - Grid.Corner.Y) / ASenniteArena::GroundCell;
			const int32 Column = FMath::Clamp(FMath::FloorToInt32(U), 0, Grid.Across - 2);
			const int32 Row = FMath::Clamp(FMath::FloorToInt32(V), 0, Grid.Down - 2);
			const double InX = U - Column;
			const double InY = V - Row;
			// a cell's two triangles: (A, B, D) where InX + InY <= 1, else (B, C, D); A is the cell's first point, B a row on, D a column on
			if (InX + InY <= 1.0) {
				const double A = Point(Column, Row);
				return A + InX * (Point(Column + 1, Row) - A) + InY * (Point(Column, Row + 1) - A);
			}
			const double C = Point(Column + 1, Row + 1);
			return C + (1.0 - InX) * (Point(Column, Row + 1) - C) + (1.0 - InY) * (Point(Column + 1, Row) - C);
		}
	};

	FVector2D Toward(double Degrees)
	{
		return FVector2D(FMath::Cos(FMath::DegreesToRadians(Degrees)), FMath::Sin(FMath::DegreesToRadians(Degrees)));
	}

	double FromLine(const FVector2D& At, const FVector2D& From, const FVector2D& To)
	{
		const FVector2D Along = To - From;
		const double Long = Along.SizeSquared();
		const double Part = Long > 0.0 ? FMath::Clamp(FVector2D::DotProduct(At - From, Along) / Long, 0.0, 1.0) : 0.0;
		return FVector2D::Distance(At, From + Along * Part);
	}

	/**
	 * The straight ways a hero walks across a plot: from its middle to where a way across begins, and
	 * from where one ends to the place on the plot it ends at, then on to the middle (the arena and
	 * match checks walk exactly these). Nothing solid stands within WalkedRoom of them.
	 */
	TArray<TPair<FVector2D, FVector2D>> WalkedLines(const FArenaPlan& Plan, int32 Base)
	{
		TArray<TPair<FVector2D, FVector2D>> Lines;
		const FVector2D Middle(Plan.Bases[Base]);
		for (const FArenaConnector& Connector : Plan.Connectors) {
			const int32 Steps = Connector.Route.Num();
			if (Steps < 2) {
				continue;
			}
			if (Connector.FromBase == Base) {
				Lines.Emplace(Middle, FVector2D(Connector.Route[0].Point));
			}
			if (Connector.ToBase == Base) {
				const FVector2D Home(Connector.Route[Steps - 1].Point);
				Lines.Emplace(FVector2D(Connector.Route[Steps - 2].Point), Home);
				Lines.Emplace(Home, Middle);
			}
		}
		return Lines;
	}

	/**
	 * Where a match may start a plot's heroes when its buildings cover the middle (ASenniteMatch: rings
	 * 600 cm apart on eight spokes, the first toward the arena's middle) and where the crates by such a
	 * start stand (ASenniteCrate::Stock: FromStart ahead of the heroes, PerBase of them spread 70 degrees
	 * apart). Those past the pad are kept clear of anything solid, so a hero is never started in a rock.
	 */
	TArray<FVector2D> StartSpots(const FArenaPlan& Plan, int32 Base)
	{
		const ASenniteCrate* Crates = GetDefault<ASenniteCrate>();
		FVector Middle = FVector::ZeroVector;
		for (const FVector& Each : Plan.Bases) {
			Middle += Each / Plan.Bases.Num();
		}
		const FVector Centre = Plan.Bases[Base];
		const FVector Out = (Middle - Centre).GetSafeNormal2D();
		TArray<FVector2D> Spots;
		for (int32 Ring = 1; Ring <= 12; ++Ring) {
			for (int32 Turn = 0; Turn < 8; ++Turn) {
				const FVector At = Centre + Out.RotateAngleAxis(Turn * 45.0, FVector::UpVector) * (Ring * 600.0);
				for (int32 Each = -1; Each < Crates->PerBase; ++Each) {
					// first the start itself, then each of its crates
					const FVector Spot = Each < 0 ? At : At + Out.RotateAngleAxis((Each - (Crates->PerBase - 1) * 0.5) * 70.0, FVector::UpVector) * Crates->FromStart;
					if (FVector::Dist2D(Spot, Centre) > Plan.Size.PadRadius - 600.0) {
						Spots.Add(FVector2D(Spot));
					}
				}
			}
		}
		return Spots;
	}

	/** One plot's dressing, from a random stream of its own. */
	struct FPlotDresser
	{
		FArenaPlan& Plan;
		const FArenaSizes& Size;
		int32 Base;
		TArray<FTaken>& Taken;
		FRandomStream Rand;
		FVector2D Middle;
		/** Toward each plot this one is joined to. */
		TArray<FVector2D> Ways;
		TArray<TPair<FVector2D, FVector2D>> Walked;
		TArray<FVector2D> Spots;
		int32 Style = 0;
		/** What of Taken stands on this plot. */
		TArray<int32> Mine;
		/** The ground as drawn, which what stands is set on. */
		FDrawnGround& Drawn;
		int32 CragAt = INDEX_NONE;
		int32 FrameAt = INDEX_NONE;

		FPlotDresser(FArenaPlan& InPlan, const FArenaSizes& InSize, int32 InBase, TArray<FTaken>& InTaken, FDrawnGround& InDrawn)
			: Plan(InPlan), Size(InSize), Base(InBase), Taken(InTaken),
			Rand(static_cast<int32>(HashCombine(GetTypeHash(InPlan.Seed ^ 0x44524553), GetTypeHash(InBase)))),
			Middle(InPlan.Bases[InBase]), Walked(WalkedLines(InPlan, InBase)), Spots(StartSpots(InPlan, InBase)), Style(InPlan.Styles[InBase]), Drawn(InDrawn)
		{
			for (const FArenaConnector& Connector : Plan.Connectors) {
				if (Connector.FromBase == Base || Connector.ToBase == Base) {
					Ways.Add((FVector2D(Plan.Bases[Connector.FromBase == Base ? Connector.ToBase : Connector.FromBase]) - Middle).GetSafeNormal());
				}
			}
		}

		double R(double Low, double High) { return Rand.FRandRange(Low, High); }

		/** In a way out: in the strip, a connector wide and Margin more each side, that runs out from the middle toward a joined plot. */
		bool InWay(const FVector2D& At, double Margin) const
		{
			const FVector2D Out = At - Middle;
			for (const FVector2D& Way : Ways) {
				if (FVector2D::DotProduct(Out, Way) > 0.0 && FMath::Abs(FVector2D::CrossProduct(Way, Out)) < Size.ConnectorWidth * 0.5 + Margin) {
					return true;
				}
			}
			return false;
		}

		/** Room for something solid reaching this far: off the pad and its edge, clear of the straight ways walked, of where a match may start heroes, of the aprons where heroes come off a connector, and of what stands already. */
		bool Room(const FVector2D& At, double Reach, double Gap) const
		{
			const double Out = FVector2D::Distance(At, Middle);
			if (Out - Reach < Size.PadRadius + 800.0 || Out + Reach > Size.BaseRadius - 400.0) {
				return false;
			}
			if (Out + Reach > ApronFrom && InWay(At, Reach)) {
				return false;
			}
			for (const TPair<FVector2D, FVector2D>& Line : Walked) {
				if (FromLine(At, Line.Key, Line.Value) < WalkedRoom + Reach) {
					return false;
				}
			}
			for (const FVector2D& Spot : Spots) {
				if (FVector2D::Distance(At, Spot) < StartRoom + Reach) {
					return false;
				}
			}
			for (const FTaken& Other : Taken) {
				if (FVector2D::Distance(At, Other.At) < Other.Reach + Reach + Gap) {
					return false;
				}
			}
			return true;
		}

		/** The drawn ground under a footprint turned Yaw, at its middle, its corners and the middles of its sides: the lowest and highest of them. */
		TPair<double, double> Footing(const FVector2D& At, double Yaw, double X, double Y)
		{
			double Low = TNumericLimits<double>::Max();
			double High = -TNumericLimits<double>::Max();
			for (const double SideX : { -0.5, 0.0, 0.5 }) {
				for (const double SideY : { -0.5, 0.0, 0.5 }) {
					const FVector2D Point = At + FVector2D(FRotator(0.0, Yaw, 0.0).RotateVector(FVector(SideX * X, SideY * Y, 0.0)));
					const double Here = Drawn.Height(Point.X, Point.Y);
					Low = FMath::Min(Low, Here);
					High = FMath::Max(High, Here);
				}
			}
			return TPair<double, double>(Low, High);
		}

		/** The way a piece turns so its long side lies across a line running the way Facing points. */
		static double Across(EArenaScenery Kind, double Facing)
		{
			const FVector Shape = ASenniteArena::ShapeOf(Kind);
			return Shape.X > Shape.Y ? Facing + 90.0 : Facing;
		}

		/**
		 * A rock of Kind, Height tall in its own proportions, turned Yaw, sunk Sink into the drawn ground
		 * under its lowest point; if there is room and the ground under it is not steeper than it is tall.
		 * A rock is solid by its own shape: its set collides as it is drawn (Build). Its index in Taken,
		 * or none.
		 */
		int32 Rock(EArenaScenery Kind, const FVector2D& At, double Yaw, double Height, double Sink, double TreeRoom)
		{
			const FVector Shape = ASenniteArena::ShapeOf(Kind);
			const FVector Box = Shape * (Height / Shape.Z);
			const double Reach = 0.5 * FVector2D(Box.X, Box.Y).Size();
			if (!Room(At, Reach, 250.0)) {
				return INDEX_NONE;
			}
			const TPair<double, double> Under = Footing(At, Yaw, Box.X, Box.Y);
			if (Under.Value - Under.Key > Height * 0.6 || Under.Key < -300.0) {
				return INDEX_NONE;
			}
			const double Foot = Under.Key - Sink;
			FArenaProp Piece;
			Piece.Scenery = Kind;
			Piece.Transform = FTransform(FRotator(0.0, Yaw, 0.0), FVector(At.X, At.Y, Foot + Box.Z * 0.5), Box);
			Piece.bFit = true;
			Plan.Props.Add(Piece);
			FTaken Stood;
			Stood.At = At;
			Stood.Reach = Reach;
			Stood.TreeRoom = TreeRoom;
			Mine.Add(Taken.Add(Stood));
			return Mine.Last();
		}

		/** Something seen and not met, of Kind, filling Box about a middle at At, turned Turn. */
		void Seen(EArenaScenery Kind, const FVector& At, const FQuat& Turn, const FVector& Box)
		{
			FArenaProp Piece;
			Piece.Scenery = Kind;
			Piece.Transform = FTransform(Turn, At, Box);
			Piece.bFit = true;
			Plan.Props.Add(Piece);
		}

		/** Groups of cover off the ways out: a tall rock with its long side across the line to the pad, and one or two more beside it, one each side, with room for a hero between. */
		void Cover()
		{
			TArray<FVector2D> Groups;
			for (int32 Group = 0; Group < Size.CoverPerBase; ++Group) {
				for (int32 Try = 0; Try < 40; ++Try) {
					const double Turn = R(0.0, 360.0);
					const FVector2D At = Middle + Toward(Turn) * R(Size.PadRadius + 1200.0, Size.BaseRadius - 1000.0);
					if (InWay(At, 600.0) || Groups.ContainsByPredicate([&At](const FVector2D& Other) { return FVector2D::Distance(At, Other) < 2200.0; })) {
						continue;
					}
					const EArenaScenery Kind = TallRocks[Style][Rand.RandRange(0, 1)];
					const double Yaw = Across(Kind, Turn) + R(-20.0, 20.0);
					const double Height = TallFor(R(TallClearLow, TallClearHigh));
					if (Rock(Kind, At, Yaw, Height, Height * RockSink, 400.0) == INDEX_NONE) {
						continue;
					}
					Groups.Add(At);
					const FVector Shape = ASenniteArena::ShapeOf(Kind);
					const FVector2D Long = Shape.X > Shape.Y ? Toward(Yaw) : Toward(Yaw + 90.0);
					const double Reach = 0.5 * FVector2D(Shape.X, Shape.Y).Size() * Height / Shape.Z;
					const int32 Beside = Rand.RandRange(1, 2);
					// the first beside it on a side the seed picks, a second on the other side
					const double FirstSide = Rand.FRand() < 0.5f ? -1.0 : 1.0;
					for (int32 Each = 0; Each < Beside; ++Each) {
						const bool bTall = Rand.FRand() < 0.3f;
						const EArenaScenery Other = bTall ? TallRocks[Style][Rand.RandRange(0, 1)] : LowRocks[Style][Rand.RandRange(0, 1)];
						const double OtherHeight = bTall ? TallFor(R(TallClearLow, TallClearHigh)) : R(LowLow, LowHigh);
						const FVector OtherShape = ASenniteArena::ShapeOf(Other);
						const double OtherReach = 0.5 * FVector2D(OtherShape.X, OtherShape.Y).Size() * OtherHeight / OtherShape.Z;
						// drawn one at a time, in this order: inside one expression the order would be the compiler's to choose
						const double Gap = R(250.0, 350.0);
						const double Step = R(-200.0, 200.0);
						const FVector2D Next = At + Long * (Each == 0 ? FirstSide : -FirstSide) * (Reach + Gap + OtherReach) + Toward(Turn) * Step;
						Rock(Other, Next, Across(Other, Turn) + R(-25.0, 25.0), OtherHeight, OtherHeight * RockSink, 400.0);
					}
					break;
				}
			}
		}

		/** Along each way out, rocks a hero hides behind every 10 m or so, either side in turn, beside the straight way to the connector: the open run to the pad has cover. */
		void WayCover()
		{
			for (const FVector2D& Way : Ways) {
				const double Facing = FMath::RadiansToDegrees(FMath::Atan2(Way.Y, Way.X));
				const FVector2D Side = FVector2D(-Way.Y, Way.X);
				const double First = Rand.FRand() < 0.5 ? -1.0 : 1.0;
				for (int32 Each = 0; Each < Size.CoverPerWay; ++Each) {
					const double Out = FMath::Lerp(Size.PadRadius + 1400.0, ApronFrom - 400.0, Size.CoverPerWay > 1 ? Each / double(Size.CoverPerWay - 1) : 0.0) + R(-300.0, 300.0);
					const EArenaScenery Kind = TallRocks[Style][Rand.RandRange(0, 1)];
					const double Height = TallFor(R(TallClearLow, TallClearHigh));
					const double Yaw = Across(Kind, Facing) + R(-15.0, 15.0);
					const double Prefer = Each % 2 == 0 ? First : -First;
					bool bStood = false;
					for (double Lateral = 1400.0; Lateral <= 5000.0 && !bStood; Lateral += 400.0) {
						for (const double Which : { Prefer, -Prefer }) {
							if (Rock(Kind, Middle + Way * Out + Side * (Which * Lateral), Yaw, Height, Height * RockSink, 400.0) != INDEX_NONE) {
								bStood = true;
								break;
							}
						}
					}
				}
			}
		}

		/** Outcrops: big rocks, two heroes tall and more, on the plot's land toward its rim, off its ways: they break the long views across it. */
		void Outcrops()
		{
			for (int32 Each = 0; Each < Size.OutcropsPerBase; ++Each) {
				for (int32 Try = 0; Try < 40; ++Try) {
					const double Turn = R(0.0, 360.0);
					const FVector2D At = Middle + Toward(Turn) * R(7800.0, Size.BaseRadius - 1200.0);
					if (InWay(At, 600.0)) {
						continue;
					}
					const EArenaScenery Kind = TallRocks[Style][Rand.RandRange(0, 1)];
					const double Height = R(OutcropLow, OutcropHigh);
					if (Rock(Kind, At, Across(Kind, Turn) + R(-30.0, 30.0), Height, Height * RockSink, 600.0) != INDEX_NONE) {
						break;
					}
				}
			}
		}

		/** A crag, the plot's landmark: on the highest ground at the plot's back, 60 degrees or more from every way out, its long side across the line to the pad. */
		void Crag()
		{
			for (int32 Each = 0; Each < Size.CragsPerBase; ++Each) {
				const EArenaScenery Kind = Rand.FRand() < 0.5f ? EArenaScenery::CragRock03 : EArenaScenery::CragRock11;
				const double Height = R(CragLow, CragHigh);
				const double Start = R(0.0, 360.0);
				double Best = -TNumericLimits<double>::Max();
				FVector2D BestAt = Middle;
				double BestTurn = 0.0;
				for (int32 Step = 0; Step < 72; ++Step) {
					const double Turn = Start + Step * 5.0;
					const FVector2D Out = Toward(Turn);
					if (Ways.ContainsByPredicate([&Out](const FVector2D& Way) { return FVector2D::DotProduct(Out, Way) > 0.5; })) {
						continue;
					}
					for (const double Far : { 8800.0, 9600.0, 10400.0 }) {
						const FVector2D At = Middle + Out * Far;
						const FVector Box = ASenniteArena::ShapeOf(Kind) * (Height / ASenniteArena::ShapeOf(Kind).Z);
						const double Reach = 0.5 * FVector2D(Box.X, Box.Y).Size();
						const double High = ASenniteArena::GroundHeight(Plan, At.X, At.Y);
						if (High > Best && !InWay(At, Reach + 600.0) && Room(At, Reach, 600.0) && Footing(At, Across(Kind, Turn), Box.X, Box.Y).Key > -300.0) {
							Best = High;
							BestAt = At;
							BestTurn = Turn;
						}
					}
				}
				if (Best > -TNumericLimits<double>::Max()) {
					const int32 Stood = Rock(Kind, BestAt, Across(Kind, BestTurn) + R(-10.0, 10.0), Height, Height * CragSink, 2000.0);
					CragAt = Stood != INDEX_NONE ? Stood : CragAt;
				}
			}
		}

		/** The steel frame: on the flattest ground found on the plot's land past its pad, its long side across the line to the pad, over blocks for its slab, columns and beams. */
		void Frame()
		{
			const FVector Shape = ASenniteArena::ShapeOf(EArenaScenery::SteelFrame);
			const double Reach = 0.5 * FVector2D(Shape.X, Shape.Y).Size();
			for (int32 Each = 0; Each < Size.FramesPerBase; ++Each) {
				const double Start = R(0.0, 360.0);
				// no more than this between the ground under its lowest and highest corners: its slab is set at the lowest
				double Flattest = 160.0;
				FVector2D BestAt = Middle;
				double BestYaw = 0.0;
				double BestFoot = 0.0;
				for (int32 Step = 0; Step < 72; ++Step) {
					const double Turn = Start + Step * 5.0;
					for (const double Far : { 6400.0, 7200.0, 8000.0, 8800.0, 9600.0, 10400.0 }) {
						const FVector2D At = Middle + Toward(Turn) * Far;
						if (InWay(At, Reach + 600.0) || !Room(At, Reach, 600.0)) {
							continue;
						}
						const TPair<double, double> Under = Footing(At, Turn, Shape.X, Shape.Y);
						if (Under.Value - Under.Key < Flattest && Under.Key > -300.0) {
							Flattest = Under.Value - Under.Key;
							BestAt = At;
							BestYaw = Turn;
							BestFoot = Under.Key;
						}
					}
				}
				if (BestAt == Middle) {
					continue;
				}
				// its slab under its lowest corner: on a slope its uphill side is in the ground
				const FRotator Turned(0.0, BestYaw, 0.0);
				const FVector Foot(BestAt.X, BestAt.Y, BestFoot);
				FArenaProp Piece;
				Piece.Scenery = EArenaScenery::SteelFrame;
				Piece.Transform = FTransform(Turned, Foot, FVector::OneVector);
				Plan.Props.Add(Piece);
				const auto Block = [this, &Turned, &Foot](const FVector& Offset, const FVector& BlockSize) {
					FArenaBox Steel;
					Steel.Centre = Foot + Turned.RotateVector(Offset);
					Steel.Size = BlockSize;
					Steel.Rotation = Turned.Quaternion();
					Steel.Tint = EArenaTint::Hidden;
					Plan.Boxes.Add(Steel);
				};
				Block(FVector(0.0, 0.0, FrameSlab * 0.5), FVector(Shape.X, Shape.Y, FrameSlab));
				for (const float X : FrameColumnsX) {
					for (const float Y : FrameColumnsY) {
						Block(FVector(X, Y, (FrameSlab + FrameBeamUnder) * 0.5), FVector(FrameSteel, FrameSteel, FrameBeamUnder - FrameSlab));
					}
				}
				// the beams, each from the outside of its end columns to the outside of the other
				for (const float X : FrameColumnsX) {
					Block(FVector(X, 0.0, (FrameBeamUnder + FrameBeamTop) * 0.5), FVector(FrameSteel, FrameColumnsY[3] * 2.0 + FrameSteel, FrameBeamTop - FrameBeamUnder));
				}
				for (const float Y : FrameColumnsY) {
					Block(FVector(0.0, Y, (FrameBeamUnder + FrameBeamTop) * 0.5), FVector(FrameColumnsX[2] * 2.0 + FrameSteel, FrameSteel, FrameBeamTop - FrameBeamUnder));
				}
				FTaken Stood;
				Stood.At = BestAt;
				Stood.Reach = Reach;
				FrameAt = Taken.Add(Stood);
				Mine.Add(FrameAt);
			}
		}

		/** Small stones at the feet of the rocks, crag and frame, a third of each in the drawn ground under its lowest point: seen, stepped over, never met. */
		void Stones()
		{
			for (int32 Each = 0; Each < Size.StonesPerBase && Mine.Num() > 0; ++Each) {
				const FTaken& Near = Taken[Mine[Rand.RandRange(0, Mine.Num() - 1)]];
				const double Turn = R(0.0, 360.0);
				const double Off = R(60.0, 260.0);
				const FVector2D At = Near.At + Toward(Turn) * (Near.Reach + Off);
				const EArenaScenery Kind = Rand.FRand() < 0.75f ? SmallStones[Style] : SmallStones[Rand.RandRange(0, 2)];
				const double Height = R(StoneLow, StoneHigh);
				const double Spin = R(0.0, 360.0);
				const FVector Shape = ASenniteArena::ShapeOf(Kind);
				const FVector Box = Shape * (Height / Shape.Z);
				if (FVector2D::Distance(At, Middle) < Size.PadRadius + 300.0) {
					continue;
				}
				const TPair<double, double> Under = Footing(At, Spin, Box.X, Box.Y);
				if (Under.Key < -300.0) {
					continue;
				}
				Seen(Kind, FVector(At.X, At.Y, Under.Key - Box.Z * 0.3 + Box.Z * 0.5), FRotator(0.0, Spin, 0.0).Quaternion(), Box);
			}
		}

		/**
		 * Patches of stony ground, laid on the slope of the drawn ground where they lie, their rim at or
		 * a little under it all round: two at the crag's foot, one by the frame (past its slab, which it
		 * would show through), the rest by cover. None where the ground bends under one more than half
		 * its height.
		 */
		void Patches()
		{
			const FVector Shape = ASenniteArena::ShapeOf(EArenaScenery::StonyGround);
			for (int32 Each = 0; Each < Size.PatchesPerBase && Mine.Num() > 0; ++Each) {
				const int32 Which = Each < 2 && CragAt != INDEX_NONE ? CragAt : (Each == 2 && FrameAt != INDEX_NONE ? FrameAt : Mine[Rand.RandRange(0, Mine.Num() - 1)]);
				const FTaken& Near = Taken[Which];
				// drawn one at a time, in this order
				const double Turn = R(0.0, 360.0);
				const double Part = R(0.5, 1.0);
				const double Off = R(100.0, 300.0);
				const double Scale = R(0.9, 1.4);
				const double Spin = R(0.0, 360.0);
				const FVector Box = Shape * Scale;
				const double Reach = 0.5 * FVector2D(Box.X, Box.Y).Size();
				const FVector2D At = Near.At + Toward(Turn) * (Which == FrameAt ? Near.Reach + Reach + Off : Near.Reach * Part + Off);
				if (FVector2D::Distance(At, Middle) < Size.PadRadius + Reach
					|| (FrameAt != INDEX_NONE && FVector2D::Distance(At, Taken[FrameAt].At) < Taken[FrameAt].Reach + Reach)) {
					continue;
				}
				const double Ground = Drawn.Height(At.X, At.Y);
				if (Ground < -300.0) {
					continue;
				}
				const FVector Slope(Drawn.Height(At.X - 200.0, At.Y) - Drawn.Height(At.X + 200.0, At.Y), Drawn.Height(At.X, At.Y - 200.0) - Drawn.Height(At.X, At.Y + 200.0), 400.0);
				const FQuat Lay = FQuat::FindBetweenNormals(FVector::UpVector, Slope.GetSafeNormal()) * FRotator(0.0, Spin, 0.0).Quaternion();
				FVector Centre(At.X, At.Y, Ground + Box.Z * 0.5);
				// how far its rim stands over the drawn ground, most and least, round it
				double Over = -TNumericLimits<double>::Max();
				double Under = TNumericLimits<double>::Max();
				for (const double SideX : { -0.5, 0.0, 0.5 }) {
					for (const double SideY : { -0.5, 0.0, 0.5 }) {
						const FVector Rim = Centre + Lay.RotateVector(FVector(SideX * Box.X, SideY * Box.Y, -Box.Z * 0.5));
						const double Gap = Rim.Z - Drawn.Height(Rim.X, Rim.Y);
						Over = FMath::Max(Over, Gap);
						Under = FMath::Min(Under, Gap);
					}
				}
				if (Over - Under > Box.Z * 0.5) {
					continue;
				}
				Centre.Z -= Over + 5.0;
				Seen(EArenaScenery::StonyGround, Centre, Lay, Box);
			}
		}

		void Dress()
		{
			// the landmark first, on the high ground, then the frame, then cover: each finds room among what stands
			Crag();
			Frame();
			Outcrops();
			WayCover();
			Cover();
			Stones();
			Patches();
		}
	};

	/**
	 * The plots' dressing: each plot's style, then each plot dressed from a random stream of its own, so
	 * the rest of the arena is what its seed makes with none. What it stood up, for the trees and bushes
	 * to keep clear of.
	 */
	TArray<FTaken> DressPlots(FArenaPlan& Plan, const FArenaSizes& Size)
	{
		const int32 Bases = Plan.Bases.Num();
		// two plots a connector joins never share a style; round a ring of three or more, the last differs from the first too
		FRandomStream Look(Plan.Seed ^ 0x4C4F4F4B);
		Plan.Styles.SetNum(Bases);
		Plan.Dryness.SetNum(Bases);
		for (int32 Base = 0; Base < Bases; ++Base) {
			TArray<int32, TInlineAllocator<3>> Choices = { 0, 1, 2 };
			if (Base > 0) {
				Choices.Remove(Plan.Styles[Base - 1]);
			}
			if (Base == Bases - 1 && Bases >= 3) {
				Choices.Remove(Plan.Styles[0]);
			}
			Plan.Styles[Base] = Choices[Look.RandRange(0, Choices.Num() - 1)];
			Plan.Dryness[Base] = FMath::Clamp(StyleDryness[Plan.Styles[Base]] + Look.FRandRange(-0.1f, 0.1f), 0.0f, 1.0f);
		}
		TArray<FTaken> Taken;
		FDrawnGround Drawn(Plan);
		for (int32 Base = 0; Base < Bases; ++Base) {
			FPlotDresser(Plan, Size, Base, Taken, Drawn).Dress();
		}
		return Taken;
	}

	/** What paints the ground, gathered once from a plan: the paths, and the things that stand in dirt or gravel. */
	struct FPaint
	{
		/** The paths: from each plot's middle to where each of its ways across begins or ends, painted from the pad's edge out. */
		TArray<TPair<FVector2D, FVector2D>> Paths;
		/** Discs of dirt and of gravel: where, out to how far fully, and how far it fades. */
		TArray<FVector4> Dirt;
		TArray<FVector4> Gravel;
		/** Which discs reach each square of ground PaintCell on a side, so a point is held against those near it only. */
		TMap<FIntPoint, TArray<int32>> DirtNear;
		TMap<FIntPoint, TArray<int32>> GravelNear;
	};

	constexpr double PaintCell = 3000.0;

	FIntPoint PaintSquare(double X, double Y)
	{
		return FIntPoint(FMath::FloorToInt32(X / PaintCell), FMath::FloorToInt32(Y / PaintCell));
	}

	void Bin(TMap<FIntPoint, TArray<int32>>& Near, const TArray<FVector4>& Discs)
	{
		for (int32 Index = 0; Index < Discs.Num(); ++Index) {
			const FVector4& Disc = Discs[Index];
			const double Out = Disc.Z + Disc.W;
			const FIntPoint Low = PaintSquare(Disc.X - Out, Disc.Y - Out);
			const FIntPoint High = PaintSquare(Disc.X + Out, Disc.Y + Out);
			for (int32 X = Low.X; X <= High.X; ++X) {
				for (int32 Y = Low.Y; Y <= High.Y; ++Y) {
					Near.FindOrAdd(FIntPoint(X, Y)).Add(Index);
				}
			}
		}
	}

	/** A path's gravel: fully this far either side of its line, fading over this much more. */
	constexpr double PathHalf = 300.0;
	constexpr double PathFade = 450.0;

	FPaint PaintOf(const FArenaPlan& Plan)
	{
		FPaint Paint;
		for (int32 Base = 0; Base < Plan.Bases.Num(); ++Base) {
			for (const FArenaConnector& Connector : Plan.Connectors) {
				const int32 Steps = Connector.Route.Num();
				if (Steps >= 2 && (Connector.FromBase == Base || Connector.ToBase == Base)) {
					Paint.Paths.Emplace(FVector2D(Plan.Bases[Base]), FVector2D(Connector.Route[Connector.FromBase == Base ? 0 : Steps - 2].Point));
				}
			}
		}
		for (const FArenaProp& Piece : Plan.Props) {
			const FVector At = Piece.Transform.GetLocation();
			const FVector Box = Piece.Transform.GetScale3D();
			const double Reach = 0.5 * FVector2D(Box.X, Box.Y).Size();
			if (Piece.Scenery >= EArenaScenery::TreeA && Piece.Scenery <= EArenaScenery::TreeD) {
				Paint.Dirt.Emplace(At.X, At.Y, 200.0, 400.0);
			}
			else if (Piece.Scenery >= EArenaScenery::CoverRock02 && Piece.Scenery <= EArenaScenery::CoverRock08) {
				Paint.Dirt.Emplace(At.X, At.Y, Reach * 0.8, 350.0);
			}
			else if (Piece.Scenery == EArenaScenery::CragRock03 || Piece.Scenery == EArenaScenery::CragRock11) {
				Paint.Dirt.Emplace(At.X, At.Y, Reach, 700.0);
			}
			else if (Piece.Scenery == EArenaScenery::SteelFrame) {
				Paint.Gravel.Emplace(At.X, At.Y, 950.0, 400.0);
			}
		}
		Bin(Paint.DirtNear, Paint.Dirt);
		Bin(Paint.GravelNear, Paint.Gravel);
		return Paint;
	}

	FLinearColor PaintAt(const FArenaPlan& Plan, const FPaint& Paint, double X, double Y)
	{
		const FVector2D At(X, Y);
		const FArenaSizes& Size = Plan.Size;
		double Gravel = 0.0;
		double Dirt = 0.0;
		// the grass of the nearer plot, blended across the land between plots
		double Dry = 0.0;
		double Weights = 0.0;
		for (int32 Base = 0; Base < Plan.Bases.Num(); ++Base) {
			const double Far = FVector2D::Distance(At, FVector2D(Plan.Bases[Base]));
			const double Weight = 1.0 / FMath::Max(1e-6, FMath::Square(FMath::Square(Far / 10000.0)));
			Weights += Weight;
			Dry += Weight * (Plan.Dryness.IsValidIndex(Base) ? Plan.Dryness[Base] : 0.0);
		}
		const double Wobble = 120.0 * FMath::PerlinNoise2D(At / 1700.0 + Plan.RipplesAt);
		for (const TPair<FVector2D, FVector2D>& Path : Paint.Paths) {
			// from the pad's edge out: the pad itself stays grass
			if (FVector2D::Distance(At, Path.Key) < Size.PadRadius - 300.0) {
				continue;
			}
			Gravel = FMath::Max(Gravel, 1.0 - Smooth(PathHalf + Wobble, PathHalf + Wobble + PathFade, FromLine(At, Path.Key, Path.Value)));
		}
		const FIntPoint Square = PaintSquare(X, Y);
		if (const TArray<int32>* Near = Paint.GravelNear.Find(Square)) {
			for (const int32 Index : *Near) {
				const FVector4& Disc = Paint.Gravel[Index];
				Gravel = FMath::Max(Gravel, 1.0 - Smooth(Disc.Z, Disc.Z + Disc.W, FVector2D::Distance(At, FVector2D(Disc.X, Disc.Y))));
			}
		}
		if (const TArray<int32>* Near = Paint.DirtNear.Find(Square)) {
			for (const int32 Index : *Near) {
				const FVector4& Disc = Paint.Dirt[Index];
				Dirt = FMath::Max(Dirt, 1.0 - Smooth(Disc.Z, Disc.Z + Disc.W, FVector2D::Distance(At, FVector2D(Disc.X, Disc.Y))));
			}
		}
		return FLinearColor(Gravel, Dirt, 0.0f, Weights > 0.0 ? Dry / Weights : 0.0);
	}

	uint32 HashOf(const FArenaPlan& Plan)
	{
		uint32 Hash = GetTypeHash(Plan.Bases.Num());
		const auto Mix = [&Hash](const FVector& Value) {
			Hash = HashCombine(Hash, GetTypeHash(FIntVector(FMath::RoundToInt(Value.X), FMath::RoundToInt(Value.Y), FMath::RoundToInt(Value.Z))));
		};
		for (const FVector& Base : Plan.Bases) {
			Mix(Base);
		}
		for (const FArenaConnector& Connector : Plan.Connectors) {
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Connector.Kind)));
		}
		for (const FArenaBox& Block : Plan.Boxes) {
			Mix(Block.Centre);
			Mix(Block.Size);
			Mix(Block.Rotation.Rotator().Euler());
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Block.Tint)));
		}
		for (const FArenaCraft& Craft : Plan.Craft) {
			Mix(Craft.LaneStart);
			Mix(FVector(Craft.Speed, Craft.Offset, Craft.LaneLength));
		}
		Mix(FVector(Plan.HillsAt.X, Plan.HillsAt.Y, Plan.Reach));
		Mix(FVector(Plan.RipplesAt.X, Plan.RipplesAt.Y, Plan.Size.HillHeight));
		Mix(FVector(Plan.CoastAt.X, Plan.CoastAt.Y, Plan.Size.WaterDepth));
		Mix(FVector(Plan.Size.PadRadius, Plan.Size.BaseRadius, Plan.Size.ConnectorWidth));
		for (int32 Base = 0; Base < Plan.Styles.Num() && Base < Plan.Dryness.Num(); ++Base) {
			Hash = HashCombine(Hash, GetTypeHash(Plan.Styles[Base]));
			Mix(FVector(Plan.Dryness[Base] * 1000.0, 0.0, 0.0));
		}
		for (const FArenaProp& Piece : Plan.Props) {
			Mix(Piece.Transform.GetLocation());
			Mix(Piece.Transform.GetRotation().Rotator().Euler());
			Mix(Piece.Transform.GetScale3D() * 1000.0);
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Piece.Scenery) + (Piece.bFit ? 64 : 0)));
		}
		return Hash;
	}
}

const TCHAR* ASenniteArena::KindName(EConnectorKind Kind)
{
	switch (Kind) {
	case EConnectorKind::Floating: return TEXT("Floating");
	case EConnectorKind::TreeTop: return TEXT("TreeTop");
	case EConnectorKind::Canyon: return TEXT("Canyon");
	case EConnectorKind::Traffic: return TEXT("Traffic");
	case EConnectorKind::Tunnel: return TEXT("Tunnel");
	case EConnectorKind::Bridges: return TEXT("Bridges");
	default: return TEXT("None");
	}
}

FString FArenaPlan::KindsText() const
{
	return FString::JoinBy(Connectors, TEXT(" "), [](const FArenaConnector& Connector) { return FString(ASenniteArena::KindName(Connector.Kind)); });
}

FArenaSizes ASenniteArena::Sizes()
{
	const ASenniteArena* Settings = GetDefault<ASenniteArena>();
	FArenaSizes Out;
	Out.BaseRadius = Settings->BaseRadius;
	Out.ConnectorLength = Settings->ConnectorLength;
	Out.ConnectorWidth = Settings->ConnectorWidth;
	Out.TreesPerBase = Settings->TreesPerBase;
	Out.BushesPerBase = Settings->BushesPerBase;
	Out.PadRadius = Settings->PadRadius;
	Out.WaterDepth = Settings->WaterDepth;
	Out.HillHeight = Settings->HillHeight;
	Out.CoverPerBase = Settings->CoverPerBase;
	Out.CoverPerWay = Settings->CoverPerWay;
	Out.OutcropsPerBase = Settings->OutcropsPerBase;
	Out.CragsPerBase = Settings->CragsPerBase;
	Out.FramesPerBase = Settings->FramesPerBase;
	Out.StonesPerBase = Settings->StonesPerBase;
	Out.PatchesPerBase = Settings->PatchesPerBase;
	return Out;
}

FVector ASenniteArena::ShapeOf(EArenaScenery Kind)
{
	// as imported: Rock Pack Vol 01's rocks and the Construction Prop's frame (Tools/import_rocks.py),
	// Tundra Rocky Ground (Tools/import_scenery.py); the arena check holds each mesh to these
	switch (Kind) {
	case EArenaScenery::CoverRock02: return FVector(21.8, 50.5, 23.5);
	case EArenaScenery::CoverRock04: return FVector(23.1, 42.0, 20.7);
	case EArenaScenery::CoverRock05: return FVector(35.1, 12.1, 6.1);
	case EArenaScenery::CoverRock06: return FVector(36.5, 12.3, 13.2);
	case EArenaScenery::CoverRock07: return FVector(34.3, 16.7, 16.2);
	case EArenaScenery::CoverRock08: return FVector(10.2, 26.3, 9.4);
	case EArenaScenery::CragRock03: return FVector(27.8, 57.0, 22.0);
	case EArenaScenery::CragRock11: return FVector(37.6, 49.5, 23.7);
	case EArenaScenery::SteelFrame: return FVector(892.0, 1318.0, 383.0);
	case EArenaScenery::Stone01: return FVector(11.5, 14.1, 6.3);
	case EArenaScenery::Stone10: return FVector(19.2, 14.1, 6.3);
	case EArenaScenery::Stone12: return FVector(12.4, 14.0, 6.2);
	case EArenaScenery::StonyGround: return FVector(706.0, 465.0, 46.0);
	default: return FVector::ZeroVector;
	}
}

FLinearColor ASenniteArena::GroundPaint(const FArenaPlan& Plan, double X, double Y)
{
	return PaintAt(Plan, PaintOf(Plan), X, Y);
}

double ASenniteArena::OutsideLand(const FArenaPlan& Plan, double X, double Y)
{
	double Out = TNumericLimits<double>::Max();
	for (const FVector& Base : Plan.Bases) {
		Out = FMath::Min(Out, FMath::Max(0.0, FVector2D::Distance(FVector2D(X, Y), FVector2D(Base)) - Plan.Size.BaseRadius));
	}
	// how far outside any gorge: the coast keeps to its outline there, so a river's banks stand where the bridges begin
	double FromGorge = TNumericLimits<double>::Max();
	for (const FArenaConnector& Connector : Plan.Connectors) {
		const FStripPlace Place = InStrip(Plan, Connector, X, Y);
		if (!IsGorge(Connector.Kind)) {
			Out = FMath::Min(Out, Place.Outside);
			continue;
		}
		// a way over a gorge is land only at its two mouths; between them is the gorge, open to the lake at both sides
		const double Near = MouthOf(Connector.Kind, false);
		const double Far = Plan.Size.ConnectorLength - MouthOf(Connector.Kind, true);
		const double Neck = Plan.Reach - Plan.Size.BaseRadius + 400.0;
		const double PastMouth = Place.Along < (Near + Far) * 0.5
			? FMath::Max3(Place.Along - Near, -Neck - Place.Along, 0.0)
			: FMath::Max3(Far - Place.Along, Place.Along - (Plan.Size.ConnectorLength + Neck), 0.0);
		Out = FMath::Min(Out, FMath::Sqrt(PastMouth * PastMouth + FMath::Square(FMath::Max(FMath::Abs(Place.Across) - Plan.Size.ConnectorWidth * 0.5, 0.0))));
		FromGorge = FMath::Min(FromGorge, Place.Outside);
	}
	// the coast wanders: the land reaches 10 to 60 m past its outline, farther in some places than others
	const double Wander = (1000.0 + (CoastReach - 1000.0) * 0.5 * (1.0 + FMath::PerlinNoise2D(FVector2D(X, Y) / 6000.0 + Plan.CoastAt))) * Smooth(0.0, 3000.0, FromGorge);
	return FMath::Max(0.0, Out - Wander);
}

double ASenniteArena::GroundHeight(const FArenaPlan& Plan, double X, double Y)
{
	const FArenaSizes& Size = Plan.Size;
	const FVector2D At(X, Y);
	// the hills: broad ones, with ripples over them, a little more up than down
	const double Hills = Size.HillHeight * (0.45 + FMath::PerlinNoise2D(At / 14000.0 + Plan.HillsAt))
		+ Size.HillHeight * 0.12 * FMath::PerlinNoise2D(At / 4000.0 + Plan.RipplesAt);
	// level where things stand: each plot's pad, for a faction's buildings, and where a connector meets the land
	double Level = 0.0;
	for (const FVector& Base : Plan.Bases) {
		Level = FMath::Max(Level, 1.0 - Smooth(Size.PadRadius, Size.PadRadius + LevelEase, FVector2D::Distance(At, FVector2D(Base))));
	}
	TArray<TPair<double, double>, TInlineAllocator<6>> Strips;
	for (const FArenaConnector& Connector : Plan.Connectors) {
		const FStripPlace Place = InStrip(Plan, Connector, X, Y);
		const double Weight = 1.0 - Smooth(0.0, LevelEase, Place.Outside);
		if (Weight <= 0.0) {
			continue;
		}
		Level = FMath::Max(Level, Weight);
		// a connector's own ground lies a hair under its floor; where it is a way over a gorge the shore, below, takes the ground down between its mouths
		Strips.Emplace(Weight, FloorTop - 4.0);
	}
	double Height = Hills * (1.0 - Level);
	for (const TPair<double, double>& Strip : Strips) {
		Height = FMath::Lerp(Height, Strip.Value, Strip.Key);
	}
	// the shore, and a gorge's banks: past the land's outline the ground goes down under the water
	return FMath::Lerp(Height, BedOf(Size), Smooth(0.0, ShoreWidth, OutsideLand(Plan, X, Y)));
}

FArenaPlan ASenniteArena::MakePlan(int32 Seed, int32 BaseCount, int32 ForcedKind)
{
	return MakePlan(Seed, BaseCount, ForcedKind, Sizes());
}

FArenaPlan ASenniteArena::MakePlan(int32 Seed, int32 BaseCount, int32 ForcedKind, const FArenaSizes& Size)
{
	const float BaseRadius = Size.BaseRadius;
	const float ConnectorLength = Size.ConnectorLength;
	const float ConnectorWidth = Size.ConnectorWidth;
	const float Length = ConnectorLength;
	FArenaPlan Plan;
	Plan.Seed = Seed;
	Plan.Size = Size;
	// where the hills, their ripples and the coast's wander are taken from, drawn one at a time from a stream of their own
	FRandomStream Lay(Seed ^ 0x4C414E44);
	for (FVector2D* Shift : { &Plan.HillsAt, &Plan.RipplesAt, &Plan.CoastAt }) {
		Shift->X = Lay.FRandRange(-500.0f, 500.0f);
		Shift->Y = Lay.FRandRange(-500.0f, 500.0f);
	}
	FRandomStream Rand(Seed);
	const int32 Bases = FMath::Clamp(BaseCount, 2, static_cast<int32>(EConnectorKind::Count));

	// The six kinds in an order the seed picks. Connectors take them in turn, so two that meet at a
	// base are never the same kind, and a match of up to six bases has no kind twice.
	TArray<EConnectorKind> Kinds;
	for (int32 Kind = 0; Kind < static_cast<int32>(EConnectorKind::Count); ++Kind) {
		Kinds.Add(static_cast<EConnectorKind>(Kind));
	}
	for (int32 Index = Kinds.Num() - 1; Index > 0; --Index) {
		Kinds.Swap(Index, Rand.RandRange(0, Index));
	}

	// The bases stand on a ring, turned as the seed says, far enough apart that two connectors
	// leaving one base do not run into each other.
	const int32 Links = Bases == 2 ? 1 : Bases;
	const float Corner = Bases == 2 ? 180.0f : 180.0f - 360.0f / Bases;
	const float Reach = FMath::Max(BaseRadius, (ConnectorWidth * 0.5f) / FMath::Tan(FMath::DegreesToRadians(Corner * 0.5f)));
	const float Apart = 2.0f * Reach + ConnectorLength;
	const float Ring = Apart / (2.0f * FMath::Sin(UE_PI / Bases));
	const float Turn = Rand.FRandRange(0.0f, 360.0f);
	for (int32 Base = 0; Base < Bases; ++Base) {
		Plan.Bases.Add(FRotator(0.0f, Turn + Base * 360.0f / Bases, 0.0f).RotateVector(FVector(Ring, 0.0f, 0.0f)));
	}

	Plan.Reach = Reach;

	Plan.Connectors.SetNum(Links);
	for (int32 Link = 0; Link < Links; ++Link) {
		FArenaConnector& Connector = Plan.Connectors[Link];
		Connector.FromBase = Link;
		Connector.ToBase = (Link + 1) % Bases;
		const FVector Along = (Plan.Bases[Connector.ToBase] - Plan.Bases[Connector.FromBase]).GetSafeNormal();
		Connector.Frame = FTransform(Along.Rotation(), Plan.Bases[Connector.FromBase] + Along * Reach);
		Connector.Kind = ForcedKind >= 0 && ForcedKind < static_cast<int32>(EConnectorKind::Count) ? static_cast<EConnectorKind>(ForcedKind) : Kinds[Link % Kinds.Num()];

		// where a connector starts short of its plot's edge, the land between is ground like any other
		FBuilder Builder{ Plan, Connector, Rand, Link, ConnectorLength, ConnectorWidth, static_cast<float>(BedOf(Size)) };
		switch (Connector.Kind) {
		case EConnectorKind::Floating: Builder.Floating(); break;
		case EConnectorKind::TreeTop: Builder.TreeTop(); break;
		case EConnectorKind::Canyon: Builder.Canyon(); break;
		case EConnectorKind::Traffic: Builder.Traffic(); break;
		case EConnectorKind::Tunnel: Builder.Tunnel(); break;
		default: Builder.Bridges(); break;
		}
		// the way across ends on the far base, not at its edge
		FArenaStep Home;
		Home.Point = Plan.Bases[Connector.ToBase] - Along * (BaseRadius * 0.5f);
		Connector.Route.Add(Home);
	}
	// The plots' dressing: each plot's style, then its crag, steel frame, rocks of cover, stones and
	// stony ground, from streams of their own, so the connectors are what the seed makes with none.
	const TArray<FTaken> Taken = DressPlots(Plan, Size);
	// a tree's trunk keeps TreeRoom from what the dressing stood up; a bush 2 m, so a line run 1.2 m
	// either side of it meets no rock
	const auto CrowdsTree = [&Taken](const FVector& At) {
		return Taken.ContainsByPredicate([&At](const FTaken& Stood) { return FVector2D::Distance(FVector2D(At), Stood.At) < Stood.Reach + Stood.TreeRoom; });
	};
	const auto CrowdsBush = [&Taken](const FVector& At) {
		return Taken.ContainsByPredicate([&At](const FTaken& Stood) { return FVector2D::Distance(FVector2D(At), Stood.At) < Stood.Reach + 200.0; });
	};

	// Trees and bushes on the land of each plot, outside its pad and clear of every way out of it and of
	// the dressing, each standing on the ground where it is. They come from a stream of their own, so
	// everything else of the arena is what its seed makes with none. A tree is scenery over a trunk
	// block, which is what a hero or a shot meets; the block is seen only if the tree is missing. A bush
	// is scenery alone.
	FRandomStream Green(Seed ^ 0x47524E);
	const int32 TreeKinds = static_cast<int32>(EArenaScenery::BushA) - static_cast<int32>(EArenaScenery::TreeA);
	const int32 BushKinds = static_cast<int32>(EArenaScenery::CoverRock02) - static_cast<int32>(EArenaScenery::BushA);
	const auto StandTree = [&Plan, &Green, TreeKinds](const FVector& At) {
		const FRotator Turned(0.0f, Green.FRandRange(0.0f, 360.0f), 0.0f);
		const float Grown = Green.FRandRange(0.8f, 1.2f);
		FArenaProp Piece;
		Piece.Scenery = static_cast<EArenaScenery>(static_cast<int32>(EArenaScenery::TreeA) + Green.RandRange(0, TreeKinds - 1));
		Piece.Transform = FTransform(Turned, At, FVector(Grown));
		Plan.Props.Add(Piece);
		FArenaBox Trunk;
		Trunk.Size = FVector(60.0f, 60.0f, 300.0f) * Grown;
		Trunk.Centre = At + FVector(0.0f, 0.0f, Trunk.Size.Z * 0.5f);
		Trunk.Rotation = Turned.Quaternion();
		Trunk.Tint = EArenaTint::Hidden;
		Plan.Boxes.Add(Trunk);
	};
	for (int32 Base = 0; Base < Bases; ++Base) {
		// the ways out: toward each base this one is joined to
		TArray<FVector> Ways;
		for (const FArenaConnector& Connector : Plan.Connectors) {
			if (Connector.FromBase == Base || Connector.ToBase == Base) {
				Ways.Add((Plan.Bases[Connector.FromBase == Base ? Connector.ToBase : Connector.FromBase] - Plan.Bases[Base]).GetSafeNormal2D());
			}
		}
		// clear of a way out: not in the strip, a connector wide and Margin more each side, that runs out along it
		const auto Clear = [&Plan, &Ways, Base, ConnectorWidth](const FVector& At, float Margin) {
			const FVector Out = At - Plan.Bases[Base];
			for (const FVector& Way : Ways) {
				if (FVector::DotProduct(Out, Way) > 0.0 && FMath::Abs(FVector::CrossProduct(Way, Out).Z) < ConnectorWidth * 0.5f + Margin) {
					return false;
				}
			}
			return true;
		};
		const auto Somewhere = [&Plan, &Green, Base, BaseRadius](float Near, float Far) {
			const float Turned = Green.FRandRange(0.0f, 360.0f);
			return Plan.Bases[Base] + FRotator(0.0f, Turned, 0.0f).RotateVector(FVector(BaseRadius * Green.FRandRange(Near, Far), 0.0f, 0.0f));
		};
		TArray<FVector> Trunks;
		for (int32 Tree = 0; Tree < Size.TreesPerBase; ++Tree) {
			for (int32 Try = 0; Try < 12; ++Try) {
				FVector At = Somewhere(0.66f, 0.97f);
				if (!Clear(At, 600.0f) || CrowdsTree(At) || Trunks.ContainsByPredicate([&At](const FVector& Other) { return FVector::Dist2D(At, Other) < 900.0f; })) {
					continue;
				}
				// on the ground there, and not where it has begun to fall toward a gorge or the water
				At.Z = GroundHeight(Plan, At.X, At.Y);
				if (At.Z < -300.0) {
					continue;
				}
				StandTree(At);
				Trunks.Add(At);
				break;
			}
		}
		for (int32 Bush = 0; Bush < Size.BushesPerBase; ++Bush) {
			for (int32 Try = 0; Try < 6; ++Try) {
				FVector At = Somewhere(0.50f, 0.99f);
				if (!Clear(At, 200.0f) || CrowdsBush(At) || Trunks.ContainsByPredicate([&At](const FVector& Other) { return FVector::Dist2D(At, Other) < 250.0f; })) {
					continue;
				}
				At.Z = GroundHeight(Plan, At.X, At.Y);
				if (At.Z < -300.0) {
					continue;
				}
				FArenaProp Piece;
				Piece.Scenery = static_cast<EArenaScenery>(static_cast<int32>(EArenaScenery::BushA) + Green.RandRange(0, BushKinds - 1));
				// drawn one at a time, in this order: inside one call the order would be the compiler's to choose
				const float Turned = Green.FRandRange(0.0f, 360.0f);
				const float Grown = Green.FRandRange(0.8f, 1.4f);
				Piece.Transform = FTransform(FRotator(0.0f, Turned, 0.0f), At, FVector(Grown));
				Plan.Props.Add(Piece);
				break;
			}
		}
	}

	// A forest under each tree-top boardwalk: four plots' worth of trees on the ground between its two
	// plots, at least 7 m apart, their tops above the walk.
	for (const FArenaConnector& Connector : Plan.Connectors) {
		if (Connector.Kind != EConnectorKind::TreeTop) {
			continue;
		}
		TArray<FVector> Trunks;
		for (int32 Tree = 0; Tree < Size.TreesPerBase * 4; ++Tree) {
			for (int32 Try = 0; Try < 8; ++Try) {
				const float Along = Green.FRandRange(1500.0f, ConnectorLength - 1500.0f);
				const float Across = Green.FRandRange(-ConnectorWidth * 0.5f + 400.0f, ConnectorWidth * 0.5f - 400.0f);
				FVector At = Connector.Frame.TransformPosition(FVector(Along, Across, 0.0f));
				if (Trunks.ContainsByPredicate([&At](const FVector& Other) { return FVector::Dist2D(At, Other) < 700.0f; })) {
					continue;
				}
				At.Z = GroundHeight(Plan, At.X, At.Y);
				StandTree(At);
				Trunks.Add(At);
				break;
			}
		}
	}

	Plan.Hash = HashOf(Plan);
	return Plan;
}

FVector ASenniteArena::CraftLocation(const FArenaCraft& Craft, double MatchSeconds)
{
	const double Along = FMath::Fmod(Craft.Offset + Craft.Speed * MatchSeconds, static_cast<double>(Craft.LaneLength));
	return Craft.LaneStart + Craft.LaneDirection * Along;
}

double ASenniteArena::MatchSeconds() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* Game = World ? World->GetGameState() : nullptr;
	return Game ? Game->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0);
}

ASenniteArena::ASenniteArena()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASenniteArena::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASenniteArena, MatchSeed);
	DOREPLIFETIME(ASenniteArena, MatchBases);
	DOREPLIFETIME(ASenniteArena, MatchForcedKind);
	DOREPLIFETIME(ASenniteArena, ServerHash);
}

void ASenniteArena::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	UWorld* World = GetWorld();
	if (World == nullptr || !World->IsGameWorld() || !HasAuthority()) {
		return;
	}
	// The server chooses, before any player has a place to start. The map's address can say
	// (?Seed=7?Bases=4?Connector=Canyon); otherwise the settings do.
	const TCHAR* SeedOption = World->URL.GetOption(TEXT("Seed="), nullptr);
	const TCHAR* BasesOption = World->URL.GetOption(TEXT("Bases="), nullptr);
	const TCHAR* KindOption = World->URL.GetOption(TEXT("Connector="), nullptr);
	MatchSeed = SeedOption ? FCString::Atoi(SeedOption) : DefaultSeed;
	if (MatchSeed < 0) {
		MatchSeed = FMath::Rand() ^ (FMath::Rand() << 15);
	}
	MatchSeed &= MAX_int32;
	MatchBases = FMath::Clamp(BasesOption ? FCString::Atoi(BasesOption) : DefaultBases, 2, static_cast<int32>(EConnectorKind::Count));
	MatchForcedKind = INDEX_NONE;
	for (int32 Kind = 0; KindOption != nullptr && Kind < static_cast<int32>(EConnectorKind::Count); ++Kind) {
		if (FCString::Stricmp(KindOption, KindName(static_cast<EConnectorKind>(Kind))) == 0) {
			MatchForcedKind = Kind;
		}
	}
	Build();
	ServerHash = Plan.Hash;
}

void ASenniteArena::OnRep_Arena()
{
	if (!bBuilt && MatchSeed >= 0 && MatchBases >= 2) {
		Build();
	}
}

void ASenniteArena::Build()
{
	Plan = MakePlan(MatchSeed, MatchBases, MatchForcedKind);
	const FArenaSizes& Size = Plan.Size;

	// what the land, and the road of a deck or a span, is drawn with: its own material when one is set and loads
	const auto Surface = [](const FSoftObjectPath& Named, const TCHAR* What) -> UMaterialInterface* {
		UMaterialInterface* Loaded = Named.IsNull() ? nullptr : Cast<UMaterialInterface>(Named.TryLoad());
		if (!Named.IsNull() && Loaded == nullptr) {
			UE_LOG(LogTemp, Error, TEXT("%s '%s' did not load; it is drawn plain"), What, *Named.ToString());
		}
		return Loaded;
	};

	// The land: one mesh of ground, a point every GroundCell, wherever the plan's ground is land or
	// shore. It is what heroes stand on, so it is made first, on every machine, under one name. The
	// part of it under the water is the lake's bed.
	{
		const FGroundGrid Grid = GridOf(Plan);
		const FVector2D Corner = Grid.Corner;
		const int32 Across = Grid.Across;
		const int32 Down = Grid.Down;
		TArray<double> High;
		TArray<bool> bLand;
		High.SetNumUninitialized(Across * Down);
		bLand.SetNumUninitialized(Across * Down);
		for (int32 Row = 0; Row < Down; ++Row) {
			for (int32 Column = 0; Column < Across; ++Column) {
				const double X = Corner.X + Column * GroundCell;
				const double Y = Corner.Y + Row * GroundCell;
				High[Row * Across + Column] = GroundHeight(Plan, X, Y);
				bLand[Row * Across + Column] = OutsideLand(Plan, X, Y) < ShoreWidth + GroundCell;
			}
		}
		// a cell is made if any corner of it is land or shore; a point is kept if a cell that is made uses it
		TArray<int32> Kept;
		Kept.Init(INDEX_NONE, Across * Down);
		TArray<FVector> Points;
		TArray<FVector> Normals;
		TArray<FVector2D> Places;
		// what each point is painted with: gravel, dirt, and how dry the grass (GroundPaint)
		const FPaint Paint = PaintOf(Plan);
		TArray<FLinearColor> Paints;
		TArray<int32> Corners;
		const auto Point = [&](int32 Column, int32 Row) {
			int32& Index = Kept[Row * Across + Column];
			if (Index == INDEX_NONE) {
				Index = Points.Num();
				const auto At = [&](int32 C, int32 R) { return High[FMath::Clamp(R, 0, Down - 1) * Across + FMath::Clamp(C, 0, Across - 1)]; };
				Points.Add(FVector(Corner.X + Column * GroundCell, Corner.Y + Row * GroundCell, At(Column, Row)));
				Normals.Add(FVector(At(Column - 1, Row) - At(Column + 1, Row), At(Column, Row - 1) - At(Column, Row + 1), 2.0 * GroundCell).GetSafeNormal());
				Places.Add(FVector2D(Points.Last().X, Points.Last().Y) / 100.0);
				Paints.Add(PaintAt(Plan, Paint, Points.Last().X, Points.Last().Y));
			}
			return Index;
		};
		for (int32 Row = 0; Row + 1 < Down; ++Row) {
			for (int32 Column = 0; Column + 1 < Across; ++Column) {
				if (!bLand[Row * Across + Column] && !bLand[Row * Across + Column + 1] && !bLand[(Row + 1) * Across + Column] && !bLand[(Row + 1) * Across + Column + 1]) {
					continue;
				}
				const int32 A = Point(Column, Row);
				const int32 B = Point(Column, Row + 1);
				const int32 C = Point(Column + 1, Row + 1);
				const int32 D = Point(Column + 1, Row);
				// wound to face up
				Corners.Append({ A, B, D, B, C, D });
			}
		}
		Ground = NewObject<UProceduralMeshComponent>(this, TEXT("ArenaGround"));
		Ground->bUseComplexAsSimpleCollision = true;
		Ground->SetCollisionProfileName(TEXT("BlockAll"));
		Ground->SetCanEverAffectNavigation(false);
		Ground->SetNetAddressable();
		Ground->SetupAttachment(RootComponent);
		Ground->RegisterComponent();
		Ground->SetWorldTransform(FTransform::Identity);
		Ground->CreateMeshSection_LinearColor(0, Points, Corners, Normals, Places, Paints, TArray<FProcMeshTangent>(), true);
		if (UMaterialInterface* Earth = Surface(BaseGround, TEXT("What the land is drawn with"))) {
			Ground->SetMaterial(0, Earth);
		}
		UE_LOG(LogTemp, Display, TEXT("Arena ground: %d points, %d triangles, %.0f by %.0f m"), Points.Num(), Corners.Num() / 3, (Across - 1) * GroundCell / 100.0, (Down - 1) * GroundCell / 100.0);
	}

	UStaticMesh* Cube = Cast<UStaticMesh>(BlockShape.TryLoad());
	UMaterialInterface* Material = Cast<UMaterialInterface>(BlockMaterial.TryLoad());
	if (Cube == nullptr || Material == nullptr) {
		UE_LOG(LogTemp, Error, TEXT("The arena's block shape or material did not load (%s, %s); there is land and nothing on it"), *BlockShape.ToString(), *BlockMaterial.ToString());
		return;
	}
	UMaterialInterface* Road = Surface(RoadSurface, TEXT("The road of a deck or a span"));

	// one colour to a sort of block, so each kind of connector reads at a glance
	const FLinearColor Colours[static_cast<int32>(EArenaTint::Count)] = {
		FLinearColor(0.10f, 0.24f, 0.50f),   // Floating
		FLinearColor(0.22f, 0.11f, 0.04f),   // Boardwalk
		FLinearColor(0.05f, 0.03f, 0.02f),   // Trunk
		FLinearColor(0.38f, 0.13f, 0.05f),   // Rock
		FLinearColor(0.03f, 0.04f, 0.06f),   // Deck
		FLinearColor(0.90f, 0.30f, 0.02f),   // Craft
		FLinearColor(0.24f, 0.21f, 0.18f),   // TunnelRock: light enough to see by the sky fill, which is all that reaches inside
		FLinearColor(0.30f, 0.25f, 0.14f),   // Bridge
		FLinearColor(0.08f, 0.13f, 0.09f),   // Cover
		FLinearColor(0.80f, 0.60f, 0.02f),   // Marking
		FLinearColor(0.38f, 0.13f, 0.05f),   // Hidden: shown, as rock, only when its scenery is missing
	};
	const auto MakeSet = [this](UStaticMesh* Shape, UMaterialInterface* Face) {
		UInstancedStaticMeshComponent* Set = NewObject<UInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("ArenaBlocks_%d"), BlockSets.Num()));
		Set->SetStaticMesh(Shape);
		Set->SetMaterial(0, Face);
		Set->SetCollisionProfileName(TEXT("BlockAll"));
		Set->SetCanEverAffectNavigation(false);
		// A player stands on these. The server tells a joined machine where its hero is by what the
		// hero stands on, so each machine must be able to name the same set: every machine makes them
		// in the same order, under the same names, before any set of scenery.
		Set->SetNetAddressable();
		Set->SetupAttachment(RootComponent);
		Set->RegisterComponent();
		BlockSets.Add(Set);
		return Set;
	};
	TArray<UInstancedStaticMeshComponent*> ByTint;
	for (int32 Tint = 0; Tint < static_cast<int32>(EArenaTint::Count); ++Tint) {
		UMaterialInstanceDynamic* Face = UMaterialInstanceDynamic::Create(Material, this);
		Face->SetVectorParameterValue(TEXT("Tint"), Colours[Tint]);
		Tints.Add(Face);
		// the traffic road and the bridges' decks are road
		const bool bRoad = Tint == static_cast<int32>(EArenaTint::Deck) || Tint == static_cast<int32>(EArenaTint::Bridge);
		ByTint.Add(MakeSet(Cube, bRoad && Road != nullptr ? Road : static_cast<UMaterialInterface*>(Face)));
	}
	for (const FArenaBox& Block : Plan.Boxes) {
		ByTint[static_cast<int32>(Block.Tint)]->AddInstance(FTransform(Block.Rotation, Block.Centre, Block.Size / 100.0f), true);
	}

	// The scenery: seen, and nothing else. Blocks marked hidden are the walls and ground inside it;
	// they stay hidden only if the scenery that covers them is there to be seen.
	bool bAllScenery = true;
	TArray<UInstancedStaticMeshComponent*> ByScenery;
	for (int32 Scenery = 0; Scenery < static_cast<int32>(EArenaScenery::Count); ++Scenery) {
		UStaticMesh* Mesh = SceneryMeshes.IsValidIndex(Scenery) ? Cast<UStaticMesh>(SceneryMeshes[Scenery].TryLoad()) : nullptr;
		UInstancedStaticMeshComponent* Set = nullptr;
		if (Mesh != nullptr) {
			Set = NewObject<UInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("ArenaScenery_%d"), Scenery));
			Set->SetStaticMesh(Mesh);
			Set->SetCanEverAffectNavigation(false);
			if (Scenery >= static_cast<int32>(EArenaScenery::CoverRock02) && Scenery <= static_cast<int32>(EArenaScenery::CragRock11)) {
				// the plots' rocks and crags are solid by their own shapes: their meshes collide as drawn
				// (Tools/import_rocks.py). A hero can stand on one, so every machine names its set alike.
				Set->SetCollisionProfileName(TEXT("BlockAll"));
				Set->SetNetAddressable();
			}
			else {
				Set->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			// how far each is drawn: the plots' rocks of cover past a rifle's reach (300 m), their frames a
			// little farther, their crags always; their small stones and stony ground near, with no shadow
			if (Scenery >= static_cast<int32>(EArenaScenery::CoverRock02) && Scenery <= static_cast<int32>(EArenaScenery::CoverRock08)) {
				Set->SetCullDistances(35000, 38000);
			}
			else if (Scenery == static_cast<int32>(EArenaScenery::SteelFrame)) {
				Set->SetCullDistances(40000, 45000);
			}
			else if (Scenery >= static_cast<int32>(EArenaScenery::Stone01)) {
				const bool bPatch = Scenery == static_cast<int32>(EArenaScenery::StonyGround);
				Set->SetCastShadow(false);
				Set->SetCullDistances(bPatch ? 12000 : 9000, bPatch ? 15000 : 11000);
			}
			Set->SetupAttachment(RootComponent);
			Set->RegisterComponent();
			BlockSets.Add(Set);
		}
		else {
			bAllScenery = false;
			UE_LOG(LogTemp, Warning, TEXT("Arena scenery %d did not load; its blocks are shown plain"), Scenery);
		}
		ByScenery.Add(Set);
	}
	for (const FArenaProp& Piece : Plan.Props) {
		if (UInstancedStaticMeshComponent* Set = ByScenery[static_cast<int32>(Piece.Scenery)]) {
			FTransform Placed = Piece.Transform;
			if (Piece.bFit) {
				// sized to fill its box, and set so the middle of the mesh is the middle of the box
				const FBox Bounds = Set->GetStaticMesh()->GetBoundingBox();
				const FVector Scale = Piece.Transform.GetScale3D() / Bounds.GetSize().ComponentMax(FVector(1.0));
				Placed.SetScale3D(Scale);
				Placed.AddToTranslation(-Placed.GetRotation().RotateVector(Bounds.GetCenter() * Scale));
			}
			Set->AddInstance(Placed, true);
		}
	}
	ByTint[static_cast<int32>(EArenaTint::Hidden)]->SetVisibility(!bAllScenery);

	// The country round the arena: seen, and nothing else, so a machine with no screen does without
	// it. Its meshes have the place the arena stands at their origin and the lake's surface at height
	// 0: set WaterDepth below the plots' level ground, the arena's land rises out of its lake.
	if (GetNetMode() != NM_DedicatedServer) {
		for (const FSoftObjectPath& Named : { Land, LandWater }) {
			if (Named.IsNull()) {
				continue;
			}
			UStaticMesh* Mesh = Cast<UStaticMesh>(Named.TryLoad());
			if (Mesh == nullptr) {
				UE_LOG(LogTemp, Error, TEXT("'%s', part of the country round the arena, did not load; it is not drawn"), *Named.ToString());
				continue;
			}
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
			Part->SetStaticMesh(Mesh);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCanEverAffectNavigation(false);
			Part->SetCastShadow(false);
			Part->SetupAttachment(RootComponent);
			Part->RegisterComponent();
			Part->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, -Size.WaterDepth), FVector(LandScale)));
			LandParts.Add(Part);
		}
	}

	// the craft: seen here, moved in Tick, and felt through the server's own sums, not through collision
	for (const FArenaCraft& Craft : Plan.Craft) {
		UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(this);
		Body->SetStaticMesh(Cube);
		Body->SetMaterial(0, Tints[static_cast<int32>(EArenaTint::Craft)]);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->SetupAttachment(RootComponent);
		Body->RegisterComponent();
		Body->SetWorldTransform(FTransform(Craft.Rotation, CraftLocation(Craft, MatchSeconds()), Craft.Size / 100.0f));
		CraftBodies.Add(Body);
	}

	// on the server, a place to start on each plot's pad, facing the way out
	if (HasAuthority()) {
		for (int32 Base = 0; Base < Plan.Bases.Num(); ++Base) {
			const int32 Out = Plan.Connectors.IsValidIndex(Base) ? Base : 0;
			const FVector Toward = Plan.Connectors[Out].Frame.GetLocation() - Plan.Bases[Base];
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (APlayerStart* Start = GetWorld()->SpawnActor<APlayerStart>(Plan.Bases[Base] + FVector(0.0f, 0.0f, 110.0f), FRotator(0.0f, Toward.Rotation().Yaw, 0.0f), Spawn)) {
				StartActors.Add(Start);
			}
		}
	}
	bBuilt = true;
	UE_LOG(LogTemp, Display, TEXT("Arena built: seed %d, %d bases, connectors: %s; %d blocks, %d craft, hash %u"), Plan.Seed, Plan.Bases.Num(), *Plan.KindsText(), Plan.Boxes.Num(), Plan.Craft.Num(), Plan.Hash);
}

void ASenniteArena::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bBuilt || Plan.Craft.Num() == 0) {
		return;
	}
	const double Now = MatchSeconds();
	for (int32 Index = 0; Index < Plan.Craft.Num() && Index < CraftBodies.Num(); ++Index) {
		CraftBodies[Index]->SetWorldLocation(CraftLocation(Plan.Craft[Index], Now));
	}
	if (!HasAuthority()) {
		return;
	}
	// A craft that reaches a player takes health from them and throws them down its lane. One pass, one hit.
	for (TActorIterator<ASenniteCharacter> It(GetWorld()); It; ++It) {
		ASenniteCharacter* Hero = *It;
		const double* Last = LastCraftHit.Find(Hero);
		if (Last != nullptr && Now - *Last < 0.6) {
			continue;
		}
		for (const FArenaCraft& Craft : Plan.Craft) {
			const FVector Offset = Craft.Rotation.UnrotateVector(Hero->GetActorLocation() - CraftLocation(Craft, Now));
			const FVector Reach = Craft.Size * 0.5f + FVector(42.0f, 42.0f, 96.0f);
			if (FMath::Abs(Offset.X) <= Reach.X && FMath::Abs(Offset.Y) <= Reach.Y && FMath::Abs(Offset.Z) <= Reach.Z) {
				LastCraftHit.Add(Hero, Now);
				Hero->LaunchCharacter(Craft.LaneDirection * (Craft.Speed * 0.6f) + FVector(0.0f, 0.0f, 380.0f), true, true);
				Hero->TakeHit(CraftDamage, nullptr);
				break;
			}
		}
	}
}
