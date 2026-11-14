#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "SenniteGameInstance.generated.h"

/** The running game. Loads the hero when the game starts, so joining a match does not stall on it. */
UCLASS()
class USenniteGameInstance : public UGameInstance
{
	GENERATED_BODY()
	
public:
	virtual void Init() override;

private:
	/** The hero's mesh and animation class, held for as long as the game runs. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> HeroAssets;
};
