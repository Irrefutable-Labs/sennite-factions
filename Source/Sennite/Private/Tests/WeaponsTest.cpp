// Weapons check. Run in the game on the test range, which has a weapon crate to the right of the
// start. With no gun in hand the fire button attacks with the hero's own weapon: she plays her own
// attack and a bolt leaves her staff. Then the hero walks to the crate and opens it, has the rifle and the shotgun it held with the rifle in
// her hand at once and nothing lying about, holds each one fixed to her weapon bone with her own
// built-in weapon hidden and both hands on the gun, which points where the player aims, switches
// between them with the number keys, and a pull of the trigger kicks the gun, spends a round and
// sends a bullet out: a bolt that is seen, starts at
// the muzzle, flies the way the gun points and is sent to every machine.
//
// Tools/check_weapons.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "ProjectileActor.h"
#include "SenniteCharacter.h"
#include "SenniteHeroMesh.h"
#include "SennitePlayerState.h"
#include "SenniteWeapons.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
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
		enum class EPhase { WaitForPawn, OwnWeapon, OpenCrate, FromCrate, HoldShotgun, HoldRifle, Fire, HoldFire, OtherGuns, Done };

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
		int32 Step = 0;
		int32 OtherGun = 0;
		UClass* BulletClass = nullptr;
		FVector OwnTip = FVector::ZeroVector;
		GameCheck::FOnlooker Onlooker;
		/** The last bullet fired: where it started, how far it has been seen from there, and what it looked like. */
		TWeakObjectPtr<AProjectileActor> Bullet;
		bool bBulletNoted = false;
		FVector BulletFrom = FVector::ZeroVector;
		FVector BulletWay = FVector::ZeroVector;
		double BulletFar = 0.0;
		double BulletOffLine = 0.0;
		double BulletFromMuzzle = 0.0;
		bool bBoltSeen = false;
		bool bBulletSent = false;
		float BoltLongest = 0.0f;
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
				if (AProjectileActor* Fired = Cast<AProjectileActor>(Spawned)) {
					++BulletsSpawned;
					BulletClass = Fired->GetClass();
					Bullet = Fired;
					bBulletNoted = false;
					// where it starts, before it has flown at all
					BulletFrom = Fired->GetActorLocation();
					BulletWay = Fired->GetActorForwardVector();
					const ASenniteCharacter* Shooter = Cast<ASenniteCharacter>(Fired->GetWorld()->GetFirstPlayerController()->GetPawn());
					BulletFromMuzzle = Shooter != nullptr && Shooter->CurrentWeapon != nullptr ? FVector::Dist(BulletFrom, Shooter->CurrentWeapon->GetMuzzleLocation()) : -1.0;
				}
			}));
			// a run without a window starts with a stray mouse movement; begin from the view behind the hero
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			Test->TestTrue(TEXT("The hero starts with no game weapon in hand"), Hero->CurrentWeapon == nullptr);
			for (const FName& Bone : Hero->HeroBuiltInWeaponBones) {
				Test->TestTrue(TEXT("With no game weapon, the hero's own weapon shows"), !Hero->GetMesh()->IsBoneHiddenByName(Bone));
			}
			Test->TestTrue(TEXT("The hero has a built-in weapon to hide"), Hero->HeroBuiltInWeaponBones.Num() > 0);
			Test->TestEqual(TEXT("No weapon or healing item lies about on the range: they are in the crate"), GameCheck::LyingAbout(World), 0);
			// every gun has a name and a shot to be heard, and opening a crate a sound (a run without sound cannot hear them)
			for (UClass* Gun : { ASenniteRifle::StaticClass(), ASenniteShotgun::StaticClass(), ASenniteEnergyRifle::StaticClass(), ASenniteSniper::StaticClass() }) {
				const AWeaponActor* Made = GetDefault<AWeaponActor>(Gun);
				Test->TestTrue(FString::Printf(TEXT("%s has a name"), *Gun->GetName()), !Made->DisplayName.IsEmpty());
				Test->TestTrue(FString::Printf(TEXT("%s has a shot sound that loads"), *Gun->GetName()), Cast<USoundBase>(Made->FireSound.TryLoad()) != nullptr);
			}
			Test->TestTrue(TEXT("A shot fades with distance"), Cast<USoundAttenuation>(GetDefault<AWeaponActor>()->FireFalloff.TryLoad()) != nullptr);
			Test->TestTrue(TEXT("Opening a crate has a sound that loads"), Cast<USoundBase>(GetDefault<ASenniteCrate>()->OpenSound.TryLoad()) != nullptr);
			Enter(EPhase::OwnWeapon, World);
			Step = 0;
			return false;
		}
		if (Hero == nullptr || State == nullptr) {
			return Fail(TEXT("The hero went away during the check"));
		}
		USkeletalMeshComponent* Mesh = Hero->GetMesh();
		const USenniteHeroMesh* Body = Cast<USenniteHeroMesh>(Mesh);
		// the bullet in flight, each frame it is there
		if (const AProjectileActor* Flying = Bullet.Get()) {
			if (!bBulletNoted) {
				bBulletNoted = true;
				BulletFar = 0.0;
				BulletOffLine = 0.0;
				BoltLongest = 0.0f;
				bBoltSeen = Flying->Bolt != nullptr && Flying->Bolt->GetStaticMesh() != nullptr && Flying->Bolt->GetMaterial(0) != nullptr && Flying->Bolt->IsVisible() && !Flying->IsHidden();
				bBulletSent = Flying->GetIsReplicated() && Flying->IsReplicatingMovement();
				if (BulletsSpawned == 4) {
					// a bolt of the held burst, on its way
					GameCheck::Shot(TEXT("Weapons"), TEXT("bolt.png"));
				}
			}
			const FVector Gone = Flying->GetActorLocation() - BulletFrom;
			BulletFar = FMath::Max(BulletFar, FVector::DotProduct(Gone, BulletWay));
			BulletOffLine = FMath::Max(BulletOffLine, (Gone - BulletWay * FVector::DotProduct(Gone, BulletWay)).Size());
			BoltLongest = FMath::Max(BoltLongest, Flying->Bolt->GetRelativeScale3D().X * 100.0f);
		}

		switch (Phase) {
		case EPhase::OwnWeapon:
			// No gun in hand: the fire button attacks with the hero's own weapon. She plays her own attack
			// and a bolt leaves her staff and travels; R does nothing, there being nothing to load.
			if (Step == 0) {
				if (Elapsed(World) < 0.8) {
					return false;
				}
				Step = 1;
				BulletsSpawned = 0;
				BulletClass = nullptr;
				bSawFireAnimation = false;
				OwnTip = Hero->OwnWeaponTip();
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			bSawFireAnimation |= Hero->OwnAttack != nullptr && Mesh->GetAnimInstance() != nullptr && Mesh->GetAnimInstance()->Montage_IsPlaying(Hero->OwnAttack);
			if (Step == 1) {
				if (Elapsed(World) < 0.25) {
					return false;
				}
				Step = 2;
				// a second pull before her attack is over sends nothing, and R loads nothing
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
				GameCheck::Key(Controller, EKeys::R, IE_Pressed);
				GameCheck::Key(Controller, EKeys::R, IE_Released);
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestNotNull(TEXT("The hero has an attack of her own"), Hero->OwnAttack.Get());
			Test->TestTrue(TEXT("With no gun in hand, the fire button plays the hero's own attack"), bSawFireAnimation);
			Test->TestEqual(TEXT("and one bolt leaves her own weapon, not two: the second pull came too soon"), BulletsSpawned, 1);
			Test->TestTrue(TEXT("The bolt is her own weapon's"), BulletClass == ASenniteOwnBolt::StaticClass());
			Test->TestTrue(TEXT("The bolt starts at the top of her staff"), FVector::Dist(BulletFrom, OwnTip) < 40.0);
			Test->TestTrue(TEXT("The bolt is seen and sent to every machine"), bBoltSeen && bBulletSent);
			Test->TestTrue(TEXT("The bolt travels: seen more than 10 m out, straight"), BulletFar > 1000.0 && BulletOffLine < 5.0);
			Test->TestTrue(TEXT("She still holds no game weapon"), Hero->CurrentWeapon == nullptr);
			UE_LOG(LogWeaponsCheck, Display, TEXT("GAMECHECK own weapon: attack=%d bolts=%d of %s, started %.0f from the staff's top, seen %.0f out"),
				bSawFireAnimation ? 1 : 0, BulletsSpawned, *GetNameSafe(BulletClass), FVector::Dist(BulletFrom, OwnTip), BulletFar);
			GameCheck::Shot(TEXT("Weapons"), TEXT("own_weapon.png"));
			bSawFireAnimation = false;
			Enter(EPhase::OpenCrate, World);
			return false;

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
				Onlooker.Look(World, Controller, Hero);
				GameCheck::Shot(TEXT("Weapons"), TEXT("shotgun.png"));
				return false;
			}
			if (Elapsed(World) < 1.5) {
				return false;
			}
			Onlooker.Back(Controller, Hero);
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
					// both hands on it, and it points where the player aims
					const FVector Aim = Controller->GetControlRotation().Vector();
					const double AlongAim = FVector::DotProduct(Hero->CurrentWeapon->WeaponMesh->GetForwardVector(), Aim);
					Test->TestTrue(TEXT("The hero's body holds the rifle in both hands"), Body != nullptr && Body->bHolding);
					Test->TestTrue(TEXT("Her grip hand is on the rifle's grip and her other hand on its foregrip"), Body != nullptr && Body->GripGap < 2.0 && Body->OffHandGap < 3.0);
					Test->TestTrue(TEXT("The rifle points where the player aims"), AlongAim > 0.99);
					Test->TestTrue(TEXT("With a gun in hand she faces where the player aims"), FMath::Abs(FMath::FindDeltaAngleDegrees(Hero->GetActorRotation().Yaw, Controller->GetControlRotation().Yaw)) < 5.0);
					UE_LOG(LogWeaponsCheck, Display, TEXT("GAMECHECK hold: both hands=%d grip hand %.1f cm off, other hand %.1f cm off, rifle along the aim %.3f"),
						Body != nullptr && Body->bHolding ? 1 : 0, Body ? Body->GripGap : -1.0, Body ? Body->OffHandGap : -1.0, AlongAim);
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
			bSawFireAnimation |= Body != nullptr && Body->KickNow > 0.3;
			if (Elapsed(World) < 0.5) {
				return false;
			}
			Test->TestTrue(TEXT("Pulling the trigger kicks the gun in her hands"), bSawFireAnimation);
			Test->TestEqual(TEXT("One pull of the trigger sends one bullet out"), BulletsSpawned, 1);
			Test->TestTrue(TEXT("The bullet is seen: a bolt with a shape and a material, showing"), bBoltSeen);
			Test->TestTrue(TEXT("The bullet is sent to every machine, with where it is going"), bBulletSent);
			Test->TestTrue(TEXT("The bullet starts at the rifle's muzzle"), BulletFromMuzzle >= 0.0 && BulletFromMuzzle < 60.0);
			Test->TestTrue(TEXT("The bullet travels: seen more than 10 m out from where it started"), BulletFar > 1000.0);
			Test->TestTrue(TEXT("The bullet flies straight, the way it was fired"), BulletOffLine < 5.0);
			Test->TestTrue(TEXT("The bolt grows behind the bullet to its length and no longer"), BoltLongest > 100.0f && BoltLongest <= GetDefault<ASenniteRifleBullet>()->BoltLength + 0.5f);
			UE_LOG(LogWeaponsCheck, Display, TEXT("GAMECHECK bolt: seen=%d sent=%d started %.0f from the muzzle, seen %.0f out, %.1f off its line, bolt up to %.0f long"),
				bBoltSeen ? 1 : 0, bBulletSent ? 1 : 0, BulletFromMuzzle, BulletFar, BulletOffLine, BoltLongest);
			Test->TestTrue(TEXT("The shot spends one round"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->CurrentBulletCount == RoundsBefore - 1);
			UE_LOG(LogWeaponsCheck, Display, TEXT("GAMECHECK fire: bullets=%d rounds %d -> %d kick=%d"), BulletsSpawned, RoundsBefore,
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
			OtherGun = 0;
			Enter(EPhase::OtherGuns, World);
			Step = 0;
			return false;

		case EPhase::OtherGuns: {
			// the energy rifle (6) and the sniper (7): each comes to both hands, and a pull of the trigger sends its own bolt
			struct FOther { FKey Key; UClass* Gun; UClass* Shot; const TCHAR* Name; };
			const FOther Others[] = { { EKeys::Six, ASenniteEnergyRifle::StaticClass(), ASenniteEnergyBolt::StaticClass(), TEXT("energy rifle") },
				{ EKeys::Seven, ASenniteSniper::StaticClass(), ASenniteSniperBullet::StaticClass(), TEXT("sniper") } };
			const FOther& One = Others[OtherGun];
			if (Step == 0) {
				Step = 1;
				GameCheck::Key(Controller, One.Key, IE_Pressed);
				GameCheck::Key(Controller, One.Key, IE_Released);
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Step == 1) {
				if (Elapsed(World) < 1.2) {
					return false;
				}
				Step = 2;
				CheckHeld(Hero, One.Gun, One.Name);
				Test->TestTrue(FString::Printf(TEXT("The %s is held in both hands"), One.Name), Body != nullptr && Body->bHolding && Body->GripGap < 2.0 && Body->OffHandGap < 3.0);
				Test->TestTrue(FString::Printf(TEXT("The %s starts with a full magazine"), One.Name), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->CurrentBulletCount == Hero->CurrentWeapon->MagazineSize);
				Onlooker.Look(World, Controller, Hero);
				GameCheck::Shot(TEXT("Weapons"), *FString::Printf(TEXT("%s.png"), *FString(One.Name).Replace(TEXT(" "), TEXT("_"))));
				BulletsSpawned = 0;
				BulletClass = nullptr;
				RoundsBefore = Hero->CurrentWeapon != nullptr ? Hero->CurrentWeapon->CurrentBulletCount : 0;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Step == 2) {
				if (Elapsed(World) < 0.4) {
					return false;
				}
				Step = 3;
				Onlooker.Back(Controller, Hero);
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
				GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 0.8) {
				return false;
			}
			Test->TestEqual(FString::Printf(TEXT("One pull of the %s's trigger sends one bolt out"), One.Name), BulletsSpawned, 1);
			Test->TestTrue(FString::Printf(TEXT("The %s fires its own bolt"), One.Name), BulletClass == One.Shot);
			Test->TestTrue(FString::Printf(TEXT("The %s's shot spends one round"), One.Name), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->CurrentBulletCount == RoundsBefore - 1);
			UE_LOG(LogWeaponsCheck, Display, TEXT("GAMECHECK %s: bolts=%d of %s, rounds %d -> %d"), One.Name, BulletsSpawned, *GetNameSafe(BulletClass), RoundsBefore,
				Hero->CurrentWeapon ? Hero->CurrentWeapon->CurrentBulletCount : -1);
			if (++OtherGun < UE_ARRAY_COUNT(Others)) {
				Step = 0;
				return false;
			}
			UE_LOG(LogWeaponsCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;
		}

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
