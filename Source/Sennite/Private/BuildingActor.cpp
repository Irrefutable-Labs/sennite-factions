// Fill out your copyright notice in the Description page of Project Settings.

#include "BuildingActor.h"
#include "Net/UnrealNetwork.h"
#include "Engine/Engine.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

// Sets default values
ABuildingActor::ABuildingActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	PieceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Piece"));
	PieceMesh->SetupAttachment(RootComponent);
	PieceMesh->SetCanEverAffectNavigation(false);
}

void ABuildingActor::SetPiece(const TCHAR* Kind, int32 Material, bool bPreview)
{
	StructureType = Kind;
	MaterialType = Material;
	IsPreview = bPreview;

	// The actor sits at the middle of the bottom of its build cell (a wall: of the cell's edge).
	// The shape is a 100 cm cube, scaled to a slab.
	const float Wide = Cell / 100.0f;
	const float Thin = Thickness / 100.0f;
	if (StructureType == TEXT("Wall")) {
		// upright, across the actor's X axis
		PieceMesh->SetRelativeScale3D(FVector(Wide, Thin, Wide));
		PieceMesh->SetRelativeLocation(FVector(0.0f, 0.0f, Cell * 0.5f));
	}
	else if (StructureType == TEXT("Ramp")) {
		// from the bottom of the near edge to the top of the far edge, rising along the actor's X axis
		PieceMesh->SetRelativeScale3D(FVector(Wide * UE_SQRT_2, Wide, Thin));
		PieceMesh->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, Cell * 0.5f), FRotator(45.0f, 0.0f, 0.0f));
	}
	else {
		// flat, its top at the level it is built on
		PieceMesh->SetRelativeScale3D(FVector(Wide, Wide, Thin));
		PieceMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -Thickness * 0.5f));
	}

	if (IsPreview) {
		// a preview is seen and nothing else: nothing stands on it, aim and the camera pass through it
		PieceMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		PieceMesh->SetCollisionObjectType(ECC_WorldDynamic);
		PieceMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
		PieceMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		PieceMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		PieceMesh->SetCastShadow(false);
	}
	else {
		// a built piece is solid; overlap events let bullets and the build checks find it
		PieceMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	}
	PieceMesh->SetGenerateOverlapEvents(true);
}

void ABuildingActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();

	// what pieces are made of is one list, held by the base class
	const ABuildingActor* Shared = GetDefault<ABuildingActor>();
	if (UStaticMesh* Shape = Cast<UStaticMesh>(Shared->PieceShape.TryLoad())) {
		PieceMesh->SetStaticMesh(Shape);
	}
	else {
		UE_LOG(LogTemp, Error, TEXT("Building piece shape '%s' did not load"), *Shared->PieceShape.ToString());
	}
	const FSoftObjectPath& Face = IsPreview ? Shared->PreviewMaterial
		: StructureType == TEXT("Floor") ? Shared->FloorMaterial
		: Shared->TierMaterials.IsValidIndex(MaterialType) ? Shared->TierMaterials[MaterialType] : Shared->FloorMaterial;
	if (UMaterialInterface* Material = Cast<UMaterialInterface>(Face.TryLoad())) {
		PieceMesh->SetMaterial(0, Material);
	}
	else {
		UE_LOG(LogTemp, Error, TEXT("Building piece material '%s' did not load for %s"), *Face.ToString(), *GetClass()->GetName());
	}
	if (Shared->TierHealth.IsValidIndex(MaterialType)) {
		Health = Shared->TierHealth[MaterialType];
	}
}

// Called when the game starts or when spawned
void ABuildingActor::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void ABuildingActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ABuildingActor::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABuildingActor, Health);
}
