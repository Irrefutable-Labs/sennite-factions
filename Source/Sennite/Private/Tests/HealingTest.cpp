// Healing check. Run in the game on the test range, which has a pack of bandages and potions lying
// behind the start and a storm whose wall crosses the range farther back: the hero turns round with
// the mouse and runs out through the wall, picking both items up on the way; outside she loses
// health; she runs back in, and with the real keys drinks a potion, which adds shield, and uses a
// bandage, which gives back health. Each takes one from its count, the item sits in her hand, and the
// HUD shows the item, its count, and the health and shield as they change.
//
// Tools/check_healing.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "HealingActor.h"
#include "SenniteCharacter.h"
#include "SenniteHealing.h"
#include "SenniteHUD.h"
#include "SennitePlayerState.h"
#include "StormActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogHealingCheck, Log, All);

namespace
{
	class FHealingRun : public IAutomationLatentCommand
	{
	public:
		explicit FHealingRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, TurnRound, RunOut, Outside, RunBack, Potion, Bandage, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; Turn.Reset(); }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		void Tap(APlayerController* Controller, const FKey& InKey) const
		{
			GameCheck::Key(Controller, InKey, IE_Pressed);
			GameCheck::Key(Controller, InKey, IE_Released);
		}
		void CheckHeld(ASenniteCharacter* Hero, UClass* ItemClass, const TCHAR* What)
		{
			AHealingActor* Held = Hero->CurrentHealingItem;
			Test->TestTrue(FString::Printf(TEXT("The hero holds the %s"), What), Held != nullptr && Held->GetClass() == ItemClass);
			if (Held == nullptr) {
				return;
			}
			Test->TestTrue(FString::Printf(TEXT("The %s is fixed to the hero's item bone"), What), Held->ItemMesh->GetAttachParent() == Hero->GetMesh() && Held->ItemMesh->GetAttachSocketName() == Hero->HeroItemBone);
			Test->TestTrue(FString::Printf(TEXT("The %s has its mesh and shows"), What), Held->ItemMesh->GetStaticMesh() != nullptr && Held->ItemMesh->IsVisible());
			const double FromHand = FVector::Dist(Held->ItemMesh->GetComponentLocation(), Hero->GetMesh()->GetSocketLocation(Hero->HeroItemBone));
			Test->TestTrue(FString::Printf(TEXT("The %s is in the hero's hand"), What), FromHand < 20.0);
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		GameCheck::FMouseTurn Turn;
		float HealthMark = 0.0f;
		float ShieldMark = 0.0f;
		int32 CountMark = 0;
		double NearestBandage = 1.0e9;
		double NearestPotion = 1.0e9;
	};

	bool FHealingRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		ASenniteHUD* Hud = Controller ? Cast<ASenniteHUD>(Controller->GetHUD()) : nullptr;

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || Hud == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("No ASenniteCharacter standing on the ground after 60 s")) : false;
			}
			if (Hero->CurrentStorm == nullptr) {
				return Fail(TEXT("The map has no storm to be hurt by"));
			}
			// a run without a window starts with a stray mouse movement; begin from the view behind the hero
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			Test->TestEqual(TEXT("The hero starts at full health"), Hero->Health, 100.0f);
			Test->TestTrue(TEXT("The hero starts with no healing items"), State->HealingItemCounts[0] == 0 && State->HealingItemCounts[1] == 0);
			// with none to hold, the bandage key does nothing
			Tap(Controller, EKeys::Four);
			Enter(EPhase::TurnRound, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr || Hud == nullptr || Hero->CurrentStorm == nullptr) {
			return Fail(TEXT("The hero, the HUD or the storm went away during the check"));
		}
		const AStormActor* Storm = Hero->CurrentStorm;
		const double FromCentre = FVector::Dist2D(Hero->GetActorLocation(), Storm->GetActorLocation());

		switch (Phase) {
		case EPhase::TurnRound:
			// half a turn with the mouse, to face the items and the wall behind her
			if (!Turn.Step(Controller, 180.0)) {
				return false;
			}
			if (Elapsed(World) < 0.8) {
				return false;
			}
			Test->TestTrue(TEXT("With none to hold, the bandage key puts nothing in her hand"), Hero->CurrentHealingItem == nullptr);
			GameCheck::Key(Controller, EKeys::W, IE_Pressed);
			GameCheck::Key(Controller, EKeys::LeftShift, IE_Pressed);
			Enter(EPhase::RunOut, World);
			return false;

		case EPhase::RunOut:
			// how close she passes to each item still on the ground
			for (TActorIterator<AHealingActor> It(World); It; ++It) {
				if (It->Holder == nullptr) {
					double& Nearest = It->HealingType == 0 ? NearestBandage : NearestPotion;
					Nearest = FMath::Min(Nearest, FVector::Dist2D(It->GetActorLocation(), Hero->GetActorLocation()));
				}
			}
			if (!(Hero->InStorm && FromCentre > Storm->GetRadius() + 150.0)) {
				return Elapsed(World) > 15.0 ? Fail(TEXT("The hero never left the storm's circle")) : false;
			}
			GameCheck::Key(Controller, EKeys::LeftShift, IE_Released);
			GameCheck::Key(Controller, EKeys::W, IE_Released);
			{
				const ASenniteBandage* Bandages = GetDefault<ASenniteBandage>();
				const ASennitePotion* Potions = GetDefault<ASennitePotion>();
				Test->TestEqual(TEXT("Running over the bandages picks up the pack"), State->HealingItemCounts[0], Bandages->Count);
				Test->TestEqual(TEXT("Running over the potions picks them up"), State->HealingItemCounts[1], Potions->Count);
				int32 Left = 0;
				for (TActorIterator<AHealingActor> It(World); It; ++It) {
					Left += It->Holder == nullptr ? 1 : 0;
				}
				Test->TestEqual(TEXT("Both pickups are gone from the ground"), Left, 0);
				UE_LOG(LogHealingCheck, Display, TEXT("GAMECHECK run out: passed within %.0f of the bandages and %.0f of the potions; ended at %s facing %.1f"),
					NearestBandage, NearestPotion, *Hero->GetActorLocation().ToCompactString(), Controller->GetControlRotation().Yaw);
			}
			Enter(EPhase::Outside, World);
			return false;

		case EPhase::Outside:
			// long enough outside to lose more than one bandage gives back
			if (Elapsed(World) < 17.3) {
				return false;
			}
			Test->TestTrue(TEXT("Outside the circle the hero has lost more health than one bandage gives back"), Hero->Health <= 84.0f && Hero->Health >= 70.0f);
			Enter(EPhase::RunBack, World);
			return false;

		case EPhase::RunBack:
			if (!Turn.Step(Controller, 180.0)) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				GameCheck::Key(Controller, EKeys::W, IE_Pressed);
				GameCheck::Key(Controller, EKeys::LeftShift, IE_Pressed);
				return false;
			}
			if (Hero->InStorm || FromCentre > Storm->GetRadius() - 400.0) {
				return Elapsed(World) > 15.0 ? Fail(TEXT("The hero never got back inside the circle")) : false;
			}
			GameCheck::Key(Controller, EKeys::LeftShift, IE_Released);
			GameCheck::Key(Controller, EKeys::W, IE_Released);
			Enter(EPhase::Potion, World);
			return false;

		case EPhase::Potion:
			// the potions were picked up last, so one is in her hand
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				CheckHeld(Hero, ASennitePotion::StaticClass(), TEXT("potion"));
				ShieldMark = Hero->Shield;
				HealthMark = Hero->Health;
				CountMark = State->HealingItemCounts[1];
				Test->TestTrue(TEXT("The HUD names the potion in hand"), Hud->LastDrawn.Contains(TEXT("item=POTION")));
				Test->TestTrue(TEXT("The HUD shows how many potions are left"), Hud->LastDrawn.Contains(FString::Printf(TEXT("itemcount=%d"), CountMark)));
				Tap(Controller, EKeys::LeftMouseButton);
				return false;
			}
			if (Elapsed(World) < 2.0) {
				return false;
			}
			if (Step == 1) {
				Step = 2;
				Test->TestEqual(TEXT("A potion takes its time: no shield yet a second in"), Hero->Shield, ShieldMark);
				Test->TestEqual(TEXT("Drinking takes one potion at once"), State->HealingItemCounts[1], CountMark - 1);
				return false;
			}
			if (Elapsed(World) < 5.0) {
				return false;
			}
			Test->TestEqual(TEXT("A potion adds 20 shield"), Hero->Shield, ShieldMark + 20.0f);
			Test->TestEqual(TEXT("A potion does not touch health"), Hero->Health, HealthMark);
			Test->TestTrue(TEXT("The HUD shows the new shield"), Hud->LastDrawn.Contains(FString::Printf(TEXT("shield=%d"), FMath::RoundToInt(Hero->Shield))));
			UE_LOG(LogHealingCheck, Display, TEXT("GAMECHECK potion: shield %.0f -> %.0f, potions %d -> %d"), ShieldMark, Hero->Shield, CountMark, State->HealingItemCounts[1]);
			Tap(Controller, EKeys::Four);
			Enter(EPhase::Bandage, World);
			return false;

		case EPhase::Bandage:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				CheckHeld(Hero, ASenniteBandage::StaticClass(), TEXT("bandage"));
				HealthMark = Hero->Health;
				ShieldMark = Hero->Shield;
				CountMark = State->HealingItemCounts[0];
				Test->TestTrue(TEXT("The HUD names the bandage in hand"), Hud->LastDrawn.Contains(TEXT("item=BANDAGE")));
				Tap(Controller, EKeys::LeftMouseButton);
				return false;
			}
			if (Elapsed(World) < 5.0) {
				return false;
			}
			Test->TestTrue(TEXT("The hero is still short of full health by more than a bandage gives"), HealthMark + 15.0f < 100.0f);
			Test->TestEqual(TEXT("A bandage gives back 15 health"), Hero->Health, HealthMark + 15.0f);
			Test->TestEqual(TEXT("A bandage does not touch shield"), Hero->Shield, ShieldMark);
			Test->TestEqual(TEXT("Using a bandage takes one from the pack"), State->HealingItemCounts[0], CountMark - 1);
			Test->TestTrue(TEXT("The HUD shows the new health"), Hud->LastDrawn.Contains(FString::Printf(TEXT("health=%d"), FMath::RoundToInt(Hero->Health))));
			Test->TestTrue(TEXT("The HUD shows how many bandages are left"), Hud->LastDrawn.Contains(FString::Printf(TEXT("itemcount=%d"), CountMark - 1)));
			UE_LOG(LogHealingCheck, Display, TEXT("GAMECHECK bandage: health %.0f -> %.0f, bandages %d -> %d"), HealthMark, Hero->Health, CountMark, State->HealingItemCounts[0]);
			UE_LOG(LogHealingCheck, Display, TEXT("GAMECHECK hud: %s"), *FString::Join(Hud->LastDrawn, TEXT(" ")));
			GameCheck::Shot(TEXT("Healing"), TEXT("healed.png"));
			Enter(EPhase::Done, World);
			return false;

		case EPhase::Done:
			// half a turn of the camera for a look at her from the front, the bandage in her hand
			if (Elapsed(World) < 0.3) {
				return false;
			}
			if (!Turn.Step(Controller, 180.0)) {
				return false;
			}
			if (Step == 0 && Elapsed(World) > 1.5) {
				Step = 1;
				GameCheck::Shot(TEXT("Healing"), TEXT("front.png"));
			}
			if (Elapsed(World) < 2.2) {
				return false;
			}
			UE_LOG(LogHealingCheck, Display, TEXT("GAMECHECK finished"));
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteHealingTest, "Sennite.Healing.HurtShieldAndHeal",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteHealingTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHealingRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
