// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HealingActor.generated.h"

class ASenniteCharacter;

class UStaticMeshComponent;

UCLASS(config=Game)
class SENNITE_API AHealingActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AHealingActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(Replicated)
	ASenniteCharacter* Holder;

	/** The item's body: what sits in the holder's hand. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	UStaticMeshComponent* ItemMesh;

	/** The static mesh this item wears. Set per item class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath MeshAsset;

	/** The mesh's full size; it is half this in the hand. Set per item class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FVector MeshScale = FVector::OneVector;

	/** The material the mesh is drawn with, and the colour given to its "Color" parameter. Set per item class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath MeshMaterial;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FLinearColor MeshColour = FLinearColor::White;

	virtual void PreInitializeComponents() override;

	UPROPERTY(EditDefaultsOnly, Category = "Health")
	float Health;

	UPROPERTY(EditDefaultsOnly, Category = "Health")
	int HealingType; // 0 for bandage, 1 for potion

	UPROPERTY(EditDefaultsOnly, Category = "Health")
	int Count;

	virtual bool IsSupportedForNetworking() const override
	{
		return true;
	}
};
