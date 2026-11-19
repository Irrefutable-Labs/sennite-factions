// Menu check. Run in the game on the Menu map, with a profile of its own (-ProfileSlot=), by taps on
// the screen as a phone gives them:
// - the menu is the first thing shown: no hero to play, the player's hero turning on the stage;
// - the menu has its songs: a game just started is on the first, and after each comes the next, round again;
// - STORE opens the store on its heroes, with SKINS and SWAP beside them: on a game with no shop
//   neither sells anything, and says so; BASE EDITOR is dimmed there too, and says why;
// - in the heroes the arrows go through the heroes, each one standing and turning on the stage as itself;
// - a hero bought with D-bucks cannot be chosen on a game with no shop;
// - CHOOSE makes a free hero the player's, kept in the profile on disk;
// - SETTINGS opens over the first screen with a music volume of its own, below everything else's, and a
//   tap turns it down; each kind of button has a sound of its own;
// - PLAY opens the arena, the land the game is played on, and the player is that hero there.
//
// Tools/check_menu.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MenuCheck.h"
#include "SenniteGameMenu.h"
#include "SenniteCharacter.h"
#include "SenniteHeroes.h"
#include "SenniteSounds.h"
#include "SennitePlayerState.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogMenuCheck, Log, All);

namespace
{
	/** The yaw of the one hero standing on the stage, or a number no yaw is when there is not one. */
	double StageYaw(UWorld* World)
	{
		TArray<ASkeletalMeshActor*> Figures;
		for (TActorIterator<ASkeletalMeshActor> It(World); It; ++It) {
			Figures.Add(*It);
		}
		return Figures.Num() == 1 ? Figures[0]->GetActorRotation().Yaw : 1000.0;
	}

	FString StageMesh(UWorld* World)
	{
		TActorIterator<ASkeletalMeshActor> It(World);
		return It ? FSoftObjectPath(It->GetSkeletalMeshComponent()->GetSkeletalMeshAsset()).ToString() : FString();
	}

	class FMenuRun : public IAutomationLatentCommand
	{
	public:
		explicit FMenuRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}

		virtual bool Update() override;

	private:
		enum class EPhase { WaitForMenu, MenuShot, Settings, SettingsQuieter, SettingsBack, Heroes, Skins, Weapons, DBucks, HeroesAgain, ToWraith, Turning, ToLocked, Locked, BackToWraith, Chosen, Play, Done };

		void Enter(EPhase Next) { Phase = Next; PhaseStart = FPlatformTime::Seconds(); }
		double Elapsed() const { return FPlatformTime::Seconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForMenu;
		double PhaseStart = 0.0;
		double YawBefore = 0.0;
		FName Free;
		FName Locked;
		float MusicBefore = 0.0f;
	};

	bool FMenuRun::Update()
	{
		if (FPlatformTime::Seconds() - RealStart > 240.0) {
			return Fail(FString::Printf(TEXT("The menu check did not finish in 240 s (stuck at step %d)"), int32(Phase)));
		}
		UWorld* World = GameCheck::FindGameWorld();
		ASenniteMenuController* Menu = MenuCheck::Menu(World);
		const TArray<FSenniteHero>& Heroes = USenniteHeroes::All();

		switch (Phase) {
		case EPhase::WaitForMenu: {
			if (Menu == nullptr || !MenuCheck::Has(Menu, TEXT("play")) || World->GetTimeSeconds() < 2.0) {
				return false;
			}
			// the heroes this check uses: the first free hero that is not the default, and the first that is bought
			const FName Default = USenniteHeroes::FindOrDefault(NAME_None).Id;
			for (const FSenniteHero& Hero : Heroes) {
				if (Free.IsNone() && Hero.bFree && Hero.Id != Default) {
					Free = Hero.Id;
				}
				if (Locked.IsNone() && !Hero.bFree) {
					Locked = Hero.Id;
				}
			}
			if (Free.IsNone() || Locked.IsNone()) {
				return Fail(TEXT("The list needs a second free hero and a bought one for this check"));
			}
			Test->TestEqual(TEXT("The game opens on the menu"), UGameplayStatics::GetCurrentLevelName(World), FString(TEXT("Menu")));
			Test->TestNull(TEXT("At the menu the player has no hero to play"), Menu->GetPawn());
			Test->TestEqual(TEXT("A new profile's hero is the default hero"), USenniteProfile::Get().PlayingHero(Menu), Default);
			Test->TestEqual(TEXT("The menu names the player's hero"), MenuCheck::Line(Menu, TEXT("hero")), USenniteHeroes::FindOrDefault(Default).Name);
			Test->TestEqual(TEXT("The player's hero stands on the stage"), StageMesh(World), USenniteHeroes::FindOrDefault(Default).Mesh.ToString());
			// the menu's songs: a game just started is on the first, and when one ends the next begins, round again
			{
				const TArray<FSoftObjectPath>& Songs = GetDefault<ASenniteMenuController>()->Songs;
				Test->TestTrue(TEXT("The menu has songs to go round"), Songs.Num() >= 2);
				for (const FSoftObjectPath& Each : Songs) {
					const USoundBase* Sound = Cast<USoundBase>(Each.TryLoad());
					Test->TestTrue(FString::Printf(TEXT("%s is a song, a minute long or more"), *Each.ToString()), Sound != nullptr && Sound->GetDuration() > 60.0f);
				}
				const auto Playing = [&]() { return Songs.IndexOfByKey(FSoftObjectPath(Menu->SongNow())); };
				Test->TestEqual(TEXT("A game just started opens on the first song"), Playing(), 0);
				const bool bHeard = Menu->SongIsHeard();
				for (int32 Next = 1; Next <= Songs.Num(); ++Next) {
					Menu->PlayNextSong();
					Test->TestEqual(TEXT("After a song comes the one after it, and after the last the first"), Playing(), Next % Songs.Num());
					Test->TestEqual(TEXT("It is heard where the first was"), Menu->SongIsHeard(), bHeard);
				}
				UE_LOG(LogMenuCheck, Display, TEXT("GAMECHECK songs: %d, on %s; %s"), Songs.Num(), Menu->SongNow() != nullptr ? *Menu->SongNow()->GetName() : TEXT("none"),
					bHeard ? TEXT("heard") : TEXT("this run has no sound device"));
			}
			// each kind of button has a sound of its own
			{
				TSet<FString> Heard;
				for (const TCHAR* Action : { TEXT("play"), TEXT("tab:skins"), TEXT("next"), TEXT("hero:1"), TEXT("back"), TEXT("choose"), TEXT("num:1"), TEXT("music+") }) {
					const TArray<FSoftObjectPath> Takes = USenniteSounds::Takes(USenniteSounds::ButtonSound(Action));
					Test->TestEqual(FString::Printf(TEXT("The button %s has one sound"), Action), Takes.Num(), 1);
					if (Takes.Num() == 1) {
						Test->TestNotNull(FString::Printf(TEXT("The sound of the button %s loads"), Action), Cast<USoundBase>(Takes[0].TryLoad()));
						Heard.Add(Takes[0].ToString());
					}
				}
				// the owner's three recordings: a button pressed and a thing done; every smaller tick; back
				Test->TestTrue(TEXT("A menu button and a thing done sound alike"), USenniteSounds::Takes(USenniteSounds::ButtonSound(TEXT("play"))) == USenniteSounds::Takes(USenniteSounds::ButtonSound(TEXT("choose"))));
				Test->TestEqual(TEXT("The menus' buttons have three sounds: a press, a smaller tick, and back"), Heard.Num(), 3);
				for (const TCHAR* Action : { TEXT("next"), TEXT("hero:1"), TEXT("num:1"), TEXT("music+") }) {
					Test->TestTrue(FString::Printf(TEXT("The button %s sounds like a section of the store: a smaller tick"), Action), USenniteSounds::Takes(USenniteSounds::ButtonSound(Action)) == USenniteSounds::Takes(USenniteSounds::ButtonSound(TEXT("tab:skins"))));
				}
				Test->TestTrue(TEXT("A smaller tick does not sound like a button pressed"), USenniteSounds::Takes(USenniteSounds::ButtonSound(TEXT("tab:skins"))) != USenniteSounds::Takes(USenniteSounds::ButtonSound(TEXT("play"))));
				Test->TestTrue(TEXT("Back does not sound like a button pressed"), USenniteSounds::Takes(USenniteSounds::ButtonSound(TEXT("back"))) != USenniteSounds::Takes(USenniteSounds::ButtonSound(TEXT("play"))));
				Test->TestEqual(TEXT("The two arrows sound alike"), USenniteSounds::ButtonSound(TEXT("prev")), USenniteSounds::ButtonSound(TEXT("next")));
			}
			for (const TCHAR* Action : { TEXT("play"), TEXT("store"), TEXT("base"), TEXT("host"), TEXT("join"), TEXT("settings"), TEXT("quit") }) {
				Test->TestTrue(FString::Printf(TEXT("The menu has %s"), Action), MenuCheck::Has(Menu, Action));
			}
			{
				// bases are the publisher's server's to keep: with none on this game there is none to plan
				bool bBase = true;
				MenuCheck::Has(Menu, TEXT("base"), &bBase);
				Test->TestFalse(TEXT("BASE EDITOR is dimmed with no publisher's server"), bBase);
				Test->TestEqual(TEXT("The menu says why"), MenuCheck::Line(Menu, TEXT("why")), FString(TEXT("THE BASE EDITOR NEEDS THE PUBLISHER'S SERVER")));
			}
			{
				// A phone keeps the menu clear of its screen's rounded corners and camera: the canvas starts in
				// from the screen's edge. A finger lands on the screen, so each button must take taps where it is
				// drawn, in there, and not that far to its left. -SafeZoneCheck says this run has such a screen.
				FDisplayMetrics Metrics;
				FSlateApplication::Get().GetCachedDisplayMetrics(Metrics);
				const FVector2D Pad(Metrics.TitleSafePaddingSize.X, Metrics.TitleSafePaddingSize.Y);
				const ASenniteMenuHUD* Drawn = MenuCheck::Hud(Menu);
				double Leftmost = 1.0e9;
				double Topmost = 1.0e9;
				for (const FSenniteMenuButton& Each : Drawn != nullptr ? Drawn->Buttons : TArray<FSenniteMenuButton>()) {
					Leftmost = FMath::Min(Leftmost, Each.Box.Min.X);
					Topmost = FMath::Min(Topmost, Each.Box.Min.Y);
				}
				Test->TestTrue(TEXT("Every button takes taps inside the screen's safe zone, where it is drawn"), Leftmost >= Pad.X * 0.9 && Topmost >= Pad.Y * 0.9);
				if (FParse::Param(FCommandLine::Get(), TEXT("SafeZoneCheck"))) {
					Test->TestTrue(TEXT("This run's screen has a safe zone, as a phone's has"), Pad.X > 8.0);
				}
				UE_LOG(LogMenuCheck, Display, TEXT("GAMECHECK safe zone: %.0f by %.0f pixels in; the buttons take taps from %.0f, %.0f"), Pad.X, Pad.Y, Leftmost, Topmost);
			}
			GameCheck::Shot(TEXT("Menu"), TEXT("menu.png"));
			UE_LOG(LogMenuCheck, Display, TEXT("GAMECHECK menu: hero=%s free=%s bought=%s"), *Default.ToString(), *Free.ToString(), *Locked.ToString());
			Enter(EPhase::MenuShot);
			return false;
		}

		case EPhase::MenuShot:
			// the picture is taken at the end of a frame: nothing is tapped until it is
			if (Elapsed() < 0.3) {
				return false;
			}
			MenuCheck::Tap(Menu, TEXT("settings"));
			Enter(EPhase::Settings);
			return false;

		case EPhase::Settings: {
			// SETTINGS opens over the first screen: the music has its own volume, apart from everything's
			USenniteGameMenu* Open = Menu->SettingsIfOpen();
			FVector2D Quieter;
			if ((Open == nullptr || !Open->Centre(TEXT("music-"), Quieter)) && Elapsed() < 3.0) {
				return false;
			}
			if (Open == nullptr || !Open->Centre(TEXT("music-"), Quieter)) {
				return Fail(TEXT("A tap on SETTINGS did not open the settings with a music volume"));
			}
			for (const TCHAR* Action : { TEXT("sens-"), TEXT("sens+"), TEXT("invert"), TEXT("vol-"), TEXT("vol+"), TEXT("music-"), TEXT("music+"), TEXT("quality-"), TEXT("quality+"), TEXT("back") }) {
				FVector2D At;
				Test->TestTrue(FString::Printf(TEXT("SETTINGS has %s"), Action), Open->Centre(Action, At));
			}
			Test->TestFalse(TEXT("The first screen's own buttons are not under it"), MenuCheck::Has(Menu, TEXT("play")));
			MusicBefore = USenniteProfile::Get().MusicVolume;
			Test->TestTrue(TEXT("The music starts below everything else, so a button is heard over it"), MusicBefore > 0.0f && MusicBefore < 0.5f);
			GameCheck::Shot(TEXT("Menu"), TEXT("settings.png"));
			GameCheck::Tap(Quieter);
			Enter(EPhase::SettingsQuieter);
			return false;
		}

		case EPhase::SettingsQuieter: {
			USenniteGameMenu* Open = Menu->SettingsIfOpen();
			if (USenniteProfile::Get().MusicVolume == MusicBefore && Elapsed() < 3.0) {
				return false;
			}
			Test->TestTrue(TEXT("A tap on less turns the music down a step"), FMath::IsNearlyEqual(USenniteProfile::Get().MusicVolume, MusicBefore - 0.1f, 0.051f) && USenniteProfile::Get().MusicVolume < MusicBefore);
			Test->TestEqual(TEXT("Everything else is as loud as it was"), USenniteProfile::Get().Volume, 1.0f);
			UE_LOG(LogMenuCheck, Display, TEXT("GAMECHECK settings: music %.2f -> %.2f, everything %.2f"), MusicBefore, USenniteProfile::Get().MusicVolume, USenniteProfile::Get().Volume);
			FVector2D Back;
			if (Open == nullptr || !Open->Centre(TEXT("back"), Back)) {
				return Fail(TEXT("SETTINGS has no BACK"));
			}
			GameCheck::Tap(Back);
			Enter(EPhase::SettingsBack);
			return false;
		}

		case EPhase::SettingsBack:
			if ((Menu->SettingsIfOpen() != nullptr || !MenuCheck::Has(Menu, TEXT("store"))) && Elapsed() < 3.0) {
				return false;
			}
			Test->TestNull(TEXT("BACK closes SETTINGS"), Menu->SettingsIfOpen());
			Test->TestTrue(TEXT("The first screen is back"), MenuCheck::Has(Menu, TEXT("store")));
			MenuCheck::Tap(Menu, TEXT("store"));
			Enter(EPhase::Heroes);
			return false;

		case EPhase::Heroes:
			if (Menu->Page != ESenniteMenuPage::Heroes && Elapsed() < 3.0) {
				return false;
			}
			if (Elapsed() < 0.5) {
				return false;
			}
			Test->TestTrue(TEXT("A tap on STORE opens the store on its heroes"), Menu->Page == ESenniteMenuPage::Heroes);
			Test->TestEqual(TEXT("The store names the section open"), MenuCheck::Line(Menu, TEXT("section")), FString(TEXT("HEROES")));
			for (const TCHAR* Action : { TEXT("back"), TEXT("tab:heroes"), TEXT("tab:skins"), TEXT("tab:weapons"), TEXT("tab:swap") }) {
				Test->TestTrue(FString::Printf(TEXT("The store has %s"), Action), MenuCheck::Has(Menu, Action));
			}
			Test->TestEqual(TEXT("It opens on the player's own hero"), Heroes[Menu->Browse].Id, USenniteProfile::Get().PlayingHero(Menu));
			Test->TestTrue(TEXT("The player's own hero is marked chosen"), MenuCheck::Has(Menu, TEXT("chosen")));
			{
				// CHOSEN is greyed out: a tap on it does nothing, and is heard to do nothing
				bool bChosenLive = true;
				MenuCheck::Has(Menu, TEXT("chosen"), &bChosenLive);
				Test->TestFalse(TEXT("CHOSEN is greyed out"), bChosenLive);
				const int32 Refused = USenniteSounds::Heard(World, TEXT("ui.denied"));
				const int32 Any = USenniteSounds::HeardBeginning(World, TEXT("ui."));
				Test->TestTrue(TEXT("CHOSEN can be tapped"), MenuCheck::Tap(Menu, TEXT("chosen")));
				Test->TestEqual(TEXT("A tap on a greyed-out button is heard refused"), USenniteSounds::Heard(World, TEXT("ui.denied")) - Refused, 1);
				Test->TestEqual(TEXT("and as nothing else"), USenniteSounds::HeardBeginning(World, TEXT("ui.")) - Any, 1);
				Test->TestTrue(TEXT("and the store stays where it was"), Menu->Page == ESenniteMenuPage::Heroes);
			}
			Test->TestTrue(TEXT("A game with no shop shows no wallet balance"), MenuCheck::Line(Menu, TEXT("balance")).IsEmpty());
			MenuCheck::Tap(Menu, TEXT("tab:skins"));
			Enter(EPhase::Skins);
			return false;

		case EPhase::Skins: {
			if (Elapsed() < 1.0) {
				return false;
			}
			bool bEnabled = true;
			Test->TestTrue(TEXT("A tap on SKINS opens the store's skins"), Menu->Page == ESenniteMenuPage::Skins);
			Test->TestEqual(TEXT("The store names the section open"), MenuCheck::Line(Menu, TEXT("section")), FString(TEXT("SKINS")));
			Test->TestTrue(TEXT("With no shop on this game it says there is none"), MenuCheck::Has(Menu, TEXT("buyskin"), &bEnabled));
			Test->TestFalse(TEXT("and no skin can be bought"), bEnabled);
			Test->TestFalse(TEXT("No skin is shown as if it were for sale"), MenuCheck::Has(Menu, TEXT("next")));
			Test->TestEqual(TEXT("The player's hero stays on the stage"), StageMesh(World), USenniteHeroes::FindOrDefault(USenniteProfile::Get().PlayingHero(Menu)).Mesh.ToString());
			GameCheck::Shot(TEXT("Menu"), TEXT("store_skins.png"));
			Enter(EPhase::Weapons);
			return false;
		}

		case EPhase::Weapons: {
			if (Menu->Page != ESenniteMenuPage::Weapons) {
				// the picture is taken at the end of a frame: nothing is tapped until it is
				if (Elapsed() > 0.3) {
					MenuCheck::Tap(Menu, TEXT("tab:weapons"));
					Enter(EPhase::Weapons);
				}
				return false;
			}
			if (Elapsed() < 1.0) {
				return false;
			}
			bool bEnabled = true;
			Test->TestEqual(TEXT("A tap on WEAPONS opens the store's weapons"), MenuCheck::Line(Menu, TEXT("section")), FString(TEXT("WEAPONS")));
			Test->TestTrue(TEXT("With no shop on this game it says there is none"), MenuCheck::Has(Menu, TEXT("buygun"), &bEnabled));
			Test->TestFalse(TEXT("and no gun can be bought"), bEnabled);
			Test->TestFalse(TEXT("No gun is shown as if it were for sale"), MenuCheck::Has(Menu, TEXT("next")));
			Test->TestTrue(TEXT("No gun is on the stage"), Menu->StageWeapon().IsEmpty());
			Test->TestTrue(TEXT("It says which guns are free"), MenuCheck::Line(Menu, TEXT("free")).Contains(TEXT("FREE")));
			GameCheck::Shot(TEXT("Menu"), TEXT("store_weapons.png"));
			Enter(EPhase::DBucks);
			return false;
		}

		case EPhase::DBucks: {
			if (Menu->Page != ESenniteMenuPage::Swap) {
				// the picture is taken at the end of a frame: nothing is tapped until it is
				if (Elapsed() > 0.3) {
					MenuCheck::Tap(Menu, TEXT("tab:swap"));
					Enter(EPhase::DBucks);
				}
				return false;
			}
			if (Elapsed() < 1.0) {
				return false;
			}
			bool bEnabled = true;
			Test->TestEqual(TEXT("A tap on SWAP opens the store's swap"), MenuCheck::Line(Menu, TEXT("section")), FString(TEXT("SWAP")));
			Test->TestTrue(TEXT("With no shop on this game it says there is none"), MenuCheck::Has(Menu, TEXT("price"), &bEnabled));
			Test->TestFalse(TEXT("and no price can be asked"), bEnabled);
			Test->TestFalse(TEXT("Nothing is offered to swap"), MenuCheck::Has(Menu, TEXT("trade")) || MenuCheck::Has(Menu, TEXT("swap:from")));
			GameCheck::Shot(TEXT("Menu"), TEXT("store_swap.png"));
			Enter(EPhase::HeroesAgain);
			return false;
		}

		case EPhase::HeroesAgain:
			if (Menu->Page != ESenniteMenuPage::Heroes) {
				if (Elapsed() > 0.3) {
					MenuCheck::Tap(Menu, TEXT("tab:heroes"));
					Enter(EPhase::HeroesAgain);
				}
				return false;
			}
			if (Elapsed() < 0.5) {
				return false;
			}
			Test->TestEqual(TEXT("A tap on HEROES goes back to the heroes"), MenuCheck::Line(Menu, TEXT("section")), FString(TEXT("HEROES")));
			// the arrows, to the free hero
			MenuCheck::Tap(Menu, Heroes[(Menu->Browse + 1) % Heroes.Num()].Id == Free ? FName(TEXT("next")) : FName(TEXT("prev")));
			Enter(EPhase::ToWraith);
			return false;

		case EPhase::ToWraith:
			if (Heroes[Menu->Browse].Id != Free) {
				if (Elapsed() > 0.3) {
					MenuCheck::Tap(Menu, TEXT("next"));
					Enter(EPhase::ToWraith);
				}
				return false;
			}
			if (Elapsed() < 1.5) {
				return false;
			}
			Test->TestEqual(TEXT("The arrows bring the free hero up"), Heroes[Menu->Browse].Id, Free);
			Test->TestEqual(TEXT("It stands on the stage as itself"), StageMesh(World), USenniteHeroes::FindOrDefault(Free).Mesh.ToString());
			Test->TestEqual(TEXT("Its name is shown"), MenuCheck::Line(Menu, TEXT("name")), USenniteHeroes::FindOrDefault(Free).Name);
			Test->TestTrue(TEXT("It says it is free"), MenuCheck::Line(Menu, TEXT("status")).EndsWith(TEXT("FREE")));
			YawBefore = StageYaw(World);
			Enter(EPhase::Turning);
			return false;

		case EPhase::Turning: {
			if (Elapsed() < 1.0) {
				return false;
			}
			const double Turned = FMath::Abs(FMath::FindDeltaAngleDegrees(YawBefore, StageYaw(World)));
			Test->TestTrue(TEXT("The hero turns on the turntable"), YawBefore < 999.0 && Turned > 10.0);
			GameCheck::Shot(TEXT("Menu"), TEXT("heroes_free.png"));
			UE_LOG(LogMenuCheck, Display, TEXT("GAMECHECK select: %s on the stage, turned %.0f degrees in a second"), *Free.ToString(), Turned);
			Enter(EPhase::ToLocked);
			return false;
		}

		case EPhase::ToLocked:
			if (Heroes[Menu->Browse].Id != Locked) {
				if (Elapsed() > 0.3) {
					MenuCheck::Tap(Menu, TEXT("next"));
					Enter(EPhase::ToLocked);
				}
				return false;
			}
			if (Elapsed() < 1.5) {
				return false;
			}
			{
				bool bEnabled = true;
				Test->TestFalse(TEXT("A bought hero has no CHOOSE"), MenuCheck::Has(Menu, TEXT("choose")));
				Test->TestTrue(TEXT("It shows how it is had"), MenuCheck::Has(Menu, TEXT("buy"), &bEnabled));
				Test->TestFalse(TEXT("With no shop on this game, it cannot be bought here"), bEnabled);
				Test->TestEqual(TEXT("It stands on the stage as itself"), StageMesh(World), USenniteHeroes::FindOrDefault(Locked).Mesh.ToString());
				UE_LOG(LogMenuCheck, Display, TEXT("GAMECHECK locked: %s status=\"%s\""), *Locked.ToString(), *MenuCheck::Line(Menu, TEXT("status")));
				GameCheck::Shot(TEXT("Menu"), TEXT("heroes_bought.png"));
				// a tap where its button is changes nothing
				MenuCheck::Tap(Menu, TEXT("buy"));
			}
			Enter(EPhase::Locked);
			return false;

		case EPhase::Locked:
			if (Elapsed() < 0.5) {
				return false;
			}
			Test->TestEqual(TEXT("Tapping a bought hero's button does not make it the player's"), USenniteProfile::Get().Hero == Locked, false);
			Test->TestTrue(TEXT("The character select stays open"), Menu->Page == ESenniteMenuPage::Heroes);
			// every hero has a tile along the bottom: a tap on the free hero's brings it up at once
			Test->TestTrue(TEXT("The free hero has a tile"), MenuCheck::Tap(Menu, FName(*FString::Printf(TEXT("hero:%s"), *Free.ToString()))));
			Enter(EPhase::BackToWraith);
			return false;

		case EPhase::BackToWraith:
			if (Elapsed() < 0.5 || (Heroes[Menu->Browse].Id != Free && Elapsed() < 3.0)) {
				return false;
			}
			Test->TestEqual(TEXT("A tap on a hero's tile shows that hero"), Heroes[Menu->Browse].Id, Free);
			Test->TestEqual(TEXT("It stands on the stage as itself"), StageMesh(World), USenniteHeroes::FindOrDefault(Free).Mesh.ToString());
			Test->TestTrue(TEXT("The free hero can be chosen"), MenuCheck::Tap(Menu, TEXT("choose")));
			Enter(EPhase::Chosen);
			return false;

		case EPhase::Chosen: {
			if (Elapsed() < 1.0) {
				return false;
			}
			FString Slot;
			FParse::Value(FCommandLine::Get(), TEXT("ProfileSlot="), Slot);
			const USenniteProfile* OnDisk = Cast<USenniteProfile>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
			Test->TestFalse(TEXT("The check runs with a profile of its own"), Slot.IsEmpty());
			Test->TestTrue(TEXT("CHOOSE goes back to the menu"), Menu->Page == ESenniteMenuPage::Main);
			Test->TestEqual(TEXT("The hero is the player's"), USenniteProfile::Get().Hero, Free);
			Test->TestTrue(TEXT("The choice is kept on disk for the next run"), OnDisk != nullptr && OnDisk->Hero == Free);
			Test->TestEqual(TEXT("The menu names the new hero"), MenuCheck::Line(Menu, TEXT("hero")), USenniteHeroes::FindOrDefault(Free).Name);
			GameCheck::Shot(TEXT("Menu"), TEXT("menu_chosen.png"));
			MenuCheck::Tap(Menu, TEXT("play"));
			Enter(EPhase::Play);
			return false;
		}

		case EPhase::Play: {
			APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
			ASenniteCharacter* Hero = Controller != nullptr ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
			if (Hero == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround() || Elapsed() < 3.0) {
				return Elapsed() > 90.0 ? Fail(TEXT("PLAY did not bring the player to the land with a hero in 90 s")) : false;
			}
			const FSenniteHero& Want = USenniteHeroes::FindOrDefault(Free);
			Test->TestEqual(TEXT("PLAY opens the land the game is played on, not the bare test range"), UGameplayStatics::GetCurrentLevelName(World), FString(TEXT("Arena")));
			Test->TestEqual(TEXT("The player's state is the hero chosen"), Controller->GetPlayerState<ASennitePlayerState>()->HeroId, Free);
			Test->TestEqual(TEXT("The character is the hero chosen"), Hero->HeroId, Free);
			Test->TestEqual(TEXT("It wears the hero's mesh"), FSoftObjectPath(Hero->GetMesh()->GetSkeletalMeshAsset()).ToString(), Want.Mesh.ToString());
			Test->TestEqual(TEXT("It runs the hero's own animation Blueprint"), FSoftClassPath(Hero->GetMesh()->GetAnimInstance()->GetClass()).ToString(), Want.AnimClass.ToString());
			GameCheck::Shot(TEXT("Menu"), TEXT("play.png"));
			UE_LOG(LogMenuCheck, Display, TEXT("GAMECHECK play: %s on %s, looking %s from %s"), *Hero->HeroId.ToString(), *UGameplayStatics::GetCurrentLevelName(World),
				*Controller->GetControlRotation().ToCompactString(), *Hero->GetActorLocation().ToCompactString());
			UE_LOG(LogMenuCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;
		}

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteMenuTest, "Sennite.Menu.ChooseAndPlay",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteMenuTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMenuRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
