#include "SenniteStructure.h"

#include "BuildingActor.h"
#include "SenniteMatchBases.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

ASenniteStructure::ASenniteStructure()
{
	PrimaryActorTick.bCanEverTick = false;
	// every machine builds a base from what the publisher's server says: nothing of it is replicated
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASenniteStructure::AddPanel(const FVector& Centre, const FVector& Size, UMaterialInterface* Face)
{
	const ABuildingActor* Pieces = GetDefault<ABuildingActor>();
	// In a match every machine builds the same structure, and a player may stand on it: its panels
	// have the same names on every machine, so the server can say which one a hero stands on.
	UStaticMeshComponent* Panel = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Panel_%d_%d"), Builds, Panels.Num()));
	Panel->SetNetAddressable();
	Panel->SetupAttachment(RootComponent);
	Panel->SetCanEverAffectNavigation(false);
	if (UStaticMesh* Cube = Cast<UStaticMesh>(Pieces->PieceShape.TryLoad())) {
		Panel->SetStaticMesh(Cube);
	}
	// the shape is a 100 cm cube
	Panel->SetRelativeScale3D(Size / 100.0);
	Panel->SetRelativeLocation(Centre);
	if (Face != nullptr) {
		Panel->SetMaterial(0, Face);
	}
	if (bStanding) {
		Panel->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		// a bullet learns what it landed on from the overlap, as it does with a building piece
		Panel->SetGenerateOverlapEvents(true);
	}
	else {
		// a plan is seen and nothing else
		Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Panel->SetCastShadow(false);
	}
	Panel->RegisterComponent();
	Panels.Add(Panel);
}

void ASenniteStructure::TakeHit(float Damage, const ASenniteCharacter* Shooter)
{
	if (ASenniteMatchBases* In = Match.Get()) {
		In->Hit(this, Damage, Shooter);
	}
}

void ASenniteStructure::Build(const FString& InPrefabId, int32 Width, int32 Depth, bool bInStanding)
{
	for (UStaticMeshComponent* Panel : Panels) {
		if (Panel != nullptr) {
			Panel->DestroyComponent();
		}
	}
	Panels.Reset();
	++Builds;
	PrefabId = InPrefabId;
	bStanding = bInStanding;

	const ABuildingActor* Pieces = GetDefault<ABuildingActor>();
	const ASenniteStructure* Shared = GetDefault<ASenniteStructure>();
	const FSenniteShape* Found = Shared->Shapes.FindByPredicate([&InPrefabId](const FSenniteShape& S) { return S.Prefab == InPrefabId; });
	FSenniteShape Shape;
	if (Found != nullptr) {
		Shape = *Found;
	}
	else {
		// a prefab the game has no drawing for still shows where it stands and what it covers
		UE_LOG(LogTemp, Error, TEXT("No shape is set for prefab '%s' ([/Script/Sennite.SenniteStructure] in DefaultGame.ini)"), *InPrefabId);
		Shape.Kind = TEXT("Enclosure");
		Shape.bRoof = true;
	}

	UMaterialInterface* Plan = Cast<UMaterialInterface>(Pieces->PreviewMaterial.TryLoad());
	UMaterialInterface* Wall = bStanding && Pieces->TierMaterials.IsValidIndex(Shape.Tier)
		? Cast<UMaterialInterface>(Pieces->TierMaterials[Shape.Tier].TryLoad()) : Plan;
	UMaterialInterface* Floor = bStanding ? Cast<UMaterialInterface>(Pieces->FloorMaterial.TryLoad()) : Plan;

	const double Cell = ABuildingActor::Cell;
	const double Thick = ABuildingActor::Thickness;
	const double W = Width;
	const double D = Depth;
	if (Shape.Kind == TEXT("Slab")) {
		AddPanel(FVector(0.0, 0.0, Cell * 0.5), FVector(W, Thick, Cell), Wall);
		return;
	}
	const int32 Storeys = FMath::Max(1, Shape.Storeys);
	for (int32 Storey = 0; Storey < Storeys; ++Storey) {
		const double Z = Storey * Cell + Cell * 0.5;
		AddPanel(FVector(0.0, -(D - Thick) * 0.5, Z), FVector(W, Thick, Cell), Wall);
		AddPanel(FVector(0.0, (D - Thick) * 0.5, Z), FVector(W, Thick, Cell), Wall);
		AddPanel(FVector(-(W - Thick) * 0.5, 0.0, Z), FVector(Thick, D, Cell), Wall);
		if (!(Shape.bOpenFront && Storey == 0)) {
			AddPanel(FVector((W - Thick) * 0.5, 0.0, Z), FVector(Thick, D, Cell), Wall);
		}
		if (Storey > 0) {
			AddPanel(FVector(0.0, 0.0, Storey * Cell - Thick * 0.5), FVector(W, D, Thick), Floor);
		}
	}
	const double Top = Storeys * Cell;
	if (Shape.bRoof) {
		AddPanel(FVector(0.0, 0.0, Top - Thick * 0.5), FVector(W, D, Thick), Floor);
	}
	if (Shape.bParapet) {
		const double High = Cell * 0.25;
		const double Z = Top + High * 0.5;
		AddPanel(FVector(0.0, -(D - Thick) * 0.5, Z), FVector(W, Thick, High), Wall);
		AddPanel(FVector(0.0, (D - Thick) * 0.5, Z), FVector(W, Thick, High), Wall);
		AddPanel(FVector(-(W - Thick) * 0.5, 0.0, Z), FVector(Thick, D, High), Wall);
		AddPanel(FVector((W - Thick) * 0.5, 0.0, Z), FVector(Thick, D, High), Wall);
	}
}
