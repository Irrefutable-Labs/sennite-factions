// Editing and damage check for built pieces. Run in the game on the test range, with the real keys and
// mouse: the hero takes the rifle from the range's crate, builds a wood wall and shoots it twice. The wall takes the
// rifle's damage, the shooter sees the damage by the aim and the wall's health under it, and the
// wall's tiles are knocked out of line. G edits the wall: the fire button marks the middle and bottom
// middle tiles, G again cuts them out for everyone, a door. A shot through the door touches nothing,
// and the hero walks through it. From the other side G and the aim button put every tile back. Then
// what holds a piece up: looking up, she builds a second wall on top of the first, which stands
// because the first holds it; a wall one level up with nothing under it or beside it is not built
// and costs nothing; four more rounds bring the first wall down, and the wall it was holding falls a
// moment after it.
//
// Tools/check_editing.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingActor.h"
#include "GameCheck.h"
#include "SenniteCharacter.h"
#include "SenniteGameMode.h"
#include "SenniteHUD.h"
#include "SennitePlayerState.h"
#include "SenniteSounds.h"
#include "WeaponActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogEditCheck, Log, All);

namespace
{
	class FEditRun : public IAutomationLatentCommand
	{
	public:
		explicit FEditRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, OpenCrate, Rifle, WallPreview, WallBuilt, RifleBack, Shoot, EditStart, Mark, Confirmed, ShootHole, ToDoor, Through, TurnBack, Reset, Stack, Fell, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		void Tap(APlayerController* Controller, const FKey& InKey) const
		{
			GameCheck::Key(Controller, InKey, IE_Pressed);
			GameCheck::Key(Controller, InKey, IE_Released);
		}

		/** The middle of a tile of the wall, in the world. */
		FVector TileAt(int32 Tile) const
		{
			return (Wall->TileTransform(Tile, false) * Wall->GetActorTransform()).GetLocation();
		}

		/** Turn the camera with the mouse until it looks at a tile. */
		bool LookAt(APlayerController* Controller, int32 Tile) const
		{
			return GameCheck::AimStep(Controller, Controller->PlayerCameraManager->GetCameraLocation(), TileAt(Tile), 0.3);
		}

		/** Turn the gun with the mouse until a shot from it goes through the middle of a tile. */
		bool AimAt(APlayerController* Controller, ASenniteCharacter* Hero, int32 Tile) const
		{
			return GameCheck::AimStep(Controller, Hero->CurrentWeapon->GetMuzzleLocation(), TileAt(Tile), 0.2);
		}

		/** Whether the HUD drew a line that starts with this. */
		static bool Drew(APlayerController* Controller, const FString& Start, FString* OutLine = nullptr)
		{
			const ASenniteHUD* Hud = Cast<ASenniteHUD>(Controller->GetHUD());
			if (Hud == nullptr) {
				return false;
			}
			for (const FString& Line : Hud->LastDrawn) {
				if (Line.StartsWith(Start)) {
					if (OutLine != nullptr) {
						*OutLine = Line;
					}
					return true;
				}
			}
			return false;
		}

		/** Built walls standing in the world. */
		static int32 WallsStanding(UWorld* World)
		{
			int32 Count = 0;
			for (TActorIterator<ABuildingActor> It(World); It; ++It) {
				Count += !It->IsPreview && !It->IsActorBeingDestroyed() && It->StructureType == TEXT("Wall") ? 1 : 0;
			}
			return Count;
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		GameCheck::FCrateRun Crate;
		int32 Shots = 0;
		int32 WoodBefore = 0;
		bool bSawPieceHit = false;
		FVector Ahead = FVector::XAxisVector;
		ABuildingActor* Wall = nullptr;
		TWeakObjectPtr<ABuildingActor> Upper;
		bool bUpperStoodLonger = false;
		GameCheck::FMouseTurn Turn;
	};

	bool FEditRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("No ASenniteCharacter standing on the ground after 60 s")) : false;
			}
			// a run without a window starts with a stray mouse movement; begin from the view behind the hero
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			// the crate is to her right
			Enter(EPhase::OpenCrate, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr) {
			return Fail(TEXT("The hero went away during the check"));
		}
		// the last round may bring it down before the check moves on to see that it fell
		bUpperStoodLonger |= Wall != nullptr && !IsValid(Wall) && Upper.IsValid();
		if (Wall != nullptr && !IsValid(Wall) && Phase != EPhase::Fell && !(Phase == EPhase::Reset && Shots >= 4)) {
			return Fail(TEXT("The wall went away before the check brought it down"));
		}

		switch (Phase) {
		case EPhase::OpenCrate:
			if (!Crate.Step(World, Controller, Hero, EKeys::D)) {
				return false;
			}
			if (!Crate.Failure.IsEmpty()) {
				return Fail(Crate.Failure);
			}
			if (!State->EquippedWeapons.Contains(1)) {
				return Fail(TEXT("The crate did not give the hero the rifle"));
			}
			Tap(Controller, EKeys::Two);
			Enter(EPhase::Rifle, World);
			return false;

		case EPhase::Rifle:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Hero->CurrentWeapon == nullptr || Hero->CurrentWeapon->WeaponType != 1) {
				return Fail(TEXT("Key 2 did not bring the rifle to hand"));
			}
			{
				const double Yaw = FMath::RoundHalfFromZero(FRotator::NormalizeAxis(Controller->GetControlRotation().Yaw) / 90.0) * 90.0;
				Ahead = FRotator(0.0, Yaw, 0.0).Vector();
			}
			WoodBefore = State->MaterialCounts[0];
			Tap(Controller, EKeys::X);
			Enter(EPhase::WallPreview, World);
			return false;

		case EPhase::WallPreview:
			if (Elapsed(World) < 0.4) {
				return false;
			}
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
			Enter(EPhase::WallBuilt, World);
			return false;

		case EPhase::WallBuilt:
			if (Elapsed(World) < 0.8) {
				return false;
			}
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
			for (TActorIterator<ABuildingActor> It(World); It; ++It) {
				if (!It->IsPreview && It->StructureType == TEXT("Wall")) {
					Wall = *It;
				}
			}
			if (Wall == nullptr) {
				return Fail(TEXT("No wall was built"));
			}
			Test->TestEqual(TEXT("A wall is nine tiles"), Wall->PieceMesh->GetInstanceCount(), 9);
			Test->TestEqual(TEXT("A new wall has no tile cut"), Wall->Cut, 0);
			Test->TestTrue(TEXT("The wall knows who built it"), Wall->Builder == State);
			Test->TestEqual(TEXT("A new wood wall has wood's strength"), Wall->Health, Wall->MaxHealth());
			WoodBefore = State->MaterialCounts[0];
			// X again leaves build mode and the rifle comes back to hand
			Tap(Controller, EKeys::X);
			Enter(EPhase::RifleBack, World);
			return false;

		case EPhase::RifleBack:
			if (Elapsed(World) < 1.2) {
				return false;
			}
			if (State->InBuildMode || Hero->CurrentWeapon == nullptr) {
				return Fail(TEXT("Leaving build mode did not bring the rifle back"));
			}
			Enter(EPhase::Shoot, World);
			return false;

		case EPhase::Shoot:
			// two rounds into the top left tile
			bSawPieceHit |= Drew(Controller, TEXT("piecehit=25"));
			if (Shots < 2) {
				if (Elapsed(World) < 0.5) {
					return false;
				}
				if (!AimAt(Controller, Hero, 6)) {
					return Elapsed(World) > 10.0 ? Fail(TEXT("Could not aim the rifle at the wall")) : false;
				}
				Tap(Controller, EKeys::LeftMouseButton);
				++Shots;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 0.8) {
				return false;
			}
			{
				Test->TestEqual(TEXT("Two rifle rounds take 50 off the wall"), Wall->Health, Wall->MaxHealth() - 50.0f);
				Test->TestTrue(TEXT("The shooter sees what a round took off the wall, by the aim"), bSawPieceHit);
				FString Line;
				Test->TestTrue(TEXT("The HUD names the wall in sight and what it has left"), Drew(Controller, TEXT("pieceaim=WOOD WALL 100/150"), &Line));
				double Moved = 0.0;
				for (int32 Tile = 0; Tile < 9; ++Tile) {
					FTransform Drawn;
					Wall->PieceMesh->GetInstanceTransform(Tile, Drawn, false);
					const FTransform Whole = Wall->TileTransform(Tile, false);
					Moved = FMath::Max(Moved, FVector::Dist(Drawn.GetLocation(), Whole.GetLocation()) + Drawn.GetRotation().AngularDistance(Whole.GetRotation()) * 100.0);
				}
				Test->TestTrue(TEXT("A hurt wall's tiles are knocked out of line"), Moved > 0.5);
				UE_LOG(LogEditCheck, Display, TEXT("GAMECHECK shot the wall: health=%.0f hud='%s' tiles moved up to %.2f"), Wall->Health, *Line, Moved);
				GameCheck::Shot(TEXT("Editing"), TEXT("damaged.png"));
			}
			Enter(EPhase::EditStart, World);
			return false;

		case EPhase::EditStart:
			if (!LookAt(Controller, 4)) {
				return Elapsed(World) > 10.0 ? Fail(TEXT("Could not look at the middle of the wall")) : false;
			}
			if (Step == 0) {
				Step = 1;
				Tap(Controller, EKeys::G);
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 0.3) {
				return false;
			}
			if (Hero->GetEditingPiece() != Wall) {
				return Fail(TEXT("G with the wall in sight did not start editing it"));
			}
			Test->TestTrue(TEXT("The HUD says the wall is being edited and how"), Drew(Controller, TEXT("edit=")));
			Enter(EPhase::Mark, World);
			return false;

		case EPhase::Mark:
			// the middle tile, then the one under it
			if (Step == 0) {
				if (!LookAt(Controller, 4)) {
					return Elapsed(World) > 10.0 ? Fail(TEXT("Could not look at the middle tile")) : false;
				}
				Tap(Controller, EKeys::LeftMouseButton);
				Step = 1;
				return false;
			}
			if (Step == 1) {
				Test->TestEqual(TEXT("The fire button marks the tile in sight to be cut"), Hero->GetEditCut(), 1 << 4);
				Step = 2;
				return false;
			}
			if (Step == 2) {
				if (!LookAt(Controller, 1)) {
					return Elapsed(World) > 20.0 ? Fail(TEXT("Could not look at the bottom middle tile")) : false;
				}
				Tap(Controller, EKeys::LeftMouseButton);
				Step = 3;
				return false;
			}
			Test->TestEqual(TEXT("Two tiles are marked"), Hero->GetEditCut(), (1 << 4) | (1 << 1));
			Test->TestEqual(TEXT("While editing, the marked tiles are gone from the wall"), Wall->PieceMesh->GetInstanceCount(), 7);
			Test->TestEqual(TEXT("While editing, the marked tiles show see-through"), Wall->GhostTiles->GetInstanceCount(), 2);
			if (Step == 3) {
				Test->TestEqual(TEXT("Nothing is cut for everyone before G"), Wall->Cut, 0);
				// the picture is taken at the end of this frame, while the edit is still open
				GameCheck::Shot(TEXT("Editing"), TEXT("editing.png"));
				Step = 4;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 0.3) {
				return false;
			}
			Tap(Controller, EKeys::G);
			Enter(EPhase::Confirmed, World);
			return false;

		case EPhase::Confirmed:
			if (Elapsed(World) < 0.6) {
				return false;
			}
			Test->TestTrue(TEXT("G again ends the edit"), Hero->GetEditingPiece() == nullptr);
			Test->TestEqual(TEXT("The server cut the two tiles: a door"), Wall->Cut, (1 << 4) | (1 << 1));
			Test->TestEqual(TEXT("The door shows"), Wall->PieceMesh->GetInstanceCount(), 7);
			// the edit is heard: as it begins and at each of the two tiles marked
			Test->TestTrue(FString::Printf(TEXT("The edit and its two tiles are heard (%d)"), USenniteSounds::Heard(World, TEXT("piece.edit"))), USenniteSounds::Heard(World, TEXT("piece.edit")) >= 3);
			Test->TestEqual(TEXT("No see-through tiles after the edit"), Wall->GhostTiles->GetInstanceCount(), 0);
			Test->TestEqual(TEXT("Editing costs nothing"), State->MaterialCounts[0], WoodBefore);
			Test->TestEqual(TEXT("Editing does not mend the wall"), Wall->Health, Wall->MaxHealth() - 50.0f);
			GameCheck::Shot(TEXT("Editing"), TEXT("door.png"));
			Enter(EPhase::ShootHole, World);
			return false;

		case EPhase::ShootHole:
			if (Step == 0) {
				if (!AimAt(Controller, Hero, 4)) {
					return Elapsed(World) > 10.0 ? Fail(TEXT("Could not aim through the door")) : false;
				}
				Tap(Controller, EKeys::LeftMouseButton);
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 0.8) {
				return false;
			}
			Test->TestEqual(TEXT("A round through the door does not touch the wall"), Wall->Health, Wall->MaxHealth() - 50.0f);
			Controller->SetIgnoreLookInput(false);
			GameCheck::Key(Controller, EKeys::W, IE_Pressed);
			Enter(EPhase::ToDoor, World);
			return false;

		case EPhase::ToDoor:
		case EPhase::Through: {
			// first to a point just before the door, then straight through it
			const FVector Door = Wall->GetActorLocation();
			const FVector Goal = Phase == EPhase::ToDoor ? Door - Ahead * 220.0 : Door + Ahead * 400.0;
			const FVector Way = (Goal - Hero->GetActorLocation()) * FVector(1.0, 1.0, 0.0);
			const double Error = FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Yaw, Way.Rotation().Yaw);
			GameCheck::Mouse(Controller, EKeys::MouseX, FMath::Clamp(Error / 0.25, -40.0, 40.0));
			const double Past = FVector::DotProduct(Hero->GetActorLocation() - Door, Ahead);
			if (Phase == EPhase::ToDoor) {
				if (Way.Size() > 50.0) {
					return Elapsed(World) > 10.0 ? Fail(TEXT("The hero did not reach the door")) : false;
				}
				Enter(EPhase::Through, World);
				return false;
			}
			if (Past < 200.0) {
				return Elapsed(World) > 8.0 ? Fail(FString::Printf(TEXT("The hero did not get through the door (%.0f from the wall)"), Past)) : false;
			}
			GameCheck::Key(Controller, EKeys::W, IE_Released);
			UE_LOG(LogEditCheck, Display, TEXT("GAMECHECK through the door: %.0f past the wall"), Past);
			GameCheck::Shot(TEXT("Editing"), TEXT("through.png"));
			Enter(EPhase::TurnBack, World);
			return false;
		}

		case EPhase::TurnBack:
			if (Elapsed(World) < 0.6) {
				return false;
			}
			if (!Turn.Step(Controller, 180.0)) {
				return false;
			}
			if (!LookAt(Controller, 4)) {
				return Elapsed(World) > 12.0 ? Fail(TEXT("Could not look back at the door")) : false;
			}
			Tap(Controller, EKeys::G);
			Enter(EPhase::Reset, World);
			return false;

		case EPhase::Reset:
			if (Step == 0) {
				if (Elapsed(World) < 0.3) {
					return false;
				}
				if (Hero->GetEditingPiece() != Wall) {
					return Fail(TEXT("G from the other side did not start editing the wall"));
				}
				Test->TestEqual(TEXT("An edit starts from the wall as it is"), Hero->GetEditCut(), (1 << 4) | (1 << 1));
				Tap(Controller, EKeys::RightMouseButton);
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 0.6) {
				return false;
			}
			if (Step == 1) {
				Test->TestTrue(TEXT("The aim button ends the edit"), Hero->GetEditingPiece() == nullptr);
				Test->TestEqual(TEXT("The aim button puts every tile back"), Wall->Cut, 0);
				Test->TestEqual(TEXT("The whole wall shows again"), Wall->PieceMesh->GetInstanceCount(), 9);
				Tap(Controller, EKeys::X);
				Enter(EPhase::Stack, World);
				return false;
			}
			// four more rounds: 100 left, 25 a round
			if (Shots < 4) {
				if (Elapsed(World) < 0.5) {
					return false;
				}
				if (!AimAt(Controller, Hero, 4)) {
					return Elapsed(World) > 10.0 ? Fail(TEXT("Could not aim at the wall from the other side")) : false;
				}
				if (Shots == 0) {
					// both walls, the second standing on the first, before the first is brought down
					GameCheck::Shot(TEXT("Editing"), TEXT("stacked.png"));
				}
				Tap(Controller, EKeys::LeftMouseButton);
				++Shots;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			Enter(EPhase::Fell, World);
			return false;

		case EPhase::Stack: {
			// in build mode, looking well up: the wall goes one level up
			const FVector Eye = Controller->PlayerCameraManager->GetCameraLocation();
			switch (Step) {
			case 0:
				// up at where a wall on top of the first would be
				if (Elapsed(World) < 0.4 || !GameCheck::AimStep(Controller, Eye, Wall->GetActorLocation() + FVector(0.0, 0.0, ABuildingActor::Cell * 2.5), 0.5)) {
					return Elapsed(World) > 12.0 ? Fail(TEXT("Could not look up at where the second wall goes")) : false;
				}
				WoodBefore = State->MaterialCounts[0];
				Test->TestTrue(TEXT("Looking up, the wall preview is one level up, on the first wall"), Hero->BuildingPreview != nullptr
					&& Hero->BuildingPreview->GetActorLocation().Equals(Wall->GetActorLocation() + FVector(0.0, 0.0, ABuildingActor::Cell), 1.0));
				// the build button is read while it is held
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			case 1:
				if (Elapsed(World) < 0.8) {
					return false;
				}
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
				for (TActorIterator<ABuildingActor> It(World); It; ++It) {
					if (!It->IsPreview && *It != Wall && It->StructureType == TEXT("Wall")) {
						Upper = *It;
					}
				}
				if (!Upper.IsValid()) {
					return Fail(TEXT("The wall on top of the first was not built"));
				}
				Test->TestTrue(TEXT("The second wall is on top of the first"), Upper->GetActorLocation().Equals(Wall->GetActorLocation() + FVector(0.0, 0.0, ABuildingActor::Cell), 1.0));
				Test->TestTrue(TEXT("The first wall stands on the ground"), Wall->TouchesSolid());
				Test->TestFalse(TEXT("The second wall touches nothing but the first"), Upper->TouchesSolid());
				Test->TestTrue(TEXT("The first wall holds the second up"), ABuildingActor::HeldUp(World).Contains(Upper.Get()));
				Test->TestEqual(TEXT("The second wall is paid for"), State->MaterialCounts[0], WoodBefore - Hero->BuildingPieceCost);
				WoodBefore = State->MaterialCounts[0];
				Step = 2;
				PhaseStart = World->GetTimeSeconds();
				return false;
			case 2:
				// turned right round and still looking up: one level up on the far edge of her cell, where nothing is
				if (!GameCheck::AimStep(Controller, Eye, Hero->GetActorLocation() + Ahead * 400.0 + FVector(0.0, 0.0, 1600.0), 0.5)) {
					return Elapsed(World) > 12.0 ? Fail(TEXT("Could not look up and away from the walls")) : false;
				}
				Test->TestTrue(TEXT("The preview is one level up, away from both walls"), Hero->BuildingPreview != nullptr
					&& Hero->BuildingPreview->GetActorLocation().Equals(Wall->GetActorLocation() + Ahead * ABuildingActor::Cell + FVector(0.0, 0.0, ABuildingActor::Cell), 1.0));
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
				Step = 3;
				PhaseStart = World->GetTimeSeconds();
				return false;
			case 3:
				if (Elapsed(World) < 0.8) {
					return false;
				}
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
				Test->TestEqual(TEXT("A wall with nothing to hold it up is not built"), WallsStanding(World), 2);
				Test->TestEqual(TEXT("and costs nothing"), State->MaterialCounts[0], WoodBefore);
				UE_LOG(LogEditCheck, Display, TEXT("GAMECHECK support: a wall on a wall stands; a wall on nothing was not built; %d walls, wood %d"), WallsStanding(World), State->MaterialCounts[0]);
				// X leaves build mode and the rifle comes back to hand
				Tap(Controller, EKeys::X);
				Step = 4;
				PhaseStart = World->GetTimeSeconds();
				return false;
			default:
				if (Elapsed(World) < 1.2) {
					return false;
				}
				if (Hero->CurrentWeapon == nullptr || Hero->CurrentWeapon->WeaponType != 1) {
					return Fail(TEXT("Leaving build mode did not bring the rifle back"));
				}
				Enter(EPhase::Reset, World);
				Step = 2;
				Shots = 0;
				return false;
			}
		}

		case EPhase::Fell:
			// the first wall goes at once; the one it was holding stands a moment longer, then falls
			if (WallsStanding(World) > 0 && Elapsed(World) < 3.0) {
				return false;
			}
			Test->TestEqual(TEXT("The wall falls when it has taken its strength, and the wall it held with it"), WallsStanding(World), 0);
			Test->TestTrue(TEXT("The wall it held fell a moment after it, not with it"), bUpperStoodLonger);
			{
				int32 Pieces = 0;
				for (TActorIterator<ASennitePieceRubble> It(World); It; ++It) {
					Pieces += It->Tiles->GetInstanceCount();
				}
				Test->TestEqual(TEXT("Both fallen walls break into their nine tiles"), Pieces, 18);
			}
			UE_LOG(LogEditCheck, Display, TEXT("GAMECHECK support: the wall shot down took the wall it held with it, a moment after"));
			GameCheck::Shot(TEXT("Editing"), TEXT("fallen.png"));
			UE_LOG(LogEditCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteEditTest, "Sennite.Editing.EditAndDamage",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteEditTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
