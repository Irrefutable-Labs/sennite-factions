// The weapons a player takes from a crate, and the bullets they fire.

#pragma once

#include "CoreMinimal.h"
#include "ProjectileActor.h"
#include "WeaponActor.h"
#include "SenniteWeapons.generated.h"

/** The assault rifle's bullet: fast and long-ranged. */
UCLASS()
class SENNITE_API ASenniteRifleBullet : public AProjectileActor
{
	GENERATED_BODY()

public:
	ASenniteRifleBullet();
};

/** The shotgun's shot: heavy, and gone after a short distance. */
UCLASS()
class SENNITE_API ASenniteShotgunBullet : public AProjectileActor
{
	GENERATED_BODY()

public:
	ASenniteShotgunBullet();
};

/** Weapon slot 1: the assault rifle. */
UCLASS()
class SENNITE_API ASenniteRifle : public AWeaponActor
{
	GENERATED_BODY()

public:
	ASenniteRifle();
};

/** Weapon slot 2: the shotgun. */
UCLASS()
class SENNITE_API ASenniteShotgun : public AWeaponActor
{
	GENERATED_BODY()

public:
	ASenniteShotgun();
};
