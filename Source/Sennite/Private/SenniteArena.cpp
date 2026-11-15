#include "SenniteArena.h"
#include "SenniteCharacter.h"
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
	return Out;
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
	// Trees and bushes on the land of each plot, outside its pad and clear of every way out of it, each
	// standing on the ground where it is. They come from a stream of their own, so everything else of
	// the arena is what its seed makes with none. A tree is scenery over a trunk block, which is what a
	// hero or a shot meets; the block is seen only if the tree is missing. A bush is scenery alone.
	FRandomStream Green(Seed ^ 0x47524E);
	const int32 TreeKinds = static_cast<int32>(EArenaScenery::BushA) - static_cast<int32>(EArenaScenery::TreeA);
	const int32 BushKinds = static_cast<int32>(EArenaScenery::Count) - static_cast<int32>(EArenaScenery::BushA);
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
				if (!Clear(At, 600.0f) || Trunks.ContainsByPredicate([&At](const FVector& Other) { return FVector::Dist2D(At, Other) < 900.0f; })) {
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
				if (!Clear(At, 200.0f) || Trunks.ContainsByPredicate([&At](const FVector& Other) { return FVector::Dist2D(At, Other) < 250.0f; })) {
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
		// past the outline: as far as the coast wanders, then the shore, and a cell more
		const double Past = CoastReach + ShoreWidth + GroundCell;
		const FVector2D Corner = Reaches.Min - FVector2D(Past);
		const int32 Across = FMath::CeilToInt32((Reaches.Max.X - Reaches.Min.X + 2.0 * Past) / GroundCell) + 1;
		const int32 Down = FMath::CeilToInt32((Reaches.Max.Y - Reaches.Min.Y + 2.0 * Past) / GroundCell) + 1;
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
		TArray<int32> Corners;
		const auto Point = [&](int32 Column, int32 Row) {
			int32& Index = Kept[Row * Across + Column];
			if (Index == INDEX_NONE) {
				Index = Points.Num();
				const auto At = [&](int32 C, int32 R) { return High[FMath::Clamp(R, 0, Down - 1) * Across + FMath::Clamp(C, 0, Across - 1)]; };
				Points.Add(FVector(Corner.X + Column * GroundCell, Corner.Y + Row * GroundCell, At(Column, Row)));
				Normals.Add(FVector(At(Column - 1, Row) - At(Column + 1, Row), At(Column, Row - 1) - At(Column, Row + 1), 2.0 * GroundCell).GetSafeNormal());
				Places.Add(FVector2D(Points.Last().X, Points.Last().Y) / 100.0);
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
		Ground->CreateMeshSection_LinearColor(0, Points, Corners, Normals, Places, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), true);
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
			Set = NewObject<UInstancedStaticMeshComponent>(this);
			Set->SetStaticMesh(Mesh);
			Set->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Set->SetCanEverAffectNavigation(false);
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
