#include "LobbyGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "SenniteHeroes.h"
#include "SenniteMenu.h"
#include "SennitePlayerState.h"

DEFINE_LOG_CATEGORY(LogSenniteLobby);

ALobbyGameMode::ALobbyGameMode()
{
	// everyone goes to the match map together, keeping their place and their hero
	bUseSeamlessTravel = true;
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ASenniteMenuController::StaticClass();
	PlayerStateClass = ASennitePlayerState::StaticClass();
	GameStateClass = ASenniteLobbyState::StaticClass();
	HUDClass = ASenniteMenuHUD::StaticClass();
}

FString ALobbyGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Said = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	if (ASennitePlayerState* State = NewPlayerController != nullptr ? NewPlayerController->GetPlayerState<ASennitePlayerState>() : nullptr) {
		State->ClaimHero(FName(UGameplayStatics::ParseOption(Options, TEXT("Hero"))), FString());
	}
	return Said;
}

void ALobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	const ASennitePlayerState* State = NewPlayer != nullptr ? NewPlayer->GetPlayerState<ASennitePlayerState>() : nullptr;
	UE_LOG(LogSenniteLobby, Log, TEXT("%s came into the lobby as %s; %d in it"), State ? *State->GetPlayerName() : TEXT("?"), State ? *State->HeroId.ToString() : TEXT("?"), GetNumPlayers());
}

void ALobbyGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
	UE_LOG(LogSenniteLobby, Log, TEXT("A player left the lobby; %d in it"), GetNumPlayers());
}

void ALobbyGameMode::StartMatch()
{
	if (bStarted) {
		return;
	}
	bStarted = true;
	UE_LOG(LogSenniteLobby, Log, TEXT("The host starts: %d players travel to %s"), GetNumPlayers(), *MatchMap);
	GetWorld()->ServerTravel(MatchMap, true);
}
