// A hero's body, which holds a gun in both hands.
//
// Every hero has its own animations, made for its own weapon or for none: a caster's idle, a
// brawler's punch. None of them holds a rifle. So while a hero has a game gun in hand, its two arms
// are placed after its animation has run, every frame, on every machine that draws it:
// - the gun points where the player aims (the camera for the player's own hero, the aim the server
//   sends for everyone else's), turned no farther than a body can turn it;
// - the grip hand holds the gun by its grip in front of that arm's shoulder: low and close at the
//   ready, up at the eye while aiming in;
// - the other hand is the grip hand mirrored across the gun, on the gun's foregrip;
// - a gun too far for the other hand to reach comes toward that hand's shoulder until it can;
// - each arm bends at the elbow to reach its hand (two-bone IK), elbows down and out;
// - a shot kicks the gun back and up for a moment.
// Fingers, sleeves and anything else below the shoulder go with the arm as the animation left them.
// Where the hands go is in shares of the hero's own arm length, so it fits a small hero and a huge
// one; the numbers are in DefaultGame.ini.

#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "SenniteHeroMesh.generated.h"

UCLASS(Config = Game)
class SENNITE_API USenniteHeroMesh : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	/**
	 * Where the grip hand is at the ready, from the shoulder of its arm, in shares of the arm's length:
	 * X ahead along the aim, Y in toward the middle of the body, Z down.
	 */
	UPROPERTY(Config)
	FVector Ready = FVector(0.35, 0.40, 0.30);

	/** The same while aiming in. */
	UPROPERTY(Config)
	FVector Aimed = FVector(0.38, 0.46, 0.08);

	/** How far up or down, and how far to either side of the way the body faces, the gun turns to follow the aim. Degrees. */
	UPROPERTY(Config)
	float PitchReach = 60.0f;

	UPROPERTY(Config)
	float YawReach = 75.0f;

	/** How much of an arm's length a hand is asked to reach: a gun farther than this from the other hand's shoulder comes nearer. */
	UPROPERTY(Config)
	float Reach = 0.95f;

	/** A shot's kick: how far back (cm) and how far up (degrees) the gun goes, and for how long. */
	UPROPERTY(Config)
	float KickBack = 6.0f;

	UPROPERTY(Config)
	float KickUp = 3.0f;

	UPROPERTY(Config)
	float KickSeconds = 0.14f;

	/** A shot was fired from the gun in hand. */
	void Kick();

	/** Whether the last pose held a gun in both hands. */
	bool bHolding = false;

	/** After the last pose: how far each hand is from where it should be on the gun (cm), and how much of a kick was in it (0 to 1). */
	double GripGap = 0.0;
	double OffHandGap = 0.0;
	double KickNow = 0.0;

	/** The most kick seen since it was last set to nothing. */
	double KickMost = 0.0;

	virtual void FinalizeBoneTransform() override;

private:
	/** An arm: shoulder to elbow, elbow to wrist, the hand, and every other bone below the shoulder. */
	struct FArm
	{
		int32 Upper = INDEX_NONE;
		int32 Lower = INDEX_NONE;
		int32 Hand = INDEX_NONE;
		TArray<int32> Others;
		bool IsValid() const { return Upper != INDEX_NONE && Lower != INDEX_NONE && Hand != INDEX_NONE; }
	};

	/** Find both arms in the mesh the hero wears now. */
	bool FindArms();

	/** Place both arms on the gun in hand, in the pose about to be shown (component space). */
	void HoldGun(TArray<FTransform>& Pose);

	/** Bend an arm so its hand is at a place and turned a way, taking everything below the shoulder with it. Returns how far the hand is from the place. */
	double PlaceArm(TArray<FTransform>& Pose, const FArm& Arm, const FTransform& Hand, const FVector& Elbow) const;

	TWeakObjectPtr<const UObject> ArmsOf;
	FArm GripArm;
	FArm OffArm;
	double KickedAt = -1000.0;
};
