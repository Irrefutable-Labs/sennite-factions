#include "SenniteGameInstance.h"
#include "SenniteCharacter.h"
#include "Animation/AnimInstance.h"

void USenniteGameInstance::Init()
{
	Super::Init();

	// A hero is a large thing to load. Loaded here, it is in memory before the first map; loaded when
	// the first hero appears, a joining player's game stalls in the middle of joining.
	const ASenniteCharacter* Hero = GetDefault<ASenniteCharacter>();
	if (UObject* Mesh = Hero->HeroMesh.TryLoad()) {
		HeroAssets.Add(Mesh);
	}
	if (UClass* AnimClass = Hero->HeroAnimClass.TryLoadClass<UAnimInstance>()) {
		HeroAssets.Add(AnimClass);
	}
	if (UObject* Fire = Hero->HeroFireMontage.TryLoad()) {
		HeroAssets.Add(Fire);
	}
}
