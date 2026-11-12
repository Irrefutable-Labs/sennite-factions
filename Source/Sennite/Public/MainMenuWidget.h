#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSenniteMainMenu, Log, All);

/** The main menu: host a match on this machine, or join one by address. */
UCLASS(BlueprintType, Blueprintable)
class SENNITE_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The lobby map a host opens as a listen server. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sennite|Menu")
	FString LobbyMap = TEXT("/Game/Maps/Lobby");

	/** Opens the lobby map as a listen server other players can join. */
	UFUNCTION(BlueprintCallable, Category = "Sennite|Menu")
	void HostGame();

	/** Joins the match hosted at `Address` ("host" or "host:port"). */
	UFUNCTION(BlueprintCallable, Category = "Sennite|Menu")
	void JoinGame(const FString& Address);
};
