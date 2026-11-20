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

/** The energy rifle's bolt: light, quick to follow the last. */
UCLASS()
class SENNITE_API ASenniteEnergyBolt : public AProjectileActor
{
	GENERATED_BODY()

public:
	ASenniteEnergyBolt();
};

/** The sniper's round: one heavy shot that reaches across an arena. */
UCLASS()
class SENNITE_API ASenniteSniperBullet : public AProjectileActor
{
	GENERATED_BODY()

public:
	ASenniteSniperBullet();
};

/** Weapon slot 3: the energy rifle. */
UCLASS()
class SENNITE_API ASenniteEnergyRifle : public AWeaponActor
{
	GENERATED_BODY()

public:
	ASenniteEnergyRifle();
};

/** Weapon slot 4: the sniper. */
UCLASS()
class SENNITE_API ASenniteSniper : public AWeaponActor
{
	GENERATED_BODY()

public:
	ASenniteSniper();
};

/** Weapon slot 5: the axe, swung. */
UCLASS()
class SENNITE_API ASenniteAxe : public AWeaponActor
{
	GENERATED_BODY()

public:
	ASenniteAxe();
};

/** Weapon slot 6: the katana, swung. */
UCLASS()
class SENNITE_API ASenniteKatana : public AWeaponActor
{
	GENERATED_BODY()

public:
	ASenniteKatana();
};

/** The bolt of a hero's own weapon, with no gun in hand: lighter than a rifle round, and it harvests as the pickaxe did. */
UCLASS()
class SENNITE_API ASenniteOwnBolt : public AProjectileActor
{
	GENERATED_BODY()

public:
	ASenniteOwnBolt();
};
