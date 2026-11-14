// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SenniteGameMode.h"
#include "SenniteCharacter.h"
#include "SennitePlayerState.h"
#include "SennitePlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "SenniteSpectator.h"
#include "SenniteHUD.h"
#include "StormActor.h"
#include "GameFramework/GameState.h"
#include "GameFramework/PlayerStart.h"
#include "SenniteArena.h"
#include "SenniteMatch.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY(LogMyServerGame);

ASenniteGameMode::ASenniteGameMode()
{
	// set default pawn class to our Blueprinted character
	GameStarted = false;
	GameEnded = false;
	TimePassed = 0;
	bUseSeamlessTravel = true;
	DefaultPawnClass = ASenniteCharacter::StaticClass();
	SpectatorClass = ASenniteSpectator::StaticClass();
	PlayerStateClass = ASennitePlayerState::StaticClass();
	PlayerControllerClass = ASennitePlayerController::StaticClass();
	HUDClass = ASenniteHUD::StaticClass();


}

void ASenniteGameMode::BeginPlay() {
	Super::BeginPlay();
	
	//NetMulticastSpawnStorm();
}

void ASenniteGameMode::StartPlay() {
	Super::StartPlay();
	//UGameplayStatics::OpenLevel((UObject*)GetWorld(), FName(TEXT("Level_BattleRoyale")));
}

void ASenniteGameMode::PostLogin(APlayerController *NewPlayer) {
	Super::PostLogin(NewPlayer);
	//set input to ui only for the main screen level
	/*NewPlayer->SetInputMode(FInputModeGameAndUI());
	ASennitePlayerState* State = Cast<ASennitePlayerState>(NewPlayer->PlayerState);
	if (State) {
		//ASenniteCharacter* Character = Cast<ASenniteCharacter>(NewPlayer->GetPawn());
		//Character->State = State;
		State->HoldingWeapon = true;
		State->CurrentWeapon = 0;
	}*/
}

void ASenniteGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) {
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
}

void ASenniteGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) {
	if (ASenniteMatch::Find(GetWorld()) != nullptr) {
		// Until the publisher's server has said who they play for they have no hero: they look down
		// on the arena from above its middle, the bases to either side.
		TActorIterator<ASenniteArena> It(GetWorld());
		const TArray<FVector> Bases = It ? It->GetPlan().Bases : TArray<FVector>();
		if (NewPlayer != nullptr && Bases.Num() >= 2) {
			FVector Middle = FVector::ZeroVector;
			for (const FVector& Base : Bases) {
				Middle += Base / Bases.Num();
			}
			double Reach = 0.0;
			for (const FVector& Base : Bases) {
				Reach = FMath::Max(Reach, FVector::Dist2D(Base, Middle) + It->BaseRadius);
			}
			const FVector Eye = Middle + FVector(0.0, 0.0, Reach);
			const FRotator Down(-89.0, (Bases[1] - Bases[0]).Rotation().Yaw - 90.0, 0.0);
			NewPlayer->SetInitialLocationAndRotation(Eye, Down);
			NewPlayer->ClientSetLocation(Eye, Down);
		}
		return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void ASenniteGameMode::Logout(AController* Exiting) {
	if (ASenniteMatch* Match = ASenniteMatch::Find(GetWorld())) {
		Match->Left(Exiting);
	}
	Super::Logout(Exiting);
	if (Exiting != nullptr) {
		ASennitePlayerController* SennitePlayerController = Cast<ASennitePlayerController>(Exiting);
		const FString& PlayerSessionId = SennitePlayerController->PlayerSessionId;
		UE_LOG(LogMyServerGame, Log, TEXT("Logout playersessionid: %s"), *PlayerSessionId);
		if (PlayerSessionId.Len() > 0) {
		}
	}
}

FString ASenniteGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) {
	FString InitializedString = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	
	/*const FString& PlayerSessionId = UGameplayStatics::ParseOption(Options, "PlayerSessionId");
	UE_LOG(LogMyServerGame, Log, TEXT("InitNewPlayer playersessionid: %s"), *PlayerSessionId);
	if (NewPlayerController != nullptr) {
		ASennitePlayerController* SennitePlayerController = Cast<ASennitePlayerController>(NewPlayerController);
		SennitePlayerController->PlayerSessionId = PlayerSessionId;
	}*/

	return InitializedString;
}

void ASenniteGameMode::GenericPlayerInitialization(AController* NewPlayer) {
	Super::GenericPlayerInitialization(NewPlayer);
	if (!GameStarted && GetNumPlayers() >= PlayersToStartStorm) {
		GameStarted = true;
		TArray<AActor*> StormActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AStormActor::StaticClass(), StormActors);
		if (StormActors.Num() > 0) {
			if (StormActors[0] != nullptr) {
				CurrentStorm = Cast<AStormActor>(StormActors[0]);
			}
		}
		FTimerHandle StormSetupTimerHandle;
		GetWorldTimerManager().SetTimer(StormSetupTimerHandle, this, &ASenniteGameMode::GameModeStartStorm, FMath::Max(0.1f, StormStartDelay), false);
		FTimerHandle InactivityCheckTimerHandle;
		GetWorldTimerManager().SetTimer(InactivityCheckTimerHandle, this, &ASenniteGameMode::CheckRemainingPlayers, 1.0f, true);
	}
	if (NewPlayer != nullptr) {
		if (ASennitePlayerState* State = Cast<ASennitePlayerState>(NewPlayer->PlayerState)) {
			for (int& Count : State->MaterialCounts) {
				Count = StartingMaterialCount;
			}
		}
		ASennitePlayerController* SennitePlayerController = Cast<ASennitePlayerController>(NewPlayer);
		SennitePlayerController->SpawnAsSpectator = false; // since we have switched to server travel, everyone joining the game should be a player
	}
}

AActor* ASenniteGameMode::ChoosePlayerStart_Implementation(AController* Player) {
	TArray<APlayerStart*> Starts;
	// in an arena the start points are the bases', in the order of the bases
	for (TActorIterator<ASenniteArena> It(GetWorld()); It; ++It) {
		for (APlayerStart* Start : It->GetStarts()) {
			Starts.Add(Start);
		}
	}
	if (Starts.Num() == 0) {
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) {
			Starts.Add(*It);
		}
		Starts.Sort([](const APlayerStart& A, const APlayerStart& B) { return A.GetName() < B.GetName(); });
	}
	if (Starts.Num() == 0) {
		return Super::ChoosePlayerStart_Implementation(Player);
	}
	for (APlayerStart* Start : Starts) {
		bool bTaken = false;
		for (TActorIterator<APawn> It(GetWorld()); It; ++It) {
			if (FVector::Dist2D(It->GetActorLocation(), Start->GetActorLocation()) < 150.0) {
				bTaken = true;
				break;
			}
		}
		if (!bTaken) {
			return Start;
		}
	}
	// more players than start points: the first one, and the engine moves the hero clear of whoever is there
	return Starts[0];
}

void ASenniteGameMode::NetMulticastSpawnStorm_Implementation() {
	GetWorld()->SpawnActor<AStormActor>(AStormActor::StaticClass(), FVector(-440, -1450, 10000), FRotator::ZeroRotator);
}

void ASenniteGameMode::GameModeStartStorm_Implementation() {
	// a map with no storm placed in it has none to start
	if (CurrentStorm != nullptr) {
		CurrentStorm->ServerStartStorm();
	}
}

bool ASenniteGameMode::GameModeStartStorm_Validate() {
	return true;
}

void ASenniteGameMode::CheckRemainingPlayers_Implementation() {
	AGameStateBase* SenniteGameState = Cast<AGameStateBase>(GetWorld()->GetGameState<AGameStateBase>());
	if (SenniteGameState) {
		//UE_LOG(LogMyServerGame, Log, TEXT("Game state exists"));
		int NumPlayers = 0;
		TArray<APlayerState*> States = SenniteGameState->PlayerArray;
		for (int i = 0; i < States.Num(); i++) {
			if (States[i]->IsSpectator() == false) {
				NumPlayers++;
			}
		}
		if (!GameEnded && GameStarted && NumPlayers < 2) {
			// the game is over, terminate the game session
			TimePassed += 1;
			if (TimePassed >= 15) {
				//UE_LOG(LogMyServerGame, Log, TEXT("Game ended"));
				GameEnded = true;
			}
		}
		else if (!GameEnded && NumPlayers >= 2) {
			TimePassed = 0;
		}
	}
	else {
		//UE_LOG(LogMyServerGame, Log, TEXT("Game state does not exist"));
	}
}

bool ASenniteGameMode::CheckRemainingPlayers_Validate() {
	return true;
}

