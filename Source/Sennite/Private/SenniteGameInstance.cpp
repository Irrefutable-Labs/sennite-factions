#include "SenniteGameInstance.h"
#include "SenniteHeroes.h"
#include "Animation/AnimInstance.h"
#include "Engine/Engine.h"

void USenniteGameInstance::Init()
{
	Super::Init();

	// A hero is a large thing to load. Loaded here, it is in memory before the first map; loaded when
	// the first hero appears, a joining player's game stalls in the middle of joining. The default
	// hero is loaded too: it is what every character is until the server says which hero it is.
	KeepHero(USenniteHeroes::FindOrDefault(NAME_None).Id);
	KeepHero(USenniteProfile::Get().Hero);

	if (GEngine != nullptr) {
		GEngine->OnNetworkFailure().AddUObject(this, &USenniteGameInstance::OnNetworkFailure);
		GEngine->OnTravelFailure().AddUObject(this, &USenniteGameInstance::OnTravelFailure);
	}
}

FString USenniteGameInstance::TakeFailure()
{
	FString Said = MoveTemp(Failure);
	Failure.Reset();
	return Said;
}

void USenniteGameInstance::OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Why)
{
	Failure = Type == ENetworkFailure::ConnectionLost || Type == ENetworkFailure::ConnectionTimeout
		? FString(TEXT("THE CONNECTION TO THE HOST WAS LOST")) : FString::Printf(TEXT("COULD NOT REACH THE HOST: %s"), *Why.ToUpper());
}

void USenniteGameInstance::OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Why)
{
	Failure = FString::Printf(TEXT("COULD NOT JOIN: %s"), *Why.ToUpper());
}

void USenniteGameInstance::KeepHero(FName Id)
{
	const FSenniteHero* Hero = USenniteHeroes::Find(Id);
	if (Hero == nullptr || KeptHeroes.Contains(Id)) {
		return;
	}
	KeptHeroes.Add(Id);
	if (UObject* Mesh = Hero->Mesh.TryLoad()) {
		HeroAssets.Add(Mesh);
	}
	if (UClass* AnimClass = Hero->AnimClass.TryLoadClass<UAnimInstance>()) {
		HeroAssets.Add(AnimClass);
	}
	if (!Hero->FireMontage.IsNull()) {
		if (UObject* Fire = Hero->FireMontage.TryLoad()) {
			HeroAssets.Add(Fire);
		}
	}
}
