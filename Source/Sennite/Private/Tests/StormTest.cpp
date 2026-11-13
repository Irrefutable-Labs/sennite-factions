// Storm check. Run in the game on the test range, whose storm has its safe circle centred ahead of
// the start. Tools/check_storm.sh starts the game with the storm set to begin with one player and to
// run its stages in seconds, so a whole hold-and-shrink cycle passes during the check:
// the hero starts inside the circle and takes no damage there; the circle holds, then shrinks by the
// part of its radius its settings say, then holds again; the hero turns round with the mouse and runs
// out through the wall, and outside loses health every second while the HUD warns her; she turns and
// runs back in and the damage stops.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "SenniteCharacter.h"
#include "SenniteHUD.h"
#include "StormActor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogStormCheck, Log, All);

namespace
{
	class FStormRun : public IAutomationLatentCommand
	{
	public:
		explicit FStormRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, Inside, WaitForShrink, Shrinking, Held, TurnRound, RunOut, Outside, RunBack, BackInside, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		static double FromCentre(const ASenniteCharacter* Hero, const AStormActor* Storm)
		{
			return FVector::Dist2D(Hero->GetActorLocation(), Storm->GetActorLocation());
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		double RadiusAtStart = 0.0;
		double RadiusBefore = 0.0;
		double ShrinkStarted = 0.0;
		double RadiusAfter = 0.0;
		float HealthMark = 0.0f;
		int32 Frames = 0;
	};

	bool FStormRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASenniteHUD* Hud = Controller ? Cast<ASenniteHUD>(Controller->GetHUD()) : nullptr;
		AStormActor* Storm = nullptr;
		if (World != nullptr) {
			TActorIterator<AStormActor> It(World);
			Storm = It ? *It : nullptr;
		}

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || Hud == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("No ASenniteCharacter standing on the ground after 60 s")) : false;
			}
			if (Storm == nullptr) {
				return Fail(TEXT("The map has no storm"));
			}
			// a run without a window starts with a stray mouse movement; begin from the view behind the hero
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			RadiusAtStart = Storm->StartRadius;
			HealthMark = Hero->Health;
			Test->TestTrue(TEXT("The storm has its wall"), Storm->Wall->GetStaticMesh() != nullptr && Storm->Wall->GetMaterial(0) != nullptr);
			Test->TestTrue(TEXT("The hero knows the storm"), Hero->CurrentStorm == Storm);
			Test->TestTrue(TEXT("The hero starts well inside the safe circle"), FromCentre(Hero, Storm) < Storm->GetRadius() - 500.0);
			Test->TestTrue(TEXT("The storm is set to run in seconds for this check (run it with Tools/check_storm.sh)"), Storm->StageSeconds <= 2.0f);
			Enter(EPhase::Inside, World);
			return false;
		}
		if (Hero == nullptr || Hud == nullptr || Storm == nullptr) {
			return Fail(TEXT("The hero, the HUD or the storm went away during the check"));
		}
		const double Radius = Storm->GetRadius();

		switch (Phase) {
		case EPhase::Inside:
			// three seconds inside: three of the storm's once-a-second damage turns pass with nothing taken
			if (Elapsed(World) < 3.2) {
				return false;
			}
			Test->TestTrue(TEXT("Inside the circle the hero is not in the storm"), !Hero->InStorm);
			Test->TestEqual(TEXT("Inside the circle the hero takes no damage"), Hero->Health, HealthMark);
			Test->TestTrue(TEXT("Inside the circle the HUD gives no storm warning"), !Hud->LastDrawn.Contains(TEXT("storm=IN THE STORM")));
			Enter(EPhase::WaitForShrink, World);
			return false;

		case EPhase::WaitForShrink:
			// wait for a hold: the circle not shrinking; then for the moment it starts to
			if (Step == 0) {
				if (!Storm->IsShrinking) {
					Step = 1;
					RadiusBefore = Radius;
				}
				return Elapsed(World) > 20.0 ? Fail(TEXT("The storm never held")) : false;
			}
			if (!Storm->IsShrinking) {
				Test->TestEqual(TEXT("While it holds, the circle keeps its radius"), Radius, RadiusBefore);
				RadiusBefore = Radius;
				return Elapsed(World) > 30.0 ? Fail(TEXT("The storm never started to shrink: did the match's storm start?")) : false;
			}
			ShrinkStarted = World->GetTimeSeconds();
			Enter(EPhase::Shrinking, World);
			return false;

		case EPhase::Shrinking:
			if (Storm->IsShrinking) {
				return Elapsed(World) > 20.0 ? Fail(TEXT("The storm never stopped shrinking")) : false;
			}
			{
				const double Seconds = World->GetTimeSeconds() - ShrinkStarted;
				const double Expected = RadiusBefore * FMath::Pow(Storm->KeptPerSecond, Seconds);
				RadiusAfter = Radius;
				Test->TestTrue(TEXT("The circle shrank"), RadiusAfter < RadiusBefore - 50.0);
				Test->TestTrue(TEXT("The circle shrank for three stages"), FMath::Abs(Seconds - 3.0 * Storm->StageSeconds) < 0.5);
				Test->TestTrue(TEXT("The circle shrank by the part of its radius its settings say, by the clock"), FMath::Abs(RadiusAfter - Expected) < 0.02 * RadiusBefore);
				UE_LOG(LogStormCheck, Display, TEXT("GAMECHECK shrink: radius %.0f -> %.0f in %.2f s (expected %.0f; placed at %.0f)"), RadiusBefore, RadiusAfter, Seconds, Expected, RadiusAtStart);
			}
			Enter(EPhase::Held, World);
			return false;

		case EPhase::Held:
			if (Elapsed(World) < 0.5) {
				return false;
			}
			Test->TestEqual(TEXT("After shrinking, the circle holds again"), Radius, RadiusAfter);
			Test->TestTrue(TEXT("The hero is still inside the smaller circle"), !Hero->InStorm);
			Enter(EPhase::TurnRound, World);
			return false;

		case EPhase::TurnRound:
			// half a turn with the mouse, to face the wall behind her
			if (Frames < 30) {
				++Frames;
				GameCheck::Mouse(Controller, EKeys::MouseX, 34.3f);
				return false;
			}
			if (Elapsed(World) < 0.8) {
				return false;
			}
			GameCheck::Shot(TEXT("Storm"), TEXT("wall_ahead.png"));
			GameCheck::Key(Controller, EKeys::W, IE_Pressed);
			GameCheck::Key(Controller, EKeys::LeftShift, IE_Pressed);
			Enter(EPhase::RunOut, World);
			return false;

		case EPhase::RunOut:
			// run until she is through the wall and a little beyond
			if (!(Hero->InStorm && FromCentre(Hero, Storm) > Radius + 150.0)) {
				return Elapsed(World) > 15.0 ? Fail(FString::Printf(TEXT("The hero never left the circle (%.0f from its centre, radius %.0f)"), FromCentre(Hero, Storm), Radius)) : false;
			}
			GameCheck::Key(Controller, EKeys::LeftShift, IE_Released);
			GameCheck::Key(Controller, EKeys::W, IE_Released);
			HealthMark = Hero->Health;
			Enter(EPhase::Outside, World);
			return false;

		case EPhase::Outside:
			if (Elapsed(World) < 3.3) {
				return false;
			}
			{
				const float Lost = HealthMark - Hero->Health;
				Test->TestTrue(TEXT("Outside the circle the hero is in the storm"), Hero->InStorm);
				Test->TestTrue(TEXT("Outside the circle the hero loses the storm's damage every second"), Lost >= 3.0f * Storm->Damage - 0.01f && Lost <= 4.0f * Storm->Damage + 0.01f);
				Test->TestTrue(TEXT("Outside the circle the HUD warns of the storm"), Hud->LastDrawn.Contains(TEXT("storm=IN THE STORM")));
				Test->TestTrue(TEXT("The HUD shows the health that is left"), Hud->LastDrawn.Contains(FString::Printf(TEXT("health=%d"), FMath::RoundToInt(Hero->Health))));
				UE_LOG(LogStormCheck, Display, TEXT("GAMECHECK outside: health %.0f -> %.0f in %.1f s, %.0f beyond the wall"), HealthMark, Hero->Health, Elapsed(World), FromCentre(Hero, Storm) - Radius);
			}
			GameCheck::Shot(TEXT("Storm"), TEXT("outside.png"));
			Frames = 0;
			Enter(EPhase::RunBack, World);
			return false;

		case EPhase::RunBack:
			// half a turn again, then a run back through the wall and far enough inside that the
			// circle, which goes on shrinking in this fast run, does not pass her while she stands
			if (Frames < 30) {
				++Frames;
				GameCheck::Mouse(Controller, EKeys::MouseX, 34.3f);
				return false;
			}
			if (Step == 0) {
				Step = 1;
				GameCheck::Key(Controller, EKeys::W, IE_Pressed);
				GameCheck::Key(Controller, EKeys::LeftShift, IE_Pressed);
				return false;
			}
			if (Hero->InStorm || FromCentre(Hero, Storm) > Radius - 1200.0) {
				return Elapsed(World) > 20.0 ? Fail(TEXT("The hero never got back inside the circle")) : false;
			}
			GameCheck::Key(Controller, EKeys::LeftShift, IE_Released);
			GameCheck::Key(Controller, EKeys::W, IE_Released);
			HealthMark = Hero->Health;
			Enter(EPhase::BackInside, World);
			return false;

		case EPhase::BackInside:
			if (Elapsed(World) < 3.2) {
				return false;
			}
			Test->TestTrue(TEXT("The hero is back inside the circle"), !Hero->InStorm && FromCentre(Hero, Storm) < Radius);
			Test->TestEqual(TEXT("Back inside the circle the damage stops"), Hero->Health, HealthMark);
			Test->TestTrue(TEXT("Back inside the circle the HUD's warning goes"), !Hud->LastDrawn.Contains(TEXT("storm=IN THE STORM")));
			UE_LOG(LogStormCheck, Display, TEXT("GAMECHECK back inside: health holds at %.0f"), Hero->Health);
			UE_LOG(LogStormCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteStormTest, "Sennite.Storm.ShrinksAndHurtsOutside",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteStormTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStormRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
