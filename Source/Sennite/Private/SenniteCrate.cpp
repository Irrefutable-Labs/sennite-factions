#include "SenniteCrate.h"

#include "SenniteCharacter.h"
#include "SennitePlayerState.h"
#include "SenniteWeapons.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenniteCrate, Log, All);

namespace
{
	/** How much of a crate's height is its lid. */
	const float LidShare = 0.2f;
}

ASenniteCrate::ASenniteCrate()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	Body->SetCollisionProfileName(TEXT("BlockAll"));
	Body->SetCanEverAffectNavigation(false);
	Lid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lid"));
	Lid->SetupAttachment(RootComponent);
	Lid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ASenniteCrate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASenniteCrate, bOpen);
}

void ASenniteCrate::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	UStaticMesh* Cube = Cast<UStaticMesh>(Shape.TryLoad());
	UMaterialInterface* Face = Cast<UMaterialInterface>(Material.TryLoad());
	if (Cube == nullptr || Face == nullptr) {
		UE_LOG(LogSenniteCrate, Error, TEXT("A crate's shape '%s' or material '%s' did not load"), *Shape.ToString(), *Material.ToString());
		return;
	}
	// the actor stands on the ground at the middle of what the crate covers; its front is the way it faces
	const float BodyHigh = Size.Z * (1.0f - LidShare);
	Body->SetStaticMesh(Cube);
	Body->SetRelativeLocation(FVector(0.0, 0.0, BodyHigh * 0.5));
	Body->SetRelativeScale3D(FVector(Size.Y, Size.X, BodyHigh) / 100.0);
	Lid->SetStaticMesh(Cube);
	Lid->SetRelativeScale3D(FVector(Size.Y, Size.X, Size.Z * LidShare) / 100.0);
	UMaterialInstanceDynamic* BodyFace = UMaterialInstanceDynamic::Create(Face, this);
	BodyFace->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.07f, 0.12f, 0.05f));
	Body->SetMaterial(0, BodyFace);
	UMaterialInstanceDynamic* LidFace = UMaterialInstanceDynamic::Create(Face, this);
	LidFace->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.80f, 0.60f, 0.02f));
	Lid->SetMaterial(0, LidFace);
	ShowLid();
}

void ASenniteCrate::ShowLid()
{
	const float BodyHigh = Size.Z * (1.0f - LidShare);
	const float LidHigh = Size.Z * LidShare;
	if (bOpen) {
		// thrown back: standing on its edge behind the crate
		Lid->SetRelativeLocationAndRotation(FVector(-Size.Y * 0.5 - LidHigh * 0.5, 0.0, BodyHigh + Size.Y * 0.5), FRotator(90.0, 0.0, 0.0));
	}
	else {
		Lid->SetRelativeLocationAndRotation(FVector(0.0, 0.0, BodyHigh + LidHigh * 0.5), FRotator::ZeroRotator);
	}
}

void ASenniteCrate::OnRep_Open()
{
	ShowLid();
}

bool ASenniteCrate::Open(ASenniteCharacter* By)
{
	if (!HasAuthority() || bOpen || By == nullptr || FVector::Dist(By->GetActorLocation(), GetActorLocation()) > Reach + Size.GetMax()) {
		return false;
	}
	bOpen = true;
	ShowLid();
	// the weapons lie in front of it, at the height a hero's hands pass
	const FVector Front = GetActorLocation() + GetActorForwardVector() * (Size.Y * 0.5 + 90.0) + FVector(0.0, 0.0, 60.0);
	const FVector Side = GetActorRightVector() * 45.0;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Laid.Reset();
	Laid.Add(GetWorld()->SpawnActor<ASenniteRifle>(Front - Side, GetActorRotation(), Spawn));
	Laid.Add(GetWorld()->SpawnActor<ASenniteShotgun>(Front + Side, GetActorRotation(), Spawn));
	// the rounds go to the hero who opened it
	if (ASennitePlayerState* State = By->GetPlayerState<ASennitePlayerState>()) {
		if (State->EquippedWeaponsAmmunition.IsValidIndex(2)) {
			State->EquippedWeaponsAmmunition[1] += RifleRounds;
			State->EquippedWeaponsAmmunition[2] += ShotgunShells;
		}
	}
	if (RestockSeconds > 0.0f) {
		FTimerHandle Filling;
		GetWorldTimerManager().SetTimer(Filling, this, &ASenniteCrate::Restock, RestockSeconds, false);
	}
	return true;
}

void ASenniteCrate::Restock()
{
	// what nobody took is gone; a weapon someone took is theirs
	for (const TWeakObjectPtr<AActor>& Each : Laid) {
		const AWeaponActor* Weapon = Cast<AWeaponActor>(Each.Get());
		if (Weapon != nullptr && Weapon->Holder == nullptr) {
			Each->Destroy();
		}
	}
	Laid.Reset();
	bOpen = false;
	ShowLid();
}

ASenniteCrate* ASenniteCrate::Nearest(const UWorld* World, const FVector& From, float Within, bool bClosed)
{
	ASenniteCrate* Found = nullptr;
	double Best = Within;
	if (World == nullptr) {
		return nullptr;
	}
	for (TActorIterator<ASenniteCrate> It(World); It; ++It) {
		const double Far = FVector::Dist(It->GetActorLocation(), From);
		if (Far <= Best && (!bClosed || !It->bOpen)) {
			Best = Far;
			Found = *It;
		}
	}
	return Found;
}

void ASenniteCrate::Stock(UWorld* World, const TArray<FTransform>& Starts, const TArray<TPair<FVector, float>>& Built)
{
	if (World == nullptr || World->GetNetMode() == NM_Client) {
		return;
	}
	const ASenniteCrate* Rules = GetDefault<ASenniteCrate>();
	int32 Stood = 0;
	for (const FTransform& Start : Starts) {
		const FVector Ahead = Start.GetRotation().GetForwardVector().GetSafeNormal2D();
		// a start is a hero's middle; the ground is under it
		const FVector Ground = Start.GetLocation() - FVector(0.0, 0.0, 110.0);
		for (int32 Each = 0; Each < Rules->PerBase; ++Each) {
			// spread either side of the way the heroes face
			const double Turn = (Each - (Rules->PerBase - 1) * 0.5) * 70.0;
			const FVector Way = Ahead.RotateAngleAxis(Turn, FVector::UpVector);
			FVector At = Ground + Way * Rules->FromStart;
			for (int32 Farther = 1; Farther <= 12; ++Farther) {
				const bool bClear = !Built.ContainsByPredicate([&At](const TPair<FVector, float>& One) { return FVector::Dist2D(At, One.Key) < One.Value + 150.0f; });
				if (bClear) {
					break;
				}
				At = Ground + Way * (Rules->FromStart + Farther * 150.0f);
			}
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			// its front faces where the heroes start
			if (World->SpawnActor<ASenniteCrate>(At, FRotator(0.0, (-Way).Rotation().Yaw, 0.0), Spawn) != nullptr) {
				++Stood;
			}
		}
	}
	UE_LOG(LogSenniteCrate, Display, TEXT("%d weapon crates stand on %d bases"), Stood, Starts.Num());
}
