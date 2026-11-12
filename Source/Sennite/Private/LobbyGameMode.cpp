// Fill out your copyright notice in the Description page of Project Settings.


#include "LobbyGameMode.h"
#include "LobbyPlayerController.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "LobbyHUD.h"
#include "LobbyCharacter.h"

DEFINE_LOG_CATEGORY(LogMyServerLobby);

ALobbyGameMode::ALobbyGameMode()
{
	bUseSeamlessTravel = true;
	GameReady = false;
	SomeoneJoined = false;
	GameEndedDueToInactivity = false;
	TimePassed = 0;
	DefaultPawnClass = ALobbyCharacter::StaticClass();
	HUDClass = ALobbyHUD::StaticClass();
	PlayerControllerClass = ALobbyPlayerController::StaticClass();
}

void ALobbyGameMode::BeginPlay() {
	Super::BeginPlay();
	/*FTimerHandle InactivityTimerHandle;
	GetWorldTimerManager().SetTimer(InactivityTimerHandle, this, &ALobbyGameMode::ServerCheckInactivity, 1.0f, true);*/
}

void ALobbyGameMode::StartPlay() {
	Super::StartPlay();
	//UGameplayStatics::OpenLevel((UObject*)GetWorld(), FName(TEXT("Level_BattleRoyale")));
}

void ALobbyGameMode::PostLogin(APlayerController* NewPlayer) {
	Super::PostLogin(NewPlayer);
	if (!SomeoneJoined && GetNumPlayers() >= 1) {
		SomeoneJoined = true;
		FTimerHandle InactivityCheckTimerHandle;
		GetWorldTimerManager().SetTimer(InactivityCheckTimerHandle, this, &ALobbyGameMode::ServerCheckInactivity, 1.0f, true);
	}
	if (!GameReady && GetNumPlayers() >= 4) {
		GameReady = true;
		// start the game (server travel) in 60 seconds once there's at least 4 players in the lobby
		FTimerHandle ServerTravelTimerHandle;
		GetWorldTimerManager().SetTimer(ServerTravelTimerHandle, this, &ALobbyGameMode::ServerStartGame, 60.0f, false);
	}
}

void ALobbyGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) {
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
}

FString ALobbyGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) {
	FString InitializedString = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

	const FString& PlayerSessionId = UGameplayStatics::ParseOption(Options, "PlayerSessionId");
	if (NewPlayerController != nullptr) {
		ALobbyPlayerController* LobbyPlayerController = Cast<ALobbyPlayerController>(NewPlayerController);
		LobbyPlayerController->PlayerSessionId = PlayerSessionId;
	}

	return InitializedString;
}

void ALobbyGameMode::Logout(AController* Exiting) {
	Super::Logout(Exiting);
	if (Exiting != nullptr) {
		ALobbyPlayerController* LobbyPlayerController = Cast<ALobbyPlayerController>(Exiting);
		const FString& PlayerSessionId = LobbyPlayerController->PlayerSessionId;
		if (PlayerSessionId.Len() > 0) {
		}
	}
}

void ALobbyGameMode::ServerStartGame_Implementation() {
	// make the game unjoinable
	// if enough people left resulting in less than 4 players in the lobby then don't start the game yet
	if (GetNumPlayers() >= 4) {
		const FString& travelUrl = MatchMap;
		GetWorld()->ServerTravel(travelUrl, true, false); // url to travel to, absolute (flush url), yes notify player clients to travel with the server
	}
	else {
		GameReady = false;
	}
}

bool ALobbyGameMode::ServerStartGame_Validate() {
	return true;
}

void ALobbyGameMode::ServerCheckInactivity_Implementation() {
	// make the game unjoinable
	if (!GameEndedDueToInactivity && SomeoneJoined && GetNumPlayers() < 1) {
		TimePassed += 1;
		if (TimePassed >= 10) {
			GameEndedDueToInactivity = true;
		}
	}
	else if(!GameEndedDueToInactivity && GetNumPlayers() >= 1){
		TimePassed = 0;
	}
}

bool ALobbyGameMode::ServerCheckInactivity_Validate() {
	return true;
}