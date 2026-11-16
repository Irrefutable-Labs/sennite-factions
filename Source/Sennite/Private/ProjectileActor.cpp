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

// Sets default values
AProjectileActor::AProjectileActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	/*MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;*/

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
}

// Called when the game starts or when spawned
void AProjectileActor::BeginPlay()
{
	Super::BeginPlay();
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
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "projectile tick tick");
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
	ASenniteCharacter* Shooter = Cast<ASenniteCharacter>(WeaponHolder);
	ASenniteCharacter* Victim = Cast<ASenniteCharacter>(OtherActor);
	if (AWeaponActor* HitWeapon = Cast<AWeaponActor>(OtherActor)) {
		// a weapon on the ground lets the bullet by; one in a player's hands is a hit on that player
		if (HitWeapon->Holder == nullptr) {
			return;
		}
		Victim = HitWeapon->Holder;
	}
	else if (AHealingActor* HitItem = Cast<AHealingActor>(OtherActor)) {
		if (HitItem->Holder == nullptr) {
			return;
		}
		Victim = HitItem->Holder;
	}
	if (Victim != nullptr) {
		if (Victim == Shooter) {
			return;
		}
		bSpent = true;
		Victim->TakeHit(Damage, Shooter);
		Destroy();
		return;
	}
	if (ABuildingActor* Piece = Cast<ABuildingActor>(OtherActor)) {
		// a preview is not there to be hit
		if (Piece->IsPreview) {
			return;
		}
		bSpent = true;
		// the piece stops the bullet whether the shot counts on it or not (ABuildingActor::TakeHit)
		Piece->TakeHit(Damage, Shooter);
		Destroy();
		return;
	}
	bSpent = true;
	if (ASenniteStructure* Structure = Cast<ASenniteStructure>(OtherActor)) {
		// a faction's built structure: in a match it comes down when it has taken enough
		Structure->TakeHit(Damage, Shooter);
	}
	if (AMaterialActor* Node = Cast<AMaterialActor>(OtherActor)) {
		Node->Health -= Damage;
		if (ProjectileType == 0 && Shooter != nullptr && Shooter->GetController() != nullptr) {
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
	Destroy();
}

void AProjectileActor::SelfDestruct() {
	Destroy();
}