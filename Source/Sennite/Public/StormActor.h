// The storm: a circle of safe ground that shrinks in steps. A player standing inside the circle is
// safe; outside it the player loses Damage health every second (ASenniteCharacter::ServerApplyStormDamage).
//
// The game has no storm for now: bEnabled is off in DefaultGame.ini. With it off a storm placed in a
// map is not seen, is never started and hurts nobody; the storm check turns it on for its own run.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StormActor.generated.h"

class UStaticMeshComponent;

UCLASS(config=Game)
class SENNITE_API AStormActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AStormActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	virtual void PreInitializeComponents() override;

	/** The wall of the safe circle, as the player sees it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Storm")
	UStaticMeshComponent* Wall;

	/** The safe circle's radius when the match starts. Set on the storm placed in a map. */
	UPROPERTY(EditAnywhere, Category = "Storm")
	float StartRadius = 20000.0f;

	/** Whether the circle moves to a random centre inside RandomCentre when the match starts. Set on the storm placed in a map. */
	UPROPERTY(EditAnywhere, Category = "Storm")
	bool bRandomCentre = false;

	/** Where a random centre may fall, when bRandomCentre is set. */
	UPROPERTY(EditAnywhere, Category = "Storm")
	FBox2D RandomCentre = FBox2D(FVector2D(-10000.0, -10000.0), FVector2D(10000.0, 10000.0));

	/** Whether the game has a storm at all. Set in DefaultGame.ini, the same on every machine. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Storm")
	bool bEnabled = false;

	/** The storm of this world, or none: none when the game has no storm, whatever the map holds. */
	static AStormActor* Find(const UWorld* World);

	/** The cylinder the wall is shaped from. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Storm")
	FSoftObjectPath WallShape;

	/** What the wall is drawn with. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Storm")
	FSoftObjectPath WallMaterial;

	/** How long one stage lasts. The circle holds for four stages, then shrinks for three. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Storm")
	float StageSeconds = 30.0f;

	/** How long between each rise of one in the damage the storm does every second. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Storm")
	float DamageRiseSeconds = 240.0f;

	/** The part of its radius the circle keeps for each second it shrinks. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Storm")
	float KeptPerSecond = 0.97f;

	/** The safe circle's radius now. */
	float GetRadius() const;

	UPROPERTY(Replicated)
	float Damage;

	UPROPERTY(Replicated)
	bool IsShrinking;

	UPROPERTY(Replicated)
	FVector SizeScale;

	UPROPERTY()
	int Stage;

	virtual bool IsSupportedForNetworking() const override
	{
		return true;
	}

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetIsShrinking();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetNewDamage();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerStartStorm();

};
