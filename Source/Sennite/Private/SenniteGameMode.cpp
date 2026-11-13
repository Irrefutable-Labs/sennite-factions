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

void ASenniteGameMode::Logout(AController* Exiting) {
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
	if (!GameStarted && GetNumPlayers() >= 2) {
		GameStarted = true;
		TArray<AActor*> StormActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AStormActor::StaticClass(), StormActors);
		if (StormActors.Num() > 0) {
			if (StormActors[0] != nullptr) {
				CurrentStorm = Cast<AStormActor>(StormActors[0]);
			}
		}
		FTimerHandle StormSetupTimerHandle;
		GetWorldTimerManager().SetTimer(StormSetupTimerHandle, this, &ASenniteGameMode::GameModeStartStorm, 30.0f, false);
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

void ASenniteGameMode::NetMulticastSpawnStorm_Implementation() {
	GetWorld()->SpawnActor<AStormActor>(AStormActor::StaticClass(), FVector(-440, -1450, 10000), FRotator::ZeroRotator);
}

void ASenniteGameMode::GameModeStartStorm_Implementation() {
	CurrentStorm->ServerStartStorm();
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

