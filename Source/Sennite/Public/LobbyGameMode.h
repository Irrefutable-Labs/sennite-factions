#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LobbyGameMode.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSenniteLobby, Log, All);

/**
 * The lobby: the Menu map hosted with ?listen?game=lobby. Players join it by address and stand on the
 * stage as the hero each chose; the host starts, and the server travels everyone to the test range
 * together, each keeping their hero (ASennitePlayerState::CopyProperties).
 */
UCLASS()
class SENNITE_API ALobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALobbyGameMode();

	/** The map, and its options, the lobby travels to when the host starts. */
	UPROPERTY(EditDefaultsOnly, Category = "Sennite|Lobby")
	FString MatchMap = TEXT("/Game/Maps/TestRange?listen");

	/** Nobody is given a body in the lobby: their hero stands on the stage. */
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override { return false; }

	/** The hero the player's game logged in as (Hero=, and HeroPass= for a hero bought in the shop), claimed as any choice is. */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	/** The host starts: everyone in the lobby travels to the match map together. Once. */
	void StartMatch();

private:
	bool bStarted = false;
};
