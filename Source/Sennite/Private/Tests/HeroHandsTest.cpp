// Hero hands check. Run in the game on the test range: the hero walks over the rifle and the shotgun,
// takes the rifle in hand, and then becomes each hero of the list in turn, as the server makes a
// player the hero they chose (ASennitePlayerState::SetHero; this game is its own server). As each hero:
// - the rifle is fixed to the hero's weapon bone, in the right hand, pointing ahead of them;
// - the hero's own built-in weapon, if it has one, is hidden;
// - a pull of the trigger plays the hero's own firing animation, if it ships with one.
//
// It also measures, for setting up a hero, what WeaponsTest.cpp measures for Dekker: the grip that
// would hold a weapon level and facing the way the hero faces in this pose, and where the palm is
// (part way to the root of the middle finger), in the weapon bone's space; the same for the left
// hand; and the bones that are not the body's own. Each hero's bones go to Saved/HeroHands/<id>.txt,
// with a picture of the hero holding the rifle (<id>.png), from the player's camera.
//
// Tools/check_hero_hands.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "SenniteCharacter.h"
#include "SenniteHeroes.h"
#include "SennitePlayerState.h"
#include "SenniteWeapons.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeroHands, Log, All);

namespace
{
	class FHeroHandsRun : public IAutomationLatentCommand
	{
	public:
		explicit FHeroHandsRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}

		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, WalkOverPickups, TakeRifle, Become, Measure, Shot, Fire, Done };

		/** Where the palm is, part way to the root of the middle finger, or of the index finger on a hand that has no middle finger; zero on a hand with neither. */
		static FVector PalmOf(USkeletalMeshComponent* Mesh, FName HandBone, const TCHAR* Side)
		{
			const FTransform Hand = Mesh->GetSocketTransform(HandBone, RTS_World);
			for (const TCHAR* Finger : { TEXT("middle_01_"), TEXT("index_01_") }) {
				const FName Bone(*(FString(Finger) + Side));
				if (Mesh->GetBoneIndex(Bone) != INDEX_NONE && Mesh->GetParentBone(Bone) == HandBone) {
					return Hand.InverseTransformPosition(Mesh->GetSocketLocation(Bone)) * 0.6;
				}
			}
			return FVector::ZeroVector;
		}

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 HeroIndex = 0;
		int32 RoundsBefore = 0;
		bool bSawFire = false;
	};

	bool FHeroHandsRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		const TArray<FSenniteHero>& Heroes = USenniteHeroes::All();

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("No ASenniteCharacter standing on the ground after 60 s")) : false;
			}
			Test->TestTrue(TEXT("The list has heroes"), Heroes.Num() > 0);
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			GameCheck::Key(Controller, EKeys::D, IE_Pressed);
			Enter(EPhase::WalkOverPickups, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr) {
			return Fail(TEXT("The hero went away during the check"));
		}
		USkeletalMeshComponent* Mesh = Hero->GetMesh();

		switch (Phase) {
		case EPhase::WalkOverPickups:
			if (!(State->EquippedWeapons.Contains(1) && State->EquippedWeapons.Contains(2)) && Elapsed(World) < 6.0) {
				return false;
			}
			GameCheck::Key(Controller, EKeys::D, IE_Released);
			if (!State->EquippedWeapons.Contains(1)) {
				return Fail(TEXT("Walking over the rifle did not pick it up"));
			}
			GameCheck::Key(Controller, EKeys::Two, IE_Pressed);
			GameCheck::Key(Controller, EKeys::Two, IE_Released);
			Enter(EPhase::TakeRifle, World);
			return false;

		case EPhase::TakeRifle:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Hero->CurrentWeapon == nullptr || !Hero->CurrentWeapon->IsA<ASenniteRifle>()) {
				return Fail(TEXT("The rifle did not come to hand"));
			}
			Enter(EPhase::Become, World);
			return false;

		case EPhase::Become:
			// the hero, as the server sets it from a player's choice
			State->SetHero(Heroes[HeroIndex].Id);
			Enter(EPhase::Measure, World);
			return false;

		case EPhase::Measure: {
			if (Elapsed(World) < 1.5) {
				return false;
			}
			const FSenniteHero& Want = Heroes[HeroIndex];
			const FString IdText = Want.Id.ToString();
			const TCHAR* Id = *IdText;
			Test->TestEqual(FString::Printf(TEXT("%s: the player state is the hero chosen"), Id), State->HeroId, Want.Id);
			Test->TestEqual(FString::Printf(TEXT("%s: the character is the hero chosen"), Id), Hero->HeroId, Want.Id);
			Test->TestEqual(FString::Printf(TEXT("%s: the body is the hero's mesh"), Id), FSoftObjectPath(Mesh->GetSkeletalMeshAsset()).ToString(), Want.Mesh.ToString());
			Test->TestTrue(FString::Printf(TEXT("%s: the hero's own animation Blueprint runs"), Id),
				Mesh->GetAnimInstance() != nullptr && FSoftClassPath(Mesh->GetAnimInstance()->GetClass()).ToString() == Want.AnimClass.ToString());

			// every bone, and those that are not the body's: where a built-in weapon would be
			TArray<FString> Lines;
			TArray<FString> NotBody;
			static const TCHAR* Gear[] = { TEXT("weapon"), TEXT("gun"), TEXT("staff"), TEXT("sword"), TEXT("blade"), TEXT("hammer"), TEXT("rifle"),
				TEXT("barrel"), TEXT("mag"), TEXT("stock"), TEXT("axe"), TEXT("shield"), TEXT("bow"), TEXT("launcher"), TEXT("cannon"), TEXT("knife"),
				TEXT("scope"), TEXT("prop"), TEXT("bazooka"), TEXT("club"), TEXT("mace") };
			for (int32 Bone = 0; Bone < Mesh->GetNumBones(); ++Bone) {
				const FName Name = Mesh->GetBoneName(Bone);
				const FName Parent = Mesh->GetParentBone(Name);
				Lines.Add(FString::Printf(TEXT("%s <- %s"), *Name.ToString(), *Parent.ToString()));
				for (const TCHAR* Word : Gear) {
					if (Name.ToString().Contains(Word)) {
						NotBody.Add(Name.ToString());
						break;
					}
				}
			}
			FFileHelper::SaveStringArrayToFile(Lines, *(FPaths::ProjectSavedDir() / TEXT("HeroHands") / (Want.Id.ToString() + TEXT(".txt"))));

			const bool bHasHand = Mesh->GetBoneIndex(Want.WeaponBone) != INDEX_NONE;
			Test->TestTrue(FString::Printf(TEXT("%s: the skeleton has the weapon bone %s"), Id, *Want.WeaponBone.ToString()), bHasHand);
			Test->TestTrue(FString::Printf(TEXT("%s: the skeleton has the item bone %s"), Id, *Want.ItemBone.ToString()), Mesh->GetBoneIndex(Want.ItemBone) != INDEX_NONE);
			if (bHasHand) {
				const FTransform Hand = Mesh->GetSocketTransform(Want.WeaponBone, RTS_World);
				const FRotator Level = (Hand.GetRotation().Inverse() * Hero->GetActorQuat()).Rotator();
				const FVector Palm = PalmOf(Mesh, Want.WeaponBone, TEXT("r"));
				const FVector PalmLeft = PalmOf(Mesh, Want.ItemBone, TEXT("l"));
				UE_LOG(LogHeroHands, Display, TEXT("GAMECHECK %s measured: level grip=(Pitch=%.1f,Yaw=%.1f,Roll=%.1f) palm=(X=%.1f,Y=%.1f,Z=%.1f) item palm=(X=%.1f,Y=%.1f,Z=%.1f) gear bones=[%s]"),
					Id, Level.Pitch, Level.Yaw, Level.Roll, Palm.X, Palm.Y, Palm.Z, PalmLeft.X, PalmLeft.Y, PalmLeft.Z, *FString::Join(NotBody, TEXT(",")));
			}

			// the rifle, as this hero holds it
			AWeaponActor* Rifle = Hero->CurrentWeapon;
			if (Rifle == nullptr) {
				return Fail(TEXT("The rifle left the hero's hand"));
			}
			Test->TestEqual(FString::Printf(TEXT("%s: the rifle is fixed to the hero's weapon bone"), Id), Rifle->WeaponMesh->GetAttachSocketName(), Want.WeaponBone);
			const double FromHand = FVector::Dist(Rifle->WeaponMesh->GetComponentLocation(), Mesh->GetSocketLocation(Want.WeaponBone));
			const FVector Ahead = Hero->GetActorTransform().InverseTransformVector(Rifle->WeaponMesh->GetForwardVector());
			const FVector Up = Hero->GetActorTransform().InverseTransformVector(Rifle->WeaponMesh->GetUpVector());
			Test->TestTrue(FString::Printf(TEXT("%s: the rifle is in the hero's right hand"), Id), FromHand < 45.0);
			Test->TestTrue(FString::Printf(TEXT("%s: standing, the hero holds the rifle pointing ahead"), Id), Ahead.X > 0.7);
			Test->TestTrue(FString::Printf(TEXT("%s: the rifle is held top up, not on its side or upside down"), Id), Up.Z > 0.5);
			for (const FName& Bone : Want.BuiltInWeaponBones) {
				Test->TestTrue(FString::Printf(TEXT("%s: holding the rifle, the hero's own weapon (%s) is hidden"), Id, *Bone.ToString()), Mesh->IsBoneHiddenByName(Bone));
			}
			UE_LOG(LogHeroHands, Display, TEXT("GAMECHECK %s holds: from_hand=%.0f muzzle (fwd %.2f, right %.2f, up %.2f) top (fwd %.2f, right %.2f, up %.2f) hidden=%d"),
				Id, FromHand, Ahead.X, Ahead.Y, Ahead.Z, Up.X, Up.Y, Up.Z, Want.BuiltInWeaponBones.Num());
			GameCheck::Shot(TEXT("HeroHands"), *(Want.Id.ToString() + TEXT(".png")));
			Enter(EPhase::Shot, World);
			return false;
		}

		case EPhase::Shot:
			// the picture is taken at the end of a frame: nothing moves until it is
			if (Elapsed(World) < 0.3) {
				return false;
			}
			RoundsBefore = Hero->CurrentWeapon != nullptr ? Hero->CurrentWeapon->CurrentBulletCount : 0;
			bSawFire = false;
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
			GameCheck::Key(Controller, EKeys::LeftMouseButton, IE_Released);
			Enter(EPhase::Fire, World);
			return false;

		case EPhase::Fire: {
			const FSenniteHero& Want = Heroes[HeroIndex];
			UAnimMontage* Fire = Hero->RifleHipShootingAnimation;
			bSawFire |= Fire != nullptr && Mesh->GetAnimInstance() != nullptr && Mesh->GetAnimInstance()->Montage_IsPlaying(Fire);
			if (Elapsed(World) < 0.6) {
				return false;
			}
			const FString IdText = Want.Id.ToString();
			const TCHAR* Id = *IdText;
			Test->TestTrue(FString::Printf(TEXT("%s: the trigger spends a round"), Id), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->CurrentBulletCount == RoundsBefore - 1);
			if (!Want.FireMontage.IsNull()) {
				Test->TestEqual(FString::Printf(TEXT("%s: the firing animation is the hero's own"), Id), FSoftObjectPath(Fire).ToString(), Want.FireMontage.ToString());
				Test->TestTrue(FString::Printf(TEXT("%s: pulling the trigger plays it"), Id), bSawFire);
			}
			else {
				Test->TestNull(FString::Printf(TEXT("%s: a hero with no firing animation plays none"), Id), Fire);
			}
			UE_LOG(LogHeroHands, Display, TEXT("GAMECHECK %s fire: animation=%s played=%d"), Id, *GetNameSafe(Fire), bSawFire ? 1 : 0);
			if (++HeroIndex < Heroes.Num()) {
				Enter(EPhase::Become, World);
				return false;
			}
			UE_LOG(LogHeroHands, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;
		}

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteHeroHandsTest, "Sennite.Heroes.Hands",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteHeroHandsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHeroHandsRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
