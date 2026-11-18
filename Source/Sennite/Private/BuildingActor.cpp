// A piece built in a fight, made of tiles that can be cut out and knocked out of line (BuildingActor.h).

#include "BuildingActor.h"
#include "Net/UnrealNetwork.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "SenniteCharacter.h"
#include "SenniteMatch.h"
#include "SennitePlayerState.h"
#include "TimerManager.h"

namespace
{
	/** How near two things must be to count as touching: pieces sit on a grid, so they touch or are far apart. */
	constexpr double TouchSlack = 6.0;

	/** While the pieces nothing holds are falling: their going asks for no second look. */
	bool bDropping = false;
}

ABuildingActor::ABuildingActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	PieceMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Piece"));
	PieceMesh->SetupAttachment(RootComponent);
	PieceMesh->SetCanEverAffectNavigation(false);
	GhostTiles = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Ghosts"));
	GhostTiles->SetupAttachment(RootComponent);
	GhostTiles->SetCanEverAffectNavigation(false);
	GhostTiles->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GhostTiles->SetCastShadow(false);
}

void ABuildingActor::SetPiece(const TCHAR* Kind, int32 Material, bool bPreview)
{
	StructureType = Kind;
	MaterialType = Material;
	IsPreview = bPreview;
	// a preview is drawn by the building player's own machine for them alone: it is never sent
	SetReplicates(!IsPreview);

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

int32 ABuildingActor::TileCount() const
{
	return StructureType == TEXT("Wall") ? 9 : 4;
}

FTransform ABuildingActor::TileTransform(int32 Tile, bool bWorn) const
{
	// The actor sits at the middle of the bottom of its build cell (a wall: of the cell's edge).
	// Every tile is the 100 cm shape scaled to a slab.
	const double Thin = Thickness / 100.0;
	FTransform Place;
	FVector Across = FVector::YAxisVector; // the way the tile's face looks, in the tile's own frame
	if (StructureType == TEXT("Wall")) {
		// upright, across the actor's X axis
		const double Side = Cell / 3.0;
		const int32 Column = Tile % 3;
		const int32 Row = Tile / 3;
		Place = FTransform(FRotator::ZeroRotator, FVector(-Cell * 0.5 + Side * (Column + 0.5), 0.0, Side * (Row + 0.5)), FVector(Side / 100.0, Thin, Side / 100.0));
	}
	else if (StructureType == TEXT("Ramp")) {
		// from the bottom of the near edge to the top of the far edge, rising along the actor's X axis
		const double Side = Cell / 2.0;
		const double X = -Cell * 0.5 + Side * ((Tile / 2) + 0.5);
		const double Y = -Cell * 0.5 + Side * ((Tile % 2) + 0.5);
		Place = FTransform(FRotator(45.0, 0.0, 0.0), FVector(X, Y, X + Cell * 0.5), FVector(Side * UE_SQRT_2 / 100.0, Side / 100.0, Thin));
		Across = FVector::ZAxisVector;
	}
	else {
		// flat, its top at the level it is built on
		const double Side = Cell / 2.0;
		const double X = -Cell * 0.5 + Side * ((Tile / 2) + 0.5);
		const double Y = -Cell * 0.5 + Side * ((Tile % 2) + 0.5);
		Place = FTransform(FRotator::ZeroRotator, FVector(X, Y, -Thickness * 0.5), FVector(Side / 100.0, Side / 100.0, Thin));
		Across = FVector::ZAxisVector;
	}
	if (!bWorn || IsPreview) {
		return Place;
	}
	const double Worn = 1.0 - FMath::Clamp<double>(Health / FMath::Max(1.0f, MaxHealth()), 0.0, 1.0);
	if (Worn < 0.05) {
		return Place;
	}
	// Each tile is knocked its own way, the same on every machine: the way comes from where the piece
	// stands and which tile it is, how far from how hurt the piece is.
	const FIntVector Cellpoint(FMath::RoundToInt(GetActorLocation().X / 10.0), FMath::RoundToInt(GetActorLocation().Y / 10.0), FMath::RoundToInt(GetActorLocation().Z / 10.0));
	FRandomStream Knock(HashCombine(HashCombine(GetTypeHash(Cellpoint), GetTypeHash(StructureType)), ::GetTypeHash(Tile)));
	const FRotator Tilt(Knock.FRandRange(-1.0f, 1.0f) * Worn * WornTilt, Knock.FRandRange(-1.0f, 1.0f) * Worn * WornTilt, Knock.FRandRange(-1.0f, 1.0f) * Worn * WornTilt);
	const FVector Push = Place.GetRotation().RotateVector(Across) * Knock.FRandRange(-1.0f, 1.0f) * Worn * WornPush;
	// a little smaller too, so the seams between tiles open as cracks
	const FVector Shrunk = Place.GetScale3D() * (FVector::OneVector - (FVector::OneVector - Across) * 0.03 * Worn);
	return FTransform(Place.GetRotation() * Tilt.Quaternion(), Place.GetLocation() + Push, Shrunk);
}

int32 ABuildingActor::TileUnderAim(const FVector& From, const FVector& Way, double Reach, double& OutDistance) const
{
	// in the actor's own frame the tiles lie on one plane
	const FTransform Frame = GetActorTransform();
	const FVector O = Frame.InverseTransformPositionNoScale(From);
	const FVector D = Frame.InverseTransformVectorNoScale(Way.GetSafeNormal());
	const double Half = Cell * 0.5;
	double T = -1.0;
	if (StructureType == TEXT("Wall")) {
		if (FMath::Abs(D.Y) < 1e-6) {
			return INDEX_NONE;
		}
		T = -O.Y / D.Y;
	}
	else if (StructureType == TEXT("Ramp")) {
		// the slope: Z = X + Cell / 2
		const double Rate = D.Z - D.X;
		if (FMath::Abs(Rate) < 1e-6) {
			return INDEX_NONE;
		}
		T = (Half + O.X - O.Z) / Rate;
	}
	else {
		if (FMath::Abs(D.Z) < 1e-6) {
			return INDEX_NONE;
		}
		T = -O.Z / D.Z;
	}
	if (T < 0.0 || T > Reach) {
		return INDEX_NONE;
	}
	const FVector P = O + D * T;
	int32 Tile = INDEX_NONE;
	if (StructureType == TEXT("Wall")) {
		const double Side = Cell / 3.0;
		const int32 Column = FMath::FloorToInt((P.X + Half) / Side);
		const int32 Row = FMath::FloorToInt(P.Z / Side);
		if (Column >= 0 && Column < 3 && Row >= 0 && Row < 3) {
			Tile = Row * 3 + Column;
		}
	}
	else {
		const double Side = Cell / 2.0;
		const int32 Row = FMath::FloorToInt((P.X + Half) / Side);
		const int32 Column = FMath::FloorToInt((P.Y + Half) / Side);
		if (Row >= 0 && Row < 2 && Column >= 0 && Column < 2) {
			Tile = Row * 2 + Column;
		}
	}
	OutDistance = T;
	return Tile;
}

FBox ABuildingActor::Fills() const
{
	const double Half = Cell * 0.5;
	FBox Own;
	if (StructureType == TEXT("Wall")) {
		Own = FBox(FVector(-Half, -Thickness * 0.5, 0.0), FVector(Half, Thickness * 0.5, Cell));
	}
	else if (StructureType == TEXT("Ramp")) {
		Own = FBox(FVector(-Half, -Half, 0.0), FVector(Half, Half, Cell));
	}
	else {
		Own = FBox(FVector(-Half, -Half, -Thickness), FVector(Half, Half, 0.0));
	}
	return Own.TransformBy(GetActorTransform());
}

bool ABuildingActor::TouchesSolid() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr) {
		return false;
	}
	const FBox Box = Fills().ExpandBy(TouchSlack);
	FCollisionObjectQueryParams Things;
	Things.AddObjectTypesToQuery(ECC_WorldStatic);
	Things.AddObjectTypesToQuery(ECC_WorldDynamic);
	TArray<FOverlapResult> Found;
	World->OverlapMultiByObjectType(Found, Box.GetCenter(), FQuat::Identity, Things, FCollisionShape::MakeBox(Box.GetExtent()),
		FCollisionQueryParams(SCENE_QUERY_STAT(SennitePieceHeld), false, this));
	for (const FOverlapResult& Each : Found) {
		const AActor* Thing = Each.GetActor();
		const UPrimitiveComponent* Part = Each.GetComponent();
		// solid is what a hero cannot walk through; a piece holds a piece only if it is held itself (HeldUp)
		if (Thing != nullptr && Part != nullptr && !Thing->IsA<ABuildingActor>() && !Thing->IsA<APawn>()
			&& Part->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block) {
			return true;
		}
	}
	return false;
}

TSet<const ABuildingActor*> ABuildingActor::HeldUp(UWorld* World)
{
	TSet<const ABuildingActor*> Held;
	if (World == nullptr) {
		return Held;
	}
	TArray<const ABuildingActor*> Pieces;
	TArray<FBox> Boxes;
	for (TActorIterator<ABuildingActor> It(World); It; ++It) {
		if (!It->IsPreview && !It->IsActorBeingDestroyed()) {
			Pieces.Add(*It);
			Boxes.Add(It->Fills().ExpandBy(TouchSlack));
		}
	}
	// from the pieces that touch something solid, outward through the pieces touching them
	TArray<int32> Reached;
	for (int32 Each = 0; Each < Pieces.Num(); ++Each) {
		if (Pieces[Each]->TouchesSolid()) {
			Held.Add(Pieces[Each]);
			Reached.Add(Each);
		}
	}
	while (Reached.Num() > 0) {
		const int32 From = Reached.Pop(EAllowShrinking::No);
		for (int32 Each = 0; Each < Pieces.Num(); ++Each) {
			if (!Held.Contains(Pieces[Each]) && Boxes[From].Intersect(Boxes[Each])) {
				Held.Add(Pieces[Each]);
				Reached.Add(Each);
			}
		}
	}
	return Held;
}

void ABuildingActor::DropUnheld(UWorld* World)
{
	if (World == nullptr || World->GetNetMode() == NM_Client || World->bIsTearingDown || bDropping) {
		return;
	}
	const TSet<const ABuildingActor*> Held = HeldUp(World);
	TArray<ABuildingActor*> Falling;
	for (TActorIterator<ABuildingActor> It(World); It; ++It) {
		if (!It->IsPreview && !It->IsActorBeingDestroyed() && !Held.Contains(*It)) {
			Falling.Add(*It);
		}
	}
	// what is left standing was held without them, so one look is enough
	TGuardValue<bool> Once(bDropping, true);
	for (ABuildingActor* Piece : Falling) {
		Piece->Health = 0.0f;
		Piece->Destroy();
	}
}

float ABuildingActor::MaxHealth() const
{
	const ABuildingActor* Shared = GetDefault<ABuildingActor>();
	return Shared->TierHealth.IsValidIndex(MaterialType) ? Shared->TierHealth[MaterialType] : Health;
}

FString ABuildingActor::Describe() const
{
	const TCHAR* Materials[] = { TEXT("WOOD"), TEXT("STONE"), TEXT("STEEL") };
	return FString::Printf(TEXT("%s %s"), MaterialType >= 0 && MaterialType < 3 ? Materials[MaterialType] : TEXT(""), *StructureType.ToUpper());
}

bool ABuildingActor::MayBeEditedBy(const APlayerState* Player) const
{
	if (Player == nullptr || IsPreview) {
		return false;
	}
	if (!FactionId.IsEmpty()) {
		const ASennitePlayerState* Theirs = Cast<ASennitePlayerState>(Player);
		return Theirs != nullptr && Theirs->FactionId == FactionId;
	}
	return Builder == Player;
}

void ABuildingActor::TakeHit(float Damage, ASenniteCharacter* Shooter)
{
	if (!HasAuthority() || IsPreview || Health <= 0.0f || IsActorBeingDestroyed()) {
		return;
	}
	// In a match a shot counts on a piece as it does on a hero: from another faction than the one
	// that built it, while the match is being played.
	if (const ASenniteMatch* Match = ASenniteMatch::Find(GetWorld())) {
		if (!Match->Harms(Shooter, FactionId)) {
			return;
		}
	}
	Health = FMath::Max(0.0f, Health - Damage);
	if (Shooter != nullptr) {
		Shooter->ClientDrawPieceHit(Damage);
	}
	if (Health <= 0.0f) {
		Destroy();
		return;
	}
	// the server's own picture shows it too (a host plays on it)
	OnRep_Shape();
}

void ABuildingActor::ShowCut(int32 Shown, int32 Ghosts)
{
	ShownCut = Shown & AllTiles();
	ShownGhosts = Ghosts & AllTiles();
	LayTiles();
}

void ABuildingActor::OnRep_Shape()
{
	// what the server says it is, unless this machine's player is editing it right now
	if (EditingHere.IsValid()) {
		LayTiles();
		return;
	}
	ShowCut(Cut, 0);
}

void ABuildingActor::LayTiles()
{
	TArray<FTransform> Tiles;
	TArray<FTransform> Ghosts;
	for (int32 Tile = 0; Tile < TileCount(); ++Tile) {
		if ((ShownCut & (1 << Tile)) == 0) {
			Tiles.Add(TileTransform(Tile, true));
		}
		if ((ShownGhosts & (1 << Tile)) != 0) {
			Ghosts.Add(TileTransform(Tile, false));
		}
	}
	PieceMesh->ClearInstances();
	PieceMesh->AddInstances(Tiles, false);
	GhostTiles->ClearInstances();
	if (Ghosts.Num() > 0) {
		GhostTiles->AddInstances(Ghosts, false);
	}
}

void ABuildingActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();

	// what pieces are made of is one list, held by the base class
	const ABuildingActor* Shared = GetDefault<ABuildingActor>();
	if (UStaticMesh* Shape = Cast<UStaticMesh>(Shared->PieceShape.TryLoad())) {
		PieceMesh->SetStaticMesh(Shape);
		GhostTiles->SetStaticMesh(Shape);
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
	if (UMaterialInterface* Ghost = Cast<UMaterialInterface>(Shared->PreviewMaterial.TryLoad())) {
		GhostTiles->SetMaterial(0, Ghost);
	}
	if (Shared->TierHealth.IsValidIndex(MaterialType)) {
		Health = Shared->TierHealth[MaterialType];
	}
	ShowCut(Cut, 0);
}

void ABuildingActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// It fell: on the server because its health ran out, on another machine because the server took
	// it away. A player's own first copy of a piece, replaced by the server's, did not fall.
	const bool bFell = EndPlayReason == EEndPlayReason::Destroyed && !IsPreview
		&& (HasAuthority() ? Health <= 0.0f : GetLocalRole() == ROLE_SimulatedProxy);
	if (bFell && GetNetMode() != NM_DedicatedServer && GetWorld() != nullptr && PieceMesh->GetInstanceCount() > 0) {
		TArray<FTransform> Laid;
		for (int32 Each = 0; Each < PieceMesh->GetInstanceCount(); ++Each) {
			FTransform Tile;
			if (PieceMesh->GetInstanceTransform(Each, Tile, true)) {
				Laid.Add(Tile);
			}
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ASennitePieceRubble* Rubble = GetWorld()->SpawnActor<ASennitePieceRubble>(GetActorLocation(), FRotator::ZeroRotator, Params)) {
			Rubble->Scatter(PieceMesh->GetStaticMesh(), PieceMesh->GetMaterial(0), Laid, GetTypeHash(FIntVector(GetActorLocation() / 10.0)));
		}
	}
	// a piece gone from the server: a moment later, what it alone was holding falls
	UWorld* World = GetWorld();
	if (EndPlayReason == EEndPlayReason::Destroyed && !IsPreview && !bDropping && World != nullptr && World->GetNetMode() != NM_Client && !World->bIsTearingDown) {
		FTimerHandle Settling;
		World->GetTimerManager().SetTimer(Settling, FTimerDelegate::CreateLambda([Where = TWeakObjectPtr<UWorld>(World)]() {
			ABuildingActor::DropUnheld(Where.Get());
		}), DropSeconds, false);
	}
	Super::EndPlay(EndPlayReason);
}

ASennitePieceRubble::ASennitePieceRubble()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	Tiles = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Tiles"));
	RootComponent = Tiles;
	Tiles->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tiles->SetCanEverAffectNavigation(false);
	Tiles->SetMobility(EComponentMobility::Movable);
}

void ASennitePieceRubble::Scatter(UStaticMesh* Shape, UMaterialInterface* Face, const TArray<FTransform>& Laid, int32 Seed)
{
	Tiles->SetStaticMesh(Shape);
	Tiles->SetMaterial(0, Face);
	FRandomStream Fling(Seed);
	const FVector Middle = GetActorLocation() + FVector(0.0, 0.0, ABuildingActor::Cell * 0.4);
	for (const FTransform& Tile : Laid) {
		// out from the piece's middle, a little up, turning
		const FVector Out = (Tile.GetLocation() - Middle).GetSafeNormal2D();
		FFlying One;
		One.Start = Tile;
		One.Velocity = Out * Fling.FRandRange(120.0f, 320.0f) + FVector(Fling.FRandRange(-60.0f, 60.0f), Fling.FRandRange(-60.0f, 60.0f), Fling.FRandRange(80.0f, 260.0f));
		One.Spin = FVector(Fling.FRandRange(-200.0f, 200.0f), Fling.FRandRange(-200.0f, 200.0f), Fling.FRandRange(-120.0f, 120.0f));
		Flying.Add(One);
		Tiles->AddInstance(Tile, true);
	}
}

void ASennitePieceRubble::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Age >= Seconds) {
		Destroy();
		return;
	}
	const float Left = 1.0f - Age / Seconds;
	for (int32 Each = 0; Each < Flying.Num(); ++Each) {
		const FFlying& One = Flying[Each];
		const FVector At = One.Start.GetLocation() + One.Velocity * Age + FVector(0.0, 0.0, -490.0) * Age * Age;
		const FQuat Turned = FQuat(FRotator(One.Spin.X * Age, One.Spin.Z * Age, One.Spin.Y * Age)) * One.Start.GetRotation();
		Tiles->UpdateInstanceTransform(Each, FTransform(Turned, At, One.Start.GetScale3D() * FMath::Max(0.05f, Left)), true, Each == Flying.Num() - 1, true);
	}
}

void ABuildingActor::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABuildingActor, Health);
	DOREPLIFETIME(ABuildingActor, FactionId);
	DOREPLIFETIME(ABuildingActor, Builder);
	DOREPLIFETIME(ABuildingActor, Cut);
}
