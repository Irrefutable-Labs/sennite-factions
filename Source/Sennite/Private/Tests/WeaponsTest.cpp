// Weapons check. Run in the game on the test range, which has a weapon crate to the right of the
// start: the hero walks to it and opens it, has the rifle and the shotgun it held with the rifle in
// her hand at once and nothing lying about, holds each one fixed to her weapon bone with her own
// built-in weapon hidden, switches between them with the number keys, and a pull of the trigger
// plays her firing animation, spends a round and sends a bullet out.
//
// Tools/check_weapons.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "ProjectileActor.h"
#include "SenniteCharacter.h"
#include "SennitePlayerState.h"
#include "SenniteWeapons.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeaponsCheck, Log, All);

namespace
{
	class FWeaponsRun : public IAutomationLatentCommand
	{
	public:
		explicit FWeaponsRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual ~FWeaponsRun() override
		{
			if (UWorld* World = SpawnWorld.Get()) {
				World->RemoveOnActorSpawnedHandler(SpawnHandle);
			}
		}

		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, OpenCrate, FromCrate, HoldShotgun, HoldRifle, Fire, HoldFire, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		/** The held weapon is of the class asked for, fixed to the hero's weapon bone, near her hand, with a mesh. */
		void CheckHeld(ASenniteCharacter* Hero, UClass* WeaponClass, const TCHAR* What)
		{
			AWeaponActor* Held = Hero->CurrentWeapon;
			Test->TestTrue(FString::Printf(TEXT("The hero holds the %s"), What), Held != nullptr && Held->GetClass() == WeaponClass);
			if (Held == nullptr) {
				return;
			}
			Test->TestTrue(FString::Printf(TEXT("The %s is fixed to the hero's body"), What), Held->WeaponMesh->GetAttachParent() == Hero->GetMesh());
			Test->TestEqual(FString::Printf(TEXT("The %s is fixed to the hero's weapon bone"), What), Held->WeaponMesh->GetAttachSocketName(), Hero->HeroWeaponBone);
			Test->TestNotNull(FString::Printf(TEXT("The %s has its mesh"), What), Held->WeaponMesh->GetStaticMesh().Get());
			Test->TestTrue(FString::Printf(TEXT("The %s shows"), What), Held->WeaponMesh->IsVisible() && !Held->IsHidden());
			const double FromHand = FVector::Dist(Held->WeaponMesh->GetComponentLocation(), Hero->GetMesh()->GetSocketLocation(TEXT("hand_r")));
			Test->TestTrue(FString::Printf(TEXT("The %s is in the hero's right hand"), What), FromHand < 45.0);
			Test->TestTrue(FString::Printf(TEXT("The %s belongs to the hero"), What), Held->Holder == Hero);
			UE_LOG(LogWeaponsCheck, Display, TEXT("GAMECHECK holding %s: class=%s mesh=%s from_hand=%.0f rounds=%d"), What, *Held->GetClass()->GetName(),
				*GetNameSafe(Held->WeaponMesh->GetStaticMesh()), FromHand, Held->CurrentBulletCount);
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		TWeakObjectPtr<UWorld> SpawnWorld;
		FDelegateHandle SpawnHandle;
		GameCheck::FCrateRun Crate;
		int32 BulletsSpawned = 0;
		int32 RoundsBefore = 0;
		int32 RoundsWhenLetGo = 0;
		bool bTriggerPulled = false;
		bool bSawFireAnimation = false;
		bool bShotTaken = false;
	};

	bool FWeaponsRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("No ASenniteCharacter standing on the ground after 60 s")) : false;
			}
			SpawnWorld = World;
			SpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Spawned) {
				BulletsSpawned += Spawned != nullptr && Spawned->IsA<AProjectileActor>() ? 1 : 0;
			}));
			// a run without a window starts with a stray mouse movement; begin from the view behind the hero
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			Test->TestTrue(TEXT("The hero starts with no game weapon in hand"), Hero->CurrentWeapon == nullptr);
			for (const FName& Bone : Hero->HeroBuiltInWeaponBones) {
				Test->TestTrue(TEXT("With no game weapon, the hero's own weapon shows"), !Hero->GetMesh()->IsBoneHiddenByName(Bone));
			}
			Test->TestTrue(TEXT("The hero has a built-in weapon to hide"), Hero->HeroBuiltInWeaponBones.Num() > 0);
			Test->TestEqual(TEXT("No weapon or healing item lies about on the range: they are in the crate"), GameCheck::LyingAbout(World), 0);
			Enter(EPhase::OpenCrate, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr) {
			return Fail(TEXT("The hero went away during the check"));
		}
		USkeletalMeshComponent* Mesh = Hero->GetMesh();

		switch (Phase) {
		case EPhase::OpenCrate:
			// D walks her to the right, to the crate; E opens it
			if (!Crate.Step(World, Controller, Hero, EKeys::D)) {
				return false;
			}
			if (!Crate.Failure.IsEmpty()) {
				return Fail(Crate.Failure);
			}
			Test->TestTrue(TEXT("Opening the crate gives her the rifle"), State->EquippedWeapons.Contains(1));
			Test->TestTrue(TEXT("Opening the crate gives her the shotgun"), State->EquippedWeapons.Contains(2));
			Enter(EPhase::FromCrate, World);
			return false;

		case EPhase::FromCrate:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			// the first weapon the crate gave is the one in hand, and nothing was laid on the ground
			CheckHeld(Hero, ASenniteRifle::StaticClass(), TEXT("rifle from the crate"));
			Test->TestEqual(TEXT("The opened crate laid nothing on the ground"), GameCheck::LyingAbout(World), 0);
			GameCheck::Key(Controller, EKeys::Three, IE_Pressed);
			GameCheck::Key(Controller, EKeys::Three, IE_Released);
			Enter(EPhase::HoldShotgun, World);
			return false;

		case EPhase::HoldShotgun:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (!bShotTaken) {
				bShotTaken = true;
				// key 3 brought the shotgun to hand
				CheckHeld(Hero, ASenniteShotgun::StaticClass(), TEXT("shotgun"));
				for (const FName& Bone : Hero->HeroBuiltInWeaponBones) {
					Test->TestTrue(TEXT("Holding a game weapon, the hero's own weapon is hidden"), Mesh->IsBoneHiddenByName(Bone));
				}
				GameCheck::Shot(TEXT("Weapons"), TEXT("shotgun.png"));
				return false;
			}
			if (Elapsed(World) < 1.5) {
				return false;
			}
			bShotTaken = false;
			GameCheck::Key(Controller, EKeys::Two, IE_Pressed);
			GameCheck::Key(Controller, EKeys::Two, IE_Released);
			Enter(EPhase::HoldRifle, World);
			return false;

		case EPhase::HoldRifle:
			if (Elapsed(World) < 0.7) {
				return false;
			}
			if (!bShotTaken) {
				bShotTaken = true;
				CheckHeld(Hero, ASenniteRifle::StaticClass(), TEXT("rifle"));
				Test->TestTrue(TEXT("The rifle has a full magazine"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->CurrentBulletCount == Hero->CurrentWeapon->MagazineSize);
				{
					// For setting up a hero: the grip that would hold a weapon level and facing the way she faces in this
					// pose, and where her palm is (part way to the root of her middle finger), both in the weapon bone's space.
					const FTransform Hand = Mesh->GetSocketTransform(Hero->HeroWeaponBone, RTS_World);
					const FRotator Level = (Hand.GetRotation().Inverse() * Hero->GetActorQuat()).Rotator();
					const FVector Palm = Hand.InverseTransformPosition(Mesh->GetSocketLocation(TEXT("middle_01_r"))) * 0.6;
					const FVector MuzzleAhead = Hero->GetActorTransform().InverseTransformVector(Hero->CurrentWeapon->WeaponMesh->GetForwardVector());
					UE_LOG(LogWeaponsCheck, Display, TEXT("GAMECHECK hand: level grip=(Pitch=%.1f,Yaw=%.1f,Roll=%.1f) palm=(X=%.1f,Y=%.1f,Z=%.1f) muzzle points (fwd %.2f, right %.2f, up %.2f)"),
						Level.Pitch, Level.Yaw, Level.Roll, Palm.X, Palm.Y, Palm.Z, MuzzleAhead.X, MuzzleAhead.Y, MuzzleAhead.Z);
					Test->TestTrue(TEXT("Standing, the hero holds the rifle pointing ahead of her, not behind or at the ground"), MuzzleAhead.X > 0.7);
				}
				GameCheck::Shot(TEXT("Weapons"), TEXT("rifle.png"));
				return false;
			}
			if (Hero->CurrentWeapon == nullptr) {
				return Fail(TEXT("The rifle did not come to hand, so there is nothing to fire"));
			}
			if (Elapsed(World) < 1.2) {
				return false;
			}
			RoundsBefore = Hero->CurrentWeapon->CurrentBulletCount;
			BulletsSpawned = 0;
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
			Enter(EPhase::Fire, World);
			return false;

		case EPhase::Fire:
			if (!bTriggerPulled) {
				bTriggerPulled = true;
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
			}
			bSawFireAnimation |= Mesh->GetAnimInstance() != nullptr && Mesh->GetAnimInstance()->Montage_IsPlaying(Hero->RifleHipShootingAnimation);
			if (Elapsed(World) < 0.5) {
				return false;
			}
			Test->TestNotNull(TEXT("The hero has a firing animation"), Hero->RifleHipShootingAnimation);
			Test->TestTrue(TEXT("Pulling the trigger plays the hero's firing animation"), bSawFireAnimation);
			Test->TestEqual(TEXT("One pull of the trigger sends one bullet out"), BulletsSpawned, 1);
			Test->TestTrue(TEXT("The shot spends one round"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->CurrentBulletCount == RoundsBefore - 1);
			UE_LOG(LogWeaponsCheck, Display, TEXT("GAMECHECK fire: bullets=%d rounds %d -> %d animation=%d"), BulletsSpawned, RoundsBefore,
				Hero->CurrentWeapon ? Hero->CurrentWeapon->CurrentBulletCount : -1, bSawFireAnimation ? 1 : 0);
			// then the button held down: the rifle goes on firing until it is let go
			RoundsBefore = Hero->CurrentWeapon ? Hero->CurrentWeapon->CurrentBulletCount : 0;
			bTriggerPulled = false;
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
			Enter(EPhase::HoldFire, World);
			return false;

		case EPhase::HoldFire:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (!bTriggerPulled) {
				bTriggerPulled = true;
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
				RoundsWhenLetGo = Hero->CurrentWeapon ? Hero->CurrentWeapon->CurrentBulletCount : -1;
				Test->TestTrue(TEXT("Held down for a second, the rifle fires four times or more"), RoundsWhenLetGo >= 0 && RoundsBefore - RoundsWhenLetGo >= 4);
				return false;
			}
			if (Elapsed(World) < 2.0) {
				return false;
			}
			Test->TestTrue(TEXT("Let go, the rifle stops: no more than the shot already asked for"), Hero->CurrentWeapon != nullptr && RoundsWhenLetGo - Hero->CurrentWeapon->CurrentBulletCount <= 1);
			UE_LOG(LogWeaponsCheck, Display, TEXT("GAMECHECK held fire: rounds %d -> %d in a second held, %d a second after letting go"), RoundsBefore, RoundsWhenLetGo,
				Hero->CurrentWeapon ? Hero->CurrentWeapon->CurrentBulletCount : -1);
			UE_LOG(LogWeaponsCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteWeaponsTest, "Sennite.Weapons.OpenHoldAndFire",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteWeaponsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FWeaponsRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
