#include "SenniteMatchBases.h"

#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "SenniteMatch.h"
#include "SennitePublisher.h"
#include "SenniteStructure.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenniteMatchBases, Log, All);

ASenniteMatchBases::ASenniteMatchBases()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASenniteMatchBases::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASenniteMatchBases, MatchId);
	DOREPLIFETIME(ASenniteMatchBases, Structures);
	DOREPLIFETIME(ASenniteMatchBases, State);
	DOREPLIFETIME(ASenniteMatchBases, Why);
}

ASenniteMatchBases* ASenniteMatchBases::Find(const UWorld* World)
{
	if (World == nullptr) {
		return nullptr;
	}
	TActorIterator<ASenniteMatchBases> It(World);
	return It ? *It : nullptr;
}

ASenniteStructure* ASenniteMatchBases::ActorOf(const FString& InstanceId) const
{
	const TObjectPtr<ASenniteStructure>* Found = Actors.Find(InstanceId);
	return Found != nullptr ? Found->Get() : nullptr;
}

void ASenniteMatchBases::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) {
		// a machine that joined is sent the bases; it may have them already
		Sync();
		return;
	}
	if (const TCHAR* Named = GetWorld()->URL.GetOption(TEXT("Match="), nullptr)) {
		MatchId = Named;
	}
	Ask();
}

void ASenniteMatchBases::Fail(const FString& InWhy)
{
	Why = InWhy;
	State = EMatchBasesState::Failed;
	UE_LOG(LogSenniteMatchBases, Error, TEXT("Match %s has no bases: %s"), *MatchId, *Why);
	if (ASenniteMatch* Match = ASenniteMatch::Find(GetWorld())) {
		Match->Fail(Why);
	}
}

void ASenniteMatchBases::Ask()
{
	USennitePublisher* Server = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
	if (MatchId.IsEmpty()) {
		Fail(TEXT("the map's address names no match (?Match=)"));
		return;
	}
	if (Server == nullptr || !Server->IsOn() || !Server->IsMatchServer()) {
		Fail(TEXT("this game is not the publisher's match server (-PublisherUrl= and -MatchSecretFile=)"));
		return;
	}
	++Asked;
	Server->AskMatchBases(MatchId, [this, Alive = TWeakObjectPtr<ASenniteMatchBases>(this)](bool bOk, const FString& Said, const TArray<FSenniteMatchBaseView>& Bases) {
		if (!Alive.IsValid()) {
			return;
		}
		if (bOk) {
			Place(Bases);
			return;
		}
		// the publisher's server may not be up yet, or busy: ask again, a bounded number of times
		if (Asked >= 30) {
			Fail(FString::Printf(TEXT("the publisher's server did not give the match's bases: %s"), *Said));
			return;
		}
		FTimerHandle Again;
		GetWorldTimerManager().SetTimer(Again, this, &ASenniteMatchBases::Ask, 2.0f, false);
	});
}

void ASenniteMatchBases::Place(const TArray<FSenniteMatchBaseView>& Bases)
{
	TActorIterator<ASenniteArena> Arena(GetWorld());
	if (!Arena || !Arena->IsBuilt()) {
		Fail(TEXT("this map has no arena"));
		return;
	}
	const FArenaPlan& Plan = Arena->GetPlan();
	if (Bases.Num() != Plan.Bases.Num()) {
		Fail(FString::Printf(TEXT("the match has %d factions and the arena %d bases (?Bases=)"), Bases.Num(), Plan.Bases.Num()));
		return;
	}
	TArray<FSenniteMatchStructure> Set;
	for (int32 Base = 0; Base < Bases.Num(); ++Base) {
		const FSenniteMatchBaseView& View = Bases[Base];
		if (View.Boundary.Num() < 3) {
			Fail(FString::Printf(TEXT("parcel %s has no boundary"), *View.ParcelId));
			return;
		}
		// an empty parcel does not appear in a match (spec 10.3)
		if (View.Structures.Num() == 0) {
			Fail(FString::Printf(TEXT("faction %s has nothing built on parcel %s"), *View.FactionId, *View.ParcelId));
			return;
		}
		// the parcel is set on the base by the middle of what it spans, as it lies on the map: not turned
		FIntPoint Min = View.Boundary[0];
		FIntPoint Max = View.Boundary[0];
		for (const FIntPoint& Corner : View.Boundary) {
			Min = FIntPoint(FMath::Min(Min.X, Corner.X), FMath::Min(Min.Y, Corner.Y));
			Max = FIntPoint(FMath::Max(Max.X, Corner.X), FMath::Max(Max.Y, Corner.Y));
		}
		const FVector2D Middle((Min.X + Max.X) * 0.5, (Min.Y + Max.Y) * 0.5);
		for (const FIntPoint& Corner : View.Boundary) {
			// a faction's buildings stand on the level pad in the middle of its plot; past it the land is hills
			if (FVector2D::Distance(FVector2D(Corner.X, Corner.Y), Middle) > Arena->PadRadius) {
				Fail(FString::Printf(TEXT("parcel %s does not fit on the level ground of a plot of the arena"), *View.ParcelId));
				return;
			}
		}
		for (const FSenniteMatchStructureView& Built : View.Structures) {
			FSenniteMatchStructure Entry;
			Entry.Base = Base;
			Entry.FactionId = View.FactionId;
			Entry.InstanceId = Built.InstanceId;
			Entry.PrefabId = Built.PrefabId;
			Entry.Location = Plan.Bases[Base] + FVector(Built.X - Middle.X, Built.Y - Middle.Y, 0.0);
			Entry.Yaw = Built.Rotation;
			Entry.Width = Built.Width;
			Entry.Depth = Built.Depth;
			Set.Add(Entry);
			Health.Add(Built.InstanceId, StructureHealth);
		}
	}
	Structures = MoveTemp(Set);
	State = EMatchBasesState::Placed;
	UE_LOG(LogSenniteMatchBases, Display, TEXT("Match %s: %d structures of %d factions set on the arena"), *MatchId, Structures.Num(), Bases.Num());
	Sync();
	if (ASenniteMatch* Match = ASenniteMatch::Find(GetWorld())) {
		TArray<FString> InOrder;
		for (const FSenniteMatchBaseView& View : Bases) {
			InOrder.Add(View.FactionId);
		}
		// what each structure takes up of its base: a circle around the ground it covers
		TArray<TPair<FVector, float>> Built;
		for (const FSenniteMatchStructure& Entry : Structures) {
			Built.Emplace(Entry.Location, 0.5f * FMath::Sqrt(static_cast<float>(Entry.Width * Entry.Width + Entry.Depth * Entry.Depth)));
		}
		Match->BasesPlaced(InOrder, Built);
	}
}

void ASenniteMatchBases::OnRep_Structures()
{
	Sync();
}

void ASenniteMatchBases::Sync()
{
	UWorld* World = GetWorld();
	if (World == nullptr || !World->IsGameWorld()) {
		return;
	}
	for (const FSenniteMatchStructure& Entry : Structures) {
		ASenniteStructure* Actor = ActorOf(Entry.InstanceId);
		if (Entry.bDestroyed) {
			if (Actor != nullptr) {
				Actor->Destroy();
			}
			Actors.Remove(Entry.InstanceId);
			continue;
		}
		if (Actor != nullptr) {
			continue;
		}
		// every machine builds it under the same name, so the server can name it to a joined machine
		FActorSpawnParameters Spawn;
		Spawn.Name = *FString::Printf(TEXT("MatchStructure_%s"), *Entry.InstanceId);
		Spawn.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Actor = World->SpawnActor<ASenniteStructure>(Entry.Location, FRotator(0.0, Entry.Yaw, 0.0), Spawn);
		if (Actor == nullptr) {
			continue;
		}
		Actor->SetNetAddressable();
		Actor->InstanceId = Entry.InstanceId;
		Actor->Match = this;
		Actor->Build(Entry.PrefabId, Entry.Width, Entry.Depth, true);
		Actors.Add(Entry.InstanceId, Actor);
	}
}

void ASenniteMatchBases::Hit(const ASenniteStructure* Structure, float Damage, const ASenniteCharacter* Shooter)
{
	if (!HasAuthority() || Structure == nullptr) {
		return;
	}
	ASenniteMatch* Match = ASenniteMatch::Find(GetWorld());
	FSenniteMatchStructure* Entry = Structures.FindByPredicate([Structure](const FSenniteMatchStructure& S) { return S.InstanceId == Structure->InstanceId; });
	float* Left = Health.Find(Structure->InstanceId);
	if (Entry == nullptr || Left == nullptr || Entry->bDestroyed) {
		return;
	}
	// a shot counts only while the match is being played, and only from a hero of another faction
	if (Match == nullptr || !Match->Harms(Shooter, Entry->FactionId)) {
		return;
	}
	*Left -= Damage;
	if (*Left > 0.0f) {
		return;
	}
	Entry->bDestroyed = true;
	UE_LOG(LogSenniteMatchBases, Display, TEXT("Match %s: %s %s of faction %s was destroyed"), *MatchId, *Entry->PrefabId, *Entry->InstanceId, *Entry->FactionId);
	const FString FactionId = Entry->FactionId;
	const FString InstanceId = Entry->InstanceId;
	Sync();
	Report(FactionId, InstanceId, 0);
	// with nothing of its base left standing, the faction is out of the match
	const bool bStands = Structures.ContainsByPredicate([&FactionId](const FSenniteMatchStructure& S) { return S.FactionId == FactionId && !S.bDestroyed; });
	if (!bStands) {
		Match->BaseWiped(FactionId);
	}
}

void ASenniteMatchBases::Report(const FString& FactionId, const FString& InstanceId, int32 Tries)
{
	USennitePublisher* Server = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
	if (Server == nullptr) {
		return;
	}
	Server->ReportDestroyed(FactionId, InstanceId, [this, Alive = TWeakObjectPtr<ASenniteMatchBases>(this), FactionId, InstanceId, Tries](bool bOk, const FString& Said) {
		if (!Alive.IsValid()) {
			return;
		}
		if (bOk) {
			++Recorded;
			UE_LOG(LogSenniteMatchBases, Display, TEXT("Match %s: the publisher's server recorded %s destroyed"), *MatchId, *InstanceId);
			return;
		}
		if (Tries >= 30) {
			UE_LOG(LogSenniteMatchBases, Error, TEXT("Match %s: the publisher's server did not record %s destroyed: %s"), *MatchId, *InstanceId, *Said);
			return;
		}
		FTimerHandle Again;
		GetWorldTimerManager().SetTimer(Again, FTimerDelegate::CreateWeakLambda(this, [this, FactionId, InstanceId, Tries]() {
			Report(FactionId, InstanceId, Tries + 1);
		}), 2.0f, false);
	});
}
