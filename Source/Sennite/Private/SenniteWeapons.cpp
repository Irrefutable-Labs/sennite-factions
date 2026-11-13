#include "SenniteWeapons.h"
#include "GameFramework/ProjectileMovementComponent.h"

namespace
{
	void SetBullet(AProjectileActor* Bullet, int32 Type, float Damage, float Speed, float Lifespan)
	{
		Bullet->ProjectileType = Type;
		Bullet->Damage = Damage;
		Bullet->ProjectileSpeed = Speed;
		Bullet->Lifespan = Lifespan;
		Bullet->ProjectileMovementComponent->InitialSpeed = Speed;
		Bullet->ProjectileMovementComponent->MaxSpeed = Speed;
	}
}

ASenniteRifleBullet::ASenniteRifleBullet()
{
	// 300 m of reach
	SetBullet(this, 1, 25.0f, 30000.0f, 1.0f);
}

ASenniteShotgunBullet::ASenniteShotgunBullet()
{
	// 30 m of reach
	SetBullet(this, 2, 80.0f, 15000.0f, 0.2f);
}

ASenniteRifle::ASenniteRifle()
{
	WeaponType = 1;
	MagazineSize = 30;
	CurrentBulletCount = MagazineSize;
	BulletClass = ASenniteRifleBullet::StaticClass();
}

ASenniteShotgun::ASenniteShotgun()
{
	WeaponType = 2;
	MagazineSize = 5;
	CurrentBulletCount = MagazineSize;
	BulletClass = ASenniteShotgunBullet::StaticClass();
}
