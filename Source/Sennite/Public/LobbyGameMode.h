// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LobbyGameMode.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMyServerLobby, Log, All);
/**
 * 
 */
UCLASS()
class SENNITE_API ALobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALobbyGameMode();

	/** The map the lobby travels to when the match starts. */
	UPROPERTY(EditDefaultsOnly, Category = "Sennite|Lobby")
	FString MatchMap = TEXT("/Game/Maps/Island");

	bool GameReady;

	bool GameEndedDueToInactivity;

	virtual void BeginPlay() override;

	virtual void StartPlay() override;

	virtual void PostLogin(APlayerController* NewPlayer) override;

	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;

	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) override;

	virtual void Logout(AController* Exiting) override;

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerCheckInactivity();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerStartGame();

private:
	int TimePassed;

	bool SomeoneJoined;
};

