// Touch check. Run in the game on the test range with the touch controls shown (as on a phone):
// every move is a finger on the screen, sent to the game's viewport as a phone's touches are. A drag
// on the right turns the hero to her right; the stick walks her to the range's crate, where OPEN
// shows, and OPEN gives her what the crate held; the slots along the bottom change what she holds;
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
#include "SenniteTouch.h"
#include "WeaponActor.h"
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
			WaitForPawn, FaceWeapons, WalkOver, Stop, OpenCrate, Slots, Jump, HoldFire, Reload, Build, Edit, FaceBack, Run, Drink, Bandage, Done
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

		/** A tap on a button: the finger comes down now and lifts a moment later, in Lift(). */
		void Press(const TCHAR* Name) { Finger(RightThumb, ETouchType::Began, At(Name)); }
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
			Controls = Controller->GetTouchControls();
			if (Controls == nullptr) {
				return Fail(TEXT("No touch controls: run with bAlwaysShowTouchInterface set, as Tools/check_touch.sh does"));
			}
			if (Controls->LastDrawn.Num() == 0) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("The HUD never drew the touch controls")) : false;
			}
			// a run without a window starts with a stray mouse movement; begin from the view behind the hero
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			for (const TCHAR* Name : { TEXT("FIRE=SWING"), TEXT("JUMP=JUMP"), TEXT("RELOAD=RELOAD"), TEXT("AIM=AIM"), TEXT("WALL=WALL"), TEXT("RAMP=RAMP"),
				TEXT("FLOOR=FLOOR"), TEXT("RIFLE=RIFLE"), TEXT("SHOTGUN=SHOTGUN"), TEXT("BANDAGE=BANDAGE"), TEXT("POTION=POTION"), TEXT("STICK=0.00,0.00") }) {
				Test->TestTrue(FString::Printf(TEXT("The HUD draws %s"), Name), Drawn(Name));
			}
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
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Step == 0) {
				// where she faced before the drag
				Step = 1;
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
			if (Step == 0) { Step = 1; Press(TEXT("SHOTGUN")); return false; }
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
				Test->TestTrue(TEXT("RIFLE puts the rifle in her hand"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == 1);
				ClipMark = Hero->CurrentWeapon != nullptr ? Hero->CurrentWeapon->CurrentBulletCount : 0;
				TopZ = Hero->GetActorLocation().Z;
				Mark = Hero->GetActorLocation();
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
				GetDefault<ASenniteCrate>()->RifleRounds - (Hero->CurrentWeapon->MagazineSize - CountMark));
			UE_LOG(LogTouchCheck, Display, TEXT("GAMECHECK reload: clip %d -> %d, %d to spare"), CountMark, Hero->CurrentWeapon->CurrentBulletCount, State->EquippedWeaponsAmmunition[1]);
			CountMark = Built(World);
			Enter(EPhase::Build, World);
			return false;

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
			Test->TestTrue(TEXT("The HUD draws the BANDAGE slot"), Drawn(TEXT("BANDAGE=BANDAGE")));
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
