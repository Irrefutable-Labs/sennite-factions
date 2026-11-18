// In-game menu check. Run in the game on the test range: Escape (or MENU on a touch screen) opens the
// menu over the game; while it is open the hero takes nothing the player presses. SETTINGS changes
// how far the view turns, whether up looks down, the volume and the graphics quality; each is in
// effect at once and kept in the player's profile. The phone's back button goes back, RESUME closes
// it, and the hero is the player's again. QUIT GAME and MAIN MENU ask first; MAIN MENU then leaves
// for the first screen.
//
// Tools/check_game_menu.sh runs this, by keys and by touch, on a profile of its own.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SenniteCharacter.h"
#include "SenniteGameMenu.h"
#include "SenniteHeroes.h"
#include "SenniteMenu.h"
#include "SennitePlayerController.h"
#include "SenniteTouch.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameMenuCheck, Log, All);

namespace
{
	class FGameMenuRun : public IAutomationLatentCommand
	{
	public:
		explicit FGameMenuRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForGame, LookBefore, Open, Still, Settings, Changed, Back, LookAfter, Walk, Ask, Leave, Left, Done };

		void Enter(EPhase Next) { Phase = Next; PhaseStart = FPlatformTime::Seconds(); Step = 0; }
		double Elapsed() const { return FPlatformTime::Seconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		/** Where under Saved/ this run's screenshots go: one folder by keys, another by touch. */
		static const TCHAR* Folder() { return USenniteTouchControls::Wanted() ? TEXT("GameMenuTouch") : TEXT("GameMenu"); }

		/** Tap the button the menu's last frame drew for Action, through the game's viewport as a finger would. */
		void Tap(USenniteGameMenu* Menu, FName Action)
		{
			FVector2D Where;
			if (!Menu->Centre(Action, Where)) {
				Test->AddError(FString::Printf(TEXT("The menu drew no button for %s"), *Action.ToString()));
				return;
			}
			GameCheck::Tap(Where);
		}

		static FString Drawn(const USenniteGameMenu* Menu, const TCHAR* Name)
		{
			const FString Prefix = FString(Name) + TEXT("=");
			for (const FString& Line : Menu->LastDrawn) {
				if (Line.StartsWith(Prefix)) {
					return Line.RightChop(Prefix.Len());
				}
			}
			return FString();
		}

		bool Has(const USenniteGameMenu* Menu, FName Action, bool bEnabled)
		{
			for (const FSenniteMenuButton& Each : Menu->Buttons) {
				if (Each.Action == Action) {
					return Each.bEnabled == bEnabled;
				}
			}
			return false;
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForGame;
		double PhaseStart = 0.0;
		int32 Step = 0;
		FRotator LookStart = FRotator::ZeroRotator;
		double YawBefore = 0.0;
		double PitchBefore = 0.0;
		FVector Stood = FVector::ZeroVector;
		int32 QualityBefore = -1;
	};

	bool FGameMenuRun::Update()
	{
		if (FPlatformTime::Seconds() - RealStart > 240.0) {
			return Fail(TEXT("The in-game menu check did not finish in 240 s"));
		}
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		if (Phase == EPhase::Left) {
			// the first screen: its own controller, on the menu map
			if (Cast<ASenniteMenuController>(Controller) == nullptr || !World->GetMapName().Contains(TEXT("Menu"))) {
				return Elapsed() > 60.0 ? Fail(TEXT("LEAVE did not bring the player to the main menu in 60 s")) : false;
			}
			if (Elapsed() < 1.0) {
				return false;
			}
			GameCheck::Shot(Folder(), TEXT("left.png"));
			UE_LOG(LogGameMenuCheck, Display, TEXT("GAMECHECK at the main menu on %s"), *World->GetMapName());
			UE_LOG(LogGameMenuCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;
		}
		ASennitePlayerController* Player = Cast<ASennitePlayerController>(Controller);
		ASenniteCharacter* Hero = Player != nullptr ? Cast<ASenniteCharacter>(Player->GetPawn()) : nullptr;
		USenniteGameMenu* Menu = Player != nullptr ? Player->GetGameMenu() : nullptr;
		if (Phase == EPhase::WaitForGame) {
			if (Hero == nullptr || Menu == nullptr || World->GetTimeSeconds() < 2.5) {
				return false;
			}
			USenniteProfile& Profile = USenniteProfile::Get();
			Test->TestEqual(TEXT("The view turns at the game's own measure until the player sets it"), Profile.LookSensitivity, 1.0f);
			Test->TestFalse(TEXT("Up looks up until the player sets it"), Profile.bInvertLook);
			Test->TestEqual(TEXT("The game is at full volume until the player sets it"), Profile.Volume, 1.0f);
			Test->TestFalse(TEXT("The menu is closed until asked for"), Menu->IsOpen());
			Player->SetControlRotation(FRotator(0.0, Hero->GetActorRotation().Yaw, 0.0));
			Enter(EPhase::LookBefore);
			return false;
		}
		if (Hero == nullptr || Menu == nullptr) {
			return Fail(TEXT("The hero or the menu went away during the check"));
		}
		USenniteProfile& Profile = USenniteProfile::Get();

		switch (Phase) {
		case EPhase::LookBefore:
			// how far a movement of the mouse turns the view before anything is set
			if (Step == 0) {
				if (Elapsed() < 0.3) {
					return false;
				}
				Step = 1;
				LookStart = Player->GetControlRotation();
				GameCheck::Mouse(Player, EKeys::MouseX, 40.0f);
				GameCheck::Mouse(Player, EKeys::MouseY, 20.0f);
				PhaseStart = FPlatformTime::Seconds();
				return false;
			}
			if (Elapsed() < 0.4) {
				return false;
			}
			YawBefore = FMath::FindDeltaAngleDegrees(LookStart.Yaw, Player->GetControlRotation().Yaw);
			PitchBefore = FMath::FindDeltaAngleDegrees(LookStart.Pitch, Player->GetControlRotation().Pitch);
			Test->TestTrue(TEXT("The mouse turns the view"), FMath::Abs(YawBefore) > 1.0 && FMath::Abs(PitchBefore) > 0.5);
			UE_LOG(LogGameMenuCheck, Display, TEXT("GAMECHECK look before: yaw %.2f pitch %.2f"), YawBefore, PitchBefore);
			// Escape on a keyboard; played by touch, the MENU button on the screen
			if (!GameCheck::Press(Player, EKeys::Escape)) {
				return Fail(TEXT("No control on the touch screen opens the menu"));
			}
			Enter(EPhase::Open);
			return false;

		case EPhase::Open:
			if (Elapsed() < 0.6) {
				return false;
			}
			Test->TestTrue(TEXT("Escape, or MENU by touch, opens the menu"), Menu->IsOpen());
			Test->TestEqual(TEXT("It opens on its first page"), Drawn(Menu, TEXT("title")), FString(TEXT("MENU")));
			for (const TCHAR* Each : { TEXT("resume"), TEXT("settings"), TEXT("mainmenu"), TEXT("quit") }) {
				Test->TestTrue(FString::Printf(TEXT("The menu has %s to tap"), Each), Has(Menu, Each, true));
			}
			// this run has no publisher's server: the store and the base are there, and say why they do nothing
			Test->TestTrue(TEXT("STORE is dimmed with no publisher's server"), Has(Menu, TEXT("store"), false));
			Test->TestTrue(TEXT("BASE is dimmed with no publisher's server"), Has(Menu, TEXT("base"), false));
			Test->TestEqual(TEXT("The menu says why"), Drawn(Menu, TEXT("why")), FString(TEXT("THE STORE AND YOUR BASE NEED THE PUBLISHER'S SERVER")));
			Test->TestTrue(TEXT("The mouse pointer shows while the menu is open"), Player->bShowMouseCursor);
			UE_LOG(LogGameMenuCheck, Display, TEXT("GAMECHECK menu: %s"), *FString::Join(Menu->LastDrawn, TEXT(" | ")));
			GameCheck::Shot(Folder(), TEXT("menu.png"));
			Stood = Hero->GetActorLocation();
			LookStart = Player->GetControlRotation();
			GameCheck::Key(Player, EKeys::W, IE_Pressed);
			GameCheck::Mouse(Player, EKeys::MouseX, 40.0f);
			Enter(EPhase::Still);
			return false;

		case EPhase::Still:
			if (Elapsed() < 0.7) {
				return false;
			}
			GameCheck::Key(Player, EKeys::W, IE_Released);
			Test->TestTrue(TEXT("The hero does not walk while the menu is open"), Hero->GetActorLocation().Equals(Stood, 2.0));
			Test->TestTrue(TEXT("The view does not turn while the menu is open"), FMath::Abs(FMath::FindDeltaAngleDegrees(LookStart.Yaw, Player->GetControlRotation().Yaw)) < 0.1);
			Tap(Menu, TEXT("settings"));
			Enter(EPhase::Settings);
			return false;

		case EPhase::Settings:
			if (Elapsed() < 0.5) {
				return false;
			}
			Test->TestTrue(TEXT("SETTINGS opens the settings"), Menu->Page == ESenniteGameMenuPage::Settings && Drawn(Menu, TEXT("title")) == TEXT("SETTINGS"));
			Test->TestEqual(TEXT("The settings show how the view turns"), Drawn(Menu, TEXT("sens")), FString(TEXT("1.0")));
			Test->TestEqual(TEXT("The settings show that up looks up"), Drawn(Menu, TEXT("button:invert")), FString(TEXT("OFF")));
			Test->TestEqual(TEXT("The settings show the volume"), Drawn(Menu, TEXT("vol")), FString(TEXT("100%")));
			QualityBefore = USenniteGameMenu::QualityNow();
			UE_LOG(LogGameMenuCheck, Display, TEXT("GAMECHECK quality before: %d (%s)"), QualityBefore, *Drawn(Menu, TEXT("quality")));
			for (int32 Times = 0; Times < 5; ++Times) {
				Tap(Menu, TEXT("sens+"));
			}
			Tap(Menu, TEXT("invert"));
			for (int32 Times = 0; Times < 3; ++Times) {
				Tap(Menu, TEXT("vol-"));
			}
			Tap(Menu, QualityBefore == 0 ? TEXT("quality+") : TEXT("quality-"));
			Enter(EPhase::Changed);
			return false;

		case EPhase::Changed: {
			if (Elapsed() < 0.6) {
				return false;
			}
			if (Step == 1) {
				// the picture is taken of the frame after it is asked for: the page is left only then
				// the phone's back button goes back a page
				GameCheck::Key(Player, EKeys::Android_Back, IE_Pressed);
				GameCheck::Key(Player, EKeys::Android_Back, IE_Released);
				Enter(EPhase::Back);
				return false;
			}
			Test->TestTrue(TEXT("Five taps on more turn the view half as far again"), FMath::IsNearlyEqual(Profile.LookSensitivity, 1.5f, 0.001f) && Drawn(Menu, TEXT("sens")) == TEXT("1.5"));
			Test->TestTrue(TEXT("INVERT LOOK is on"), Profile.bInvertLook && Drawn(Menu, TEXT("button:invert")) == TEXT("ON"));
			Test->TestTrue(TEXT("Three taps on less bring the volume to 70%"), FMath::IsNearlyEqual(Profile.Volume, 0.7f, 0.001f) && Drawn(Menu, TEXT("vol")) == TEXT("70%"));
			const int32 QualityWant = QualityBefore == 0 ? 1 : (QualityBefore < 0 ? 0 : FMath::Min(QualityBefore, 3) - 1);
			Test->TestEqual(TEXT("The graphics quality chosen is kept"), Profile.Quality, QualityWant);
			Test->TestEqual(TEXT("The graphics quality chosen is the one in effect"), USenniteGameMenu::QualityNow(), QualityWant);
			static const TCHAR* Names[] = { TEXT("LOW"), TEXT("MEDIUM"), TEXT("HIGH"), TEXT("EPIC") };
			Test->TestEqual(TEXT("The settings show the quality in effect"), Drawn(Menu, TEXT("quality")), FString(Names[FMath::Clamp(QualityWant, 0, 3)]));
			// kept on this machine: what its save slot holds now
			FString Slot;
			if (!FParse::Value(FCommandLine::Get(), TEXT("ProfileSlot="), Slot) || Slot.IsEmpty()) {
				Slot = TEXT("Profile");
			}
			const USenniteProfile* Kept = Cast<USenniteProfile>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
			Test->TestTrue(TEXT("The settings are kept in the player's profile"), Kept != nullptr && FMath::IsNearlyEqual(Kept->LookSensitivity, 1.5f, 0.001f) && Kept->bInvertLook
				&& FMath::IsNearlyEqual(Kept->Volume, 0.7f, 0.001f) && Kept->Quality == QualityWant);
			UE_LOG(LogGameMenuCheck, Display, TEXT("GAMECHECK settings: %s"), *FString::Join(Menu->LastDrawn, TEXT(" | ")));
			GameCheck::Shot(Folder(), TEXT("settings.png"));
			Step = 1;
			PhaseStart = FPlatformTime::Seconds();
			return false;
		}

		case EPhase::Back:
			if (Elapsed() < 0.4) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				Test->TestTrue(TEXT("The back button goes back to the menu's first page"), Menu->IsOpen() && Menu->Page == ESenniteGameMenuPage::Main);
				Tap(Menu, TEXT("resume"));
				PhaseStart = FPlatformTime::Seconds();
				return false;
			}
			Test->TestFalse(TEXT("RESUME closes the menu"), Menu->IsOpen());
			Test->TestFalse(TEXT("The mouse pointer is hidden again in play"), Player->bShowMouseCursor);
			Player->SetControlRotation(FRotator(0.0, Player->GetControlRotation().Yaw, 0.0));
			Enter(EPhase::LookAfter);
			return false;

		case EPhase::LookAfter: {
			if (Step == 0) {
				if (Elapsed() < 0.3) {
					return false;
				}
				Step = 1;
				LookStart = Player->GetControlRotation();
				GameCheck::Mouse(Player, EKeys::MouseX, 40.0f);
				GameCheck::Mouse(Player, EKeys::MouseY, 20.0f);
				PhaseStart = FPlatformTime::Seconds();
				return false;
			}
			if (Elapsed() < 0.4) {
				return false;
			}
			const double Yaw = FMath::FindDeltaAngleDegrees(LookStart.Yaw, Player->GetControlRotation().Yaw);
			const double Pitch = FMath::FindDeltaAngleDegrees(LookStart.Pitch, Player->GetControlRotation().Pitch);
			UE_LOG(LogGameMenuCheck, Display, TEXT("GAMECHECK look after: yaw %.2f pitch %.2f"), Yaw, Pitch);
			Test->TestTrue(TEXT("The same movement of the mouse turns the view half as far again"), FMath::IsNearlyEqual(Yaw, YawBefore * 1.5, FMath::Abs(YawBefore) * 0.05));
			Test->TestTrue(TEXT("Up looks down, half as far again"), FMath::IsNearlyEqual(Pitch, -PitchBefore * 1.5, FMath::Abs(PitchBefore) * 0.05));
			Stood = Hero->GetActorLocation();
			GameCheck::Key(Player, EKeys::W, IE_Pressed);
			Enter(EPhase::Walk);
			return false;
		}

		case EPhase::Walk:
			if (Elapsed() < 0.8) {
				return false;
			}
			GameCheck::Key(Player, EKeys::W, IE_Released);
			Test->TestTrue(TEXT("The hero walks again once the menu is closed"), FVector::Dist2D(Hero->GetActorLocation(), Stood) > 30.0);
			if (!GameCheck::Press(Player, EKeys::Escape)) {
				return Fail(TEXT("No control on the touch screen opens the menu a second time"));
			}
			Enter(EPhase::Ask);
			return false;

		case EPhase::Ask:
			if (Elapsed() < 0.5) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				Test->TestTrue(TEXT("The menu opens a second time"), Menu->IsOpen() && Menu->Page == ESenniteGameMenuPage::Main);
				Tap(Menu, TEXT("quit"));
				PhaseStart = FPlatformTime::Seconds();
				return false;
			}
			if (Step == 1) {
				Step = 2;
				Test->TestTrue(TEXT("QUIT GAME asks first"), Menu->Page == ESenniteGameMenuPage::Quit && Drawn(Menu, TEXT("title")) == TEXT("QUIT THE GAME?") && Has(Menu, TEXT("quitnow"), true));
				GameCheck::Shot(Folder(), TEXT("quit.png"));
				PhaseStart = FPlatformTime::Seconds();
				return false;
			}
			if (Step == 2) {
				Step = 3;
				Tap(Menu, TEXT("stay"));
				PhaseStart = FPlatformTime::Seconds();
				return false;
			}
			Test->TestTrue(TEXT("STAY goes back to the menu"), Menu->IsOpen() && Menu->Page == ESenniteGameMenuPage::Main);
			Tap(Menu, TEXT("mainmenu"));
			Enter(EPhase::Leave);
			return false;

		case EPhase::Leave:
			if (Elapsed() < 0.5) {
				return false;
			}
			Test->TestTrue(TEXT("MAIN MENU asks first"), Menu->Page == ESenniteGameMenuPage::Leave && Drawn(Menu, TEXT("title")) == TEXT("LEAVE THE GAME?") && Has(Menu, TEXT("leave"), true));
			Test->TestEqual(TEXT("It says where the player goes"), Drawn(Menu, TEXT("under")), FString(TEXT("You go back to the main menu.")));
			Tap(Menu, TEXT("leave"));
			Enter(EPhase::Left);
			return false;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteGameMenuTest, "Sennite.GameMenu.OpensSetsAndLeaves",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteGameMenuTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGameMenuRun(this));
	return true;
}

#endif
