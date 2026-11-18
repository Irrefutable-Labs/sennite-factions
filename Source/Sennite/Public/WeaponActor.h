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
	int WeaponType; // 0 for pickaxe, 1 for assault rifle, 2 for shotgun

	virtual bool IsSupportedForNetworking() const override
	{
		return true;
	}
};
