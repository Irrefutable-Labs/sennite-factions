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
// And of the land far below (Tools/import_land.py), on this machine, which draws it: the land and its
// water are drawn; nothing of them can be stood on or hit; the lake's surface, read from the water's
// own mesh under the middle of the arena, lies as far below the bases as the settings say, and the
// map's haze lies on it; and under the widest arena there can be (six bases) no triangle of the land
// comes within 50 m of the lowest thing of the arena built here.
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
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogArenaCheck, Log, All);

namespace
{
	/**
	 * The land far below, on a machine that draws it. The arena made its two parts and they are
	 * drawn. Nothing of them can be stood on or hit. The lake's surface, read from the water's own
	 * triangle under the middle of the arena, lies LandDepth below the bases. The map's haze lies on
	 * that lake. And the land is clear of the arena: no triangle of it that reaches under the square
	 * the widest arena fits in (six bases on their ring) has a corner within 50 m of the lowest thing
	 * of the arena built here, which is the rock under a base or whatever this kind of connector
	 * hangs down. A triangle counts whole, so the height it gives is no lower than the truth.
	 */
	void CheckLand(FAutomationTestBase* Test, UWorld* World, const ASenniteArena* Arena)
	{
		const ASenniteArena* Rules = GetDefault<ASenniteArena>();
		const int32 Wanted = (Rules->Land.IsNull() ? 0 : 1) + (Rules->LandWater.IsNull() ? 0 : 1);
		if (Wanted == 0) {
			UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK land: none is set; below the arena is haze"));
			return;
		}
		const TArray<TObjectPtr<UStaticMeshComponent>>& Parts = Arena->GetLand();
		if (!Test->TestEqual(TEXT("The arena made a part for each mesh of the land that is set"), Parts.Num(), Wanted)) {
			return;
		}

		// how far from the middle the widest arena reaches, and how far down this one goes
		const FArenaPlan Widest = ASenniteArena::MakePlan(1, static_cast<int32>(EConnectorKind::Count));
		double Reach = 0.0;
		for (const FVector& Base : Widest.Bases) {
			Reach = FMath::Max(Reach, Base.Size2D() + Arena->BaseRadius);
		}
		double Lowest = 0.0;
		TInlineComponentArray<UPrimitiveComponent*> Drawn(Arena);
		for (const UPrimitiveComponent* Each : Drawn) {
			if (!Parts.Contains(Each)) {
				Lowest = FMath::Min(Lowest, Each->Bounds.GetBox().Min.Z);
			}
		}

		const UObject* WaterMesh = Rules->LandWater.ResolveObject();
		const double Unread = TNumericLimits<double>::Max();
		double Highest = -Unread;
		double Lake = Unread;
		for (const UStaticMeshComponent* Part : Parts) {
			Test->TestTrue(TEXT("The land below the arena and its water are drawn"),
				Part->IsRegistered() && Part->ShouldRender() && (!FApp::CanEverRender() || Part->SceneProxy != nullptr));
			Test->TestTrue(TEXT("Nothing of the land can be stood on or hit"), Part->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
			const FStaticMeshRenderData* Data = Part->GetStaticMesh() ? Part->GetStaticMesh()->GetRenderData() : nullptr;
			if (!Test->TestTrue(TEXT("The land's mesh can be read"), Data != nullptr && Data->LODResources.Num() > 0)) {
				continue;
			}
			const FStaticMeshLODResources& Lod = Data->LODResources[0];
			const FPositionVertexBuffer& Points = Lod.VertexBuffers.PositionVertexBuffer;
			const FIndexArrayView Corners = Lod.IndexBuffer.GetArrayView();
			Test->TestTrue(TEXT("The land's mesh has triangles to read"), Points.GetNumVertices() > 0 && Corners.Num() >= 3);
			const bool bWater = WaterMesh != nullptr && Part->GetStaticMesh() == WaterMesh;
			const FTransform Placed = Part->GetComponentTransform();
			for (int32 Corner = 0; Corner + 2 < Corners.Num(); Corner += 3) {
				FVector At[3];
				FBox2D Covers(ForceInit);
				for (int32 Each = 0; Each < 3; ++Each) {
					At[Each] = Placed.TransformPosition(FVector(Points.VertexPosition(Corners[Corner + Each])));
					Covers += FVector2D(At[Each].X, At[Each].Y);
				}
				// a triangle that reaches under the arena's square counts whole: no higher than its highest corner
				if (Covers.Min.X <= Reach && Covers.Max.X >= -Reach && Covers.Min.Y <= Reach && Covers.Max.Y >= -Reach) {
					Highest = FMath::Max(Highest, FMath::Max3(At[0].Z, At[1].Z, At[2].Z));
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
			Test->TestTrue(TEXT("The lake under the middle of the arena lies LandDepth below the bases, to within a metre"),
				Lake != Unread && FMath::Abs(Lake + Rules->LandDepth) < 100.0);
		}
		Test->TestTrue(TEXT("Some of the land lies under the arena"), Highest != -Unread);
		Test->TestTrue(TEXT("Under the widest arena the land stays at least 50 m below the lowest thing of the arena"), Highest < Lowest - 5000.0);

		// The map's haze is set on the lake when the map is made (Tools/build_arena.py); the land is set
		// there when the arena is built. A map made for another depth shows here.
		TActorIterator<AExponentialHeightFog> Haze(World);
		if (Test->TestTrue(TEXT("The map has its haze"), static_cast<bool>(Haze) && Haze->GetComponent() != nullptr)) {
			Test->TestTrue(TEXT("The map's haze lies on the land's lake (after changing Land or LandDepth, make the map again with Tools/build_arena.py)"),
				FMath::Abs(Haze->GetComponent()->GetComponentLocation().Z + Rules->LandDepth) < 1.0);
		}

		// and a line from under each base straight down past the lake meets nothing
		bool bMetNothing = true;
		for (const FVector& Base : Arena->GetPlan().Bases) {
			FHitResult Hit;
			const FVector From(Base.X, Base.Y, Lowest - 100.0);
			const FVector To(Base.X, Base.Y, -Rules->LandDepth - 50000.0);
			bMetNothing = bMetNothing && !World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility) && !World->LineTraceSingleByChannel(Hit, From, To, ECC_WorldStatic);
		}
		Test->TestTrue(TEXT("A line from under a base down past the lake meets nothing"), bMetNothing);
		UE_LOG(LogArenaCheck, Display, TEXT("GAMECHECK land: the lake under the middle lies %.1f m below the bases; no triangle of the land reaching under the square %.0f m each way from the middle has a corner higher than %.0f m below them; the arena's lowest point is %.0f m below them"),
			Lake == Unread ? 0.0 : -Lake / 100.0, Reach / 100.0, -Highest / 100.0, -Lowest / 100.0);
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
				bEveryConnectorBuilt &= Plan.Boxes.Num() > 0 && Plan.Bases.Num() == Bases;
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
			CheckLand(Test, World, Arena);

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
