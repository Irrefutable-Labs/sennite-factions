// The building pieces: a wall, a ramp and a floor in each of the three materials, and the
// see-through preview of each. What each material looks like is set in DefaultGame.ini
// ([/Script/Sennite.BuildingActor]); the names here are the game's three strengths, weakest first.

#pragma once

#include "CoreMinimal.h"
#include "BuildingActor.h"
#include "SenniteBuildings.generated.h"

UCLASS()
class SENNITE_API ASenniteWoodWall : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteWoodWall() { SetPiece(TEXT("Wall"), 0, false); }
};

UCLASS()
class SENNITE_API ASenniteWoodWallPreview : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteWoodWallPreview() { SetPiece(TEXT("Wall"), 0, true); }
};

UCLASS()
class SENNITE_API ASenniteStoneWall : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteStoneWall() { SetPiece(TEXT("Wall"), 1, false); }
};

UCLASS()
class SENNITE_API ASenniteStoneWallPreview : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteStoneWallPreview() { SetPiece(TEXT("Wall"), 1, true); }
};

UCLASS()
class SENNITE_API ASenniteSteelWall : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteSteelWall() { SetPiece(TEXT("Wall"), 2, false); }
};

UCLASS()
class SENNITE_API ASenniteSteelWallPreview : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteSteelWallPreview() { SetPiece(TEXT("Wall"), 2, true); }
};

UCLASS()
class SENNITE_API ASenniteWoodRamp : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteWoodRamp() { SetPiece(TEXT("Ramp"), 0, false); }
};

UCLASS()
class SENNITE_API ASenniteWoodRampPreview : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteWoodRampPreview() { SetPiece(TEXT("Ramp"), 0, true); }
};

UCLASS()
class SENNITE_API ASenniteStoneRamp : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteStoneRamp() { SetPiece(TEXT("Ramp"), 1, false); }
};

UCLASS()
class SENNITE_API ASenniteStoneRampPreview : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteStoneRampPreview() { SetPiece(TEXT("Ramp"), 1, true); }
};

UCLASS()
class SENNITE_API ASenniteSteelRamp : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteSteelRamp() { SetPiece(TEXT("Ramp"), 2, false); }
};

UCLASS()
class SENNITE_API ASenniteSteelRampPreview : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteSteelRampPreview() { SetPiece(TEXT("Ramp"), 2, true); }
};

UCLASS()
class SENNITE_API ASenniteWoodFloor : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteWoodFloor() { SetPiece(TEXT("Floor"), 0, false); }
};

UCLASS()
class SENNITE_API ASenniteWoodFloorPreview : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteWoodFloorPreview() { SetPiece(TEXT("Floor"), 0, true); }
};

UCLASS()
class SENNITE_API ASenniteStoneFloor : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteStoneFloor() { SetPiece(TEXT("Floor"), 1, false); }
};

UCLASS()
class SENNITE_API ASenniteStoneFloorPreview : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteStoneFloorPreview() { SetPiece(TEXT("Floor"), 1, true); }
};

UCLASS()
class SENNITE_API ASenniteSteelFloor : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteSteelFloor() { SetPiece(TEXT("Floor"), 2, false); }
};

UCLASS()
class SENNITE_API ASenniteSteelFloorPreview : public ABuildingActor
{
	GENERATED_BODY()

public:
	ASenniteSteelFloorPreview() { SetPiece(TEXT("Floor"), 2, true); }
};
