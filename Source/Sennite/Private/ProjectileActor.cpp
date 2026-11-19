// Fill out your copyright notice in the Description page of Project Settings.

#include "ProjectileActor.h"
#include "WeaponActor.h"
#include "SenniteCharacter.h"
#include "BuildingActor.h"
#include "HealingActor.h"
#include "MaterialActor.h"
#include "SennitePlayerState.h"
#include "SenniteHUD.h"
#include "SenniteMatch.h"
#include "SenniteStructure.h"
#include "StormActor.h"
#include "SennitePlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

// Sets default values
AProjectileActor::AProjectileActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// The server flies the bullet; every machine is sent it and flies its own copy from where it
	// started, so the bolt is seen leaving the gun and travelling.
	bReplicates = true;
	SetReplicatingMovement(true);

	CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComp"));
	CollisionComp->InitSphereRadius(5.0f);
	CollisionComp->SetCollisionProfileName("Projectile");
	CollisionComp->bEditableWhenInherited = true;
	RootComponent = CollisionComp;

	ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovementComponent"));
	ProjectileMovementComponent->UpdatedComponent = CollisionComp;
	ProjectileMovementComponent->ProjectileGravityScale = 0.0;
	/*ProjectileMovementComponent->InitialSpeed = ProjectileSpeed;
	ProjectileMovementComponent->MaxSpeed = ProjectileSpeed;*/
	ProjectileMovementComponent->bRotationFollowsVelocity = true;

	Bolt = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bolt"));
	Bolt->SetupAttachment(CollisionComp);
	Bolt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bolt->SetGenerateOverlapEvents(false);
	Bolt->SetCastShadow(false);
	Bolt->SetCanEverAffectNavigation(false);
}

void AProjectileActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	// a bullet seen as one of the ammo models: the model as it is, its middle on the bullet, its point the way it flies
	if (!Round.IsNull()) {
		if (UStaticMesh* Model = Cast<UStaticMesh>(Round.TryLoad())) {
			Bolt->SetStaticMesh(Model);
			Bolt->SetRelativeLocation(FVector::ZeroVector);
			Bolt->SetRelativeScale3D(FVector::OneVector);
			bRound = true;
			return;
		}
		UE_LOG(LogTemp, Error, TEXT("Round '%s' did not load: the bullet is seen as a bolt"), *Round.ToString());
	}
	// what a bolt is made of is one setting, held by the base class
	const AProjectileActor* Shared = GetDefault<AProjectileActor>();
	if (UStaticMesh* Shape = Cast<UStaticMesh>(Shared->BoltShape.TryLoad())) {
		Bolt->SetStaticMesh(Shape);
	}
	else {
		UE_LOG(LogTemp, Error, TEXT("Bolt shape '%s' did not load: bullets cannot be seen"), *Shared->BoltShape.ToString());
	}
	if (UMaterialInterface* Face = Cast<UMaterialInterface>(Shared->BoltMaterial.TryLoad())) {
		if (UMaterialInstanceDynamic* Coloured = Bolt->CreateDynamicMaterialInstance(0, Face)) {
			Coloured->SetVectorParameterValue(TEXT("Color"), BoltColour);
		}
	}
	else {
		UE_LOG(LogTemp, Error, TEXT("Bolt material '%s' did not load"), *Shared->BoltMaterial.ToString());
	}
	LayBolt();
}

void AProjectileActor::LayBolt()
{
	if (bRound || Beam != nullptr) {
		return;
	}
	// a streak behind the bullet, no longer than the bullet has flown: it never reaches back through the gun
	const float Length = FMath::Clamp(Flown, BoltWidth, BoltLength);
	Bolt->SetRelativeLocation(FVector(-Length * 0.5f, 0.0f, 0.0f));
	Bolt->SetRelativeScale3D(FVector(Length, BoltWidth, BoltWidth) / 100.0f);
}

// Called when the game starts or when spawned
void AProjectileActor::BeginPlay()
{
	Super::BeginPlay();
	// a laser: carried along with the bullet, and flashing where it leaves, on every machine that draws it
	if (!Laser.IsNull() && GetNetMode() != NM_DedicatedServer) {
		if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Laser.TryLoad())) {
			Beam = UNiagaraFunctionLibrary::SpawnSystemAttached(System, CollisionComp, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, true, false, ENCPoolMethod::None, true);
			if (Beam != nullptr) {
				Bolt->SetVisibility(false);
				LayLaser();
				Beam->Activate(true);
			}
		}
		else {
			UE_LOG(LogTemp, Error, TEXT("Laser '%s' did not load: the bullet is seen as a bolt"), *Laser.ToString());
		}
		PlayFor(this, LaserFlash, GetActorLocation(), GetActorRotation(), LaserFlashSeconds);
	}
	if (HasAuthority()) {
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "projectile beginplay");
		CollisionComp->OnComponentBeginOverlap.AddDynamic(this, &AProjectileActor::OnOverlapBegin);	// set up a notification for when this component hits something blocking
		FTimerHandle LifeTimerHandle;
		GetWorldTimerManager().SetTimer(LifeTimerHandle, this, &AProjectileActor::SelfDestruct, Lifespan, false);
	}
}

// Called every frame
void AProjectileActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Flown += ProjectileMovementComponent->Velocity.Size() * DeltaTime;
	LayBolt();
	LayLaser();
}

void AProjectileActor::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) {
	// A bullet lands once. A player is several bodies (capsule, trigger, mesh, what they hold), and
	// each one the bullet touches calls this.
	if (!HasAuthority() || OtherActor == nullptr || bSpent) {
		return;
	}
	// the bullet passes its own weapon, the player who fired it, other bullets and the storm's wall
	if ((Weapon && OtherActor == (AActor*)Weapon) || (WeaponHolder && OtherActor == (AActor*)WeaponHolder) || OtherActor == this
		|| OtherActor->IsA(AProjectileActor::StaticClass()) || OtherActor->IsA(AStormActor::StaticClass())) {
		return;
	}
	if (Land(OtherActor, Damage, ProjectileType, Cast<ASenniteCharacter>(WeaponHolder))) {
		bSpent = true;
		Destroy();
	}
}

bool AProjectileActor::Land(AActor* OtherActor, float Damage, int32 Kind, ASenniteCharacter* Shooter) {
	ASenniteCharacter* Victim = Cast<ASenniteCharacter>(OtherActor);
	if (AWeaponActor* HitWeapon = Cast<AWeaponActor>(OtherActor)) {
		// a weapon on the ground lets the bullet by; one in a player's hands is a hit on that player
		if (HitWeapon->Holder == nullptr) {
			return false;
		}
		Victim = HitWeapon->Holder;
	}
	else if (AHealingActor* HitItem = Cast<AHealingActor>(OtherActor)) {
		if (HitItem->Holder == nullptr) {
			return false;
		}
		Victim = HitItem->Holder;
	}
	if (Victim != nullptr) {
		if (Victim == Shooter) {
			return false;
		}
		Victim->TakeHit(Damage, Shooter);
		return true;
	}
	if (ABuildingActor* Piece = Cast<ABuildingActor>(OtherActor)) {
		// a preview is not there to be hit
		if (Piece->IsPreview) {
			return false;
		}
		// the piece stops the bullet whether the shot counts on it or not (ABuildingActor::TakeHit)
		Piece->TakeHit(Damage, Shooter);
		return true;
	}
	if (ASenniteStructure* Structure = Cast<ASenniteStructure>(OtherActor)) {
		// a faction's built structure: in a match it comes down when it has taken enough
		Structure->TakeHit(Damage, Shooter);
	}
	if (AMaterialActor* Node = Cast<AMaterialActor>(OtherActor)) {
		Node->Health -= Damage;
		if (Kind == 0 && Shooter != nullptr && Shooter->GetController() != nullptr) {
			// the pickaxe harvests: its holder gains the node's material
			ASennitePlayerState* State = Cast<ASennitePlayerState>(Shooter->GetController()->PlayerState);
			if (State != nullptr && State->MaterialCounts.IsValidIndex(Node->MaterialType)) {
				State->MaterialCounts[Node->MaterialType] += Node->MaterialCount;
			}
		}
		if (Node->Health <= 0) {
			Node->Destroy();
		}
	}
	// anything else stops the bullet
	return true;
}

void AProjectileActor::Preload(TArray<TObjectPtr<UObject>>& Into)
{
	for (TObjectIterator<UClass> It; It; ++It) {
		if (!It->IsChildOf(AProjectileActor::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract)) {
			continue;
		}
		const AProjectileActor* Made = GetDefault<AProjectileActor>(*It);
		for (const FSoftObjectPath* Each : { &Made->Round, &Made->Laser, &Made->LaserFlash, &Made->LaserHit }) {
			if (!Each->IsNull()) {
				if (UObject* Loaded = Each->TryLoad()) {
					Into.AddUnique(Loaded);
				}
			}
		}
	}
}

void AProjectileActor::LayLaser()
{
	if (Beam != nullptr) {
		Beam->SetVariablePosition(TEXT("LaserEnd"), GetActorLocation() - GetActorForwardVector() * FMath::Clamp(Flown, 1.0f, LaserTrail));
	}
}

void AProjectileActor::PlayFor(UObject* WorldContext, const FSoftObjectPath& Effect, const FVector& Where, const FRotator& Facing, float Seconds)
{
	UWorld* World = WorldContext != nullptr ? WorldContext->GetWorld() : nullptr;
	UNiagaraSystem* System = Effect.IsNull() || World == nullptr ? nullptr : Cast<UNiagaraSystem>(Effect.TryLoad());
	if (System == nullptr) {
		return;
	}
	UNiagaraComponent* Playing = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Where, Facing);
	if (Playing == nullptr) {
		return;
	}
	// stopped, what it has let out fades as it would, and then it is gone
	FTimerHandle Stop;
	World->GetTimerManager().SetTimer(Stop, FTimerDelegate::CreateWeakLambda(Playing, [Playing]() { Playing->Deactivate(); }), FMath::Max(0.01f, Seconds), false);
}

void AProjectileActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// a laser that landed on something before its time ran out is seen hitting it there
	if (EndPlayReason == EEndPlayReason::Destroyed && Beam != nullptr && GetWorld() != nullptr && GetWorld()->GetTimeSeconds() - CreationTime < Lifespan - 0.05f) {
		PlayFor(this, LaserHit, GetActorLocation(), GetActorRotation(), LaserHitSeconds);
	}
	Super::EndPlay(EndPlayReason);
}

void AProjectileActor::SelfDestruct() {
	Destroy();
}