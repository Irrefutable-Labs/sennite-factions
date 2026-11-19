// The healing items a player takes from a crate and uses.

#pragma once

#include "CoreMinimal.h"
#include "HealingActor.h"
#include "SenniteHealing.generated.h"

/** Healing slot 0: a pack of bandages. Each one used gives back health. */
UCLASS()
class SENNITE_API ASenniteBandage : public AHealingActor
{
	GENERATED_BODY()

public:
	ASenniteBandage() { HealingType = 0; Count = 5; }
};

/** Healing slot 1: potions. Each one used adds shield. */
UCLASS()
class SENNITE_API ASennitePotion : public AHealingActor
{
	GENERATED_BODY()

public:
	ASennitePotion() { HealingType = 1; Count = 2; }
};
