// Weapon shop check. Run in the game on the Menu map with a publisher's server (-PublisherUrl=) and a
// wallet outside the game that approves the game's connect code (Server/test/game/weapons.test.ts is
// that wallet), by taps on the screen as a phone gives them:
// - STORE, WEAPONS: the first gun the shop sells, not owned; CONNECT YOUR WALLET hands the code over;
// - a new wallet holds too few D-bucks: GET D-BUCKS trades ERA for them;
// - BUY: the wallet pays, the gun is issued and given, and the wallet proves it owns it: the store
//   marks it bought, and the other gun stays for sale;
// - a hero pass made out to this game speaks for the gun, in the list a game server is given.
//
// Server/test/game/weapons.test.ts runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MenuCheck.h"
#include "SennitePublisher.h"
#include "SenniteUi.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeaponShopCheck, Log, All);

namespace
{
	class FWeaponShopRun : public IAutomationLatentCommand
	{
	public:
		explicit FWeaponShopRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}

		virtual bool Update() override;

	private:
		enum class EPhase { WaitForMenu, Open, Connect, GetDBucks, TapBuy, Bought, Pass, Checked, Other, Done };

		void Enter(EPhase Next) { Phase = Next; PhaseStart = FPlatformTime::Seconds(); }
		double Elapsed() const { return FPlatformTime::Seconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForMenu;
		double PhaseStart = 0.0;
		bool bAsked = false;
		bool bCodeWritten = false;
		int32 Trades = 0;
		FString Gun;
		int32 Price = 0;
		int32 DBucksBefore = 0;
		/** What the hero pass answered: set by the publisher's server's answers. */
		TSharedRef<TArray<FString>> PassSpeaksFor = MakeShared<TArray<FString>>();
		TSharedRef<int32> PassState = MakeShared<int32>(0);
	};

	bool FWeaponShopRun::Update()
	{
		if (FPlatformTime::Seconds() - RealStart > 1500.0) {
			return Fail(FString::Printf(TEXT("The weapon shop check did not finish in 1500 s (stuck at step %d)"), int32(Phase)));
		}
		UWorld* World = GameCheck::FindGameWorld();
		ASenniteMenuController* Menu = MenuCheck::Menu(World);
		USennitePublisher* Server = World != nullptr && World->GetGameInstance() != nullptr ? World->GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
		if (Server == nullptr || !Server->IsOn()) {
			return Fail(TEXT("No publisher's server is set: run the game with -PublisherUrl= (Server/test/game/weapons.test.ts does)"));
		}
		if (Phase != EPhase::WaitForMenu && Phase != EPhase::Open && (Menu == nullptr || Menu->Page != ESenniteMenuPage::Weapons)) {
			return Fail(TEXT("The store's weapons closed"));
		}

		switch (Phase) {
		case EPhase::WaitForMenu:
			if (Menu == nullptr || !MenuCheck::Has(Menu, TEXT("store")) || World->GetTimeSeconds() < 2.0) {
				return false;
			}
			MenuCheck::Tap(Menu, TEXT("store"));
			Enter(EPhase::Open);
			return false;

		case EPhase::Open:
			if (Menu == nullptr || Menu->Page != ESenniteMenuPage::Weapons) {
				if (Elapsed() > 1.0) {
					MenuCheck::Tap(Menu, TEXT("tab:weapons"));
					Enter(EPhase::Open);
				}
				return false;
			}
			Enter(EPhase::Connect);
			return false;

		case EPhase::Connect: {
			if (!bCodeWritten) {
				bool bEnabled = false;
				if (!(MenuCheck::Has(Menu, TEXT("wallet"), &bEnabled) && bEnabled) || Server->ShopWeapons.Num() == 0) {
					return Elapsed() > 120.0 ? Fail(TEXT("The first gun did not offer CONNECT YOUR WALLET in 120 s")) : false;
				}
				if (!bAsked) {
					Gun = Server->ShopWeapons[0].Id;
					Price = Server->ShopWeapons[0].Price;
					Test->TestEqual(TEXT("The gun shown is on the stage"), Menu->StageWeapon(), Gun);
					Test->TestEqual(TEXT("It shows its price"), MenuCheck::Line(Menu, TEXT("status")), FString::Printf(TEXT("%s D-BUCKS"), *SenniteUi::Grouped(Price)));
					bAsked = MenuCheck::Tap(Menu, TEXT("wallet"));
					Enter(EPhase::Connect);
					return false;
				}
				if (Elapsed() < 0.5) {
					return false;
				}
				// the tap handed the connect code over, as it does to a wallet: here the wallet outside the game is given it
				FString Handed;
				FPlatformApplicationMisc::ClipboardPaste(Handed);
				Test->TestEqual(TEXT("A tap on CONNECT YOUR WALLET hands over the game's connect code"), Handed, Server->ConnectCode);
				bCodeWritten = FFileHelper::SaveStringToFile(Handed, *(FPaths::ProjectSavedDir() / TEXT("WeaponShop") / TEXT("connect-code.txt")));
			}
			if (!(Server->bConnected && Server->bMaterialsKnown) || Server->bBusy || Server->ProofPending()) {
				return Elapsed() > 900.0 ? Fail(TEXT("The wallet did not connect and prove what it holds in 900 s")) : false;
			}
			Test->TestFalse(TEXT("A new wallet owns no gun"), Server->OwnsWeapon(Gun));
			UE_LOG(LogWeaponShopCheck, Display, TEXT("GAMECHECK connected: the wallet proves %d D-bucks; the shop sells %d guns; %s costs %d"), Server->DBucks, Server->ShopWeapons.Num(), *Gun, Price);
			Enter(EPhase::GetDBucks);
			return false;
		}

		case EPhase::GetDBucks: {
			if (Server->bBusy || Server->ProofPending()) {
				return Elapsed() > 600.0 ? Fail(FString::Printf(TEXT("The trade for D-bucks did not show in 600 s: %s"), *Server->Message)) : false;
			}
			if (Server->DBucks >= Price) {
				Test->TestTrue(TEXT("The D-bucks came from a trade under the gun"), Trades > 0);
				DBucksBefore = Server->DBucks;
				bool bEnabled = false;
				Test->TestFalse(TEXT("With enough D-bucks GET D-BUCKS is gone"), MenuCheck::Has(Menu, TEXT("dbucks")));
				Test->TestTrue(TEXT("BUY is there and can be tapped"), MenuCheck::Has(Menu, TEXT("buygun"), &bEnabled) && bEnabled);
				GameCheck::Shot(TEXT("WeaponShop"), TEXT("for_sale.png"));
				Enter(EPhase::TapBuy);
				return false;
			}
			// too few D-bucks: the gun's button trades ERA for them. A trade's D-bucks show with the wallet's next proof.
			if (Elapsed() < (Trades == 0 ? 1.0 : 20.0)) {
				return false;
			}
			if (Trades >= 3) {
				return Fail(FString::Printf(TEXT("Three trades did not bring the gun's price: the wallet proves %d D-bucks"), Server->DBucks));
			}
			bool bEnabled = false;
			Test->TestTrue(TEXT("With too few D-bucks the gun's button is GET D-BUCKS"), MenuCheck::Has(Menu, TEXT("dbucks"), &bEnabled) && bEnabled);
			Test->TestFalse(TEXT("BUY is not offered yet"), MenuCheck::Has(Menu, TEXT("buygun")));
			++Trades;
			MenuCheck::Tap(Menu, TEXT("dbucks"));
			Enter(EPhase::GetDBucks);
			return false;
		}

		case EPhase::TapBuy:
			// the picture is taken at the end of a frame: BUY is tapped after it
			if (Elapsed() < 0.3) {
				return false;
			}
			MenuCheck::Tap(Menu, TEXT("buygun"));
			Enter(EPhase::Bought);
			return false;

		case EPhase::Bought: {
			if (Elapsed() < 1.0 || Server->bBusy || Server->ProofPending() || !Server->OwnsWeapon(Gun)) {
				return Elapsed() > 900.0 ? Fail(FString::Printf(TEXT("The wallet did not prove it owns %s in 900 s: %s"), *Gun, *Server->Message)) : false;
			}
			if (Elapsed() < 2.0 || !MenuCheck::Has(Menu, TEXT("bought"))) {
				return Elapsed() > 900.0 ? Fail(TEXT("The store did not mark the gun bought")) : false;
			}
			Test->TestEqual(TEXT("The gun is the wallet's"), MenuCheck::Line(Menu, TEXT("status")), FString(TEXT("YOURS")));
			Test->TestFalse(TEXT("It is not offered again"), MenuCheck::Has(Menu, TEXT("buygun")));
			Test->TestEqual(TEXT("The wallet paid its price"), DBucksBefore - Server->DBucks, Price);
			Test->TestTrue(TEXT("Its tile says it is the player's"), MenuCheck::Line(Menu, TEXT("tile0")).EndsWith(TEXT("YOURS")));
			UE_LOG(LogWeaponShopCheck, Display, TEXT("GAMECHECK bought %s for %d D-bucks: the wallet proves it owns it, and %d D-bucks"), *Gun, Price, Server->DBucks);
			GameCheck::Shot(TEXT("WeaponShop"), TEXT("bought.png"));
			Enter(EPhase::Pass);
			return false;
		}

		case EPhase::Pass: {
			// what a game server is told of this wallet: a hero pass made out to this game, asked about by this game
			if (Server->Audience.IsEmpty()) {
				return Elapsed() > 60.0 ? Fail(TEXT("The game has no audience of its own to make a pass out to")) : false;
			}
			const TSharedRef<TArray<FString>> SpeaksFor = PassSpeaksFor;
			const TSharedRef<int32> State = PassState;
			TWeakObjectPtr<USennitePublisher> Asker(Server);
			Server->AskHeroPass(Server->Audience, [Asker, SpeaksFor, State](const FString& Pass) {
				USennitePublisher* Still = Asker.Get();
				if (Still == nullptr || Pass.IsEmpty()) {
					*State = -1;
					return;
				}
				Still->CheckHeroPass(Pass, [SpeaksFor, State](bool bOk, const TArray<FString>& Heroes, const FString&, const FString&) {
					*SpeaksFor = Heroes;
					*State = bOk ? 1 : -1;
				});
			});
			Enter(EPhase::Checked);
			return false;
		}

		case EPhase::Checked:
			if (*PassState == 0) {
				return Elapsed() > 120.0 ? Fail(TEXT("The hero pass was not answered in 120 s")) : false;
			}
			Test->TestEqual(TEXT("The publisher's server gave a pass and answered for it"), *PassState, 1);
			Test->TestTrue(TEXT("The pass speaks for the gun, by the shop's id"), PassSpeaksFor->Contains(Gun));
			UE_LOG(LogWeaponShopCheck, Display, TEXT("GAMECHECK the hero pass speaks for: %s"), *FString::Join(*PassSpeaksFor, TEXT(", ")));
			Enter(EPhase::Other);
			return false;

		case EPhase::Other: {
			// the other gun the shop sells is still for sale, and not the wallet's
			if (Server->ShopWeapons.Num() < 2) {
				UE_LOG(LogWeaponShopCheck, Display, TEXT("GAMECHECK finished"));
				Phase = EPhase::Done;
				return true;
			}
			const FString Second = Server->ShopWeapons[1].Id;
			if (Menu->BrowseWeapon != 1) {
				if (Elapsed() > 0.5) {
					MenuCheck::Tap(Menu, FName(*(FString(TEXT("gun:")) + Second)));
					Enter(EPhase::Other);
				}
				return false;
			}
			if (Elapsed() < 1.5) {
				return false;
			}
			Test->TestFalse(TEXT("The other gun is not the wallet's"), Server->OwnsWeapon(Second));
			Test->TestFalse(TEXT("and the pass does not speak for it"), PassSpeaksFor->Contains(Second));
			Test->TestFalse(TEXT("It is not marked bought"), MenuCheck::Has(Menu, TEXT("bought")));
			Test->TestEqual(TEXT("It shows its price"), MenuCheck::Line(Menu, TEXT("status")), FString::Printf(TEXT("%s D-BUCKS"), *SenniteUi::Grouped(Server->ShopWeapons[1].Price)));
			UE_LOG(LogWeaponShopCheck, Display, TEXT("GAMECHECK %s is still for sale"), *Second);
			UE_LOG(LogWeaponShopCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;
		}

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteWeaponShopTest, "Sennite.WeaponShop.BuyAndOwn",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteWeaponShopTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FWeaponShopRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
