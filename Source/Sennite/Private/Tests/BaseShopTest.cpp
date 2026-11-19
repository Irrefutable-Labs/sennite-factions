// Base shop check. Run in the game on the test range against a publisher's server on DSM: once a
// wallet is connected the game asks it what it holds; H opens the shop; F trades ERA for D-bucks
// through the publisher's vault; S and F buy a skin with D-bucks. The skin is then in the
// inventory (I), owned and not worn; F there wears it, and the hero is in that skin. The player
// then sells the skin to another player from their own wallet, outside the game: the next proof no
// longer lists it, the inventory is empty, and the hero is back in her own mesh.
//
// The wallet is a real one, driven from outside the game. This check writes the connect code the
// game shows to Saved/BaseShop/connect-code.txt, as a player would read it off the screen, and
// writes Saved/BaseShop/worn.txt once the skin is on the hero, as someone watching would see.
//
// Tools/check_base_editor.sh runs this with the accounts it needs and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "SenniteBaseEditor.h"
#include "SenniteCharacter.h"
#include "SennitePublisher.h"
#include "SenniteUi.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogBaseShopCheck, Log, All);

namespace
{
	class FBaseShopRun : public IAutomationLatentCommand
	{
	public:
		explicit FBaseShopRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForGame, Opened, Shop, DBucks, Bought, Inventory, Worn, InPlay, Sold, AfterSale, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		void Tap(APlayerController* Controller, const FKey& InKey) const
		{
			GameCheck::Key(Controller, InKey, IE_Pressed);
			GameCheck::Key(Controller, InKey, IE_Released);
		}

		static FString Drawn(const ASenniteBaseEditor* Editor, const TCHAR* Name)
		{
			const FString Prefix = FString(Name) + TEXT("=");
			for (const FString& Line : Editor->LastDrawn) {
				if (Line.StartsWith(Prefix)) {
					return Line.RightChop(Prefix.Len());
				}
			}
			return FString();
		}

		void Log(const ASenniteBaseEditor* Editor, const TCHAR* When)
		{
			UE_LOG(LogBaseShopCheck, Display, TEXT("GAMECHECK panel %s: %s"), When, *FString::Join(Editor->LastDrawn, TEXT(" | ")));
		}

		/** The asset the hero's body is now. */
		static FString Body(const ASenniteCharacter* Hero)
		{
			const USkeletalMesh* Mesh = Hero->GetMesh()->GetSkeletalMeshAsset();
			return Mesh != nullptr ? Mesh->GetPathName() : FString();
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForGame;
		double PhaseStart = 0.0;
		int32 Step = 0;
		double LastProof = 0.0;
		FString OwnBody;
		int32 DBucksBought = 0;
		int32 DBucksAfterSkin = 0;
		/** The first skin the shop lists: the one this check buys. */
		FSenniteSkinForSale Skin;
	};

	bool FBaseShopRun::Update()
	{
		if (FPlatformTime::Seconds() - RealStart > 480.0) {
			return Fail(TEXT("The base shop check did not finish in 480 s"));
		}
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller != nullptr ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		USennitePublisher* Server = World != nullptr && World->GetGameInstance() != nullptr ? World->GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
		ASenniteBaseEditor* Editor = ASenniteBaseEditor::Find(World);
		if (Phase == EPhase::WaitForGame) {
			if (Server != nullptr && !Server->IsOn()) {
				return Fail(TEXT("No publisher's server is set: run the game with -PublisherUrl= (Tools/check_base_editor.sh does)"));
			}
			if (Hero == nullptr || Editor == nullptr || Server == nullptr || !Server->HasWorld() || Server->ConnectCode.IsEmpty() || World->GetTimeSeconds() < 2.0) {
				return false;
			}
			// a run without a window starts with a stray mouse movement; begin from the view behind the hero
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			OwnBody = Body(Hero);
			Test->TestTrue(TEXT("The hero starts in her own mesh"), OwnBody == Hero->HeroMesh.ToString());
			Tap(Controller, EKeys::B);
			Enter(EPhase::Opened, World);
			return false;
		}
		if (Hero == nullptr || Editor == nullptr || Server == nullptr) {
			return Fail(TEXT("The hero, the editor or the publisher's client went away during the check"));
		}

		switch (Phase) {
		case EPhase::Opened:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				Test->TestTrue(TEXT("B opens the planning view"), Editor->bEditing);
				// as a player reads the code off the screen into their wallet
				FFileHelper::SaveStringToFile(Server->ConnectCode, *(FPaths::ProjectSavedDir() / TEXT("BaseShop") / TEXT("connect-code.txt")));
				Tap(Controller, EKeys::H);
				return false;
			}
			if (!Server->bConnected) {
				return Elapsed(World) > 180.0 ? Fail(TEXT("No wallet approved the connect code in 180 s")) : false;
			}
			Test->TestFalse(TEXT("H opens no shop before a wallet is connected"), Editor->bShop);
			Tap(Controller, EKeys::H);
			Enter(EPhase::Shop, World);
			return false;

		case EPhase::Shop:
			// once the wallet is connected the game asks it, unasked, what it holds
			if (!(Editor->bShop && Server->bMaterialsKnown && Server->ShopSkins.Num() > 0) || Server->bBusy || Server->ProofPending()) {
				return Elapsed(World) > 120.0 ? Fail(FString::Printf(TEXT("The shop did not show with the wallet's proof in 120 s: %s"), *Server->Message)) : false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Skin = Server->ShopSkins[0];
			Test->TestEqual(TEXT("The panel is on its shop page"), Drawn(Editor, TEXT("page")), FString(TEXT("SHOP")));
			Test->TestEqual(TEXT("A new wallet holds no D-bucks"), Server->DBucks, 0);
			Test->TestTrue(TEXT("The panel shows what the wallet proved it holds"), Drawn(Editor, TEXT("purse")).StartsWith(TEXT("D-BUCKS 0   ERA ")));
			Test->TestEqual(TEXT("The shop shows the wallet's D-bucks at its top right"), Drawn(Editor, TEXT("balance")), FString(TEXT("0 D-BUCKS")));
			Test->TestEqual(TEXT("The first row trades ERA for D-bucks, chosen"), Drawn(Editor, TEXT("shop0")), FString(TEXT("> BUY D-BUCKS   2 ERA")));
			Test->TestEqual(TEXT("The shop lists the skin at its price"), Drawn(Editor, TEXT("shop1")), FString::Printf(TEXT("  %s   %d D-BUCKS"), *Skin.Name.ToUpper(), Skin.Price));
			Test->TestEqual(TEXT("The wallet owns no skin"), Server->OwnedSkins.Num(), 0);
			Log(Editor, TEXT("in the shop"));
			Tap(Controller, EKeys::F);
			Enter(EPhase::DBucks, World);
			return false;

		case EPhase::DBucks:
			if (Server->DBucks < Skin.Price || Server->bBusy) {
				return Elapsed(World) > 180.0 ? Fail(FString::Printf(TEXT("The trade for D-bucks did not show in 180 s: %s"), *Server->Message)) : false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				DBucksBought = Server->DBucks;
				Tap(Controller, EKeys::S);
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestEqual(TEXT("The panel shows the D-bucks the wallet proves it holds"), Drawn(Editor, TEXT("purse")).Left(FString::Printf(TEXT("D-BUCKS %d   "), DBucksBought).Len()), FString::Printf(TEXT("D-BUCKS %d   "), DBucksBought));
			Test->TestEqual(TEXT("The shop's top right shows the D-bucks the trade brought"), Drawn(Editor, TEXT("balance")), FString::Printf(TEXT("%s D-BUCKS"), *SenniteUi::Grouped(DBucksBought)));
			Test->TestTrue(TEXT("The skin's row is chosen, and the panel offers to buy it"), Drawn(Editor, TEXT("shop1")).StartsWith(TEXT("> ")) && Drawn(Editor, TEXT("keys")).StartsWith(TEXT("F: buy")));
			Log(Editor, TEXT("with D-bucks"));
			Tap(Controller, EKeys::F);
			Enter(EPhase::Bought, World);
			return false;

		case EPhase::Bought:
			if (!Server->Owns(Skin.Id) || Server->bBusy || Server->ProofPending()) {
				return Elapsed(World) > 240.0 ? Fail(FString::Printf(TEXT("The skin was not the wallet's in 240 s: %s"), *Server->Message)) : false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			DBucksAfterSkin = Server->DBucks;
			Test->TestEqual(TEXT("The wallet paid the skin's price in D-bucks"), DBucksBought - DBucksAfterSkin, Skin.Price);
			Test->TestEqual(TEXT("The shop still sells the skin, and says the wallet owns one"), Drawn(Editor, TEXT("shop1")), FString::Printf(TEXT("> %s   %d D-BUCKS   OWNED 1"), *Skin.Name.ToUpper(), Skin.Price));
			Test->TestTrue(TEXT("Owning a skin does not put it on: the hero is in her own mesh"), Body(Hero) == OwnBody);
			Test->TestTrue(TEXT("Nothing is worn"), Server->Wearing.IsEmpty());
			Log(Editor, TEXT("with the skin bought"));
			Tap(Controller, EKeys::I);
			Enter(EPhase::Inventory, World);
			return false;

		case EPhase::Inventory:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestTrue(TEXT("I opens the inventory"), Editor->bInventory && !Editor->bShop);
			Test->TestEqual(TEXT("The panel is on its inventory page"), Drawn(Editor, TEXT("page")), FString(TEXT("INVENTORY")));
			Test->TestEqual(TEXT("The inventory holds the one skin, owned and not worn"), Drawn(Editor, TEXT("skin0")), FString::Printf(TEXT("> %s #1"), *Skin.Name.ToUpper()));
			Test->TestEqual(TEXT("The inventory holds nothing else"), Server->Inventory.Num(), 1);
			Test->TestTrue(TEXT("The inventory shows the D-bucks the wallet proves it holds"), Drawn(Editor, TEXT("purse")).StartsWith(FString::Printf(TEXT("D-BUCKS %d   "), DBucksAfterSkin)));
			Test->TestTrue(TEXT("The panel offers to wear it"), Drawn(Editor, TEXT("keys")).StartsWith(TEXT("F: wear")));
			Test->TestTrue(TEXT("The hero is still in her own mesh"), Body(Hero) == OwnBody);
			Log(Editor, TEXT("in the inventory"));
			Tap(Controller, EKeys::F);
			Enter(EPhase::Worn, World);
			return false;

		case EPhase::Worn:
			if (Elapsed(World) < 1.5) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				Test->TestEqual(TEXT("The player wears the skin"), Server->Wearing, Skin.Id);
				Test->TestTrue(TEXT("The hero is in the skin's mesh"), Body(Hero) != OwnBody && !Body(Hero).IsEmpty());
				Test->TestEqual(TEXT("The inventory shows the skin as worn"), Drawn(Editor, TEXT("skin0")), FString::Printf(TEXT("> %s #1   WORN"), *Skin.Name.ToUpper()));
				Test->TestTrue(TEXT("The panel offers to take it off"), Drawn(Editor, TEXT("keys")).StartsWith(TEXT("F: take off")));
				Test->TestFalse(TEXT("Wearing asks nothing of the wallet"), Server->bBusy);
				UE_LOG(LogBaseShopCheck, Display, TEXT("GAMECHECK the hero wears %s"), *Body(Hero));
				Log(Editor, TEXT("with the skin worn"));
				GameCheck::Shot(TEXT("BaseShop"), TEXT("inventory.png"));
				return false;
			}
			if (Elapsed(World) < 3.0) {
				return false;
			}
			Tap(Controller, EKeys::B);
			Enter(EPhase::InPlay, World);
			return false;

		case EPhase::InPlay:
			if (Elapsed(World) < 1.5) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				Test->TestFalse(TEXT("B closes the planning view"), Editor->bEditing);
				Test->TestTrue(TEXT("The hero wears the skin in play"), Body(Hero) != OwnBody);
				GameCheck::Shot(TEXT("BaseShop"), TEXT("worn.png"));
				return false;
			}
			if (Elapsed(World) < 3.0) {
				return false;
			}
			// someone watching has seen the skin on the hero: the player now sells it, from their own wallet
			FFileHelper::SaveStringToFile(Body(Hero), *(FPaths::ProjectSavedDir() / TEXT("BaseShop") / TEXT("worn.txt")));
			Tap(Controller, EKeys::B);
			Enter(EPhase::Sold, World);
			return false;

		case EPhase::Sold:
			// the game learns of the sale only from the wallet's next proof: M asks for one until the skin is gone from it
			if (Server->Owns(Skin.Id) || Server->bBusy || Server->ProofPending()) {
				if (Elapsed(World) > 300.0) {
					return Fail(FString::Printf(TEXT("The wallet still proved it owned the skin after 300 s: %s"), *Server->Message));
				}
				if (!Server->bBusy && !Server->ProofPending() && World->GetTimeSeconds() - LastProof > 3.0) {
					LastProof = World->GetTimeSeconds();
					Tap(Controller, EKeys::M);
				}
				return false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				Tap(Controller, EKeys::I);
				return false;
			}
			if (Elapsed(World) < 1.5) {
				return false;
			}
			Test->TestTrue(TEXT("A skin the wallet no longer owns is not on the hero"), Body(Hero) == OwnBody);
			Test->TestEqual(TEXT("The inventory holds no skin"), Drawn(Editor, TEXT("skins")), FString(TEXT("NO SKINS")));
			Test->TestTrue(TEXT("The seller's wallet holds the proceeds"), Server->DBucks > DBucksAfterSkin);
			Test->TestTrue(TEXT("The inventory shows them"), Drawn(Editor, TEXT("purse")).StartsWith(FString::Printf(TEXT("D-BUCKS %d   "), Server->DBucks)));
			UE_LOG(LogBaseShopCheck, Display, TEXT("GAMECHECK the sale brought %d D-bucks"), Server->DBucks - DBucksAfterSkin);
			Log(Editor, TEXT("after the sale"));
			Tap(Controller, EKeys::B);
			Enter(EPhase::AfterSale, World);
			return false;

		case EPhase::AfterSale:
			if (Elapsed(World) < 1.5) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				GameCheck::Shot(TEXT("BaseShop"), TEXT("after-sale.png"));
				return false;
			}
			if (Elapsed(World) < 3.0) {
				return false;
			}
			UE_LOG(LogBaseShopCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteBaseShopTest, "Sennite.BaseShop.BuyWearAndSell",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteBaseShopTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBaseShopRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
