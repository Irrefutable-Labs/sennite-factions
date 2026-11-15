// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SenniteCharacter.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSenniteCharacter, Log, All);

class USpringArmComponent;
class UCameraComponent;
class UCapsuleComponent;
class ABuildingActor;
class AWeaponActor;
class AHealingActor;
class ASennitePlayerState;
class UThirdPersonAnimInstance;
class AStormActor;
class UAnimMontage;

UCLASS(config=Game)
class ASenniteCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	UPROPERTY(VisibleAnywhere, Category = "Trigger Capsule")
	UCapsuleComponent* TriggerCapsule;

public:
	ASenniteCharacter();

	/** Base turn rate, in deg/sec. Other scaling may affect final turn rate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Camera)
	float BaseTurnRate;

	/** Base look up/down rate, in deg/sec. Other scaling may affect final rate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Camera)
	float BaseLookUpRate;

	/* Class for wall preview actor */
	UPROPERTY(EditDefaultsOnly, Category = "Wall")
	TArray<TSubclassOf<ABuildingActor>> WallPreviewClasses;

	/* Class for wall preview actor */
	UPROPERTY(EditDefaultsOnly, Category = "Wall")
	TArray<TSubclassOf<ABuildingActor>> WallClasses;

	/* Class for wall preview actor */
	UPROPERTY(EditDefaultsOnly, Category = "Ramp")
	TArray<TSubclassOf<ABuildingActor>> RampPreviewClasses;

	/* Class for wall preview actor */
	UPROPERTY(EditDefaultsOnly, Category = "Ramp")
	TArray<TSubclassOf<ABuildingActor>> RampClasses;

	/* Class for wall preview actor */
	UPROPERTY(EditDefaultsOnly, Category = "Floor")
	TArray<TSubclassOf<ABuildingActor>> FloorPreviewClasses;

	/* Class for wall preview actor */
	UPROPERTY(EditDefaultsOnly, Category = "Floor")
	TArray<TSubclassOf<ABuildingActor>> FloorClasses;

	/* Array of weapon classes */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	TArray<TSubclassOf<AWeaponActor>> WeaponClasses;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TSubclassOf<UThirdPersonAnimInstance> AnimInstanceClass;

	/** The hero this character is: the skeletal mesh, with the materials it ships with. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	FSoftObjectPath HeroMesh;

	/** The hero's animation Blueprint class. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	FSoftClassPath HeroAnimClass;

	/** The bone of the hero's skeleton a held weapon is fixed to. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	FName HeroWeaponBone;

	/** How a held weapon is turned against that bone: its muzzle end (+X) and its top (+Z), in the bone's space. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	FRotator HeroGripRotation;

	/** Where the hero's palm is from that bone, in the bone's space; a weapon's grip point sits here. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	FVector HeroPalmOffset;

	/** The bones of the weapon that is part of the hero's own mesh; hidden while the hero holds a game weapon. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	TArray<FName> HeroBuiltInWeaponBones;

	/** The bone a held healing item is fixed to, and where the palm is from it, in the bone's space. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	FName HeroItemBone;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	FVector HeroItemPalmOffset;

	/** The hero's own firing animation, played for every shot. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	FSoftObjectPath HeroFireMontage;

	/** Fix a weapon to the hero's weapon bone, at the weapon's own grip. */
	void AttachWeaponToHand(AWeaponActor* Weapon);

	/** What this player last told the server about moving and running: -1, 0 or 1 once told. */
	int32 LastForwardSent = MAX_int32;
	int32 LastRightSent = MAX_int32;
	int32 LastRunSent = MAX_int32;

	/** Whether the hero's built-in weapon is hidden right now. */
	bool bBuiltInWeaponHidden = false;

	UPROPERTY(EditDefaultsOnly, Category = "State")
	TSubclassOf<ASennitePlayerState> PlayerStateClass;

	/* Class for healing actor */
	UPROPERTY(EditDefaultsOnly, Category = "Healing")
	TArray<TSubclassOf<AHealingActor>> HealingItemClasses;

	UPROPERTY(EditDefaultsOnly, Category = "Skin")
	TArray<UMaterial*> SkinMaterials;

	UPROPERTY(EditDefaultsOnly, Category = "Skin")
	TArray<UMaterialInstance*> SkinMaterialInstances;

	UPROPERTY(EditDefaultsOnly, Category = "Shooting")
	UAnimMontage* RifleHipShootingAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Shooting")
	UAnimMontage* RifleIronsightsShootingAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Shooting")
	UAnimMontage* ShotgunHipShootingAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Shooting")
	UAnimMontage* ShotgunIronsightsShootingAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Pickaxe")
	UAnimMontage* PickaxeSwingingAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Healing")
	UAnimMontage* HealingAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Reload")
	UAnimMontage* RifleHipReloadAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Reload")
	UAnimMontage* ShotgunHipReloadAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Reload")
	UAnimMontage* RifleIronsightsReloadAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Reload")
	UAnimMontage* ShotgunIronsightsReloadAnimation;

	UPROPERTY(EditDefaultsOnly, Replicated, Category = "Health")
	float Health;

	UPROPERTY(EditDefaultsOnly, Replicated, Category = "Health")
	float Shield;

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealth();

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetShield();

	/** On the server: this character is hit for Damage by Shooter (none for the storm). Shield goes first, then health; with no health left the character dies. */
	void TakeHit(float Damage, ASenniteCharacter* Shooter);

	/** Falling below the world's floor: on the server the character dies, as from any other cause. */
	virtual void FellOutOfWorld(const class UDamageType& DamageType) override;

	/** On the server: this character dies. What it holds goes, its player becomes a spectator, and Killer, if there is one, is credited. */
	void Die(ASenniteCharacter* Killer);

	/** The building material in use: 0, 1 or 2, weakest first. */
	int GetCurrentBuildingMaterial() const { return CurrentBuildingMaterial; }

	UFUNCTION(BlueprintPure, Category = "Material")
	int GetWoodMaterialCount();

	UFUNCTION(BlueprintPure, Category = "Material")
	int GetStoneMaterialCount();

	UFUNCTION(BlueprintPure, Category = "Material")
	int GetSteelMaterialCount();

	UFUNCTION(BlueprintPure, Category = "Items")
	int GetAssaultRifleAmmoCount();

	UFUNCTION(BlueprintPure, Category = "Items")
	int GetShotgunAmmoCount();

	UFUNCTION(BlueprintPure, Category = "Items")
	int GetHealingItemCount(int HealingItemType);

	/* The current weapon being held */
	UPROPERTY(Replicated)
	AWeaponActor* CurrentWeapon;

	/* The current healing item being held */
	UPROPERTY(Replicated)
	AHealingActor* CurrentHealingItem;

	/* Pointer to storm instance to get current damage */
	UPROPERTY(Replicated)
	AStormActor* CurrentStorm;

	/* Current preview of wall to be built in build mode */
	UPROPERTY(Replicated)
	ABuildingActor* BuildingPreview;

	/* Anim instance properties */
	UPROPERTY(Replicated)
	bool IsRunning;

	UPROPERTY(Replicated)
	bool IsWalking;

	UPROPERTY(Replicated)
	float WalkingX;

	UPROPERTY(Replicated)
	float WalkingY;

	UPROPERTY(Replicated)
	float RunningX;

	UPROPERTY(Replicated)
	float RunningY;

	UPROPERTY(Replicated)
	bool HoldingWeapon;

	UPROPERTY(Replicated)
	bool AimedIn;

	UPROPERTY(Replicated)
	int HoldingWeaponType;

	UPROPERTY(Replicated)
	float AimPitch;

	UPROPERTY(Replicated)
	float AimYaw;

	UPROPERTY(Replicated)
	float InterpSpeed;

	UPROPERTY(ReplicatedUsing=OnRepSetSkin)
	bool SkinInitialized;

	UPROPERTY(Replicated)
	int MaterialOrMaterialInstance;

	UPROPERTY(Replicated)
	int CurrentSkin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Storm")
	bool InStorm;

	UPROPERTY()
	TArray<ABuildingActor*> ClientSpawnedStructures;
	
	UPROPERTY()
	int CurrentStructureId;

protected:


	/** Called for forwards/backward input */
	void MoveForward(float Value);

	/** Called for side to side input */
	void MoveRight(float Value);

	/* Start  and stop sprinting */
	UFUNCTION()
	void Sprint(float Value);

	/* Sets iswalking variable in anim instance to true */
	UFUNCTION()
	void StartWalking();

	/* Sets iswalking variable in anim instance to false */
	UFUNCTION()
	void StopWalking();

	UFUNCTION()
	TArray<float> CalculateWalkingXY();

	/* Show preview of where the wall will be built */
	UFUNCTION()
	void PreviewWall();

	/* Show preview of where the ramp will be built */
	UFUNCTION()
	void PreviewRamp();

	/* Show preview of where the floor will be built */
	UFUNCTION()
	void PreviewFloor();

	/* When the wall is shown, you will have the option to attempt to build it*/
	UFUNCTION()
	void BuildStructure(float Value);

	UFUNCTION()
	void SwitchBuildingMaterial();

	/* Set the animation variable as well as shoot a very small projectile from the gun or pickaxe*/
	UFUNCTION()
	void ShootGun();
	/** The fire button let go: the rifle stops. */
	void StopShooting();
	/** The fire button still held with the rifle in hand: another shot. */
	void FireWhileHeld();
	FTimerHandle HeldFireTimer;

	/* Set the animation variable as well as shoot a very small projectile from the gun*/
	UFUNCTION()
	void UseHealingItem();

	UFUNCTION()
	void AimGunIn();

	UFUNCTION()
	void AimGunOut();

	UFUNCTION()
	void Reload();

	UFUNCTION()
	void PickaxeTimeOut();

	UFUNCTION()
	void RifleTimeOut();

	UFUNCTION()
	void ShotgunTimeOut();

	UFUNCTION()
	void RifleReloadTimeOut();

	UFUNCTION()
	void ShotgunReloadTimeOut();

	UFUNCTION()
	void HoldPickaxe();

	UFUNCTION()
	void HoldAssaultRifle();

	UFUNCTION()
	void HoldShotgun();

	UFUNCTION()
	void HoldBandage();

	UFUNCTION()
	void HoldPotion();

	UFUNCTION()
	void OpenSettingsMenu();

	/** Open the weapon crate the hero is beside. */
	UFUNCTION()
	void OpenCrate();

	UFUNCTION(Server, Reliable)
	void ServerOpenCrate();


	/*UFUNCTION(Server, WithValidation)
	void ServerSetAnimInstance(UThirdPersonAnimInstance* AnimInstance);*/

	/* Index of the class in array to spawn the weapon */
	UPROPERTY(Replicated)
	int CurrentWeaponType; // 0 for pickaxe, 1 for assault rifle, 2 for shotgun, -1 for non weapon items

	UPROPERTY(Replicated)
	int CurrentHealingItemType; // 0 for bandage, 1 for potion, -1 for non healing type items

	/* Index of the class in array to spawn structure */
	UPROPERTY(Replicated)
	int CurrentBuildingMaterial; // 0 for wood, 1 for stone, 2 for steel
	/** 
	 * Called via input to turn at a given rate. 
	 * @param Rate	This is a normalized rate, i.e. 1.0 means 100% of desired turn rate
	 */
	void TurnAtRate(float Rate);

	/**
	 * Called via input to turn look up/down at a given rate. 
	 * @param Rate	This is a normalized rate, i.e. 1.0 means 100% of desired turn rate
	 */
	void LookUpAtRate(float Rate);

	/** Handler for when a touch input begins. */
	void TouchStarted(ETouchIndex::Type FingerIndex, FVector Location);

	/** Handler for when a touch input stops. */
	void TouchStopped(ETouchIndex::Type FingerIndex, FVector Location);

protected:
	// APawn interface
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// End of APawn interface

public:
	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
	// Tick function is called every frame
	virtual void Tick(float DeltaTime) override;

	/** How long since the hero was last offered what it stands on. On the server. */
	float SincePickupOffer = 0.0f;
	// Override playanimmontage to use pawn mesh
	virtual float PlayAnimMontage(class UAnimMontage* AnimMontage, float InPlayRate = 1.f, FName StartSectionName = NAME_None) override;

	/** Gives the mesh component its hero before the components start up. */
	virtual void PreInitializeComponents() override;

	virtual void PostInitializeComponents() override;

	virtual bool ReplicateSubobjects(class UActorChannel *Channel, class FOutBunch *Bunch, FReplicationFlags *RepFlags) override;

	/* called when character touches something with its body */
	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	virtual bool IsSupportedForNetworking() const override
	{
		return true;
	}

public:
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetIsWalkingTrue();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetIsWalkingFalse();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetIsRunningTrue();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetIsRunningFalse();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetWalkingSpeed();

	UFUNCTION(Client, Reliable)
	void ClientSetWalkingSpeed();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetRunningSpeed();

	UFUNCTION(Client, Reliable)
	void ClientSetRunningSpeed();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetAimedInSpeed();

	UFUNCTION(Client, Reliable)
	void ClientSetAimedInSpeed();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetMovingLeft();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetMovingRight();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetMovingBackwards();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetMovingForwards();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerResetMovingRight();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerResetMovingForward();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetBuildModeWall();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetBuildModeRamp();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetBuildModeFloor();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerFireWeapon();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerUseHealingItem(int HealingItemType);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerReloadWeapons();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSwitchToPickaxe();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSwitchToRifle();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSwitchToShotgun();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSwitchToHealingItem(int HealingItemType);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerChangeBuildingMaterial();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerAimDownSights();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerAimHipFire();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerShotgunTimeOut();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerPickaxeTimeOut();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRifleTimeOut();

	/** Run by the server's own timer when a healing item finishes; not callable by a client. */
	UFUNCTION()
	void HealingItemTimeOut(int HealingItemType);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerShotgunReloadTimeOut();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRifleReloadTimeOut();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerApplyStormDamage();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSpawnProjectile(FTransform SpawnTransform);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSpawnAndAttachWeapon(int WeaponType, FTransform SpawnTransform);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSpawnAndAttachHealingItem(int HealingItemType, FTransform SpawnTransform);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerBuildStructure(TSubclassOf<ABuildingActor> StructureClass, FVector SpawnLocationFRotator, FRotator SpawnRotation, int StructureId);

	/** The material a piece of this character's own lists is built from and its kind ("Wall", "Ramp", "Floor"), or INDEX_NONE for any other class. */
	int32 BuildingMaterialOf(TSubclassOf<ABuildingActor> StructureClass, FString& OutPieceKind) const;

	/** Where a piece of this kind ("Wall", "Ramp", "Floor") goes for where the player stands and looks: one rule for the preview and the build. */
	void BuildPlacement(const FString& PieceKind, FVector& OutLocation, FRotator& OutRotation) const;

	/** The piece classes of a kind: built pieces, or their previews. Null for a kind there is none of. */
	const TArray<TSubclassOf<ABuildingActor>>* BuildingClassesOf(const FString& PieceKind, bool bPreview) const;

	/** The last build this player asked for, so holding the button does not ask for the same piece every frame. */
	FVector LastBuildLocation = FVector(TNumericLimits<double>::Max());
	FString LastBuildKind;
	double LastBuildTime = 0.0;

	/** Whether a transform is a place the build grid has. */
	bool IsOnBuildGrid(const FVector& Location, const FRotator& Rotation) const;

	/** Whether a spawned piece overlaps a player or a piece of its kind already standing there. */
	bool IsBuildingPieceBlocked(const ABuildingActor* Piece) const;

	/** The side of one build grid cell. */
	UPROPERTY(EditDefaultsOnly, Category = "Building")
	float BuildGridCell = 480.0f;

	/** Set by the server when it accepts a trigger pull; the one projectile of that shot clears it. Server only. */
	bool bShotAuthorized = false;

	/** What one piece costs of its material. */
	UPROPERTY(EditDefaultsOnly, Category = "Building")
	int32 BuildingPieceCost = 10;

	/** How far from this character the server builds a piece. */
	UPROPERTY(EditDefaultsOnly, Category = "Building")
	float MaxBuildReach = 1500.0f;

	/** How far from this character the server lets a shot start. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float MaxMuzzleDistance = 400.0f;

	UFUNCTION(Client, Reliable)
	void ClientCameraAimIn();

	UFUNCTION(Client, Reliable)
	void ClientCameraAimOut();

	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastPlayPickaxeSwingAnimation();

	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastPlayShootRifleAnimation();

	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastPlayShootShotgunAnimation();

	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastPlayShootRifleIronsightsAnimation();

	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastPlayShootShotgunIronsightsAnimation();

	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastPlayUseHealingItemAnimation();

	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastPlayReloadRifleAnimation();

	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastPlayReloadRifleIronsightsAnimation();

	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastPlayReloadShotgunAnimation();

	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastPlayReloadShotgunIronsightsAnimation();

	UFUNCTION(Client, Reliable)
	void ClientDrawHitMarker();

	UFUNCTION(Client, Reliable)
	void ClientDrawBloodEffect();

	UFUNCTION(Client, Reliable)
	void ClientGetBulletTransform();

	UFUNCTION(Client, Reliable)
	void ClientGetWeaponTransform(int WeaponType);

	UFUNCTION(Client, Reliable)
	void ClientGetHealingItemTransform(int HealingItemType);

	UFUNCTION(Client, Reliable)
	void ClientDestroyStructure(int StructureId);

	UFUNCTION()
	void OnRepSetSkin();

private:
	// Object creation can only happen after the character has finished being constructed
	virtual void BeginPlay() override;
};

