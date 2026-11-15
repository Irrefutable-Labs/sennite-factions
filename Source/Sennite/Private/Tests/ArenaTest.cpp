// Arena check. Run in the game on the arena map (Tools/check_arena.sh starts it once for each kind
// of connector, with ?Connector= naming the kind).
//
// First, on plans alone, over hundreds of seeds and every number of bases: the same seed gives the
// same arena; different seeds give different arenas; connectors that meet at a base are never the
// same kind, and no kind appears twice in a match of up to six bases; every kind turns up; and every
// jump a floating connector asks for is inside what a walking jump reaches.
//
// Then, in the running match: the arena built here is the plan for the server's seed; the hero
// starts on the first base; and with the real keys and mouse she follows the connector's way across
// to the far base, jumping where it is a jump and waiting for a clear lane where craft cross, and
// arrives alive.
//
// Of the land: on plans, that each plot's pad is level and the land round it is hills; that the
// ground meets every connector's floor; that a gorge goes under the water and the other kinds are
// ground; that the way out of a plot is dry ground a hero can walk; and that past the shore is the
// lake's bed. In the running match, that the ground the arena built is where the plan's land is.
//
// Of the trees and bushes on the plots: on plans, that every plot has them on its land, off its pad,
// clear of every way out, each tree with its trunk block, and the rest of the arena unchanged by
// them; and in the running match, that a tree's trunk stops a line run through it and a bush does not.
//
// And of the country round the arena (Tools/import_land.py), on this machine, which draws it: the
// country and its water are drawn; nothing of them can be stood on or hit; the lake's surface, read
// from the water's own mesh under the middle of the arena, lies as far below the plots as the
// settings say; the map's haze lies on it and a fall ends just under it; and where the arena is,
// the country's land is under the water.
//
// The overview picture is taken from a camera put above the connector for the purpose; the player
// has no such view.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "RouteWalker.h"
#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "Camera/CameraActor.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogArenaCheck, Log, All);

namespace
{
	/**
	 * The land of the arena, held against plans for many seeds. Each plot's pad is level. The land
	 * round it is not: it has hills. Where a connector meets a plot the ground lies just under the
	 * connector's floor, across its whole width. Between plots a river or a gorge goes down under the
	 * water and the other kinds are ground to walk on. The straight way from a plot's middle to where its
	 * connector's way across begins is ground a hero can walk, and dry. And past the shore the ground
	 * is the lake's bed.
	 */
	void CheckGround(FAutomationTestBase* Test)
	{
		const FArenaSizes Size = ASenniteArena::Sizes();
		const double Water = -Size.WaterDepth;
		bool bPadsLevel = true, bHilly = true, bMouthsMeet = true, bBetween = true, bWalkable = true, bShore = true;
		double LeastHills = TNumericLimits<double>::Max();
		double SteepestWay = 0.0;
		double ShallowestBed = -TNumericLimits<double>::Max();
		for (int32 Seed = 1; Seed <= 40; ++Seed) {
			for (int32 Bases = 2; Bases <= static_cast<int32>(EConnectorKind::Count); ++Bases) {
				const FArenaPlan Plan = ASenniteArena::MakePlan(Seed, Bases, INDEX_NONE, Size);
				const auto High = [&Plan](const FVector& At) { return ASenniteArena::GroundHeight(Plan, At.X, At.Y); };
				FVector Middle = FVector::ZeroVector;
				for (const FVector& Base : Plan.Bases) {
					Middle += Base / Plan.Bases.Num();
				}
				for (const FVector& Base : Plan.Bases) {
					double Low = TNumericLimits<double>::Max(), Top = -TNumericLimits<double>::Max();
					for (double Turn = 0.0; Turn < 360.0; Turn += 15.0) {
						const FVector Way = FRotator(0.0, Turn, 0.0).Vector();
						bPadsLevel = bPadsLevel && FMath::Abs(High(Base + Way * Size.PadRadius * 0.5)) < 0.01 && FMath::Abs(High(Base + Way * Size.PadRadius * 0.99)) < 0.01;
						// the plot's land past its pad, where it is not falling to a gorge or the shore
						for (double Out = Size.PadRadius + 2500.0; Out <= Size.BaseRadius; Out += 500.0) {
							// (the hills are still rising from the pad here; farther out they are at their full height)
							const FVector At = Base + Way * Out;
							const double Here = High(At);
							if (ASenniteArena::OutsideLand(Plan, At.X, At.Y) <= 0.0 && Here > -200.0) {
								Low = FMath::Min(Low, Here);
								Top = FMath::Max(Top, Here);
							}
						}
					}
					bPadsLevel = bPadsLevel && FMath::Abs(High(Base)) < 0.01;
					LeastHills = FMath::Min(LeastHills, Top - Low);
					bHilly = bHilly && Top - Low >= 200.0;
					// past the shore, away from the rest of the arena: the lake's bed
					const FVector Away = (Base - Middle).GetSafeNormal2D();
					const double Bed = High(Base + Away * (Size.BaseRadius + ASenniteArena::CoastReach + ASenniteArena::ShoreWidth + 500.0));
					ShallowestBed = FMath::Max(ShallowestBed, Bed);
					bShore = bShore && Bed < Water - 700.0;
				}
				for (const FArenaConnector& Connector : Plan.Connectors) {
					const bool bGorge = Connector.Kind == EConnectorKind::Floating || Connector.Kind == EConnectorKind::Bridges;
					for (const double Across : { -Size.ConnectorWidth * 0.5 + 300.0, 0.0, Size.ConnectorWidth * 0.5 - 300.0 }) {
						for (const double Along : { -300.0, Size.ConnectorLength + 300.0 }) {
							const double Here = High(Connector.Frame.TransformPosition(FVector(Along, Across, 0.0)));
							bMouthsMeet = bMouthsMeet && Here < -2.0 && Here > -12.0;
						}
						const double Half = High(Connector.Frame.TransformPosition(FVector(Size.ConnectorLength * 0.5, Across, 0.0)));
						bBetween = bBetween && (bGorge ? Half < Water - 700.0 : (Half < -2.0 && Half > -12.0));
					}
					// from each plot's middle straight to where the way across begins, or ends, on its side
					const FVector Ends[2][2] = {
						{ Plan.Bases[Connector.FromBase], Connector.Route[0].Point },
						{ Plan.Bases[Connector.ToBase], Connector.Route[Connector.Route.Num() - 2].Point },
					};
					for (const FVector* End : Ends) {
						const double Far = FVector::Dist2D(End[0], End[1]);
						double Before = High(End[0]);
						for (double Gone = 100.0; Gone <= Far; Gone += 100.0) {
							const double Here = High(FMath::Lerp(End[0], End[1], Gone / Far));
							SteepestWay = FMath::Max(SteepestWay, FMath::Abs(Here - Before) / 100.0);
							bWalkable = bWalkable && FMath::Abs(Here - Before) / 100.0 < 0.7 && Here > Water + 500.0;
							Before = Here;
						}
					}
				}
			}
		}
		Test->TestTrue(TEXT("Each plot's pad, where a faction's buildings stand, is level"), bPadsLevel);
		Test->TestTrue(TEXT("The land of every plot past its pad is not flat: its hills rise and fall 2 m or more"), bHilly);
		Test->TestTrue(TEXT("Where a connector meets a plot the ground lies just under the connector's floor, across its width"), bMouthsMeet);
		Test->TestTrue(TEXT("Between plots a river or a gorge goes down under the water, and the other kinds are ground"), bBetween);
		Test->TestTrue(TEXT("From a plot's middle straight to its connector's way across is ground under 35 degrees steep and well above the water"), bWalkable);
		Test->TestTrue(TEXT("Past the shore the ground is the lake's bed, under the water"), bShore);
		UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK ground: over 200 plans the flattest plot's hills span %.1f m; the steepest step on a way out is %.0f degrees; past the shore the bed is %.1f m under the water at its shallowest"),
			LeastHills / 100.0, FMath::RadiansToDegrees(FMath::Atan(SteepestWay)), (Water - ShallowestBed) / 100.0);
	}

	/**
	 * The country round the arena, on a machine that draws it. The arena made its two parts and they
	 * are drawn; nothing of them can be stood on or hit. The lake's surface, read from the water's own
	 * triangle under the middle of the arena, lies WaterDepth below the plots' level ground, so the
	 * arena's land rises out of it. The map's haze lies on that lake, and a fall ends just under its
	 * surface. And where the arena is, the country is lake: under a sixteen-sided outline drawn
	 * round the widest arena and its shore, the country's land, read from its own triangles cut down
	 * to that outline, does not come up to the water.
	 */
	void CheckLand(FAutomationTestBase* Test, UWorld* World, const ASenniteArena* Arena)
	{
		const ASenniteArena* Rules = GetDefault<ASenniteArena>();
		const double Water = -Rules->WaterDepth;
		const int32 Wanted = (Rules->Land.IsNull() ? 0 : 1) + (Rules->LandWater.IsNull() ? 0 : 1);
		if (Wanted == 0) {
			UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK country: none is set; round the arena is haze"));
			return;
		}
		const TArray<TObjectPtr<UStaticMeshComponent>>& Parts = Arena->GetLand();
		if (!Test->TestEqual(TEXT("The arena made a part for each mesh of the country that is set"), Parts.Num(), Wanted)) {
			return;
		}

		// how far from the middle the widest arena's land reaches, shore and all
		const FArenaPlan Widest = ASenniteArena::MakePlan(1, static_cast<int32>(EConnectorKind::Count));
		double Reach = 0.0;
		for (const FVector& Base : Widest.Bases) {
			Reach = FMath::Max(Reach, Base.Size2D() + Arena->BaseRadius + ASenniteArena::CoastReach + ASenniteArena::ShoreWidth);
		}

		const double Outer = Reach / FMath::Cos(UE_PI / 16.0);
		const UObject* WaterMesh = Rules->LandWater.ResolveObject();
		const double Unread = TNumericLimits<double>::Max();
		double Highest = -Unread;
		double Lake = Unread;
		for (const UStaticMeshComponent* Part : Parts) {
			Test->TestTrue(TEXT("The country round the arena and its water are drawn"),
				Part->IsRegistered() && Part->ShouldRender() && (!FApp::CanEverRender() || Part->SceneProxy != nullptr));
			Test->TestTrue(TEXT("Nothing of the country can be stood on or hit"), Part->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
			const FStaticMeshRenderData* Data = Part->GetStaticMesh() ? Part->GetStaticMesh()->GetRenderData() : nullptr;
			if (!Test->TestTrue(TEXT("The country's mesh can be read"), Data != nullptr && Data->LODResources.Num() > 0)) {
				continue;
			}
			const FStaticMeshLODResources& Lod = Data->LODResources[0];
			const FPositionVertexBuffer& Points = Lod.VertexBuffers.PositionVertexBuffer;
			const FIndexArrayView Corners = Lod.IndexBuffer.GetArrayView();
			Test->TestTrue(TEXT("The country's mesh has triangles to read"), Points.GetNumVertices() > 0 && Corners.Num() >= 3);
			const bool bWater = WaterMesh != nullptr && Part->GetStaticMesh() == WaterMesh;
			const FTransform Placed = Part->GetComponentTransform();
			for (int32 Corner = 0; Corner + 2 < Corners.Num(); Corner += 3) {
				FVector At[3];
				FBox2D Covers(ForceInit);
				for (int32 Each = 0; Each < 3; ++Each) {
					At[Each] = Placed.TransformPosition(FVector(Points.VertexPosition(Corners[Corner + Each])));
					Covers += FVector2D(At[Each].X, At[Each].Y);
				}
				// the part of a triangle of the country's land that lies under the arena: the triangle cut down
				// to a sixteen-sided outline drawn round the arena's reach; a flat piece is highest at a corner
				if (!bWater && Covers.Min.X <= Outer && Covers.Max.X >= -Outer && Covers.Min.Y <= Outer && Covers.Max.Y >= -Outer) {
					TArray<FVector, TInlineAllocator<24>> Piece(At, 3);
					for (int32 Side = 0; Side < 16 && Piece.Num() > 0; ++Side) {
						const FVector Outward = FRotator(0.0, Side * 22.5, 0.0).Vector();
						TArray<FVector, TInlineAllocator<24>> Cut;
						for (int32 Each = 0; Each < Piece.Num(); ++Each) {
							const FVector& From = Piece[Each];
							const FVector& To = Piece[(Each + 1) % Piece.Num()];
							const double FromOut = FVector::DotProduct(From, Outward) - Reach;
							const double ToOut = FVector::DotProduct(To, Outward) - Reach;
							if (FromOut <= 0.0) {
								Cut.Add(From);
							}
							if ((FromOut <= 0.0) != (ToOut <= 0.0)) {
								Cut.Add(FMath::Lerp(From, To, FromOut / (FromOut - ToOut)));
							}
						}
						Piece = MoveTemp(Cut);
					}
					for (const FVector& Each : Piece) {
						Highest = FMath::Max(Highest, Each.Z);
					}
				}
				// the water's triangle over the middle of the arena: its height there is the lake's
				if (bWater && Covers.IsInside(FVector2D::ZeroVector)) {
					const double Den = (At[1].Y - At[2].Y) * (At[0].X - At[2].X) + (At[2].X - At[1].X) * (At[0].Y - At[2].Y);
					if (FMath::Abs(Den) > UE_DOUBLE_SMALL_NUMBER) {
						const double U = ((At[1].Y - At[2].Y) * (0.0 - At[2].X) + (At[2].X - At[1].X) * (0.0 - At[2].Y)) / Den;
						const double V = ((At[2].Y - At[0].Y) * (0.0 - At[2].X) + (At[0].X - At[2].X) * (0.0 - At[2].Y)) / Den;
						if (U >= 0.0 && V >= 0.0 && U + V <= 1.0) {
							Lake = U * At[0].Z + V * At[1].Z + (1.0 - U - V) * At[2].Z;
						}
					}
				}
			}
		}
		if (WaterMesh != nullptr) {
			Test->TestTrue(TEXT("The lake under the middle of the arena lies WaterDepth below the plots' level ground, to within a metre"),
				Lake != Unread && FMath::Abs(Lake - Water) < 100.0);
		}
		Test->TestTrue(TEXT("Some of the country's land lies under the arena"), Highest != -Unread);
		Test->TestTrue(TEXT("Where the arena and its shore are, the country is lake: its land there is under the water"), Highest < Water);

		// The map's haze and the height a fall ends at are set when the map is made (Tools/build_arena.py);
		// the country is set when the arena is built. A map made for another depth shows here.
		TActorIterator<AExponentialHeightFog> Haze(World);
		if (Test->TestTrue(TEXT("The map has its haze"), static_cast<bool>(Haze) && Haze->GetComponent() != nullptr)) {
			Test->TestTrue(TEXT("The map's haze lies on the lake (after changing Land or WaterDepth, make the map again with Tools/build_arena.py)"),
				FMath::Abs(Haze->GetComponent()->GetComponentLocation().Z - Water) < 1.0);
		}
		Test->TestTrue(TEXT("A fall ends 1.5 m under the lake's surface (after changing WaterDepth, make the map again with Tools/build_arena.py)"),
			FMath::Abs(World->GetWorldSettings()->KillZ - (Water - 150.0)) < 1.0);
		UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK country: the lake under the middle lies %.1f m below the plots; within %.0f m of the middle the country's land is %.0f m under the water at its highest"),
			Lake == Unread ? 0.0 : -Lake / 100.0, Reach / 100.0, (Water - Highest) / 100.0);
	}

	/**
	 * The land as the arena built it here: there is ground, it is what stops a line from above each
	 * plot's middle, at the pad's height; and at each bush, which stops nothing itself, the ground is
	 * where the plan says the land is, to within the 40 cm a mesh of points 5 m apart can be out.
	 */
	void CheckGroundBuilt(FAutomationTestBase* Test, UWorld* World, const ASenniteArena* Arena)
	{
		const UPrimitiveComponent* Ground = Cast<UPrimitiveComponent>(Arena->GetGround());
		if (!Test->TestTrue(TEXT("The arena built its ground, and it is drawn and solid"),
			Ground != nullptr && Ground->IsRegistered() && Ground->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics && (!FApp::CanEverRender() || Ground->SceneProxy != nullptr))) {
			return;
		}
		const FArenaPlan& Plan = Arena->GetPlan();
		bool bPads = true, bHills = true;
		double Furthest = 0.0;
		int32 Read = 0;
		const auto Under = [World](const FVector& At, FHitResult& Hit) {
			return World->LineTraceSingleByChannel(Hit, FVector(At.X, At.Y, 6000.0), FVector(At.X, At.Y, -6000.0), ECC_Visibility);
		};
		for (const FVector& Base : Plan.Bases) {
			FHitResult Hit;
			bPads = bPads && Under(Base, Hit) && Hit.GetComponent() == Ground && FMath::Abs(Hit.Location.Z) < 1.0;
		}
		for (const FArenaProp& Piece : Plan.Props) {
			if (Piece.Scenery < EArenaScenery::BushA) {
				continue;
			}
			FHitResult Hit;
			const FVector At = Piece.Transform.GetLocation();
			const bool bOnGround = Under(At, Hit) && Hit.GetComponent() == Ground;
			if (bOnGround) {
				Furthest = FMath::Max(Furthest, FMath::Abs(Hit.Location.Z - At.Z));
				++Read;
			}
			bHills = bHills && bOnGround && FMath::Abs(Hit.Location.Z - At.Z) < 40.0;
		}
		Test->TestTrue(TEXT("Under each plot's middle is the ground, at the pad's height"), bPads);
		Test->TestTrue(TEXT("At every bush the ground is where the plan says the land is"), bHills && Read > 0);
		UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK ground built: read under %d plots and %d bushes; the ground is within %.0f cm of the plan's land at each"), Plan.Bases.Num(), Read, Furthest);
	}

	bool IsTree(const FArenaProp& Piece)
	{
		return Piece.Scenery >= EArenaScenery::TreeA && Piece.Scenery < EArenaScenery::BushA;
	}

	bool IsBush(const FArenaProp& Piece)
	{
		return Piece.Scenery >= EArenaScenery::BushA && Piece.Scenery < EArenaScenery::Count;
	}

	/**
	 * The trees and bushes round the bases, held against plans for many seeds: every base has trees
	 * and bushes; each stands on its plot's land, off its pad, on the ground; none stands in a way out; trees
	 * stand apart; every tree has its trunk block; and the rest of the arena (its bases, connectors,
	 * ways across, craft, blocks and other scenery) is what the seed makes when the settings ask for
	 * no trees or bushes.
	 */
	void CheckGreen(FAutomationTestBase* Test)
	{
		const FArenaSizes Size = ASenniteArena::Sizes();
		FArenaSizes Bare = Size;
		Bare.TreesPerBase = 0;
		Bare.BushesPerBase = 0;
		bool bEveryBaseHasSome = true, bOnTheRim = true, bWaysOutClear = true, bApart = true, bTrunks = true, bRestUnchanged = true, bForest = true;
		int32 Trees = 0, Bushes = 0, Plans = 0, ForestTrees = 0;
		for (int32 Seed = 1; Seed <= 60; ++Seed) {
			for (int32 Bases = 2; Bases <= static_cast<int32>(EConnectorKind::Count); ++Bases) {
				const FArenaPlan Plan = ASenniteArena::MakePlan(Seed, Bases, INDEX_NONE, Size);
				const FArenaPlan Without = ASenniteArena::MakePlan(Seed, Bases, INDEX_NONE, Bare);
				++Plans;
				TArray<int32> TreesOn, BushesOn;
				TreesOn.SetNumZeroed(Bases);
				BushesOn.SetNumZeroed(Bases);
				TArray<FVector> Stood;
				int32 TrunkBlocks = 0;
				int32 GreenPieces = 0;
				for (const FArenaProp& Piece : Plan.Props) {
					if (!IsTree(Piece) && !IsBush(Piece)) {
						continue;
					}
					++GreenPieces;
					const FVector At = Piece.Transform.GetLocation();
					int32 Base = 0;
					for (int32 Each = 1; Each < Bases; ++Each) {
						if (FVector::Dist2D(At, Plan.Bases[Each]) < FVector::Dist2D(At, Plan.Bases[Base])) {
							Base = Each;
						}
					}
					const double Out = FVector::Dist2D(At, Plan.Bases[Base]);
					if (Out > Size.BaseRadius) {
						// not on a plot: a tree of the forest under a tree-top boardwalk, on the ground of its strip, with its trunk
						bool bInForest = false;
						for (const FArenaConnector& Connector : Plan.Connectors) {
							const FVector Local = Connector.Frame.InverseTransformPosition(FVector(At.X, At.Y, 0.0));
							bInForest = bInForest || (Connector.Kind == EConnectorKind::TreeTop && Local.X > 0.0 && Local.X < Size.ConnectorLength && FMath::Abs(Local.Y) < Size.ConnectorWidth * 0.5);
						}
						const bool bTrunk = Plan.Boxes.ContainsByPredicate([&At](const FArenaBox& Block) {
							return Block.Tint == EArenaTint::Hidden && FVector::Dist2D(Block.Centre, At) < 1.0 && FMath::IsNearlyEqual(Block.Centre.Z - Block.Size.Z * 0.5, At.Z, 1.0);
						});
						bForest = bForest && IsTree(Piece) && bInForest && bTrunk && FMath::IsNearlyEqual(At.Z, ASenniteArena::GroundHeight(Plan, At.X, At.Y), 1.0);
						TrunkBlocks += bTrunk ? 1 : 0;
						++ForestTrees;
						continue;
					}
					bOnTheRim = bOnTheRim && Out > Size.PadRadius && Out < Size.BaseRadius && At.Z >= -300.0 && FMath::IsNearlyEqual(At.Z, ASenniteArena::GroundHeight(Plan, At.X, At.Y), 1.0);
					for (const FArenaConnector& Connector : Plan.Connectors) {
						if (Connector.FromBase != Base && Connector.ToBase != Base) {
							continue;
						}
						const FVector Way = (Plan.Bases[Connector.FromBase == Base ? Connector.ToBase : Connector.FromBase] - Plan.Bases[Base]).GetSafeNormal2D();
						const FVector From = At - Plan.Bases[Base];
						bWaysOutClear = bWaysOutClear && !(FVector::DotProduct(From, Way) > 0.0 && FMath::Abs(FVector::CrossProduct(Way, From).Z) < Size.ConnectorWidth * 0.5 + 150.0);
					}
					if (IsTree(Piece)) {
						++TreesOn[Base];
						++Trees;
						bApart = bApart && !Stood.ContainsByPredicate([&At](const FVector& Other) { return FVector::Dist2D(At, Other) < 899.0; });
						Stood.Add(At);
						const bool bHasTrunk = Plan.Boxes.ContainsByPredicate([&At](const FArenaBox& Block) {
							return Block.Tint == EArenaTint::Hidden && FVector::Dist2D(Block.Centre, At) < 1.0 && Block.Size.Z >= 200.0 && FMath::IsNearlyEqual(Block.Centre.Z - Block.Size.Z * 0.5, At.Z, 1.0);
						});
						bTrunks = bTrunks && bHasTrunk;
						TrunkBlocks += bHasTrunk ? 1 : 0;
					}
					else {
						++BushesOn[Base];
						++Bushes;
					}
				}
				for (int32 Base = 0; Base < Bases; ++Base) {
					bEveryBaseHasSome = bEveryBaseHasSome && TreesOn[Base] >= Size.TreesPerBase / 2 && TreesOn[Base] <= Size.TreesPerBase
						&& BushesOn[Base] >= Size.BushesPerBase / 2 && BushesOn[Base] <= Size.BushesPerBase;
				}
				// with the trees and bushes and their trunks taken away, the plan is the bare one
				bRestUnchanged = bRestUnchanged && Plan.Bases == Without.Bases && Plan.Craft.Num() == Without.Craft.Num()
					&& Plan.Boxes.Num() - TrunkBlocks == Without.Boxes.Num() && Plan.Props.Num() - GreenPieces == Without.Props.Num();
				bRestUnchanged = bRestUnchanged && Plan.Connectors.Num() == Without.Connectors.Num();
				for (int32 Link = 0; bRestUnchanged && Link < Without.Connectors.Num(); ++Link) {
					const FArenaConnector& With = Plan.Connectors[Link];
					const FArenaConnector& Bare0 = Without.Connectors[Link];
					bRestUnchanged = With.Kind == Bare0.Kind && With.FromBase == Bare0.FromBase && With.ToBase == Bare0.ToBase && With.Frame.Equals(Bare0.Frame, 0.01) && With.Route.Num() == Bare0.Route.Num();
					for (int32 Step = 0; bRestUnchanged && Step < Bare0.Route.Num(); ++Step) {
						bRestUnchanged = With.Route[Step].Point.Equals(Bare0.Route[Step].Point, 0.01) && With.Route[Step].bJump == Bare0.Route[Step].bJump && With.Route[Step].Lane == Bare0.Route[Step].Lane;
					}
				}
				for (int32 Each = 0; bRestUnchanged && Each < Without.Craft.Num(); ++Each) {
					const FArenaCraft& With = Plan.Craft[Each];
					const FArenaCraft& Bare0 = Without.Craft[Each];
					bRestUnchanged = With.LaneStart.Equals(Bare0.LaneStart, 0.01) && With.LaneDirection.Equals(Bare0.LaneDirection, 0.0001) && With.LaneLength == Bare0.LaneLength && With.Speed == Bare0.Speed
						&& With.Offset == Bare0.Offset && With.Size.Equals(Bare0.Size, 0.01) && With.Rotation.Equals(Bare0.Rotation, 0.0001) && With.Connector == Bare0.Connector && With.Lane == Bare0.Lane;
				}
				for (int32 Block = 0; bRestUnchanged && Block < Without.Boxes.Num(); ++Block) {
					bRestUnchanged = Plan.Boxes[Block].Centre.Equals(Without.Boxes[Block].Centre, 0.01) && Plan.Boxes[Block].Size.Equals(Without.Boxes[Block].Size, 0.01)
						&& Plan.Boxes[Block].Rotation.Equals(Without.Boxes[Block].Rotation, 0.0001) && Plan.Boxes[Block].Tint == Without.Boxes[Block].Tint;
				}
				for (int32 Piece = 0; bRestUnchanged && Piece < Without.Props.Num(); ++Piece) {
					bRestUnchanged = Plan.Props[Piece].Scenery == Without.Props[Piece].Scenery && Plan.Props[Piece].bFit == Without.Props[Piece].bFit && Plan.Props[Piece].Transform.Equals(Without.Props[Piece].Transform, 0.01);
				}
			}
		}
		Test->TestTrue(TEXT("Every base has at least half the trees and bushes the settings ask for, and no more than they ask for"), bEveryBaseHasSome);
		Test->TestTrue(TEXT("Every tree and bush stands on its plot's land, off its pad, on the ground where it is"), bOnTheRim);
		Test->TestTrue(TEXT("No tree or bush stands in a way out of its base"), bWaysOutClear);
		Test->TestTrue(TEXT("Trees stand at least 9 m apart"), bApart);
		Test->TestTrue(TEXT("Every tree has a trunk block standing on the base where it stands"), bTrunks);
		Test->TestTrue(TEXT("Every tree that is not on a plot is one of a forest under a tree-top boardwalk, on the ground, with its trunk"), bForest && ForestTrees > 0);
		Test->TestTrue(TEXT("But for its trees and bushes, an arena is what its seed makes when the settings ask for none"), bRestUnchanged);
		UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK green: %d trees and %d bushes on plots, and %d forest trees, over %d plans"), Trees, Bushes, ForestTrees, Plans);
	}

	/**
	 * The trees of the arena built here: every piece of scenery the settings name loads, so no trunk
	 * block shows; and a tree's trunk stops a line run through it, where a bush stops nothing.
	 */
	void CheckTrees(FAutomationTestBase* Test, UWorld* World, const ASenniteArena* Arena)
	{
		const ASenniteArena* Rules = GetDefault<ASenniteArena>();
		bool bAllLoad = Rules->SceneryMeshes.Num() == static_cast<int32>(EArenaScenery::Count);
		for (const FSoftObjectPath& Named : Rules->SceneryMeshes) {
			bAllLoad = bAllLoad && Cast<UStaticMesh>(Named.TryLoad()) != nullptr;
		}
		Test->TestTrue(TEXT("Every piece of scenery the settings name is there to load"), bAllLoad);
		int32 Trees = 0, Bushes = 0;
		bool bTrunksStop = true, bBushesDoNot = true;
		for (const FArenaProp& Piece : Arena->GetPlan().Props) {
			// a short line, well above the ground's slope: it meets a trunk or nothing
			const FVector At = Piece.Transform.GetLocation() + FVector(0.0, 0.0, 150.0);
			FHitResult Hit;
			const bool bStopped = World->LineTraceSingleByChannel(Hit, At - FVector(120.0, 0.0, 0.0), At + FVector(120.0, 0.0, 0.0), ECC_Visibility);
			if (IsTree(Piece)) {
				++Trees;
				bTrunksStop = bTrunksStop && bStopped && Hit.GetActor() == Arena && Hit.GetComponent() != Cast<UPrimitiveComponent>(Arena->GetGround());
			}
			else if (IsBush(Piece)) {
				++Bushes;
				bBushesDoNot = bBushesDoNot && !bStopped;
			}
		}
		Test->TestTrue(TEXT("The arena built here has trees and bushes"), Trees > 0 && Bushes > 0);
		Test->TestTrue(TEXT("Every tree's trunk stops a line run through it"), bTrunksStop);
		Test->TestTrue(TEXT("No bush stops a line run through it"), bBushesDoNot);
		UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK trees: %d trees and %d bushes on %d bases"), Trees, Bushes, Arena->GetPlan().Bases.Num());
	}

	/** What the seeding promises, held against plans for many seeds. */
	void CheckPlans(FAutomationTestBase* Test)
	{
		const int32 Seeds = 300;
		const int32 Kinds = static_cast<int32>(EConnectorKind::Count);
		bool bRepeatable = true;
		bool bNeighboursDiffer = true;
		bool bNoKindTwice = true;
		bool bEveryConnectorBuilt = true;
		bool bJumpsInReach = true;
		TSet<int32> KindsAsOnlyConnector;
		TSet<FString> OrdersOfFour;
		double LongestJump = 0.0;
		double HighestJump = 0.0;
		for (int32 Bases = 2; Bases <= Kinds; ++Bases) {
			TSet<uint32> Hashes;
			for (int32 Seed = 1; Seed <= Seeds; ++Seed) {
				const FArenaPlan Plan = ASenniteArena::MakePlan(Seed, Bases);
				bRepeatable &= ASenniteArena::MakePlan(Seed, Bases).Hash == Plan.Hash;
				Hashes.Add(Plan.Hash);
				TSet<EConnectorKind> Seen;
				for (int32 Index = 0; Index < Plan.Connectors.Num(); ++Index) {
					const FArenaConnector& Connector = Plan.Connectors[Index];
					bEveryConnectorBuilt &= Connector.Route.Num() >= 2;
					bNeighboursDiffer &= Plan.Connectors.Num() < 2 || Connector.Kind != Plan.Connectors[(Index + 1) % Plan.Connectors.Num()].Kind;
					bNoKindTwice &= !Seen.Contains(Connector.Kind);
					Seen.Add(Connector.Kind);
					for (int32 Step = 0; Step + 1 < Connector.Route.Num(); ++Step) {
						if (Connector.Route[Step].bJump) {
							const FVector Leap = Connector.Route[Step + 1].Point - Connector.Route[Step].Point;
							LongestJump = FMath::Max(LongestJump, Leap.Size2D());
							HighestJump = FMath::Max(HighestJump, Leap.Z);
							// the landing point is 70 in from the edge; the edge itself is nearer
							bJumpsInReach &= Leap.Size2D() <= 330.0 && Leap.Z <= 75.0;
						}
					}
				}
				// counted on the same arena with no trees, whose trunks are blocks too
				FArenaSizes NoTrees = ASenniteArena::Sizes();
				NoTrees.TreesPerBase = 0;
				NoTrees.BushesPerBase = 0;
				bEveryConnectorBuilt &= ASenniteArena::MakePlan(Seed, Bases, INDEX_NONE, NoTrees).Boxes.Num() > 0 && Plan.Bases.Num() == Bases;
				if (Bases == 2) {
					KindsAsOnlyConnector.Add(static_cast<int32>(Plan.Connectors[0].Kind));
				}
				if (Bases == 4) {
					OrdersOfFour.Add(Plan.KindsText());
				}
			}
			Test->TestEqual(FString::Printf(TEXT("With %d bases, %d seeds give %d different arenas"), Bases, Seeds, Seeds), Hashes.Num(), Seeds);
		}
		Test->TestTrue(TEXT("The same seed gives the same arena"), bRepeatable);
		Test->TestTrue(TEXT("Every connector has blocks and a way across"), bEveryConnectorBuilt);
		Test->TestTrue(TEXT("Connectors that meet at a base are never the same kind"), bNeighboursDiffer);
		Test->TestTrue(TEXT("No kind of connector appears twice in a match of up to six bases"), bNoKindTwice);
		Test->TestEqual(TEXT("Every kind turns up as the connector between two bases"), KindsAsOnlyConnector.Num(), Kinds);
		Test->TestTrue(TEXT("Four bases are joined in many different orders of kinds"), OrdersOfFour.Num() >= 150);
		Test->TestTrue(TEXT("Every jump a floating connector asks for is inside a walking jump's reach"), bJumpsInReach);
		UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK plans: %d seeds for each of 2 to %d bases; kinds seen alone %d of %d; orders of four %d; longest jump %.0f, highest %.0f"),
			Seeds, Kinds, KindsAsOnlyConnector.Num(), Kinds, OrdersOfFour.Num(), LongestJump, HighestJump);
	}

	class FArenaRun : public IAutomationLatentCommand
	{
	public:
		explicit FArenaRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, Overview, Cross, Arrived, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		GameCheck::FRouteWalker Walker;
		TWeakObjectPtr<ACameraActor> Above;
		float HealthAtStart = 0.0f;
		bool bMidShot = false;
		double CrossingBegan = 0.0;
		double CrossingTook = 0.0;
	};

	bool FArenaRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		const FArenaSizes Size = ASenniteArena::Sizes();
		ASenniteArena* Arena = nullptr;
		if (World != nullptr) {
			TActorIterator<ASenniteArena> It(World);
			Arena = It ? *It : nullptr;
		}

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("No ASenniteCharacter standing on the ground after 60 s: is this the arena map?")) : false;
			}
			if (Arena == nullptr || !Arena->IsBuilt()) {
				return Fail(TEXT("The map has no built arena (run this with Tools/check_arena.sh)"));
			}
			CheckPlans(Test);
			CheckGround(Test);
			CheckGreen(Test);
			CheckGroundBuilt(Test, World, Arena);
			CheckLand(Test, World, Arena);
			CheckTrees(Test, World, Arena);

			const FArenaPlan& Plan = Arena->GetPlan();
			const FArenaConnector& Connector = Plan.Connectors[0];
			Test->TestEqual(TEXT("The arena built here is the plan for the server's seed"), Plan.Hash, Arena->GetServerHash());
			Test->TestTrue(TEXT("The hero starts on the first base"), FVector::Dist2D(Hero->GetActorLocation(), Plan.Bases[0]) < 100.0);
			Test->TestTrue(TEXT("The hero starts facing the way out of the base"),
				FMath::Abs(FMath::FindDeltaAngleDegrees(Hero->GetActorRotation().Yaw, (Connector.Frame.GetLocation() - Plan.Bases[0]).Rotation().Yaw)) < 2.0);
			HealthAtStart = Hero->Health;
			// a run without a window starts with a stray mouse movement; begin from the view behind the hero
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK arena: seed %d, %d bases, connectors: %s; %d blocks, %d craft, way across in %d points"),
				Plan.Seed, Plan.Bases.Num(), *Plan.KindsText(), Plan.Boxes.Num(), Plan.Craft.Num(), Connector.Route.Num());

			// a camera above the middle of the connector, looking down along it, for a picture of the whole of it
			const FVector Middle = Connector.Frame.TransformPosition(FVector(Size.ConnectorLength * 0.5, 0.0, 0.0));
			const FVector Eye = Connector.Frame.TransformPosition(FVector(-Size.ConnectorLength * 0.25, -Size.ConnectorLength * 1.3, Size.ConnectorLength * 1.35));
			Above = World->SpawnActor<ACameraActor>(Eye, (Middle - Eye).Rotation());
			if (Above.IsValid()) {
				Controller->SetViewTarget(Above.Get());
			}
			Enter(EPhase::Overview, World);
			return false;
		}
		if (Arena == nullptr) {
			return Fail(TEXT("The arena went away during the check"));
		}
		const FArenaPlan& Plan = Arena->GetPlan();
		const FString Kind = ASenniteArena::KindName(Plan.Connectors[0].Kind);

		switch (Phase) {
		case EPhase::Overview:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				GameCheck::Shot(TEXT("Arena"), *FString::Printf(TEXT("%s_from_above.png"), *Kind));
				return false;
			}
			if (Elapsed(World) < 1.8) {
				return false;
			}
			if (Step == 1) {
				// and from the side and below, for the bases and what is under them
				Step = 2;
				const FVector Middle = Plan.Connectors[0].Frame.TransformPosition(FVector(Size.ConnectorLength * 0.5, 0.0, -Size.ConnectorLength * 0.12));
				const FVector Eye = Plan.Connectors[0].Frame.TransformPosition(FVector(Size.ConnectorLength * 0.5, -Size.ConnectorLength * 1.9, Size.ConnectorLength * 0.12));
				if (Above.IsValid()) {
					Above->SetActorLocationAndRotation(Eye, (Middle - Eye).Rotation());
				}
				return false;
			}
			if (Elapsed(World) < 2.6) {
				return false;
			}
			if (Step == 2) {
				Step = 3;
				GameCheck::Shot(TEXT("Arena"), *FString::Printf(TEXT("%s_from_the_side.png"), *Kind));
				return false;
			}
			if (Elapsed(World) < 3.4) {
				return false;
			}
			if (Hero == nullptr) {
				return Fail(TEXT("The hero went away before setting off"));
			}
			Controller->SetViewTarget(Hero);
			if (Above.IsValid()) {
				Above->Destroy();
			}
			Enter(EPhase::Cross, World);
			return false;

		case EPhase::Cross:
			if (Hero == nullptr) {
				return Fail(FString::Printf(TEXT("The hero was lost crossing the %s connector, at point %d of its way across"), *Kind, Walker.Next));
			}
			if (Step == 0 && Elapsed(World) > 0.8) {
				Step = 1;
				GameCheck::Shot(TEXT("Arena"), *FString::Printf(TEXT("%s_setting_off.png"), *Kind));
			}
			if (!bMidShot && Walker.Next >= Plan.Connectors[0].Route.Num() / 2) {
				bMidShot = true;
				GameCheck::Shot(TEXT("Arena"), *FString::Printf(TEXT("%s_half_way.png"), *Kind));
			}
			if (CrossingBegan == 0.0) {
				CrossingBegan = World->GetTimeSeconds();
			}
			if (!Walker.Step(Controller, Hero, Arena, 0)) {
				return Elapsed(World) > 420.0
					? Fail(FString::Printf(TEXT("The hero did not get across the %s connector in 420 s: stopped at point %d of %d, at %s"), *Kind, Walker.Next, Plan.Connectors[0].Route.Num(), *Hero->GetActorLocation().ToCompactString()))
					: false;
			}
			CrossingTook = World->GetTimeSeconds() - CrossingBegan;
			Enter(EPhase::Arrived, World);
			return false;

		case EPhase::Arrived:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Hero == nullptr) {
				return Fail(TEXT("The hero was lost on arriving"));
			}
			Test->TestTrue(FString::Printf(TEXT("The hero crosses the %s connector to the far base"), *Kind),
				FVector::Dist2D(Hero->GetActorLocation(), Plan.Bases[1]) < Size.BaseRadius - 200.0 && Hero->GetCharacterMovement()->IsMovingOnGround());
			Test->TestEqual(FString::Printf(TEXT("The hero arrives unhurt across the %s connector"), *Kind), Hero->Health, HealthAtStart);
			UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK crossed %s in %.0f s: %d jumps, %d lanes waited for, health %.0f, %.0f from the far base's centre"),
				*Kind, CrossingTook, Walker.Jumps, Walker.LanesCrossed, Hero->Health, FVector::Dist2D(Hero->GetActorLocation(), Plan.Bases[1]));
			GameCheck::Shot(TEXT("Arena"), *FString::Printf(TEXT("%s_arrived.png"), *Kind));
			Enter(EPhase::Done, World);
			return false;

		case EPhase::Done:
			if (Elapsed(World) < 0.6) {
				return false;
			}
			UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK finished"));
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteArenaTest, "Sennite.Arena.SeededAndCrossable",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteArenaTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArenaRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
