// Swap check. Run in the game on the Menu map with a publisher's server (-PublisherUrl=) and a wallet
// outside the game that approves the game's connect code (Server/test/game/swap.test.ts is that wallet),
// by taps on the screen as a phone gives them:
// - STORE, SWAP: CONNECT YOUR WALLET hands the connect code over, and the wallet connects;
// - 2 ERA typed on the keypad, PRICE: the wallet's price in D-bucks is shown, and SWAP can be tapped;
// - SWAP: the wallet proves more D-bucks than before;
// - SWITCH, 500 typed, PRICE, SWAP: the wallet proves 500 D-bucks fewer, and more ERA.
//
// Server/test/game/swap.test.ts runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MenuCheck.h"
#include "SennitePublisher.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwapCheck, Log, All);

namespace
{
	class FSwapRun : public IAutomationLatentCommand
	{
	public:
		explicit FSwapRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}

		virtual bool Update() override;

	private:
		enum class EPhase { WaitForMenu, Open, Connect, TypeEra, PriceEra, TapSwapEra, SwapEra, Switch, TypeDBucks, PriceDBucks, SwapDBucks, Done };

		void Enter(EPhase Next) { Phase = Next; PhaseStart = FPlatformTime::Seconds(); }
		double Elapsed() const { return FPlatformTime::Seconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		/** Tap the keypad until the amount typed is Want, a tap at a time. True once it is. */
		bool Typed(ASenniteMenuController* Menu, const FString& Want);

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForMenu;
		double PhaseStart = 0.0;
		double LastTap = 0.0;
		bool bCodeWritten = false;
		bool bAsked = false;
		int32 DBucksBefore = 0;
		FString EraBefore;
		FString Shown;
	};

	bool FSwapRun::Typed(ASenniteMenuController* Menu, const FString& Want)
	{
		const FString Now = MenuCheck::Line(Menu, TEXT("amount"));
		if (Now == Want) {
			return true;
		}
		if (FPlatformTime::Seconds() - LastTap < 0.4) {
			return false;
		}
		LastTap = FPlatformTime::Seconds();
		// what is there and is not the start of what is wanted comes off; then the next mark goes on
		if (!Now.IsEmpty() && !Want.StartsWith(Now)) {
			MenuCheck::Tap(Menu, TEXT("num:del"));
		}
		else {
			MenuCheck::Tap(Menu, FName(*FString::Printf(TEXT("num:%c"), Want[Now.Len()])));
		}
		return false;
	}

	bool FSwapRun::Update()
	{
		if (FPlatformTime::Seconds() - RealStart > 1500.0) {
			return Fail(FString::Printf(TEXT("The swap check did not finish in 1500 s (stuck at step %d)"), int32(Phase)));
		}
		UWorld* World = GameCheck::FindGameWorld();
		ASenniteMenuController* Menu = MenuCheck::Menu(World);
		USennitePublisher* Server = World != nullptr && World->GetGameInstance() != nullptr ? World->GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
		if (Server == nullptr || !Server->IsOn()) {
			return Fail(TEXT("No publisher's server is set: run the game with -PublisherUrl= (Server/test/game/swap.test.ts does)"));
		}
		if (Phase != EPhase::WaitForMenu && Phase != EPhase::Open && (Menu == nullptr || Menu->Page != ESenniteMenuPage::Swap)) {
			return Fail(TEXT("The store's swap closed"));
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
			if (Menu == nullptr || Menu->Page != ESenniteMenuPage::Swap) {
				if (Elapsed() > 1.0) {
					MenuCheck::Tap(Menu, TEXT("tab:swap"));
					Enter(EPhase::Open);
				}
				return false;
			}
			Enter(EPhase::Connect);
			return false;

		case EPhase::Connect: {
			if (!bCodeWritten) {
				// with no wallet connected, the way to connect one, once the game has a code for it and the vaults are read
				bool bEnabled = false;
				if (!(MenuCheck::Has(Menu, TEXT("wallet"), &bEnabled) && bEnabled) || Server->SwapTokens.Num() < 2) {
					return Elapsed() > 120.0 ? Fail(TEXT("The swap did not offer CONNECT YOUR WALLET in 120 s")) : false;
				}
				if (!bAsked) {
					Test->TestEqual(TEXT("It opens paying ERA"), MenuCheck::Line(Menu, TEXT("pay")), FString(TEXT("ERA")));
					Test->TestEqual(TEXT("for D-bucks"), MenuCheck::Line(Menu, TEXT("get")), FString(TEXT("D-BUCKS")));
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
				bCodeWritten = FFileHelper::SaveStringToFile(Handed, *(FPaths::ProjectSavedDir() / TEXT("Swap") / TEXT("connect-code.txt")));
			}
			if (!(Server->bConnected && Server->bMaterialsKnown) || Server->bBusy || Server->ProofPending()) {
				return Elapsed() > 900.0 ? Fail(TEXT("The wallet did not connect and prove what it holds in 900 s")) : false;
			}
			Test->TestTrue(TEXT("Connected, the swap shows what the wallet holds of what is paid"), MenuCheck::Line(Menu, TEXT("holds")).StartsWith(TEXT("YOUR WALLET HAS ")));
			Test->TestTrue(TEXT("A new wallet has ERA to swap"), FCString::Atod(*Server->Era) >= 2.0);
			UE_LOG(LogSwapCheck, Display, TEXT("GAMECHECK connected: the wallet proves %s ERA and %d D-bucks"), *Server->Era, Server->DBucks);
			Enter(EPhase::TypeEra);
			return false;
		}

		case EPhase::TypeEra: {
			if (!Typed(Menu, TEXT("2"))) {
				return Elapsed() > 30.0 ? Fail(TEXT("The keypad did not type 2")) : false;
			}
			bool bPrice = false;
			bool bTrade = true;
			Test->TestTrue(TEXT("With an amount typed PRICE can be tapped"), MenuCheck::Has(Menu, TEXT("price"), &bPrice) && bPrice);
			Test->TestTrue(TEXT("SWAP is there"), MenuCheck::Has(Menu, TEXT("trade"), &bTrade));
			Test->TestFalse(TEXT("but not before a price is shown"), bTrade);
			Test->TestFalse(TEXT("No price is shown that was not asked"), !MenuCheck::Line(Menu, TEXT("gets")).IsEmpty());
			Test->TestFalse(TEXT("The vault that trades the two is shown as it stands"), MenuCheck::Line(Menu, TEXT("vault")).IsEmpty());
			MenuCheck::Tap(Menu, TEXT("price"));
			Enter(EPhase::PriceEra);
			return false;
		}

		case EPhase::PriceEra: {
			if (Elapsed() < 1.0 || Server->bBusy) {
				return Elapsed() > 300.0 ? Fail(FString::Printf(TEXT("The price of 2 ERA did not come in 300 s: %s"), *Server->Message)) : false;
			}
			Shown = MenuCheck::Line(Menu, TEXT("gets"));
			bool bTrade = false;
			Test->TestTrue(FString::Printf(TEXT("The wallet's price is shown (the server says: %s)"), *Server->Message), FCString::Atoi(*Shown) > 0);
			Test->TestEqual(TEXT("It is the price the wallet gave"), Shown, Server->SwapPrice.AmountOut);
			Test->TestTrue(TEXT("With a price shown SWAP can be tapped"), MenuCheck::Has(Menu, TEXT("trade"), &bTrade) && bTrade);
			if (!bTrade) {
				return Fail(TEXT("No price, no swap"));
			}
			DBucksBefore = Server->DBucks;
			EraBefore = Server->Era;
			UE_LOG(LogSwapCheck, Display, TEXT("GAMECHECK price: 2 ERA gets %s D-bucks now"), *Shown);
			GameCheck::Shot(TEXT("Swap"), TEXT("priced.png"));
			Enter(EPhase::TapSwapEra);
			return false;
		}

		case EPhase::TapSwapEra:
			// the picture is taken at the end of a frame: SWAP is tapped after it
			if (Elapsed() < 0.3) {
				return false;
			}
			MenuCheck::Tap(Menu, TEXT("trade"));
			Enter(EPhase::SwapEra);
			return false;

		case EPhase::SwapEra:
			if (Elapsed() < 1.0) {
				return false;
			}
			if (Server->bBusy || Server->ProofPending() || Server->DBucks <= DBucksBefore) {
				return Elapsed() > 600.0 ? Fail(FString::Printf(TEXT("The swap of 2 ERA did not show in the wallet's proof in 600 s: %s"), *Server->Message)) : false;
			}
			Test->TestTrue(TEXT("The wallet proves no less than 97% of the D-bucks shown"), Server->DBucks - DBucksBefore >= FCString::Atoi(*Shown) * 97 / 100);
			Test->TestTrue(TEXT("and less ERA than before"), FCString::Atod(*Server->Era) < FCString::Atod(*EraBefore));
			Test->TestTrue(TEXT("The price shown is gone with the swap: the next is asked again"), MenuCheck::Line(Menu, TEXT("gets")).IsEmpty());
			UE_LOG(LogSwapCheck, Display, TEXT("GAMECHECK swapped 2 ERA for %d D-bucks: the wallet proves %d D-bucks and %s ERA"), Server->DBucks - DBucksBefore, Server->DBucks, *Server->Era);
			GameCheck::Shot(TEXT("Swap"), TEXT("swapped.png"));
			Enter(EPhase::Switch);
			return false;

		case EPhase::Switch:
			if (MenuCheck::Line(Menu, TEXT("pay")) != TEXT("D-BUCKS")) {
				if (Elapsed() > 0.5) {
					MenuCheck::Tap(Menu, TEXT("swap:flip"));
					Enter(EPhase::Switch);
				}
				return false;
			}
			Test->TestEqual(TEXT("SWITCH: D-bucks are paid, for ERA"), MenuCheck::Line(Menu, TEXT("get")), FString(TEXT("ERA")));
			Enter(EPhase::TypeDBucks);
			return false;

		case EPhase::TypeDBucks:
			if (!Typed(Menu, TEXT("500"))) {
				return Elapsed() > 30.0 ? Fail(TEXT("The keypad did not type 500")) : false;
			}
			Test->TestTrue(TEXT("The wallet holds the 500 D-bucks to swap back"), Server->DBucks >= 500);
			MenuCheck::Tap(Menu, TEXT("price"));
			Enter(EPhase::PriceDBucks);
			return false;

		case EPhase::PriceDBucks: {
			if (Elapsed() < 1.0 || Server->bBusy) {
				return Elapsed() > 300.0 ? Fail(FString::Printf(TEXT("The price of 500 D-bucks did not come in 300 s: %s"), *Server->Message)) : false;
			}
			Shown = MenuCheck::Line(Menu, TEXT("gets"));
			bool bTrade = false;
			Test->TestTrue(FString::Printf(TEXT("The wallet's price in ERA is shown (the server says: %s)"), *Server->Message), FCString::Atod(*Shown) > 0.0);
			if (!(MenuCheck::Has(Menu, TEXT("trade"), &bTrade) && bTrade)) {
				return Fail(TEXT("No price for D-bucks in ERA, no swap back"));
			}
			DBucksBefore = Server->DBucks;
			EraBefore = Server->Era;
			UE_LOG(LogSwapCheck, Display, TEXT("GAMECHECK price: 500 D-bucks get %s ERA now"), *Shown);
			GameCheck::Shot(TEXT("Swap"), TEXT("priced_back.png"));
			MenuCheck::Tap(Menu, TEXT("trade"));
			Enter(EPhase::SwapDBucks);
			return false;
		}

		case EPhase::SwapDBucks:
			if (Elapsed() < 1.0 || Server->bBusy || Server->ProofPending() || Server->DBucks >= DBucksBefore) {
				return Elapsed() > 600.0 ? Fail(FString::Printf(TEXT("The swap of 500 D-bucks did not show in the wallet's proof in 600 s: %s"), *Server->Message)) : false;
			}
			Test->TestEqual(TEXT("The wallet proves 500 D-bucks fewer"), DBucksBefore - Server->DBucks, 500);
			Test->TestTrue(TEXT("and more ERA than before"), FCString::Atod(*Server->Era) > FCString::Atod(*EraBefore));
			UE_LOG(LogSwapCheck, Display, TEXT("GAMECHECK swapped 500 D-bucks for ERA: the wallet proves %d D-bucks and %s ERA (it had %s)"), Server->DBucks, *Server->Era, *EraBefore);
			GameCheck::Shot(TEXT("Swap"), TEXT("swapped_back.png"));
			UE_LOG(LogSwapCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteSwapTest, "Sennite.Swap.BothWays",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteSwapTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSwapRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
