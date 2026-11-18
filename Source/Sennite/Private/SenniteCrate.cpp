#include "SenniteCrate.h"

#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "SenniteHealing.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenniteCrate, Log, All);

ASenniteCrate::ASenniteCrate()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	Body->SetCollisionProfileName(TEXT("BlockAll"));
	Body->SetCanEverAffectNavigation(false);
}

void ASenniteCrate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASenniteCrate, bOpen);
	DOREPLIFETIME(ASenniteCrate, bLeft);
}

void ASenniteCrate::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// a crate holds everything: both weapons, rounds for both, a pack of bandages and of potions
	Rounds = RifleRounds;
	Shells = ShotgunShells;
	Bandages = GetDefault<ASenniteBandage>()->Count;
	Potions = GetDefault<ASennitePotion>()->Count;
	UStaticMesh* Mesh = Cast<UStaticMesh>(Model.TryLoad());
	const FVector Extent = Mesh != nullptr ? Mesh->GetBoundingBox().GetSize() : FVector::ZeroVector;
	const bool bLongX = Extent.X >= Extent.Y;
	const double Long = bLongX ? Extent.X : Extent.Y;
	if (Mesh == nullptr || Long <= 0.0) {
		UE_LOG(LogSenniteCrate, Error, TEXT("A crate's model '%s' did not load"), *Model.ToString());
		return;
	}
	// The actor stands on the ground at the middle of what the crate covers; its front is the way it
	// faces. The model is scaled evenly to be Across along its long side, and turned so that side
	// lies across the way the crate faces.
	const FBox Box = Mesh->GetBoundingBox();
	const double Scale = Across / Long;
	const FRotator Turn(0.0, bLongX ? 90.0 : 0.0, 0.0);
	Size = FVector(Across, (bLongX ? Extent.Y : Extent.X) * Scale, Extent.Z * Scale);
	Body->SetStaticMesh(Mesh);
	Body->SetRelativeScale3D(FVector(Scale));
	Body->SetRelativeRotation(Turn);
	Body->SetRelativeLocation(-Turn.RotateVector(FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z) * Scale));
}

void ASenniteCrate::BeginPlay()
{
	Super::BeginPlay();
	if (bLeft) {
		// heroes walk through the crate a fallen hero left: it may lie on a bridge
		Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}
}

bool ASenniteCrate::Open(ASenniteCharacter* By)
{
	if (!HasAuthority() || bOpen || By == nullptr || FVector::Dist(By->GetActorLocation(), GetActorLocation()) > Reach + Across) {
		return false;
	}
	// what it holds goes straight to the hero who opened it
	if (!By->TakeFromCrate(bRifle, bShotgun, Rounds, Shells, Bandages, Potions)) {
		return false;
	}
	if (bLeft) {
		Destroy();
		return true;
	}
	bOpen = true;
	if (RestockSeconds > 0.0f) {
		FTimerHandle Filling;
		GetWorldTimerManager().SetTimer(Filling, this, &ASenniteCrate::Restock, RestockSeconds, false);
	}
	return true;
}

void ASenniteCrate::Restock()
{
	bOpen = false;
}

ASenniteCrate* ASenniteCrate::Leave(UWorld* World, const AActor* Fallen, bool bHadRifle, bool bHadShotgun)
{
	if (World == nullptr || Fallen == nullptr || World->GetNetMode() == NM_Client || (!bHadRifle && !bHadShotgun)) {
		return nullptr;
	}
	// on the ground under where the hero fell: the land, or what is built there
	const FVector Fell = Fallen->GetActorLocation();
	const double Yaw = Fallen->GetActorRotation().Yaw;
	FHitResult Ground;
	FCollisionQueryParams Past(SCENE_QUERY_STAT(SenniteCrateLeave), false, Fallen);
	if (!World->LineTraceSingleByChannel(Ground, Fell, Fell - FVector(0.0, 0.0, 3000.0), ECC_Visibility, Past)) {
		return nullptr;
	}
	ASenniteCrate* Crate = World->SpawnActorDeferred<ASenniteCrate>(ASenniteCrate::StaticClass(), FTransform(FRotator(0.0, Yaw, 0.0), Ground.ImpactPoint),
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Crate == nullptr) {
		return nullptr;
	}
	Crate->bLeft = true;
	Crate->FinishSpawning(FTransform(FRotator(0.0, Yaw, 0.0), Ground.ImpactPoint));
	// the weapons the hero had, as new, and nothing else
	Crate->bRifle = bHadRifle;
	Crate->bShotgun = bHadShotgun;
	Crate->Rounds = 0;
	Crate->Shells = 0;
	Crate->Bandages = 0;
	Crate->Potions = 0;
	Crate->SetLifeSpan(FMath::Max(1.0f, Crate->LeftSeconds));
	return Crate;
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
	TActorIterator<ASenniteArena> Arenas(World);
	const ASenniteArena* Arena = Arenas ? *Arenas : nullptr;
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
			// standing on the land there
			if (Arena != nullptr) {
				At.Z = ASenniteArena::GroundHeight(Arena->GetPlan(), At.X, At.Y);
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
