#include "SenniteBaseEditor.h"

#include "BuildingActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Materials/MaterialInterface.h"
#include "SennitePublisher.h"
#include "SenniteStructure.h"

namespace
{
	/** How much the commit button commits: everything still needed, or a part of it. */
	const int32 Amounts[] = { 0, 100, 50, 10 };

	/** The ground a structure covers: a quarter turn swaps its width and depth. */
	void CoveredBy(const FSennitePrefab& Prefab, int32 Rotation, int32& Width, int32& Depth)
	{
		const bool bTurned = Rotation == 90 || Rotation == 270;
		Width = bTurned ? Prefab.Depth : Prefab.Width;
		Depth = bTurned ? Prefab.Width : Prefab.Depth;
	}

	FString RecipeOf(const FSennitePrefab& Prefab)
	{
		FString Text;
		for (const TPair<FString, int32>& Line : Prefab.Recipe) {
			Text += FString::Printf(TEXT("%s%s %d"), Text.IsEmpty() ? TEXT("") : TEXT(", "), *Line.Key, Line.Value);
		}
		return Text;
	}
}

ASenniteBaseEditor::ASenniteBaseEditor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ASenniteBaseEditor* ASenniteBaseEditor::Find(const UWorld* World)
{
	if (World == nullptr) {
		return nullptr;
	}
	TActorIterator<ASenniteBaseEditor> It(World);
	return It ? *It : nullptr;
}

bool ASenniteBaseEditor::IsPlanning(const UWorld* World)
{
	const ASenniteBaseEditor* Editor = Find(World);
	return Editor != nullptr && Editor->bEditing;
}

USennitePublisher* ASenniteBaseEditor::Publisher() const
{
	const UGameInstance* Game = GetGameInstance();
	return Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
}

const FSenniteBaseView* ASenniteBaseEditor::Base() const
{
	const USennitePublisher* Server = Publisher();
	return Server != nullptr ? Server->Bases.Find(Parcel) : nullptr;
}

const FSennitePrefab* ASenniteBaseEditor::Chosen() const
{
	const USennitePublisher* Server = Publisher();
	return Server != nullptr && Server->Prefabs.IsValidIndex(PrefabIndex) ? &Server->Prefabs[PrefabIndex] : nullptr;
}

const FSenniteStructureView* ASenniteBaseEditor::Selected() const
{
	const USennitePublisher* Server = Publisher();
	const FSenniteBaseView* View = Base();
	if (Server == nullptr || View == nullptr) {
		return nullptr;
	}
	// the middle of the cursor's cell
	const double PX = Cursor.X + Server->Grid * 0.5;
	const double PY = Cursor.Y + Server->Grid * 0.5;
	for (const FSenniteStructureView& Structure : View->Structures) {
		const FSennitePrefab* Prefab = Server->FindPrefab(Structure.PrefabId);
		if (Prefab == nullptr) {
			continue;
		}
		int32 Width = 0, Depth = 0;
		CoveredBy(*Prefab, Structure.Rotation, Width, Depth);
		if (FMath::Abs(PX - Structure.X) < Width * 0.5 && FMath::Abs(PY - Structure.Y) < Depth * 0.5) {
			return &Structure;
		}
	}
	return nullptr;
}

double ASenniteBaseEditor::GroundAt(double X, double Y) const
{
	// the ground is the level's own geometry; structures and players are not it
	FHitResult Hit;
	FCollisionObjectQueryParams Ground(ECC_WorldStatic);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(X, Y, 20000.0), FVector(X, Y, -20000.0), Ground)) {
		return Hit.ImpactPoint.Z;
	}
	return 0.0;
}

void ASenniteBaseEditor::BindToggle()
{
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (Controller == nullptr || !Controller->IsLocalController()) {
		return;
	}
	ToggleInput = NewObject<UInputComponent>(this);
	ToggleInput->RegisterComponent();
	ToggleInput->bBlockInput = false;
	ToggleInput->BindKey(EKeys::B, IE_Pressed, this, &ASenniteBaseEditor::Toggle);
	Controller->PushInputComponent(ToggleInput);
}

void ASenniteBaseEditor::Toggle()
{
	if (bEditing) {
		Close();
	}
	else {
		Open();
	}
}

void ASenniteBaseEditor::ChooseParcel(const FString& Id)
{
	const USennitePublisher* Server = Publisher();
	const FSenniteParcel* Found = Server != nullptr ? Server->FindParcel(Id) : nullptr;
	if (Found == nullptr || Found->Boundary.Num() == 0) {
		return;
	}
	Parcel = Id;
	FIntPoint Min = Found->Boundary[0], Max = Found->Boundary[0];
	for (const FIntPoint& Corner : Found->Boundary) {
		Min = Min.ComponentMin(Corner);
		Max = Max.ComponentMax(Corner);
	}
	// the cursor starts on the first whole cell of the parcel's first corner
	Cursor = Found->Boundary[0];
	ClearOutline();
	BuildOutline();
	if (Camera != nullptr) {
		const FVector2D Centre((Min.X + Max.X) * 0.5, (Min.Y + Max.Y) * 0.5);
		const FVector2D Extent(Max.X - Min.X, Max.Y - Min.Y);
		// looking straight down with the map's +X up the screen: high enough to see the whole parcel on a wide screen
		const double Height = FMath::Max(Extent.X / 1.125, Extent.Y * 0.5) * 1.25 + 600.0;
		Camera->SetActorLocationAndRotation(FVector(Centre.X, Centre.Y, GroundAt(Centre.X, Centre.Y) + Height), FRotator(-90.0, 0.0, 0.0));
	}
}

void ASenniteBaseEditor::Open()
{
	USennitePublisher* Server = Publisher();
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (Server == nullptr || Controller == nullptr || bEditing) {
		return;
	}
	bEditing = true;
	if (Camera == nullptr) {
		Camera = GetWorld()->SpawnActor<ACameraActor>();
		Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
		Camera->GetCameraComponent()->SetFieldOfView(90.0f);
	}
	// the faction's own parcel; with none, the parcel nearest the player
	FString Start = Server->ParcelId;
	if (Start.IsEmpty() && Server->Parcels.Num() > 0) {
		const APawn* Pawn = Controller->GetPawn();
		const FVector Here = Pawn != nullptr ? Pawn->GetActorLocation() : FVector::ZeroVector;
		double Nearest = TNumericLimits<double>::Max();
		for (const FSenniteParcel& Candidate : Server->Parcels) {
			for (const FIntPoint& Corner : Candidate.Boundary) {
				const double Distance = FVector2D::DistSquared(FVector2D(Here.X, Here.Y), FVector2D(Corner.X, Corner.Y));
				if (Distance < Nearest) {
					Nearest = Distance;
					Start = Candidate.Id;
				}
			}
		}
	}
	ChooseParcel(Start);
	Controller->SetViewTargetWithBlend(Camera, 0.25f);

	EditorInput = NewObject<UInputComponent>(this);
	EditorInput->RegisterComponent();
	// while planning, the keys are the editor's: the hero stands where she is
	EditorInput->bBlockInput = true;
	EditorInput->BindKey(EKeys::B, IE_Pressed, this, &ASenniteBaseEditor::Toggle);
	EditorInput->BindKey(EKeys::W, IE_Pressed, this, &ASenniteBaseEditor::MoveUp);
	EditorInput->BindKey(EKeys::S, IE_Pressed, this, &ASenniteBaseEditor::MoveDown);
	EditorInput->BindKey(EKeys::A, IE_Pressed, this, &ASenniteBaseEditor::MoveLeft);
	EditorInput->BindKey(EKeys::D, IE_Pressed, this, &ASenniteBaseEditor::MoveRight);
	EditorInput->BindKey(EKeys::E, IE_Pressed, this, &ASenniteBaseEditor::NextPrefab);
	EditorInput->BindKey(EKeys::Q, IE_Pressed, this, &ASenniteBaseEditor::PreviousPrefab);
	EditorInput->BindKey(EKeys::R, IE_Pressed, this, &ASenniteBaseEditor::Turn);
	EditorInput->BindKey(EKeys::F, IE_Pressed, this, &ASenniteBaseEditor::Place);
	EditorInput->BindKey(EKeys::N, IE_Pressed, this, &ASenniteBaseEditor::Found);
	EditorInput->BindKey(EKeys::Tab, IE_Pressed, this, &ASenniteBaseEditor::NextParcel);
	EditorInput->BindKey(EKeys::C, IE_Pressed, this, &ASenniteBaseEditor::Commit);
	EditorInput->BindKey(EKeys::One, IE_Pressed, this, &ASenniteBaseEditor::NextNeed);
	EditorInput->BindKey(EKeys::Z, IE_Pressed, this, &ASenniteBaseEditor::NextAmount);
	EditorInput->BindKey(EKeys::T, IE_Pressed, this, &ASenniteBaseEditor::TakeBack);
	EditorInput->BindKey(EKeys::V, IE_Pressed, this, &ASenniteBaseEditor::ReplaceSelected);
	EditorInput->BindKey(EKeys::X, IE_Pressed, this, &ASenniteBaseEditor::Dismantle);
	EditorInput->BindKey(EKeys::G, IE_Pressed, this, &ASenniteBaseEditor::Rebuild);
	EditorInput->BindKey(EKeys::O, IE_Pressed, this, &ASenniteBaseEditor::CopyCode);
	EditorInput->BindKey(EKeys::M, IE_Pressed, this, &ASenniteBaseEditor::ProveMaterials);
	EditorInput->BindKey(EKeys::P, IE_Pressed, this, &ASenniteBaseEditor::TogglePeople);
	EditorInput->BindKey(EKeys::J, IE_Pressed, this, &ASenniteBaseEditor::Join);
	EditorInput->BindKey(EKeys::I, IE_Pressed, this, &ASenniteBaseEditor::InvitePerson);
	EditorInput->BindKey(EKeys::K, IE_Pressed, this, &ASenniteBaseEditor::VotePerson);
	EditorInput->BindKey(EKeys::U, IE_Pressed, this, &ASenniteBaseEditor::UnvotePerson);
	EditorInput->BindKey(EKeys::L, IE_Pressed, this, &ASenniteBaseEditor::LeaveFaction);
	Controller->PushInputComponent(EditorInput);
}

void ASenniteBaseEditor::Close()
{
	if (!bEditing) {
		return;
	}
	bEditing = false;
	bPeople = false;
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController()) {
		if (EditorInput != nullptr) {
			Controller->PopInputComponent(EditorInput);
		}
		if (APawn* Pawn = Controller->GetPawn()) {
			Controller->SetViewTargetWithBlend(Pawn, 0.25f);
		}
	}
	if (EditorInput != nullptr) {
		EditorInput->DestroyComponent();
		EditorInput = nullptr;
	}
	ClearOutline();
	UpdateCursorTile();
	if (Ghost != nullptr) {
		Ghost->Destroy();
		Ghost = nullptr;
		GhostSignature.Reset();
	}
}

void ASenniteBaseEditor::EndPlay(const EEndPlayReason::Type Reason)
{
	Close();
	Super::EndPlay(Reason);
}

void ASenniteBaseEditor::Move(int32 DX, int32 DY)
{
	const USennitePublisher* Server = Publisher();
	const FSenniteParcel* Found = Server != nullptr ? Server->FindParcel(Parcel) : nullptr;
	if (Found == nullptr || Found->Boundary.Num() == 0) {
		return;
	}
	FIntPoint Min = Found->Boundary[0], Max = Found->Boundary[0];
	for (const FIntPoint& Corner : Found->Boundary) {
		Min = Min.ComponentMin(Corner);
		Max = Max.ComponentMax(Corner);
	}
	// the cursor stays over the parcel's ground; whether a placement there is valid is not the cursor's to say
	Cursor.X = FMath::Clamp(Cursor.X + DX * Server->Grid, Min.X, Max.X - Server->Grid);
	Cursor.Y = FMath::Clamp(Cursor.Y + DY * Server->Grid, Min.Y, Max.Y - Server->Grid);
	NeedIndex = 0;
}

// the camera looks straight down with the map's +X up the screen and +Y to the right
void ASenniteBaseEditor::MoveUp()
{
	if (bPeople) {
		PersonIndex = FMath::Max(0, PersonIndex - 1);
	}
	else {
		Move(1, 0);
	}
}

void ASenniteBaseEditor::MoveDown()
{
	if (bPeople) {
		PersonIndex = FMath::Min(FMath::Max(0, PeopleRows().Num() - 1), PersonIndex + 1);
	}
	else {
		Move(-1, 0);
	}
}
void ASenniteBaseEditor::MoveLeft() { Move(0, -1); }
void ASenniteBaseEditor::MoveRight() { Move(0, 1); }

void ASenniteBaseEditor::NextPrefab()
{
	const USennitePublisher* Server = Publisher();
	if (Server != nullptr && Server->Prefabs.Num() > 0) {
		PrefabIndex = (PrefabIndex + 1) % Server->Prefabs.Num();
	}
}

void ASenniteBaseEditor::PreviousPrefab()
{
	const USennitePublisher* Server = Publisher();
	if (Server != nullptr && Server->Prefabs.Num() > 0) {
		PrefabIndex = (PrefabIndex + Server->Prefabs.Num() - 1) % Server->Prefabs.Num();
	}
}

void ASenniteBaseEditor::Turn()
{
	Rotation = (Rotation + 90) % 360;
}

void ASenniteBaseEditor::Place()
{
	USennitePublisher* Server = Publisher();
	const FSennitePrefab* Prefab = Chosen();
	if (Server == nullptr || Prefab == nullptr || Selected() != nullptr) {
		return;
	}
	int32 Width = 0, Depth = 0;
	CoveredBy(*Prefab, Rotation, Width, Depth);
	// the cursor is the near corner of what the prefab will cover; a placement names its centre
	Server->Place(Prefab->Id, Cursor.X + Width / 2, Cursor.Y + Depth / 2, Rotation);
}

void ASenniteBaseEditor::Found()
{
	USennitePublisher* Server = Publisher();
	const FSenniteBaseView* View = Base();
	if (Server != nullptr && View != nullptr && !View->bAssigned) {
		Server->Found(Parcel);
	}
}

void ASenniteBaseEditor::NextParcel()
{
	const USennitePublisher* Server = Publisher();
	if (Server == nullptr || Server->Parcels.Num() == 0) {
		return;
	}
	const int32 Now = Server->Parcels.IndexOfByPredicate([this](const FSenniteParcel& P) { return P.Id == Parcel; });
	ChooseParcel(Server->Parcels[(Now + 1) % Server->Parcels.Num()].Id);
}

bool ASenniteBaseEditor::CommitChoice(FString& Resource, int32& Amount) const
{
	const FSenniteStructureView* Structure = Selected();
	if (Structure == nullptr) {
		return false;
	}
	TArray<const FSenniteNeed*> Unmet;
	for (const FSenniteNeed& Need : Structure->Needs) {
		if (Need.Shortfall > 0) {
			Unmet.Add(&Need);
		}
	}
	if (Unmet.Num() == 0) {
		return false;
	}
	const FSenniteNeed* Need = Unmet[NeedIndex % Unmet.Num()];
	const int32 Part = Amounts[AmountIndex % UE_ARRAY_COUNT(Amounts)];
	Resource = Need->Resource;
	Amount = Part == 0 ? Need->Shortfall : FMath::Min(Part, Need->Shortfall);
	return true;
}

void ASenniteBaseEditor::Commit()
{
	USennitePublisher* Server = Publisher();
	const FSenniteStructureView* Structure = Selected();
	FString Resource;
	int32 Amount = 0;
	if (Server != nullptr && Structure != nullptr && CommitChoice(Resource, Amount)) {
		Server->Commit(Structure->InstanceId, Resource, Amount);
	}
}

void ASenniteBaseEditor::NextNeed() { ++NeedIndex; }
void ASenniteBaseEditor::NextAmount() { AmountIndex = (AmountIndex + 1) % UE_ARRAY_COUNT(Amounts); }

void ASenniteBaseEditor::TakeBack()
{
	USennitePublisher* Server = Publisher();
	if (const FSenniteStructureView* Structure = Selected(); Server != nullptr && Structure != nullptr) {
		Server->Withdraw(Structure->InstanceId);
	}
}

void ASenniteBaseEditor::ReplaceSelected()
{
	USennitePublisher* Server = Publisher();
	const FSennitePrefab* Prefab = Chosen();
	if (const FSenniteStructureView* Structure = Selected(); Server != nullptr && Structure != nullptr && Prefab != nullptr && Prefab->Id != Structure->PrefabId) {
		Server->Replace(Structure->InstanceId, Prefab->Id);
	}
}

void ASenniteBaseEditor::Dismantle()
{
	USennitePublisher* Server = Publisher();
	if (bPeople) {
		// on the people page the same key withdraws an invitation
		FPersonRow Row;
		if (Server != nullptr && ChosenPerson(Row) && Row.Kind == FPersonRow::EKind::Invited) {
			Server->Uninvite(Row.Person->SigningKey);
		}
		return;
	}
	if (const FSenniteStructureView* Structure = Selected(); Server != nullptr && Structure != nullptr) {
		Server->Dismantle(Structure->InstanceId);
	}
}

void ASenniteBaseEditor::Rebuild()
{
	USennitePublisher* Server = Publisher();
	if (const FSenniteStructureView* Structure = Selected(); Server != nullptr && Structure != nullptr && Structure->bDestroyed) {
		Server->Confirm(Structure->InstanceId);
	}
}

void ASenniteBaseEditor::CopyCode()
{
	USennitePublisher* Server = Publisher();
	if (Server == nullptr || Server->ConnectCode.IsEmpty()) {
		return;
	}
	FPlatformApplicationMisc::ClipboardCopy(*Server->ConnectCode);
#if PLATFORM_ANDROID || PLATFORM_IOS
	// on a phone the code is a link the DSM wallet opens
	FPlatformProcess::LaunchURL(*Server->ConnectCode, nullptr, nullptr);
#endif
	Server->Message = TEXT("the connect code is copied: read it in your DSM wallet");
}

void ASenniteBaseEditor::ProveMaterials()
{
	if (USennitePublisher* Server = Publisher()) {
		Server->ProveMaterials();
	}
}

TArray<ASenniteBaseEditor::FPersonRow> ASenniteBaseEditor::PeopleRows() const
{
	TArray<FPersonRow> Rows;
	const USennitePublisher* Server = Publisher();
	const FSenniteBaseView* View = Base();
	if (Server == nullptr || View == nullptr || !View->bMember) {
		return Rows;
	}
	for (const FSennitePerson& Person : View->People) {
		Rows.Add({ FPersonRow::EKind::Member, &Person });
	}
	for (const FSennitePerson& Person : View->Invited) {
		Rows.Add({ FPersonRow::EKind::Invited, &Person });
	}
	for (const FSennitePerson& Person : Server->FreePlayers) {
		// someone already invited here is listed as invited
		if (!View->Invited.ContainsByPredicate([&Person](const FSennitePerson& I) { return I.Wallet == Person.Wallet; })) {
			Rows.Add({ FPersonRow::EKind::Free, &Person });
		}
	}
	return Rows;
}

bool ASenniteBaseEditor::ChosenPerson(FPersonRow& Row) const
{
	const TArray<FPersonRow> Rows = PeopleRows();
	if (!bPeople || Rows.Num() == 0) {
		return false;
	}
	Row = Rows[FMath::Clamp(PersonIndex, 0, Rows.Num() - 1)];
	return true;
}

void ASenniteBaseEditor::TogglePeople()
{
	const FSenniteBaseView* View = Base();
	// a faction's people are its members' to see
	bPeople = !bPeople && View != nullptr && View->bMember;
	PersonIndex = 0;
}

void ASenniteBaseEditor::Join()
{
	USennitePublisher* Server = Publisher();
	const FSenniteBaseView* View = Base();
	if (Server != nullptr && View != nullptr && View->bInvited) {
		Server->Accept(View->FactionId);
	}
}

void ASenniteBaseEditor::InvitePerson()
{
	USennitePublisher* Server = Publisher();
	FPersonRow Row;
	if (Server != nullptr && ChosenPerson(Row) && Row.Kind == FPersonRow::EKind::Free) {
		Server->Invite(Row.Person->Wallet);
	}
}

void ASenniteBaseEditor::VotePerson()
{
	USennitePublisher* Server = Publisher();
	FPersonRow Row;
	if (Server != nullptr && ChosenPerson(Row) && Row.Kind == FPersonRow::EKind::Member && !Row.Person->bYou && !Row.Person->bVoted) {
		Server->Vote(Row.Person->SigningKey);
	}
}

void ASenniteBaseEditor::UnvotePerson()
{
	USennitePublisher* Server = Publisher();
	FPersonRow Row;
	if (Server != nullptr && ChosenPerson(Row) && Row.Kind == FPersonRow::EKind::Member && Row.Person->bVoted) {
		Server->Unvote(Row.Person->SigningKey);
	}
}

void ASenniteBaseEditor::LeaveFaction()
{
	USennitePublisher* Server = Publisher();
	FPersonRow Row;
	if (Server != nullptr && ChosenPerson(Row) && Row.Kind == FPersonRow::EKind::Member && Row.Person->bYou) {
		Server->Leave();
	}
}

void ASenniteBaseEditor::BuildOutline()
{
	const USennitePublisher* Server = Publisher();
	const FSenniteParcel* Found = Server != nullptr ? Server->FindParcel(Parcel) : nullptr;
	if (Found == nullptr) {
		return;
	}
	const ABuildingActor* Pieces = GetDefault<ABuildingActor>();
	UStaticMesh* Cube = Cast<UStaticMesh>(Pieces->PieceShape.TryLoad());
	UMaterialInterface* Face = Cast<UMaterialInterface>(Pieces->PreviewMaterial.TryLoad());
	for (int32 Index = 0; Index < Found->Boundary.Num(); ++Index) {
		const FIntPoint A = Found->Boundary[Index];
		const FIntPoint B = Found->Boundary[(Index + 1) % Found->Boundary.Num()];
		const FVector2D From(A.X, A.Y), To(B.X, B.Y);
		const FVector2D Middle = (From + To) * 0.5;
		UStaticMeshComponent* Edge = NewObject<UStaticMeshComponent>(this);
		Edge->SetupAttachment(RootComponent);
		Edge->SetStaticMesh(Cube);
		Edge->SetMaterial(0, Face);
		Edge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Edge->SetCastShadow(false);
		Edge->SetWorldLocationAndRotation(FVector(Middle.X, Middle.Y, GroundAt(Middle.X, Middle.Y) + 6.0),
			FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(To.Y - From.Y, To.X - From.X)), 0.0));
		Edge->SetWorldScale3D(FVector(FVector2D::Distance(From, To) / 100.0, 0.6, 0.1));
		Edge->RegisterComponent();
		Outline.Add(Edge);
	}
}

void ASenniteBaseEditor::ClearOutline()
{
	for (UStaticMeshComponent* Edge : Outline) {
		if (Edge != nullptr) {
			Edge->DestroyComponent();
		}
	}
	Outline.Reset();
}

void ASenniteBaseEditor::SyncStructures()
{
	const USennitePublisher* Server = Publisher();
	if (Server == nullptr) {
		return;
	}
	TSet<FString> Wanted;
	for (const TPair<FString, FSenniteBaseView>& Entry : Server->Bases) {
		for (const FSenniteStructureView& View : Entry.Value.Structures) {
			// what stands is drawn for everyone; a plan only in the editor, on the parcel being planned
			if (!View.bStanding && !(bEditing && Entry.Key == Parcel)) {
				continue;
			}
			const FSennitePrefab* Prefab = Server->FindPrefab(View.PrefabId);
			if (Prefab == nullptr) {
				continue;
			}
			Wanted.Add(View.InstanceId);
			const FString Signature = FString::Printf(TEXT("%s|%d|%d|%d|%d"), *View.PrefabId, View.X, View.Y, View.Rotation, View.bStanding ? 1 : 0);
			ASenniteStructure* Structure = Structures.FindRef(View.InstanceId);
			if (Structure != nullptr && Signatures.FindRef(View.InstanceId) == Signature) {
				continue;
			}
			if (Structure == nullptr) {
				Structure = GetWorld()->SpawnActor<ASenniteStructure>();
				Structures.Add(View.InstanceId, Structure);
			}
			Structure->SetActorLocationAndRotation(FVector(View.X, View.Y, GroundAt(View.X, View.Y)), FRotator(0.0, View.Rotation, 0.0));
			Structure->Build(View.PrefabId, Prefab->Width, Prefab->Depth, View.bStanding);
			Signatures.Add(View.InstanceId, Signature);
		}
	}
	for (auto It = Structures.CreateIterator(); It; ++It) {
		if (!Wanted.Contains(It.Key())) {
			if (It.Value() != nullptr) {
				It.Value()->Destroy();
			}
			Signatures.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}

void ASenniteBaseEditor::UpdateCursorTile()
{
	const USennitePublisher* Server = Publisher();
	if (!bEditing || Server == nullptr) {
		if (CursorTile != nullptr) {
			CursorTile->DestroyComponent();
			CursorTile = nullptr;
		}
		return;
	}
	if (CursorTile == nullptr) {
		const ABuildingActor* Pieces = GetDefault<ABuildingActor>();
		CursorTile = NewObject<UStaticMeshComponent>(this);
		CursorTile->SetupAttachment(RootComponent);
		CursorTile->SetStaticMesh(Cast<UStaticMesh>(Pieces->PieceShape.TryLoad()));
		CursorTile->SetMaterial(0, Cast<UMaterialInterface>(Pieces->PreviewMaterial.TryLoad()));
		CursorTile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CursorTile->SetCastShadow(false);
		CursorTile->RegisterComponent();
	}
	// the cell the cursor is on, lying on the ground
	const double X = Cursor.X + Server->Grid * 0.5;
	const double Y = Cursor.Y + Server->Grid * 0.5;
	CursorTile->SetWorldLocation(FVector(X, Y, GroundAt(X, Y) + 4.0));
	CursorTile->SetWorldScale3D(FVector(Server->Grid / 100.0, Server->Grid / 100.0, 0.08));
}

void ASenniteBaseEditor::UpdateGhost()
{
	const FSennitePrefab* Prefab = Chosen();
	const FSenniteBaseView* View = Base();
	// the prefab to place shows where it would go, on free ground of a parcel the player may plan
	const bool bShow = bEditing && !bPeople && Prefab != nullptr && View != nullptr && View->bMember && Selected() == nullptr;
	if (!bShow) {
		if (Ghost != nullptr) {
			Ghost->Destroy();
			Ghost = nullptr;
			GhostSignature.Reset();
		}
		return;
	}
	int32 Width = 0, Depth = 0;
	CoveredBy(*Prefab, Rotation, Width, Depth);
	const double X = Cursor.X + Width * 0.5;
	const double Y = Cursor.Y + Depth * 0.5;
	if (Ghost == nullptr) {
		Ghost = GetWorld()->SpawnActor<ASenniteStructure>();
	}
	if (GhostSignature != Prefab->Id) {
		Ghost->Build(Prefab->Id, Prefab->Width, Prefab->Depth, false);
		GhostSignature = Prefab->Id;
	}
	Ghost->SetActorLocationAndRotation(FVector(X, Y, GroundAt(X, Y)), FRotator(0.0, Rotation, 0.0));
}

void ASenniteBaseEditor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (ToggleInput == nullptr) {
		BindToggle();
	}
	SyncStructures();
	UpdateGhost();
	UpdateCursorTile();
}

void ASenniteBaseEditor::DrawOn(AHUD* Hud, UCanvas* Canvas)
{
	ASenniteBaseEditor* Editor = Hud != nullptr ? Find(Hud->GetWorld()) : nullptr;
	if (Editor == nullptr || Canvas == nullptr) {
		return;
	}
	Editor->LastDrawn.Reset();
	const USennitePublisher* Server = Editor->Publisher();
	UFont* Font = GEngine->GetLargeFont();
	if (!Editor->bEditing || Server == nullptr || Font == nullptr) {
		return;
	}
	// every size is for a screen 720 lines high and scales with the screen, phone or desktop
	const float S = Canvas->ClipY / 720.0f;
	const float Left = 24.0f * S;
	float Y = 20.0f * S;
	const FLinearColor White = FLinearColor::White;
	const FLinearColor Dim(1.0f, 1.0f, 1.0f, 0.7f);
	const FLinearColor Good(0.45f, 0.95f, 0.45f);
	const FLinearColor Short(1.0f, 0.75f, 0.25f);

	// the lines are gathered first, so the plate behind them is as tall as what it holds
	struct FPanelLine { FString Text; int32 Points; FLinearColor Colour; };
	TArray<FPanelLine> Lines;
	auto Line = [&](const TCHAR* Name, const FString& Text, int32 Points, const FLinearColor& Colour) {
		Lines.Add({ Text, Points, Colour });
		Editor->LastDrawn.Add(FString::Printf(TEXT("%s=%s"), Name, *Text));
	};
	auto Draw = [&]() {
		float Height = 0.0f, Width = 0.0f;
		for (const FPanelLine& One : Lines) {
			const float High = One.Points * 1.4f * S;
			float TextWidth = 0.0f, TextHeight = 0.0f;
			Canvas->TextSize(Font, One.Text, TextWidth, TextHeight, High / FMath::Max(1.0f, Font->GetMaxCharHeight()), High / FMath::Max(1.0f, Font->GetMaxCharHeight()));
			Width = FMath::Max(Width, TextWidth);
			Height += High * 1.25f;
		}
		FCanvasTileItem Plate(FVector2D(Left - 10.0f * S, Y - 8.0f * S), FVector2D(Width + 20.0f * S, Height + 12.0f * S), FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
		Plate.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Plate);
		for (const FPanelLine& One : Lines) {
			const float High = One.Points * 1.4f * S;
			FCanvasTextItem Item(FVector2D(Left, Y), FText::FromString(One.Text), Font, One.Colour);
			Item.Scale = FVector2D(High / FMath::Max(1.0f, Font->GetMaxCharHeight()));
			Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
			Canvas->DrawItem(Item);
			Y += High * 1.25f;
		}
	};

	Line(TEXT("title"), FString::Printf(TEXT("BASE EDITOR  %s"), *Editor->Parcel.ToUpper()), 16, White);
	if (!Server->HasWorld()) {
		Line(TEXT("server"), Server->Message.IsEmpty() ? FString(TEXT("reaching the publisher's server...")) : Server->Message, 11, Short);
		Draw();
		return;
	}

	if (!Server->bConnected) {
		Line(TEXT("wallet"), TEXT("WALLET NOT CONNECTED"), 12, Short);
		if (Server->ConnectCode.IsEmpty()) {
			Line(TEXT("code"), TEXT("asking for a connect code..."), 10, Dim);
		}
		else {
			Line(TEXT("connect"), TEXT("Read this code in your DSM wallet (O copies it):"), 10, Dim);
			// the code is long: in rows the panel holds
			const int32 Row = 56;
			for (int32 At = 0, Index = 0; At < Server->ConnectCode.Len() && Index < 8; At += Row, ++Index) {
				Line(*FString::Printf(TEXT("code%d"), Index), Server->ConnectCode.Mid(At, Row), 8, White);
			}
		}
	}
	else {
		Line(TEXT("wallet"), FString::Printf(TEXT("WALLET %s"), *Server->Wallet.Left(8)), 12, Good);
		if (Server->bMaterialsKnown) {
			FString Held;
			for (const FString& Resource : Server->Resources) {
				Held += FString::Printf(TEXT("%s%s %d"), Held.IsEmpty() ? TEXT("") : TEXT("   "), *Resource, Server->Materials.FindRef(Resource));
			}
			Line(TEXT("materials"), Held, 11, White);
		}
		else {
			Line(TEXT("materials"), TEXT("M: show what the wallet holds"), 10, Dim);
		}
	}

	const FSenniteBaseView* View = Editor->Base();
	if (View == nullptr || !View->bKnown) {
		Line(TEXT("faction"), TEXT("reading the parcel..."), 11, Dim);
	}
	else if (!View->bAssigned) {
		Line(TEXT("faction"), Server->bConnected ? TEXT("UNASSIGNED PARCEL   N: found a faction here") : TEXT("UNASSIGNED PARCEL"), 11, White);
	}
	else if (View->bMember) {
		Line(TEXT("faction"), FString::Printf(TEXT("YOUR FACTION   %d member%s   %d change%s"), View->Members, View->Members == 1 ? TEXT("") : TEXT("s"),
			View->Height, View->Height == 1 ? TEXT("") : TEXT("s")), 11, Good);
	}
	else if (View->bInvited) {
		Line(TEXT("faction"), TEXT("YOU ARE INVITED   J: join"), 11, Good);
	}
	else {
		Line(TEXT("faction"), TEXT("ANOTHER FACTION'S PARCEL"), 11, Dim);
	}

	if (Editor->bPeople && View != nullptr && View->bMember) {
		// the faction's people, the chosen one marked
		const TArray<FPersonRow> Rows = Editor->PeopleRows();
		const int32 At = FMath::Clamp(Editor->PersonIndex, 0, FMath::Max(0, Rows.Num() - 1));
		Line(TEXT("page"), TEXT("PEOPLE"), 13, White);
		int32 Counts[3] = { 0, 0, 0 };
		for (int32 Index = 0; Index < Rows.Num(); ++Index) {
			const FPersonRow& Row = Rows[Index];
			const TCHAR* Mark = Index == At ? TEXT("> ") : TEXT("  ");
			const FString Name = Row.Person->Wallet.Left(8);
			const int32 Kind = (int32)Row.Kind;
			if (Row.Kind == FPersonRow::EKind::Member) {
				const FString Votes = Row.Person->Votes == 0 ? FString()
					: FString::Printf(TEXT("   %d vote%s to remove%s"), Row.Person->Votes, Row.Person->Votes == 1 ? TEXT("") : TEXT("s"), Row.Person->bVoted ? TEXT(" (yours)") : TEXT(""));
				Line(*FString::Printf(TEXT("member%d"), Counts[Kind]++), FString::Printf(TEXT("%s%s%s   member%s"), Mark, Row.Person->bYou ? TEXT("YOU  ") : TEXT(""), *Name, *Votes), 11, Index == At ? White : Dim);
			}
			else if (Row.Kind == FPersonRow::EKind::Invited) {
				Line(*FString::Printf(TEXT("invited%d"), Counts[Kind]++), FString::Printf(TEXT("%s%s   invited"), Mark, *Name), 11, Index == At ? White : Dim);
			}
			else {
				Line(*FString::Printf(TEXT("free%d"), Counts[Kind]++), FString::Printf(TEXT("%s%s   in no faction"), Mark, *Name), 11, Index == At ? White : Dim);
			}
		}
		if (Rows.IsValidIndex(At)) {
			const FPersonRow& Row = Rows[At];
			Line(TEXT("keys"), Row.Kind == FPersonRow::EKind::Free ? TEXT("I: invite   W S: choose   P: back")
				: Row.Kind == FPersonRow::EKind::Invited ? TEXT("X: withdraw the invitation   W S: choose   P: back")
				: Row.Person->bYou ? TEXT("L: leave the faction   W S: choose   P: back")
				: Row.Person->bVoted ? TEXT("U: take your vote back   W S: choose   P: back")
				: TEXT("K: vote to remove   W S: choose   P: back"), 10, Dim);
		}
		if (!Server->Message.IsEmpty()) {
			Line(TEXT("message"), Server->Message, 11, Server->bBusy ? Dim : Server->bLastOk ? Good : Short);
		}
		Draw();
		return;
	}

	const FSennitePrefab* Prefab = Editor->Chosen();
	const FSenniteStructureView* Structure = Editor->Selected();
	if (Structure != nullptr) {
		const FSennitePrefab* Is = Server->FindPrefab(Structure->PrefabId);
		Line(TEXT("structure"), FString::Printf(TEXT("%s   %s"), Is != nullptr ? *Is->Name.ToUpper() : *Structure->PrefabId,
			Structure->bStanding ? TEXT("STANDING") : Structure->bDestroyed ? TEXT("DESTROYED IN BATTLE") : TEXT("PLAN")), 13, White);
		Line(TEXT("status"), Structure->Status.ToUpper(), 11, Structure->Status == TEXT("fully funded") ? Good : Short);
		for (int32 Index = 0; Index < Structure->Needs.Num(); ++Index) {
			const FSenniteNeed& Need = Structure->Needs[Index];
			Line(*FString::Printf(TEXT("need%d"), Index), Need.Shortfall > 0
				? FString::Printf(TEXT("%s %d of %d   needs %d"), *Need.Resource, Need.Funded, Need.Needed, Need.Shortfall)
				: FString::Printf(TEXT("%s %d of %d"), *Need.Resource, Need.Funded, Need.Needed), 11, Need.Shortfall > 0 ? Short : Good);
		}
		for (int32 Index = 0; Index < Structure->Committed.Num() && Index < 6; ++Index) {
			const FSenniteCommitted& One = Structure->Committed[Index];
			Line(*FString::Printf(TEXT("committed%d"), Index), FString::Printf(TEXT("%s committed %d %s%s"), One.bMine ? TEXT("you") : *One.Owner.Left(8),
				One.Amount, *One.Resource, One.bLeaving ? TEXT("  (leaving)") : TEXT("")), 10, Dim);
		}
		if (View != nullptr && View->bMember) {
			FString Resource;
			int32 Amount = 0;
			if (Editor->CommitChoice(Resource, Amount)) {
				Line(TEXT("commit"), FString::Printf(TEXT("C: commit %d %s   (1: resource, Z: amount)"), Amount, *Resource), 11, White);
			}
			Line(TEXT("keys"), Structure->bDestroyed ? TEXT("G: rebuild   T: take back   V: replace   X: dismantle") : TEXT("T: take back   V: replace   X: dismantle"), 10, Dim);
		}
	}
	else if (Prefab != nullptr) {
		Line(TEXT("prefab"), FString::Printf(TEXT("%s   %s"), *Prefab->Name.ToUpper(), *RecipeOf(*Prefab)), 13, White);
		Line(TEXT("keys"), View != nullptr && View->bMember ? TEXT("F: place   Q E: prefab   R: turn   W A S D: move   P: people") : TEXT("W A S D: move   TAB: next parcel"), 10, Dim);
	}

	if (!Server->Message.IsEmpty()) {
		Line(TEXT("message"), Server->Message, 11, Server->bBusy ? Dim : Server->bLastOk ? Good : Short);
	}
	Draw();
}

bool USenniteBaseWorld::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USenniteBaseWorld::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	const UGameInstance* Game = World.GetGameInstance();
	const USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
	// with no publisher's server set, the game has no faction bases and plays as it did
	if (Server != nullptr && Server->IsOn()) {
		World.SpawnActor<ASenniteBaseEditor>();
	}
}
