// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SenniteCharacter.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSenniteCharacter, Log, All);

class AProjectileActor;
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
	ASenniteCharacter(const FObjectInitializer& ObjectInitializer);

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

	/** Which hero this character is (USenniteHeroes, DefaultGame.ini). The server sets it from the player's choice; every machine dresses the character from it. */
	UPROPERTY(ReplicatedUsing = OnRep_HeroId)
	FName HeroId;

	/** On the server: be this hero. Its body, animation, size and hands; a weapon or item in hand is held anew. */
	void SetHero(FName Id);

	UFUNCTION()
	void OnRep_HeroId();

	/** The hero this character is now: the skeletal mesh, with the materials it ships with. From the hero's entry in the list. */
	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	FSoftObjectPath HeroMesh;

	/** The hero's animation Blueprint class. */
	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	FSoftClassPath HeroAnimClass;

	/** The bone of the hero's skeleton a held weapon is fixed to. */
	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	FName HeroWeaponBone;

	/** How a held weapon is turned against that bone: its muzzle end (+X) and its top (+Z), in the bone's space. */
	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	FRotator HeroGripRotation;

	/** Where the hero's palm is from that bone, in the bone's space; a weapon's grip point sits here. */
	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	FVector HeroPalmOffset;

	/** The bones of the weapon that is part of the hero's own mesh; hidden while the hero holds a game weapon. */
	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	TArray<FName> HeroBuiltInWeaponBones;

	/** The bone a held healing item is fixed to, and where the palm is from it, in the bone's space. */
	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	FName HeroItemBone;

	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	FVector HeroItemPalmOffset;

	/** The hero's own attack and the bone its own weapon's bolt leaves from (FSenniteHero::AttackMontage, CastBone). */
	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	FSoftObjectPath HeroAttackMontage;

	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	FName HeroCastBone;

	/** The hero's own weapon is a blow up close, with no bolt and no aiming; and the hero floats, unheard as steps
	 * (FSenniteHero::bMelee, bFloats). */
	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	bool bHeroMelee = false;

	UPROPERTY(VisibleInstanceOnly, Category = "Hero")
	bool bHeroFloats = false;

	/** A blow of the hero's own weapon: what it takes off what it lands on, how far ahead it reaches, how wide it is,
	 * and how long after the swing starts it lands. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	float MeleeDamage = 40.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	float MeleeReach = 260.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	float MeleeAcross = 45.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Hero")
	float MeleeLandsAfter = 0.25f;

	/** The server: the blow lands on the first thing ahead within reach, as a bolt of the hero's own weapon would. */
	void LandBlow();

	/** Every bullet's round and laser, held loaded while the hero is in play. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> BulletLooks;

	/** The mesh on the body now: the hero's own, or the skin's the player wears. */
	FSoftObjectPath WornMesh;

	/** The player's own machine: the skin the player chose in the store is on the hero wherever it plays, only while the
	 * wallet's proof says it owns it and only on the hero it is a skin of; otherwise the hero as it ships. */
	void WearChosenSkin();

	/** The hero's own attack, loaded. */
	UPROPERTY()
	TObjectPtr<UAnimMontage> OwnAttack;

	/** How long after an attack with the hero's own weapon before the next. */
	UPROPERTY(EditDefaultsOnly, Category = "Hero")
	float OwnAttackSeconds = 0.5f;

	/** What the hero's own weapon sends out. */
	UPROPERTY(EditDefaultsOnly, Category = "Hero")
	TSubclassOf<AProjectileActor> OwnBoltClass;

	/** Where the hero's own weapon's bolt leaves from, in the world: its cast bone, or the hand that holds a weapon. */
	FVector OwnWeaponTip() const;

	/** Fix a weapon to the hero's weapon bone, at the weapon's own grip. */
	void AttachWeaponToHand(AWeaponActor* Weapon);

	/** With a gun in hand, how far the player's camera moves from straight behind the hero: to the right and up, to look over its shoulder. */
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FVector ShoulderView = FVector(0.0f, 60.0f, 35.0f);

	/**
	 * Aiming, the player looks through the hero's own eyes: the camera comes in from behind the hero to its eye height and
	 * this far ahead of its middle, and the hero and what it holds are not drawn for its own player, so nothing of the hero
	 * is under the crosshair. Everyone else sees the hero as before. The owner, 2026-11-19, of aiming from behind the hero:
	 * "When it zooms in, it's still behind him. It should go into first person or something, I guess. Right now, the
	 * crosshairs are on his butt."
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float AimEyesAhead = 18.0f;

	/** How wide the player's view is, in degrees: following the hero, and aiming. */
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float FollowFieldOfView = 90.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float AimFieldOfView = 45.0f;

	/** On the player's own machine: stop drawing the hero and the gun in its hand for its own player, or draw them again. */
	void SeeThroughOwnEyes(bool bEyes);

	/** The player's own machine: the camera is close enough to the hero's eyes that the hero is not drawn for its player. */
	bool IsSeenThroughOwnEyes() const { return bThroughOwnEyes; }

	/** How much larger than its model a gun is in this hero's hands: the hero's arm against GunArm, between GunSizeLeast and GunSizeMost. */
	float GunSize() const;

	/** The arm, shoulder to wrist in cm, the guns' models are the right size for; and how small and how large a gun is made for a hero. */
	UPROPERTY(EditDefaultsOnly, Category = "Hero")
	float GunArm = 55.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Hero")
	float GunSizeLeast = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Hero")
	float GunSizeMost = 2.2f;

	/** What this player last told the server about moving and running: -1, 0 or 1 once told. */
	int32 LastForwardSent = MAX_int32;
	int32 LastRightSent = MAX_int32;
	int32 LastRunSent = MAX_int32;

	/** Whether the hero's built-in weapon is hidden right now. */
	bool bBuiltInWeaponHidden = false;
	/** The player's own machine: the hero is not drawn for its own player, who is looking through its eyes. */
	bool bThroughOwnEyes = false;
	/** How the hero's bones were kept up to date while it was drawn, to go back to. */
	EVisibilityBasedAnimTickOption SeenAnimTick = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	/** How far behind the hero the camera follows when not aiming: the boom's length as the hero was made. */
	float FollowDistance = 300.0f;

	UPROPERTY(EditDefaultsOnly, Category = "State")
	TSubclassOf<ASennitePlayerState> PlayerStateClass;

	/* Class for healing actor */
	UPROPERTY(EditDefaultsOnly, Category = "Healing")
	TArray<TSubclassOf<AHealingActor>> HealingItemClasses;

	UPROPERTY(EditDefaultsOnly, Category = "Skin")
	TArray<UMaterial*> SkinMaterials;

	UPROPERTY(EditDefaultsOnly, Category = "Skin")
	TArray<UMaterialInstance*> SkinMaterialInstances;

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

	/**
	 * Editing a built piece (G). G on a piece of the player's own side, aimed at and within reach,
	 * starts editing it; while editing, the fire button marks the tile under the aim to be cut out or
	 * put back (dragging across tiles marks them alike), the aim button puts every tile back and
	 * finishes, and G again finishes. The server makes the change for everyone, or refuses it.
	 */
	UFUNCTION()
	void EditPiece();

	/** The piece this player is editing on this machine, or null. */
	ABuildingActor* GetEditingPiece() const { return EditingPiece.Get(); }

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The tiles marked to be cut in the piece being edited. */
	int32 GetEditCut() const { return EditCut; }

	/** The built piece of this player's side the aim falls on within reach, and the tile it falls on; null if none. */
	ABuildingActor* EditablePieceUnderAim(int32& OutTile) const;

	/** The built piece the camera looks at within sight, for the HUD; null if none. */
	ABuildingActor* PieceInSight() const;

	/** On the server: change a piece's cut tiles, if this player may and nothing is in the way. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerEditPiece(ABuildingActor* Piece, int32 NewCut);

	/** On the server: whether this player's edit of a piece would be made. */
	bool MayEditPiece(const ABuildingActor* Piece, int32 NewCut) const;

	/** While editing a piece: take the whole piece away (the reload button, while editing). */
	void RemoveEditedPiece();

	/**
	 * On the server: take a piece away whole, if this player may: a piece of the player's own side, within
	 * reach, as for an edit. What it cost goes back to whoever built it, and what it alone held up falls as
	 * after any piece.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRemovePiece(ABuildingActor* Piece);

	/** On the server: whether this player may take a piece away. */
	bool MayRemovePiece(const ABuildingActor* Piece) const;

	/** On every machine: a piece was taken away. It goes at once, heard as taken away, and does not fall apart. */
	UFUNCTION(NetMulticast, Reliable)
	void NetMulticastPieceTakenAway(ABuildingActor* Piece);

	/** How long after a piece is taken away the server ends it: time for every machine to be told first. */
	static constexpr float PieceTakenAwaySeconds = 0.2f;

	/** The server refused an edit: show the piece as it is. */
	UFUNCTION(Client, Reliable)
	void ClientEditRefused(ABuildingActor* Piece);

	/**
	 * What a crate held goes to the hero, on the server: each of its guns the hero does not have yet
	 * (Guns has a bit for each weapon slot), with a full magazine; with bRounds, each of those guns'
	 * rounds (AWeaponActor::CrateRounds); the bandages and the potions. The first new gun comes to
	 * hand. False, and nothing is given, while the hero is busy (building, firing, reloading, healing).
	 */
	bool TakeFromCrate(int32 Guns, bool bRounds, int32 Bandages, int32 Potions);

	/** A bit for each gun the hero has (bit n for weapon slot n). */
	int32 GunsHeld() const;

	/** A bit for each gun bought in the store whose ShopId is among these: what a wallet's owned list makes its player's. */
	static int32 BoughtGunsAmong(const TArray<FString>& Owned);

	/** A shot left the gun in hand: it kicks. */
	void KickGun();

	/** A shot of this player's took damage off a built piece. */
	UFUNCTION(Client, Reliable)
	void ClientDrawPieceHit(float Damage);

	/** How far from the camera a piece is named on the HUD. */
	UPROPERTY(EditDefaultsOnly, Category = "Building")
	float PieceSightReach = 3000.0f;

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

	/** Where the next piece would go, drawn on this player's own machine only while they are in build mode. */
	UPROPERTY(Transient)
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
	void HoldEnergyRifle();

	UFUNCTION()
	void HoldSniper();

	/** Whether a weapon slot holds a gun: every slot but the pickaxe's that has a weapon class. */
	bool IsGun(int32 Type) const;

	UFUNCTION()
	void HoldBandage();

	UFUNCTION()
	void HoldPotion();

	/** Open the weapon crate the hero is beside. */
	UFUNCTION()
	void OpenCrate();

	UFUNCTION(Server, Reliable)
	void ServerOpenCrate();

	/** What a crate this player opened gave, for the HUD to say. */
	UFUNCTION(Client, Reliable)
	void ClientCrateGave(int32 NewGuns, int32 InStore, bool bRounds, int32 Bandages, int32 Potions);


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

	/** The touch controls walk the hero as the walking keys do, with StartWalking and StopWalking. */
	friend class USenniteTouchControls;

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

	// Override playanimmontage to use pawn mesh
	virtual float PlayAnimMontage(class UAnimMontage* AnimMontage, float InPlayRate = 1.f, FName StartSectionName = NAME_None) override;

	/** Gives the mesh component its hero before the components start up. */
	virtual void PreInitializeComponents() override;

	virtual void PostInitializeComponents() override;

	/** On the server: a player takes this character over, and it becomes the hero they chose. */
	virtual void PossessedBy(AController* NewController) override;

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

	/** Take a gun the hero has into its hands: 1 the rifle, 2 the shotgun, 3 the energy rifle, 4 the sniper. */
	UFUNCTION(Server, Reliable)
	void ServerHoldGun(int32 Type);

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

	/** Editing on this machine: the piece, the tiles marked to cut, and a drag across tiles that marks them alike. */
	TWeakObjectPtr<ABuildingActor> EditingPiece;
	int32 EditCut = 0;
	bool bEditDragging = false;
	bool bEditDragCuts = false;
	int32 EditDragLast = INDEX_NONE;

	/** Mark a tile of the piece being edited, keeping at least one tile. */
	void MarkEditTile(int32 Tile, bool bCut);

	/** Send the edit to the server and stop editing. */
	void FinishEdit();

	/** Stop editing without a change. */
	void CancelEdit();

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

	/** Looking this many degrees above the horizon builds one level up; standing on a piece and looking this many below it, one level down. */
	UPROPERTY(EditDefaultsOnly, Category = "Building")
	float BuildUpAbove = 40.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Building")
	float BuildDownBelow = 50.0f;

	/**
	 * The shot the server last let out and that has not left yet: its bullet, the gun it was let out
	 * of (none for the hero's own weapon) and when. The one projectile of that shot is this bullet, from
	 * this gun, and takes it away; a shot not taken within ShotKeptSeconds is gone. Server only: a
	 * player's game can ask for the projectile, but not choose what it is.
	 */
	UPROPERTY()
	TSubclassOf<AProjectileActor> ShotBullet;

	TWeakObjectPtr<AWeaponActor> ShotGun;
	double ShotAt = -1000.0;

	/** How long a shot the server let out waits for its projectile. */
	static constexpr double ShotKeptSeconds = 0.5;

	/**
	 * When the server will next let a shot out, and when the reload under way is over, by the server's
	 * own clock. The marks on the player state (JustShotRifle and the rest) say the same for the HUD
	 * and are cleared by calls a player's game can make; these are not.
	 */
	double NextShotAt = 0.0;
	double ReloadOverAt = 0.0;

	/** How much sooner than NextShotAt a shot asked for is still let out: a held trigger asks on its own clock. */
	static constexpr double ShotEarly = 0.02;

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

	/** The hero attacked with its own weapon: on every machine it plays the hero's own attack. */
	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastOwnAttack();

	/** The gun in hand fired: on every machine it kicks and is heard. */
	UFUNCTION(NetMulticast, Unreliable)
	void NetMulticastGunFired();

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

	/** Put a mesh on the hero's body, its own or a skin of it, drawn with that mesh's own materials and none left from the mesh before. */
	void WearMesh(USkeletalMesh* Mesh);

private:
	/** Dress the character as HeroId's hero: the mesh, its animation Blueprint, its capsule and its hands. */
	void ApplyHero();

	/** The hero the character was last dressed as. */
	FName DressedHero;

	// Object creation can only happen after the character has finished being constructed
	virtual void BeginPlay() override;
};

