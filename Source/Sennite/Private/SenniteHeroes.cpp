#include "SenniteHeroes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SennitePublisher.h"

const FSenniteHero* USenniteHeroes::Find(FName Id)
{
	return All().FindByPredicate([Id](const FSenniteHero& Hero) { return Hero.Id == Id; });
}

const FSenniteHero& USenniteHeroes::FindOrDefault(FName Id)
{
	if (const FSenniteHero* Hero = Find(Id)) {
		return *Hero;
	}
	if (const FSenniteHero* Hero = Find(GetDefault<USenniteHeroes>()->DefaultHero)) {
		return *Hero;
	}
	// a game whose ini lists no heroes has no body to give anyone: that is a broken build, not a choice
	checkf(All().Num() > 0, TEXT("No heroes listed under [/Script/Sennite.SenniteHeroes] in DefaultGame.ini"));
	return All()[0];
}

bool USenniteHeroes::MayPlay(const UObject* WorldContext, FName Id)
{
	const FSenniteHero* Hero = Find(Id);
	if (Hero == nullptr) {
		return false;
	}
	if (Hero->bFree) {
		return true;
	}
	const UWorld* World = WorldContext != nullptr ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* Game = World != nullptr ? World->GetGameInstance() : nullptr;
	const USennitePublisher* Shop = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
	return Shop != nullptr && Shop->OwnsHero(Hero->Id.ToString());
}

FString USenniteProfile::Slot()
{
	FString Name;
	return FParse::Value(FCommandLine::Get(), TEXT("ProfileSlot="), Name) && !Name.IsEmpty() ? Name : FString(TEXT("Profile"));
}

USenniteProfile& USenniteProfile::Get()
{
	static TStrongObjectPtr<USenniteProfile> Kept;
	if (!Kept.IsValid()) {
		USenniteProfile* Read = Cast<USenniteProfile>(UGameplayStatics::LoadGameFromSlot(Slot(), 0));
		Kept.Reset(Read != nullptr ? Read : NewObject<USenniteProfile>());
	}
	return *Kept.Get();
}

void USenniteProfile::Save()
{
	if (!UGameplayStatics::SaveGameToSlot(this, Slot(), 0)) {
		UE_LOG(LogTemp, Error, TEXT("The player's profile could not be saved to slot %s"), *Slot());
	}
}

FName USenniteProfile::PlayingHero(const UObject* WorldContext) const
{
	return USenniteHeroes::MayPlay(WorldContext, Hero) ? Hero : USenniteHeroes::FindOrDefault(NAME_None).Id;
}

FString USenniteLocalPlayer::GetGameLoginOptions() const
{
	// a hero bought in the shop is claimed once in, with a hero pass made out to that server (ASennitePlayerState)
	return FString::Printf(TEXT("Hero=%s"), *USenniteProfile::Get().PlayingHero(this).ToString());
}
