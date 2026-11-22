#include "SenniteMatch.h"

#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "SenniteCrate.h"
#include "SenniteMining.h"
#include "SennitePlayerController.h"
#include "SennitePlayerState.h"
#include "SennitePublisher.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/CoreMisc.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenniteMatch, Log, All);

ASenniteMatch::ASenniteMatch()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASenniteMatch::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASenniteMatch, MatchId);
	DOREPLIFETIME(ASenniteMatch, Factions);
	DOREPLIFETIME(ASenniteMatch, State);
	DOREPLIFETIME(ASenniteMatch, bBases);
	DOREPLIFETIME(ASenniteMatch, Target);
	DOREPLIFETIME(ASenniteMatch, Length);
	DOREPLIFETIME(ASenniteMatch, EndsAt);
	DOREPLIFETIME(ASenniteMatch, Outcome);
	DOREPLIFETIME(ASenniteMatch, Why);
	DOREPLIFETIME(ASenniteMatch, bRecorded);
}

ASenniteMatch* ASenniteMatch::Find(const UWorld* World)
{
	if (World == nullptr) {
		return nullptr;
	}
	TActorIterator<ASenniteMatch> It(World);
	return It ? *It : nullptr;
}

FSenniteMatchVerdict ASenniteMatch::Judge(const TArray<FSenniteMatchFaction>& Factions, int32 KillsToWin, bool bTimeUp)
{
	FSenniteMatchVerdict Verdict;
	// a faction whose base was wiped is out, and its kills count for nothing
	TArray<const FSenniteMatchFaction*> In;
	for (const FSenniteMatchFaction& Faction : Factions) {
		if (!Faction.bOut) {
			In.Add(&Faction);
		}
	}
	if (In.Num() == 0) {
		Verdict.bOver = true;
		Verdict.Outcome = Void();
		Verdict.Why = TEXT("no faction has a base standing");
		return Verdict;
	}
	if (In.Num() == 1 && Factions.Num() > 1) {
		Verdict.bOver = true;
		Verdict.Outcome = In[0]->FactionId;
		Verdict.Why = TEXT("the last faction with a base standing");
		return Verdict;
	}
	// the most kills, and whether one faction alone has them
	const FSenniteMatchFaction* Top = In[0];
	bool bShared = false;
	for (int32 Each = 1; Each < In.Num(); ++Each) {
		if (In[Each]->Kills > Top->Kills) {
			Top = In[Each];
			bShared = false;
		}
		else if (In[Each]->Kills == Top->Kills) {
			bShared = true;
		}
	}
	if (KillsToWin > 0 && Top->Kills >= KillsToWin && !bShared) {
		Verdict.bOver = true;
		Verdict.Outcome = Top->FactionId;
		Verdict.Why = FString::Printf(TEXT("first to %d kills"), KillsToWin);
		return Verdict;
	}
	if (bTimeUp) {
		Verdict.bOver = true;
		Verdict.Outcome = bShared ? FString(Void()) : Top->FactionId;
		Verdict.Why = bShared ? TEXT("the time ran out with the most kills shared") : TEXT("the most kills when the time ran out");
	}
	return Verdict;
}

const FSenniteMatchFaction* ASenniteMatch::FactionOf(const FString& FactionId) const
{
	return Factions.FindByPredicate([&FactionId](const FSenniteMatchFaction& Each) { return Each.FactionId == FactionId; });
}

FSenniteMatchFaction* ASenniteMatch::Find(const FString& FactionId)
{
	return Factions.FindByPredicate([&FactionId](const FSenniteMatchFaction& Each) { return Each.FactionId == FactionId; });
}

double ASenniteMatch::ServerSeconds() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* Game = World != nullptr ? World->GetGameState() : nullptr;
	return Game != nullptr ? Game->GetServerWorldTimeSeconds() : 0.0;
}

double ASenniteMatch::SecondsLeft() const
{
	if (State != EMatchState::Playing || Length <= 0.0f) {
		return 0.0;
	}
	return FMath::Max(0.0, EndsAt - ServerSeconds());
}

FTransform ASenniteMatch::StartOf(int32 Base) const
{
	return Starts.IsValidIndex(Base) ? Starts[Base] : FTransform::Identity;
}

bool ASenniteMatch::Harms(const ASenniteCharacter* Shooter, const FString& TargetFaction) const
{
	if (State != EMatchState::Playing || Shooter == nullptr) {
		return false;
	}
	const ASennitePlayerState* By = Shooter->GetPlayerState<ASennitePlayerState>();
	if (By == nullptr || By->FactionId.IsEmpty() || By->FactionId == TargetFaction) {
		return false;
	}
	const FSenniteMatchFaction* Mine = FactionOf(By->FactionId);
	const FSenniteMatchFaction* Theirs = FactionOf(TargetFaction);
	return Mine != nullptr && Theirs != nullptr && !Mine->bOut && !Theirs->bOut;
}

void ASenniteMatch::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) {
		return;
	}
	if (const TCHAR* Named = GetWorld()->URL.GetOption(TEXT("Match="), nullptr)) {
		MatchId = Named;
	}
	Target = KillsToWin;
	Length = MatchSeconds;
	if (GetNetMode() != NM_DedicatedServer) {
		Fail(TEXT("a match is played on the publisher's match server, which has no player of its own: start it with -server"));
	}
}

void ASenniteMatch::Fail(const FString& InWhy)
{
	if (State == EMatchState::Failed || State == EMatchState::Over) {
		return;
	}
	State = EMatchState::Failed;
	Why = InWhy;
	UE_LOG(LogSenniteMatch, Error, TEXT("Match %s cannot be played: %s"), *MatchId, *Why);
}

void ASenniteMatch::BasesPlaced(const TArray<FString>& InFactions, const TArray<TPair<FVector, float>>& Built)
{
	if (!HasAuthority() || bBases) {
		return;
	}
	TActorIterator<ASenniteArena> Arena(GetWorld());
	if (!Arena || !Arena->IsBuilt() || Arena->GetPlan().Bases.Num() != InFactions.Num()) {
		Fail(TEXT("the arena does not have a base for each faction of the match"));
		return;
	}
	const FArenaPlan& Plan = Arena->GetPlan();
	FVector Middle = FVector::ZeroVector;
	for (const FVector& Base : Plan.Bases) {
		Middle += Base / Plan.Bases.Num();
	}
	const auto Clear = [&Built](const FVector& At, float Room) {
		for (const TPair<FVector, float>& Each : Built) {
			if (FVector::Dist2D(At, Each.Key) < Each.Value + Room) {
				return false;
			}
		}
		return true;
	};
	Factions.Reset();
	Starts.Reset();
	for (int32 Base = 0; Base < InFactions.Num(); ++Base) {
		FSenniteMatchFaction& Faction = Factions.AddDefaulted_GetRef();
		Faction.FactionId = InFactions[Base];
		Faction.Base = Base;
		// heroes start at the middle of their base, or as near it as is clear of what is built there
		const FVector Centre = Plan.Bases[Base];
		const FVector Out = (Middle - Centre).GetSafeNormal2D();
		FVector At = Centre;
		bool bFound = Clear(At, 200.0f);
		for (int32 Ring = 1; Ring <= 12 && !bFound; ++Ring) {
			for (int32 Turn = 0; Turn < 8 && !bFound; ++Turn) {
				At = Centre + Out.RotateAngleAxis(Turn * 45.0, FVector::UpVector) * (Ring * 600.0);
				bFound = Clear(At, 200.0f);
			}
		}
		// standing on the land there: past the pad it is hills
		const FVector On = bFound ? At : Centre;
		Starts.Add(FTransform(FRotator(0.0, Out.Rotation().Yaw, 0.0), FVector(On.X, On.Y, ASenniteArena::GroundHeight(Plan, On.X, On.Y) + 110.0)));
	}
	bBases = true;
	ASenniteCrate::Stock(GetWorld(), Starts, Built);
	// what the arena holds to mine is the publisher's to say: its server said it with the bases
	if (const USennitePublisher* Server = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr) {
		TArray<int32> Totals;
		Totals.Init(0, ASenniteDeposit::Kinds);
		for (const TPair<FString, int32>& Each : Server->MatchMining) {
			const int32 Kind = ASenniteDeposit::MaterialOf(Each.Key);
			if (Totals.IsValidIndex(Kind)) {
				Totals[Kind] = Each.Value;
			}
		}
		ASenniteDeposit::Stock(GetWorld(), Totals);
	}
	UE_LOG(LogSenniteMatch, Display, TEXT("Match %s: %d factions; first to %d kills, %.0f s"), *MatchId, Factions.Num(), Target, Length);
}

void ASenniteMatch::Enter(ASennitePlayerController* Player, const FString& Ticket)
{
	if (!HasAuthority() || Player == nullptr) {
		return;
	}
	const ASennitePlayerState* Known = Player->GetPlayerState<ASennitePlayerState>();
	if (Known == nullptr || !Known->FactionId.IsEmpty()) {
		return;
	}
	if (State == EMatchState::Failed || !bBases) {
		Player->ClientMatchSaid(State == EMatchState::Failed ? Why : FString(TEXT("the match's bases are not set yet")));
		return;
	}
	USennitePublisher* Server = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
	if (Server == nullptr || !Server->IsMatchServer()) {
		Player->ClientMatchSaid(TEXT("this server is not the publisher's match server"));
		return;
	}
	Server->EnterMatch(MatchId, Ticket, [this, Alive = TWeakObjectPtr<ASenniteMatch>(this), Who = TWeakObjectPtr<ASennitePlayerController>(Player)](bool bOk, const FString& Told, const FString& FactionId, const FString& Wallet, const FString& SigningKey) {
		if (!Alive.IsValid() || !Who.IsValid()) {
			return;
		}
		ASennitePlayerState* Entering = Who->GetPlayerState<ASennitePlayerState>();
		if (Entering == nullptr || !Entering->FactionId.IsEmpty()) {
			return;
		}
		if (!bOk) {
			Who->ClientMatchSaid(Told);
			return;
		}
		FSenniteMatchFaction* Faction = Find(FactionId);
		if (Faction == nullptr) {
			Who->ClientMatchSaid(TEXT("the wallet's faction is not in this match"));
			return;
		}
		// one hero to a wallet
		if (const AGameStateBase* Game = GetWorld()->GetGameState()) {
			for (const APlayerState* Other : Game->PlayerArray) {
				const ASennitePlayerState* In = Cast<ASennitePlayerState>(Other);
				if (In != nullptr && In != Entering && In->Wallet == Wallet) {
					Who->ClientMatchSaid(TEXT("this wallet is already in the match"));
					return;
				}
			}
		}
		Entering->FactionId = FactionId;
		Entering->Wallet = Wallet;
		Entering->SigningKey = SigningKey;
		Faction->Players += 1;
		Who->ClientMatchSaid(FString());
		UE_LOG(LogSenniteMatch, Display, TEXT("Match %s: %s plays for faction %s"), *MatchId, *Wallet, *FactionId);
		if (State == EMatchState::Over || Faction->bOut) {
			Who->BecomeSpectator();
			return;
		}
		Spawn(Who.Get());
		MaybeStart();
	});
}

void ASenniteMatch::Left(AController* Player)
{
	const ASennitePlayerState* Leaving = Player != nullptr ? Player->GetPlayerState<ASennitePlayerState>() : nullptr;
	if (!HasAuthority() || Leaving == nullptr) {
		return;
	}
	if (FSenniteMatchFaction* Faction = Find(Leaving->FactionId)) {
		Faction->Players = FMath::Max(0, Faction->Players - 1);
	}
}

void ASenniteMatch::Spawn(ASennitePlayerController* Player)
{
	ASennitePlayerState* Playing = Player != nullptr ? Player->GetPlayerState<ASennitePlayerState>() : nullptr;
	const FSenniteMatchFaction* Faction = Playing != nullptr ? FactionOf(Playing->FactionId) : nullptr;
	AGameModeBase* Mode = GetWorld()->GetAuthGameMode();
	if (Faction == nullptr || Mode == nullptr || Player->GetPawn() != nullptr) {
		return;
	}
	// a hero starts, and returns, with what a hero starts with
	Playing->StartOver();
	Mode->RestartPlayerAtTransform(Player, StartOf(Faction->Base));
}

void ASenniteMatch::MaybeStart()
{
	if (State != EMatchState::Waiting || !bBases) {
		return;
	}
	int32 With = 0;
	for (const FSenniteMatchFaction& Faction : Factions) {
		With += Faction.Players > 0 ? 1 : 0;
	}
	if (With < 2) {
		return;
	}
	State = EMatchState::Playing;
	EndsAt = ServerSeconds() + Length;
	UE_LOG(LogSenniteMatch, Display, TEXT("Match %s is being played: players of %d factions are in"), *MatchId, With);
}

bool ASenniteMatch::Fell(ASenniteCharacter* Victim, ASenniteCharacter* Killer)
{
	if (!HasAuthority() || Victim == nullptr) {
		return false;
	}
	ASennitePlayerController* Player = Cast<ASennitePlayerController>(Victim->GetController());
	const ASennitePlayerState* Fallen = Player != nullptr ? Player->GetPlayerState<ASennitePlayerState>() : nullptr;
	if (Fallen == nullptr) {
		return false;
	}
	if (Killer != nullptr && Killer != Victim && Harms(Killer, Fallen->FactionId)) {
		const ASennitePlayerState* By = Killer->GetPlayerState<ASennitePlayerState>();
		if (FSenniteMatchFaction* Scoring = By != nullptr ? Find(By->FactionId) : nullptr) {
			Scoring->Kills += 1;
			UE_LOG(LogSenniteMatch, Display, TEXT("Match %s: faction %s has %d kills"), *MatchId, *Scoring->FactionId, Scoring->Kills);
		}
	}
	const FSenniteMatchFaction* Own = FactionOf(Fallen->FactionId);
	const bool bReturns = Own != nullptr && !Own->bOut && (State == EMatchState::Playing || State == EMatchState::Waiting);
	if (bReturns) {
		Player->UnPossess();
		FTimerHandle Returning;
		GetWorldTimerManager().SetTimer(Returning, FTimerDelegate::CreateWeakLambda(this, [this, Who = TWeakObjectPtr<ASennitePlayerController>(Player)]() {
			const ASennitePlayerState* Back = Who.IsValid() ? Who->GetPlayerState<ASennitePlayerState>() : nullptr;
			const FSenniteMatchFaction* Theirs = Back != nullptr ? FactionOf(Back->FactionId) : nullptr;
			if (Theirs != nullptr && !Theirs->bOut && (State == EMatchState::Playing || State == EMatchState::Waiting)) {
				Spawn(Who.Get());
			}
		}), FMath::Max(0.1f, RespawnSeconds), false);
	}
	JudgeNow();
	return bReturns;
}

void ASenniteMatch::BaseWiped(const FString& FactionId)
{
	FSenniteMatchFaction* Faction = Find(FactionId);
	if (!HasAuthority() || Faction == nullptr || Faction->bOut || State != EMatchState::Playing) {
		return;
	}
	Faction->bOut = true;
	UE_LOG(LogSenniteMatch, Display, TEXT("Match %s: faction %s's base is wiped; it is out"), *MatchId, *FactionId);
	// its heroes are gone, and its players watch
	TArray<ASenniteCharacter*> Heroes;
	for (TActorIterator<ASenniteCharacter> It(GetWorld()); It; ++It) {
		const ASennitePlayerState* Of = It->GetPlayerState<ASennitePlayerState>();
		if (Of != nullptr && Of->FactionId == FactionId) {
			Heroes.Add(*It);
		}
	}
	for (ASenniteCharacter* Hero : Heroes) {
		Hero->Die(nullptr);
	}
	JudgeNow();
}

void ASenniteMatch::JudgeNow()
{
	if (State != EMatchState::Playing) {
		return;
	}
	const FSenniteMatchVerdict Verdict = Judge(Factions, Target, Length > 0.0f && ServerSeconds() >= EndsAt);
	if (Verdict.bOver) {
		End(Verdict.Outcome, Verdict.Why);
	}
}

void ASenniteMatch::End(const FString& InOutcome, const FString& InWhy)
{
	State = EMatchState::Over;
	Outcome = InOutcome;
	Why = InWhy;
	FString Score;
	for (const FSenniteMatchFaction& Faction : Factions) {
		Score += FString::Printf(TEXT(" %s %d%s"), *Faction.FactionId, Faction.Kills, Faction.bOut ? TEXT(" (out)") : TEXT(""));
	}
	UE_LOG(LogSenniteMatch, Display, TEXT("Match %s is over: %s (%s). Kills:%s"), *MatchId, *Outcome, *Why, *Score);
	Report(0);
}

void ASenniteMatch::Report(int32 Tries)
{
	USennitePublisher* Server = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
	if (Server == nullptr || !Server->IsMatchServer()) {
		return;
	}
	Server->ReportOutcome(MatchId, Outcome, [this, Alive = TWeakObjectPtr<ASenniteMatch>(this), Tries](bool bOk, const FString& Told) {
		if (!Alive.IsValid()) {
			return;
		}
		if (bOk) {
			bRecorded = true;
			UE_LOG(LogSenniteMatch, Display, TEXT("Match %s: the publisher's server recorded the outcome %s"), *MatchId, *Outcome);
			// the match is over for the publisher: it takes what was mined now, and then the match server closes
			TellMined(0);
			return;
		}
		if (Tries >= 40) {
			UE_LOG(LogSenniteMatch, Error, TEXT("Match %s: the publisher's server did not record the outcome %s: %s"), *MatchId, *Outcome, *Told);
			return;
		}
		FTimerHandle Again;
		GetWorldTimerManager().SetTimer(Again, FTimerDelegate::CreateWeakLambda(this, [this, Tries]() { Report(Tries + 1); }), 3.0f, false);
	});
}

void ASenniteMatch::Mined(const ASennitePlayerState* By, int32 Material, int32 Units)
{
	// what is mined counts while the match can still be played: nothing is added once it is over
	if (!HasAuthority() || By == nullptr || By->SigningKey.IsEmpty() || Units <= 0 || Material < 0 || Material >= ASenniteDeposit::Kinds
		|| State == EMatchState::Over || State == EMatchState::Failed) {
		return;
	}
	MinedBy.FindOrAdd(By->SigningKey)[Material] += Units;
}

void ASenniteMatch::TellMined(int32 Tries)
{
	USennitePublisher* Server = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
	TArray<TTuple<FString, FString, int32>> Told;
	for (const TPair<FString, FIntVector>& Each : MinedBy) {
		for (int32 Kind = 0; Kind < ASenniteDeposit::Kinds; ++Kind) {
			if (Each.Value[Kind] > 0) {
				Told.Emplace(Each.Key, FString(ASenniteDeposit::NameOf(Kind)), Each.Value[Kind]);
			}
		}
	}
	if (Server == nullptr || !Server->IsMatchServer() || Told.Num() == 0) {
		bMinedTold = true;
		Close();
		return;
	}
	Server->ReportMined(MatchId, Told, [this, Alive = TWeakObjectPtr<ASenniteMatch>(this), Tries, Count = Told.Num()](bool bOk, const FString& Refused) {
		if (!Alive.IsValid()) {
			return;
		}
		if (bOk) {
			bMinedTold = true;
			UE_LOG(LogSenniteMatch, Display, TEXT("Match %s: the publisher's server has what was mined (%d shares)"), *MatchId, Count);
			Close();
			return;
		}
		if (Tries >= 40) {
			UE_LOG(LogSenniteMatch, Error, TEXT("Match %s: the publisher's server did not take what was mined: %s"), *MatchId, *Refused);
			Close();
			return;
		}
		FTimerHandle Again;
		GetWorldTimerManager().SetTimer(Again, FTimerDelegate::CreateWeakLambda(this, [this, Tries]() { TellMined(Tries + 1); }), 3.0f, false);
	});
}

void ASenniteMatch::Close()
{
	// the arena is the match's own, and goes with it
	if (GetNetMode() != NM_DedicatedServer) {
		return;
	}
	FTimerHandle Closing;
	GetWorldTimerManager().SetTimer(Closing, FTimerDelegate::CreateWeakLambda(this, [this]() {
		UE_LOG(LogSenniteMatch, Display, TEXT("Match %s: the match server closes"), *MatchId);
		FPlatformMisc::RequestExit(false);
	}), FMath::Max(0.1f, CloseSeconds), false);
}

void ASenniteMatch::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!HasAuthority()) {
		AskToEnter();
		return;
	}
	JudgeNow();
	// a game that joined and showed no ticket in time is put out
	AGameModeBase* Mode = GetWorld()->GetAuthGameMode();
	if (Mode == nullptr || Mode->GameSession == nullptr || EnterSeconds <= 0.0f) {
		return;
	}
	TArray<APlayerController*> Out;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It) {
		APlayerController* Player = It->Get();
		const ASennitePlayerState* Of = Player != nullptr ? Player->GetPlayerState<ASennitePlayerState>() : nullptr;
		if (Of != nullptr && Of->FactionId.IsEmpty() && !Player->IsLocalController() && GetWorld()->GetTimeSeconds() - Of->GetStartTime() > EnterSeconds) {
			Out.Add(Player);
		}
	}
	for (APlayerController* Player : Out) {
		UE_LOG(LogSenniteMatch, Display, TEXT("Match %s: a game that showed no ticket is put out"), *MatchId);
		Mode->GameSession->KickPlayer(Player, FText::FromString(TEXT("This match is for the members of its factions.")));
	}
}

void ASenniteMatch::AskToEnter()
{
	UWorld* World = GetWorld();
	ASennitePlayerController* Player = World != nullptr ? Cast<ASennitePlayerController>(World->GetFirstPlayerController()) : nullptr;
	const ASennitePlayerState* Mine = Player != nullptr ? Player->GetPlayerState<ASennitePlayerState>() : nullptr;
	if (Mine == nullptr || !Mine->FactionId.IsEmpty() || !bBases || State == EMatchState::Failed || bAsking || MatchId.IsEmpty()) {
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	if (Now < NextAsk) {
		return;
	}
	USennitePublisher* Server = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
	if (Server == nullptr || !Server->IsOn() || !Server->HasPlayer() || !Server->bConnected) {
		Said = TEXT("This match is for the members of its factions. Connect your wallet to the game first.");
		NextAsk = Now + 1.0;
		return;
	}
	bAsking = true;
	++Asked;
	Server->AskMatchTicket(MatchId, [this, Alive = TWeakObjectPtr<ASenniteMatch>(this), Who = TWeakObjectPtr<ASennitePlayerController>(Player)](bool bOk, const FString& Told, const FString& Ticket) {
		if (!Alive.IsValid()) {
			return;
		}
		bAsking = false;
		NextAsk = GetWorld()->GetRealTimeSeconds() + 5.0;
		if (!bOk || !Who.IsValid()) {
			Said = Told;
			return;
		}
		Said = TEXT("Entering the match...");
		Who->ServerEnterMatch(Ticket);
	});
}
