// Fill out your copyright notice in the Description page of Project Settings.

#include "WeaponActor.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

// Sets default values
AWeaponActor::AWeaponActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	// A weapon is in a hero's hand or not in the world at all (weapons come out of crates): it
	// overlaps everything and blocks nothing, and neither aim traces nor the camera see it.
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Weapon"));
	RootComponent = WeaponMesh;
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WeaponMesh->SetCollisionObjectType(ECC_WorldDynamic);
	WeaponMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
	WeaponMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	WeaponMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	WeaponMesh->SetGenerateOverlapEvents(true);
	WeaponMesh->SetCanEverAffectNavigation(false);
}

void AWeaponActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	if (MeshAsset.IsNull()) {
		return;
	}
	if (UStaticMesh* Mesh = Cast<UStaticMesh>(MeshAsset.TryLoad())) {
		WeaponMesh->SetStaticMesh(Mesh);
	}
	else {
		UE_LOG(LogTemp, Error, TEXT("Weapon mesh '%s' did not load for %s"), *MeshAsset.ToString(), *GetClass()->GetName());
	}
}

FVector AWeaponActor::GetMuzzleLocation() const
{
	return WeaponMesh->GetComponentTransform().TransformPosition(MuzzleOffset);
}

// Called when the game starts or when spawned
void AWeaponActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AWeaponActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AWeaponActor::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWeaponActor, Holder);
	DOREPLIFETIME(AWeaponActor, CurrentBulletCount);
}