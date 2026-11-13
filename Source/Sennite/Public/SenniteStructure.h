// A faction's structure on its parcel: one prefab of the publisher's catalogue, drawn from the
// building panels the game already has. A structure that stands in play is solid for everyone. A
// plan is see-through, stops nothing, and is shown only in the faction's editor.
//
// How each prefab is put together is set in DefaultGame.ini ([/Script/Sennite.SenniteStructure]).
// It is drawing only: what a prefab is, costs and covers is the catalogue's.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SenniteStructure.generated.h"

class UStaticMeshComponent;

/** How one prefab is put together from panels. */
USTRUCT()
struct FSenniteShape
{
	GENERATED_BODY()

	/** The catalogue's prefab_id. */
	UPROPERTY()
	FString Prefab;

	/** "Enclosure": walls around what it covers. "Slab": one wall across it. */
	UPROPERTY()
	FString Kind;

	/** How many storeys of walls, each one build cell high. */
	UPROPERTY()
	int32 Storeys = 1;

	/** A floor over the top storey. */
	UPROPERTY()
	bool bRoof = false;

	/** A low wall around the roof. */
	UPROPERTY()
	bool bParapet = false;

	/** The ground storey is open on one side. */
	UPROPERTY()
	bool bOpenFront = false;

	/** Which of the three panel materials its walls are faced with (0, 1, 2). */
	UPROPERTY()
	int32 Tier = 0;
};

UCLASS(Config = Game)
class SENNITE_API ASenniteStructure : public AActor
{
	GENERATED_BODY()

public:
	ASenniteStructure();

	/** How each prefab is put together. Set in DefaultGame.ini. */
	UPROPERTY(Config)
	TArray<FSenniteShape> Shapes;

	/**
	 * Put the structure together: the prefab, the ground it covers before it is turned (map units),
	 * and whether it stands in play (solid) or is a plan (see-through, stops nothing).
	 */
	void Build(const FString& InPrefabId, int32 Width, int32 Depth, bool bInStanding);

	FString PrefabId;
	bool bStanding = false;

	/** The panels it is made of. */
	UPROPERTY()
	TArray<UStaticMeshComponent*> Panels;

private:
	void AddPanel(const FVector& Centre, const FVector& Size, class UMaterialInterface* Face);
};
