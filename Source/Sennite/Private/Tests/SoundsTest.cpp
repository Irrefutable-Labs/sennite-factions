// Sound check. Run in the game on the test range: every sound the game names has recordings and each
// one loads; no sound goes round in the background; the hero's steps are heard as it walks, its jump as it
// leaves the ground and its landing as it meets it again; its own weapon's attack is heard; and the
// in-game menu is heard opening, its buttons and its closing.
//
// What is checked is that the game asked for each sound (USenniteSounds::Heard): a run has no sound
// device (-nosound), so nothing here can say what a sound is like to the ear.
//
// Tools/check_sounds.sh runs this.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BuildingActor.h"
#include "EngineUtils.h"
#include "ProjectileActor.h"
#include "SenniteBuildings.h"
#include "SenniteCharacter.h"
#include "SenniteHeroes.h"
#include "SennitePlayerState.h"
#include "SenniteGameMenu.h"
#include "SennitePlayerController.h"
#include "SenniteSounds.h"

DEFINE_LOG_CATEGORY_STATIC(LogSoundsCheck, Log, All);

namespace
{
	class FSoundsRun : public IAutomationLatentCommand
	{
	public:
		explicit FSoundsRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForGame, Walk, Jump, Land, Attack, Floats, Menu, Settings, Closed, Done };

		void Enter(EPhase Next) { Phase = Next; PhaseStart = FPlatformTime::Seconds(); Step = 0; }
		double Elapsed() const { return FPlatformTime::Seconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForGame;
		double PhaseStart = 0.0;
		int32 Step = 0;
		FVector Began = FVector::ZeroVector;
		int32 Before = 0;
		int32 SwingsBefore = 0;
		int32 ShotsBefore = 0;
		bool bBoltSeen = false;
		TWeakObjectPtr<ABuildingActor> Wall;
		float WallHealth = 0.0f;
	};

	bool FSoundsRun::Update()
	{
		if (FPlatformTime::Seconds() - RealStart > 120.0) {
			return Fail(TEXT("The sound check did not finish in 120 s"));
		}
		UWorld* World = GameCheck::FindGameWorld();
		ASennitePlayerController* Player = World != nullptr ? Cast<ASennitePlayerController>(World->GetFirstPlayerController()) : nullptr;
		ASenniteCharacter* Hero = Player != nullptr ? Cast<ASenniteCharacter>(Player->GetPawn()) : nullptr;
		USenniteGameMenu* Menu = Player != nullptr ? Player->GetGameMenu() : nullptr;
		if (Phase == EPhase::WaitForGame) {
			if (Hero == nullptr || Menu == nullptr || World->GetTimeSeconds() < 2.5) {
				return false;
			}
			// every sound named has its recordings, and each loads
			const TArray<FName> Names = USenniteSounds::Names();
			int32 Takes = 0;
			for (const FName& Name : Names) {
				const int32 Named = USenniteSounds::TakesNamed(Name);
				const int32 Load = USenniteSounds::TakesThatLoad(Name);
				Takes += Load;
				Test->TestTrue(FString::Printf(TEXT("%s has a recording"), *Name.ToString()), Named > 0);
				Test->TestEqual(FString::Printf(TEXT("Every recording of %s loads"), *Name.ToString()), Load, Named);
			}
			// the ones the game's own code asks for by name are in the list
			for (const TCHAR* Name : { TEXT("ui.press"), TEXT("ui.tab"), TEXT("ui.turn"), TEXT("ui.pick"), TEXT("ui.back"), TEXT("ui.confirm"), TEXT("ui.key"), TEXT("ui.step"),
				TEXT("ui.click"), TEXT("ui.select"), TEXT("ui.page"), TEXT("ui.open"), TEXT("ui.denied"),
				TEXT("ui.bought"), TEXT("ui.coins"), TEXT("ui.tick"), TEXT("wallet.connected"), TEXT("hit.marker"), TEXT("attack.own"), TEXT("swing"),
				TEXT("reload.rifle"), TEXT("reload.shotgun"), TEXT("weapon.switch"), TEXT("bolt.hit"), TEXT("body.hit"), TEXT("fell"), TEXT("harvest"),
				TEXT("piece.place"), TEXT("piece.remove"), TEXT("piece.edit"), TEXT("piece.hit.wood"), TEXT("piece.hit.stone"), TEXT("piece.hit.steel"), TEXT("piece.break.wood"), TEXT("piece.break.stone"),
				TEXT("piece.break.steel"), TEXT("pickup.item"), TEXT("pickup.weapon"), TEXT("pickup.rounds"), TEXT("heal.bandage"), TEXT("heal.potion"),
				TEXT("match.start"), TEXT("match.won"), TEXT("match.lost"), TEXT("storm.alarm"), TEXT("air.menu"), TEXT("air.field"), TEXT("air.storm"),
				TEXT("step.grass.walk"), TEXT("step.grass.run"), TEXT("jump.grass"), TEXT("land.grass"), TEXT("step.wood.walk"), TEXT("step.rock.walk"),
				TEXT("step.metal.walk"), TEXT("voice.f.hit"), TEXT("voice.f.pain"), TEXT("voice.m.hit"), TEXT("voice.m.pain") }) {
				Test->TestTrue(FString::Printf(TEXT("The game has a sound called %s"), Name), Names.Contains(FName(Name)));
			}
			Test->TestEqual(TEXT("No sound goes round in the background"), USenniteSounds::HeardBeginning(World, TEXT("air.")), 0);
			UE_LOG(LogSoundsCheck, Display, TEXT("GAMECHECK sounds: %d named, %d recordings load; nothing goes round in the background"), Names.Num(), Takes);
			Player->SetControlRotation(FRotator(0.0, Hero->GetActorRotation().Yaw, 0.0));
			Began = Hero->GetActorLocation();
			Before = USenniteSounds::HeardBeginning(World, TEXT("step."));
			GameCheck::Key(Player, EKeys::W, IE_Pressed);
			Enter(EPhase::Walk);
			return false;
		}
		if (Hero == nullptr || Menu == nullptr) {
			return Fail(TEXT("The hero or the menu went away during the check"));
		}

		switch (Phase) {
		case EPhase::Walk: {
			if (Elapsed() < 2.0) {
				return false;
			}
			GameCheck::Key(Player, EKeys::W, IE_Released);
			const double Went = FVector::Dist2D(Hero->GetActorLocation(), Began);
			const int32 Steps = USenniteSounds::HeardBeginning(World, TEXT("step.")) - Before;
			Test->TestTrue(TEXT("The hero walked"), Went > 300.0);
			// a step each stride: between one for every 2.5 m and one for every metre walked
			Test->TestTrue(FString::Printf(TEXT("Steps are heard as the hero walks (%d in %.0f cm)"), Steps, Went), Steps >= FMath::FloorToInt32(Went / 250.0) && Steps <= FMath::CeilToInt32(Went / 100.0));
			UE_LOG(LogSoundsCheck, Display, TEXT("GAMECHECK steps: %d heard in %.0f cm walked"), Steps, Went);
			Enter(EPhase::Jump);
			return false;
		}

		case EPhase::Jump:
			if (Step == 0) {
				// standing still first: no steps while it stands
				if (Elapsed() < 1.0) {
					return false;
				}
				Before = USenniteSounds::HeardBeginning(World, TEXT("step."));
				Step = 1;
				return false;
			}
			if (Step == 1) {
				if (Elapsed() < 2.0) {
					return false;
				}
				Test->TestEqual(TEXT("No steps are heard while the hero stands"), USenniteSounds::HeardBeginning(World, TEXT("step.")) - Before, 0);
				Before = USenniteSounds::HeardBeginning(World, TEXT("jump."));
				GameCheck::Key(Player, EKeys::SpaceBar, IE_Pressed);
				Step = 2;
				return false;
			}
			if (!Hero->GetCharacterMovement()->IsFalling()) {
				return Elapsed() > 5.0 ? Fail(TEXT("The hero did not leave the ground on a jump")) : false;
			}
			GameCheck::Key(Player, EKeys::SpaceBar, IE_Released);
			Enter(EPhase::Land);
			return false;

		case EPhase::Land:
			if (Hero->GetCharacterMovement()->IsFalling()) {
				return Elapsed() > 5.0 ? Fail(TEXT("The hero did not land")) : false;
			}
			if (Step == 0) {
				// a frame for the landing to be heard
				Step = 1;
				PhaseStart = FPlatformTime::Seconds();
				return false;
			}
			if (Elapsed() < 0.3) {
				return false;
			}
			Test->TestEqual(TEXT("The jump is heard"), USenniteSounds::HeardBeginning(World, TEXT("jump.")) - Before, 1);
			Test->TestTrue(TEXT("The landing is heard"), USenniteSounds::HeardBeginning(World, TEXT("land.")) >= 1);
			UE_LOG(LogSoundsCheck, Display, TEXT("GAMECHECK jump heard %d, landing heard %d"), USenniteSounds::HeardBeginning(World, TEXT("jump.")) - Before, USenniteSounds::HeardBeginning(World, TEXT("land.")));
			Before = USenniteSounds::Heard(World, TEXT("attack.own"));
			// aimed down at the ground a few metres ahead. (On the test range a bolt flies through the ground:
			// the ground stops no bullet there, so nothing lands and no landing is heard; what is said below is a count, not a check.)
			Player->SetControlRotation(FRotator(-35.0, Hero->GetActorRotation().Yaw, 0.0));
			Enter(EPhase::Attack);
			return false;

		case EPhase::Attack:
			if (Step == 0) {
				// the camera has turned by the next frames
				if (Elapsed() < 0.3) {
					return false;
				}
				GameCheck::Key(Player, EKeys::LeftMouseButton, IE_Pressed);
				Step = 1;
				return false;
			}
			if (Step == 1) {
				if (Elapsed() < 0.5) {
					return false;
				}
				GameCheck::Key(Player, EKeys::LeftMouseButton, IE_Released);
				Step = 2;
				return false;
			}
			if (Elapsed() < 2.0) {
				return false;
			}
			Test->TestTrue(TEXT("The hero's own attack is heard"), USenniteSounds::Heard(World, TEXT("attack.own")) - Before >= 1);
			UE_LOG(LogSoundsCheck, Display, TEXT("GAMECHECK own attack heard %d; the bolt landing heard %d"), USenniteSounds::Heard(World, TEXT("attack.own")) - Before, USenniteSounds::Heard(World, TEXT("bolt.hit")));
			Step = 0;
			Enter(EPhase::Floats);
			return false;

		case EPhase::Floats: {
			// Sevarog floats over the ground and swings a hammer: his going is not heard as steps, and his attack is a blow up
			// close, heard as a swing and never as a shot, that lets no bolt go and is not aimed
			ASennitePlayerState* State = Player->GetPlayerState<ASennitePlayerState>();
			if (State == nullptr) {
				return Fail(TEXT("The player has no state"));
			}
			if (Step == 0) {
				State->SetHero(FName(TEXT("sevarog")));
				Step = 1;
				return false;
			}
			if (Step == 1) {
				if (Elapsed() < 0.6) {
					return false;
				}
				Test->TestTrue(TEXT("Sevarog floats, and his own weapon is a blow"), Hero->bHeroFloats && Hero->bHeroMelee);
				Player->SetControlRotation(FRotator(0.0, Hero->GetActorRotation().Yaw, 0.0));
				Began = Hero->GetActorLocation();
				Before = USenniteSounds::HeardBeginning(World, TEXT("step."));
				GameCheck::Key(Player, EKeys::W, IE_Pressed);
				Step = 2;
				return false;
			}
			if (Step == 2) {
				if (Elapsed() < 2.6) {
					return false;
				}
				GameCheck::Key(Player, EKeys::W, IE_Released);
				const double Went = FVector::Dist2D(Hero->GetActorLocation(), Began);
				const int32 Steps = USenniteSounds::HeardBeginning(World, TEXT("step.")) - Before;
				Test->TestTrue(TEXT("Sevarog went"), Went > 300.0);
				Test->TestEqual(TEXT("and no step of his is heard"), Steps, 0);
				UE_LOG(LogSoundsCheck, Display, TEXT("GAMECHECK floats: %d steps heard in %.0f cm"), Steps, Went);
				Step = 3;
				return false;
			}
			if (Step == 3) {
				// standing, a wall is put up within his reach, across the way he looks
				if (Elapsed() < 3.4) {
					return false;
				}
				const FRotator Facing(0.0, Player->GetControlRotation().Yaw, 0.0);
				Wall = World->SpawnActor<ASenniteWoodWall>(Hero->GetActorLocation() + Facing.Vector() * 180.0, FRotator(0.0, Facing.Yaw + 90.0, 0.0));
				WallHealth = Wall.IsValid() ? Wall->Health : 0.0f;
				SwingsBefore = USenniteSounds::Heard(World, TEXT("swing"));
				ShotsBefore = USenniteSounds::Heard(World, TEXT("attack.own"));
				bBoltSeen = false;
				GameCheck::Key(Player, EKeys::RightMouseButton, IE_Pressed);
				GameCheck::Key(Player, EKeys::LeftMouseButton, IE_Pressed);
				Step = 4;
				return false;
			}
			bBoltSeen |= bool(TActorIterator<AProjectileActor>(World));
			if (Step == 4) {
				if (Elapsed() < 3.6) {
					return false;
				}
				GameCheck::Key(Player, EKeys::LeftMouseButton, IE_Released);
				Step = 5;
				return false;
			}
			if (Elapsed() < 4.6) {
				return false;
			}
			Test->TestFalse(TEXT("A blow is not aimed: AIM does nothing with no gun in hand"), Hero->AimedIn);
			GameCheck::Key(Player, EKeys::RightMouseButton, IE_Released);
			Test->TestEqual(TEXT("His attack is heard as a swing"), USenniteSounds::Heard(World, TEXT("swing")) - SwingsBefore, 1);
			Test->TestEqual(TEXT("and not as a shot"), USenniteSounds::Heard(World, TEXT("attack.own")) - ShotsBefore, 0);
			Test->TestFalse(TEXT("No bolt leaves him"), bBoltSeen);
			Test->TestTrue(TEXT("The wall within his reach was put up"), Wall.IsValid());
			if (Wall.IsValid()) {
				Test->TestEqual(TEXT("The blow lands on the wall within his reach"), Wall->Health, WallHealth - Hero->MeleeDamage);
				UE_LOG(LogSoundsCheck, Display, TEXT("GAMECHECK blow: swings %d, shots %d, bolt %d, the wall %.0f -> %.0f"), USenniteSounds::Heard(World, TEXT("swing")) - SwingsBefore,
					USenniteSounds::Heard(World, TEXT("attack.own")) - ShotsBefore, bBoltSeen ? 1 : 0, WallHealth, Wall->Health);
				Wall->Destroy();
			}
			State->SetHero(GetDefault<USenniteHeroes>()->DefaultHero);
			GameCheck::Key(Player, EKeys::Escape, IE_Pressed);
			GameCheck::Key(Player, EKeys::Escape, IE_Released);
			Enter(EPhase::Menu);
			return false;
		}

		case EPhase::Menu: {
			if (Elapsed() < 0.5) {
				return false;
			}
			Test->TestTrue(TEXT("Escape opens the menu"), Menu->IsOpen());
			Test->TestEqual(TEXT("The menu is heard opening"), USenniteSounds::Heard(World, TEXT("ui.open")), 1);
			FVector2D Where;
			if (!Menu->Centre(TEXT("settings"), Where)) {
				return Fail(TEXT("The menu drew no SETTINGS button"));
			}
			GameCheck::Tap(Where);
			Enter(EPhase::Settings);
			return false;
		}

		case EPhase::Settings: {
			if (Elapsed() < 0.5) {
				return false;
			}
			Test->TestEqual(TEXT("A button of the menu is heard, once"), USenniteSounds::Heard(World, TEXT("ui.press")), 1);
			Menu->Close();
			Enter(EPhase::Closed);
			return false;
		}

		case EPhase::Closed:
			if (Elapsed() < 0.3) {
				return false;
			}
			Test->TestFalse(TEXT("The menu is closed"), Menu->IsOpen());
			Test->TestEqual(TEXT("The menu is heard closing"), USenniteSounds::Heard(World, TEXT("ui.back")), 1);
			UE_LOG(LogSoundsCheck, Display, TEXT("GAMECHECK menu: opening heard %d, a button %d, closing %d"), USenniteSounds::Heard(World, TEXT("ui.open")),
				USenniteSounds::Heard(World, TEXT("ui.press")), USenniteSounds::Heard(World, TEXT("ui.back")));
			UE_LOG(LogSoundsCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteSoundsTest, "Sennite.Sounds.HeardInTheGame",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteSoundsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSoundsRun(this));
	return true;
}

#endif
