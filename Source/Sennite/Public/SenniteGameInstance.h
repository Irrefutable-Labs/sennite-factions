#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "SenniteGameInstance.generated.h"

/** The running game. Loads the player's hero when the game starts, so joining a match does not stall on it. */
UCLASS()
class USenniteGameInstance : public UGameInstance
{
	GENERATED_BODY()
	
public:
	virtual void Init() override;

	/** Load a hero (its mesh, animation class and firing animation) and hold it for as long as the game runs. */
	void KeepHero(FName Id);

	/** Why the last join or match ended against the player's will (a server not there, or gone), once: the menu says it. */
	FString TakeFailure();

private:
	/** The heroes' meshes and animation classes, held for as long as the game runs. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> HeroAssets;

	TSet<FName> KeptHeroes;

	FString Failure;
	void OnNetworkFailure(UWorld* World, class UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Why);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Why);
};
