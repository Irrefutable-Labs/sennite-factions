// First playable check. Run in the game (not the editor) on a map that has a player start:
// the player's pawn is ASenniteCharacter itself, wearing the hero named in DefaultGame.ini with the
// materials the hero ships with, the hero's animation Blueprint is running and its pose changes, and
// the keys and mouse bound in DefaultInput.ini walk, sprint, jump and turn the camera.
//
// The keys are pressed on the player controller, so everything from the input bindings down is the
// game's own code. Tools/check_first_playable.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "SenniteCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogFirstPlayable, Log, All);

namespace
{
	/** How far a set of sampled points spread: the diagonal of their bounding box. */
	struct FSpread
	{
		FBox Box = FBox(ForceInit);
		void Add(const FVector& Point) { Box += Point; }
		double Size() const { return Box.IsValid ? Box.GetSize().Size() : 0.0; }
	};

	class FFirstPlayableRun : public IAutomationLatentCommand
	{
	public:
		explicit FFirstPlayableRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}

		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, Idle, Walk, Sprint, Jump, Stop, Look, FrontShot, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }

		void Key(APlayerController* Controller, const FKey& InKey, EInputEvent Event) const { GameCheck::Key(Controller, InKey, Event); }
		void Mouse(APlayerController* Controller, const FKey& Axis, float Counts) const { GameCheck::Mouse(Controller, Axis, Counts); }
		void Shot(const TCHAR* Name) const { GameCheck::Shot(TEXT("FirstPlayable"), Name); }
		static double AnimNumber(UAnimInstance* Anim, const TCHAR* Name)
		{
			const FNumericProperty* Property = CastField<FNumericProperty>(Anim->GetClass()->FindPropertyByName(Name));
			return Property ? Property->GetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Anim)) : -1.0;
		}
		static FVector BonePosition(USkeletalMeshComponent* Mesh, FName Bone)
		{
			return Mesh->GetSocketTransform(Bone, RTS_Component).GetLocation();
		}

		bool Fail(const FString& Why)
		{
			Test->AddError(Why);
			return true;
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;

		FVector WalkFrom = FVector::ZeroVector;
		double WalkSpeed = 0.0;
		double JumpFromZ = 0.0;
		double JumpPeak = 0.0;
		bool bSawFalling = false;
		bool bSprintShot = false;
		FRotator LookFrom = FRotator::ZeroRotator;
		FVector LookFromLocation = FVector::ZeroVector;
		int32 LookFrames = 0;
		FSpread IdlePose;
		FSpread WalkFeet;
	};

	bool FFirstPlayableRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				if (FPlatformTime::Seconds() - RealStart > 60.0) {
					return Fail(FString::Printf(TEXT("No ASenniteCharacter standing on the ground after 60 s (world %s, controller %s, pawn %s)"),
						World ? *World->GetMapName() : TEXT("none"), Controller ? *Controller->GetName() : TEXT("none"),
						Controller && Controller->GetPawn() ? *Controller->GetPawn()->GetClass()->GetName() : TEXT("none")));
				}
				return false;
			}

			const ASenniteCharacter* Defaults = GetDefault<ASenniteCharacter>();
			USkeletalMeshComponent* Mesh = Hero->GetMesh();
			USkeletalMesh* Body = Mesh->GetSkeletalMeshAsset();
			UAnimInstance* Anim = Mesh->GetAnimInstance();

			Test->TestTrue(TEXT("The pawn's class is ASenniteCharacter itself"), Hero->GetClass() == ASenniteCharacter::StaticClass());
			Test->TestNotNull(TEXT("The pawn has a skeletal mesh"), Body);
			Test->TestNotNull(TEXT("The pawn has a running animation instance"), Anim);
			if (Body == nullptr || Anim == nullptr) {
				return true;
			}
			Test->TestEqual(TEXT("The mesh is the hero named in DefaultGame.ini"), FSoftObjectPath(Body).ToString(), Defaults->HeroMesh.ToString());
			Test->TestEqual(TEXT("The animation class is the hero's own, named in DefaultGame.ini"), FSoftClassPath(Anim->GetClass()).ToString(), Defaults->HeroAnimClass.ToString());
			Test->TestTrue(TEXT("The hero pack is a Paragon hero"), Defaults->HeroMesh.ToString().StartsWith(TEXT("/Game/Paragon")));

			const TArray<FSkeletalMaterial>& Shipped = Body->GetMaterials();
			int32 Changed = 0;
			for (int32 Slot = 0; Slot < Shipped.Num(); ++Slot) {
				// the hero's animation Blueprint wraps each of its materials in a live instance, whose parent is the shipped one
				const UMaterialInterface* Shown = Mesh->GetMaterial(Slot);
				if (const UMaterialInstanceDynamic* Live = Cast<UMaterialInstanceDynamic>(Shown)) {
					Shown = Live->Parent;
				}
				const UMaterialInterface* Own = Shipped[Slot].MaterialInterface;
				if (Shown != Own) {
					++Changed;
					UE_LOG(LogFirstPlayable, Error, TEXT("GAMECHECK material slot %d shows %s, the hero ships %s"), Slot, *GetPathNameSafe(Shown), *GetPathNameSafe(Own));
				}
			}
			Test->TestEqual(TEXT("Every material slot shows the material the hero ships with"), Changed, 0);
			Test->TestTrue(TEXT("The hero has material slots"), Shipped.Num() > 0);

			// A run without a window feeds one large stray mouse movement on its first frame, which leaves the camera
			// pointing at the ground. Start from the view a player has before touching the mouse: behind the hero.
			UE_LOG(LogFirstPlayable, Display, TEXT("GAMECHECK view before reset=%s"), *Controller->GetControlRotation().ToCompactString());
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			UE_LOG(LogFirstPlayable, Display, TEXT("GAMECHECK map=%s pawn=%s mesh=%s anim=%s materials=%d"),
				*World->GetMapName(), *Hero->GetClass()->GetPathName(), *Body->GetPathName(), *Anim->GetClass()->GetName(), Shipped.Num());
			Enter(EPhase::Idle, World);
			return false;
		}

		if (Hero == nullptr) {
			return Fail(TEXT("The hero went away during the check"));
		}
		USkeletalMeshComponent* Mesh = Hero->GetMesh();
		UAnimInstance* Anim = Mesh->GetAnimInstance();
		UCharacterMovementComponent* Movement = Hero->GetCharacterMovement();
		const double Speed = Hero->GetVelocity().Size2D();

		switch (Phase) {
		case EPhase::Idle:
			// standing still, the idle animation still moves her hands and head
			IdlePose.Add(BonePosition(Mesh, TEXT("hand_r")));
			IdlePose.Add(BonePosition(Mesh, TEXT("hand_l")) + FVector(1000.0, 0.0, 0.0));
			if (Elapsed(World) < 1.5) {
				return false;
			}
			// two hands are sampled 1000 apart, so anything over 1000 is movement of the hands themselves
			Test->TestTrue(TEXT("Standing still, the idle animation moves the hero's hands (needs a renderer: run without -nullrhi)"), IdlePose.Size() > 1000.2);
			Test->TestTrue(TEXT("Standing still, the hero does not drift"), Speed < 1.0);
			{
				const FVector Camera = Controller->PlayerCameraManager->GetCameraLocation();
				const double CameraDistance = FVector::Dist(Camera, Hero->GetActorLocation());
				const FVector Back = -Hero->GetActorForwardVector();
				Test->TestTrue(TEXT("The camera stands off from the hero, in third person"), CameraDistance > 250.0);
				Test->TestTrue(TEXT("The camera starts behind the hero"), FVector::DotProduct((Camera - Hero->GetActorLocation()).GetSafeNormal2D(), Back) > 0.9);
				UE_LOG(LogFirstPlayable, Display, TEXT("GAMECHECK idle hand_travel=%.2f camera_distance=%.0f"), IdlePose.Size() - 1000.0, CameraDistance);
			}
			WalkFrom = Hero->GetActorLocation();
			Key(Controller, EKeys::W, IE_Pressed);
			Enter(EPhase::Walk, World);
			return false;

		case EPhase::Walk:
			WalkFeet.Add(BonePosition(Mesh, TEXT("foot_l")));
			if (Elapsed(World) < 1.5) {
				return false;
			}
			{
				const FVector Moved = Hero->GetActorLocation() - WalkFrom;
				const FVector Forward = FRotator(0.0, Controller->GetControlRotation().Yaw, 0.0).Vector();
				WalkSpeed = Speed;
				Test->TestTrue(TEXT("Holding W sets the walking state"), Hero->IsWalking && !Hero->IsRunning);
				Test->TestTrue(TEXT("Holding W moves the hero forward, the way the camera faces"), FVector::DotProduct(Moved, Forward) > 150.0);
				Test->TestTrue(TEXT("The hero walks at a walking speed"), Speed > 150.0 && Speed < 0.6 * Movement->MaxWalkSpeed);
				Test->TestTrue(TEXT("The hero turns to face the way she walks"), FMath::Abs(FMath::FindDeltaAngleDegrees(Hero->GetActorRotation().Yaw, Forward.Rotation().Yaw)) < 10.0);
				Test->TestTrue(TEXT("The animation Blueprint sees the hero's speed"), AnimNumber(Anim, TEXT("Speed")) > 100.0);
				Test->TestTrue(TEXT("Walking, the hero's feet step"), WalkFeet.Size() > 10.0);
				UE_LOG(LogFirstPlayable, Display, TEXT("GAMECHECK walk moved=%.0f speed=%.0f anim_speed=%.0f foot_travel=%.1f"),
					FVector::DotProduct(Moved, Forward), Speed, AnimNumber(Anim, TEXT("Speed")), WalkFeet.Size());
			}
			Key(Controller, EKeys::LeftShift, IE_Pressed);
			Enter(EPhase::Sprint, World);
			return false;

		case EPhase::Sprint:
			if (!bSprintShot && Elapsed(World) > 0.8) {
				bSprintShot = true;
				Shot(TEXT("running.png"));
			}
			if (Elapsed(World) < 1.2) {
				return false;
			}
			Test->TestTrue(TEXT("Holding Left Shift with W sets the running state"), Hero->IsRunning);
			Test->TestTrue(TEXT("Running is faster than walking"), Speed > WalkSpeed + 100.0);
			UE_LOG(LogFirstPlayable, Display, TEXT("GAMECHECK sprint speed=%.0f"), Speed);
			Key(Controller, EKeys::LeftShift, IE_Released);
			JumpFromZ = Hero->GetActorLocation().Z;
			JumpPeak = JumpFromZ;
			Key(Controller, EKeys::SpaceBar, IE_Pressed);
			Enter(EPhase::Jump, World);
			return false;

		case EPhase::Jump:
			bSawFalling |= Movement->IsFalling();
			JumpPeak = FMath::Max(JumpPeak, Hero->GetActorLocation().Z);
			if (Elapsed(World) < 0.2 || (Movement->IsFalling() && Elapsed(World) < 3.0)) {
				return false;
			}
			Test->TestTrue(TEXT("Space Bar takes the hero off the ground"), bSawFalling);
			Test->TestTrue(TEXT("The jump rises"), JumpPeak - JumpFromZ > 30.0);
			Test->TestTrue(TEXT("The hero lands again"), Movement->IsMovingOnGround());
			UE_LOG(LogFirstPlayable, Display, TEXT("GAMECHECK jump rise=%.0f"), JumpPeak - JumpFromZ);
			Key(Controller, EKeys::SpaceBar, IE_Released);
			Key(Controller, EKeys::W, IE_Released);
			Enter(EPhase::Stop, World);
			return false;

		case EPhase::Stop:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestTrue(TEXT("Letting go of W clears the walking state"), !Hero->IsWalking);
			Test->TestTrue(TEXT("Letting go of W stops the hero"), Speed < 5.0);
			LookFrom = Controller->GetControlRotation();
			LookFromLocation = Hero->GetActorLocation();
			Enter(EPhase::Look, World);
			return false;

		case EPhase::Look:
			// the mouse turns the camera around the hero and tips it down, without moving her
			if (LookFrames < 30) {
				++LookFrames;
				// mouse counts, as a mouse reports them: the engine scales them down to degrees
				Mouse(Controller, EKeys::MouseX, 30.0f);
				Mouse(Controller, EKeys::MouseY, -1.0f);
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			{
				const FRotator Now = Controller->GetControlRotation();
				const double Yaw = FMath::Abs(FMath::FindDeltaAngleDegrees(LookFrom.Yaw, Now.Yaw));
				const double Pitch = FMath::Abs(FMath::FindDeltaAngleDegrees(LookFrom.Pitch, Now.Pitch));
				const FVector ToHero = (Hero->GetActorLocation() - Controller->PlayerCameraManager->GetCameraLocation()).GetSafeNormal();
				Test->TestTrue(TEXT("Moving the mouse sideways turns the camera"), Yaw > 20.0);
				Test->TestTrue(TEXT("Moving the mouse up or down tips the camera"), Pitch > 3.0);
				Test->TestTrue(TEXT("The camera still looks at the hero"), FVector::DotProduct(Controller->PlayerCameraManager->GetCameraRotation().Vector(), ToHero) > 0.8);
				Test->TestTrue(TEXT("Looking around does not move the hero"), FVector::Dist(Hero->GetActorLocation(), LookFromLocation) < 1.0);
				UE_LOG(LogFirstPlayable, Display, TEXT("GAMECHECK look yaw=%.0f pitch=%.0f (camera pitch now %.0f)"), Yaw, Pitch, FRotator::NormalizeAxis(Now.Pitch));
			}
			Shot(TEXT("standing.png"));
			Enter(EPhase::FrontShot, World);
			return false;

		case EPhase::FrontShot:
			// leave a few frames for the screenshot to be written
			if (Elapsed(World) < 0.5) {
				return false;
			}
			UE_LOG(LogFirstPlayable, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteFirstPlayableTest, "Sennite.FirstPlayable.HeroSpawnsAnimatesAndMoves",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteFirstPlayableTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFirstPlayableRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
