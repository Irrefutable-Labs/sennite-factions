// Lobby check: two copies of the game on this machine, each starting on the menu with a profile of its
// own, everything done by taps on the screen (Tools/check_lobby.sh).
//
// Host: taps HOST; the lobby opens with them in it, as their hero (the default). Once the other player
// is in and has chosen another hero, both heroes stand on the stage and the list names both; the host
// taps START, and both travel to the test range together, each as their own hero.
//
// Joiner: taps JOIN, types the host's address on the screen's keypad, taps JOIN; in the lobby, taps
// HEROES, goes to the second free hero and taps CHOOSE; the host sees it. Before that it sends what a
// game changed to lie would: a claim to a hero bought in the shop, with a made-up pass; the server keeps it
// as the hero it is. On the test range they are
// that hero, and the host's hero is the host's.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MenuCheck.h"
#include "SenniteCharacter.h"
#include "SenniteHeroes.h"
#include "SennitePlayerState.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogLobbyCheck, Log, All);

namespace
{
	/** The second free hero of the list: the one the joining player chooses. */
	FName SecondFree()
	{
		const FName Default = USenniteHeroes::FindOrDefault(NAME_None).Id;
		for (const FSenniteHero& Hero : USenniteHeroes::All()) {
			if (Hero.bFree && Hero.Id != Default) {
				return Hero.Id;
			}
		}
		return NAME_None;
	}

	/** On the test range: every hero there, with the hero each is. Empty until both are standing. */
	TArray<ASenniteCharacter*> HeroesOnRange(UWorld* World)
	{
		TArray<ASenniteCharacter*> Found;
		if (World == nullptr || UGameplayStatics::GetCurrentLevelName(World) != TEXT("TestRange")) {
			return Found;
		}
		for (TActorIterator<ASenniteCharacter> It(World); It; ++It) {
			Found.Add(*It);
		}
		return Found;
	}

	class FLobbyRun : public IAutomationLatentCommand
	{
	public:
		FLobbyRun(FAutomationTestBase* InTest, bool bInHost) : Test(InTest), bHost(bInHost), RealStart(FPlatformTime::Seconds()) {}

		virtual bool Update() override;

	private:
		enum class EPhase { WaitForMenu, Opening, Typing, Joining, Lie, InLobby, ChooseHero, Chosen, WaitStart, Travel, Done };

		void Enter(EPhase Next) { Phase = Next; PhaseStart = FPlatformTime::Seconds(); }
		double Elapsed() const { return FPlatformTime::Seconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		const TCHAR* Side() const { return bHost ? TEXT("host") : TEXT("joiner"); }

		FAutomationTestBase* Test;
		bool bHost;
		double RealStart;
		EPhase Phase = EPhase::WaitForMenu;
		double PhaseStart = 0.0;
		FString Address;
		int32 Typed = 0;
		FName Lied;
	};

	bool FLobbyRun::Update()
	{
		if (FPlatformTime::Seconds() - RealStart > 300.0) {
			return Fail(FString::Printf(TEXT("The %s's half did not finish in 300 s (stuck at step %d)"), Side(), int32(Phase)));
		}
		UWorld* World = GameCheck::FindGameWorld();
		ASenniteMenuController* Menu = MenuCheck::Menu(World);
		const FName Default = USenniteHeroes::FindOrDefault(NAME_None).Id;
		const FName Chosen = SecondFree();

		switch (Phase) {
		case EPhase::WaitForMenu:
			if (Menu == nullptr || !MenuCheck::Has(Menu, TEXT("host")) || World->GetTimeSeconds() < 2.0) {
				return false;
			}
			Test->TestTrue(TEXT("The game opens on the menu"), Menu->Page == ESenniteMenuPage::Main && !Menu->InLobby());
			if (bHost) {
				MenuCheck::Tap(Menu, TEXT("host"));
				Enter(EPhase::Opening);
			}
			else {
				FParse::Value(FCommandLine::Get(), TEXT("LobbyAddress="), Address);
				if (Address.IsEmpty()) {
					return Fail(TEXT("No -LobbyAddress= for the joining player to type"));
				}
				MenuCheck::Tap(Menu, TEXT("join"));
				Enter(EPhase::Typing);
			}
			return false;

		case EPhase::Opening:
			// the host's own game reopens the Menu map as a listen server, in the lobby
			if (Menu == nullptr || !Menu->InLobby() || Menu->Page != ESenniteMenuPage::Lobby || !MenuCheck::Has(Menu, TEXT("start"))) {
				return Elapsed() > 60.0 ? Fail(TEXT("HOST did not open a lobby in 60 s")) : false;
			}
			Test->TestEqual(TEXT("The lobby is hosted for others to join"), int32(World->GetNetMode()), int32(NM_ListenServer));
			Test->TestEqual(TEXT("The host is in it"), Menu->LobbyPlayers().Num(), 1);
			Test->TestTrue(TEXT("The list names the host's hero"), MenuCheck::Line(Menu, TEXT("player0")).Contains(USenniteHeroes::FindOrDefault(Default).Name));
			UE_LOG(LogLobbyCheck, Display, TEXT("GAMECHECK lobby open: %s"), *MenuCheck::Line(Menu, TEXT("addresses")));
			Enter(EPhase::InLobby);
			return false;

		case EPhase::Typing: {
			if (Menu == nullptr || Menu->Page != ESenniteMenuPage::Join) {
				return Elapsed() > 5.0 ? Fail(TEXT("JOIN did not open the address page")) : false;
			}
			if (Typed == 0 && !Menu->Address.IsEmpty()) {
				// a new profile has no last address
				return Fail(TEXT("The address page did not start empty"));
			}
			if (Typed < Address.Len()) {
				if (Elapsed() > 0.15) {
					const FString Key = FString::Printf(TEXT("key:%c"), Address[Typed]);
					if (!MenuCheck::Tap(Menu, FName(*Key))) {
						return Fail(FString::Printf(TEXT("The keypad has no key for '%c'"), Address[Typed]));
					}
					++Typed;
					Enter(EPhase::Typing);
				}
				return false;
			}
			if (Elapsed() < 0.3) {
				return false;
			}
			Test->TestEqual(TEXT("The keypad typed the address"), Menu->Address, Address);
			Test->TestEqual(TEXT("The page shows it"), MenuCheck::Line(Menu, TEXT("address")), Address);
			GameCheck::Shot(TEXT("Lobby"), TEXT("joiner_address.png"));
			MenuCheck::Tap(Menu, TEXT("connect"));
			Enter(EPhase::Joining);
			return false;
		}

		case EPhase::Joining:
			if (Menu == nullptr || World->GetNetMode() != NM_Client || !Menu->InLobby() || Menu->Page != ESenniteMenuPage::Lobby || Menu->LobbyPlayers().Num() < 2) {
				return Elapsed() > 60.0 ? Fail(TEXT("JOIN did not bring the player into the host's lobby in 60 s")) : false;
			}
			Test->TestFalse(TEXT("A joining player has no START"), MenuCheck::Has(Menu, TEXT("start")));
			Test->TestTrue(TEXT("The joining player's address is kept for next time"), USenniteProfile::Get().LastAddress == Address);
			// what a game changed to lie would send: a hero bought in the shop that this wallet never bought,
			// with a pass of the right form that no publisher's server gave. The server must not take it.
			for (const FSenniteHero& Hero : USenniteHeroes::All()) {
				if (!Hero.bFree) {
					Lied = Hero.Id;
					break;
				}
			}
			Menu->GetPlayerState<ASennitePlayerState>()->ServerChooseHero(Lied, TEXT("0123456789abcdef0123456789abcdef"));
			Enter(EPhase::Lie);
			return false;

		case EPhase::Lie: {
			if (Elapsed() < 2.0) {
				return false;
			}
			const ASennitePlayerState* Me = Menu != nullptr ? Menu->GetPlayerState<ASennitePlayerState>() : nullptr;
			Test->TestTrue(TEXT("The server does not make a player a hero bought in the shop on their game's word"), Me != nullptr && Me->HeroId == Default);
			Test->TestFalse(TEXT("Nor does anyone's stage show it"), Menu->StageHeroes().Contains(Lied));
			UE_LOG(LogLobbyCheck, Display, TEXT("GAMECHECK joiner claimed %s with no real pass: still %s"), *Lied.ToString(), Me ? *Me->HeroId.ToString() : TEXT("?"));
			MenuCheck::Tap(Menu, TEXT("heroes"));
			Enter(EPhase::ChooseHero);
			return false;
		}

		case EPhase::ChooseHero:
			if (Menu == nullptr || Menu->Page != ESenniteMenuPage::Heroes) {
				return Elapsed() > 5.0 ? Fail(TEXT("HEROES did not open the character select in the lobby")) : false;
			}
			if (USenniteHeroes::All()[Menu->Browse].Id != Chosen) {
				if (Elapsed() > 0.3) {
					MenuCheck::Tap(Menu, TEXT("next"));
					Enter(EPhase::ChooseHero);
				}
				return false;
			}
			if (Elapsed() < 0.5) {
				return false;
			}
			MenuCheck::Tap(Menu, TEXT("choose"));
			Enter(EPhase::Chosen);
			return false;

		case EPhase::Chosen: {
			const ASennitePlayerState* Me = Menu != nullptr ? Menu->GetPlayerState<ASennitePlayerState>() : nullptr;
			const TArray<FName> Stage = Menu != nullptr ? Menu->StageHeroes() : TArray<FName>();
			if (Me == nullptr || Me->HeroId != Chosen || Menu->Page != ESenniteMenuPage::Lobby || !Stage.Contains(Chosen)) {
				return Elapsed() > 20.0 ? Fail(TEXT("The hero chosen in the lobby did not come back from the server in 20 s")) : false;
			}
			Test->TestEqual(TEXT("The choice is the joining player's own"), USenniteProfile::Get().Hero, Chosen);
			Test->TestTrue(TEXT("The host's hero stands on the stage beside it"), Stage.Num() == 2 && Stage.Contains(Default));
			GameCheck::Shot(TEXT("Lobby"), TEXT("joiner_lobby.png"));
			UE_LOG(LogLobbyCheck, Display, TEXT("GAMECHECK joiner chose %s in the lobby; stage=[%s]"), *Chosen.ToString(), *FString::JoinBy(Stage, TEXT(","), [](FName N) { return N.ToString(); }));
			Enter(EPhase::Travel);
			return false;
		}

		case EPhase::InLobby: {
			// the host waits for the other player and their choice
			const TArray<const ASennitePlayerState*> Players = Menu != nullptr ? Menu->LobbyPlayers() : TArray<const ASennitePlayerState*>();
			const TArray<FName> Stage = Menu != nullptr ? Menu->StageHeroes() : TArray<FName>();
			if (Players.Num() < 2 || Players[1]->HeroId != Chosen || !Stage.Contains(Chosen) || Elapsed() < 1.0) {
				return Elapsed() > 180.0 ? Fail(TEXT("No second player with their chosen hero came into the lobby in 180 s")) : false;
			}
			Test->TestEqual(TEXT("Both players' heroes stand on the stage"), Stage.Num(), 2);
			Test->TestTrue(TEXT("The list names the other player's hero"), MenuCheck::Line(Menu, TEXT("player1")).Contains(USenniteHeroes::FindOrDefault(Chosen).Name));
			Test->TestEqual(TEXT("The list says two are in"), MenuCheck::Line(Menu, TEXT("count")), FString(TEXT("2 IN THE LOBBY")));
			GameCheck::Shot(TEXT("Lobby"), TEXT("host_lobby.png"));
			UE_LOG(LogLobbyCheck, Display, TEXT("GAMECHECK host sees: %s | %s"), *MenuCheck::Line(Menu, TEXT("player0")), *MenuCheck::Line(Menu, TEXT("player1")));
			Enter(EPhase::WaitStart);
			return false;
		}

		case EPhase::WaitStart:
			// as a host would: a moment to see who is in before starting
			if (Elapsed() < 3.0) {
				return false;
			}
			Test->TestTrue(TEXT("The host taps START"), MenuCheck::Tap(Menu, TEXT("start")));
			Enter(EPhase::Travel);
			return false;

		case EPhase::Travel: {
			const TArray<ASenniteCharacter*> Heroes = HeroesOnRange(World);
			APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
			ASenniteCharacter* Mine = Controller != nullptr ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
			if (Heroes.Num() < 2 || Mine == nullptr || !Mine->GetCharacterMovement()->IsMovingOnGround() || Elapsed() < 2.0) {
				return Elapsed() > 120.0 ? Fail(FString::Printf(TEXT("The %s did not reach the test range with both heroes in 120 s"), Side())) : false;
			}
			const FName Own = bHost ? Default : Chosen;
			const FName Other = bHost ? Chosen : Default;
			ASenniteCharacter* Theirs = Heroes[0] == Mine ? Heroes[1] : Heroes[0];
			Test->TestEqual(TEXT("Everyone travelled together to the test range"), UGameplayStatics::GetCurrentLevelName(World), FString(TEXT("TestRange")));
			Test->TestEqual(TEXT("This player is their own hero there"), Mine->HeroId, Own);
			Test->TestEqual(TEXT("And wears its mesh"), FSoftObjectPath(Mine->GetMesh()->GetSkeletalMeshAsset()).ToString(), USenniteHeroes::FindOrDefault(Own).Mesh.ToString());
			Test->TestEqual(TEXT("The other player is theirs"), Theirs->HeroId, Other);
			Test->TestEqual(TEXT("And this player's game draws it so"), FSoftObjectPath(Theirs->GetMesh()->GetSkeletalMeshAsset()).ToString(), USenniteHeroes::FindOrDefault(Other).Mesh.ToString());
			GameCheck::Shot(TEXT("Lobby"), *FString::Printf(TEXT("%s_range.png"), Side()));
			UE_LOG(LogLobbyCheck, Display, TEXT("GAMECHECK %s on the range: me=%s other=%s"), Side(), *Mine->HeroId.ToString(), *Theirs->HeroId.ToString());
			UE_LOG(LogLobbyCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;
		}

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteLobbyHost, "Sennite.Lobby.Host", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteLobbyJoiner, "Sennite.Lobby.Joiner", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteLobbyHost::RunTest(const FString& Parameters)
{
	// The Paragon packs' own character Blueprints call a function this engine no longer has; the editor's
	// game logs it when such a hero first loads. The game never uses those Blueprints.
	AddExpectedErrorPlain(TEXT("ResetOrientationAndPosition"), EAutomationExpectedErrorFlags::Contains, 0);
	ADD_LATENT_AUTOMATION_COMMAND(FLobbyRun(this, true));
	return true;
}

bool FSenniteLobbyJoiner::RunTest(const FString& Parameters)
{
	AddExpectedErrorPlain(TEXT("ResetOrientationAndPosition"), EAutomationExpectedErrorFlags::Contains, 0);
	ADD_LATENT_AUTOMATION_COMMAND(FLobbyRun(this, false));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
