// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildingActor.generated.h"

class UStaticMeshComponent;

UCLASS(config=Game)
class SENNITE_API ABuildingActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ABuildingActor();

	UPROPERTY(EditDefaultsOnly, Replicated, Category = "Health")
	float Health;

	UPROPERTY(EditDefaultsOnly, Category = "Preview")
	bool IsPreview;

	UPROPERTY(EditDefaultsOnly, Category = "Type")
	int MaterialType; // 0 for wood, 1 for stone, 2 for steel

	UPROPERTY(EditDefaultsOnly, Category = "Type")
	FString StructureType; // Wall, Ramp, Floor

	UPROPERTY()
	int Id;

	/** The piece's body: a slab one build cell across. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	UStaticMeshComponent* PieceMesh;

	/** The unit cube every piece is shaped from. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath PieceShape;

	/** What walls and ramps of each material (0, 1, 2) are faced with. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	TArray<FSoftObjectPath> TierMaterials;

	/** What floors are faced with. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath FloorMaterial;

	/** What a preview of a piece is drawn with. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath PreviewMaterial;

	/** How much damage a piece of each material (0, 1, 2) takes before it falls. Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Health")
	TArray<float> TierHealth;

	/** One side of a build cell; a piece is this wide and, for a wall or ramp, this high. */
	static constexpr float Cell = 480.0f;

	/** How thick a piece is. */
	static constexpr float Thickness = 20.0f;

	virtual void PreInitializeComponents() override;

protected:
	/** Called by each piece class's constructor: what it is, what it is made of, and whether it is only a preview. */
	void SetPiece(const TCHAR* Kind, int32 Material, bool bPreview);


protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	virtual bool IsSupportedForNetworking() const override
	{
		return true;
	}
};
