// Building check. Run in the game on the test range: with the materials a player starts with, X
// shows a wall preview and the left mouse button builds the wall on the build grid; turning the
// camera a quarter turn and pressing C then F does the same for a ramp and a floor, the right mouse
// button changing the material in between; each piece costs its price once, has its material's
// strength, and is solid. G then edits the wall she built and the reload button takes it away whole:
// it is gone without rubble, heard as taken away, what it cost is given back once (asking again for the
// same piece is refused) and the ramp and the floor stand. The hero then runs up the ramp she built.
//
// Tools/check_building.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingActor.h"
#include "GameCheck.h"
#include "SenniteCharacter.h"
#include "SenniteGameMode.h"
#include "SennitePlayerState.h"
#include "SenniteSounds.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogBuildingCheck, Log, All);

namespace
{
	class FBuildingRun : public IAutomationLatentCommand
	{
	public:
		explicit FBuildingRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, WallPreview, WallBuilt, Turn, RampPreview, RampBuilt, FloorPreview, FloorBuilt, TakeAway, Climb, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		void Tap(APlayerController* Controller, const FKey& InKey) const
		{
			GameCheck::Key(Controller, InKey, IE_Pressed);
			GameCheck::Key(Controller, InKey, IE_Released);
		}

		/** The built (not preview) pieces of a kind standing in the world. */
		static TArray<ABuildingActor*> Built(UWorld* World, const TCHAR* Kind)
		{
			TArray<ABuildingActor*> Found;
			for (TActorIterator<ABuildingActor> It(World); It; ++It) {
				if (!It->IsPreview && It->StructureType == Kind) {
					Found.Add(*It);
				}
			}
			return Found;
		}

		void CheckPreview(ASenniteCharacter* Hero, const TCHAR* Kind, UClass* PreviewClass, const FVector& Where, double Yaw)
		{
			ABuildingActor* Preview = Hero->BuildingPreview;
			Test->TestTrue(FString::Printf(TEXT("The %s preview shows, in the class for its material"), Kind), Preview != nullptr && Preview->GetClass() == PreviewClass);
			if (Preview == nullptr) {
				return;
			}
			Test->TestTrue(FString::Printf(TEXT("The %s preview is marked a preview"), Kind), Preview->IsPreview);
			Test->TestTrue(FString::Printf(TEXT("The %s preview is where the piece will go"), Kind), Preview->GetActorLocation().Equals(Where, 1.0));
			Test->TestTrue(FString::Printf(TEXT("The %s preview is turned as the piece will be"), Kind), FMath::Abs(FMath::FindDeltaAngleDegrees(Preview->GetActorRotation().Yaw, Yaw)) < 1.0);
			Test->TestTrue(FString::Printf(TEXT("The %s preview does not stop anything"), Kind), Preview->PieceMesh->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block);
			Test->TestTrue(FString::Printf(TEXT("No %s is built by the preview alone"), Kind), Built(Hero->GetWorld(), Kind).Num() == 0);
		}

		void CheckBuilt(ASenniteCharacter* Hero, ASennitePlayerState* State, const TCHAR* Kind, UClass* PieceClass, int32 Material, const FVector& Where, double Yaw, int32 CountBefore)
		{
			const ABuildingActor* Shared = GetDefault<ABuildingActor>();
			const TArray<ABuildingActor*> Pieces = Built(Hero->GetWorld(), Kind);
			Test->TestEqual(FString::Printf(TEXT("Holding the button builds one %s, not one a frame"), Kind), Pieces.Num(), 1);
			if (Pieces.Num() == 0) {
				return;
			}
			const ABuildingActor* Piece = Pieces[0];
			Test->TestTrue(FString::Printf(TEXT("The %s is the class for its material"), Kind), Piece->GetClass() == PieceClass);
			Test->TestTrue(FString::Printf(TEXT("The %s stands on the build grid where the preview was"), Kind), Piece->GetActorLocation().Equals(Where, 1.0));
			Test->TestTrue(FString::Printf(TEXT("The %s is turned as the preview was"), Kind), FMath::Abs(FMath::FindDeltaAngleDegrees(Piece->GetActorRotation().Yaw, Yaw)) < 1.0);
			Test->TestTrue(FString::Printf(TEXT("The %s has its shape"), Kind), Piece->PieceMesh->GetStaticMesh() != nullptr);
			Test->TestEqual(FString::Printf(TEXT("The %s is made of its tiles, none cut"), Kind), Piece->PieceMesh->GetInstanceCount(), Piece->TileCount());
			const FSoftObjectPath Face = FCString::Strcmp(Kind, TEXT("Floor")) == 0 ? Shared->FloorMaterial : Shared->TierMaterials[Material];
			Test->TestEqual(FString::Printf(TEXT("The %s is faced with the panel for its material"), Kind), FSoftObjectPath(Piece->PieceMesh->GetMaterial(0)).ToString(), Face.ToString());
			Test->TestEqual(FString::Printf(TEXT("The %s has the strength of its material"), Kind), Piece->Health, Shared->TierHealth[Material]);
			Test->TestTrue(FString::Printf(TEXT("The %s is solid"), Kind), Piece->PieceMesh->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
			Test->TestEqual(FString::Printf(TEXT("The %s cost its price once"), Kind), State->MaterialCounts[Material], CountBefore - Hero->BuildingPieceCost);
			UE_LOG(LogBuildingCheck, Display, TEXT("GAMECHECK built %s: class=%s at=%s yaw=%.0f face=%s health=%.0f material %d: %d -> %d"), Kind, *Piece->GetClass()->GetName(),
				*Piece->GetActorLocation().ToCompactString(), Piece->GetActorRotation().Yaw, *GetNameSafe(Piece->PieceMesh->GetMaterial(0)), Piece->Health, Material, CountBefore, State->MaterialCounts[Material]);
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		int32 Start = 0;
		FVector Origin = FVector::ZeroVector;
		double StartZ = 0.0;
		double TopZ = 0.0;
		TWeakObjectPtr<ABuildingActor> Taken;
		FRotator LookBefore = FRotator::ZeroRotator;
		int32 WoodBefore = 0;
		int32 TakenAwayBefore = 0;
		bool AskedAgain = false;
		int32 DeniedBefore = 0;
		int32 DeniedAfter = 0;
		GameCheck::FMouseTurn Turn;
	};

	bool FBuildingRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		const double Cell = ABuildingActor::Cell;

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("No ASenniteCharacter standing on the ground after 60 s")) : false;
			}
			// a run without a window starts with a stray mouse movement; begin from the view behind the hero
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			const ASenniteGameMode* Mode = World->GetAuthGameMode<ASenniteGameMode>();
			Start = Mode ? Mode->StartingMaterialCount : -1;
			Test->TestTrue(TEXT("The game mode gives a player materials to start with"), Start >= 3 * Hero->BuildingPieceCost);
			for (int32 Material = 0; Material < 3; ++Material) {
				Test->TestEqual(TEXT("The player holds the starting count of each material"), State->MaterialCounts[Material], Start);
			}
			// the cell she stands in: the test range starts her on a grid point, facing +X
			Origin = FVector(FMath::RoundHalfFromZero(Hero->GetActorLocation().X / Cell) * Cell, FMath::RoundHalfFromZero(Hero->GetActorLocation().Y / Cell) * Cell, 0.0);
			Test->TestTrue(TEXT("No piece stands before anything is built"), Built(World, TEXT("Wall")).Num() + Built(World, TEXT("Ramp")).Num() + Built(World, TEXT("Floor")).Num() == 0);
			Tap(Controller, EKeys::X);
			Enter(EPhase::WallPreview, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr) {
			return Fail(TEXT("The hero went away during the check"));
		}

		switch (Phase) {
		case EPhase::WallPreview:
			if (Elapsed(World) < 0.4) {
				return false;
			}
			// facing +X: the wall goes on the far edge of her cell, across her way
			CheckPreview(Hero, TEXT("Wall"), Hero->WallPreviewClasses[0], Origin + FVector(Cell * 0.5, 0.0, 0.0), 90.0);
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
			Enter(EPhase::WallBuilt, World);
			return false;

		case EPhase::WallBuilt:
			// the button stays down for most of a second
			if (Elapsed(World) < 0.8) {
				return false;
			}
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
			CheckBuilt(Hero, State, TEXT("Wall"), Hero->WallClasses[0], 0, Origin + FVector(Cell * 0.5, 0.0, 0.0), 90.0, Start);
			Enter(EPhase::Turn, World);
			return false;

		case EPhase::Turn:
			// a quarter turn to the left with the mouse, to face -Y
			if (!Turn.Step(Controller, -90.0)) {
				return false;
			}
			if (Elapsed(World) < 0.6) {
				return false;
			}
			Test->TestTrue(TEXT("The mouse turned the camera a quarter turn to the left"), FMath::Abs(FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Yaw, -90.0)) < 20.0);
			Tap(Controller, EKeys::C);
			Enter(EPhase::RampPreview, World);
			return false;

		case EPhase::RampPreview:
			if (Elapsed(World) < 0.4) {
				return false;
			}
			// facing -Y: the ramp fills the next cell that way, rising away from her
			CheckPreview(Hero, TEXT("Ramp"), Hero->RampPreviewClasses[0], Origin + FVector(0.0, -Cell, 0.0), -90.0);
			GameCheck::Shot(TEXT("Building"), TEXT("preview.png"));
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
			Enter(EPhase::RampBuilt, World);
			return false;

		case EPhase::RampBuilt:
			if (Elapsed(World) < 0.8) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
				CheckBuilt(Hero, State, TEXT("Ramp"), Hero->RampClasses[0], 0, Origin + FVector(0.0, -Cell, 0.0), -90.0, Start - Hero->BuildingPieceCost);
				// the right mouse button moves on to the next material, then F asks for a floor
				Tap(Controller, EKeys::RightMouseButton);
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Tap(Controller, EKeys::F);
			Enter(EPhase::FloorPreview, World);
			return false;

		case EPhase::FloorPreview:
			if (Elapsed(World) < 0.4) {
				return false;
			}
			CheckPreview(Hero, TEXT("Floor"), Hero->FloorPreviewClasses[1], Origin + FVector(0.0, -Cell, 0.0), -90.0);
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
			Enter(EPhase::FloorBuilt, World);
			return false;

		case EPhase::FloorBuilt:
			if (Elapsed(World) < 0.8) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
				CheckBuilt(Hero, State, TEXT("Floor"), Hero->FloorClasses[1], 1, Origin + FVector(0.0, -Cell, 0.0), -90.0, Start);
				Test->TestEqual(TEXT("The third material was not touched"), State->MaterialCounts[2], Start);
				// F again leaves build mode
				Tap(Controller, EKeys::F);
				return false;
			}
			if (Elapsed(World) < 1.2) {
				return false;
			}
			if (Step == 1) {
				Step = 2;
				Test->TestTrue(TEXT("Pressing the piece's key again leaves build mode"), !State->InBuildMode);
				Test->TestTrue(TEXT("Out of build mode the preview is gone"), Hero->BuildingPreview == nullptr);
				GameCheck::Shot(TEXT("Building"), TEXT("built.png"));
				return false;
			}
			if (Elapsed(World) < 1.6) {
				return false;
			}
			LookBefore = Controller->GetControlRotation();
			Enter(EPhase::TakeAway, World);
			return false;

		case EPhase::TakeAway:
			// Out of build mode, looking at the wall she built: G edits it, and the reload button takes it away
			// whole. (The floor lies under the ramp, so from above the aim falls on the ramp first.)
			if (Step == 0) {
				const TArray<ABuildingActor*> Walls = Built(World, TEXT("Wall"));
				if (Walls.Num() != 1) {
					return Fail(TEXT("The wall she built is not standing"));
				}
				Taken = Walls[0];
				const FVector Middle = (Taken->TileTransform(Taken->TileCount() / 2, false) * Taken->GetActorTransform()).GetLocation();
				if (!GameCheck::AimStep(Controller, Controller->PlayerCameraManager->GetCameraLocation(), Middle, 0.5)) {
					return Elapsed(World) > 10.0 ? Fail(TEXT("Could not look at the wall she built")) : false;
				}
				int32 Tile = INDEX_NONE;
				Test->TestTrue(TEXT("The wall she built is hers to edit"), Hero->EditablePieceUnderAim(Tile) == Taken.Get());
				WoodBefore = State->MaterialCounts[0];
				TakenAwayBefore = USenniteSounds::Heard(World, TEXT("piece.remove"));
				Tap(Controller, EKeys::G);
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Step == 1) {
				if (Elapsed(World) < 0.3) {
					return false;
				}
				Test->TestTrue(TEXT("G begins editing the wall"), Hero->GetEditingPiece() == Taken.Get());
				// the reload button, while editing
				Tap(Controller, EKeys::R);
				Step = 2;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			// Taken away and not yet ended: the same ask a second time is refused and gives nothing back.
			if (!AskedAgain && Taken.IsValid() && Taken->bTakenAway) {
				AskedAgain = true;
				DeniedBefore = USenniteSounds::Heard(World, TEXT("ui.denied"));
				Hero->ServerRemovePiece(Taken.Get());
				DeniedAfter = USenniteSounds::Heard(World, TEXT("ui.denied"));
				return false;
			}
			if (Elapsed(World) < 0.8) {
				return false;
			}
			Test->TestTrue(TEXT("The piece was asked for a second time while it was taken away"), AskedAgain);
			Test->TestEqual(TEXT("The second ask is refused"), DeniedAfter, DeniedBefore + 1);
			Test->TestTrue(TEXT("Taking the piece away ends the edit"), Hero->GetEditingPiece() == nullptr);
			Test->TestFalse(TEXT("The wall is gone"), Taken.IsValid());
			Test->TestEqual(TEXT("No wall stands"), Built(World, TEXT("Wall")).Num(), 0);
			Test->TestEqual(TEXT("The ramp and the floor stand"), Built(World, TEXT("Ramp")).Num() + Built(World, TEXT("Floor")).Num(), 2);
			Test->TestEqual(TEXT("What the wall cost is given back, once"), State->MaterialCounts[0], WoodBefore + Hero->BuildingPieceCost);
			Test->TestEqual(TEXT("The piece is heard taken away, once"), USenniteSounds::Heard(World, TEXT("piece.remove")), TakenAwayBefore + 1);
			{
				int32 Rubble = 0;
				for (TActorIterator<ASennitePieceRubble> It(World); It; ++It) {
					++Rubble;
				}
				Test->TestEqual(TEXT("A piece taken away leaves no rubble"), Rubble, 0);
			}
			UE_LOG(LogBuildingCheck, Display, TEXT("GAMECHECK taken away: walls %d, wood %d -> %d, heard %d, asked again %d, refused %d"), Built(World, TEXT("Wall")).Num(),
				WoodBefore, State->MaterialCounts[0], USenniteSounds::Heard(World, TEXT("piece.remove")) - TakenAwayBefore, AskedAgain ? 1 : 0, DeniedAfter - DeniedBefore);
			// back to the view she had, facing the ramp
			Controller->SetControlRotation(LookBefore);
			StartZ = Hero->GetActorLocation().Z;
			TopZ = StartZ;
			GameCheck::Key(Controller, EKeys::W, IE_Pressed);
			GameCheck::Key(Controller, EKeys::LeftShift, IE_Pressed);
			Enter(EPhase::Climb, World);
			return false;

		case EPhase::Climb:
			// W with Left Shift runs her the way the camera faces: onto the ramp and up it
			TopZ = FMath::Max(TopZ, Hero->GetActorLocation().Z);
			if (Step == 0 && TopZ - StartZ > 0.6 * Cell) {
				Step = 1;
				GameCheck::Shot(TEXT("Building"), TEXT("on_ramp.png"));
			}
			if (Elapsed(World) < 2.2) {
				return false;
			}
			GameCheck::Key(Controller, EKeys::LeftShift, IE_Released);
			GameCheck::Key(Controller, EKeys::W, IE_Released);
			Test->TestTrue(TEXT("The hero runs up the ramp she built"), TopZ - StartZ > 0.6 * Cell);
			UE_LOG(LogBuildingCheck, Display, TEXT("GAMECHECK climb: rose %.0f of %.0f"), TopZ - StartZ, Cell);
			UE_LOG(LogBuildingCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteBuildingTest, "Sennite.Building.PreviewBuildAndClimb",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteBuildingTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBuildingRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
