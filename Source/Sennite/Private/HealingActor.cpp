// Fill out your copyright notice in the Description page of Project Settings.

#include "HealingActor.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

// Sets default values
AHealingActor::AHealingActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	// An item is in a hero's hand or not in the world at all (packs come out of crates): it overlaps
	// everything and blocks nothing, and neither aim traces nor the camera see it.
	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Item"));
	RootComponent = ItemMesh;
	ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ItemMesh->SetCollisionObjectType(ECC_WorldDynamic);
	ItemMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
	ItemMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	ItemMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	ItemMesh->SetGenerateOverlapEvents(true);
	ItemMesh->SetCanEverAffectNavigation(false);

}

// Called when the game starts or when spawned
void AHealingActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AHealingActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AHealingActor::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHealingActor, Holder);
}

void AHealingActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	if (MeshAsset.IsNull()) {
		return;
	}
	if (UStaticMesh* Mesh = Cast<UStaticMesh>(MeshAsset.TryLoad())) {
		ItemMesh->SetStaticMesh(Mesh);
		SetActorScale3D(MeshScale);
	}
	else {
		UE_LOG(LogTemp, Error, TEXT("Healing item mesh '%s' did not load for %s"), *MeshAsset.ToString(), *GetClass()->GetName());
	}
	if (UMaterialInterface* Material = Cast<UMaterialInterface>(MeshMaterial.TryLoad())) {
		if (UMaterialInstanceDynamic* Coloured = ItemMesh->CreateDynamicMaterialInstance(0, Material)) {
			Coloured->SetVectorParameterValue(TEXT("Color"), MeshColour);
		}
	}
}
