// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SenniteCharacter.h"
#include "SenniteSounds.h"
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
#include "SenniteCrate.h"
#include "SenniteHeroMesh.h"
#include "SenniteGameInstance.h"
#include "SenniteHeroes.h"
#include "SenniteMatch.h"
#include "SennitePublisher.h"
#include "SenniteBaseEditor.h"
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
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Materials/Material.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"

DEFINE_LOG_CATEGORY(LogSenniteCharacter);
//////////////////////////////////////////////////////////////////////////
// ASenniteCharacter

ASenniteCharacter::ASenniteCharacter(const FObjectInitializer& ObjectInitializer)
	// the hero's body holds a gun in both hands (USenniteHeroMesh)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USenniteHeroMesh>(ACharacter::MeshComponentName))
{
	bReplicates = true;
	// A hero is sent to the players within a kilometre of it. The engine's own limit is 150 m, and the
	// middles of two neighbouring plots are 720 m apart over open ground: a hero across it was unseen.
	// Not every hero to everyone: a player's game is not told where heroes are that it could never see.
	SetNetCullDistanceSquared(FMath::Square(100000.0f));
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
	FollowDistance = CameraBoom->TargetArmLength;
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

	// The hero's mesh and animation Blueprint come from the hero's entry in the list (USenniteHeroes) and are
	// put on the Mesh component in ApplyHero. The heroes are authored facing +Y with their feet at the
	// origin, so the mesh sits at the bottom of the capsule, turned to face the capsule's forward.
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -97.0f), FRotator(0.0f, -90.0f, 0.0f));

	Health = 100.0f;
	Shield = 0.0f;

	// slot 0 is the pickaxe, which is not held yet; then the guns: 1 the rifle, 2 the shotgun, 3 the
	// energy rifle, 4 the sniper
	WeaponClasses.SetNum(5);
	WeaponClasses[1] = ASenniteRifle::StaticClass();
	WeaponClasses[2] = ASenniteShotgun::StaticClass();
	WeaponClasses[3] = ASenniteEnergyRifle::StaticClass();
	WeaponClasses[4] = ASenniteSniper::StaticClass();
	OwnBoltClass = ASenniteOwnBolt::StaticClass();

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
	// The player builds along the quarter turn the camera faces, on the level they stand on.
	const double Cell = BuildGridCell;
	const FRotator View = GetControlRotation();
	const double Pitch = FRotator::NormalizeAxis(View.Pitch);
	const double FacingYaw = FMath::RoundHalfFromZero(FRotator::NormalizeAxis(View.Yaw) / 90.0) * 90.0;
	const FVector Ahead = FRotator(0.0, FacingYaw, 0.0).Vector();
	const FVector Feet = GetActorLocation() - FVector(0.0, 0.0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const auto Snap = [Cell](double Value) { return FMath::RoundHalfFromZero(Value / Cell) * Cell; };
	double Level = FMath::FloorToDouble((Feet.Z + 1.0) / Cell) * Cell;
	FVector2D Own(Snap(Feet.X), Snap(Feet.Y));
	// Standing on a ramp, the level is the one the ramp reaches at the edge the player faces: facing up it, its
	// top, so that the next ramp goes on from where this one ends and a run of them climbs into the sky; facing
	// down it, its foot; across it, whichever the feet are nearer. The owner, 2026-11-19: "the next ramp just
	// automatically goes to the ground level. You should be able to build a ramp that walks you into the sky".
	const UCharacterMovementComponent* Moves = GetCharacterMovement();
	const ABuildingActor* On = Moves != nullptr ? Cast<ABuildingActor>(Moves->CurrentFloor.HitResult.GetActor()) : nullptr;
	if (On != nullptr && On->IsPreview) {
		On = nullptr;
	}
	if (On != nullptr && On->StructureType == TEXT("Ramp")) {
		const double Foot = On->GetActorLocation().Z;
		const double Rising = FVector::DotProduct(Ahead, On->GetActorForwardVector());
		Own = FVector2D(On->GetActorLocation().X, On->GetActorLocation().Y);
		Level = Rising > 0.5 ? Foot + Cell : Rising < -0.5 ? Foot : Foot + (Feet.Z - Foot > Cell * 0.5 ? Cell : 0.0);
	}
	// The height is the player's to pick: looking well above the horizon builds one level up; and from a piece,
	// looking well down builds one level down. ("You should be able to pick your build height if it's coming
	// off of another piece.")
	if (Pitch > BuildUpAbove) {
		Level += Cell;
	}
	else if (On != nullptr && Pitch < -BuildDownBelow) {
		Level -= Cell;
	}
	const FVector OwnCell(Own.X, Own.Y, Level);
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

float ASenniteCharacter::GunSize() const
{
	// the arm of the hero's own skeleton as it was made, shoulder to wrist, against the arm the guns are sized for
	const USkeletalMesh* Body = GetMesh() != nullptr ? GetMesh()->GetSkeletalMeshAsset() : nullptr;
	if (Body == nullptr || GunArm <= 0.0f) {
		return 1.0f;
	}
	const FReferenceSkeleton& Bones = Body->GetRefSkeleton();
	const int32 Lower = Bones.FindBoneIndex(TEXT("lowerarm_r"));
	const int32 Hand = Bones.FindBoneIndex(TEXT("hand_r"));
	if (Lower == INDEX_NONE || Hand == INDEX_NONE) {
		return 1.0f;
	}
	const TArray<FTransform>& Made = Bones.GetRefBonePose();
	const float Arm = Made[Lower].GetLocation().Size() + Made[Hand].GetLocation().Size();
	return FMath::Clamp(Arm / GunArm, GunSizeLeast, GunSizeMost);
}

void ASenniteCharacter::AttachWeaponToHand(AWeaponActor* Weapon)
{
	if (Weapon == nullptr || Weapon->WeaponMesh == nullptr) {
		return;
	}
	Weapon->WeaponMesh->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, HeroWeaponBone);
	// a big hero holds a big gun: the gun is as large as the hero's arm is long, within limits
	const float Size = GunSize();
	Weapon->WeaponMesh->SetRelativeScale3D(FVector(Size));
	// the hero's hand decides how the weapon is turned; the weapon says which point of it the palm closes on
	Weapon->WeaponMesh->SetRelativeLocationAndRotation(HeroPalmOffset - HeroGripRotation.RotateVector(Weapon->GripPoint * Size), HeroGripRotation);
}

void ASenniteCharacter::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	// the hero the server named, or until it has, the default hero
	ApplyHero();
}

void ASenniteCharacter::SetHero(FName Id)
{
	if (!HasAuthority() || USenniteHeroes::Find(Id) == nullptr || Id == HeroId) {
		return;
	}
	HeroId = Id;
	ApplyHero();
}

void ASenniteCharacter::OnRep_HeroId()
{
	ApplyHero();
}

void ASenniteCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (const ASennitePlayerState* State = NewController != nullptr ? NewController->GetPlayerState<ASennitePlayerState>() : nullptr) {
		SetHero(State->HeroId);
	}
}

void ASenniteCharacter::WearMesh(USkeletalMesh* Mesh)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (Body == nullptr || Mesh == nullptr) {
		return;
	}
	// A hero's animation makes its own copies of its mesh's materials and sets them on the body. They are
	// the old mesh's: left there, the new mesh is drawn with them, place for place, and a skin whose parts
	// are in another order has its arms in its mask's material and its legs in its gun's.
	Body->EmptyOverrideMaterials();
	Body->SetSkeletalMeshAsset(Mesh);
}

void ASenniteCharacter::ApplyHero()
{
	const FSenniteHero& Hero = USenniteHeroes::FindOrDefault(HeroId);
	USkeletalMeshComponent* Body = GetMesh();
	if (Body == nullptr || (Hero.Id == DressedHero && Body->GetSkeletalMeshAsset() != nullptr)) {
		return;
	}
	DressedHero = Hero.Id;
	HeroMesh = Hero.Mesh;
	HeroAnimClass = Hero.AnimClass;
	HeroWeaponBone = Hero.WeaponBone;
	HeroGripRotation = Hero.GripRotation;
	HeroPalmOffset = Hero.PalmOffset;
	HeroBuiltInWeaponBones = Hero.BuiltInWeaponBones;
	HeroItemBone = Hero.ItemBone;
	HeroItemPalmOffset = Hero.ItemPalmOffset;
	HeroAttackMontage = Hero.AttackMontage;
	HeroCastBone = Hero.CastBone;
	bHeroMelee = Hero.bMelee;
	bHeroFloats = Hero.bFloats;
	// the hero's own attack, for when it has no gun in hand
	OwnAttack = HeroAttackMontage.IsNull() ? nullptr : Cast<UAnimMontage>(HeroAttackMontage.TryLoad());
	if (!HeroAttackMontage.IsNull() && OwnAttack == nullptr) {
		UE_LOG(LogSenniteCharacter, Error, TEXT("Hero attack animation '%s' did not load"), *HeroAttackMontage.ToString());
	}

	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(HeroMesh.TryLoad())) {
		WearMesh(Mesh);
		WornMesh = HeroMesh;
	}
	else {
		UE_LOG(LogSenniteCharacter, Error, TEXT("Hero mesh '%s' did not load; the character has no body"), *HeroMesh.ToString());
	}
	if (UClass* AnimClass = HeroAnimClass.TryLoadClass<UAnimInstance>()) {
		Body->SetAnimInstanceClass(AnimClass);
	}
	else {
		UE_LOG(LogSenniteCharacter, Error, TEXT("Hero animation class '%s' did not load; the character will not animate"), *HeroAnimClass.ToString());
	}
	// the new mesh shows every bone: the built-in weapon is hidden again by Tick while a game weapon is held
	bBuiltInWeaponHidden = false;
	// the feet on the floor of the capsule, the face to the capsule's forward
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, -Hero.MeshDrop));

	// what is in hand is held anew, in this hero's hand
	if (CurrentWeapon != nullptr) {
		AttachWeaponToHand(CurrentWeapon);
	}
	if (CurrentHealingItem != nullptr && CurrentHealingItem->ItemMesh != nullptr) {
		CurrentHealingItem->ItemMesh->AttachToComponent(Body, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, true), HeroItemBone);
		CurrentHealingItem->ItemMesh->SetRelativeLocation(HeroItemPalmOffset);
	}
	// a hero seen once stays loaded, so the next one of it does not stall the game
	if (USenniteGameInstance* Game = GetGameInstance<USenniteGameInstance>()) {
		Game->KeepHero(Hero.Id);
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
	PlayerInputComponent->BindAction("ShootGun", IE_Released, this, &ASenniteCharacter::StopShooting);
	PlayerInputComponent->BindAction("UseHealingItem", IE_Pressed, this, &ASenniteCharacter::UseHealingItem);
	PlayerInputComponent->BindAction("Reload", IE_Pressed, this, &ASenniteCharacter::Reload);
	PlayerInputComponent->BindAction("HoldPickaxe", IE_Pressed, this, &ASenniteCharacter::HoldPickaxe);
	PlayerInputComponent->BindAction("HoldEnergyRifle", IE_Pressed, this, &ASenniteCharacter::HoldEnergyRifle);
	PlayerInputComponent->BindAction("HoldSniper", IE_Pressed, this, &ASenniteCharacter::HoldSniper);
	PlayerInputComponent->BindAction("HoldAssaultRifle", IE_Pressed, this, &ASenniteCharacter::HoldAssaultRifle);
	PlayerInputComponent->BindAction("HoldShotgun", IE_Pressed, this, &ASenniteCharacter::HoldShotgun);
	PlayerInputComponent->BindAction("HoldBandage", IE_Pressed, this, &ASenniteCharacter::HoldBandage);
	PlayerInputComponent->BindAction("HoldPotion", IE_Pressed, this, &ASenniteCharacter::HoldPotion);
	PlayerInputComponent->BindAction("Ironsights", IE_Pressed, this, &ASenniteCharacter::AimGunIn);
	PlayerInputComponent->BindAction("Ironsights", IE_Released, this, &ASenniteCharacter::AimGunOut);
	PlayerInputComponent->BindAction("OpenCrate", IE_Pressed, this, &ASenniteCharacter::OpenCrate);
	PlayerInputComponent->BindAction("EditPiece", IE_Pressed, this, &ASenniteCharacter::EditPiece);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &ASenniteCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &ASenniteCharacter::LookUpAtRate);
	PlayerInputComponent->BindAxis("BuildStructure", this, &ASenniteCharacter::BuildStructure);

	// a device played by touch has its own controls, which press these same keys (SenniteTouch.h)

	// VR headset functionality

}

void ASenniteCharacter::BeginPlay() {
	Super::BeginPlay();
	// every bullet's round and laser is loaded before the first shot (AProjectileActor::Preload)
	AProjectileActor::Preload(BulletLooks);
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
		// the storm, if the game has one, for the damage it does
		CurrentStorm = AStormActor::Find(GetWorld());
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
	DOREPLIFETIME(ASenniteCharacter, HeroId);
}

namespace
{
	/** The tile of a piece, cut or not, that the player's view falls on, or INDEX_NONE. */
	int32 AimedTile(const ASenniteCharacter* Hero, const ABuildingActor* Piece)
	{
		const AController* Own = Hero->GetController();
		if (Own == nullptr || Piece == nullptr) {
			return INDEX_NONE;
		}
		FVector Eye;
		FRotator Look;
		Own->GetPlayerViewPoint(Eye, Look);
		double At = 0.0;
		return Piece->TileUnderAim(Eye, Look.Vector(), Hero->MaxBuildReach + FVector::Dist(Eye, Hero->GetActorLocation()) + ABuildingActor::Cell, At);
	}
}

void ASenniteCharacter::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);
	// The skin the player wears is on the hero in every world it plays in: the arena and a match as much as the
	// faction's land. The owner, 2026-11-19: "It doesn't actually play as that skin in the demo, in the gameplay".
	if (const APlayerController* Mine = Cast<APlayerController>(GetController()); Mine != nullptr && Mine->IsLocalController()) {
		WearChosenSkin();
	}
	// Editing a piece, on this player's own machine: a drag with the fire button held marks each tile
	// the aim crosses alike; walking out of reach of the piece, or its fall, ends the edit unmade.
	if (ABuildingActor* Editing = EditingPiece.Get()) {
		if (Editing->IsActorBeingDestroyed() || FVector::Dist(Editing->GetActorLocation(), GetActorLocation()) > MaxBuildReach + ABuildingActor::Cell) {
			CancelEdit();
		}
		else if (bEditDragging) {
			const int32 Tile = AimedTile(this, Editing);
			if (Tile != INDEX_NONE && Tile != EditDragLast) {
				EditDragLast = Tile;
				MarkEditTile(Tile, bEditDragCuts);
			}
		}
	}
	else if (!EditingPiece.IsExplicitlyNull()) {
		EditingPiece = nullptr;
		bEditDragging = false;
	}
	// The weapon that is part of the hero's own mesh gives way to a game weapon in the hand.
	// CurrentWeapon is replicated, so every machine hides and shows it alike.
	// In the storm means outside its safe circle: farther from the circle's centre than its radius.
	// The server decides; InStorm is replicated.
	if (HasAuthority() && CurrentStorm != nullptr) {
		InStorm = FVector::Dist2D(GetActorLocation(), CurrentStorm->GetActorLocation()) > CurrentStorm->GetRadius();
	}
	// With a gun in hand the hero faces where the player aims, and steps sideways and back to move
	// that way; with none it turns to face the way it walks. On the server and the player's own machine,
	// which move the hero; everyone else is sent which way it faces.
	{
		const bool bFaceAim = CurrentWeapon != nullptr && CurrentWeapon->WeaponType >= 1;
		UCharacterMovementComponent* Moves = GetCharacterMovement();
		if (Moves != nullptr && Moves->bUseControllerDesiredRotation != bFaceAim) {
			Moves->bUseControllerDesiredRotation = bFaceAim;
			Moves->bOrientRotationToMovement = !bFaceAim;
		}
		// and the player's camera looks over the hero's shoulder, so the gun and its bolts are seen past
		// the hero, not hidden behind it
		// Aiming, it comes in from behind the hero to the hero's own eyes: the hero's back is not under the
		// crosshair, the player sees what the hero sees. Whether the hero is aiming is the one thing asked: the
		// view closes in and opens out with it, however the aiming ended.
		if (IsLocallyControlled() && CameraBoom != nullptr && FollowCamera != nullptr) {
			const bool bEyes = AimedIn;
			const FVector Beside = bEyes ? FVector(AimEyesAhead, 0.0f, 0.0f) : bFaceAim ? ShoulderView : FVector::ZeroVector;
			CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, Beside, DeltaTime, bEyes ? 14.0f : 8.0f);
			// up to the eyes whichever way the player looks: an offset in the world, not one that tips with the view
			CameraBoom->TargetOffset = FMath::VInterpTo(CameraBoom->TargetOffset, FVector(0.0f, 0.0f, bEyes ? BaseEyeHeight : 0.0f), DeltaTime, 14.0f);
			CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, bEyes ? 0.0f : FollowDistance, DeltaTime, 14.0f);
			FollowCamera->SetFieldOfView(FMath::FInterpTo(FollowCamera->FieldOfView, bEyes ? AimFieldOfView : FollowFieldOfView, DeltaTime, 14.0f));
			// Close enough to be inside the hero, the hero is not drawn for its own player (it still casts its shadow, and
			// everyone else sees it), and its bones are kept up to date unseen: a shot leaves the gun where the gun is.
			SeeThroughOwnEyes(CameraBoom->TargetArmLength < 0.4f * FollowDistance);
		}
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
	// The preview shows where the next piece would go: drawn on the building player's own machine only,
	// where it follows them every frame; it changes with the kind and the material, and goes when they
	// leave build mode.
	if (IsLocallyControlled()) {
		const ASennitePlayerState* State = GetPlayerState<ASennitePlayerState>();
		const TArray<TSubclassOf<ABuildingActor>>* Previews = State != nullptr && State->InBuildMode ? BuildingClassesOf(State->BuildMode, true) : nullptr;
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
	if (HasAuthority()) {
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
			// Weapons and healing items are in crates (ASenniteCrate), never lying about: only a box
			// of rounds is taken by walking over it.
			if (OtherActor->IsA(AAmmunitionActor::StaticClass())) {
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
	if (Value <= 0 || GetController() == nullptr || EditingPiece.IsValid()) {
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
	if (EditingPiece.IsValid()) {
		return;
	}
	ServerChangeBuildingMaterial();
}

void ASenniteCharacter::ShootGun() {
	// while editing a piece the fire button marks tiles, and fires nothing
	if (ABuildingActor* Editing = EditingPiece.Get()) {
		const int32 Tile = AimedTile(this, Editing);
		if (Tile != INDEX_NONE) {
			bEditDragCuts = (EditCut & (1 << Tile)) == 0;
			MarkEditTile(Tile, bEditDragCuts);
			bEditDragging = true;
			EditDragLast = Tile;
		}
		return;
	}
	ServerFireWeapon();
	// A gun that fires while held goes on firing: a shot is asked for again a little slower than the
	// server lets one out, so every one asked for is fired.
	if (CurrentWeapon != nullptr && CurrentWeapon->bHeldFires) {
		GetWorldTimerManager().SetTimer(HeldFireTimer, this, &ASenniteCharacter::FireWhileHeld, CurrentWeapon->FireSeconds + 0.007f, true);
	}
}

void ASenniteCharacter::FireWhileHeld() {
	const ASennitePlayerState* State = GetPlayerState<ASennitePlayerState>();
	if (CurrentWeapon == nullptr || !CurrentWeapon->bHeldFires || State == nullptr || State->InBuildMode || EditingPiece.IsValid()) {
		StopShooting();
		return;
	}
	ServerFireWeapon();
}

void ASenniteCharacter::StopShooting() {
	GetWorldTimerManager().ClearTimer(HeldFireTimer);
	bEditDragging = false;
}

void ASenniteCharacter::UseHealingItem() {
	if (EditingPiece.IsValid()) {
		return;
	}
	ServerUseHealingItem(CurrentHealingItemType);
}

void ASenniteCharacter::Reload() {
	// while editing a piece the reload button takes the whole piece away
	if (EditingPiece.IsValid()) {
		RemoveEditedPiece();
		return;
	}
	ServerReloadWeapons();
}

void ASenniteCharacter::AimGunIn() {
	// while editing a piece the aim button puts every tile back and finishes
	if (EditingPiece.IsValid()) {
		EditCut = 0;
		FinishEdit();
		return;
	}
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
	ServerHoldGun(1);
}

void ASenniteCharacter::HoldShotgun() {
	ServerHoldGun(2);
}

void ASenniteCharacter::HoldEnergyRifle() {
	ServerHoldGun(3);
}

void ASenniteCharacter::HoldSniper() {
	ServerHoldGun(4);
}

bool ASenniteCharacter::IsGun(int32 Type) const {
	return Type > 0 && WeaponClasses.IsValidIndex(Type) && WeaponClasses[Type] != nullptr;
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

void ASenniteCharacter::OpenCrate() {
	const ASenniteCrate* Rules = GetDefault<ASenniteCrate>();
	if (ASenniteCrate::Nearest(GetWorld(), GetActorLocation(), Rules->Reach + Rules->Across, true) != nullptr) {
		ServerOpenCrate();
	}
}

void ASenniteCharacter::ServerOpenCrate_Implementation() {
	const ASenniteCrate* Rules = GetDefault<ASenniteCrate>();
	if (ASenniteCrate* Crate = ASenniteCrate::Nearest(GetWorld(), GetActorLocation(), Rules->Reach + Rules->Across, true)) {
		Crate->Open(this);
	}
}

int32 ASenniteCharacter::GunsHeld() const {
	int32 Held = 0;
	if (const ASennitePlayerState* State = GetPlayerState<ASennitePlayerState>()) {
		for (const int32 Type : State->EquippedWeapons) {
			Held |= IsGun(Type) ? (1 << Type) : 0;
		}
	}
	return Held;
}

int32 ASenniteCharacter::BoughtGunsAmong(const TArray<FString>& Owned) {
	int32 Bought = 0;
	const TArray<TSubclassOf<AWeaponActor>>& Slots = GetDefault<ASenniteCharacter>()->WeaponClasses;
	for (int32 Type = 1; Type < Slots.Num(); ++Type) {
		const AWeaponActor* Gun = Slots[Type] != nullptr ? GetDefault<AWeaponActor>(Slots[Type]) : nullptr;
		if (Gun != nullptr && !Gun->bFree && !Gun->ShopId.IsEmpty() && Owned.Contains(Gun->ShopId)) {
			Bought |= 1 << Type;
		}
	}
	return Bought;
}

bool ASenniteCharacter::TakeFromCrate(int32 Guns, bool bRounds, int32 Bandages, int32 Potions) {
	ASennitePlayerState* State = GetController() ? Cast<ASennitePlayerState>(GetController()->PlayerState) : nullptr;
	if (!HasAuthority() || State == nullptr || !State->HealingItemCounts.IsValidIndex(1)) {
		return false;
	}
	if (State->InBuildMode || State->JustShotRifle || State->JustShotShotgun || State->JustSwungPickaxe || State->JustUsedHealingItem || State->JustReloadedRifle || State->JustReloadedShotgun) {
		return false; // hands full: the crate stays closed and full
	}
	// each gun the hero does not have yet, with a full magazine; and the crate's rounds for each of its guns
	int32 ToHand = 0;
	int32 NewGuns = 0;
	int32 InStore = 0;
	for (int32 Type = 1; Type < WeaponClasses.Num(); ++Type) {
		if ((Guns & (1 << Type)) == 0 || !IsGun(Type) || !State->EquippedWeaponsClips.IsValidIndex(Type) || !State->EquippedWeaponsAmmunition.IsValidIndex(Type)) {
			continue;
		}
		const AWeaponActor* Gun = GetDefault<AWeaponActor>(WeaponClasses[Type]);
		// a gun bought in the store is this player's only if their wallet proved it owns it; the crate keeps it otherwise
		if (!Gun->bFree && (State->GunsOwned & (1 << Type)) == 0) {
			InStore |= 1 << Type;
			continue;
		}
		if (!State->EquippedWeapons.Contains(Type)) {
			State->EquippedWeapons.Add(Type);
			State->EquippedWeaponsClips[Type] = Gun->MagazineSize;
			NewGuns |= 1 << Type;
			ToHand = ToHand == 0 ? Type : ToHand;
		}
		if (bRounds) {
			State->EquippedWeaponsAmmunition[Type] += Gun->CrateRounds;
		}
	}
	State->HealingItemCounts[0] += Bandages;
	State->HealingItemCounts[1] += Potions;
	if (ToHand != 0) {
		// the first new gun comes to hand; what was in it is put away as it is
		if (CurrentWeapon && IsGun(CurrentWeaponType)) {
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
		ClientGetWeaponTransform(ToHand);
	}
	ClientCrateGave(NewGuns, InStore, bRounds, Bandages, Potions);
	return true;
}

void ASenniteCharacter::ClientCrateGave_Implementation(int32 NewGuns, int32 InStore, bool bRounds, int32 Bandages, int32 Potions) {
	const APlayerController* Own = Cast<APlayerController>(GetController());
	ASenniteHUD* Hud = Own != nullptr ? Cast<ASenniteHUD>(Own->GetHUD()) : nullptr;
	if (Hud == nullptr) {
		return;
	}
	// each new gun by name, then what else it gave
	TArray<FString> Gave;
	for (int32 Type = 1; Type < WeaponClasses.Num(); ++Type) {
		if ((NewGuns & (1 << Type)) != 0 && IsGun(Type)) {
			Gave.Add(GetDefault<AWeaponActor>(WeaponClasses[Type])->DisplayName);
		}
	}
	if (bRounds) {
		Gave.Add(TEXT("+ROUNDS"));
	}
	if (Bandages > 0) {
		Gave.Add(FString::Printf(TEXT("+%d BANDAGES"), Bandages));
	}
	if (Potions > 0) {
		Gave.Add(FString::Printf(TEXT("+%d POTIONS"), Potions));
	}
	// and the guns it kept, which are bought in the store
	TArray<FString> Kept;
	for (int32 Type = 1; Type < WeaponClasses.Num(); ++Type) {
		if ((InStore & (1 << Type)) != 0 && IsGun(Type)) {
			Kept.Add(GetDefault<AWeaponActor>(WeaponClasses[Type])->DisplayName);
		}
	}
	if (Kept.Num() > 0) {
		Gave.Add(TEXT("IN THE STORE: ") + FString::Join(Kept, TEXT(", ")));
	}
	Hud->DrawCrateGave(FString::Join(Gave, TEXT("   ")));
	// and is heard by the player who opened it
	if (USoundBase* Opened = Cast<USoundBase>(GetDefault<ASenniteCrate>()->OpenSound.TryLoad())) {
		UGameplayStatics::PlaySound2D(this, Opened);
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
	// and of a piece already there, with something to hold it up, and paid for
	// from the count the server holds. The client's own copy of the piece is
	// removed either way.
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
			if (IsBuildingPieceBlocked(Piece) || !ABuildingActor::HeldUp(GetWorld()).Contains(Piece)) {
				Piece->Destroy();
			}
			else {
				State->MaterialCounts[MaterialType] -= BuildingPieceCost;
				Piece->FactionId = State->FactionId;
				Piece->Builder = State;
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
				if (CurrentWeaponType > -1 && CurrentWeaponType < WeaponClasses.Num()) {
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
				if (CurrentWeapon && IsGun(CurrentWeaponType)) {
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
				if (CurrentWeaponType > -1 && CurrentWeaponType < WeaponClasses.Num()) {
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
				if (CurrentWeapon && IsGun(CurrentWeaponType)) {
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
				if (CurrentWeaponType > -1 && CurrentWeaponType < WeaponClasses.Num()) {
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
				if (CurrentWeapon && IsGun(CurrentWeaponType)) {
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
	ASennitePlayerState* State = GetController() ? Cast<ASennitePlayerState>(GetController()->PlayerState) : nullptr;
	if (State == nullptr || !State->HoldingWeapon) {
		return;
	}
	// by the server's own clock: too soon after the last shot, or while reloading, nothing is let out
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextShotAt - ShotEarly || Now < ReloadOverAt) {
		return;
	}
	if (State->CurrentWeapon == 0) {
		// No gun in hand: the hero attacks with its own weapon (a staff, a fist, a hammer). It plays its
		// own attack and a bolt leaves the weapon, good up close and at a distance; there is nothing to
		// load and nothing to run out of, only OwnAttackSeconds between one and the next.
		if (CurrentWeapon != nullptr || State->InBuildMode || State->JustSwungPickaxe || State->JustUsedHealingItem) {
			return;
		}
		NetMulticastOwnAttack();
		State->JustSwungPickaxe = true;
		NextShotAt = Now + FMath::Max(0.05f, OwnAttackSeconds);
		FTimerHandle Again;
		GetWorldTimerManager().SetTimer(Again, this, &ASenniteCharacter::ServerPickaxeTimeOut, FMath::Max(0.05f, OwnAttackSeconds), false);
		if (bHeroMelee) {
			// fists, or what the hero swings: no bolt leaves it. The blow lands up close, a moment into the swing.
			FTimerHandle Lands;
			GetWorldTimerManager().SetTimer(Lands, this, &ASenniteCharacter::LandBlow, FMath::Max(0.01f, MeleeLandsAfter), false);
			return;
		}
		ShotBullet = OwnBoltClass;
		ShotGun = nullptr;
		ShotAt = Now;
		ClientGetBulletTransform();
		return;
	}
	if (CurrentWeapon == nullptr || !IsGun(State->CurrentWeapon)) {
		return;
	}
	if (CurrentWeapon->CurrentBulletCount <= 0) {
		// nothing in the magazine: reload
		ServerReloadWeapons();
		return;
	}
	if (State->JustReloadedRifle || State->JustReloadedShotgun || State->JustShotRifle || State->JustShotShotgun) {
		return; // reloading, or the last shot is not over
	}
	// One shot, the same for every gun; what differs is the gun's own: how long until the next
	// (FireSeconds), and whether it goes on while held. A gun that fires while held and one that fires
	// a shot a pull keep the marks the rifle and the shotgun always had.
	NetMulticastGunFired();
	CurrentWeapon->CurrentBulletCount--;
	ShotBullet = CurrentWeapon->BulletClass;
	ShotGun = CurrentWeapon;
	ShotAt = Now;
	NextShotAt = Now + FMath::Max(0.02f, CurrentWeapon->FireSeconds);
	State->EquippedWeaponsClips[CurrentWeaponType]--;
	FTimerHandle Again;
	if (CurrentWeapon->bHeldFires) {
		State->JustShotRifle = true;
		GetWorldTimerManager().SetTimer(Again, this, &ASenniteCharacter::ServerRifleTimeOut, FMath::Max(0.02f, CurrentWeapon->FireSeconds), false);
	}
	else {
		State->JustShotShotgun = true;
		GetWorldTimerManager().SetTimer(Again, this, &ASenniteCharacter::ServerShotgunTimeOut, FMath::Max(0.02f, CurrentWeapon->FireSeconds), false);
	}
	ClientGetBulletTransform();
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


void ASenniteCharacter::ServerReloadWeapons_Implementation() {
	ASennitePlayerState* State = GetController() ? Cast<ASennitePlayerState>(GetController()->PlayerState) : nullptr;
	if (CurrentWeapon == nullptr || State == nullptr || !IsGun(State->CurrentWeapon) || !State->EquippedWeaponsAmmunition.IsValidIndex(State->CurrentWeapon)) {
		return; // only a gun reloads
	}
	if (State->JustShotRifle || State->JustShotShotgun || State->JustReloadedRifle || State->JustReloadedShotgun) {
		return; // reloading already, or the last shot is not over
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextShotAt - ShotEarly || Now < ReloadOverAt) {
		return; // the same, by the server's own clock
	}
	// as many rounds as the magazine lacks, or as many as the hero carries for this gun
	int& Carried = State->EquippedWeaponsAmmunition[State->CurrentWeapon];
	const int Loaded = FMath::Min(CurrentWeapon->MagazineSize - CurrentWeapon->CurrentBulletCount, Carried);
	if (Loaded <= 0) {
		return; // full, or nothing to load it with
	}
	Carried -= Loaded;
	CurrentWeapon->CurrentBulletCount += Loaded;
	State->EquippedWeaponsClips[State->CurrentWeapon] += Loaded;
	// the reload takes the gun's own time (ReloadSeconds); the marks are the rifle's and the shotgun's, as for a shot
	ReloadOverAt = Now + FMath::Max(0.05f, CurrentWeapon->ReloadSeconds);
	FTimerHandle Loading;
	if (CurrentWeapon->bHeldFires) {
		State->AimedIn ? NetMulticastPlayReloadRifleIronsightsAnimation() : NetMulticastPlayReloadRifleAnimation();
		State->JustReloadedRifle = true;
		GetWorldTimerManager().SetTimer(Loading, this, &ASenniteCharacter::ServerRifleReloadTimeOut, FMath::Max(0.05f, CurrentWeapon->ReloadSeconds), false);
	}
	else {
		State->AimedIn ? NetMulticastPlayReloadShotgunIronsightsAnimation() : NetMulticastPlayReloadShotgunAnimation();
		State->JustReloadedShotgun = true;
		GetWorldTimerManager().SetTimer(Loading, this, &ASenniteCharacter::ServerShotgunReloadTimeOut, FMath::Max(0.05f, CurrentWeapon->ReloadSeconds), false);
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

void ASenniteCharacter::ServerHoldGun_Implementation(int32 Type) {
	ASennitePlayerState* State = GetController() ? Cast<ASennitePlayerState>(GetController()->PlayerState) : nullptr;
	if (State == nullptr || !IsGun(Type)) {
		return;
	}
	if (State->CurrentWeapon == Type && !State->InBuildMode) {
		return; // in hand already
	}
	if (State->HoldingWeapon && State->AimedIn) {
		return; // aiming in
	}
	if (!State->EquippedWeapons.Contains(Type) || State->JustUsedHealingItem || State->JustReloadedRifle || State->JustReloadedShotgun
		|| State->JustSwungPickaxe || State->JustShotRifle || State->JustShotShotgun) {
		return; // has none, or is healing, reloading, swinging or between shots
	}
	if (State->InBuildMode) {
		State->InBuildMode = false;
		State->BuildMode = FString("None");
		if (BuildingPreview != nullptr) {
			BuildingPreview->Destroy();
			BuildingPreview = nullptr;
		}
	}
	// the gun put away keeps what is in its magazine
	if (CurrentWeapon && IsGun(CurrentWeaponType)) {
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
	ClientGetWeaponTransform(Type);
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
				if (CurrentWeapon && IsGun(CurrentWeaponType)) {
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
		else if (State && State->CurrentWeapon == 0 && !State->InBuildMode && CurrentHealingItem == nullptr && !bHeroMelee) {
			// the hero's own weapon is aimed as a gun is: the view closes in on where its bolt will go. Fists and what
			// a hero swings are not aimed: there is no bolt to send anywhere.
			AimedIn = true;
			ClientCameraAimIn();
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
		else if (State && (AimedIn || State->AimedIn)) {
			// aiming its own weapon, or whatever it took in hand since
			AimedIn = false;
			ClientCameraAimOut();
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
		// the storm takes health directly, past any shield
		Health -= CurrentStorm->Damage;
		if (Health <= 0) {
			Die(nullptr);
		}
	}
}

void ASenniteCharacter::TakeHit(float Damage, ASenniteCharacter* Shooter) {
	if (!HasAuthority() || Health <= 0 || IsActorBeingDestroyed()) {
		return;
	}
	if (Shooter != nullptr) {
		if (const ASenniteMatch* Match = ASenniteMatch::Find(GetWorld())) {
			// in a match a shot counts only across factions, and only while the match is being played
			const ASennitePlayerState* Mine = GetPlayerState<ASennitePlayerState>();
			if (Mine == nullptr || !Match->Harms(Shooter, Mine->FactionId)) {
				return;
			}
		}
	}
	ClientDrawBloodEffect();
	if (Shield > 0) {
		if (Shield - Damage < 0) {
			Health -= Damage - Shield;
			Shield = 0;
		}
		else {
			Shield -= Damage;
		}
	}
	else {
		Health -= Damage;
	}
	if (Shooter != nullptr) {
		Shooter->ClientDrawHitMarker();
	}
	if (Health <= 0) {
		Die(Shooter);
	}
}

void ASenniteCharacter::FellOutOfWorld(const UDamageType& DamageType) {
	if (HasAuthority()) {
		Die(nullptr);
	}
}

void ASenniteCharacter::Die(ASenniteCharacter* Killer) {
	if (!HasAuthority() || IsActorBeingDestroyed()) {
		return;
	}
	if (BuildingPreview != nullptr) {
		BuildingPreview->Destroy();
		BuildingPreview = nullptr;
	}
	if (CurrentWeapon != nullptr) {
		CurrentWeapon->Destroy();
		CurrentWeapon = nullptr;
	}
	if (CurrentHealingItem != nullptr) {
		CurrentHealingItem->Destroy();
		CurrentHealingItem = nullptr;
	}
	// The weapons the hero had are in a crate left where it fell, for whoever comes by: one of each
	// kind, as new. What it carried besides (rounds, bandages, potions, materials) goes with it.
	ASenniteCrate::Leave(GetWorld(), this, GunsHeld());

	if (Killer != nullptr && Killer != this && Killer->GetController() != nullptr) {
		if (ASennitePlayerState* KillerState = Cast<ASennitePlayerState>(Killer->GetController()->PlayerState)) {
			KillerState->KillCount++;
		}
	}
	// in a match the hero returns on its faction's base, unless its faction is out
	if (ASenniteMatch* Match = ASenniteMatch::Find(GetWorld())) {
		if (Match->Fell(this, Killer)) {
			Destroy();
			return;
		}
	}
	// the player goes on watching from where they fell
	if (ASennitePlayerController* SennitePlayerController = Cast<ASennitePlayerController>(GetController())) {
		SennitePlayerController->BecomeSpectator();
	}
	Destroy();
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
	// The bullet is the one the server let out, from the gun it let it out of, whatever is in hand by
	// now: a shot let out of the hero's own weapon cannot leave as a sniper's round. It is taken once,
	// and is gone if it was not taken soon.
	const TSubclassOf<AProjectileActor> BulletClass = ShotBullet;
	AWeaponActor* const FiredFrom = ShotGun.Get();
	const bool bAuthorized = BulletClass != nullptr && GetWorld()->GetTimeSeconds() - ShotAt <= ShotKeptSeconds;
	ShotBullet = nullptr;
	ShotGun = nullptr;
	if (bAuthorized && FVector::Dist(SpawnTransform.GetLocation(), GetActorLocation()) <= MaxMuzzleDistance) {
		auto Bullet = Cast<AProjectileActor>(UGameplayStatics::BeginDeferredActorSpawnFromClass(this, BulletClass, SpawnTransform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn));
		if (Bullet != nullptr)
		{
			//spawnactor has no way of passing parameters so need to use begindeferredactorspawn and finishspawningactor
			Bullet->Weapon = FiredFrom;
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

// The view closes in and opens out in Tick, with AimedIn: what these two once set at a stroke.
void ASenniteCharacter::ClientCameraAimIn_Implementation() {
}

void ASenniteCharacter::ClientCameraAimOut_Implementation() {
}

void ASenniteCharacter::SeeThroughOwnEyes(bool bEyes)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (Body != nullptr && bEyes != bThroughOwnEyes) {
		bThroughOwnEyes = bEyes;
		Body->bCastHiddenShadow = bEyes;
		Body->SetOwnerNoSee(bEyes);
		if (bEyes) {
			SeenAnimTick = Body->VisibilityBasedAnimTickOption;
		}
		Body->VisibilityBasedAnimTickOption = bEyes ? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones : SeenAnimTick;
	}
	// the gun in the hand is its own actor, and may be put in the hand or changed while aiming: on this machine, the player's own
	if (CurrentWeapon != nullptr && CurrentWeapon->WeaponMesh != nullptr && CurrentWeapon->WeaponMesh->IsVisible() == bThroughOwnEyes) {
		CurrentWeapon->WeaponMesh->SetVisibility(!bThroughOwnEyes);
	}
}

void ASenniteCharacter::ClientGetBulletTransform_Implementation() {
	// The shot leaves the barrel of the held gun, or the hero's own weapon when it holds none, along
	// the line the camera looks down, when that line meets something.
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (PlayerController == nullptr) {
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
		ServerSpawnProjectile(FTransform(ShootDirection.Rotation(), CurrentWeapon != nullptr ? CurrentWeapon->GetMuzzleLocation() : OwnWeaponTip()));
	}
}

void ASenniteCharacter::KickGun() {
	// A shot shows as the gun's kick, on every machine that draws this hero: the heroes' own firing
	// animations are for their own weapons (a cast, a punch, a swing), not for a gun held in both hands.
	if (USenniteHeroMesh* Body = Cast<USenniteHeroMesh>(GetMesh())) {
		Body->Kick();
	}
}

void ASenniteCharacter::NetMulticastOwnAttack_Implementation() {
	if (OwnAttack != nullptr) {
		PlayAnimMontage(OwnAttack);
	}
	// a blow is heard as a swing, never as a shot
	USenniteSounds::PlayAt(this, bHeroMelee ? TEXT("swing") : TEXT("attack.own"), OwnWeaponTip());
}

void ASenniteCharacter::LandBlow() {
	if (!HasAuthority()) {
		return;
	}
	// ahead of the chest, the way the player looks, as far as the blow reaches: the nearest thing in the way takes it
	const FVector From = GetActorLocation() + FVector(0.0, 0.0, BaseEyeHeight * 0.5);
	const FVector Ahead = GetControlRotation().Vector();
	FCollisionQueryParams Skip(SCENE_QUERY_STAT(SenniteBlow), false, this);
	if (CurrentWeapon != nullptr) {
		Skip.AddIgnoredActor(CurrentWeapon);
	}
	FCollisionObjectQueryParams Kinds;
	Kinds.AddObjectTypesToQuery(ECC_Pawn);
	Kinds.AddObjectTypesToQuery(ECC_WorldStatic);
	Kinds.AddObjectTypesToQuery(ECC_WorldDynamic);
	Kinds.AddObjectTypesToQuery(ECC_PhysicsBody);
	TArray<FHitResult> InTheWay;
	GetWorld()->SweepMultiByObjectType(InTheWay, From, From + Ahead * MeleeReach, FQuat::Identity, Kinds, FCollisionShape::MakeSphere(MeleeAcross), Skip);
	InTheWay.Sort([](const FHitResult& A, const FHitResult& B) { return A.Distance < B.Distance; });
	for (const FHitResult& Each : InTheWay) {
		AActor* Other = Each.GetActor();
		if (Other == nullptr || Other->IsA(AProjectileActor::StaticClass()) || Other->IsA(AStormActor::StaticClass())) {
			continue;
		}
		// kind 0, the pickaxe's: it takes materials from what it harvests, as the own weapon's bolt does
		if (AProjectileActor::Land(Other, MeleeDamage, 0, this)) {
			USenniteSounds::PlayAt(this, TEXT("bolt.hit"), Each.ImpactPoint);
			return;
		}
	}
}

void ASenniteCharacter::WearChosenSkin() {
	const UGameInstance* Game = GetGameInstance();
	const USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
	if (Server == nullptr || !Server->IsOn() || HeroMesh.IsNull()) {
		return;
	}
	FSoftObjectPath Want = HeroMesh;
	if (Server->bMaterialsKnown && !Server->Wearing.IsEmpty() && Server->Owns(Server->Wearing)) {
		const ASenniteBaseEditor* Shared = GetDefault<ASenniteBaseEditor>();
		if (const FSenniteSkinMesh* Skin = Shared->Skins.FindByPredicate([Server](const FSenniteSkinMesh& S) { return S.Id == Server->Wearing; })) {
			if (Skin->Mesh == WornMesh) {
				return; // on already
			}
			const USkeletalMesh* SkinMesh = Cast<USkeletalMesh>(Skin->Mesh.TryLoad());
			const USkeletalMesh* Own = Cast<USkeletalMesh>(HeroMesh.TryLoad());
			if (SkinMesh != nullptr && Own != nullptr && SkinMesh->GetSkeleton() == Own->GetSkeleton()) {
				Want = Skin->Mesh;
			}
		}
	}
	if (Want == WornMesh) {
		return;
	}
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Want.TryLoad())) {
		WearMesh(Mesh);
	}
	else {
		UE_LOG(LogSenniteCharacter, Error, TEXT("Skin mesh '%s' did not load ([/Script/Sennite.SenniteBaseEditor] in DefaultGame.ini)"), *Want.ToString());
	}
	WornMesh = Want;
}

FVector ASenniteCharacter::OwnWeaponTip() const {
	const USkeletalMeshComponent* Body = GetMesh();
	for (const FName& Bone : { HeroCastBone, HeroWeaponBone }) {
		if (Body != nullptr && !Bone.IsNone() && Body->GetBoneIndex(Bone) != INDEX_NONE) {
			return Body->GetSocketLocation(Bone);
		}
	}
	// a body with neither bone: in front of its chest
	return GetActorLocation() + GetActorForwardVector() * 50.0 + FVector(0.0, 0.0, 30.0);
}

void ASenniteCharacter::NetMulticastPlayPickaxeSwingAnimation_Implementation() {
	PlayAnimMontage(PickaxeSwingingAnimation, 1.f, NAME_None);
	USenniteSounds::PlayAt(this, TEXT("swing"), GetActorLocation());
}

void ASenniteCharacter::NetMulticastGunFired_Implementation() {
	KickGun();
	// and is heard, where the gun is
	if (CurrentWeapon != nullptr && GetNetMode() != NM_DedicatedServer) {
		if (USoundBase* Shot = Cast<USoundBase>(GetDefault<AWeaponActor>(CurrentWeapon->GetClass())->FireSound.TryLoad())) {
			USoundAttenuation* Fades = Cast<USoundAttenuation>(GetDefault<AWeaponActor>()->FireFalloff.TryLoad());
			UGameplayStatics::PlaySoundAtLocation(this, Shot, CurrentWeapon->GetMuzzleLocation(), 1.0f, 1.0f, 0.0f, Fades);
		}
	}
}

void ASenniteCharacter::NetMulticastPlayUseHealingItemAnimation_Implementation() {
	PlayAnimMontage(HealingAnimation);
	USenniteSounds::PlayAt(this, CurrentHealingItemType == 1 ? TEXT("heal.potion") : TEXT("heal.bandage"), GetActorLocation());
}

void ASenniteCharacter::NetMulticastPlayReloadRifleAnimation_Implementation() {
	PlayAnimMontage(RifleHipReloadAnimation);
	USenniteSounds::PlayAt(this, TEXT("reload.rifle"), GetActorLocation());
}

void ASenniteCharacter::NetMulticastPlayReloadRifleIronsightsAnimation_Implementation() {
	PlayAnimMontage(RifleIronsightsReloadAnimation);
	USenniteSounds::PlayAt(this, TEXT("reload.rifle"), GetActorLocation());
}

void ASenniteCharacter::NetMulticastPlayReloadShotgunAnimation_Implementation() {
	PlayAnimMontage(ShotgunHipReloadAnimation);
	USenniteSounds::PlayAt(this, TEXT("reload.shotgun"), GetActorLocation());
}

void ASenniteCharacter::NetMulticastPlayReloadShotgunIronsightsAnimation_Implementation() {
	PlayAnimMontage(ShotgunIronsightsReloadAnimation);
	USenniteSounds::PlayAt(this, TEXT("reload.shotgun"), GetActorLocation());
}

void ASenniteCharacter::ClientDrawHitMarker_Implementation() {
	USenniteSounds::Play(this, TEXT("hit.marker"));
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
void ASenniteCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	// a hero that goes mid-edit leaves the piece as the server has it, and takes its preview with it
	CancelEdit();
	if (BuildingPreview != nullptr) {
		BuildingPreview->Destroy();
		BuildingPreview = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

ABuildingActor* ASenniteCharacter::EditablePieceUnderAim(int32& OutTile) const {
	OutTile = INDEX_NONE;
	const AController* Own = GetController();
	if (Own == nullptr) {
		return nullptr;
	}
	FVector Eye;
	FRotator Look;
	Own->GetPlayerViewPoint(Eye, Look);
	// the nearest piece of the player's side whose face the view crosses, a cut tile's place included
	double Nearest = MaxBuildReach + FVector::Dist(Eye, GetActorLocation()) + ABuildingActor::Cell;
	ABuildingActor* Found = nullptr;
	for (TActorIterator<ABuildingActor> It(GetWorld()); It; ++It) {
		ABuildingActor* Piece = *It;
		if (Piece->IsPreview || Piece->IsActorBeingDestroyed() || ClientSpawnedStructures.Contains(Piece) || !Piece->MayBeEditedBy(GetPlayerState())
			|| FVector::Dist(Piece->GetActorLocation(), GetActorLocation()) > MaxBuildReach + ABuildingActor::Cell) {
			continue;
		}
		double At = 0.0;
		const int32 Tile = Piece->TileUnderAim(Eye, Look.Vector(), Nearest, At);
		if (Tile != INDEX_NONE && At < Nearest) {
			Nearest = At;
			Found = Piece;
			OutTile = Tile;
		}
	}
	return Found;
}

ABuildingActor* ASenniteCharacter::PieceInSight() const {
	const AController* Own = GetController();
	if (Own == nullptr) {
		return nullptr;
	}
	FVector Eye;
	FRotator Look;
	Own->GetPlayerViewPoint(Eye, Look);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PieceInSight), false, this);
	Params.AddIgnoredActor(CurrentWeapon);
	Params.AddIgnoredActor(CurrentHealingItem);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Look.Vector() * PieceSightReach, ECC_Visibility, Params)) {
		return nullptr;
	}
	ABuildingActor* Piece = Cast<ABuildingActor>(Hit.GetActor());
	return Piece != nullptr && !Piece->IsPreview && !Piece->IsActorBeingDestroyed() ? Piece : nullptr;
}

void ASenniteCharacter::EditPiece() {
	if (EditingPiece.IsValid()) {
		FinishEdit();
		return;
	}
	int32 Tile = INDEX_NONE;
	ABuildingActor* Piece = EditablePieceUnderAim(Tile);
	if (Piece == nullptr) {
		return;
	}
	StopShooting();
	EditingPiece = Piece;
	EditCut = Piece->Cut;
	EditDragLast = INDEX_NONE;
	Piece->EditingHere = this;
	Piece->ShowCut(EditCut, EditCut);
	USenniteSounds::Play(this, TEXT("piece.edit"));
}

void ASenniteCharacter::MarkEditTile(int32 Tile, bool bCut) {
	ABuildingActor* Piece = EditingPiece.Get();
	if (Piece == nullptr || Tile < 0 || Tile >= Piece->TileCount()) {
		return;
	}
	const int32 Next = bCut ? (EditCut | (1 << Tile)) : (EditCut & ~(1 << Tile));
	// at least one tile stays
	if (Next == Piece->AllTiles()) {
		return;
	}
	if (Next != EditCut) {
		// a tile taken out is heard as a piece taken away, one put back as the edit
		USenniteSounds::Play(this, bCut ? TEXT("piece.remove") : TEXT("piece.edit"));
	}
	EditCut = Next;
	Piece->ShowCut(EditCut, EditCut);
}

void ASenniteCharacter::FinishEdit() {
	ABuildingActor* Piece = EditingPiece.Get();
	EditingPiece = nullptr;
	bEditDragging = false;
	if (Piece == nullptr) {
		return;
	}
	Piece->EditingHere = nullptr;
	// shown at once as it will be; the server makes it so for everyone, or refuses and it goes back
	Piece->ShowCut(EditCut, 0);
	if (EditCut != Piece->Cut) {
		USenniteSounds::PlayAt(this, TEXT("piece.place"), Piece->GetActorLocation());
		ServerEditPiece(Piece, EditCut);
	}
}

void ASenniteCharacter::CancelEdit() {
	ABuildingActor* Piece = EditingPiece.Get();
	EditingPiece = nullptr;
	bEditDragging = false;
	if (Piece != nullptr) {
		Piece->EditingHere = nullptr;
		Piece->ShowCut(Piece->Cut, 0);
	}
}

bool ASenniteCharacter::MayEditPiece(const ABuildingActor* Piece, int32 NewCut) const {
	if (!IsValid(Piece) || Piece->IsPreview || Piece->IsActorBeingDestroyed() || Health <= 0.0f) {
		return false;
	}
	// a piece of the player's own side, within reach, keeping at least one tile
	if (!Piece->MayBeEditedBy(GetPlayerState()) || FVector::Dist(Piece->GetActorLocation(), GetActorLocation()) > MaxBuildReach + ABuildingActor::Cell) {
		return false;
	}
	const int32 All = Piece->AllTiles();
	if ((NewCut & ~All) != 0 || NewCut == All) {
		return false;
	}
	// and no tile put back into anyone standing where it goes
	const int32 Back = Piece->Cut & ~NewCut;
	const FTransform Frame = Piece->GetActorTransform();
	for (int32 Tile = 0; Tile < Piece->TileCount(); ++Tile) {
		if ((Back & (1 << Tile)) == 0) {
			continue;
		}
		const FTransform At = Piece->TileTransform(Tile, false) * Frame;
		if (GetWorld()->OverlapAnyTestByObjectType(At.GetLocation(), At.GetRotation(), FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeBox(At.GetScale3D() * 50.0))) {
			return false;
		}
	}
	return true;
}

void ASenniteCharacter::RemoveEditedPiece() {
	ABuildingActor* Piece = EditingPiece.Get();
	// the edit ends with nothing cut: the piece goes whole, or the server refuses and it stands as it was
	CancelEdit();
	if (Piece != nullptr) {
		ServerRemovePiece(Piece);
	}
}

bool ASenniteCharacter::MayRemovePiece(const ABuildingActor* Piece) const {
	// a standing piece of the player's own side, within reach: what the player may edit, the player may take away
	return IsValid(Piece) && !Piece->IsPreview && !Piece->IsActorBeingDestroyed() && !Piece->bTakenAway && Health > 0.0f
		&& Piece->MayBeEditedBy(GetPlayerState())
		&& FVector::Dist(Piece->GetActorLocation(), GetActorLocation()) <= MaxBuildReach + ABuildingActor::Cell;
}

void ASenniteCharacter::ServerRemovePiece_Implementation(ABuildingActor* Piece) {
	if (!MayRemovePiece(Piece)) {
		if (IsValid(Piece)) {
			ClientEditRefused(Piece);
		}
		return;
	}
	// What the piece cost goes back to whoever built it, in its material; a piece nobody paid for gives
	// nothing back.
	if (ASennitePlayerState* Paid = Cast<ASennitePlayerState>(Piece->Builder)) {
		if (Paid->MaterialCounts.IsValidIndex(Piece->MaterialType)) {
			Paid->MaterialCounts[Piece->MaterialType] += BuildingPieceCost;
		}
	}
	// Every machine is told first, so none sees it break; then it ends, and what it alone held up falls
	// a moment later, as after any piece (ABuildingActor::EndPlay).
	NetMulticastPieceTakenAway(Piece);
	Piece->SetLifeSpan(PieceTakenAwaySeconds);
}

bool ASenniteCharacter::ServerRemovePiece_Validate(ABuildingActor* Piece) {
	return true;
}

void ASenniteCharacter::NetMulticastPieceTakenAway_Implementation(ABuildingActor* Piece) {
	if (!IsValid(Piece)) {
		return;
	}
	Piece->bTakenAway = true;
	Piece->EditingHere = nullptr;
	Piece->SetActorHiddenInGame(true);
	Piece->SetActorEnableCollision(false);
	// the player who took it hears it wherever the piece is; anyone else from where it stood
	if (IsLocallyControlled()) {
		USenniteSounds::Play(this, TEXT("piece.remove"));
	}
	else {
		USenniteSounds::PlayAt(this, TEXT("piece.remove"), Piece->GetActorLocation());
	}
}

void ASenniteCharacter::ServerEditPiece_Implementation(ABuildingActor* Piece, int32 NewCut) {
	if (!MayEditPiece(Piece, NewCut)) {
		if (IsValid(Piece)) {
			ClientEditRefused(Piece);
		}
		return;
	}
	Piece->Cut = NewCut;
	if (!Piece->EditingHere.IsValid()) {
		Piece->ShowCut(NewCut, 0);
	}
}

bool ASenniteCharacter::ServerEditPiece_Validate(ABuildingActor* Piece, int32 NewCut) {
	return true;
}

void ASenniteCharacter::ClientEditRefused_Implementation(ABuildingActor* Piece) {
	USenniteSounds::Play(this, TEXT("ui.denied"));
	if (IsValid(Piece) && !Piece->EditingHere.IsValid()) {
		Piece->ShowCut(Piece->Cut, 0);
	}
}

void ASenniteCharacter::ClientDrawPieceHit_Implementation(float Damage) {
	const APlayerController* Own = Cast<APlayerController>(GetController());
	if (ASenniteHUD* Hud = Own != nullptr ? Cast<ASenniteHUD>(Own->GetHUD()) : nullptr) {
		Hud->DrawPieceHit(Damage);
	}
}
