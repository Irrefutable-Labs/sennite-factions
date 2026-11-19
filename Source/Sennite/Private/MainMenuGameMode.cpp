#include "MainMenuGameMode.h"
#include "SenniteMenu.h"
#include "SennitePlayerState.h"

AMainMenuGameMode::AMainMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ASenniteMenuController::StaticClass();
	PlayerStateClass = ASennitePlayerState::StaticClass();
	HUDClass = ASenniteMenuHUD::StaticClass();
}
