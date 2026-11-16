// A piece built in a fight: a wall, a ramp or a floor one build cell across, in wood, stone or steel.
//
// A piece is made of tiles: a wall of three by three, a ramp or a floor of two by two. Its builder's
// side can edit it, cutting tiles out for a door, a window or a hole and putting them back; the cut
// tiles are gone for players and bullets alike. A piece shows what it has taken: its tiles are knocked
// out of line, more the closer it is to falling, the same way on every machine.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildingActor.generated.h"

class APlayerState;
class ASenniteCharacter;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

UCLASS(config=Game)
class SENNITE_API ABuildingActor : public AActor
{
	GENERATED_BODY()

public:
	ABuildingActor();

	/** What it has left before it falls. Starts at its material's strength. */
	UPROPERTY(EditDefaultsOnly, ReplicatedUsing = OnRep_Shape, Category = "Health")
	float Health;

	/** The faction of the player who built it: in a match a shot counts on a piece only from another faction. Empty outside a match. */
	UPROPERTY(Replicated)
	FString FactionId;

	/** The player who built it. Outside a match only they may edit it; in a match, anyone of its faction. */
	UPROPERTY(Replicated)
	TObjectPtr<APlayerState> Builder;

	/** The tiles cut out of it, one bit a tile (see TileTransform for the order). */
	UPROPERTY(ReplicatedUsing = OnRep_Shape)
	int32 Cut = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Preview")
	bool IsPreview;

	UPROPERTY(EditDefaultsOnly, Category = "Type")
	int MaterialType; // 0 for wood, 1 for stone, 2 for steel

	UPROPERTY(EditDefaultsOnly, Category = "Type")
	FString StructureType; // Wall, Ramp, Floor

	UPROPERTY()
	int Id;

	/** The piece's body: one instance of the shape for each tile not cut out. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	UInstancedStaticMeshComponent* PieceMesh;

	/** While its player edits it, on that player's machine only: the tiles marked to be cut, see-through. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	UInstancedStaticMeshComponent* GhostTiles;

	/** The unit cube every tile is shaped from. Set in DefaultGame.ini. */
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

	/** How far out of line a tile is knocked when its piece is about to fall: degrees, and centimetres across the piece. */
	static constexpr float WornTilt = 5.0f;
	static constexpr float WornPush = 6.0f;

	/** How many tiles it is made of: nine for a wall, four for a ramp or a floor. */
	int32 TileCount() const;

	/** Every tile's bit. */
	int32 AllTiles() const { return (1 << TileCount()) - 1; }

	/**
	 * Where a tile sits, from the actor: a wall's tiles go along its X axis by column and up by row
	 * (index = row * 3 + column, row 0 at the bottom); a ramp's or a floor's go along X by row from its
	 * near or low end and along Y by column (index = row * 2 + column). Worn, the tile is knocked out of
	 * line as far as the piece is hurt.
	 */
	FTransform TileTransform(int32 Tile, bool bWorn) const;

	/** The tile, cut or not, a line from From along Way meets first within Reach, or INDEX_NONE; OutDistance is how far along the line. */
	int32 TileUnderAim(const FVector& From, const FVector& Way, double Reach, double& OutDistance) const;

	/** The strength of its material: what it starts with. */
	float MaxHealth() const;

	/** "WOOD WALL", "STEEL RAMP", ... */
	FString Describe() const;

	/** Whether this player may edit it: its builder, or in a match anyone of its faction. */
	bool MayBeEditedBy(const APlayerState* Player) const;

	/** A bullet's damage, on the server: in a match only from another faction while the match is played. At nothing it falls. */
	void TakeHit(float Damage, ASenniteCharacter* Shooter);

	/** Draw it with these tiles cut out and these shown see-through; for its own player while editing, else what Cut says. */
	void ShowCut(int32 Shown, int32 Ghosts);

	/** The player on this machine who is editing it, if one is. While they are, what arrives from the server does not redraw it. */
	TWeakObjectPtr<ASenniteCharacter> EditingHere;

	virtual void PreInitializeComponents() override;

	/** A piece that falls breaks into its tiles where it stood (ASennitePieceRubble), on every machine that draws. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	/** Called by each piece class's constructor: what it is, what it is made of, and whether it is only a preview. */
	void SetPiece(const TCHAR* Kind, int32 Material, bool bPreview);

	UFUNCTION()
	void OnRep_Shape();

	/** Lay the tiles as ShownCut and ShownGhosts say, worn as far as it is hurt. */
	void LayTiles();

	int32 ShownCut = 0;
	int32 ShownGhosts = 0;

public:
	virtual bool IsSupportedForNetworking() const override
	{
		return true;
	}
};

/**
 * What is left of a fallen piece for a moment: its tiles as they last stood, tumbling down and
 * shrinking away. Drawn by each machine for itself; it stops nothing and is gone in a second or two.
 */
UCLASS()
class SENNITE_API ASennitePieceRubble : public AActor
{
	GENERATED_BODY()

public:
	ASennitePieceRubble();

	/** Lay out the tiles of a piece, in the world, and send each off its own way. */
	void Scatter(UStaticMesh* Shape, UMaterialInterface* Face, const TArray<FTransform>& Tiles, int32 Seed);

	virtual void Tick(float DeltaSeconds) override;

	/** How long the tiles fall before they are gone. */
	static constexpr float Seconds = 1.4f;

	UPROPERTY(VisibleAnywhere, Category = "Mesh")
	UInstancedStaticMeshComponent* Tiles;

private:
	struct FFlying
	{
		FTransform Start;
		FVector Velocity;
		FVector Spin;
	};
	TArray<FFlying> Flying;
	float Age = 0.0f;
};
