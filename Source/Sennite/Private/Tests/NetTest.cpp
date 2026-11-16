// Two-player check: two copies of the game on one machine, one hosting the test range as a listen
// server and one joining it. Each runs its own half and checks what it sees of the other.
//
// The joining player (Sennite.Net.Joiner): gets a hero of their own and sees the host's, with its
// mesh and animation; holds the starting materials the server gave; previews and builds a wall with
// the real keys, and ends with exactly one wall, the server's, and the price taken; walks, and stays
// where they walked to; and sees the host's hero walk.
//
// The host (Sennite.Net.Host): sees the joining player's hero arrive; finds the wall that player
// built, on the grid by that player's cell, with the price taken from that player's count on the
// server; walks so the joining player can see it; and waits for the joining player to leave.
//
// Tools/check_two_players.sh starts both and reads both results.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingActor.h"
#include "GameCheck.h"
#include "SenniteCharacter.h"
#include "SenniteGameMode.h"
#include "SenniteHUD.h"
#include "SennitePlayerState.h"
#include "Animation/AnimInstance.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogNetCheck, Log, All);

namespace
{
	/** The hero in this world that is not the local player's. */
	ASenniteCharacter* OtherHero(UWorld* World, const ASenniteCharacter* Own)
	{
		for (TActorIterator<ASenniteCharacter> It(World); It; ++It) {
			if (*It != Own) {
				return *It;
			}
		}
		return nullptr;
	}

	TArray<ABuildingActor*> BuiltWalls(UWorld* World)
	{
		TArray<ABuildingActor*> Found;
		for (TActorIterator<ABuildingActor> It(World); It; ++It) {
			if (!It->IsPreview && It->StructureType == TEXT("Wall")) {
				Found.Add(*It);
			}
		}
		return Found;
	}

	/** The grid point of the build cell a hero stands in, at ground level. */
	FVector CellOf(const ASenniteCharacter* Hero)
	{
		const double Cell = ABuildingActor::Cell;
		return FVector(FMath::RoundHalfFromZero(Hero->GetActorLocation().X / Cell) * Cell, FMath::RoundHalfFromZero(Hero->GetActorLocation().Y / Cell) * Cell, 0.0);
	}

	bool HasHeroBody(const ASenniteCharacter* Hero)
	{
		const USkeletalMeshComponent* Mesh = Hero->GetMesh();
		return Mesh->GetSkeletalMeshAsset() != nullptr && FSoftObjectPath(Mesh->GetSkeletalMeshAsset()).ToString() == GetDefault<ASenniteCharacter>()->HeroMesh.ToString()
			&& Mesh->GetAnimInstance() != nullptr;
	}

	double AnimSpeed(const ASenniteCharacter* Hero)
	{
		UAnimInstance* Anim = Hero->GetMesh()->GetAnimInstance();
		const FNumericProperty* Property = Anim ? CastField<FNumericProperty>(Anim->GetClass()->FindPropertyByName(TEXT("Speed"))) : nullptr;
		return Property ? Property->GetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Anim)) : -1.0;
	}

	class FNetRun : public IAutomationLatentCommand
	{
	public:
		FNetRun(FAutomationTestBase* InTest, bool bInHost) : Test(InTest), bHost(bInHost), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override { return bHost ? UpdateHost() : UpdateJoiner(); }

	private:
		enum class EPhase { WaitForPawn, WaitForOther, Build, Built, Walk, Settle, WatchOther, WaitForWall, HostWalk, WaitForLeave, Done, Left };

		bool UpdateHost();
		bool UpdateJoiner();
		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		void Tap(APlayerController* Controller, const FKey& InKey) const
		{
			GameCheck::Key(Controller, InKey, IE_Pressed);
			GameCheck::Key(Controller, InKey, IE_Released);
		}

		FAutomationTestBase* Test;
		bool bHost;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		int32 Start = 0;
		FVector Origin = FVector::ZeroVector;
		FVector Mark = FVector::ZeroVector;
		FVector OtherMark = FVector::ZeroVector;
		double OtherTopSpeed = 0.0;
		double OtherTopAnimSpeed = 0.0;
		double LeftAt = 0.0;
	};

	bool FNetRun::UpdateJoiner()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		ASenniteHUD* Hud = Controller ? Cast<ASenniteHUD>(Controller->GetHUD()) : nullptr;
		ASenniteCharacter* Other = Hero ? OtherHero(World, Hero) : nullptr;
		const double Cell = ABuildingActor::Cell;

		if (Phase == EPhase::Left) {
			// a second for the goodbye to reach the host before this game closes
			return FPlatformTime::Seconds() - LeftAt > 1.5;
		}
		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || Hud == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 120.0 ? Fail(TEXT("The joining player got no hero within two minutes")) : false;
			}
			Test->TestTrue(TEXT("The joining player is on another machine's game, not hosting"), World->GetNetMode() == NM_Client);
			Test->TestTrue(TEXT("The joining player's hero is theirs to steer"), Hero->GetLocalRole() == ROLE_AutonomousProxy);
			Test->TestTrue(TEXT("The joining player's hero has its body"), HasHeroBody(Hero));
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			Enter(EPhase::WaitForOther, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr || Hud == nullptr) {
			return Fail(TEXT("The joining player's hero or HUD went away during the check"));
		}

		switch (Phase) {
		case EPhase::WaitForOther:
			if (Other == nullptr || Elapsed(World) < 1.5) {
				return Elapsed(World) > 30.0 ? Fail(TEXT("The joining player never saw the host's hero")) : false;
			}
			Start = GetDefault<ASenniteGameMode>()->StartingMaterialCount;
			Origin = CellOf(Hero);
			Test->TestTrue(TEXT("The joining player sees the host's hero with its body"), HasHeroBody(Other));
			Test->TestTrue(TEXT("The host's hero is a copy the server moves"), Other->GetLocalRole() == ROLE_SimulatedProxy);
			Test->TestTrue(TEXT("The two heroes do not start in the same place"), FVector::Dist2D(Hero->GetActorLocation(), Other->GetActorLocation()) > 200.0);
			Test->TestEqual(TEXT("The joining player holds the starting count of the first material"), State->MaterialCounts.IsValidIndex(0) ? State->MaterialCounts[0] : -1, Start);
			Test->TestTrue(TEXT("The HUD shows two players left"), Hud->LastDrawn.Contains(TEXT("alive=2 ALIVE")));
			UE_LOG(LogNetCheck, Display, TEXT("GAMECHECK joiner: own hero at %s, host's hero at %s"), *Hero->GetActorLocation().ToCompactString(), *Other->GetActorLocation().ToCompactString());
			Tap(Controller, EKeys::X);
			Enter(EPhase::Build, World);
			return false;

		case EPhase::Build:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestTrue(TEXT("The joining player sees the wall preview the server shows them"), Hero->BuildingPreview != nullptr && Hero->BuildingPreview->IsPreview
				&& Hero->BuildingPreview->GetActorLocation().Equals(Origin + FVector(Cell * 0.5, 0.0, 0.0), 1.0));
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
			Enter(EPhase::Built, World);
			return false;

		case EPhase::Built:
			if (Elapsed(World) < 0.8) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
				return false;
			}
			// time for the server's wall to arrive and the joining player's own copy to go
			if (Elapsed(World) < 2.3) {
				return false;
			}
			{
				const TArray<ABuildingActor*> Walls = BuiltWalls(World);
				Test->TestEqual(TEXT("The joining player ends with one wall, the server's"), Walls.Num(), 1);
				if (Walls.Num() > 0) {
					Test->TestTrue(TEXT("The wall is where the joining player built it"), Walls[0]->GetActorLocation().Equals(Origin + FVector(Cell * 0.5, 0.0, 0.0), 1.0));
					Test->TestTrue(TEXT("The wall is the server's, not the joining player's own copy"), Walls[0]->GetLocalRole() != ROLE_Authority);
					Test->TestTrue(TEXT("The wall has its body on the joining player's machine"), Walls[0]->PieceMesh->GetStaticMesh() != nullptr && Walls[0]->PieceMesh->GetMaterial(0) != nullptr);
				}
				Test->TestEqual(TEXT("The server took the wall's price from the joining player"), State->MaterialCounts[0], Start - Hero->BuildingPieceCost);
				Test->TestTrue(TEXT("The joining player's HUD shows the count after the wall"), Hud->LastDrawn.Contains(FString::Printf(TEXT("material0=%d"), Start - Hero->BuildingPieceCost)));
				UE_LOG(LogNetCheck, Display, TEXT("GAMECHECK joiner: walls=%d material0=%d"), Walls.Num(), State->MaterialCounts[0]);
			}
			GameCheck::Shot(TEXT("Net"), TEXT("joiner_built.png"));
			Tap(Controller, EKeys::X);
			Mark = Hero->GetActorLocation();
			GameCheck::Key(Controller, EKeys::A, IE_Pressed);
			Enter(EPhase::Walk, World);
			return false;

		case EPhase::Walk:
			// A walks the joining player to the left of the way the camera faces
			if (Elapsed(World) < 1.5) {
				return false;
			}
			GameCheck::Key(Controller, EKeys::A, IE_Released);
			Test->TestTrue(TEXT("The joining player's keys walk their hero"), FVector::Dist2D(Hero->GetActorLocation(), Mark) > 150.0);
			Enter(EPhase::Settle, World);
			return false;

		case EPhase::Settle:
			if (Elapsed(World) < 0.6) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				Mark = Hero->GetActorLocation();
				return false;
			}
			if (Elapsed(World) < 1.8) {
				return false;
			}
			// the server agrees with where the walk ended: it does not pull the hero back
			Test->TestTrue(TEXT("The server leaves the joining player where they walked to"), FVector::Dist2D(Hero->GetActorLocation(), Mark) < 20.0);
			UE_LOG(LogNetCheck, Display, TEXT("GAMECHECK joiner: walked to %s and stayed"), *Hero->GetActorLocation().ToCompactString());
			if (Other == nullptr) {
				return Fail(TEXT("The host's hero went away"));
			}
			OtherMark = Other->GetActorLocation();
			Enter(EPhase::WatchOther, World);
			return false;

		case EPhase::WatchOther:
			// the host walks once it has found the joining player's wall
			if (Other == nullptr) {
				return Fail(TEXT("The host's hero went away"));
			}
			OtherTopSpeed = FMath::Max(OtherTopSpeed, Other->GetVelocity().Size2D());
			OtherTopAnimSpeed = FMath::Max(OtherTopAnimSpeed, AnimSpeed(Other));
			if (FVector::Dist2D(Other->GetActorLocation(), OtherMark) < 250.0) {
				return Elapsed(World) > 40.0 ? Fail(TEXT("The joining player never saw the host's hero walk")) : false;
			}
			Test->TestTrue(TEXT("The joining player sees the host's hero walk"), OtherTopSpeed > 150.0);
			Test->TestTrue(TEXT("The host's hero animates as it walks on the joining player's machine"), OtherTopAnimSpeed > 100.0);
			UE_LOG(LogNetCheck, Display, TEXT("GAMECHECK joiner: saw the host's hero walk %.0f, top speed %.0f, animation speed %.0f"),
				FVector::Dist2D(Other->GetActorLocation(), OtherMark), OtherTopSpeed, OtherTopAnimSpeed);
			GameCheck::Shot(TEXT("Net"), TEXT("joiner_sees_host.png"));
			Enter(EPhase::Done, World);
			return false;

		case EPhase::Done:
			// leave a few frames for the screenshot, then leave the match as a player does, so the host
			// is told at once and is not left waiting for a connection that has gone
			if (Elapsed(World) < 0.6) {
				return false;
			}
			UE_LOG(LogNetCheck, Display, TEXT("GAMECHECK joiner: leaving the match"));
			UE_LOG(LogNetCheck, Display, TEXT("GAMECHECK finished"));
			Controller->ConsoleCommand(TEXT("disconnect"));
			LeftAt = FPlatformTime::Seconds();
			Phase = EPhase::Left;
			return false;

		default:
			return true;
		}
	}

	bool FNetRun::UpdateHost()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASenniteCharacter* Other = Hero ? OtherHero(World, Hero) : nullptr;
		const double Cell = ABuildingActor::Cell;

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("The host got no hero")) : false;
			}
			Test->TestTrue(TEXT("The host is a listen server"), World->GetNetMode() == NM_ListenServer);
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			Start = World->GetAuthGameMode<ASenniteGameMode>() ? World->GetAuthGameMode<ASenniteGameMode>()->StartingMaterialCount : -1;
			Enter(EPhase::WaitForOther, World);
			return false;
		}
		if (Hero == nullptr) {
			return Fail(TEXT("The host's hero went away during the check"));
		}

		switch (Phase) {
		case EPhase::WaitForOther:
			if (Other == nullptr || !Other->GetCharacterMovement()->IsMovingOnGround()) {
				return Elapsed(World) > 150.0 ? Fail(TEXT("No second player joined the host")) : false;
			}
			Test->TestTrue(TEXT("The host sees the joining player's hero with its body"), HasHeroBody(Other));
			Test->TestTrue(TEXT("The joining player's hero is another player's, on the server"), Other->GetController() != nullptr && Other->GetController() != Controller && !Other->IsLocallyControlled());
			Test->TestTrue(TEXT("The two heroes do not start in the same place"), FVector::Dist2D(Hero->GetActorLocation(), Other->GetActorLocation()) > 200.0);
			Origin = CellOf(Other);
			UE_LOG(LogNetCheck, Display, TEXT("GAMECHECK host: own hero at %s, joining player's at %s"), *Hero->GetActorLocation().ToCompactString(), *Other->GetActorLocation().ToCompactString());
			Enter(EPhase::WaitForWall, World);
			return false;

		case EPhase::WaitForWall:
			if (BuiltWalls(World).Num() == 0) {
				return Elapsed(World) > 60.0 ? Fail(TEXT("The joining player's wall never reached the host")) : false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			// a second for a duplicate to show itself, if the held button made one
			if (Elapsed(World) < 1.5) {
				return false;
			}
			{
				const TArray<ABuildingActor*> Walls = BuiltWalls(World);
				const ASennitePlayerState* TheirState = Other && Other->GetController() ? Cast<ASennitePlayerState>(Other->GetController()->PlayerState) : nullptr;
				Test->TestEqual(TEXT("The host has one wall from the joining player"), Walls.Num(), 1);
				Test->TestTrue(TEXT("The wall is on the grid by the joining player's cell"), Walls.Num() > 0 && Walls[0]->GetActorLocation().Equals(Origin + FVector(Cell * 0.5, 0.0, 0.0), 1.0));
				Test->TestTrue(TEXT("The wall is the server's own"), Walls.Num() > 0 && Walls[0]->HasAuthority());
				Test->TestTrue(TEXT("The server took the wall's price from the joining player, not the host"), TheirState != nullptr && TheirState->MaterialCounts[0] == Start - Hero->BuildingPieceCost);
				const ASennitePlayerState* OwnState = Cast<ASennitePlayerState>(Controller->PlayerState);
				Test->TestTrue(TEXT("The host's own materials are untouched"), OwnState != nullptr && OwnState->MaterialCounts[0] == Start);
				UE_LOG(LogNetCheck, Display, TEXT("GAMECHECK host: walls=%d joining player's material0=%d host's=%d"), Walls.Num(), TheirState ? TheirState->MaterialCounts[0] : -1, OwnState ? OwnState->MaterialCounts[0] : -1);
			}
			Enter(EPhase::HostWalk, World);
			return false;

		case EPhase::HostWalk:
			// give the joining player time to finish their own walk and start watching, then walk forward
			if (Elapsed(World) < 6.0) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				Mark = Hero->GetActorLocation();
				GameCheck::Key(Controller, EKeys::W, IE_Pressed);
				return false;
			}
			if (Elapsed(World) < 8.5) {
				return false;
			}
			GameCheck::Key(Controller, EKeys::W, IE_Released);
			Test->TestTrue(TEXT("The host's keys walk the host's hero"), FVector::Dist2D(Hero->GetActorLocation(), Mark) > 300.0);
			GameCheck::Shot(TEXT("Net"), TEXT("host_view.png"));
			Enter(EPhase::WaitForLeave, World);
			return false;

		case EPhase::WaitForLeave:
			// the joining player quits when their half is done
			if (Other != nullptr) {
				return Elapsed(World) > 60.0 ? Fail(TEXT("The joining player never left")) : false;
			}
			UE_LOG(LogNetCheck, Display, TEXT("GAMECHECK host: the joining player left"));
			UE_LOG(LogNetCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteNetHostTest, "Sennite.Net.Host", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteNetHostTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNetRun(this, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteNetJoinerTest, "Sennite.Net.Joiner", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteNetJoinerTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNetRun(this, false));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
