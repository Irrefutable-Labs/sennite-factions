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
// The overview picture is taken from a camera put above the connector for the purpose; the player
// has no such view.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogArenaCheck, Log, All);

namespace
{
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

	/** Follows a connector's way across with the keys and the mouse. */
	struct FRouteWalker
	{
		int32 Next = 0;
		bool bForward = false;
		bool bRunning = false;
		int32 WaitingForLane = INDEX_NONE;
		FVector From = FVector::ZeroVector;
		bool bBegun = false;
		int32 Jumps = 0;
		int32 LanesCrossed = 0;
		bool bJumpHeld = false;

		void Forward(APlayerController* Controller, bool bGo, bool bRun)
		{
			if (bGo != bForward) {
				bForward = bGo;
				GameCheck::Key(Controller, EKeys::W, bGo ? IE_Pressed : IE_Released);
			}
			if ((bGo && bRun) != bRunning) {
				bRunning = bGo && bRun;
				GameCheck::Key(Controller, EKeys::LeftShift, bRunning ? IE_Pressed : IE_Released);
			}
		}

		/** Whether a sprint across a lane, begun now, meets no craft. */
		static bool LaneIsClear(const ASenniteArena* Arena, int32 ConnectorIndex, int32 Lane, const FVector& Crossing)
		{
			const double Now = Arena->MatchSeconds();
			for (const FArenaCraft& Craft : Arena->GetPlan().Craft) {
				if (Craft.Connector != ConnectorIndex || Craft.Lane != Lane) {
					continue;
				}
				const double Where = FVector::DotProduct(Crossing - Craft.LaneStart, Craft.LaneDirection);
				// From a standing start 320 short of the lane's middle, a sprint is in the craft's path from
				// about 0.45 s to about 1.0 s after setting off. A little is added at each end and each side.
				for (double Ahead = 0.4; Ahead <= 1.06; Ahead += 0.03) {
					const double Along = FVector::DotProduct(ASenniteArena::CraftLocation(Craft, Now + Ahead) - Craft.LaneStart, Craft.LaneDirection);
					if (FMath::Abs(Along - Where) < Craft.Size.Y * 0.5 + 42.0 + 60.0) {
						return false;
					}
				}
			}
			return true;
		}

		/** One step along the way. True once the last point is reached. */
		bool Step(APlayerController* Controller, ASenniteCharacter* Hero, const ASenniteArena* Arena, int32 ConnectorIndex)
		{
			const FArenaConnector& Connector = Arena->GetPlan().Connectors[ConnectorIndex];
			const TArray<FArenaStep>& Route = Connector.Route;
			if (!bBegun) {
				bBegun = true;
				From = Hero->GetActorLocation();
			}
			if (Next >= Route.Num()) {
				Forward(Controller, false, false);
				return true;
			}
			const FVector Here = Hero->GetActorLocation();
			if (bJumpHeld && Hero->GetCharacterMovement()->IsFalling()) {
				bJumpHeld = false;
				GameCheck::Key(Controller, EKeys::SpaceBar, IE_Released);
			}
			// a point to jump from is passed through on the way to the landing after it
			const bool bTakeOff = Route[Next].bJump && Next + 1 < Route.Num();
			const FVector Target = bTakeOff ? Route[Next + 1].Point : Route[Next].Point;

			// reached: the hero has passed the point, measured along the way to it
			const FVector Leg = (Route[Next].Point - From).GetSafeNormal2D();
			const double LegLength = FVector::Dist2D(Route[Next].Point, From);
			const bool bReached = FVector::DotProduct(Here - From, Leg) >= LegLength - (bTakeOff ? 0.0 : 25.0) || FVector::Dist2D(Here, Route[Next].Point) < 30.0;
			if (bReached) {
				if (bTakeOff) {
					// held until she is off the ground: a press let go in the same frame is never seen
					GameCheck::Key(Controller, EKeys::SpaceBar, IE_Pressed);
					bJumpHeld = true;
					++Jumps;
				}
				if (Route[Next].Lane != INDEX_NONE) {
					WaitingForLane = Route[Next].Lane;
				}
				From = Route[Next].Point;
				++Next;
				return false;
			}

			// face the target with the mouse
			const double Want = (Target - Here).Rotation().Yaw;
			const double Error = FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Yaw, Want);
			if (FMath::Abs(Error) > 0.5) {
				GameCheck::Mouse(Controller, EKeys::MouseX, FMath::Clamp(Error / 0.25, -40.0, 40.0));
			}

			if (WaitingForLane != INDEX_NONE) {
				// stand, facing across, until a sprint started now would meet no craft
				if (FMath::Abs(Error) < 3.0 && Hero->GetVelocity().Size2D() < 5.0 && LaneIsClear(Arena, ConnectorIndex, WaitingForLane, Target)) {
					WaitingForLane = INDEX_NONE;
					++LanesCrossed;
				}
				else {
					Forward(Controller, false, false);
					return false;
				}
			}
			// walk where the way has jumps, which are sized for a walk; run elsewhere. In the air, keep going.
			const bool bHasJumps = Route.ContainsByPredicate([](const FArenaStep& Each) { return Each.bJump; });
			const bool bInAir = Hero->GetCharacterMovement()->IsFalling();
			Forward(Controller, bInAir || FMath::Abs(Error) < 6.0, !bHasJumps);
			return false;
		}
	};

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
		FRouteWalker Walker;
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
