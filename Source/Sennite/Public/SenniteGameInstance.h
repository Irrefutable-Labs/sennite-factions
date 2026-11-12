#pragma once

#include "CoreMinimal.h"
#include "SenniteGameInstance.generated.h"


UCLASS()
class  USenniteGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:

	virtual void Init() override;

};