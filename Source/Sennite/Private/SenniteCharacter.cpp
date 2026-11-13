// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SenniteCharacter.h"
#include "SennitePlayerController.h"
#include "SennitePlayerState.h"
#include "ThirdPersonAnimInstance.h"
#include "ProjectileActor.h"
#include "StormActor.h"
#include "SenniteHUD.h"
#include "WeaponActor.h"
#include "SenniteWeapons.h"
#include "SenniteBuildings.h"
#include "SenniteHealing.h"
#include "Animation/AnimMontage.h"
#include "EngineUtils.h"
#include "BuildingActor.h"
#include "HealingActor.h"
#include "AmmunitionActor.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "Engine/ActorChannel.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"

DEFINE_LOG_CATEGORY(LogSenniteCharacter);
//////////////////////////////////////////////////////////////////////////
// ASenniteCharacter

ASenniteCharacter::ASenniteCharacter()
{
	bReplicates = true;
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Set up capsule component for detecting overlap
	TriggerCapsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Trigger Capsule"));
	TriggerCapsule->InitCapsuleSize(42.f, 96.0f);;
	TriggerCapsule->SetCollisionProfileName(TEXT("Trigger"));
	TriggerCapsule->SetupAttachment(RootComponent);
	TriggerCapsule->OnComponentBeginOverlap.AddDynamic(this, &ASenniteCharacter::OnOverlapBegin);
	TriggerCapsule->OnComponentEndOverlap.AddDynamic(this, &ASenniteCharacter::OnOverlapEnd);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	CurrentWeaponType = 0;
	CurrentHealingItemType = -1;
	CurrentBuildingMaterial = 0;
	BuildingPreview = nullptr;

	// Animinstance properties
	IsWalking = false;
	IsRunning = false;
	HoldingWeapon = false;
	AimedIn = false;
	HoldingWeaponType = 0;
	AimPitch = 0.0;
	AimYaw = 0.0;
	InterpSpeed = 15.0; //change this to set aim sensitivity
	WalkingX = 0;
	WalkingY = 0;
	RunningX = 0;
	RunningY = 0;
	InStorm = true;
	CurrentStructureId = 0;	

	// Playerstate properties
	/*InBuildMode = false;
	BuildMode = FString("None");
	HoldingWeapon = false;
	HoldingHealingItem = false;
	AimedIn = false;
	EquippedWeapons.Add(0); //pickaxe
	EquippedWeaponsAmmunition.Add(0); // pickaxe
	EquippedWeaponsAmmunition.Add(0); // assault rifle
	EquippedWeaponsAmmunition.Add(0); // shotgun
	EquippedWeaponsClips.Add(0); // pickaxe
	EquippedWeaponsClips.Add(0); // assault rifle
	EquippedWeaponsClips.Add(0); // shotgun
	MaterialCounts.Add(0); // wood
	MaterialCounts.Add(0); // stone
	MaterialCounts.Add(0); // steel
	CurrentWeapon = 0;
	JustShotRifle = false;
	JustShotShotgun = false;
	JustSwungPickaxe = false;
	JustUsedHealingItem = false;
	JustReloadedRifle = false;
	JustReloadedShotgun = false;
	KillCount = 0;*/

	// The hero's mesh and animation Blueprint are named in DefaultGame.ini (HeroMesh, HeroAnimClass) and put on
	// the Mesh component in PreInitializeComponents. The heroes are authored facing +Y with their feet at the
	// origin, so the mesh sits at the bottom of the capsule, turned to face the capsule's forward.
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -97.0f), FRotator(0.0f, -90.0f, 0.0f));

	Health = 100.0f;
	Shield = 0.0f;

	// slot 0 is the pickaxe, which has no model yet; 1 the assault rifle, 2 the shotgun
	WeaponClasses.SetNum(3);
	WeaponClasses[1] = ASenniteRifle::StaticClass();
	WeaponClasses[2] = ASenniteShotgun::StaticClass();

	// healing items: 0 the bandage, 1 the potion
	HealingItemClasses = { ASenniteBandage::StaticClass(), ASennitePotion::StaticClass() };

	// building pieces by material: 0, 1, 2, weakest first
	WallClasses = { ASenniteWoodWall::StaticClass(), ASenniteStoneWall::StaticClass(), ASenniteSteelWall::StaticClass() };
	RampClasses = { ASenniteWoodRamp::StaticClass(), ASenniteStoneRamp::StaticClass(), ASenniteSteelRamp::StaticClass() };
	FloorClasses = { ASenniteWoodFloor::StaticClass(), ASenniteStoneFloor::StaticClass(), ASenniteSteelFloor::StaticClass() };
	WallPreviewClasses = { ASenniteWoodWallPreview::StaticClass(), ASenniteStoneWallPreview::StaticClass(), ASenniteSteelWallPreview::StaticClass() };
	RampPreviewClasses = { ASenniteWoodRampPreview::StaticClass(), ASenniteStoneRampPreview::StaticClass(), ASenniteSteelRampPreview::StaticClass() };
	FloorPreviewClasses = { ASenniteWoodFloorPreview::StaticClass(), ASenniteStoneFloorPreview::StaticClass(), ASenniteSteelFloorPreview::StaticClass() };

	// a ramp rises one cell over one cell, 45 degrees, and must be walkable
	GetCharacterMovement()->SetWalkableFloorAngle(50.0f);
}

const TArray<TSubclassOf<ABuildingActor>>* ASenniteCharacter::BuildingClassesOf(const FString& PieceKind, bool bPreview) const
{
	if (PieceKind == TEXT("Wall")) {
		return bPreview ? &WallPreviewClasses : &WallClasses;
	}
	if (PieceKind == TEXT("Ramp")) {
		return bPreview ? &RampPreviewClasses : &RampClasses;
	}
	if (PieceKind == TEXT("Floor")) {
		return bPreview ? &FloorPreviewClasses : &FloorClasses;
	}
	return nullptr;
}

void ASenniteCharacter::BuildPlacement(const FString& PieceKind, FVector& OutLocation, FRotator& OutRotation) const
{
	// Build cells are squares of BuildGridCell centred on the grid points, in levels BuildGridCell apart.
	// The player builds along the quarter turn the camera faces, on the level they stand on, or one
	// level up when looking well above the horizon.
	const double Cell = BuildGridCell;
	const FRotator View = GetControlRotation();
	const double FacingYaw = FMath::RoundHalfFromZero(FRotator::NormalizeAxis(View.Yaw) / 90.0) * 90.0;
	const FVector Ahead = FRotator(0.0, FacingYaw, 0.0).Vector();
	const FVector Feet = GetActorLocation() - FVector(0.0, 0.0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const auto Snap = [Cell](double Value) { return FMath::RoundHalfFromZero(Value / Cell) * Cell; };
	const double Level = FMath::FloorToDouble((Feet.Z + 1.0) / Cell) * Cell + (FRotator::NormalizeAxis(View.Pitch) > 40.0 ? Cell : 0.0);
	const FVector OwnCell(Snap(Feet.X), Snap(Feet.Y), Level);
	if (PieceKind == TEXT("Wall")) {
		// on the far edge of the cell the player stands in, across the way they face
		OutLocation = OwnCell + Ahead * (Cell * 0.5);
		OutRotation = FRotator(0.0, FacingYaw + 90.0, 0.0);
	}
	else {
		// a floor or a ramp fills the next cell ahead; a ramp rises away from the player
		OutLocation = OwnCell + Ahead * Cell;
		OutRotation = FRotator(0.0, FacingYaw, 0.0);
	}
	OutRotation.Normalize();
}

void ASenniteCharacter::AttachWeaponToHand(AWeaponActor* Weapon)
{
	if (Weapon == nullptr || Weapon->WeaponMesh == nullptr) {
		return;
	}
	Weapon->WeaponMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, HeroWeaponBone);
	// the hero's hand decides how the weapon is turned; the weapon says which point of it the palm closes on
	Weapon->WeaponMesh->SetRelativeLocationAndRotation(HeroPalmOffset - HeroGripRotation.RotateVector(Weapon->GripPoint), HeroGripRotation);
}

void ASenniteCharacter::PreInitializeComponents()
{
	Super::PreInitializeComponents();

	USkeletalMeshComponent* HeroComponent = GetMesh();
	if (HeroComponent == nullptr) {
		return;
	}
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(HeroMesh.TryLoad())) {
		HeroComponent->SetSkeletalMeshAsset(Mesh);
	}
	else {
		UE_LOG(LogSenniteCharacter, Error, TEXT("Hero mesh '%s' did not load; the character has no body"), *HeroMesh.ToString());
	}
	if (UClass* AnimClass = HeroAnimClass.TryLoadClass<UAnimInstance>()) {
		HeroComponent->SetAnimInstanceClass(AnimClass);
	}
	else {
		UE_LOG(LogSenniteCharacter, Error, TEXT("Hero animation class '%s' did not load; the character will not animate"), *HeroAnimClass.ToString());
	}
	// every shot plays the hero's own firing animation, where no other was given
	if (UAnimMontage* Fire = Cast<UAnimMontage>(HeroFireMontage.TryLoad())) {
		for (UAnimMontage** Slot : { &RifleHipShootingAnimation, &RifleIronsightsShootingAnimation, &ShotgunHipShootingAnimation, &ShotgunIronsightsShootingAnimation }) {
			if (*Slot == nullptr) {
				*Slot = Fire;
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void ASenniteCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAxis("MoveForward", this, &ASenniteCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ASenniteCharacter::MoveRight);
	PlayerInputComponent->BindAxis("Sprint", this, &ASenniteCharacter::Sprint);

	PlayerInputComponent->BindAction("Walk", IE_Pressed, this, &ASenniteCharacter::StartWalking);
	PlayerInputComponent->BindAction("Walk", IE_Released, this, &ASenniteCharacter::StopWalking);
	PlayerInputComponent->BindAction("PreviewWall", IE_Pressed, this, &ASenniteCharacter::PreviewWall);
	PlayerInputComponent->BindAction("PreviewRamp", IE_Pressed, this, &ASenniteCharacter::PreviewRamp);
	PlayerInputComponent->BindAction("PreviewFloor", IE_Pressed, this, &ASenniteCharacter::PreviewFloor);
	PlayerInputComponent->BindAction("SwitchBuildingMaterial", IE_Pressed, this, &ASenniteCharacter::SwitchBuildingMaterial);
	PlayerInputComponent->BindAction("ShootGun", IE_Pressed, this, &ASenniteCharacter::ShootGun);
	PlayerInputComponent->BindAction("UseHealingItem", IE_Pressed, this, &ASenniteCharacter::UseHealingItem);
	PlayerInputComponent->BindAction("Reload", IE_Pressed, this, &ASenniteCharacter::Reload);
	PlayerInputComponent->BindAction("HoldPickaxe", IE_Pressed, this, &ASenniteCharacter::HoldPickaxe);
	PlayerInputComponent->BindAction("HoldAssaultRifle", IE_Pressed, this, &ASenniteCharacter::HoldAssaultRifle);
	PlayerInputComponent->BindAction("HoldShotgun", IE_Pressed, this, &ASenniteCharacter::HoldShotgun);
	PlayerInputComponent->BindAction("HoldBandage", IE_Pressed, this, &ASenniteCharacter::HoldBandage);
	PlayerInputComponent->BindAction("HoldPotion", IE_Pressed, this, &ASenniteCharacter::HoldPotion);
	PlayerInputComponent->BindAction("Ironsights", IE_Pressed, this, &ASenniteCharacter::AimGunIn);
	PlayerInputComponent->BindAction("Ironsights", IE_Released, this, &ASenniteCharacter::AimGunOut);
	PlayerInputComponent->BindAction("OpenSettings", IE_Pressed, this, &ASenniteCharacter::OpenSettingsMenu);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &ASenniteCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &ASenniteCharacter::LookUpAtRate);
	PlayerInputComponent->BindAxis("BuildStructure", this, &ASenniteCharacter::BuildStructure);

	// handle touch devices
	PlayerInputComponent->BindTouch(IE_Pressed, this, &ASenniteCharacter::TouchStarted);
	PlayerInputComponent->BindTouch(IE_Released, this, &ASenniteCharacter::TouchStopped);

	// VR headset functionality

}

void ASenniteCharacter::BeginPlay() {
	Super::BeginPlay();
	//GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("Client ")  + FString::FromInt(ENetMode::NM_Client) + FString(" server ") + FString::FromInt(ENetMode::NM_DedicatedServer));
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::FromInt(GetNetMode()));
	/*if (GetNetMode() != ENetMode::NM_Client || GetNetMode() != ENetMode::NM_Standalone) {
		return;
	}*/
	if (HasAuthority()) {
		if (WeaponClasses.IsValidIndex(CurrentWeaponType) && WeaponClasses[CurrentWeaponType]) {
			// for some reason I can't call clientgetweapontransform here
			FName WeaponSocketName = TEXT("hand_right_socket_pickaxe");
			FAttachmentTransformRules AttachmentRules(EAttachmentRule::SnapToTarget, EAttachmentRule::KeepRelative, EAttachmentRule::KeepWorld, true);
			FTransform SpawnTransform(GetActorRotation(), GetActorLocation());
			CurrentWeapon = Cast<AWeaponActor>(UGameplayStatics::BeginDeferredActorSpawnFromClass(this, WeaponClasses[CurrentWeaponType], SpawnTransform));
			if (CurrentWeapon != nullptr)
			{
				//spawnactor has no way of passing parameters so need to use begindeferredactorspawn and finishspawningactor
				CurrentWeapon->Holder = this;
				UGameplayStatics::FinishSpawningActor(CurrentWeapon, SpawnTransform);

				AttachWeaponToHand(CurrentWeapon);
				HoldingWeapon = true;
				AimedIn = false;
				HoldingWeaponType = 1;
			}

		}
		// find the storm and keep a reference to it for damage purposes
		TArray<AActor*> StormActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AStormActor::StaticClass(), StormActors);
		if (StormActors.Num() > 0) {
			if (StormActors[0] != nullptr) {
				CurrentStorm = Cast<AStormActor>(StormActors[0]);
			}
		}
		// set skin
		if (GetMesh()) {
			MaterialOrMaterialInstance = FMath::RandRange(0, 1);
			CurrentSkin = FMath::RandRange(0, 1);

			USkeletalMeshComponent* CharacterMesh = GetMesh();
			if (MaterialOrMaterialInstance == 0 && SkinMaterials.IsValidIndex(CurrentSkin)) {
				// assign UMaterial
				CharacterMesh->SetMaterial(0, SkinMaterials[CurrentSkin]);
			}
			else if (MaterialOrMaterialInstance == 1 && SkinMaterialInstances.IsValidIndex(CurrentSkin)) {
				// assign UMaterialInstance
				CharacterMesh->SetMaterial(0, SkinMaterialInstances[CurrentSkin]);
			}
			SkinInitialized = true;
		}
		
		FTimerHandle StormDamageTimerHandle;
		GetWorldTimerManager().SetTimer(StormDamageTimerHandle, this, &ASenniteCharacter::ServerApplyStormDamage, 1.0f, true);

	}
}

void ASenniteCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (HasAuthority())
	{
		// Objects only replicate from server to client. If we didn't guard this
		// the client would create the object just fine but it would get replaced
		// by the server version (more accurately the property would be replaced to
		// point to the version from the server. The one the client allocated would
		// eventually be garbage collected.
		//AnimInstance = NewObject<UThirdPersonAnimInstance>(this); // NOTE: Very important, objects Outer must be our Actor!
	}
}

bool ASenniteCharacter::ReplicateSubobjects(class UActorChannel *Channel, class FOutBunch *Bunch, FReplicationFlags *RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);
	/*if (AnimInstance != nullptr)
	{
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("replicated ") + FString::FromInt(GetNetMode()));
		WroteSomething |= Channel->ReplicateSubobject(AnimInstance, *Bunch, *RepFlags);
	}
	if (State != nullptr) {
		WroteSomething |= Channel->ReplicateSubobject(State, *Bunch, *RepFlags);
	}
	if (BuildingPreview != nullptr) {
		WroteSomething |= Channel->ReplicateSubobject(BuildingPreview, *Bunch, *RepFlags);
	}*/
	return WroteSomething;
}

void ASenniteCharacter::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASenniteCharacter, BuildingPreview);
	DOREPLIFETIME(ASenniteCharacter, CurrentBuildingMaterial);
	DOREPLIFETIME(ASenniteCharacter, CurrentHealingItem);
	DOREPLIFETIME(ASenniteCharacter, CurrentHealingItemType);
	DOREPLIFETIME(ASenniteCharacter, CurrentWeapon);
	DOREPLIFETIME(ASenniteCharacter, CurrentStorm);
	DOREPLIFETIME(ASenniteCharacter, CurrentWeaponType);
	DOREPLIFETIME(ASenniteCharacter, Health);
	DOREPLIFETIME(ASenniteCharacter, Shield);

	DOREPLIFETIME(ASenniteCharacter, IsWalking);
	DOREPLIFETIME(ASenniteCharacter, IsRunning);
	DOREPLIFETIME(ASenniteCharacter, WalkingX);
	DOREPLIFETIME(ASenniteCharacter, WalkingY);
	DOREPLIFETIME(ASenniteCharacter, RunningX);
	DOREPLIFETIME(ASenniteCharacter, RunningY);
	DOREPLIFETIME(ASenniteCharacter, HoldingWeapon);
	DOREPLIFETIME(ASenniteCharacter, HoldingWeaponType);
	DOREPLIFETIME(ASenniteCharacter, AimedIn);
	DOREPLIFETIME(ASenniteCharacter, AimPitch);
	DOREPLIFETIME(ASenniteCharacter, AimYaw);
	DOREPLIFETIME(ASenniteCharacter, InterpSpeed);
	DOREPLIFETIME(ASenniteCharacter, InStorm);
	DOREPLIFETIME(ASenniteCharacter, SkinInitialized);
	DOREPLIFETIME(ASenniteCharacter, MaterialOrMaterialInstance);
	DOREPLIFETIME(ASenniteCharacter, CurrentSkin);
}

void ASenniteCharacter::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);
	// The weapon that is part of the hero's own mesh gives way to a game weapon in the hand.
	// CurrentWeapon is replicated, so every machine hides and shows it alike.
	// In the storm means outside its safe circle: farther from the circle's centre than its radius.
	// The server decides; InStorm is replicated.
	if (HasAuthority() && CurrentStorm != nullptr) {
		InStorm = FVector::Dist2D(GetActorLocation(), CurrentStorm->GetActorLocation()) > CurrentStorm->GetRadius();
	}
	const bool bHideBuiltIn = CurrentWeapon != nullptr;
	if (bHideBuiltIn != bBuiltInWeaponHidden && GetMesh() != nullptr) {
		bBuiltInWeaponHidden = bHideBuiltIn;
		for (const FName& Bone : HeroBuiltInWeaponBones) {
			if (bHideBuiltIn) {
				GetMesh()->HideBoneByName(Bone, PBO_None);
			}
			else {
				GetMesh()->UnHideBoneByName(Bone);
			}
		}
	}
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("Tick mode ") + FString::FromInt(GetNetMode()));
	if (HasAuthority()) {
		// TODO: building preview should only be spawned client side
		if (GetController()) {
			ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
			if (State) {
				// the preview shows where the next piece would go; it follows the player and changes with the kind and material
				const TArray<TSubclassOf<ABuildingActor>>* Previews = State->InBuildMode ? BuildingClassesOf(State->BuildMode, true) : nullptr;
				TSubclassOf<ABuildingActor> PreviewClass = Previews != nullptr && Previews->IsValidIndex(CurrentBuildingMaterial) ? (*Previews)[CurrentBuildingMaterial] : nullptr;
				if (BuildingPreview != nullptr && BuildingPreview->GetClass() != PreviewClass) {
					BuildingPreview->Destroy();
					BuildingPreview = nullptr;
				}
				if (PreviewClass != nullptr) {
					FVector GridLocation;
					FRotator GridRotation;
					BuildPlacement(State->BuildMode, GridLocation, GridRotation);
					if (BuildingPreview == nullptr) {
						BuildingPreview = GetWorld()->SpawnActor<ABuildingActor>(PreviewClass, GridLocation, GridRotation);
					}
					else {
						BuildingPreview->SetActorLocationAndRotation(GridLocation, GridRotation);
					}
				}
			}
		}
		FRotator ControlRotation = GetControlRotation();
		FRotator ActorRotation = GetActorRotation();

		FRotator DeltaRotation = ControlRotation - ActorRotation;
		DeltaRotation.Normalize();

		FRotator AimRotation = FRotator(AimPitch, AimYaw, 0);
		FRotator InterpolatedRotation = FMath::RInterpTo(AimRotation, DeltaRotation, DeltaTime, InterpSpeed);

		float NewPitch = FMath::ClampAngle(InterpolatedRotation.Pitch, -90, 90);
		float NewYaw = FMath::ClampAngle(InterpolatedRotation.Yaw, -90, 90);
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::FromInt(NewPitch).Append(FString::FromInt(NewYaw)));
		AimPitch = NewPitch;
		AimYaw = NewYaw;
	}
	
}

void ASenniteCharacter::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) {
	if (HasAuthority()) {
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("NetMode: ") + FString::FromInt(GetNetMode()) + FString(" Player overlapped with: ") + OtherActor->GetName());
		if (OtherActor != nullptr && OtherActor != this) {
			if (CurrentWeapon != nullptr && OtherActor == (AActor*)CurrentWeapon) {
				// if the character is overlapping with its weapon, dont do anything about it
				return;
			}
			if (CurrentHealingItem != nullptr && OtherActor == (AActor*)CurrentHealingItem) {
				// if the character is overlapping with its healing item, dont do anything about it
				return;
			}
			if (OtherActor->IsA(AWeaponActor::StaticClass())) {
				AWeaponActor* WeaponActor = Cast<AWeaponActor>(OtherActor);
				if (WeaponActor->WeaponType == 0) {
					return; // do nothing if it's a pickaxe
				}
				if (WeaponActor->Holder != nullptr) {
					return; // do nothing if someone is holding the weapon
				}
				if (GetController()) {
					// pick up the item if the two conditions above are false
					ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
					if (State) {
						if (State->InBuildMode || State->JustShotRifle || State->JustShotShotgun || State->JustSwungPickaxe || State->JustUsedHealingItem || State->JustReloadedRifle || State->JustReloadedShotgun) {
							return; // can't pick up items while in build mode or if just shot rifle, shot shotgun, swung pickaxe, used healing item, or reloaded
						}
						// if the player already has a weapon of this type, do not equip it
						if (State->EquippedWeapons.Contains(WeaponActor->WeaponType)) {
							return;
						}
						// Destroy old weapon/healing item
						if (CurrentWeapon && CurrentWeaponType > 0 && CurrentWeaponType < 3) {
							State->EquippedWeaponsClips[CurrentWeaponType] = CurrentWeapon->CurrentBulletCount;
						}
						if (CurrentWeapon) {
							CurrentWeapon->Destroy();
							CurrentWeapon = nullptr;
						}
						if (CurrentHealingItem) {
							CurrentHealingItem->Destroy();
							CurrentHealingItem = nullptr;
						}
						// PICK UP WEAPON
						State->EquippedWeapons.Add(WeaponActor->WeaponType);
						State->EquippedWeaponsClips[WeaponActor->WeaponType] = WeaponActor->MagazineSize; // this has to be done before calling client method below
						//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("MagSize: ") + FString::FromInt(WeaponActor->MagazineSize));
						ClientGetWeaponTransform(WeaponActor->WeaponType);

						WeaponActor->Destroy();
					}

				}

			}
			else if (OtherActor->IsA(AHealingActor::StaticClass())) {
				//pick up the item
				AHealingActor* HealingActor = Cast<AHealingActor>(OtherActor);
				ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
				if (CurrentHealingItem) {
					CurrentHealingItem->Destroy();
					CurrentHealingItem = nullptr;
				}
				if (State) {
					//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, OtherActor->GetName());
					if (State->InBuildMode || State->JustShotRifle || State->JustShotShotgun || State->JustSwungPickaxe || State->JustUsedHealingItem || State->JustReloadedRifle || State->JustReloadedShotgun) {
						return; // can't pick up items while in build mode or if just shot rifle, shot shotgun, swung pickaxe, used healing item, or reloaded
					}
					//CurrentHealingItem = Cast<AHealingActor>(OtherActor);
					if (HealingActor->Holder != nullptr) {
						return; // do nothing if someone is holding the healing item
					}
					//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("didn't end early"));
					// Destroy old weapon
					if (CurrentWeapon && CurrentWeaponType > 0 && CurrentWeaponType < 3) {
						State->EquippedWeaponsClips[CurrentWeaponType] = CurrentWeapon->CurrentBulletCount;
					}
					if (CurrentWeapon) {
						CurrentWeapon->Destroy();
						CurrentWeapon = nullptr;
					}
					// pick up healing item
					State->HealingItemCounts[HealingActor->HealingType] += HealingActor->Count;
					ClientGetHealingItemTransform(HealingActor->HealingType);
					HealingActor->Destroy();
				}
			}
			else if (OtherActor->IsA(AAmmunitionActor::StaticClass())) {
				if (GetController()) {
					ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
					AAmmunitionActor* Ammo = Cast<AAmmunitionActor>(OtherActor);
					if (State) {
						// increment ammo count
						State->EquippedWeaponsAmmunition[Ammo->WeaponType] += Ammo->BulletCount;
					}
					Ammo->Destroy();
				}
			}
			//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, OtherActor->GetName());
		}
	}
}

void ASenniteCharacter::OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex) {
	if (HasAuthority()) {
		/*FString LogMsg = FString("storm overlap end ") + FString::FromInt(GetNetMode());
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, LogMsg);
		UE_LOG(LogSenniteCharacter, Warning, TEXT("%s"), *LogMsg);*/
		if (OtherActor != nullptr) {
			if (OtherActor == this) {
				return;
			}
		}
	}
}

float ASenniteCharacter::PlayAnimMontage(class UAnimMontage* AnimMontage, float InPlayRate, FName StartSectionName)
{
	USkeletalMeshComponent* UseMesh = GetMesh();
	if (AnimMontage && UseMesh && UseMesh->AnimScriptInstance)
	{
		return UseMesh->AnimScriptInstance->Montage_Play(AnimMontage, InPlayRate);
	}

	return 0.0f;
}


void ASenniteCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
	Jump();
}

void ASenniteCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
	StopJumping();
}

void ASenniteCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void ASenniteCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void ASenniteCharacter::MoveForward(float Value)
{
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("move forward ") + FString::FromInt(GetNetMode()));
	
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		ASennitePlayerState* State = Cast<ASennitePlayerState>(Controller->PlayerState);
		if (State) {
			if (State->JustUsedHealingItem) {
				return;
			}
			// find out which way is forward
			const FRotator Rotation = Controller->GetControlRotation();
			const FRotator YawRotation(0, Rotation.Yaw, 0);

			// get forward vector
			const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
			if (AimedIn) {
				AddMovementInput(Direction, Value * 0.2);
			}
			else if (IsRunning) {
				AddMovementInput(Direction, Value * 0.9);
			}
			else if (IsWalking) {
				AddMovementInput(Direction, Value * 0.45);
			}
		}
	}
	/*UThirdPersonAnimInstance* Animation = Cast<UThirdPersonAnimInstance>(GetMesh()->GetAnimInstance());
	if (Animation) {
		//set blend space variable
		Animation->WalkingY = Value * 90;
		Animation->RunningY = Value * 90;
		//Server_SetMovingVariables();
	}*/
	// the server is told which way only when it changes, not every frame the key is held
	const int32 Way = Value == 0 ? 0 : (Value > 0 ? 1 : -1);
	if (Way != LastForwardSent) {
		LastForwardSent = Way;
		if (Way == 0) {
			ServerResetMovingForward();
		}
		else if (Way > 0) {
			ServerSetMovingForwards();
		}
		else {
			ServerSetMovingBackwards();
		}
	}
}

void ASenniteCharacter::MoveRight(float Value)
{

	if ((Controller != nullptr) && (Value != 0.0f))
	{
		ASennitePlayerState* State = Cast<ASennitePlayerState>(Controller->PlayerState);
		if (State) {
			if (State->JustUsedHealingItem) {
				return;
			}
		}
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		if (AimedIn) {
			AddMovementInput(Direction, Value * 0.2);
		}
		else if (IsRunning) {
			AddMovementInput(Direction, Value * 0.9);
		}
		else if (IsWalking) {
			AddMovementInput(Direction, Value * 0.45);
		}
	}
	/*UThirdPersonAnimInstance* Animation = Cast<UThirdPersonAnimInstance>(GetMesh()->GetAnimInstance());
	if (Animation) {
		//set blend space variable
		Animation->WalkingX = Value * 90;
		Animation->RunningX = Value * 90;
		//Server_SetMovingVariables();
	}*/
	const int32 Way = Value == 0 ? 0 : (Value > 0 ? 1 : -1);
	if (Way != LastRightSent) {
		LastRightSent = Way;
		if (Way == 0) {
			ServerResetMovingRight();
		}
		else if (Way > 0) {
			ServerSetMovingRight();
		}
		else {
			ServerSetMovingLeft();
		}
	}
}

void ASenniteCharacter::Sprint(float Value) {
	APlayerController* LocalController = Cast<APlayerController>(GetController());
	if (LocalController == nullptr) {
		return;
	}
	bool ADown = LocalController->IsInputKeyDown(EKeys::A);
	bool WDown = LocalController->IsInputKeyDown(EKeys::W);
	bool SDown = LocalController->IsInputKeyDown(EKeys::S);
	bool DDown = LocalController->IsInputKeyDown(EKeys::D);
	bool OnlyAOrDDown = !WDown && !SDown && (ADown || DDown);
	// The character's own aim state decides this, whichever animation class the hero uses.
	if (AimedIn) {
		return;
	}
	// can only sprint if the w key is held down by itself or in combination with the a or d keys
	const bool bWantRun = Value != 0 && !(OnlyAOrDDown || SDown) && WDown;
	// the server is told only when that changes, not every frame
	if (static_cast<int32>(bWantRun) != LastRunSent) {
		LastRunSent = static_cast<int32>(bWantRun);
		if (bWantRun) {
			ServerSetIsRunningTrue();
		}
		else {
			ServerSetIsRunningFalse();
		}
	}
}

void ASenniteCharacter::StartWalking() {
	ServerSetIsWalkingTrue();
}


void ASenniteCharacter::StopWalking() {
	//UThirdPersonAnimInstance* Animation = Cast<UThirdPersonAnimInstance>(GetMesh()->GetAnimInstance());
	APlayerController* LocalController = Cast<APlayerController>(GetController());
	bool ADown = LocalController->IsInputKeyDown(EKeys::A);
	bool WDown = LocalController->IsInputKeyDown(EKeys::W);
	bool SDown = LocalController->IsInputKeyDown(EKeys::S);
	bool DDown = LocalController->IsInputKeyDown(EKeys::D);
	bool NoWalkingKeysDown = !ADown && !WDown && !SDown && !DDown;

	if (NoWalkingKeysDown) {
		ServerSetIsWalkingFalse();
	}
	//ServerSetAnimationVariables();
}

TArray<float> ASenniteCharacter::CalculateWalkingXY() {
	float X = 0;
	float Y = 0;
	APlayerController* LocalController = Cast<APlayerController>(GetController());
	if (LocalController->IsInputKeyDown(EKeys::A)) {
		X -= 90;
	}
	if (LocalController->IsInputKeyDown(EKeys::D)) {
		X += 90;
	}
	if (LocalController->IsInputKeyDown(EKeys::W)) {
		Y += 90;
	}
	if (LocalController->IsInputKeyDown(EKeys::S)) {
		Y -= 90;
	}
	TArray<float> Coords;
	Coords.Add(X);
	Coords.Add(Y);
	return Coords;
}

void ASenniteCharacter::PreviewWall() {
	ServerSetBuildModeWall();
}

void ASenniteCharacter::PreviewRamp() {
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "c key pressed");
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "c key presse2d");
	ServerSetBuildModeRamp();
}

void ASenniteCharacter::PreviewFloor() {
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "f key pressed");
	ServerSetBuildModeFloor();
}

void ASenniteCharacter::BuildStructure(float Value) {
	if (Value <= 0 || GetController() == nullptr) {
		return;
	}
	ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
	if (State == nullptr || !State->InBuildMode) {
		return;
	}
	const TArray<TSubclassOf<ABuildingActor>>* Classes = BuildingClassesOf(State->BuildMode, false);
	if (Classes == nullptr || !Classes->IsValidIndex(CurrentBuildingMaterial) || (*Classes)[CurrentBuildingMaterial] == nullptr
		|| !State->MaterialCounts.IsValidIndex(CurrentBuildingMaterial) || State->MaterialCounts[CurrentBuildingMaterial] < BuildingPieceCost) {
		return;
	}
	FVector GridLocation;
	FRotator GridRotation;
	BuildPlacement(State->BuildMode, GridLocation, GridRotation);

	// the button is read every frame it is held: ask for a given piece once, not sixty times a second
	const double Now = GetWorld()->GetTimeSeconds();
	if (State->BuildMode == LastBuildKind && GridLocation.Equals(LastBuildLocation, 1.0) && Now - LastBuildTime < 0.5) {
		return;
	}
	LastBuildKind = State->BuildMode;
	LastBuildLocation = GridLocation;
	LastBuildTime = Now;

	const TSubclassOf<ABuildingActor> PieceClass = (*Classes)[CurrentBuildingMaterial];
	if (HasAuthority()) {
		// the host's own machine is the server: the piece is built once, there
		ServerBuildStructure(PieceClass, GridLocation, GridRotation, INDEX_NONE);
		return;
	}
	// a player on another machine sees the piece at once; the server's answer replaces it, and the
	// server alone takes the materials
	ABuildingActor* Piece = GetWorld()->SpawnActor<ABuildingActor>(PieceClass, GridLocation, GridRotation);
	if (Piece == nullptr) {
		return;
	}
	if (IsBuildingPieceBlocked(Piece)) {
		Piece->Destroy();
		return;
	}
	Piece->Id = CurrentStructureId;
	CurrentStructureId += 1;
	ClientSpawnedStructures.Add(Piece);
	ServerBuildStructure(PieceClass, GridLocation, GridRotation, Piece->Id);
}

void ASenniteCharacter::SwitchBuildingMaterial() {
	ServerChangeBuildingMaterial();
}

void ASenniteCharacter::ShootGun() {
	ServerFireWeapon();
}

void ASenniteCharacter::UseHealingItem() {
	ServerUseHealingItem(CurrentHealingItemType);
}

void ASenniteCharacter::Reload() {
	ServerReloadWeapons();
}

void ASenniteCharacter::AimGunIn() {
	ServerAimDownSights();
}

void ASenniteCharacter::AimGunOut() {
	ServerAimHipFire();
}

void ASenniteCharacter::PickaxeTimeOut() {
	ServerPickaxeTimeOut();
}

void ASenniteCharacter::RifleTimeOut() {
	ServerRifleTimeOut();
}

void ASenniteCharacter::ShotgunTimeOut() {
	ServerShotgunTimeOut();
}

void ASenniteCharacter::RifleReloadTimeOut() {
	ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
	State->JustReloadedRifle= false;
}

void ASenniteCharacter::ShotgunReloadTimeOut() {
	ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
	State->JustReloadedShotgun = false;
}

void ASenniteCharacter::HoldPickaxe() {
	ServerSwitchToPickaxe();
}

void ASenniteCharacter::HoldAssaultRifle() {
	ServerSwitchToRifle();
}

void ASenniteCharacter::HoldShotgun() {
	ServerSwitchToShotgun();
}

void ASenniteCharacter::HoldBandage() {
	ServerSwitchToHealingItem(0);
}

void ASenniteCharacter::HoldPotion() {
	ServerSwitchToHealingItem(1);
}

float ASenniteCharacter::GetHealth() {
	return Health;
}

float ASenniteCharacter::GetShield() {
	return Shield;
}

int ASenniteCharacter::GetWoodMaterialCount() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			return State->MaterialCounts[0];
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
}

int ASenniteCharacter::GetStoneMaterialCount() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			return State->MaterialCounts[1];
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
}

int ASenniteCharacter::GetSteelMaterialCount() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			return State->MaterialCounts[2];
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
}

int ASenniteCharacter::GetAssaultRifleAmmoCount() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			return State->EquippedWeaponsAmmunition[1] + State->EquippedWeaponsClips[1];
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
}

int ASenniteCharacter::GetShotgunAmmoCount() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			return State->EquippedWeaponsAmmunition[2] + State->EquippedWeaponsClips[2];
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
}

int ASenniteCharacter::GetHealingItemCount(int HealingItemType) {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			return State->HealingItemCounts[HealingItemType];
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
}

void ASenniteCharacter::OpenSettingsMenu() {
	if (GetController()) {
		APlayerController* PlayerController = Cast<APlayerController>(GetController());
		if (PlayerController->GetHUD()) {
			ASenniteHUD* SenniteHUD = Cast<ASenniteHUD>(PlayerController->GetHUD());
			SenniteHUD->DrawSettingsMenu();
		}
	}
}

void ASenniteCharacter::ServerSetIsWalkingTrue_Implementation() {
	IsWalking = true;
}

bool ASenniteCharacter::ServerSetIsWalkingTrue_Validate() {
	return true;
}

void ASenniteCharacter::ServerSetIsWalkingFalse_Implementation() {
	IsWalking = false;
}

bool ASenniteCharacter::ServerSetIsWalkingFalse_Validate() {
	return true;
}

void ASenniteCharacter::ServerSetIsRunningTrue_Implementation() {
	IsRunning = true;
}

bool ASenniteCharacter::ServerSetIsRunningTrue_Validate() {
	return true;
}

void ASenniteCharacter::ServerSetIsRunningFalse_Implementation() {
	IsRunning = false;
}

bool ASenniteCharacter::ServerSetIsRunningFalse_Validate() {
	return true;
}

void ASenniteCharacter::ServerSetWalkingSpeed_Implementation() {
	GetCharacterMovement()->MaxWalkSpeed = 450.0;
	ClientSetWalkingSpeed();
}

bool ASenniteCharacter::ServerSetWalkingSpeed_Validate() {
	return true;
}

void ASenniteCharacter::ClientSetWalkingSpeed_Implementation() {
	GetCharacterMovement()->MaxWalkSpeed = 450.0;
}

void ASenniteCharacter::ServerSetRunningSpeed_Implementation() {
	GetCharacterMovement()->MaxWalkSpeed = 900.0;
	ClientSetRunningSpeed();
}

bool ASenniteCharacter::ServerSetRunningSpeed_Validate() {
	return true;
}

void ASenniteCharacter::ClientSetRunningSpeed_Implementation() {
	GetCharacterMovement()->MaxWalkSpeed = 900.0;
}

void ASenniteCharacter::ServerSetAimedInSpeed_Implementation() {
	GetCharacterMovement()->MaxWalkSpeed = 200.0;
	ClientSetAimedInSpeed();
}

bool ASenniteCharacter::ServerSetAimedInSpeed_Validate() {
	return true;
}

void ASenniteCharacter::ClientSetAimedInSpeed_Implementation() {
	GetCharacterMovement()->MaxWalkSpeed = 200.0;
}

void ASenniteCharacter::ServerSetMovingForwards_Implementation() {
	WalkingY = 90;
	RunningY = 90;
}

bool ASenniteCharacter::ServerSetMovingForwards_Validate() {
	return true;
}

void ASenniteCharacter::ServerSetMovingBackwards_Implementation() {
	WalkingY = -90;
	RunningY = -90;
}

bool ASenniteCharacter::ServerSetMovingBackwards_Validate() {
	return true;
}

void ASenniteCharacter::ServerSetMovingLeft_Implementation() {
	WalkingX = -90;
	RunningX = -90;
}

bool ASenniteCharacter::ServerSetMovingLeft_Validate() {
	return true;
}

void ASenniteCharacter::ServerSetMovingRight_Implementation() {
	WalkingX = 90;
	RunningX = 90;
}

bool ASenniteCharacter::ServerSetMovingRight_Validate() {
	return true;
}

void ASenniteCharacter::ServerResetMovingForward_Implementation() {
	WalkingY = 0;
	RunningY = 0;
}

bool ASenniteCharacter::ServerResetMovingForward_Validate() {
	return true;
}

void ASenniteCharacter::ServerResetMovingRight_Implementation() {
	WalkingX = 0;
	RunningX = 0;
}

bool ASenniteCharacter::ServerResetMovingRight_Validate() {
	return true;
}

void ASenniteCharacter::ServerBuildStructure_Implementation(TSubclassOf<ABuildingActor> StructureClass, FVector SpawnLocation, FRotator SpawnRotation, int StructureId) {
	// The server builds only a piece of its own lists, of the kind this player
	// is in build mode for, on the build grid, within reach, clear of players
	// and of a piece already there, and paid for from the count the server
	// holds. The client's own copy of the piece is removed either way.
	FString PieceKind;
	const int32 MaterialType = BuildingMaterialOf(StructureClass, PieceKind);
	ASennitePlayerState* State = GetController() ? Cast<ASennitePlayerState>(GetController()->PlayerState) : nullptr;
	if (MaterialType != INDEX_NONE && State != nullptr
		&& State->InBuildMode && State->BuildMode == PieceKind
		&& State->MaterialCounts.IsValidIndex(MaterialType)
		&& State->MaterialCounts[MaterialType] >= BuildingPieceCost
		&& IsOnBuildGrid(SpawnLocation, SpawnRotation)
		&& FVector::Dist(SpawnLocation, GetActorLocation()) <= MaxBuildReach) {
		ABuildingActor* Piece = GetWorld()->SpawnActor<ABuildingActor>(StructureClass, SpawnLocation, SpawnRotation);
		if (Piece != nullptr) {
			if (IsBuildingPieceBlocked(Piece)) {
				Piece->Destroy();
			}
			else {
				State->MaterialCounts[MaterialType] -= BuildingPieceCost;
			}
		}
	}
	ClientDestroyStructure(StructureId);
}

int32 ASenniteCharacter::BuildingMaterialOf(TSubclassOf<ABuildingActor> StructureClass, FString& OutPieceKind) const {
	if (StructureClass == nullptr) {
		return INDEX_NONE;
	}
	const TPair<const TArray<TSubclassOf<ABuildingActor>>*, const TCHAR*> Lists[] = {
		{ &WallClasses, TEXT("Wall") }, { &RampClasses, TEXT("Ramp") }, { &FloorClasses, TEXT("Floor") } };
	for (const auto& List : Lists) {
		const int32 Found = List.Key->Find(StructureClass);
		if (Found != INDEX_NONE) {
			OutPieceKind = List.Value;
			return Found;
		}
	}
	return INDEX_NONE;
}

bool ASenniteCharacter::IsOnBuildGrid(const FVector& Location, const FRotator& Rotation) const {
	// every piece sits on a multiple of half a grid cell, turned in quarter turns, and upright
	const auto IsMultipleOf = [](double Value, double Step) {
		return FMath::Abs(Value - FMath::RoundHalfFromZero(Value / Step) * Step) <= 0.5;
	};
	const double HalfCell = BuildGridCell * 0.5;
	return IsMultipleOf(Location.X, HalfCell) && IsMultipleOf(Location.Y, HalfCell) && IsMultipleOf(Location.Z, HalfCell)
		&& IsMultipleOf(Rotation.Yaw, 90.0) && FMath::Abs(Rotation.Pitch) <= 0.5 && FMath::Abs(Rotation.Roll) <= 0.5;
}

bool ASenniteCharacter::IsBuildingPieceBlocked(const ABuildingActor* Piece) const {
	// a piece may not be built into a player: players' trigger capsules overlap it
	TArray<AActor*> OverlappingActors;
	Piece->GetOverlappingActors(OverlappingActors, ASenniteCharacter::StaticClass());
	if (OverlappingActors.Num() > 0) {
		return true;
	}
	// nor where a piece of its kind already stands. Built pieces are solid to each other, which is not an
	// overlap, so the pieces themselves are asked.
	for (TActorIterator<ABuildingActor> It(GetWorld()); It; ++It) {
		const ABuildingActor* Structure = *It;
		if (Structure != Piece && !Structure->IsPreview && Structure->StructureType.Equals(Piece->StructureType)
			&& FVector::Dist(Structure->GetActorLocation(), Piece->GetActorLocation()) < 1.0) {
			return true;
		}
	}
	return false;
}

bool ASenniteCharacter::ServerBuildStructure_Validate(TSubclassOf<ABuildingActor> StructureClass, FVector SpawnLocation, FRotator SpawnRotation, int StructureId) {
	return !SpawnLocation.ContainsNaN() && !SpawnRotation.ContainsNaN();
}

void ASenniteCharacter::ServerSetBuildModeWall_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			if (State->JustUsedHealingItem || State->JustReloadedRifle || State->JustReloadedShotgun) {
				return; //currently healing or reloading
			}
			if (State->HoldingWeapon && State->AimedIn) {
				return; // currently aimed down sight
			}
			if (State->BuildMode == FString("Wall")) {
				// getting out of build mode
				State->InBuildMode = false;
				State->BuildMode = FString("None");
				if (BuildingPreview != nullptr) {
					BuildingPreview->Destroy();
					BuildingPreview = nullptr;
				}
				// equip weapon being held before
				if (CurrentWeaponType > -1 && CurrentWeaponType < 3) {
					ClientGetWeaponTransform(CurrentWeaponType);
				}
				else if (CurrentHealingItemType > -1 && CurrentHealingItemType < 2) {
					//equip healing item since current weapon was null
					ClientGetHealingItemTransform(CurrentHealingItemType);
				}
			}
			else if (State->InBuildMode) {
				// switching to a different build mode
				State->BuildMode = FString("Wall");
			}
			else {
				// getting into build mode
				State->InBuildMode = true;
				State->BuildMode = FString("Wall");
				State->HoldingWeapon = false;
				State->HoldingHealingItem = false;
				State->AimedIn = false; 
				//animinstance properties
				HoldingWeapon = false;
				AimedIn = false;
				HoldingWeaponType = 0;

				// unequip weapon/healing item
				if (CurrentWeapon && CurrentWeaponType > 0 && CurrentWeaponType < 3) {
					State->EquippedWeaponsClips[CurrentWeaponType] = CurrentWeapon->CurrentBulletCount;
				}
				if (CurrentWeapon) {
					CurrentWeapon->Destroy();
					CurrentWeapon = nullptr;
				}
				if (CurrentHealingItem) {
					CurrentHealingItem->Destroy();
					CurrentHealingItem = nullptr;
				}
			}
		}
	}
}

bool ASenniteCharacter::ServerSetBuildModeWall_Validate() {
	return true;
}

void ASenniteCharacter::ServerSetBuildModeRamp_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			if (State->JustUsedHealingItem || State->JustReloadedRifle || State->JustReloadedShotgun) {
				return; //currently healing or reloading
			}
			if (State->HoldingWeapon && State->AimedIn) {
				return; // currently aimed down sight
			}
			if (State->BuildMode == FString("Ramp")) {
				// getting out of build mode
				State->InBuildMode = false;
				State->BuildMode = FString("None");
				if (BuildingPreview != nullptr) {
					BuildingPreview->Destroy();
					BuildingPreview = nullptr;
				}
				// equip weapon being held before
				if (CurrentWeaponType > -1 && CurrentWeaponType < 3) {
					ClientGetWeaponTransform(CurrentWeaponType);
				}
				else if(CurrentHealingItemType > -1 && CurrentHealingItemType < 2){
					//equip healing item since current weapon was null
					ClientGetHealingItemTransform(CurrentHealingItemType);
				}
			}
			else if (State->InBuildMode) {
				// switching to a different build mode
				State->BuildMode = FString("Ramp");
			}
			else {
				// getting into build mode
				State->InBuildMode = true;
				State->BuildMode = FString("Ramp");
				State->HoldingWeapon = false;
				State->HoldingHealingItem = false;
				State->AimedIn = false;
				//animinstance properties
				HoldingWeapon = false;
				AimedIn = false;
				HoldingWeaponType = 0;
				// unequip weapon/healing item
				if (CurrentWeapon && CurrentWeaponType > 0 && CurrentWeaponType < 3) {
					State->EquippedWeaponsClips[CurrentWeaponType] = CurrentWeapon->CurrentBulletCount;
				}
				if (CurrentWeapon) {
					CurrentWeapon->Destroy();
					CurrentWeapon = nullptr;
				}
				if (CurrentHealingItem) {
					CurrentHealingItem->Destroy();
					CurrentHealingItem = nullptr;
				}
			}
		}
	}
}

bool ASenniteCharacter::ServerSetBuildModeRamp_Validate() {
	return true;
}

void ASenniteCharacter::ServerSetBuildModeFloor_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			if (State->JustUsedHealingItem || State->JustReloadedRifle || State->JustReloadedShotgun || State->JustSwungPickaxe) {
				return; //currently healing or reloading or swinging pickaxe
			}
			if (State->HoldingWeapon && State->AimedIn) {
				return; // currently aimed down sight
			}
			if (State->BuildMode == FString("Floor")) {
				// getting out of build mode
				State->InBuildMode = false;
				State->BuildMode = FString("None");
				if (BuildingPreview != nullptr) {
					BuildingPreview->Destroy();
					BuildingPreview = nullptr;
				}
				// equip weapon being held before
				if (CurrentWeaponType > -1 && CurrentWeaponType < 3) {
					ClientGetWeaponTransform(CurrentWeaponType);
				}
				else if (CurrentHealingItemType > -1 && CurrentHealingItemType < 2) {
					//equip healing item since current weapon was null
					ClientGetHealingItemTransform(CurrentHealingItemType);
				}
			}
			else if (State->InBuildMode) {
				// switching to a different build mode
				State->BuildMode = FString("Floor");
			}
			else {
				// getting into build mode
				State->InBuildMode = true;
				State->BuildMode = FString("Floor");
				State->HoldingWeapon = false;
				State->HoldingHealingItem = false;
				State->AimedIn = false;
				//animinstance properties
				HoldingWeapon = false;
				AimedIn = false;
				HoldingWeaponType = 0;
				// unequip weapon/healing item
				if (CurrentWeapon && CurrentWeaponType > 0 && CurrentWeaponType < 3) {
					State->EquippedWeaponsClips[CurrentWeaponType] = CurrentWeapon->CurrentBulletCount;
				}
				if (CurrentWeapon) {
					CurrentWeapon->Destroy();
					CurrentWeapon = nullptr;
				}
				if (CurrentHealingItem) {
					CurrentHealingItem->Destroy();
					CurrentHealingItem = nullptr;
				}
			}
		}
	}
}

bool ASenniteCharacter::ServerSetBuildModeFloor_Validate() {
	return true;
}

void ASenniteCharacter::ServerFireWeapon_Implementation() {
	if (CurrentWeapon != nullptr) {
		if (GetController()) {
			ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
			if (State) {
				//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::FromInt(GetNetMode()) + FString(" Current weapon ") + FString::FromInt(State->CurrentWeapon));
				if (State->HoldingWeapon) {
					if (State->CurrentWeapon > 0 && State->CurrentWeapon < 3 && CurrentWeapon->CurrentBulletCount <= 0) {
						// no bullets in magazine, need to reload
						ServerReloadWeapons();
						return;
					}
					if (State->JustReloadedRifle || State->JustReloadedShotgun) {
						return; //currently reloading
					}
					if (State->AimedIn) {
						if (State->CurrentWeapon == 1) {
							if (State->JustShotRifle) {
								return;
							}
							NetMulticastPlayShootRifleIronsightsAnimation();
							CurrentWeapon->CurrentBulletCount--;
							bShotAuthorized = true;
							State->EquippedWeaponsClips[CurrentWeaponType]--;
							State->JustShotRifle = true;
							FTimerHandle RifleTimerHandle;
							GetWorldTimerManager().SetTimer(RifleTimerHandle, this, &ASenniteCharacter::ServerRifleTimeOut, 0.233f, false);
						}
						else if (State->CurrentWeapon == 2) {
							if (State->JustShotShotgun) {
								return;
							}
							NetMulticastPlayShootShotgunIronsightsAnimation();
							CurrentWeapon->CurrentBulletCount--;
							bShotAuthorized = true;
							State->EquippedWeaponsClips[CurrentWeaponType]--;
							State->JustShotShotgun = true;
							FTimerHandle ShotgunTimerHandle;
							GetWorldTimerManager().SetTimer(ShotgunTimerHandle, this, &ASenniteCharacter::ServerShotgunTimeOut, 1.3f, false);
						}
					}
					else {
						if (State->CurrentWeapon == 0) {
							//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "pickaxe swung");
							if (State->JustSwungPickaxe) {
								return;
							}
							NetMulticastPlayPickaxeSwingAnimation();
							State->JustSwungPickaxe = true;
							FTimerHandle PickaxeTimerHandle;
							GetWorldTimerManager().SetTimer(PickaxeTimerHandle, this, &ASenniteCharacter::ServerPickaxeTimeOut, 0.403f, false);
						}
						if (State->CurrentWeapon == 1) {
							if (State->JustShotRifle) {
								return;
							}
							NetMulticastPlayShootRifleAnimation();
							CurrentWeapon->CurrentBulletCount--;
							bShotAuthorized = true;
							State->EquippedWeaponsClips[CurrentWeaponType]--;
							State->JustShotRifle = true;
							FTimerHandle RifleTimerHandle;
							GetWorldTimerManager().SetTimer(RifleTimerHandle, this, &ASenniteCharacter::ServerRifleTimeOut, 0.233f, false);
						}
						else if (State->CurrentWeapon == 2) {
							if (State->JustShotShotgun) {
								return;
							}
							NetMulticastPlayShootShotgunAnimation();
							CurrentWeapon->CurrentBulletCount--;
							bShotAuthorized = true;
							State->EquippedWeaponsClips[CurrentWeaponType]--;
							State->JustShotShotgun = true;
							FTimerHandle ShotgunTimerHandle;
							GetWorldTimerManager().SetTimer(ShotgunTimerHandle, this, &ASenniteCharacter::ServerShotgunTimeOut, 1.3f, false);
						}

					}
					ClientGetBulletTransform();


				}
			}
		}
	}
}

bool ASenniteCharacter::ServerFireWeapon_Validate() {
	return true;
}

void ASenniteCharacter::ServerUseHealingItem_Implementation(int HealingItemType) {
	if (HealingItemType == 0) {
		if (Health >= 100) {
			return; // player has full health
		}
	}
	else if (HealingItemType == 1) {
		if (Shield >= 100) {
			return; // player has full shield
		}
	}
	else {
		return;
	}
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State && State->HoldingHealingItem) {
			if (State->HealingItemCounts[HealingItemType] < 1) {
				return; // player has no healing items to use
			}

			if (State->JustUsedHealingItem) {
				return;
			}
			NetMulticastPlayUseHealingItemAnimation();
			State->JustUsedHealingItem = true;
			State->HealingItemCounts[HealingItemType]--;
			FTimerHandle HealingItemTimerHandle;
			FTimerDelegate HealingItemTimerDelegate;
			HealingItemTimerDelegate.BindUFunction(this, FName("HealingItemTimeOut"), HealingItemType);
			GetWorldTimerManager().SetTimer(HealingItemTimerHandle, HealingItemTimerDelegate, 3.321f, false);
		}
	}
}

bool ASenniteCharacter::ServerUseHealingItem_Validate(int HealingItemType) {
	return true;
}


void ASenniteCharacter::ServerReloadWeapons_Implementation() { // todo add current weapon null check
	if (CurrentWeapon != nullptr) {
		if (GetController()) {
			ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
			if (State) {
				if (State->CurrentWeapon > 2 || State->CurrentWeapon < 1) {
					return; // can only reload if holding a assault rifle or shotgun
				}
				if (State->JustShotRifle || State->JustShotShotgun || State->JustReloadedRifle || State->JustReloadedShotgun) {
					return; // currently reloading or just shot
				}
				if (State->AimedIn) {
					if (State->CurrentWeapon == 1) {
						if (State->JustShotRifle) {
							return;
						}
						if (State->EquippedWeaponsAmmunition[State->CurrentWeapon] <= 0) {
							return; // no ammo left
						}

						int BulletsNeeded = CurrentWeapon->MagazineSize - CurrentWeapon->CurrentBulletCount;
						if (BulletsNeeded == 0) {
							return; // magazine is full
						}

						if (State->EquippedWeaponsAmmunition[State->CurrentWeapon] < BulletsNeeded) {
							BulletsNeeded = State->EquippedWeaponsAmmunition[State->CurrentWeapon];
							State->EquippedWeaponsAmmunition[State->CurrentWeapon] = 0;
						}
						else {
							State->EquippedWeaponsAmmunition[State->CurrentWeapon] -= BulletsNeeded;
						}
						NetMulticastPlayReloadRifleIronsightsAnimation();
						CurrentWeapon->CurrentBulletCount += BulletsNeeded;
						State->EquippedWeaponsClips[State->CurrentWeapon] += BulletsNeeded;
						State->JustReloadedRifle = true;
						FTimerHandle RifleTimerHandle;
						GetWorldTimerManager().SetTimer(RifleTimerHandle, this, &ASenniteCharacter::ServerRifleReloadTimeOut, 2.167f, false);
					}
					else if (State->CurrentWeapon == 2) {
						if (State->JustShotShotgun) {
							return;
						}
						if (State->EquippedWeaponsAmmunition[State->CurrentWeapon] <= 0) {
							return; // no ammo left
						}

						int BulletsNeeded = CurrentWeapon->MagazineSize - CurrentWeapon->CurrentBulletCount;
						if (BulletsNeeded == 0) {
							return; // magazine is full
						}

						if (State->EquippedWeaponsAmmunition[State->CurrentWeapon] < BulletsNeeded) {
							BulletsNeeded = State->EquippedWeaponsAmmunition[State->CurrentWeapon];
							State->EquippedWeaponsAmmunition[State->CurrentWeapon] = 0;
						}
						else {
							State->EquippedWeaponsAmmunition[State->CurrentWeapon] -= BulletsNeeded;
						}
						NetMulticastPlayReloadShotgunIronsightsAnimation();
						CurrentWeapon->CurrentBulletCount += BulletsNeeded;
						State->EquippedWeaponsClips[State->CurrentWeapon] += BulletsNeeded;
						State->JustReloadedShotgun = true;
						FTimerHandle ShotgunTimerHandle;
						GetWorldTimerManager().SetTimer(ShotgunTimerHandle, this, &ASenniteCharacter::ServerShotgunReloadTimeOut, 4.3f, false);
					}
				}
				else {
					if (State->CurrentWeapon == 1) {
						if (State->JustShotRifle) {
							return;
						}
						if (State->EquippedWeaponsAmmunition[State->CurrentWeapon] <= 0) {
							return; // no ammo left
						}
						int BulletsNeeded = CurrentWeapon->MagazineSize - CurrentWeapon->CurrentBulletCount;
						if (BulletsNeeded == 0) {
							return; // magazine is full
						}
						//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::FromInt(State->EquippedWeaponsAmmunition[State->CurrentWeapon]));
						if (State->EquippedWeaponsAmmunition[State->CurrentWeapon] < BulletsNeeded) {
							BulletsNeeded = State->EquippedWeaponsAmmunition[State->CurrentWeapon];
							State->EquippedWeaponsAmmunition[State->CurrentWeapon] = 0;
						}
						else {
							State->EquippedWeaponsAmmunition[State->CurrentWeapon] -= BulletsNeeded;
						}
						NetMulticastPlayReloadRifleAnimation();
						CurrentWeapon->CurrentBulletCount += BulletsNeeded;
						State->EquippedWeaponsClips[State->CurrentWeapon] += BulletsNeeded;
						//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::FromInt(CurrentWeapon->CurrentBulletCount));
						State->JustReloadedRifle = true;
						FTimerHandle RifleTimerHandle;
						GetWorldTimerManager().SetTimer(RifleTimerHandle, this, &ASenniteCharacter::ServerRifleReloadTimeOut, 2.167f, false);
					}
					else if (State->CurrentWeapon == 2) {
						if (State->JustShotShotgun) {
							return;
						}
						if (State->EquippedWeaponsAmmunition[State->CurrentWeapon] <= 0) {
							return; // no ammo left
						}
						int BulletsNeeded = CurrentWeapon->MagazineSize - CurrentWeapon->CurrentBulletCount;
						if (BulletsNeeded == 0) {
							return; // magazine is full
						}

						if (State->EquippedWeaponsAmmunition[State->CurrentWeapon] < BulletsNeeded) {
							BulletsNeeded = State->EquippedWeaponsAmmunition[State->CurrentWeapon];
							State->EquippedWeaponsAmmunition[State->CurrentWeapon] = 0;
						}
						else {
							State->EquippedWeaponsAmmunition[State->CurrentWeapon] -= BulletsNeeded;
						}
						NetMulticastPlayReloadShotgunAnimation();
						CurrentWeapon->CurrentBulletCount += BulletsNeeded;
						State->EquippedWeaponsClips[State->CurrentWeapon] += BulletsNeeded;
						State->JustReloadedShotgun = true;
						FTimerHandle ShotgunTimerHandle;
						GetWorldTimerManager().SetTimer(ShotgunTimerHandle, this, &ASenniteCharacter::ServerShotgunReloadTimeOut, 4.3f, false);
					}

				}
			}
		}
	}
}

bool ASenniteCharacter::ServerReloadWeapons_Validate() {
	return true;
}

void ASenniteCharacter::ServerSwitchToPickaxe_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			if (State->CurrentWeapon == 0 && !State->InBuildMode) {
				return; // currently holding a pickaxe while not in build mode
			}
			if (State->HoldingWeapon && State->AimedIn) {
				return; // currently aimed down sight
			}
			if (State->JustUsedHealingItem || State->JustReloadedRifle || State->JustReloadedShotgun || State->JustShotRifle || State->JustShotShotgun) {
				return; // currently healing or currently reloading or just shot a weapon
			}
			else {
				if (State->InBuildMode) {
					State->InBuildMode = false;
					State->BuildMode = FString("None");
					if (BuildingPreview != nullptr) {
						BuildingPreview->Destroy();
						BuildingPreview = nullptr;
					}
				}
				if (CurrentWeapon) {
					State->EquippedWeaponsClips[CurrentWeaponType] = CurrentWeapon->CurrentBulletCount;
					CurrentWeapon->Destroy();
					CurrentWeapon = nullptr;
				}
				if (CurrentHealingItem) {
					CurrentHealingItem->Destroy();
					CurrentHealingItem = nullptr;
				}
				ClientGetWeaponTransform(0);
			}
		}
	}
}

bool ASenniteCharacter::ServerSwitchToPickaxe_Validate() {
	return true;
}

void ASenniteCharacter::ServerSwitchToRifle_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			if (State->CurrentWeapon == 1 && !State->InBuildMode) {
				return; // currently holding a assault rifle while not in build mode
			}
			if (State->HoldingWeapon && State->AimedIn) {
				return; // currently aimed down sight
			}
			if (!State->EquippedWeapons.Contains(1) || State->JustUsedHealingItem || State->JustReloadedRifle || State->JustReloadedShotgun || State->JustSwungPickaxe || State->JustShotShotgun) {
				return; // already holding the assault rifle or doesn't have one or is currently healing or currently reloading or swinging pickaxe or shooting shotgun
			}
			else {
				if (State->InBuildMode) {
					State->InBuildMode = false;
					State->BuildMode = FString("None");
					if (BuildingPreview != nullptr) {
						BuildingPreview->Destroy();
						BuildingPreview = nullptr;
					}
				}
				if (CurrentWeapon && CurrentWeaponType > 0 && CurrentWeaponType < 3) {
					State->EquippedWeaponsClips[CurrentWeaponType] = CurrentWeapon->CurrentBulletCount;
				}
				if (CurrentWeapon) {
					CurrentWeapon->Destroy();
					CurrentWeapon = nullptr;
				}
				if (CurrentHealingItem) {
					CurrentHealingItem->Destroy();
					CurrentHealingItem = nullptr;
				}
				ClientGetWeaponTransform(1);
			}
		}
	}
}

bool ASenniteCharacter::ServerSwitchToRifle_Validate() {
	return true;
}

void ASenniteCharacter::ServerSwitchToShotgun_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			if (State->CurrentWeapon == 2 && !State->InBuildMode) {
				return; // currently holding a shotgun while not in build mode
			}
			if (State->HoldingWeapon && State->AimedIn) {
				return; // currently aimed down sight
			}
			if (!State->EquippedWeapons.Contains(2) || State->JustUsedHealingItem || State->JustReloadedRifle || State->JustReloadedShotgun || State->JustSwungPickaxe || State->JustShotRifle) {
				return; // already holding the pickaxe or doesn't have one or is currently healing or currently reloading or swinging pickaxe or just shot rifle
			}
			else {
				if (State->InBuildMode) {
					State->InBuildMode = false;
					State->BuildMode = FString("None");
					if (BuildingPreview != nullptr) {
						BuildingPreview->Destroy();
						BuildingPreview = nullptr;
					}
				}
				if (CurrentWeapon && CurrentWeaponType > 0 && CurrentWeaponType < 3) {
					State->EquippedWeaponsClips[CurrentWeaponType] = CurrentWeapon->CurrentBulletCount;
				}
				if (CurrentWeapon) {
					CurrentWeapon->Destroy();
					CurrentWeapon = nullptr;
				}
				if (CurrentHealingItem) {
					CurrentHealingItem->Destroy();
					CurrentHealingItem = nullptr;
				}
				ClientGetWeaponTransform(2);
			}
		}
	}
}

bool ASenniteCharacter::ServerSwitchToShotgun_Validate() {
	return true;
}

void ASenniteCharacter::ServerSwitchToHealingItem_Implementation(int HealingItemType) {
	/*FString LogMsg = FString("server switch to healing item Current healing item type ") + FString::FromInt(HealingItemType);
	UE_LOG(LogSenniteCharacter, Warning, TEXT("%s"), *LogMsg);*/
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			if (State->JustReloadedRifle || State->JustReloadedShotgun || State->JustSwungPickaxe || State->JustShotRifle || State->JustShotShotgun) {
				return; //currently reloading weapons or swinging pickaxe
			}
			if (State->HoldingWeapon && State->AimedIn) {
				return; // currently aimed down sight
			}
			if (State->CurrentHealingItem == HealingItemType && !State->InBuildMode) {
				return; // already holding healing items while not in build mode
			}
			if (!State->HealingItemCounts.IsValidIndex(HealingItemType) || State->HealingItemCounts[HealingItemType] < 1) {
				return; // has none of it to hold
			}
			else {
				if (State->InBuildMode) {
					State->InBuildMode = false;
					State->BuildMode = FString("None");
					if (BuildingPreview != nullptr) {
						BuildingPreview->Destroy();
						BuildingPreview = nullptr;
					}
				}
				//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::FromInt(CurrentWeaponType));
				if (CurrentWeapon && CurrentWeaponType > 0 && CurrentWeaponType < 3) {
					State->EquippedWeaponsClips[CurrentWeaponType] = CurrentWeapon->CurrentBulletCount;
				}
				if (CurrentWeapon) {
					CurrentWeapon->Destroy();
					CurrentWeapon = nullptr;
				}
				if (CurrentHealingItem) {
					CurrentHealingItem->Destroy();
					CurrentHealingItem = nullptr;
				}
				ClientGetHealingItemTransform(HealingItemType);
			}
		}
	}
}

bool ASenniteCharacter::ServerSwitchToHealingItem_Validate(int HealingItemType) {
	return true;
}

void ASenniteCharacter::ServerChangeBuildingMaterial_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State && State->InBuildMode) {
			if (CurrentBuildingMaterial == 2) {
				CurrentBuildingMaterial = 0;
			}
			else {
				CurrentBuildingMaterial++;
			}
		}
	}
}

bool ASenniteCharacter::ServerChangeBuildingMaterial_Validate() {
	return true;
}

void ASenniteCharacter::ServerAimDownSights_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State && State->HoldingWeapon && State->CurrentWeapon != 0) {
			AimedIn = true;
			HoldingWeaponType = 2;
			ClientCameraAimIn();
			//ServerSetAimedInSpeed();
			State->AimedIn = true;
		}
	}
}

bool ASenniteCharacter::ServerAimDownSights_Validate() {
	return true;
}

void ASenniteCharacter::ServerAimHipFire_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State && State->HoldingWeapon && State->CurrentWeapon != 0) {
			AimedIn = false;
			HoldingWeaponType = 1;
			ClientCameraAimOut();
			//ServerSetWalkingSpeed();
			State->AimedIn = false;
		}
	}
	
}

bool ASenniteCharacter::ServerAimHipFire_Validate() {
	return true;
}

void ASenniteCharacter::ServerPickaxeTimeOut_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			State->JustSwungPickaxe = false;
		}
	}
}

bool ASenniteCharacter::ServerPickaxeTimeOut_Validate() {
	return true;
}

void ASenniteCharacter::ServerRifleTimeOut_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			State->JustShotRifle = false;
		}
	}
}

bool ASenniteCharacter::ServerRifleTimeOut_Validate() {
	return true;
}

void ASenniteCharacter::ServerShotgunTimeOut_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			State->JustShotShotgun = false;
		}
	}
}

bool ASenniteCharacter::ServerShotgunTimeOut_Validate() {
	return true;
}


void ASenniteCharacter::HealingItemTimeOut(int HealingItemType) {
	if (HealingItemType == 0) {
		if (Health < 100) {
			if (Health + 15 > 100) {
				Health = 100;
			}
			else {
				Health += 15;
			}
		}
	}
	else if (HealingItemType == 1) {
		if (Shield < 100) {
			if (Shield + 20 > 100) {
				Shield = 100;
			}
			else {
				Shield += 20;
			}
		}
	}
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			State->JustUsedHealingItem = false;
		}
	}
}

void ASenniteCharacter::ServerApplyStormDamage_Implementation() {
	if (CurrentStorm != nullptr && InStorm) {
		//get storm actor and get its damage component and apply the damage to the player's health
		Health -= CurrentStorm->Damage;
		if (Health <= 0) {
			if (BuildingPreview != nullptr) {
				BuildingPreview->Destroy();
				BuildingPreview = nullptr;
			}
			if (CurrentWeapon) {
				CurrentWeapon->Destroy();
			}
			if (CurrentHealingItem) {
				CurrentHealingItem->Destroy();
			}
			if (GetController()) {
				ASennitePlayerController* SennitePlayerController = Cast<ASennitePlayerController>(GetController());
				if (SennitePlayerController) {
					SennitePlayerController->ServerSwitchToSpectatorMode();
				}
			}
			Destroy();
		}
	}
}

bool ASenniteCharacter::ServerApplyStormDamage_Validate() {
	return true;
}

void ASenniteCharacter::ServerRifleReloadTimeOut_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			State->JustReloadedRifle = false;
		}
	}
}

bool ASenniteCharacter::ServerRifleReloadTimeOut_Validate() {
	return true;
}

void ASenniteCharacter::ServerShotgunReloadTimeOut_Implementation() {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			State->JustReloadedShotgun = false;
		}
	}
}

bool ASenniteCharacter::ServerShotgunReloadTimeOut_Validate() {
	return true;
}

void ASenniteCharacter::ServerSpawnProjectile_Implementation(FTransform SpawnTransform) { // to do add current weapon null check
	// A projectile spawns only for a shot ServerFireWeapon accepted (weapon held,
	// ammunition, cooldowns), once, and only from close to the shooter.
	const bool bAuthorized = bShotAuthorized;
	bShotAuthorized = false;
	if (bAuthorized && CurrentWeapon != nullptr && FVector::Dist(SpawnTransform.GetLocation(), GetActorLocation()) <= MaxMuzzleDistance) {
		auto Bullet = Cast<AProjectileActor>(UGameplayStatics::BeginDeferredActorSpawnFromClass(this, CurrentWeapon->BulletClass, SpawnTransform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn));
		if (Bullet != nullptr)
		{
			//spawnactor has no way of passing parameters so need to use begindeferredactorspawn and finishspawningactor
			Bullet->Weapon = CurrentWeapon;
			Bullet->WeaponHolder = this;
			Bullet->SetOwner(this);

			UGameplayStatics::FinishSpawningActor(Bullet, SpawnTransform);
		}
	}
}

bool ASenniteCharacter::ServerSpawnProjectile_Validate(FTransform SpawnTransform) {
	return !SpawnTransform.ContainsNaN();
}

void ASenniteCharacter::ServerSpawnAndAttachWeapon_Implementation(int WeaponType, FTransform SpawnTransform) {
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			CurrentWeaponType = WeaponType;
			CurrentHealingItemType = -1;
			CurrentWeapon = WeaponClasses.IsValidIndex(WeaponType) && WeaponClasses[WeaponType] != nullptr
				? Cast<AWeaponActor>(UGameplayStatics::BeginDeferredActorSpawnFromClass(this, WeaponClasses[WeaponType], SpawnTransform))
				: nullptr;
			if (CurrentWeapon != nullptr)
			{
				CurrentWeapon->CurrentBulletCount = State->EquippedWeaponsClips[CurrentWeaponType];
				//spawnactor has no way of passing parameters so need to use begindeferredactorspawn and finishspawningactor
				CurrentWeapon->Holder = this;
				UGameplayStatics::FinishSpawningActor(CurrentWeapon, SpawnTransform);

				AttachWeaponToHand(CurrentWeapon);

				//animinstance properties
				HoldingWeapon = true;
				AimedIn = false;
				HoldingWeaponType = 1;
				State->HoldingWeapon = true;
				State->HoldingHealingItem = false;
				State->CurrentWeapon = WeaponType;
				State->CurrentHealingItem = -1;
			}
		}
	}
}

bool ASenniteCharacter::ServerSpawnAndAttachWeapon_Validate(int WeaponType, FTransform SpawnTransform) {
	return true;
}

void ASenniteCharacter::ServerSpawnAndAttachHealingItem_Implementation(int HealingItemType, FTransform SpawnTransform) {
	/*FString LogMsg = FString("server spawn and attach Current healing item type ") + FString::FromInt(HealingItemType);
	UE_LOG(LogSenniteCharacter, Warning, TEXT("%s"), *LogMsg);*/
	if (GetController()) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(GetController()->PlayerState);
		if (State) {
			CurrentWeaponType = -1;
			CurrentHealingItemType = HealingItemType;
			auto HealingItem = HealingItemClasses.IsValidIndex(CurrentHealingItemType) && HealingItemClasses[CurrentHealingItemType] != nullptr
				? Cast<AHealingActor>(UGameplayStatics::BeginDeferredActorSpawnFromClass(this, HealingItemClasses[CurrentHealingItemType], SpawnTransform))
				: nullptr;
			if (HealingItem != nullptr)
			{
				CurrentHealingItem = HealingItem;
				CurrentHealingItem->Holder = this;

				UGameplayStatics::FinishSpawningActor(CurrentHealingItem, SpawnTransform);

				// half its size on the ground, in the palm of the hand the hero names
				CurrentHealingItem->SetActorScale3D(CurrentHealingItem->GetActorScale3D() * 0.5);
				CurrentHealingItem->ItemMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, true), HeroItemBone);
				CurrentHealingItem->ItemMesh->SetRelativeLocation(HeroItemPalmOffset);

				HoldingWeapon = false;
				AimedIn = false;
				HoldingWeaponType = 0;
				State->HoldingWeapon = false;
				State->HoldingHealingItem = true;
				State->CurrentWeapon = -1;
				State->CurrentHealingItem = HealingItemType;
			}
		}
	}
}

bool ASenniteCharacter::ServerSpawnAndAttachHealingItem_Validate(int HealingItemType, FTransform SpawnTransform) {
	return true;
}

void ASenniteCharacter::ClientCameraAimIn_Implementation() {
	FollowCamera->FieldOfView = 45;
}

void ASenniteCharacter::ClientCameraAimOut_Implementation() {
	FollowCamera->FieldOfView = 90;
}

void ASenniteCharacter::ClientGetBulletTransform_Implementation() {
	// The shot leaves the barrel of the held weapon, along the line the camera looks down,
	// when that line meets something.
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (CurrentWeapon == nullptr || PlayerController == nullptr) {
		return;
	}
	FRotator CameraRotation;
	FVector CameraLocation;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector ShootDirection = CameraRotation.Vector();
	const FVector StartTrace = FollowCamera->GetComponentLocation();
	const FVector EndTrace = StartTrace + ShootDirection * 100000.0;
	FHitResult Impact;
	FCollisionQueryParams CollisionParams;
	CollisionParams.AddIgnoredActor(this);
	GetWorld()->LineTraceSingleByChannel(Impact, StartTrace, EndTrace, ECC_Visibility, CollisionParams);
	if (Impact.bBlockingHit) {
		ServerSpawnProjectile(FTransform(ShootDirection.Rotation(), CurrentWeapon->GetMuzzleLocation()));
	}
}

void ASenniteCharacter::NetMulticastPlayPickaxeSwingAnimation_Implementation() {
	PlayAnimMontage(PickaxeSwingingAnimation, 1.f, NAME_None);
}

void ASenniteCharacter::NetMulticastPlayShootRifleAnimation_Implementation() {
	PlayAnimMontage(RifleHipShootingAnimation);
}

void ASenniteCharacter::NetMulticastPlayShootShotgunAnimation_Implementation() {
	PlayAnimMontage(ShotgunHipShootingAnimation);
}

void ASenniteCharacter::NetMulticastPlayShootRifleIronsightsAnimation_Implementation() {
	PlayAnimMontage(RifleIronsightsShootingAnimation);
}

void ASenniteCharacter::NetMulticastPlayShootShotgunIronsightsAnimation_Implementation() {
	PlayAnimMontage(ShotgunIronsightsShootingAnimation);
}

void ASenniteCharacter::NetMulticastPlayUseHealingItemAnimation_Implementation() {
	PlayAnimMontage(HealingAnimation);
}

void ASenniteCharacter::NetMulticastPlayReloadRifleAnimation_Implementation() {
	PlayAnimMontage(RifleHipReloadAnimation);
}

void ASenniteCharacter::NetMulticastPlayReloadRifleIronsightsAnimation_Implementation() {
	PlayAnimMontage(RifleIronsightsReloadAnimation);
}

void ASenniteCharacter::NetMulticastPlayReloadShotgunAnimation_Implementation() {
	PlayAnimMontage(ShotgunHipReloadAnimation);
}

void ASenniteCharacter::NetMulticastPlayReloadShotgunIronsightsAnimation_Implementation() {
	PlayAnimMontage(ShotgunIronsightsReloadAnimation);
}

void ASenniteCharacter::ClientDrawHitMarker_Implementation() {
	if (GetController()) {
		APlayerController* PlayerController = Cast<APlayerController>(GetController());
		if (PlayerController->GetHUD()) {
			ASenniteHUD* SenniteHUD = Cast<ASenniteHUD>(PlayerController->GetHUD());
			SenniteHUD->DrawHitMarker();
		}
	}
}

void ASenniteCharacter::ClientDrawBloodEffect_Implementation() {
	if (GetController()) {
		APlayerController* PlayerController = Cast<APlayerController>(GetController());
		if (PlayerController->GetHUD()) {
			ASenniteHUD* SenniteHUD = Cast<ASenniteHUD>(PlayerController->GetHUD());
			SenniteHUD->DrawBloodSplash();
		}
	}
}

void ASenniteCharacter::ClientDestroyStructure_Implementation(int StructureId) {
	for (int i = 0; i < ClientSpawnedStructures.Num(); i++) {
		if (ClientSpawnedStructures[i] != nullptr) {
			if (ClientSpawnedStructures[i]->Id == StructureId) {
				ABuildingActor* Structure = ClientSpawnedStructures[i];
				ClientSpawnedStructures.Remove(Structure);
				Structure->Destroy();
				break;
			}
		}
		
	}
}

void ASenniteCharacter::ClientGetWeaponTransform_Implementation(int WeaponType) {
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("ClientGetweapontransform"));
	FTransform SpawnTransform(GetActorRotation(), GetActorLocation());
	ServerSpawnAndAttachWeapon(WeaponType, SpawnTransform);
}

void ASenniteCharacter::ClientGetHealingItemTransform_Implementation(int HealingItemType) {
	/*FString LogMsg = FString("client get healing item transform Current healing item type ") + FString::FromInt(HealingItemType);
	UE_LOG(LogSenniteCharacter, Warning, TEXT("%s"), *LogMsg);*/
	FTransform SpawnTransform(GetActorRotation(), GetActorLocation());
	ServerSpawnAndAttachHealingItem(HealingItemType, SpawnTransform);
}

void ASenniteCharacter::OnRepSetSkin() {
	// The same rule as the server's in BeginPlay: a skin is put on only if the list has one at that
	// place. With no skins listed the hero keeps the materials it ships with.
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (CharacterMesh == nullptr) {
		return;
	}
	if (MaterialOrMaterialInstance == 0 && SkinMaterials.IsValidIndex(CurrentSkin)) {
		// assign UMaterial
		CharacterMesh->SetMaterial(0, SkinMaterials[CurrentSkin]);
	}
	else if (MaterialOrMaterialInstance == 1 && SkinMaterialInstances.IsValidIndex(CurrentSkin)) {
		// assign UMaterialInstance
		CharacterMesh->SetMaterial(0, SkinMaterialInstances[CurrentSkin]);
	}
}