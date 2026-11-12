#include "MainMenuWidget.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY(LogSenniteMainMenu);

void UMainMenuWidget::HostGame()
{
	UE_LOG(LogSenniteMainMenu, Log, TEXT("Hosting %s"), *LobbyMap);
	UGameplayStatics::OpenLevel(this, FName(*LobbyMap), true, TEXT("listen"));
}

void UMainMenuWidget::JoinGame(const FString& Address)
{
	const FString Trimmed = Address.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		UE_LOG(LogSenniteMainMenu, Warning, TEXT("No address to join"));
		return;
	}
	if (APlayerController* Player = GetOwningPlayer())
	{
		UE_LOG(LogSenniteMainMenu, Log, TEXT("Joining %s"), *Trimmed);
		Player->ClientTravel(Trimmed, TRAVEL_Absolute);
	}
}
