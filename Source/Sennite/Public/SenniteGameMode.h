// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SenniteGameMode.generated.h"

class AStormActor;

DECLARE_LOG_CATEGORY_EXTERN(LogMyServerGame, Log, All);

UCLASS(minimalapi)
class ASenniteGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASenniteGameMode();

	bool GameStarted; 

	bool GameEnded;

	int TimePassed;

	AStormActor* CurrentStorm;

	virtual void BeginPlay() override;

	virtual void StartPlay() override;

	virtual void PostLogin(APlayerController *NewPlayer) override;

	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;

	virtual void Logout(AController* Exiting) override;

	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) override;

	virtual void GenericPlayerInitialization(AController* NewPlayer) override;

	/** How many of each building material a player starts a match with. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Materials")
	int32 StartingMaterialCount = 0;

	/** How many players must be in the match before its storm starts. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Storm")
	int32 PlayersToStartStorm = 2;

	/** How long after that the storm starts its stages. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Storm")
	float StormStartDelay = 30.0f;

	UFUNCTION(NetMulticast, Reliable)
	void NetMulticastSpawnStorm();

	UFUNCTION(Server, Reliable, WithValidation)
	void GameModeStartStorm();

	UFUNCTION(Server, Reliable, WithValidation)
	void CheckRemainingPlayers();
};



