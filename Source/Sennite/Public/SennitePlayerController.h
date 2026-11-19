// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SennitePlayerController.generated.h"

class ASenniteSpectator;
class AStormActor;
class AGameMode;
class ASennitePlayerState;
class USenniteGameMenu;
class USenniteTouchControls;
/**
 * 
 */
UCLASS()
class SENNITE_API ASennitePlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	ASennitePlayerController();

	virtual void Tick(float DeltaTime) override;

	/**
	 * On the server only: this player stops playing and watches. The server calls it from its own
	 * rules (a death, or a player who joins to watch). It is not a call a player's machine can send.
	 */
	void BecomeSpectator();

	/** Where a player starts to watch from when they become a spectator. */
	FVector SpectatorStart() const;

	bool Initialized;

	FString PlayerSessionId;

	AStormActor* CurrentStorm;

	TSubclassOf<ASenniteSpectator> PlayerSpectatorClass;

	UPROPERTY(Replicated)
	int PlayerCount;

	UPROPERTY(Replicated)
	int SpectatorCount;

	UFUNCTION(BlueprintPure, Category = "Count")
	int GetPlayerCount();
		
	UFUNCTION(BlueprintPure, Category = "Count")
	int GetSpectatorCount();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerGetNumPlayers();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerGetNumSpectators();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerUpdateCountAfterDeath();

	UFUNCTION(BlueprintPure, Category = "Count")
	int GetKillCount();

	UPROPERTY(Replicated)
	bool SpawnAsSpectator;

	/** In a match: this player's game shows the match server the ticket the publisher's server gave it. */
	UFUNCTION(Server, Reliable)
	void ServerEnterMatch(const FString& Ticket);

	/** In a match: what the match server says about this player entering; nothing once they are in. */
	UFUNCTION(Client, Reliable)
	void ClientMatchSaid(const FString& Said);

	virtual bool IsSupportedForNetworking() const override
	{
		return true;
	}

	/** On a device played by touch: the stick and buttons on the screen (SenniteTouch.h). None elsewhere. */
	USenniteTouchControls* GetTouchControls();

	/** A touch on the screen goes to the touch controls, when there are any. */
	virtual bool InputTouch(const FTouchId TouchId, const ETouchType::Type Type, const FVector2D& TouchLocation, const float Force, const uint64 Timestamp) override;

	/** The engine's own on-screen joysticks are not used: the touch controls are drawn on the HUD. */
	virtual void CreateTouchInterface() override;

	virtual void PlayerTick(float DeltaTime) override;

	/** The menu this machine's player opens while playing (SenniteGameMenu.h). None for a player of another machine. */
	USenniteGameMenu* GetGameMenu();

	/** Escape and the phone's back button open the in-game menu; while it is open it takes every key. */
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	/** Looking round, by the player's settings: how far it turns, and whether up looks down. */
	virtual void AddYawInput(float Val) override;
	virtual void AddPitchInput(float Val) override;

private:
	virtual void BeginPlay() override;

	UPROPERTY()
	TObjectPtr<USenniteGameMenu> GameMenu;

	UPROPERTY()
	TObjectPtr<USenniteTouchControls> TouchControls;

};
