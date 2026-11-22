#include "SenniteMining.h"

#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "SenniteMatch.h"
#include "SennitePlayerState.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenniteMining, Log, All);

namespace
{
	/**
	 * Put a model on a body: scaled evenly so its longer side on the ground is Across, its middle over
	 * the actor's place, standing on it (bOnGround) or centred on it. How big it then is; nothing when the
	 * model did not load.
	 */
	FVector Wear(UStaticMeshComponent* Body, const FSoftObjectPath& Model, float Across, bool bOnGround, const TCHAR* What)
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(Model.TryLoad());
		const FBox Box = Mesh != nullptr ? Mesh->GetBoundingBox() : FBox(ForceInit);
		const FVector Extent = Box.GetSize();
		const double Long = FMath::Max(Extent.X, Extent.Y);
		if (Mesh == nullptr || Long <= 0.0 || Across <= 0.0f) {
			UE_LOG(LogSenniteMining, Error, TEXT("%s's model '%s' did not load"), What, *Model.ToString());
			return FVector::ZeroVector;
		}
		const double Scale = Across / Long;
		Body->SetStaticMesh(Mesh);
		Body->SetRelativeScale3D(FVector(Scale));
		Body->SetRelativeLocation(-FVector(Box.GetCenter().X, Box.GetCenter().Y, bOnGround ? Box.Min.Z : Box.GetCenter().Z) * Scale);
		return Extent * Scale;
	}
}

// ── deposits ────────────────────────────────────────────────────────────────

ASenniteDeposit::ASenniteDeposit()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	Body->SetCollisionProfileName(TEXT("BlockAll"));
	Body->SetCanEverAffectNavigation(false);
}

const TCHAR* ASenniteDeposit::NameOf(int32 InMaterial)
{
	static const TCHAR* Names[Kinds] = { TEXT("WOOD"), TEXT("STONE"), TEXT("STEEL") };
	return InMaterial >= 0 && InMaterial < Kinds ? Names[InMaterial] : TEXT("");
}

int32 ASenniteDeposit::MaterialOf(const FString& Symbol)
{
	for (int32 Each = 0; Each < Kinds; ++Each) {
		if (Symbol.Equals(NameOf(Each), ESearchCase::IgnoreCase)) {
			return Each;
		}
	}
	return INDEX_NONE;
}

void ASenniteDeposit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASenniteDeposit, Material);
	DOREPLIFETIME(ASenniteDeposit, Left);
	DOREPLIFETIME(ASenniteDeposit, Held);
}

void ASenniteDeposit::BeginPlay()
{
	Super::BeginPlay();
	Dress();
	Show();
}

void ASenniteDeposit::OnRep_Material()
{
	Dress();
}

void ASenniteDeposit::OnRep_Left()
{
	Show();
}

void ASenniteDeposit::Dress()
{
	const ASenniteDeposit* Rules = GetDefault<ASenniteDeposit>();
	if (!Rules->Models.IsValidIndex(Material) || !Rules->Across.IsValidIndex(Material)) {
		UE_LOG(LogSenniteMining, Error, TEXT("No model is set for a deposit of %s"), NameOf(Material));
		return;
	}
	Size = Wear(Body, Rules->Models[Material], Rules->Across[Material], true, TEXT("A deposit"));
}

void ASenniteDeposit::Show()
{
	const bool bHolds = Left > 0;
	SetActorHiddenInGame(!bHolds);
	SetActorEnableCollision(bHolds);
}

int32 ASenniteDeposit::Take(int32 Units)
{
	if (!HasAuthority()) {
		return 0;
	}
	const int32 Got = FMath::Clamp(Units, 0, Left);
	Left -= Got;
	if (Left <= 0) {
		Show();
	}
	return Got;
}

int32 ASenniteDeposit::Stock(UWorld* World, const TArray<int32>& Totals)
{
	if (World == nullptr || World->GetNetMode() == NM_Client) {
		return 0;
	}
	TActorIterator<ASenniteArena> Arenas(World);
	if (!Arenas || !Arenas->IsBuilt() || Arenas->GetPlan().Bases.Num() == 0) {
		return 0;
	}
	const ASenniteArena* Arena = *Arenas;
	const FArenaPlan& Plan = Arena->GetPlan();
	const ASenniteDeposit* Rules = GetDefault<ASenniteDeposit>();
	FVector Middle = FVector::ZeroVector;
	for (const FVector& Base : Plan.Bases) {
		Middle += Base / Plan.Bases.Num();
	}
	const int32 Each = FMath::Max(1, Rules->PerBase);
	const int32 Count = Plan.Bases.Num() * Each;
	int32 Stood = 0;
	for (int32 Kind = 0; Kind < Kinds; ++Kind) {
		const int32 Total = Totals.IsValidIndex(Kind) ? Totals[Kind] : 0;
		// what a material's deposits hold between them, shared out evenly; what does not divide goes to the first
		for (int32 Nth = 0; Nth < Count; ++Nth) {
			const int32 Units = Total / Count + (Nth < Total % Count ? 1 : 0);
			if (Units <= 0) {
				continue;
			}
			const int32 Base = Nth / Each;
			const int32 Of = Nth % Each;
			const FVector Centre = Plan.Bases[Base];
			const FVector ToMiddle = Middle - Centre;
			// with one base there is no other side: its deposits stand out ahead of it
			const FVector Out = ToMiddle.Size2D() > 1.0 ? ToMiddle.GetSafeNormal2D() : FVector::ForwardVector;
			const FVector Across2D = Out.RotateAngleAxis(90.0, FVector::UpVector);
			const double Span = FMath::Max(0.0, ToMiddle.Size2D() - Arena->BaseRadius);
			const double Along = Rules->Way.IsValidIndex(Kind) ? FMath::Clamp(Rules->Way[Kind], 0.0f, 1.0f) : (Kind + 1.0) / (Kinds + 1.0);
			// each material in a lane of its own; a base's second and third deposits of it in lanes farther out, to either side
			const int32 Lane = (Kind - 1) + Kinds * ((Of + 1) / 2) * (Of % 2 == 1 ? 1 : -1);
			FVector At = Centre + Out * (Arena->BaseRadius + (Span > 0.0 ? Span * Along : 1500.0 * (Kind + 1))) + Across2D * (Rules->Apart * Lane);
			At.Z = ASenniteArena::GroundHeight(Plan, At.X, At.Y);
			// on what is there: the land, or what stands on it
			FHitResult Ground;
			if (World->LineTraceSingleByChannel(Ground, At + FVector(0.0, 0.0, 6000.0), At - FVector(0.0, 0.0, 2000.0), ECC_Visibility)) {
				At.Z = Ground.ImpactPoint.Z;
			}
			const FTransform Stand(FRotator(0.0, (-Out).Rotation().Yaw, 0.0), At);
			ASenniteDeposit* Deposit = World->SpawnActorDeferred<ASenniteDeposit>(ASenniteDeposit::StaticClass(), Stand, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Deposit == nullptr) {
				continue;
			}
			Deposit->Material = Kind;
			Deposit->Held = Units;
			Deposit->Left = Units;
			Deposit->FinishSpawning(Stand);
			++Stood;
		}
	}
	UE_LOG(LogSenniteMining, Display, TEXT("%d deposits stand on the arena: %d WOOD, %d STONE, %d STEEL to mine"), Stood, Totals.IsValidIndex(0) ? Totals[0] : 0,
		Totals.IsValidIndex(1) ? Totals[1] : 0, Totals.IsValidIndex(2) ? Totals[2] : 0);
	return Stood;
}

ASenniteDeposit* ASenniteDeposit::Aimed(const UWorld* World, const FVector& From, const FVector& Toward, float Range, float Off)
{
	ASenniteDeposit* Found = nullptr;
	const FVector Line = Toward.GetSafeNormal();
	if (World == nullptr || Line.IsNearlyZero()) {
		return nullptr;
	}
	double Best = TNumericLimits<double>::Max();
	for (TActorIterator<ASenniteDeposit> It(World); It; ++It) {
		if (It->Left <= 0) {
			continue;
		}
		// its middle, half way up it
		const FVector To = It->GetActorLocation() + FVector(0.0, 0.0, It->Height() * 0.5) - From;
		const double Along = FVector::DotProduct(To, Line);
		if (Along <= 0.0 || Along > Range) {
			continue;
		}
		const double Aside = (To - Line * Along).Size();
		if (Aside <= Off + It->Size.GetMax() * 0.5 && Aside < Best) {
			Best = Aside;
			Found = *It;
		}
	}
	return Found;
}

// ── the robot ───────────────────────────────────────────────────────────────

ASenniteMiningRobot::ASenniteMiningRobot()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	// heroes pass it, and a shot lands on it
	Body->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Body->SetGenerateOverlapEvents(true);
	Body->SetCanEverAffectNavigation(false);
}

void ASenniteMiningRobot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASenniteMiningRobot, Health);
	DOREPLIFETIME(ASenniteMiningRobot, Target);
	DOREPLIFETIME(ASenniteMiningRobot, bMining);
	DOREPLIFETIME(ASenniteMiningRobot, Miner);
}

void ASenniteMiningRobot::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	Wear(Body, Model, Across, false, TEXT("The mining robot"));
}

void ASenniteMiningRobot::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && Health <= 0) {
		Health = StartHealth;
	}
}

ASenniteMiningRobot* ASenniteMiningRobot::Launch(ASenniteCharacter* By)
{
	ASennitePlayerState* State = By != nullptr ? By->GetPlayerState<ASennitePlayerState>() : nullptr;
	UWorld* World = By != nullptr ? By->GetWorld() : nullptr;
	if (World == nullptr || State == nullptr || !By->HasAuthority() || State->bRobotDown || State->Robot != nullptr) {
		return nullptr;
	}
	// out of the hero's hands: a little ahead of it and above its head
	const FVector Ahead = By->GetActorForwardVector().GetSafeNormal2D();
	const FTransform Out(FRotator(0.0, Ahead.Rotation().Yaw, 0.0), By->GetActorLocation() + Ahead * 120.0 + FVector(0.0, 0.0, 160.0));
	ASenniteMiningRobot* Robot = World->SpawnActorDeferred<ASenniteMiningRobot>(ASenniteMiningRobot::StaticClass(), Out, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Robot == nullptr) {
		return nullptr;
	}
	Robot->Miner = State;
	Robot->Health = Robot->StartHealth;
	Robot->FinishSpawning(Out);
	State->Robot = Robot;
	return Robot;
}

void ASenniteMiningRobot::SendTo(ASenniteDeposit* Deposit)
{
	if (!HasAuthority() || Deposit == Target) {
		return;
	}
	Target = Deposit;
	bMining = false;
	Taking = 0.0f;
}

FVector ASenniteMiningRobot::Over(const ASenniteDeposit* Deposit) const
{
	return Deposit->GetActorLocation() + FVector(0.0, 0.0, Deposit->Height() + Above);
}

void ASenniteMiningRobot::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!HasAuthority()) {
		return;
	}
	// a robot is its player's: with its player gone from the game, so is it
	if (Miner == nullptr) {
		Destroy();
		return;
	}
	const ASenniteDeposit* To = Target;
	if (To == nullptr || To->Left <= 0) {
		// nothing to mine where it was sent: it hangs where it is until it is sent somewhere
		bMining = false;
		Taking = 0.0f;
		return;
	}
	const FVector Here = GetActorLocation();
	const FVector Goal = Over(To);
	FVector Step = Goal - Here;
	if (Step.Size2D() > 400.0) {
		// on its way it keeps above the land; over its deposit it comes down to it
		double Floor = Goal.Z;
		TActorIterator<ASenniteArena> Arena(GetWorld());
		if (Arena && Arena->IsBuilt()) {
			Floor = FMath::Max(Floor, ASenniteArena::GroundHeight(Arena->GetPlan(), Here.X, Here.Y) + Cruise);
		}
		Step.Z = Floor - Here.Z;
		SetActorRotation(FRotator(0.0, Step.Rotation().Yaw, 0.0));
	}
	const double Far = Step.Size();
	const double Reach = Speed * DeltaTime;
	const FVector Next = Far <= Reach ? Here + Step : Here + Step / Far * Reach;
	SetActorLocation(Next);
	if (FVector::Dist(Next, Goal) > 20.0) {
		bMining = false;
		Taking = 0.0f;
		return;
	}
	bMining = true;
	Taking += DeltaTime;
	const float One = FMath::Max(0.05f, UnitSeconds);
	while (Taking >= One) {
		Taking -= One;
		const int32 Got = Target->Take(1);
		if (Got <= 0) {
			break;
		}
		// its player builds with it at once, and it is counted as mined
		const int32 Kind = Target->Material;
		if (Miner->MaterialCounts.IsValidIndex(Kind)) {
			Miner->MaterialCounts[Kind] += Got;
		}
		if (Miner->Mined.IsValidIndex(Kind)) {
			Miner->Mined[Kind] += Got;
		}
		if (ASenniteMatch* Match = ASenniteMatch::Find(GetWorld())) {
			Match->Mined(Miner, Kind, Got);
		}
	}
}

void ASenniteMiningRobot::TakeHit(float Damage, ASenniteCharacter* Shooter)
{
	if (!HasAuthority() || Health <= 0) {
		return;
	}
	const ASennitePlayerState* By = Shooter != nullptr ? Shooter->GetPlayerState<ASennitePlayerState>() : nullptr;
	if (By == nullptr || By == Miner) {
		return;
	}
	if (const ASenniteMatch* Match = ASenniteMatch::Find(GetWorld())) {
		// in a match, as a hero is: only a faction that can harm its player's harms it
		if (Miner == nullptr || !Match->Harms(Shooter, Miner->FactionId)) {
			return;
		}
	}
	Health -= FMath::Max(1, FMath::RoundToInt(Damage));
	if (Health > 0) {
		return;
	}
	UE_LOG(LogSenniteMining, Display, TEXT("A mining robot was shot down"));
	if (Miner != nullptr) {
		// gone for the rest of the match
		Miner->bRobotDown = true;
		Miner->Robot = nullptr;
	}
	Destroy();
}

FString ASenniteMiningRobot::Status(const ASennitePlayerState* Of)
{
	if (Of == nullptr) {
		return FString();
	}
	if (Of->bRobotDown) {
		return FString(TEXT("ROBOT DOWN"));
	}
	const ASenniteMiningRobot* Robot = Of->Robot;
	if (Robot == nullptr) {
		return FString(TEXT("ROBOT READY"));
	}
	const ASenniteDeposit* To = Robot->Target;
	if (To != nullptr && To->Left > 0) {
		return FString::Printf(TEXT("ROBOT %s %s"), Robot->bMining ? TEXT("MINING") : TEXT("GOING TO"), ASenniteDeposit::NameOf(To->Material));
	}
	return FString(TEXT("ROBOT IDLE"));
}
