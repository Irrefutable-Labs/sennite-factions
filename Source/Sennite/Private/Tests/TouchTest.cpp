// Touch check. Run in the game on the test range with the touch controls shown (as on a phone):
// every move is a finger on the screen, sent to the game's viewport as a phone's touches are. A drag
// on the right turns the hero to her right; the stick walks her to the range's crate, where OPEN
// shows, and OPEN gives her what the crate held; the weapon wheel changes what she holds (slid to and let go,
// held open and let go where it came down, tapped to take the next thing; what she has not got is not on it);
// FIRE held keeps the rifle firing, and RELOAD fills it from the rounds the crate gave; JUMP jumps
// and fires nothing; WALL, MAT, FIRE, RAMP and FLOOR build; EDIT, TILE and DONE cut a tile out of
// the wall she built; then she turns, runs with the stick pushed all the way, drinks a potion with
// POTION and USE, and takes a bandage in hand with BANDAGE. The controls are drawn on the HUD and say
// what each button does now.
//
// Tools/check_touch.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingActor.h"
#include "GameCheck.h"
#include "HealingActor.h"
#include "SenniteCharacter.h"
#include "SennitePlayerController.h"
#include "SennitePlayerState.h"
#include "SenniteSounds.h"
#include "SenniteTouch.h"
#include "SenniteWeapons.h"
#include "WeaponActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogTouchCheck, Log, All);

namespace
{
	/** The fingers the check uses: the left thumb on the stick, the right on the rest. */
	constexpr int32 LeftThumb = 0;
	constexpr int32 RightThumb = 1;

	/** A finger on the game's viewport, sent as the phone's screen sends it. */
	void Finger(int32 Index, ETouchType::Type Type, const FVector2D& At)
	{
		UGameViewportClient* Client = GEngine->GameViewport;
		Client->InputTouch(Client->Viewport, FTouchId(IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), static_cast<ETouchIndex::Type>(Index)),
			Type, At, 1.0f, FPlatformTime::Cycles64());
	}

	FVector2D ViewportSize()
	{
		FVector2D Size;
		GEngine->GameViewport->GetViewportSize(Size);
		return Size;
	}

	class FTouchRun : public IAutomationLatentCommand
	{
	public:
		explicit FTouchRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase {
			WaitForPawn, FaceWeapons, WalkOver, Stop, OpenCrate, Slots, Jump, HoldFire, Reload, Aim, Build, Edit, FaceBack, Run, Drink, Bandage, Done
		};

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		/** Where a control is drawn now. */
		FVector2D At(const TCHAR* Name) const
		{
			FVector2D Centre = FVector2D::ZeroVector;
			Test->TestTrue(FString::Printf(TEXT("The touch controls have %s"), Name), Controls->Where(Name, ViewportSize(), Centre));
			return Centre;
		}

		/** Whether a name is a place on the weapon wheel (RIFLE, AXE, POTION, ...), not a button of its own. */
		static bool OnWheel(const TCHAR* Name)
		{
			for (const TCHAR* Place : { TEXT("OWN"), TEXT("RIFLE"), TEXT("SHOTGUN"), TEXT("ENERGY"), TEXT("SNIPER"), TEXT("AXE"), TEXT("KATANA"), TEXT("BANDAGE"), TEXT("POTION") }) {
				if (FCString::Strcmp(Place, Name) == 0) {
					return true;
				}
			}
			return false;
		}

		/**
		 * A tap on a button: the finger comes down now and lifts a moment later, in Lift(). For a place on the wheel the
		 * finger comes down on the wheel and slides to the place, which opens the wheel, and lets go there in Lift().
		 */
		void Press(const TCHAR* Name)
		{
			if (OnWheel(Name)) {
				Finger(RightThumb, ETouchType::Began, At(TEXT("WHEEL")));
				Finger(RightThumb, ETouchType::Moved, At(Name));
				return;
			}
			Finger(RightThumb, ETouchType::Began, At(Name));
		}
		void Lift(const TCHAR* Name) { Finger(RightThumb, ETouchType::Ended, At(Name)); }

		/** One step of turning the hero to face a yaw by dragging a finger across the right of the screen. True once she does. */
		bool TurnStep(APlayerController* Controller, double Yaw)
		{
			const FVector2D View = ViewportSize();
			const double Error = FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Yaw, Yaw);
			if (FMath::Abs(Error) < 1.0) {
				if (bLooking) {
					Finger(RightThumb, ETouchType::Ended, LookAt);
					bLooking = false;
				}
				return true;
			}
			// a thumb that has run to the edge of its side lifts and comes down again in the middle of it
			if (bLooking && (LookAt.X > View.X * 0.9 || LookAt.X < View.X * 0.45)) {
				Finger(RightThumb, ETouchType::Ended, LookAt);
				bLooking = false;
			}
			if (!bLooking) {
				LookAt = FVector2D(View.X * 0.66, View.Y * 0.3);
				Finger(RightThumb, ETouchType::Began, LookAt);
				bLooking = true;
				return false;
			}
			LookAt.X += FMath::Clamp(Error * 1.5, -24.0, 24.0);
			Finger(RightThumb, ETouchType::Moved, LookAt);
			return false;
		}

		/** The left thumb on the stick, pushed up the screen by a share of its reach. */
		void PushStick(double Share)
		{
			const FVector2D Rest = At(TEXT("STICK"));
			const double Reach = USenniteTouchControls::StickReach * ViewportSize().Y / 720.0;
			Finger(LeftThumb, ETouchType::Began, Rest);
			Finger(LeftThumb, ETouchType::Moved, Rest - FVector2D(0.0, Share * Reach));
		}
		void LetGoOfStick() { Finger(LeftThumb, ETouchType::Ended, At(TEXT("STICK"))); }

		int32 Built(UWorld* World) const
		{
			int32 Count = 0;
			for (TActorIterator<ABuildingActor> It(World); It; ++It) {
				Count += It->IsPreview ? 0 : 1;
			}
			return Count;
		}

		bool Drawn(const FString& Entry) const { return Controls->LastDrawn.Contains(Entry); }

		static double YawTo(const ASenniteCharacter* Hero, const FVector& Where)
		{
			return (Where - Hero->GetActorLocation()).Rotation().Yaw;
		}

		FAutomationTestBase* Test;
		double RealStart;
		USenniteTouchControls* Controls = nullptr;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		bool bLooking = false;
		FVector2D LookAt = FVector2D::ZeroVector;
		FVector Mark = FVector::ZeroVector;
		int32 ClipMark = 0;
		int32 CountMark = 0;
		int32 JumpMark = 0;
		int32 PressMark = 0;
		double BehindMark = 0.0;
		float ShieldMark = 0.0f;
		bool bSawWalking = false;
		bool bSawRunning = false;
		double TopZ = 0.0;
		TWeakObjectPtr<ABuildingActor> Piece;
		int32 CutMark = 0;
	};

	bool FTouchRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		ASennitePlayerController* Controller = World ? Cast<ASennitePlayerController>(World->GetFirstPlayerController()) : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || GEngine->GameViewport == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("No ASenniteCharacter standing on the ground after 60 s")) : false;
			}
			// this player's wallet owns both guns sold in the store, not the blades: set as the server sets it once the
			// publisher's server says so. What is bought is hers at once, with no crate.
			State->SetGunsOwned((1 << 3) | (1 << 4));
			Controls = Controller->GetTouchControls();
			if (Controls == nullptr) {
				return Fail(TEXT("No touch controls: run with bAlwaysShowTouchInterface set, as Tools/check_touch.sh does"));
			}
			if (Controls->LastDrawn.Num() == 0) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("The HUD never drew the touch controls")) : false;
			}
			// a run without a window starts with a stray mouse movement; begin from the view behind the hero
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			for (const TCHAR* Name : { TEXT("FIRE=ATTACK"), TEXT("JUMP=JUMP"), TEXT("AIM=AIM"), TEXT("WALL=WALL"), TEXT("RAMP=RAMP"),
				TEXT("FLOOR=FLOOR"), TEXT("WHEEL=OWN"), TEXT("STICK=0.00,0.00") }) {
				Test->TestTrue(FString::Printf(TEXT("The HUD draws %s"), Name), Drawn(Name));
			}
			Test->TestTrue(TEXT("With no gun in hand the RELOAD button is there, dimmed: her own weapon has nothing to load"), Drawn(TEXT("RELOAD=dimmed")));
			Test->TestFalse(TEXT("There is no row of slots along the bottom: what she holds is on the wheel"), Drawn(TEXT("RIFLE=RIFLE")) || Drawn(TEXT("POTION=POTION")));
			Test->TestFalse(TEXT("The wheel is shut until a finger opens it"), Controls->LastDrawn.ContainsByPredicate([](const FString& Each) { return Each.StartsWith(TEXT("WHEEL:")); }));
			Test->TestFalse(TEXT("With no crate near, there is no OPEN button"), Controls->LastDrawn.ContainsByPredicate([](const FString& Each) { return Each.StartsWith(TEXT("OPEN=")); }));
			UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK controls: %s"), *FString::Join(Controls->LastDrawn, TEXT(" ")));
			GameCheck::Shot(TEXT("Touch"), TEXT("controls.png"));
			Enter(EPhase::FaceWeapons, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr || Controls == nullptr) {
			return Fail(TEXT("The hero or the touch controls went away during the check"));
		}

		switch (Phase) {
		case EPhase::FaceWeapons:
			// a drag to the right turns her right, a quarter turn, to face along the crate
			if (Step == 0) {
				// first a tap of the dimmed RELOAD, once the picture of the controls at rest is taken
				if (Elapsed(World) > 0.5) {
					Step = 1;
					Press(TEXT("RELOAD"));
				}
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Step == 1) {
				Lift(TEXT("RELOAD"));
				Test->TestEqual(TEXT("A tap of RELOAD with no gun in hand is heard as refused"), USenniteSounds::Heard(World, TEXT("ui.denied")), 1);
				Test->TestEqual(TEXT("and not as a press"), USenniteSounds::Heard(World, TEXT("ui.press")), 0);
				// where she faced before the drag
				Step = 2;
				Mark = FVector(Controller->GetControlRotation().Yaw, 0.0, 0.0);
			}
			if (!TurnStep(Controller, Mark.X + 90.0)) {
				return Elapsed(World) > 20.0 ? Fail(TEXT("Dragging on the right never turned the hero a quarter turn")) : false;
			}
			UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK look: turned from %.1f to %.1f by dragging"), Mark.X, Controller->GetControlRotation().Yaw);
			Test->TestTrue(TEXT("Dragging right turns the hero to her right"), FMath::FindDeltaAngleDegrees(Mark.X, Controller->GetControlRotation().Yaw) > 80.0);
			Mark = Hero->GetActorLocation();
			PushStick(0.7);
			Enter(EPhase::WalkOver, World);
			return false;

		case EPhase::WalkOver:
			bSawWalking |= Hero->IsWalking;
			Test->TestFalse(TEXT("A stick pushed part way walks, it does not run"), Hero->IsRunning);
			if (Hero->GetActorLocation().Y <= Mark.Y + 520.0) {
				return Elapsed(World) > 12.0 ? Fail(FString::Printf(TEXT("The stick never walked the hero up to the crate (at %s)"), *Hero->GetActorLocation().ToCompactString())) : false;
			}
			Test->TestTrue(TEXT("While the stick is held the hero walks"), bSawWalking);
			Test->TestTrue(TEXT("The stick walks her the way she faces"), FMath::Abs(Hero->GetActorLocation().X - Mark.X) < 120.0);
			Test->TestTrue(TEXT("The stick is drawn pushed ahead"), Controls->LastDrawn.ContainsByPredicate([](const FString& Each) { return Each.StartsWith(TEXT("STICK=0.00,-0.70")); }));
			GameCheck::Shot(TEXT("Touch"), TEXT("walking.png"));
			Enter(EPhase::Stop, World);
			return false;

		case EPhase::Stop:
			// the thumb lifts once the screenshot of the walk is taken
			if (Step == 0) {
				if (Elapsed(World) < 0.2) {
					return false;
				}
				Step = 1;
				LetGoOfStick();
			}
			if (Elapsed(World) < 1.2) {
				return false;
			}
			Test->TestFalse(TEXT("Let go of, the stick stops the walk"), Hero->IsWalking);
			Test->TestTrue(TEXT("Let go of, the stick leaves the hero standing"), Hero->GetVelocity().Size2D() < 5.0);
			UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK walk: from %s to %s"), *Mark.ToCompactString(), *Hero->GetActorLocation().ToCompactString());
			Enter(EPhase::OpenCrate, World);
			return false;

		case EPhase::OpenCrate:
			// beside the closed crate the OPEN button shows; a tap on it gives her what the crate held
			if (Step == 0) {
				Step = 1;
				Test->TestTrue(TEXT("Beside the closed crate there is an OPEN button"), Controls->LastDrawn.ContainsByPredicate([](const FString& Each) { return Each.StartsWith(TEXT("OPEN=")); }));
				Test->TestTrue(TEXT("She has no gun before the crate is opened"), !State->EquippedWeapons.Contains(1) && !State->EquippedWeapons.Contains(2));
				Press(TEXT("OPEN"));
				return false;
			}
			if (Step == 1 && Elapsed(World) > 0.15) { Step = 2; Lift(TEXT("OPEN")); return false; }
			if (Elapsed(World) < 1.5) {
				return false;
			}
			Test->TestTrue(TEXT("OPEN gives her the crate's rifle and shotgun"), State->EquippedWeapons.Contains(1) && State->EquippedWeapons.Contains(2));
			Test->TestTrue(TEXT("The crate's rifle is in her hand at once"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == 1);
			Test->TestTrue(TEXT("With a gun in hand there is a RELOAD button, and FIRE says FIRE"), Drawn(TEXT("RELOAD=RELOAD")) && Drawn(TEXT("FIRE=FIRE")));
			Test->TestTrue(TEXT("and its bandages and potions are hers"), State->HealingItemCounts[0] > 0 && State->HealingItemCounts[1] > 0);
			Test->TestEqual(TEXT("The opened crate laid nothing on the ground"), GameCheck::LyingAbout(World), 0);
			Test->TestFalse(TEXT("Beside the opened crate there is no OPEN button"), Controls->LastDrawn.ContainsByPredicate([](const FString& Each) { return Each.StartsWith(TEXT("OPEN=")); }));
			UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK crate: opened by touch; weapons %d, rounds %d, shells %d, bandages %d, potions %d"), State->EquippedWeapons.Num(),
				State->EquippedWeaponsAmmunition[1], State->EquippedWeaponsAmmunition[2], State->HealingItemCounts[0], State->HealingItemCounts[1]);
			GameCheck::Shot(TEXT("Touch"), TEXT("crate.png"));
			Enter(EPhase::Slots, World);
			return false;

		case EPhase::Slots:
			// SHOTGUN, then RIFLE, each a tap
			if (Step == 0) { Step = 1; PressMark = USenniteSounds::Heard(World, TEXT("ui.press")); Press(TEXT("SHOTGUN")); return false; }
			if (Step == 1 && Elapsed(World) > 0.15) { Step = 2; Lift(TEXT("SHOTGUN")); return false; }
			if (Step == 2 && Elapsed(World) > 1.0) {
				Step = 3;
				Test->TestTrue(TEXT("SHOTGUN puts the shotgun in her hand"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == 2);
				Test->TestTrue(TEXT("With a gun in hand FIRE says FIRE"), Drawn(TEXT("FIRE=FIRE")));
				Press(TEXT("RIFLE"));
				return false;
			}
			if (Step == 3 && Elapsed(World) > 1.15) { Step = 4; Lift(TEXT("RIFLE")); return false; }
			if (Step == 4 && Elapsed(World) > 2.0) {
				Step = 5;
				Test->TestTrue(TEXT("RIFLE puts the rifle in her hand"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == 1);
				Press(TEXT("ENERGY"));
				return false;
			}
			if (Step == 5 && Elapsed(World) > 2.15) { Step = 6; Lift(TEXT("ENERGY")); return false; }
			if (Step == 6 && Elapsed(World) > 3.0) {
				Step = 7;
				Test->TestTrue(TEXT("ENERGY puts the energy rifle in her hand"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == 3);
				Press(TEXT("SNIPER"));
				return false;
			}
			if (Step == 7 && Elapsed(World) > 3.15) { Step = 8; Lift(TEXT("SNIPER")); return false; }
			if (Step == 8 && Elapsed(World) > 4.0) {
				Step = 9;
				Test->TestTrue(TEXT("SNIPER puts the sniper in her hand"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == 4);
				Press(TEXT("RIFLE"));
				return false;
			}
			if (Step == 9 && Elapsed(World) > 4.15) { Step = 10; Lift(TEXT("RIFLE")); return false; }
			if (Step == 10 && Elapsed(World) > 5.0) {
				Step = 11;
				Test->TestTrue(TEXT("RIFLE puts the rifle back in her hand"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == 1);
				Test->TestTrue(TEXT("The wheel names what is in hand"), Drawn(TEXT("WHEEL=RIFLE")));
				// a finger held on the wheel, not moving
				Finger(RightThumb, ETouchType::Began, At(TEXT("WHEEL")));
				return false;
			}
			if (Step == 11 && Elapsed(World) > 5.6) {
				Test->TestTrue(TEXT("Held, the wheel opens with everything she has on it"), Drawn(TEXT("WHEEL:RIFLE=RIFLE")) && Drawn(TEXT("WHEEL:SHOTGUN=SHOTGUN"))
					&& Drawn(TEXT("WHEEL:OWN=OWN")) && Drawn(TEXT("WHEEL:BANDAGE=BANDAGE")) && Drawn(TEXT("WHEEL:POTION=POTION")));
				Test->TestTrue(TEXT("and the guns she bought, which no crate had to give her"), Drawn(TEXT("WHEEL:ENERGY=ENERGY")) && Drawn(TEXT("WHEEL:SNIPER=SNIPER")));
				Test->TestFalse(TEXT("The axe and the katana, which she has not got, are not on the wheel at all"),
					Controls->LastDrawn.ContainsByPredicate([](const FString& Each) { return Each.StartsWith(TEXT("WHEEL:AXE")) || Each.StartsWith(TEXT("WHEEL:KATANA")); }));
				FVector2D Nowhere;
				Test->TestFalse(TEXT("and there is no place for the axe to slide to"), Controls->Where(TEXT("AXE"), ViewportSize(), Nowhere));
				GameCheck::Shot(TEXT("Touch"), TEXT("wheel.png"));
				Step = 120;
				return false;
			}
			if (Step == 120 && Elapsed(World) > 5.9) {
				// the picture taken with the wheel open; let go where it came down: nothing changes
				Step = 12;
				Lift(TEXT("WHEEL"));
				return false;
			}
			if (Step == 12 && Elapsed(World) > 6.4) {
				Step = 13;
				Test->TestTrue(TEXT("Let go without moving, the rifle stays in her hand"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == 1);
				Test->TestFalse(TEXT("and the wheel shuts"), Controls->LastDrawn.ContainsByPredicate([](const FString& Each) { return Each.StartsWith(TEXT("WHEEL:")); }));
				// a tap: the next thing she has after the rifle
				Finger(RightThumb, ETouchType::Began, At(TEXT("WHEEL")));
				Finger(RightThumb, ETouchType::Ended, At(TEXT("WHEEL")));
				return false;
			}
			if (Step == 13 && Elapsed(World) > 7.2) {
				Step = 14;
				Test->TestTrue(TEXT("A tap of the wheel takes the next thing she has: the shotgun"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == 2);
				Test->TestTrue(TEXT("and the wheel says so"), Drawn(TEXT("WHEEL=SHOTGUN")));
				Step = 16;
				Press(TEXT("RIFLE"));
				return false;
			}
			if (Step == 16 && Elapsed(World) > 7.35) { Step = 17; Lift(TEXT("RIFLE")); return false; }
			if (Step == 17 && Elapsed(World) > 8.1) {
				Test->TestTrue(TEXT("Slid to the rifle and let go, the rifle is in her hand"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == 1);
				ClipMark = Hero->CurrentWeapon != nullptr ? Hero->CurrentWeapon->CurrentBulletCount : 0;
				TopZ = Hero->GetActorLocation().Z;
				Mark = Hero->GetActorLocation();
				JumpMark = USenniteSounds::HeardBeginning(World, TEXT("jump."));
				Press(TEXT("JUMP"));
				Enter(EPhase::Jump, World);
			}
			return false;

		case EPhase::Jump:
			TopZ = FMath::Max(TopZ, Hero->GetActorLocation().Z);
			if (Step == 0 && Elapsed(World) > 0.15) { Step = 1; Lift(TEXT("JUMP")); return false; }
			if (Elapsed(World) < 1.6) {
				return false;
			}
			Test->TestTrue(TEXT("JUMP lifts the hero off the ground"), TopZ > Mark.Z + 40.0);
			Test->TestTrue(TEXT("She is back on the ground"), Hero->GetCharacterMovement()->IsMovingOnGround());
			Test->TestEqual(TEXT("A touch on JUMP fires no shot"), Hero->CurrentWeapon != nullptr ? Hero->CurrentWeapon->CurrentBulletCount : -1, ClipMark);
			// in play a button is heard as the owner's press recording, the one the menus' buttons play, and never as the
			// small tick of a row: the five slots tapped above, once each. JUMP alone is heard only as the jump.
			Test->TestEqual(TEXT("The tap of JUMP is heard as the jump"), USenniteSounds::HeardBeginning(World, TEXT("jump.")) - JumpMark, 1);
			Test->TestEqual(TEXT("Each thing taken from the wheel is heard as a press, and JUMP is not"), USenniteSounds::Heard(World, TEXT("ui.press")) - PressMark, 7);
			Test->TestEqual(TEXT("No tap in play is heard as a tick"), USenniteSounds::Heard(World, TEXT("ui.tick")), 0);
			Test->TestEqual(TEXT("nor as one of a menu's row"), USenniteSounds::Heard(World, TEXT("ui.pick")), 0);
			UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK jump: rose %.0f cm"), TopZ - Mark.Z);
			Press(TEXT("FIRE"));
			Enter(EPhase::HoldFire, World);
			return false;

		case EPhase::HoldFire:
			// held for 1.2 s, the rifle keeps firing (a shot each 0.24 s); let go, it stops
			if (Step == 0 && Elapsed(World) > 1.2) {
				Step = 1;
				Lift(TEXT("FIRE"));
				CountMark = Hero->CurrentWeapon->CurrentBulletCount;
				return false;
			}
			if (Elapsed(World) < 2.2) {
				return false;
			}
			Test->TestTrue(FString::Printf(TEXT("FIRE held keeps the rifle firing (%d rounds spent)"), ClipMark - CountMark), ClipMark - CountMark >= 4);
			Test->TestTrue(TEXT("Let go of, FIRE stops the rifle"), Hero->CurrentWeapon->CurrentBulletCount >= CountMark - 1);
			CountMark = Hero->CurrentWeapon->CurrentBulletCount;
			UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK fire: clip %d -> %d held, %d after"), ClipMark, CountMark, Hero->CurrentWeapon->CurrentBulletCount);
			Press(TEXT("RELOAD"));
			Enter(EPhase::Reload, World);
			return false;

		case EPhase::Reload:
			// RELOAD fills the clip from the rounds the crate gave
			if (Step == 0 && Elapsed(World) > 0.15) { Step = 1; Lift(TEXT("RELOAD")); return false; }
			if (Hero->CurrentWeapon == nullptr) {
				return Fail(TEXT("The rifle went out of her hand during the reload"));
			}
			// until the clip is full and her hands are free again
			if (Hero->CurrentWeapon->CurrentBulletCount != Hero->CurrentWeapon->MagazineSize || State->JustReloadedRifle) {
				return Elapsed(World) > 8.0 ? Fail(TEXT("RELOAD did not fill the rifle from the rounds the crate gave")) : false;
			}
			Test->TestEqual(TEXT("RELOAD takes the rounds it loads from those the crate gave"), State->EquippedWeaponsAmmunition[1],
				GetDefault<ASenniteRifle>()->CrateRounds - (Hero->CurrentWeapon->MagazineSize - CountMark));
			Test->TestTrue(TEXT("The reload is heard"), USenniteSounds::HeardBeginning(World, TEXT("reload.")) >= 1);
			UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK reload: clip %d -> %d, %d to spare"), CountMark, Hero->CurrentWeapon->CurrentBulletCount, State->EquippedWeaponsAmmunition[1]);
			Enter(EPhase::Aim, World);
			return false;

		case EPhase::Aim: {
			// AIM, tapped, looks through the hero's own eyes: the camera is where her eyes are, not behind her, and she is not
			// drawn for her own player, so nothing of her is under the crosshair; tapped again, the camera is behind her again
			const FVector Eyes = Hero->GetActorLocation() + FVector(0.0, 0.0, Hero->BaseEyeHeight);
			const double CameraOff = FVector::Dist(Controller->PlayerCameraManager->GetCameraLocation(), Eyes);
			switch (Step) {
			case 0: if (Elapsed(World) > 0.5) { Step = 1; BehindMark = CameraOff; PressMark = USenniteSounds::Heard(World, TEXT("ui.press")); Press(TEXT("AIM")); } return false;
			case 1: if (Elapsed(World) > 0.65) { Step = 2; Lift(TEXT("AIM")); } return false;
			case 2:
				if (Elapsed(World) < 1.9) { return false; }
				Test->TestTrue(TEXT("A tap of AIM aims"), Hero->AimedIn);
				Test->TestEqual(TEXT("and is heard as a press"), USenniteSounds::Heard(World, TEXT("ui.press")) - PressMark, 1);
				Test->TestTrue(FString::Printf(TEXT("Aiming, the camera is at her eyes (%.0f cm from them; it follows %.0f cm off)"), CameraOff, BehindMark), CameraOff < 45.0);
				Test->TestTrue(TEXT("and she is not drawn for her own player"), Hero->GetMesh()->bOwnerNoSee);
				Test->TestTrue(TEXT("nor is the gun in her hand"), Hero->CurrentWeapon == nullptr || !Hero->CurrentWeapon->WeaponMesh->IsVisible());
				UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK aim: the camera %.0f cm from her eyes aiming, %.0f cm following"), CameraOff, BehindMark);
				GameCheck::Shot(TEXT("Touch"), TEXT("aim.png"));
				Step = 3;
				return false;
			case 3: if (Elapsed(World) > 2.2) { Step = 4; Press(TEXT("AIM")); } return false;
			case 4: if (Elapsed(World) > 2.35) { Step = 5; Lift(TEXT("AIM")); } return false;
			default:
				if (Elapsed(World) < 3.6) { return false; }
				Test->TestFalse(TEXT("A second tap of AIM lets go"), Hero->AimedIn);
				Test->TestTrue(FString::Printf(TEXT("Let go, the camera is behind her again (%.0f cm from her eyes)"), CameraOff), CameraOff > 200.0);
				Test->TestFalse(TEXT("and she is drawn again"), Hero->GetMesh()->bOwnerNoSee);
				Test->TestTrue(TEXT("and so is the gun in her hand"), Hero->CurrentWeapon == nullptr || Hero->CurrentWeapon->WeaponMesh->IsVisible());
				CountMark = Built(World);
				Enter(EPhase::Build, World);
				return false;
			}
		}

		case EPhase::Build:
			// WALL, MAT, then FIRE (now BUILD) builds it; RAMP and FLOOR change the piece
			switch (Step) {
			case 0: if (Elapsed(World) > 1.0) { Step = 1; Press(TEXT("WALL")); } return false;
			case 1: if (Elapsed(World) > 1.15) { Step = 2; Lift(TEXT("WALL")); } return false;
			case 2:
				if (Elapsed(World) < 1.8) { return false; }
				Test->TestTrue(TEXT("WALL starts building a wall"), State->InBuildMode && State->BuildMode == TEXT("Wall"));
				Test->TestTrue(TEXT("In build mode FIRE says BUILD and AIM says MAT"), Drawn(TEXT("FIRE=BUILD")) && Drawn(TEXT("AIM=MAT")));
				CountMark = Hero->GetCurrentBuildingMaterial();
				Step = 3; Press(TEXT("AIM"));
				return false;
			case 3: if (Elapsed(World) > 1.95) { Step = 4; Lift(TEXT("AIM")); } return false;
			case 4:
				if (Elapsed(World) < 2.6) { return false; }
				Test->TestEqual(TEXT("MAT changes the building material"), Hero->GetCurrentBuildingMaterial(), (CountMark + 1) % 3);
				CountMark = Built(World);
				Step = 5; Press(TEXT("FIRE"));
				return false;
			case 5: if (Elapsed(World) > 2.75) { Step = 6; Lift(TEXT("FIRE")); } return false;
			case 6:
				if (Elapsed(World) < 3.4) { return false; }
				Test->TestEqual(TEXT("BUILD builds the wall"), Built(World), CountMark + 1);
				GameCheck::Shot(TEXT("Touch"), TEXT("built.png"));
				Step = 7; Press(TEXT("RAMP"));
				return false;
			case 7: if (Elapsed(World) > 3.55) { Step = 8; Lift(TEXT("RAMP")); } return false;
			case 8:
				if (Elapsed(World) < 4.2) { return false; }
				Test->TestTrue(TEXT("RAMP changes the piece to a ramp"), State->InBuildMode && State->BuildMode == TEXT("Ramp"));
				Step = 9; Press(TEXT("FLOOR"));
				return false;
			case 9: if (Elapsed(World) > 4.35) { Step = 10; Lift(TEXT("FLOOR")); } return false;
			case 10:
				if (Elapsed(World) < 5.0) { return false; }
				Test->TestTrue(TEXT("FLOOR changes the piece to a floor"), State->InBuildMode && State->BuildMode == TEXT("Floor"));
				Test->TestTrue(TEXT("The piece being placed is marked"), Drawn(TEXT("FLOOR=FLOOR")));
				Step = 11; Press(TEXT("RIFLE"));
				return false;
			case 11: if (Elapsed(World) > 5.15) { Step = 12; Lift(TEXT("RIFLE")); } return false;
			default:
				if (Elapsed(World) < 6.0) { return false; }
				Test->TestFalse(TEXT("RIFLE leaves build mode"), State->InBuildMode);
				UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK build: %d pieces stand"), Built(World));
				Enter(EPhase::Edit, World);
				return false;
			}

		case EPhase::Edit:
			// the wall she built is in sight: EDIT, then FIRE (now TILE) marks the tile in sight, and DONE cuts it
			switch (Step) {
			case 0:
				if (Elapsed(World) < 0.5) { return false; }
				Test->TestTrue(TEXT("Facing her own wall, there is an EDIT button"), Drawn(TEXT("EDIT=EDIT")));
				Step = 1; Press(TEXT("EDIT"));
				return false;
			case 1: if (Elapsed(World) > 0.65) { Step = 2; Lift(TEXT("EDIT")); } return false;
			case 2:
				if (Elapsed(World) < 1.3) { return false; }
				Piece = Hero->GetEditingPiece();
				if (!Piece.IsValid()) {
					return Fail(TEXT("EDIT did not start editing the wall in sight"));
				}
				CutMark = Piece->Cut;
				Test->TestTrue(TEXT("While editing, FIRE says TILE, AIM says RESET and EDIT says DONE"), Drawn(TEXT("FIRE=TILE")) && Drawn(TEXT("AIM=RESET")) && Drawn(TEXT("EDIT=DONE")));
				Step = 3; Press(TEXT("FIRE"));
				return false;
			case 3: if (Elapsed(World) > 1.45) { Step = 4; Lift(TEXT("FIRE")); } return false;
			case 4:
				if (Elapsed(World) < 2.1) { return false; }
				Test->TestTrue(TEXT("TILE marks the tile in sight"), Hero->GetEditCut() != CutMark);
				CutMark = Hero->GetEditCut();
				GameCheck::Shot(TEXT("Touch"), TEXT("editing.png"));
				Step = 5; Press(TEXT("EDIT"));
				return false;
			case 5: if (Elapsed(World) > 2.25) { Step = 6; Lift(TEXT("EDIT")); } return false;
			default:
				if (Elapsed(World) < 3.2) { return false; }
				Test->TestTrue(TEXT("DONE finishes the edit"), Hero->GetEditingPiece() == nullptr);
				Test->TestTrue(TEXT("DONE cuts the marked tile out of the wall"), Piece.IsValid() && Piece->Cut == CutMark);
				UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK edit: wall cut %d"), Piece.IsValid() ? Piece->Cut : -1);
				Enter(EPhase::FaceBack, World);
				return false;
			}

		case EPhase::FaceBack:
			// turn back the way she came, then the stick pushed all the way: she runs
			if (Step == 0) {
				Step = 1;
				Mark = FVector(Controller->GetControlRotation().Yaw, 0.0, 0.0);
			}
			if (!TurnStep(Controller, Mark.X + 180.0)) {
				return Elapsed(World) > 20.0 ? Fail(TEXT("Dragging never turned the hero round")) : false;
			}
			PushStick(1.0);
			Enter(EPhase::Run, World);
			return false;

		case EPhase::Run:
			bSawRunning |= Hero->IsRunning;
			if (Elapsed(World) < 1.2) {
				return false;
			}
			Test->TestTrue(TEXT("The stick pushed all the way ahead runs"), bSawRunning);
			LetGoOfStick();
			Enter(EPhase::Drink, World);
			return false;

		case EPhase::Drink:
			switch (Step) {
			case 0: if (Elapsed(World) > 0.8) { Step = 1; Press(TEXT("POTION")); } return false;
			case 1: if (Elapsed(World) > 0.95) { Step = 2; Lift(TEXT("POTION")); } return false;
			case 2:
				if (Elapsed(World) < 1.8) { return false; }
				Test->TestTrue(TEXT("POTION puts a potion in her hand"), Hero->CurrentHealingItem != nullptr && Hero->CurrentHealingItem->HealingType == 1);
				Test->TestTrue(TEXT("With an item in hand FIRE says USE"), Drawn(TEXT("FIRE=USE")));
				CountMark = State->HealingItemCounts[1];
				ShieldMark = Hero->Shield;
				Step = 3; Press(TEXT("FIRE"));
				return false;
			case 3: if (Elapsed(World) > 1.95) { Step = 4; Lift(TEXT("FIRE")); } return false;
			case 4:
				if (Elapsed(World) < 2.8) { return false; }
				Test->TestEqual(TEXT("USE drinks a potion"), State->HealingItemCounts[1], CountMark - 1);
				Step = 5;
				return false;
			default:
				if (Elapsed(World) < 7.0) { return false; }
				Test->TestEqual(TEXT("The potion adds 20 shield"), Hero->Shield, ShieldMark + 20.0f);
				UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK potion: shield %.0f -> %.0f, potions %d -> %d"), ShieldMark, Hero->Shield, CountMark, State->HealingItemCounts[1]);
				Enter(EPhase::Bandage, World);
				return false;
			}

		case EPhase::Bandage:
			if (Step == 0) { Step = 1; Press(TEXT("BANDAGE")); return false; }
			if (Step == 1 && Elapsed(World) > 0.15) { Step = 2; Lift(TEXT("BANDAGE")); return false; }
			if (Elapsed(World) < 1.2) {
				return false;
			}
			Test->TestTrue(TEXT("BANDAGE puts a bandage in her hand"), Hero->CurrentHealingItem != nullptr && Hero->CurrentHealingItem->HealingType == 0);
			Test->TestTrue(TEXT("The wheel names the bandage in hand"), Drawn(TEXT("WHEEL=BANDAGE")));
			UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK controls: %s"), *FString::Join(Controls->LastDrawn, TEXT(" ")));
			GameCheck::Shot(TEXT("Touch"), TEXT("bandage.png"));
			Enter(EPhase::Done, World);
			return false;

		case EPhase::Done:
			if (Elapsed(World) < 0.5) {
				return false;
			}
			UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK finished"));
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteTouchTest, "Sennite.Touch.PlaysByTouch",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteTouchTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTouchRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
