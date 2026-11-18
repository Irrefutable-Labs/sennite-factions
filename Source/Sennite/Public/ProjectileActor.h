// A bullet. The server flies it and decides what it lands on; every machine sees it: a bright bolt
// that leaves the muzzle and travels, drawn as a streak that trails behind the bullet and grows to
// its length as the bullet gets clear of the gun. What a bolt is shaped from and drawn with is one
// setting for all bullets; how long, how thick and what colour is set per bullet class, all in
// DefaultGame.ini.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectileActor.generated.h"

class UProjectileMovementComponent;
class USphereComponent;
class AWeaponActor;
class ASenniteCharacter;

UCLASS(Config = Game)
class SENNITE_API AProjectileActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AProjectileActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/** The bolt the bullet is seen as. Seen only: it touches nothing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	UStaticMeshComponent* Bolt;

	/** The unit cube a bolt is shaped from, and what it is drawn with (its "Color" is the bolt's colour). One setting for all bullets. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath BoltShape;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath BoltMaterial;

	/** How long and how thick this bullet's bolt is, in cm, and its colour. Set per bullet class. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	float BoltLength = 300.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	float BoltWidth = 4.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FLinearColor BoltColour = FLinearColor(1.0f, 0.5f, 0.1f);

	/** How far the bullet has flown, on this machine. */
	float Flown = 0.0f;

	virtual void PreInitializeComponents() override;

	/** Sphere collision component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	USphereComponent* CollisionComp;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	UProjectileMovementComponent* ProjectileMovementComponent;

	/* called when projectile hits something */
	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	AWeaponActor* Weapon;

	ASenniteCharacter* WeaponHolder;

	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float Damage;

	UPROPERTY(EditDefaultsOnly, Category = "Lifespan")
	float Lifespan; //used to distinguish range or projectile 

	UPROPERTY(EditDefaultsOnly, Category = "Speed")
	float ProjectileSpeed; //used to distinguish range or projectile 

	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	int ProjectileType; // 0 for pickaxe, 1 for assault rifle, 2 for shotgun, will be used for determining the lifespan of the projectile

	/** Set once the bullet has landed on something; it lands only once. */
	bool bSpent = false;

	UFUNCTION()
	void SelfDestruct();

private:
	/** Size the bolt to how far the bullet has flown. */
	void LayBolt();
};
