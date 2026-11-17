#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MainMenuGameMode.generated.h"

/** The Menu map on its own: the first screen and the character select (SenniteMenu.h). No pawn: the player looks at the stage. */
UCLASS()
class SENNITE_API AMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMainMenuGameMode();

	/** Nobody is given a body at the menu. */
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override { return false; }
};
