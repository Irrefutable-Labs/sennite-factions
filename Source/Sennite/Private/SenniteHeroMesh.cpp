// A hero's body, which holds a gun in both hands (SenniteHeroMesh.h).

#include "SenniteHeroMesh.h"

#include "SenniteCharacter.h"
#include "WeaponActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "ReferenceSkeleton.h"
#include "TwoBoneIK.h"

void USenniteHeroMesh::Kick()
{
	if (const UWorld* World = GetWorld()) {
		KickedAt = World->GetTimeSeconds();
	}
}

void USenniteHeroMesh::FinalizeBoneTransform()
{
	// the pose the animation made is in the buffer about to be shown: the arms are placed in it first
	bHolding = false;
	if (bNeedToFlipSpaceBaseBuffers) {
		HoldGun(GetEditableComponentSpaceTransforms());
	}
	Super::FinalizeBoneTransform();
}

bool USenniteHeroMesh::FindArms()
{
	const USkeletalMesh* Body = GetSkeletalMeshAsset();
	if (Body == nullptr) {
		return false;
	}
	if (ArmsOf.Get() == Body) {
		return GripArm.IsValid() && OffArm.IsValid();
	}
	ArmsOf = Body;
	const FReferenceSkeleton& Bones = Body->GetRefSkeleton();
	const auto Find = [&Bones](FArm& Arm, const TCHAR* Side) {
		Arm = FArm();
		Arm.Upper = Bones.FindBoneIndex(FName(*FString::Printf(TEXT("upperarm_%s"), Side)));
		Arm.Lower = Bones.FindBoneIndex(FName(*FString::Printf(TEXT("lowerarm_%s"), Side)));
		Arm.Hand = Bones.FindBoneIndex(FName(*FString::Printf(TEXT("hand_%s"), Side)));
		if (!Arm.IsValid()) {
			return;
		}
		// every other bone below the shoulder, parents before children as the skeleton lists them
		for (int32 Bone = Arm.Upper + 1; Bone < Bones.GetNum(); ++Bone) {
			if (Bone == Arm.Lower || Bone == Arm.Hand) {
				continue;
			}
			for (int32 Above = Bones.GetParentIndex(Bone); Above != INDEX_NONE; Above = Bones.GetParentIndex(Above)) {
				if (Above == Arm.Upper) {
					Arm.Others.Add(Bone);
					break;
				}
			}
		}
	};
	Find(GripArm, TEXT("r"));
	Find(OffArm, TEXT("l"));
	return GripArm.IsValid() && OffArm.IsValid();
}

double USenniteHeroMesh::PlaceArm(TArray<FTransform>& Pose, const FArm& Arm, const FTransform& Hand, const FVector& Elbow) const
{
	const FReferenceSkeleton& Bones = GetSkeletalMeshAsset()->GetRefSkeleton();
	// where each other bone of the arm is from its parent, as the animation left it
	TArray<FTransform, TInlineAllocator<64>> FromParent;
	for (const int32 Bone : Arm.Others) {
		FromParent.Add(Pose[Bone].GetRelativeTransform(Pose[Bones.GetParentIndex(Bone)]));
	}
	FTransform Upper = Pose[Arm.Upper];
	FTransform Lower = Pose[Arm.Lower];
	FTransform End = Pose[Arm.Hand];
	AnimationCore::SolveTwoBoneIK(Upper, Lower, End, Elbow, Hand.GetLocation(), false, 1.0, 1.0);
	End.SetRotation(Hand.GetRotation());
	Pose[Arm.Upper] = Upper;
	Pose[Arm.Lower] = Lower;
	Pose[Arm.Hand] = End;
	for (int32 Each = 0; Each < Arm.Others.Num(); ++Each) {
		const int32 Bone = Arm.Others[Each];
		Pose[Bone] = FromParent[Each] * Pose[Bones.GetParentIndex(Bone)];
	}
	return FVector::Dist(End.GetLocation(), Hand.GetLocation());
}

void USenniteHeroMesh::HoldGun(TArray<FTransform>& Pose)
{
	const ASenniteCharacter* Hero = Cast<ASenniteCharacter>(GetOwner());
	const AWeaponActor* Gun = Hero != nullptr ? Hero->CurrentWeapon : nullptr;
	const UWorld* World = GetWorld();
	// a gun, in this body's hand: the pickaxe is swung, not held like one
	if (Gun == nullptr || World == nullptr || Gun->WeaponType < 1 || Gun->WeaponMesh == nullptr || Gun->WeaponMesh->GetAttachParent() != this || !FindArms()) {
		return;
	}
	const FReferenceSkeleton& Bones = GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 HeldBy = Bones.FindBoneIndex(Gun->WeaponMesh->GetAttachSocketName());
	if (HeldBy == INDEX_NONE || !Pose.IsValidIndex(HeldBy) || !Pose.IsValidIndex(GripArm.Hand) || !Pose.IsValidIndex(OffArm.Hand)) {
		return;
	}
	const FTransform ToWorld = GetComponentTransform();

	// The way the gun points: the player's own hero follows the camera, anyone else's the aim the
	// server sends; no farther round or up than a body turns a gun.
	const FRotator Faces = Hero->GetActorRotation();
	double Pitch = Hero->AimPitch;
	double Yaw = Hero->AimYaw;
	if (const AController* Own = Hero->IsLocallyControlled() ? Hero->GetController() : nullptr) {
		const FRotator Look = Own->GetControlRotation();
		Pitch = FRotator::NormalizeAxis(Look.Pitch);
		Yaw = FMath::FindDeltaAngleDegrees(Faces.Yaw, Look.Yaw);
	}
	const FQuat Level = FRotator(FMath::Clamp(Pitch, -PitchReach, PitchReach), Faces.Yaw + FMath::Clamp(Yaw, -YawReach, YawReach), 0.0).Quaternion();

	// a shot's kick, strongest at the shot and gone KickSeconds later
	const double Since = World->GetTimeSeconds() - KickedAt;
	KickNow = KickSeconds > 0.0f && Since >= 0.0 && Since < KickSeconds ? FMath::Square(1.0 - Since / KickSeconds) : 0.0;
	KickMost = FMath::Max(KickMost, KickNow);
	const FQuat Points = Level * FRotator(KickUp * KickNow, 0.0, 0.0).Quaternion();
	const FVector Ahead = Level.GetAxisX();
	const FVector Side = Level.GetAxisY();
	const FVector Up = Level.GetAxisZ();

	// the grip hand's place, from its shoulder, by the length of the arm
	const FVector Shoulder = ToWorld.TransformPosition(Pose[GripArm.Upper].GetLocation());
	const double Arm = (FVector::Dist(Pose[GripArm.Upper].GetLocation(), Pose[GripArm.Lower].GetLocation())
		+ FVector::Dist(Pose[GripArm.Lower].GetLocation(), Pose[GripArm.Hand].GetLocation())) * ToWorld.GetScale3D().X;
	const FVector Place = FMath::Lerp(Ready, Aimed, Hero->AimedIn ? 1.0 : 0.0);
	const FVector Grip = Shoulder + Ahead * (Place.X * Arm - KickBack * KickNow) - Side * (Place.Y * Arm) - Up * (Place.Z * Arm);

	// the gun, its grip at that place; and from it the bone that holds it, then the hand. A big hero's
	// gun is larger than its model: where its grip and foregrip are is larger by as much.
	const double Size = Gun->WeaponMesh->GetRelativeScale3D().X;
	FTransform InHand = Gun->WeaponMesh->GetRelativeTransform();
	InHand.SetScale3D(FVector::OneVector);
	const FTransform GunAt(Points, Grip - Points.RotateVector(Gun->GripPoint * Size));
	const FTransform HolderAt = InHand.Inverse() * GunAt;
	FTransform HandAt = Pose[GripArm.Hand].GetRelativeTransform(Pose[HeldBy]) * HolderAt;

	// The other hand: the grip hand mirrored across the gun's upright plane, moved along the gun to its
	// foregrip. A mirrored bone's axes are the reflection of its twin's, each turned about.
	const auto Across = [&Side](const FVector& V) { return V - Side * (2.0 * FVector::DotProduct(V, Side)); };
	const FQuat Hand = HandAt.GetRotation();
	const FQuat Other = FMatrix(-Across(Hand.GetAxisX()), -Across(Hand.GetAxisY()), -Across(Hand.GetAxisZ()), FVector::ZeroVector).ToQuat();
	FVector Wrist = GunAt.TransformPosition(Gun->OffHandPoint * Size) + Across(HandAt.GetLocation() - Grip);

	// A gun too far for the other hand comes toward that hand's shoulder until it is in reach: the
	// whole gun, both hands with it.
	const FVector OffShoulder = ToWorld.TransformPosition(Pose[OffArm.Upper].GetLocation());
	const double OffArmLength = (FVector::Dist(Pose[OffArm.Upper].GetLocation(), Pose[OffArm.Lower].GetLocation())
		+ FVector::Dist(Pose[OffArm.Lower].GetLocation(), Pose[OffArm.Hand].GetLocation())) * ToWorld.GetScale3D().X;
	const FVector ToShoulder = OffShoulder - Wrist;
	const double TooFar = ToShoulder.Size() - OffArmLength * Reach;
	if (TooFar > 0.0) {
		const FVector Nearer = ToShoulder.GetSafeNormal() * TooFar;
		HandAt.AddToTranslation(Nearer);
		Wrist += Nearer;
	}

	const FVector GripElbow = Shoulder + (Side * 0.7 - Up - Ahead * 0.2) * Arm;
	GripGap = PlaceArm(Pose, GripArm, HandAt.GetRelativeTransform(ToWorld), ToWorld.InverseTransformPosition(GripElbow));
	const FVector OffElbow = OffShoulder + (-Side * 0.7 - Up - Ahead * 0.2) * Arm;
	OffHandGap = PlaceArm(Pose, OffArm, FTransform(Other, Wrist).GetRelativeTransform(ToWorld), ToWorld.InverseTransformPosition(OffElbow));
	bHolding = true;
}
