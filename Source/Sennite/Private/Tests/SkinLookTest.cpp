// Skin look check. Run in the game on the test range. The player's hero is given each skin the shop
// sells in turn, put on as the game puts it on (the hero becomes the skin's hero, then wears the skin's
// mesh on the hero's own animation), and for each:
// - the skin's mesh loads and has its hero's skeleton;
// - each part of it is drawn with the skin's own material for that part, not one the hero's own mesh left
//   on the body (which draws a skin's arms and legs in the wrong materials, half of them not at all);
// - no bone the skin's body is hung on is shrunk away: an arm or a leg whose bone has no size is not drawn,
//   and its armour is left hanging in the air;
// - a picture from in front and one from behind land in Saved/Skins/, for eyes.
//
// Tools/check_skins.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "SenniteBaseEditor.h"
#include "SenniteCharacter.h"
#include "SenniteHeroes.h"
#include "SennitePlayerState.h"
#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkinLook, Log, All);

namespace
{
	class FSkinLookRun : public IAutomationLatentCommand
	{
	public:
		explicit FSkinLookRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}

		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, Become, Wear, Front, Back, Next, Done };

		void Enter(EPhase To, UWorld* World) { Phase = To; PhaseStart = World->GetTimeSeconds(); }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		void Look(APlayerController* Controller, const ASenniteCharacter* Hero, double Ahead)
		{
			if (!Onlooker.IsValid()) {
				Onlooker = Controller->GetWorld()->SpawnActor<ACameraActor>();
			}
			if (Onlooker.IsValid()) {
				const FVector Chest = Hero->GetActorLocation() + FVector(0.0, 0.0, 10.0);
				const FVector From = Chest + Hero->GetActorForwardVector() * Ahead + Hero->GetActorRightVector() * 60.0 + FVector(0.0, 0.0, 30.0);
				Onlooker->SetActorLocationAndRotation(From, (Chest - From).Rotation());
				Controller->SetViewTarget(Onlooker.Get());
			}
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Index = 0;
		TWeakObjectPtr<ACameraActor> Onlooker;
	};

	bool FSkinLookRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		const TArray<FSenniteSkinMesh>& Skins = GetDefault<ASenniteBaseEditor>()->Skins;

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("No ASenniteCharacter standing on the ground after 60 s")) : false;
			}
			Test->TestTrue(TEXT("The shop's skins are listed"), Skins.Num() > 0);
			Enter(EPhase::Become, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr) {
			return Fail(TEXT("The hero went away during the check"));
		}
		if (!Skins.IsValidIndex(Index)) {
			UE_LOG(LogSkinLook, Display, TEXT("GAMECHECK finished"));
			return true;
		}
		const FSenniteSkinMesh& Skin = Skins[Index];
		USkeletalMeshComponent* Body = Hero->GetMesh();

		switch (Phase) {
		case EPhase::Become:
			State->SetHero(Skin.Hero);
			Enter(EPhase::Wear, World);
			return false;

		case EPhase::Wear: {
			if (Hero->HeroId != Skin.Hero || Elapsed(World) < 0.5) {
				return Elapsed(World) > 10.0 ? Fail(FString::Printf(TEXT("%s: the player did not become %s"), *Skin.Id, *Skin.Hero.ToString())) : false;
			}
			USkeletalMesh* Mesh = Cast<USkeletalMesh>(Skin.Mesh.TryLoad());
			const USkeletalMesh* Own = Body->GetSkeletalMeshAsset();
			Test->TestNotNull(FString::Printf(TEXT("%s: the skin's mesh loads"), *Skin.Id), Mesh);
			if (Mesh == nullptr) {
				++Index;
				Enter(EPhase::Become, World);
				return false;
			}
			Test->TestTrue(FString::Printf(TEXT("%s: it has its hero's skeleton"), *Skin.Id), Own != nullptr && Mesh->GetSkeleton() == Own->GetSkeleton());
			// as the game puts a skin on: the skin's mesh on the hero's own body and animation
			Hero->WearMesh(Mesh);
			Enter(EPhase::Front, World);
			return false;
		}

		case EPhase::Front: {
			if (Elapsed(World) < 1.0) {
				return false;
			}
			// every bone the body's skin is hung on has its size: one shrunk to nothing takes its part of the body with it
			TArray<FString> Gone;
			const TArray<FTransform>& Pose = Body->GetComponentSpaceTransforms();
			const FReferenceSkeleton& Bones = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
			for (int32 Bone = 0; Bone < Pose.Num() && Bone < Bones.GetNum(); ++Bone) {
				if (Pose[Bone].GetScale3D().GetAbsMin() < 0.2) {
					Gone.Add(Bones.GetBoneName(Bone).ToString());
				}
			}
			// each part of the skin is drawn with the skin's own material for that part, or the animation's own copy of it:
			// never one left on the body by the hero's mesh before it
			int32 Strangers = 0;
			const TArray<FSkeletalMaterial>& Own = Body->GetSkeletalMeshAsset()->GetMaterials();
			for (int32 Slot = 0; Slot < Own.Num(); ++Slot) {
				const UMaterialInterface* Drawn = Body->GetMaterial(Slot);
				// a part the pack ships with no material has none here either
				bool bItsOwn = Drawn == Own[Slot].MaterialInterface;
				for (const UMaterialInterface* Each = Drawn; Each != nullptr && !bItsOwn; ) {
					bItsOwn = Each == Own[Slot].MaterialInterface;
					const UMaterialInstance* Copy = Cast<UMaterialInstance>(Each);
					Each = Copy != nullptr ? Copy->Parent.Get() : nullptr;
				}
				if (!bItsOwn) {
					++Strangers;
					Test->AddError(FString::Printf(TEXT("%s: its part %d is drawn with %s, not its own %s"), *Skin.Id, Slot, Drawn != nullptr ? *Drawn->GetName() : TEXT("nothing"),
						Own[Slot].MaterialInterface != nullptr ? *Own[Slot].MaterialInterface->GetName() : TEXT("nothing")));
				}
			}
			UE_LOG(LogSkinLook, Display, TEXT("GAMECHECK skin %s on %s: %d parts, %d drawn with another mesh's material; %d bones, %d shrunk away%s%s; stands %.0f cm"), *Skin.Id, *Skin.Hero.ToString(),
				Own.Num(), Strangers, Pose.Num(), Gone.Num(), Gone.Num() > 0 ? TEXT(": ") : TEXT(""), *FString::Join(Gone, TEXT(" ")), Body->Bounds.BoxExtent.Z * 2.0);
			for (const FString& Each : Gone) {
				// the bones an arm or a leg is hung on, by the names every hero's skeleton gives them
				bool bLimb = false;
				for (const TCHAR* Part : { TEXT("upperarm_"), TEXT("lowerarm_"), TEXT("hand_l"), TEXT("hand_r"), TEXT("thigh_"), TEXT("calf_"), TEXT("foot_"), TEXT("clavicle_"), TEXT("spine_"), TEXT("neck_"), TEXT("head") }) {
					bLimb |= Each.StartsWith(Part) && !Each.Contains(TEXT("slide")) && !Each.Contains(TEXT("twist"));
				}
				Test->TestFalse(FString::Printf(TEXT("%s: the bone %s, part of its body, is not shrunk away"), *Skin.Id, *Each), bLimb);
			}
			Look(Controller, Hero, 260.0);
			GameCheck::Shot(TEXT("Skins"), *(Skin.Id + TEXT("_front.png")));
			Enter(EPhase::Back, World);
			return false;
		}

		case EPhase::Back:
			if (Elapsed(World) < 0.4) {
				return false;
			}
			Look(Controller, Hero, -260.0);
			GameCheck::Shot(TEXT("Skins"), *(Skin.Id + TEXT("_back.png")));
			Enter(EPhase::Next, World);
			return false;

		case EPhase::Next:
			if (Elapsed(World) < 0.4) {
				return false;
			}
			++Index;
			Enter(EPhase::Become, World);
			return false;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteSkinLookTest, "Sennite.Skins.Look",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteSkinLookTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSkinLookRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
