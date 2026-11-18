// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeaponActor.generated.h"

class AProjectileActor;
class ASenniteCharacter;
class UStaticMeshComponent;

UCLASS(config=Game)
class SENNITE_API AWeaponActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AWeaponActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/** The weapon's body: what sits in the holder's hand. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	UStaticMeshComponent* WeaponMesh;

	/** The static mesh this weapon wears. Set per weapon class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath MeshAsset;

	/** The point of the mesh that sits in the holder's palm, in the mesh's own space (+X is the muzzle end). Set per weapon class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Grip")
	FVector GripPoint;

	/** What the HUD and a crate call it. Set per weapon class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Gun")
	FString DisplayName;

	/** How long after a shot before the next, and how long a reload takes, in seconds. Set per weapon class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Gun")
	float FireSeconds = 0.233f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Gun")
	float ReloadSeconds = 2.167f;

	/** Whether it goes on firing while the button is held; if not, it fires one shot a pull. Set per weapon class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Gun")
	bool bHeldFires = false;

	/** The rounds a crate gives for it each time it is opened. Set per weapon class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Gun")
	int32 CrateRounds = 0;

	/** What a shot sounds like. Set per weapon class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Gun")
	FSoftObjectPath FireSound;

	/** How a shot fades with distance. One setting for every gun, held by the base class. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Gun")
	FSoftObjectPath FireFalloff;

	/** Where the other hand holds the gun, in the mesh's own space: its foregrip. Set per weapon class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Grip")
	FVector OffHandPoint;

	/** The end of the barrel, in the mesh's own space. Set per weapon class in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Bullet")
	FVector MuzzleOffset;

	/** Where a shot leaves the barrel, in the world. */
	FVector GetMuzzleLocation() const;

	virtual void PreInitializeComponents() override;

	//associated bullet
	UPROPERTY(EditDefaultsOnly, Category = "Bullet")
	TSubclassOf<AProjectileActor> BulletClass;

	UPROPERTY(Replicated)
	ASenniteCharacter* Holder;

	UPROPERTY(EditDefaultsOnly, Category = "Bullet")
	int MagazineSize; // Only applies to assault rifle and shotgun

	UPROPERTY(EditDefaultsOnly, Replicated, Category = "Bullet")
	int CurrentBulletCount; // Only applies to assault rifle and shotgun

	UPROPERTY(EditDefaultsOnly, Category = "WeaponType")
	int WeaponType; // 0 for pickaxe, 1 for the rifle, 2 for the shotgun, 3 for the energy rifle, 4 for the sniper

	virtual bool IsSupportedForNetworking() const override
	{
		return true;
	}
};
