// HUD check. Run in the game on the test range: the HUD draws the player's health and shield, the
// three building materials, the players left and the kills from the start; in build mode it names
// the piece and the material count drops when a piece is built; beside the range's crate it says how
// to open it, and once opened what the crate gave; with a weapon in hand it names the weapon and
// shows the rounds in it and the rounds carried, and the rounds in it drop when it is fired.
//
// What is checked is each line of text the HUD sent to the canvas on its last frame
// (ASenniteHUD::LastDrawn), after the real keys and mouse changed the game. That proves the HUD reads
// the game rightly; that the text is legible on screen is for the eye, in the screenshots this leaves
// in Saved/Hud/. Tools/check_hud.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "SenniteCharacter.h"
#include "SenniteHealing.h"
#include "SenniteHUD.h"
#include "SenniteWeapons.h"
#include "SennitePlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogHudCheck, Log, All);

namespace
{
	class FHudRun : public IAutomationLatentCommand
	{
	public:
		explicit FHudRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, Start, BuildMode, Built, OpenCrate, Opened, Shotgun, Energy, Sniper, Rifle, Fired, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		void Tap(APlayerController* Controller, const FKey& InKey) const
		{
			GameCheck::Key(Controller, InKey, IE_Pressed);
			GameCheck::Key(Controller, InKey, IE_Released);
		}
		void Shows(const ASenniteHUD* Hud, const TCHAR* Entry, const TCHAR* What)
		{
			Test->TestTrue(FString::Printf(TEXT("The HUD writes %s (%s)"), What, Entry), Hud->LastDrawn.Contains(Entry));
		}
		void Hides(const ASenniteHUD* Hud, const TCHAR* Name, const TCHAR* What)
		{
			const FString Prefix = FString(Name) + TEXT("=");
			const bool bDrawn = Hud->LastDrawn.ContainsByPredicate([&Prefix](const FString& Entry) { return Entry.StartsWith(Prefix); });
			Test->TestTrue(FString::Printf(TEXT("The HUD does not write %s"), What), !bDrawn);
		}
		void Log(const ASenniteHUD* Hud, const TCHAR* When) const
		{
			UE_LOG(LogHudCheck, Display, TEXT("GAMECHECK hud %s: %s"), When, *FString::Join(Hud->LastDrawn, TEXT(" ")));
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		GameCheck::FCrateRun Crate;
		bool bCratePrompt = false;
	};

	bool FHudRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		ASenniteHUD* Hud = Controller ? Cast<ASenniteHUD>(Controller->GetHUD()) : nullptr;

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || Hud == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("No ASenniteCharacter with an ASenniteHUD standing on the ground after 60 s")) : false;
			}
			// a run without a window starts with a stray mouse movement; begin from the view behind the hero
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			Enter(EPhase::Start, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr || Hud == nullptr) {
			return Fail(TEXT("The hero or the HUD went away during the check"));
		}

		switch (Phase) {
		case EPhase::Start:
			if (Elapsed(World) < 0.5) {
				return false;
			}
			Test->TestTrue(TEXT("The HUD draws (needs a renderer: run without -nullrhi)"), Hud->LastDrawn.Num() > 0);
			Shows(Hud, TEXT("health=100"), TEXT("full health"));
			Shows(Hud, TEXT("shield=0"), TEXT("no shield"));
			Shows(Hud, *FString::Printf(TEXT("material0=%d"), State->MaterialCounts[0]), TEXT("the first material's count"));
			Shows(Hud, *FString::Printf(TEXT("material1=%d"), State->MaterialCounts[1]), TEXT("the second material's count"));
			Shows(Hud, *FString::Printf(TEXT("material2=%d"), State->MaterialCounts[2]), TEXT("the third material's count"));
			Shows(Hud, TEXT("alive=1 ALIVE"), TEXT("one player left"));
			Shows(Hud, TEXT("kills=0 KILLS"), TEXT("no kills"));
			Hides(Hud, TEXT("ammo"), TEXT("rounds with no weapon in hand"));
			Hides(Hud, TEXT("mode"), TEXT("a piece name outside build mode"));
			Log(Hud, TEXT("at the start"));
			GameCheck::Shot(TEXT("Hud"), TEXT("start.png"));
			Tap(Controller, EKeys::X);
			Enter(EPhase::BuildMode, World);
			return false;

		case EPhase::BuildMode:
			if (Elapsed(World) < 0.5) {
				return false;
			}
			Shows(Hud, TEXT("mode=WALL"), TEXT("the piece being built"));
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
			Enter(EPhase::Built, World);
			return false;

		case EPhase::Built:
			if (Elapsed(World) < 0.5) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
				Shows(Hud, *FString::Printf(TEXT("material0=%d"), State->MaterialCounts[0]), TEXT("the first material's count after a wall"));
				Test->TestTrue(TEXT("The wall was paid for"), Hud->LastReadout.Materials[0] < Hud->LastReadout.Materials[1]);
				Log(Hud, TEXT("after building a wall"));
				GameCheck::Shot(TEXT("Hud"), TEXT("building.png"));
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Step == 1) {
				Step = 2;
				Tap(Controller, EKeys::X);
				return false;
			}
			if (Elapsed(World) < 1.4) {
				return false;
			}
			Hides(Hud, TEXT("mode"), TEXT("a piece name after leaving build mode"));
			Hides(Hud, TEXT("crate"), TEXT("how to open a crate away from one"));
			Enter(EPhase::OpenCrate, World);
			return false;

		case EPhase::OpenCrate:
			// D walks her to the crate to the right of the start; beside it the HUD says how to open it
			bCratePrompt |= Crate.Stage == 2 && Hud->LastDrawn.Contains(TEXT("crate=E   OPEN THE CRATE"));
			if (!Crate.Step(World, Controller, Hero, EKeys::D)) {
				return false;
			}
			if (!Crate.Failure.IsEmpty()) {
				return Fail(Crate.Failure);
			}
			Test->TestTrue(TEXT("Beside the closed crate the HUD says how to open it"), bCratePrompt);
			Enter(EPhase::Opened, World);
			return false;

		case EPhase::Opened: {
			if (Elapsed(World) < 0.8) {
				return false;
			}
			Hides(Hud, TEXT("crate"), TEXT("how to open a crate that is open"));
			Shows(Hud, *FString::Printf(TEXT("crategave=RIFLE   SHOTGUN   ENERGY RIFLE   SNIPER   +ROUNDS   +%d BANDAGES   +%d POTIONS"),
				GetDefault<ASenniteBandage>()->Count, GetDefault<ASennitePotion>()->Count), TEXT("what the crate gave"));
			Shows(Hud, TEXT("weapon=RIFLE"), TEXT("the rifle the crate gave, in hand"));
			Shows(Hud, *FString::Printf(TEXT("reserve=%d"), GetDefault<ASenniteRifle>()->CrateRounds), TEXT("the rounds the crate gave"));
			Log(Hud, TEXT("after opening the crate"));
			GameCheck::Shot(TEXT("Hud"), TEXT("crate.png"));
			Tap(Controller, EKeys::Three);
			Enter(EPhase::Shotgun, World);
			return false;
		}

		case EPhase::Shotgun:
			if (Elapsed(World) < 0.8) {
				return false;
			}
			Shows(Hud, TEXT("weapon=SHOTGUN"), TEXT("the shotgun in hand"));
			Shows(Hud, TEXT("ammo=5"), TEXT("the shotgun's five rounds"));
			Shows(Hud, *FString::Printf(TEXT("reserve=%d"), GetDefault<ASenniteShotgun>()->CrateRounds), TEXT("the shells the crate gave"));
			Tap(Controller, EKeys::Six);
			Enter(EPhase::Energy, World);
			return false;

		case EPhase::Energy:
			if (Elapsed(World) < 0.8) {
				return false;
			}
			Shows(Hud, TEXT("weapon=ENERGY RIFLE"), TEXT("the energy rifle in hand"));
			Shows(Hud, *FString::Printf(TEXT("ammo=%d"), GetDefault<ASenniteEnergyRifle>()->MagazineSize), TEXT("the energy rifle's full cell"));
			Shows(Hud, *FString::Printf(TEXT("reserve=%d"), GetDefault<ASenniteEnergyRifle>()->CrateRounds), TEXT("the energy rifle's rounds from the crate"));
			Tap(Controller, EKeys::Seven);
			Enter(EPhase::Sniper, World);
			return false;

		case EPhase::Sniper:
			if (Elapsed(World) < 0.8) {
				return false;
			}
			Shows(Hud, TEXT("weapon=SNIPER"), TEXT("the sniper in hand"));
			Shows(Hud, *FString::Printf(TEXT("ammo=%d"), GetDefault<ASenniteSniper>()->MagazineSize), TEXT("the sniper's five rounds"));
			Shows(Hud, *FString::Printf(TEXT("reserve=%d"), GetDefault<ASenniteSniper>()->CrateRounds), TEXT("the sniper's rounds from the crate"));
			Tap(Controller, EKeys::Two);
			Enter(EPhase::Rifle, World);
			return false;

		case EPhase::Rifle:
			if (Elapsed(World) < 0.8) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				Shows(Hud, TEXT("weapon=RIFLE"), TEXT("the rifle in hand"));
				Shows(Hud, TEXT("ammo=30"), TEXT("the rifle's thirty rounds"));
				Log(Hud, TEXT("holding the rifle"));
				GameCheck::Shot(TEXT("Hud"), TEXT("armed.png"));
				return false;
			}
			if (Elapsed(World) < 1.2) {
				return false;
			}
			Tap(Controller, EKeys::LeftMouseButton);
			Enter(EPhase::Fired, World);
			return false;

		case EPhase::Fired:
			if (Elapsed(World) < 0.5) {
				return false;
			}
			Shows(Hud, TEXT("ammo=29"), TEXT("one round fewer after a shot"));
			Log(Hud, TEXT("after one shot"));
			UE_LOG(LogHudCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteHudTest, "Sennite.Hud.FollowsTheGame",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteHudTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHudRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
