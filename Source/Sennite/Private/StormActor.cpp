#include "StormActor.h"
#include "SenniteCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// the engine's cylinder is 50 across the radius and 100 high
	constexpr float ShapeRadius = 50.0f;
	constexpr float ShapeHeight = 100.0f;
	// the wall stands well above anything a player can build or climb
	constexpr float WallHeight = 20000.0f;
}

AStormActor::AStormActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);
	PrimaryActorTick.bCanEverTick = true;
	Damage = 1;
	IsShrinking = false;
	SizeScale = FVector::OneVector;
	Stage = 0; // stages 0, 1, 2, 3: the circle holds; stages 4, 5, 6: it shrinks, and then the count starts again

	// The wall is only seen. Who is inside the circle is a matter of distance from its centre
	// (ASenniteCharacter::Tick), so the wall touches nothing.
	Wall = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Wall"));
	RootComponent = Wall;
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Wall->SetGenerateOverlapEvents(false);
	Wall->SetCastShadow(false);
	Wall->SetCanEverAffectNavigation(false);
}

void AStormActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	if (UStaticMesh* Shape = Cast<UStaticMesh>(WallShape.TryLoad())) {
		Wall->SetStaticMesh(Shape);
	}
	else {
		UE_LOG(LogTemp, Error, TEXT("Storm wall shape '%s' did not load; the storm cannot be seen"), *WallShape.ToString());
	}
	if (UMaterialInterface* Material = Cast<UMaterialInterface>(WallMaterial.TryLoad())) {
		Wall->SetMaterial(0, Material);
	}
	else {
		UE_LOG(LogTemp, Error, TEXT("Storm wall material '%s' did not load"), *WallMaterial.ToString());
	}
	// the size it starts at, on every machine, before the first replicated size arrives
	SizeScale = FVector(StartRadius / ShapeRadius, StartRadius / ShapeRadius, WallHeight / ShapeHeight);
	SetActorScale3D(SizeScale);
}

AStormActor* AStormActor::Find(const UWorld* World)
{
	if (World == nullptr || !GetDefault<AStormActor>()->bEnabled) {
		return nullptr;
	}
	TActorIterator<AStormActor> First(World);
	return First ? *First : nullptr;
}

void AStormActor::BeginPlay()
{
	Super::BeginPlay();
	if (!GetDefault<AStormActor>()->bEnabled) {
		// the game has no storm: one placed in a map is not seen, on any machine
		SetActorHiddenInGame(true);
		SetActorTickEnabled(false);
		return;
	}
	if (HasAuthority() && bRandomCentre) {
		SetActorLocation(FVector(FMath::FRandRange(RandomCentre.Min.X, RandomCentre.Max.X), FMath::FRandRange(RandomCentre.Min.Y, RandomCentre.Max.Y), GetActorLocation().Z));
	}
}

float AStormActor::GetRadius() const
{
	return ShapeRadius * GetActorScale3D().X;
}

void AStormActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (HasAuthority()) {
		if (IsShrinking) {
			// by the clock, not by the frame: the same shrink on a slow machine as a fast one
			const double Kept = FMath::Pow(FMath::Clamp(KeptPerSecond, 0.0f, 1.0f), DeltaTime);
			SizeScale = FVector(SizeScale.X * Kept, SizeScale.Y * Kept, SizeScale.Z);
			SetActorScale3D(SizeScale);
		}
	}
	else if (!GetActorScale3D().Equals(SizeScale)) {
		// the server's size, on every other machine
		SetActorScale3D(SizeScale);
	}
}

void AStormActor::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AStormActor, Damage);
	DOREPLIFETIME(AStormActor, IsShrinking);
	DOREPLIFETIME(AStormActor, SizeScale);
}

void AStormActor::ServerSetIsShrinking_Implementation() {
	if (Stage >= 4) {
		Stage = Stage + 1;
		if (Stage == 7) {
			// the circle has shrunk for three stages; it holds again
			Stage = 0;
			IsShrinking = !IsShrinking;
		}
	}
	else {
		Stage = Stage + 1;
		if (Stage == 4) {
			IsShrinking = !IsShrinking;
		}
	}
}

bool AStormActor::ServerSetIsShrinking_Validate() {
	return true;
}

void AStormActor::ServerSetNewDamage_Implementation() {
	Damage = Damage + 1;
}

bool AStormActor::ServerSetNewDamage_Validate() {
	return true;
}

void AStormActor::ServerStartStorm_Implementation() {
	FTimerHandle StormDamageTimerHandle;
	GetWorldTimerManager().SetTimer(StormDamageTimerHandle, this, &AStormActor::ServerSetIsShrinking, FMath::Max(0.1f, StageSeconds), true);
	FTimerHandle StormStateTimerHandle;
	GetWorldTimerManager().SetTimer(StormStateTimerHandle, this, &AStormActor::ServerSetNewDamage, FMath::Max(0.1f, DamageRiseSeconds), true);
}

bool AStormActor::ServerStartStorm_Validate() {
	return true;
}
