// Hero shop check. Run in the game on the menu against a publisher's server on DSM, by taps on the
// screen, as a player with a new wallet meets it. In HEROES a tap on a bought hero's tile shows it; with
// no wallet connected its button is CONNECT YOUR WALLET, and a tap hands the wallet the connect code.
// Connected, the wallet holds ERA and no D-bucks: the button is GET D-BUCKS, and a tap trades ERA for
// them. With enough, the button is BUY and the wallet pays; once the wallet proves it owns the hero,
// CHOOSE makes it the player's. The player then hosts a lobby: they
// come in as the default hero (a bought hero never travels in the login address), their game asks
// the publisher's server for a hero pass made out to the lobby's server, and that server, asking the
// publisher's server with it, makes them the hero they bought. START takes them to the test range
// as that hero.
//
// The wallet is a real one, driven from outside the game. This check writes the connect code the
// game hands over to Saved/HeroShop/connect-code.txt, as a player's wallet would be given it.
//
// Server/test/game/heroes.test.ts runs this with the accounts it needs and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MenuCheck.h"
#include "SenniteCharacter.h"
#include "SenniteHeroes.h"
#include "SennitePlayerState.h"
#include "SennitePublisher.h"
#include "SenniteUi.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeroShopCheck, Log, All);

namespace
{
	class FHeroShopRun : public IAutomationLatentCommand
	{
	public:
		explicit FHeroShopRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}

		virtual bool Update() override;

	private:
		enum class EPhase { WaitForMenu, ToLocked, TapWallet, Connect, GetDBucks, ToHero, Buy, Bought, Chosen, Lobby, Claimed, Range, Done };

		void Enter(EPhase Next) { Phase = Next; PhaseStart = FPlatformTime::Seconds(); }
		double Elapsed() const { return FPlatformTime::Seconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForMenu;
		double PhaseStart = 0.0;
		FName Bought;
		int32 Price = 0;
		int32 DBucksBefore = 0;
		bool bCodeWritten = false;
		int32 Trades = 0;
	};

	bool FHeroShopRun::Update()
	{
		if (FPlatformTime::Seconds() - RealStart > 900.0) {
			return Fail(FString::Printf(TEXT("The hero shop check did not finish in 900 s (stuck at step %d)"), int32(Phase)));
		}
		UWorld* World = GameCheck::FindGameWorld();
		ASenniteMenuController* Menu = MenuCheck::Menu(World);
		USennitePublisher* Server = World != nullptr && World->GetGameInstance() != nullptr ? World->GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
		if (Server == nullptr || !Server->IsOn()) {
			return Fail(TEXT("No publisher's server is set: run the game with -PublisherUrl= (Server/test/game/heroes.test.ts does)"));
		}
		const TArray<FSenniteHero>& Heroes = USenniteHeroes::All();

		switch (Phase) {
		case EPhase::WaitForMenu:
			if (Menu == nullptr || !MenuCheck::Has(Menu, TEXT("heroes")) || World->GetTimeSeconds() < 2.0) {
				return false;
			}
			// the first hero the shop sells
			for (const FSenniteHero& Hero : Heroes) {
				if (!Hero.bFree) {
					Bought = Hero.Id;
					break;
				}
			}
			Test->TestFalse(TEXT("A new profile cannot play the bought hero"), USenniteHeroes::MayPlay(Menu, Bought));
			MenuCheck::Tap(Menu, TEXT("heroes"));
			Enter(EPhase::ToLocked);
			return false;

		case EPhase::ToLocked: {
			if (Menu == nullptr || Menu->Page != ESenniteMenuPage::Heroes) {
				return Elapsed() > 10.0 ? Fail(TEXT("HEROES did not open the character select")) : false;
			}
			// a tap on the bought hero's own tile
			if (Heroes[Menu->Browse].Id != Bought) {
				if (Elapsed() > 0.5) {
					Test->TestTrue(TEXT("The bought hero has a tile"), MenuCheck::Tap(Menu, FName(*FString::Printf(TEXT("hero:%s"), *Bought.ToString()))));
					Enter(EPhase::ToLocked);
				}
				return false;
			}
			// with no wallet connected, the hero's button is the way to connect one, once the game has a code for it
			bool bEnabled = false;
			if (!(MenuCheck::Has(Menu, TEXT("wallet"), &bEnabled) && bEnabled)) {
				return Elapsed() > 120.0 ? Fail(TEXT("The bought hero did not offer CONNECT YOUR WALLET in 120 s")) : false;
			}
			Test->TestFalse(TEXT("It has no CHOOSE before it is bought"), MenuCheck::Has(Menu, TEXT("choose")));
			Test->TestEqual(TEXT("The screen says no wallet is connected"), MenuCheck::Line(Menu, TEXT("balance")), FString(TEXT("NO WALLET CONNECTED")));
			Test->TestFalse(TEXT("It says what the button is for"), MenuCheck::Line(Menu, TEXT("hint")).IsEmpty());
			GameCheck::Shot(TEXT("HeroShop"), TEXT("connect.png"));
			Enter(EPhase::TapWallet);
			return false;
		}

		case EPhase::TapWallet:
			// the picture is taken at the end of a frame: nothing is tapped until it is
			if (Elapsed() < 0.3) {
				return false;
			}
			Test->TestTrue(TEXT("CONNECT YOUR WALLET can be tapped"), MenuCheck::Tap(Menu, TEXT("wallet")));
			Enter(EPhase::Connect);
			return false;

		case EPhase::Connect:
			// the tap handed the connect code over, as it does to a wallet: here the wallet outside the game is given it
			if (!bCodeWritten) {
				if (Elapsed() < 0.5) {
					return false;
				}
				FString Handed;
				FPlatformApplicationMisc::ClipboardPaste(Handed);
				Test->TestEqual(TEXT("A tap on CONNECT YOUR WALLET hands over the game's connect code"), Handed, Server->ConnectCode);
				bCodeWritten = FFileHelper::SaveStringToFile(Handed, *(FPaths::ProjectSavedDir() / TEXT("HeroShop") / TEXT("connect-code.txt")));
			}
			if (!(Server->bConnected && Server->bMaterialsKnown && Server->ShopHeroes.Num() > 0) || Server->bBusy || Server->ProofPending()) {
				return false;
			}
			UE_LOG(LogHeroShopCheck, Display, TEXT("GAMECHECK connected: the wallet proves %d D-bucks; the shop sells %d heroes"), Server->DBucks, Server->ShopHeroes.Num());
			Enter(EPhase::GetDBucks);
			return false;

		case EPhase::GetDBucks: {
			if (Menu == nullptr || Menu->Page != ESenniteMenuPage::Heroes || Heroes[Menu->Browse].Id != Bought) {
				return Fail(TEXT("The character select left the bought hero"));
			}
			const FSenniteHeroForSale* ForSale = Server->HeroForSale(Bought.ToString());
			if (ForSale == nullptr) {
				return Fail(FString::Printf(TEXT("The shop does not sell %s"), *Bought.ToString()));
			}
			if (Server->bBusy || Server->ProofPending()) {
				return Elapsed() > 600.0 ? Fail(FString::Printf(TEXT("The trade for D-bucks did not show in 600 s: %s"), *Server->Message)) : false;
			}
			if (Server->DBucks >= ForSale->Price) {
				Test->TestTrue(TEXT("The D-bucks came from a trade on the character select"), Trades > 0);
				UE_LOG(LogHeroShopCheck, Display, TEXT("GAMECHECK the character select traded ERA for D-bucks %d time(s): the wallet proves %d"), Trades, Server->DBucks);
				Enter(EPhase::ToHero);
				return false;
			}
			// too few D-bucks: the hero's button trades ERA for them. A trade's D-bucks show with the wallet's next proof.
			if (Elapsed() < (Trades == 0 ? 1.0 : 20.0)) {
				return false;
			}
			if (Trades >= 3) {
				return Fail(FString::Printf(TEXT("Three trades did not bring the hero's price: the wallet proves %d D-bucks"), Server->DBucks));
			}
			bool bEnabled = false;
			Test->TestTrue(TEXT("With too few D-bucks the hero's button is GET D-BUCKS"), MenuCheck::Has(Menu, TEXT("dbucks"), &bEnabled) && bEnabled);
			Test->TestFalse(TEXT("BUY is not offered yet"), MenuCheck::Has(Menu, TEXT("buy")));
			if (Trades == 0) {
				Test->TestEqual(TEXT("A new wallet's balance is shown as it is"), MenuCheck::Line(Menu, TEXT("balance")), FString(TEXT("0 D-BUCKS")));
				GameCheck::Shot(TEXT("HeroShop"), TEXT("get_dbucks.png"));
			}
			++Trades;
			MenuCheck::Tap(Menu, TEXT("dbucks"));
			Enter(EPhase::GetDBucks);
			return false;
		}

		case EPhase::ToHero:
			if (Menu == nullptr || Menu->Page != ESenniteMenuPage::Heroes) {
				return Fail(TEXT("The character select closed"));
			}
			if (Heroes[Menu->Browse].Id != Bought) {
				if (Elapsed() > 0.3) {
					MenuCheck::Tap(Menu, TEXT("next"));
					Enter(EPhase::ToHero);
				}
				return false;
			}
			if (Elapsed() < 1.0) {
				return false;
			}
			{
				const FSenniteHeroForSale* ForSale = Server->HeroForSale(Bought.ToString());
				if (ForSale == nullptr) {
					return Fail(FString::Printf(TEXT("The shop does not sell %s"), *Bought.ToString()));
				}
				Price = ForSale->Price;
				DBucksBefore = Server->DBucks;
				bool bEnabled = false;
				Test->TestTrue(TEXT("The bought hero shows its price"), MenuCheck::Line(Menu, TEXT("status")) == FString::Printf(TEXT("%d D-BUCKS"), Price));
				Test->TestFalse(TEXT("With enough D-bucks GET D-BUCKS is gone"), MenuCheck::Has(Menu, TEXT("dbucks")));
				Test->TestFalse(TEXT("It has no CHOOSE before it is bought"), MenuCheck::Has(Menu, TEXT("choose")));
				Test->TestTrue(TEXT("BUY is there and can be tapped"), MenuCheck::Has(Menu, TEXT("buy"), &bEnabled) && bEnabled);
				Test->TestTrue(TEXT("The wallet holds enough D-bucks"), DBucksBefore >= Price);
				Test->TestEqual(TEXT("The screen shows the D-bucks the wallet proved it holds"), MenuCheck::Line(Menu, TEXT("balance")), FString::Printf(TEXT("%s D-BUCKS"), *SenniteUi::Grouped(DBucksBefore)));
				GameCheck::Shot(TEXT("HeroShop"), TEXT("for_sale.png"));
				MenuCheck::Tap(Menu, TEXT("buy"));
			}
			Enter(EPhase::Buy);
			return false;

		case EPhase::Buy:
			// paid, issued, given, and proved owned by the wallet's next proof
			if (!Server->OwnsHero(Bought.ToString()) || Server->bBusy || Server->ProofPending()) {
				return Elapsed() > 600.0 ? Fail(FString::Printf(TEXT("The wallet did not prove it owns %s in 600 s: %s"), *Bought.ToString(), *Server->Message)) : false;
			}
			Test->TestEqual(TEXT("The wallet paid the hero's price"), DBucksBefore - Server->DBucks, Price);
			UE_LOG(LogHeroShopCheck, Display, TEXT("GAMECHECK bought %s for %d D-bucks: %s"), *Bought.ToString(), Price, *Server->Message);
			Enter(EPhase::Bought);
			return false;

		case EPhase::Bought: {
			if (Elapsed() < 0.5) {
				return false;
			}
			bool bEnabled = false;
			Test->TestTrue(TEXT("The bought hero says it is the player's"), MenuCheck::Line(Menu, TEXT("status")) == TEXT("YOURS"));
			Test->TestEqual(TEXT("The screen shows what the wallet holds after paying"), MenuCheck::Line(Menu, TEXT("balance")), FString::Printf(TEXT("%s D-BUCKS"), *SenniteUi::Grouped(DBucksBefore - Price)));
			Test->TestTrue(TEXT("It can be chosen now"), MenuCheck::Has(Menu, TEXT("choose"), &bEnabled) && bEnabled);
			GameCheck::Shot(TEXT("HeroShop"), TEXT("bought.png"));
			MenuCheck::Tap(Menu, TEXT("choose"));
			Enter(EPhase::Chosen);
			return false;
		}

		case EPhase::Chosen:
			if (Menu == nullptr || Menu->Page != ESenniteMenuPage::Main) {
				return Elapsed() > 5.0 ? Fail(TEXT("CHOOSE did not go back to the menu")) : false;
			}
			Test->TestEqual(TEXT("The bought hero is the player's choice"), USenniteProfile::Get().Hero, Bought);
			MenuCheck::Tap(Menu, TEXT("host"));
			Enter(EPhase::Lobby);
			return false;

		case EPhase::Lobby: {
			const ASennitePlayerState* Me = Menu != nullptr ? Menu->GetPlayerState<ASennitePlayerState>() : nullptr;
			if (Menu == nullptr || !Menu->InLobby() || Me == nullptr || Me->HeroId.IsNone()) {
				return Elapsed() > 60.0 ? Fail(TEXT("HOST did not open a lobby in 60 s")) : false;
			}
			// a bought hero never travels in the login address: the player comes in as the default hero
			if (Me->HeroId != Bought) {
				Test->TestEqual(TEXT("The player comes into the lobby as the default hero"), Me->HeroId, USenniteHeroes::FindOrDefault(NAME_None).Id);
			}
			Enter(EPhase::Claimed);
			return false;
		}

		case EPhase::Claimed: {
			const ASennitePlayerState* Me = Menu != nullptr ? Menu->GetPlayerState<ASennitePlayerState>() : nullptr;
			if (Me == nullptr || Me->HeroId != Bought || !Menu->StageHeroes().Contains(Bought)) {
				return Elapsed() > 120.0 ? Fail(FString::Printf(TEXT("The lobby's server did not make the player %s in 120 s (still %s)"), *Bought.ToString(), Me ? *Me->HeroId.ToString() : TEXT("?"))) : false;
			}
			Test->TestFalse(TEXT("The pass was asked for this server by its audience"), Me->HeroAudience.IsEmpty());
			Test->TestFalse(TEXT("The server bound the player to the wallet the pass named"), Me->HeroWallet.IsEmpty());
			Test->TestTrue(TEXT("The list names the bought hero"), MenuCheck::Line(Menu, TEXT("player0")).Contains(USenniteHeroes::FindOrDefault(Bought).Name));
			GameCheck::Shot(TEXT("HeroShop"), TEXT("lobby.png"));
			UE_LOG(LogHeroShopCheck, Display, TEXT("GAMECHECK the lobby's server checked the pass: the player is %s"), *Me->HeroId.ToString());
			MenuCheck::Tap(Menu, TEXT("start"));
			Enter(EPhase::Range);
			return false;
		}

		case EPhase::Range: {
			APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
			ASenniteCharacter* Hero = Controller != nullptr ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
			if (Hero == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround() || Elapsed() < 2.0) {
				return Elapsed() > 120.0 ? Fail(TEXT("START did not bring the player to the test range with a hero in 120 s")) : false;
			}
			Test->TestEqual(TEXT("START opens the test range"), UGameplayStatics::GetCurrentLevelName(World), FString(TEXT("TestRange")));
			Test->TestEqual(TEXT("The player is the bought hero there"), Hero->HeroId, Bought);
			Test->TestEqual(TEXT("It wears the hero's mesh"), FSoftObjectPath(Hero->GetMesh()->GetSkeletalMeshAsset()).ToString(), USenniteHeroes::FindOrDefault(Bought).Mesh.ToString());
			GameCheck::Shot(TEXT("HeroShop"), TEXT("range.png"));
			UE_LOG(LogHeroShopCheck, Display, TEXT("GAMECHECK on the range as %s"), *Hero->HeroId.ToString());
			UE_LOG(LogHeroShopCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;
		}

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteHeroShopTest, "Sennite.HeroShop.BuyChooseAndPlay",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteHeroShopTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHeroShopRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
