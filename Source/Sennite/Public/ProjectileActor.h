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

	/** What this bullet is seen as instead of a bolt, set per bullet class: one of the owner's ammo models
	 * (Tools/import_ammo.py), drawn as it is, its point the way it flies. The owner, 2026-11-19: "I got bullet models";
	 * asked which gun fires which, the owner chose the rifle, shotgun and sniper firing the ammo models. None set: the bolt. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath Round;

	/** Or one of the owner's laser beams ("Stylized Laser Beam VFX"), carried by the bullet, with the pack's muzzle
	 * flash where it leaves and its hit where it lands. The owner chose the energy rifle and the heroes' own shots
	 * firing the laser beams. Each beam starts where it is carried and ends at the point it is given as LaserEnd; the
	 * bullet gives it the point LaserTrail behind itself (less until it has flown that far), so it is a short beam
	 * trailing the shot, a few frames of its flight long. The pack's flash and hit go on until stopped: each is stopped after its seconds here. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath Laser;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath LaserFlash;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	FSoftObjectPath LaserHit;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	float LaserTrail = 400.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	float LaserFlashSeconds = 0.12f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Mesh")
	float LaserHitSeconds = 0.35f;

	/** The laser the bullet carries, on every machine that draws it; none for a round or a bolt. */
	UPROPERTY(Transient)
	TObjectPtr<class UNiagaraComponent> Beam;

	/** How far the bullet has flown, on this machine. */
	float Flown = 0.0f;

	virtual void PreInitializeComponents() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Play one of the pack's flash or hit effects here, and stop it after so many seconds: they go on until stopped. */
	static void PlayFor(UObject* WorldContext, const FSoftObjectPath& Effect, const FVector& Where, const FRotator& Facing, float Seconds);

	/** Where the laser ends: LaserTrail behind the bullet, or where it left if it has not flown that far. */
	void LayLaser();

	/** Every bullet's round and laser, loaded before the first shot: loaded at the shot, the first one waits
	 * on them and is spent before it has flown. Kept loaded by what is passed in. */
	static void Preload(TArray<TObjectPtr<UObject>>& Into);

	/** Seen as one of the ammo models, drawn as it is rather than stretched as a bolt. */
	bool bRound = false;

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
	int ProjectileType; // the kind of weapon that fired it: 0 for pickaxe, 1 rifle, 2 shotgun, 3 energy rifle, 4 sniper

	/** The server: what a bullet, or a blow of a hero's own weapon, does to what it meets. It takes Damage off a player
	 * (one hit through what they hold), a build piece, a faction's structure or a material node, and a blow of kind 0 (the
	 * pickaxe's) harvests the node. Answers whether it landed: false for what lets it by (the one who dealt it, a weapon
	 * or an item lying on the ground, a preview), true for everything else, which stops it. */
	static bool Land(AActor* OtherActor, float Damage, int32 Kind, ASenniteCharacter* Shooter);

	/** Set once the bullet has landed on something; it lands only once. */
	bool bSpent = false;

	UFUNCTION()
	void SelfDestruct();

private:
	/** Size the bolt to how far the bullet has flown. */
	void LayBolt();
};
