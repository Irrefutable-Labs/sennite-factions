// Store check. Run in the game on the Menu map with a publisher's server (-PublisherUrl=), a player of
// its own on it with no wallet connected, by taps on the screen as a phone gives them:
// - the first screen has STORE and BASE EDITOR, and with a publisher's server BASE EDITOR can be tapped;
// - STORE opens on the heroes; SKINS shows each skin the shop lists, on the hero it is a skin of, turning
//   on the stage in the skin's own mesh, with its price; the arrows and the tiles go through them;
// - a skin not owned offers the next step toward it: with no wallet connected, connecting one;
// - SWAP shows what the publisher's vaults hold to swap, takes an amount from the screen's keypad, and
//   changes the two sides over; with no wallet connected it offers to connect one, and no price is asked;
// - BACK goes to the first screen, and BASE EDITOR opens the test range on the planning view of a parcel.
//
// Tools/check_store.sh runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MenuCheck.h"
#include "SenniteBaseEditor.h"
#include "SenniteHeroes.h"
#include "SennitePublisher.h"
#include "SenniteUi.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogStoreCheck, Log, All);

namespace
{
	FString StoreStageMesh(UWorld* World)
	{
		TActorIterator<ASkeletalMeshActor> It(World);
		return It ? FSoftObjectPath(It->GetSkeletalMeshComponent()->GetSkeletalMeshAsset()).ToString() : FString();
	}

	FString StoreSkinMesh(const FString& SkinId)
	{
		const FSenniteSkinMesh* Skin = GetDefault<ASenniteBaseEditor>()->Skins.FindByPredicate([&SkinId](const FSenniteSkinMesh& Each) { return Each.Id == SkinId; });
		return Skin != nullptr ? Skin->Mesh.ToString() : FString();
	}

	/** The hero a shop skin is a skin of, as the game's list of skins names it. */
	FName StoreSkinHero(const FString& SkinId)
	{
		const FSenniteSkinMesh* Skin = GetDefault<ASenniteBaseEditor>()->Skins.FindByPredicate([&SkinId](const FSenniteSkinMesh& Each) { return Each.Id == SkinId; });
		return Skin != nullptr ? Skin->Hero : FName();
	}

	class FStoreRun : public IAutomationLatentCommand
	{
	public:
		explicit FStoreRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}

		virtual bool Update() override;

	private:
		enum class EPhase { WaitForMenu, Store, Skins, SecondSkin, ByTile, DBucks, Back, Base, Done };

		void Enter(EPhase Next) { Phase = Next; PhaseStart = FPlatformTime::Seconds(); }
		double Elapsed() const { return FPlatformTime::Seconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForMenu;
		double PhaseStart = 0.0;
		/** How far through the swap section's steps the check is. */
		int32 Step = 0;
	};

	bool FStoreRun::Update()
	{
		if (FPlatformTime::Seconds() - RealStart > 300.0) {
			return Fail(FString::Printf(TEXT("The store check did not finish in 300 s (stuck at step %d)"), int32(Phase)));
		}
		UWorld* World = GameCheck::FindGameWorld();
		ASenniteMenuController* Menu = MenuCheck::Menu(World);
		USennitePublisher* Server = World != nullptr && World->GetGameInstance() != nullptr ? World->GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
		if (Server == nullptr || !Server->IsOn()) {
			return Fail(TEXT("No publisher's server is set: run the game with -PublisherUrl= (Tools/check_store.sh does)"));
		}

		switch (Phase) {
		case EPhase::WaitForMenu: {
			if (Menu == nullptr || !MenuCheck::Has(Menu, TEXT("store")) || World->GetTimeSeconds() < 2.0) {
				return false;
			}
			bool bBase = false;
			Test->TestTrue(TEXT("The first screen has BASE EDITOR"), MenuCheck::Has(Menu, TEXT("base"), &bBase));
			Test->TestTrue(TEXT("With a publisher's server it can be tapped"), bBase);
			Test->TestTrue(TEXT("and nothing says it cannot"), MenuCheck::Line(Menu, TEXT("why")).IsEmpty());
			Test->TestFalse(TEXT("The first screen has no HEROES of its own: they are in the store"), MenuCheck::Has(Menu, TEXT("heroes")));
			GameCheck::Shot(TEXT("Store"), TEXT("menu.png"));
			Enter(EPhase::Store);
			return false;
		}

		case EPhase::Store:
			if (Menu == nullptr || Menu->Page != ESenniteMenuPage::Heroes) {
				// the picture is taken at the end of a frame: nothing is tapped until it is
				if (Elapsed() > 0.3) {
					MenuCheck::Tap(Menu, TEXT("store"));
					Enter(EPhase::Store);
				}
				return false;
			}
			if (Elapsed() < 0.5) {
				return false;
			}
			Test->TestEqual(TEXT("STORE opens on the heroes"), MenuCheck::Line(Menu, TEXT("section")), FString(TEXT("HEROES")));
			Test->TestFalse(TEXT("A game with a shop shows what the wallet holds, or why it shows nothing"), MenuCheck::Line(Menu, TEXT("balance")).IsEmpty());
			MenuCheck::Tap(Menu, TEXT("tab:skins"));
			Enter(EPhase::Skins);
			return false;

		case EPhase::Skins: {
			if (Menu == nullptr || Menu->Page != ESenniteMenuPage::Skins || Server->ShopSkins.Num() == 0) {
				return Elapsed() > 60.0 ? Fail(TEXT("SKINS did not show the shop's skins in 60 s")) : false;
			}
			const FSenniteSkinForSale& Skin = Server->ShopSkins[Menu->BrowseSkin];
			bool bWallet = false;
			if (StoreStageMesh(World) != StoreSkinMesh(Skin.Id) || !(MenuCheck::Has(Menu, TEXT("wallet"), &bWallet) && bWallet) || Elapsed() < 2.0) {
				return Elapsed() > 120.0 ? Fail(FString::Printf(TEXT("The skin %s did not stand on the stage in its mesh with CONNECT YOUR WALLET under it in 120 s (the stage has %s)"), *Skin.Id, *StoreStageMesh(World))) : false;
			}
			Test->TestEqual(TEXT("The store names the section open"), MenuCheck::Line(Menu, TEXT("section")), FString(TEXT("SKINS")));
			Test->TestEqual(TEXT("It opens on the shop's first skin"), Menu->BrowseSkin, 0);
			Test->TestEqual(TEXT("The skin's name is shown"), MenuCheck::Line(Menu, TEXT("name")), Skin.Name.ToUpper());
			Test->TestEqual(TEXT("and its price, as the shop gives it"), MenuCheck::Line(Menu, TEXT("status")), FString::Printf(TEXT("%s D-BUCKS"), *SenniteUi::Grouped(Skin.Price)));
			Test->TestTrue(TEXT("It says which hero it is a skin of"), MenuCheck::Line(Menu, TEXT("count")).Contains(TEXT("FOR ")));
			Test->TestEqual(TEXT("The stage holds the skin, not a hero as it ships"), Menu->StageHeroes().Num() == 1 ? Menu->StageHeroes()[0].ToString() : FString(), FString(TEXT("skin:")) + Skin.Id);
			Test->TestFalse(TEXT("A skin not owned has no WEAR"), MenuCheck::Has(Menu, TEXT("wear")));
			Test->TestFalse(TEXT("It says what the button is for"), MenuCheck::Line(Menu, TEXT("hint")).IsEmpty());
			{
				// the row of heroes that have skins, and under it a tile for each skin of the hero shown, and no other's
				TArray<FName> Wearers;
				for (const FSenniteSkinForSale& Each : Server->ShopSkins) {
					const FName Wearer = StoreSkinHero(Each.Id);
					Wearers.AddUnique(Wearer);
					Test->TestEqual(FString::Printf(TEXT("The skin %s has a tile only while its hero's skins are shown"), *Each.Id), MenuCheck::Has(Menu, FName(*(FString(TEXT("skin:")) + Each.Id))), Wearer == StoreSkinHero(Skin.Id));
				}
				if (Wearers.Num() > 1) {
					for (const FName Wearer : Wearers) {
						Test->TestTrue(FString::Printf(TEXT("%s, which has skins, is in the row of heroes"), *Wearer.ToString()), MenuCheck::Has(Menu, FName(*FString::Printf(TEXT("skinsof:%s"), *Wearer.ToString()))));
					}
				}
				UE_LOG(LogStoreCheck, Display, TEXT("GAMECHECK skins: for %d heroes"), Wearers.Num());
			}
			UE_LOG(LogStoreCheck, Display, TEXT("GAMECHECK skins: %d listed; %s on the stage as %s; %s"), Server->ShopSkins.Num(), *Skin.Id, *StoreStageMesh(World), *MenuCheck::Line(Menu, TEXT("count")));
			GameCheck::Shot(TEXT("Store"), TEXT("skins.png"));
			Enter(EPhase::SecondSkin);
			return false;
		}

		case EPhase::SecondSkin: {
			if (Server->ShopSkins.Num() < 2) {
				Enter(EPhase::DBucks);
				return false;
			}
			if (Menu->BrowseSkin != 1) {
				if (Elapsed() > 0.3) {
					MenuCheck::Tap(Menu, TEXT("next"));
					Enter(EPhase::SecondSkin);
				}
				return false;
			}
			const FSenniteSkinForSale& Skin = Server->ShopSkins[1];
			if (StoreStageMesh(World) != StoreSkinMesh(Skin.Id) || Elapsed() < 1.5) {
				return Elapsed() > 30.0 ? Fail(FString::Printf(TEXT("The arrow did not bring the skin %s onto the stage in 30 s"), *Skin.Id)) : false;
			}
			Test->TestEqual(TEXT("The arrow brings the next skin up"), MenuCheck::Line(Menu, TEXT("name")), Skin.Name.ToUpper());
			GameCheck::Shot(TEXT("Store"), TEXT("skins_second.png"));
			Enter(EPhase::ByTile);
			return false;
		}

		case EPhase::ByTile: {
			// the last skin the shop lists is another hero's, when more than one hero has skins: that hero's place in
			// the row brings its skins up, and the skin's own tile shows it
			const int32 Last = Server->ShopSkins.Num() - 1;
			const FName Wearer = StoreSkinHero(Server->ShopSkins[Last].Id);
			if (Menu->BrowseSkin != Last) {
				if (Elapsed() > 0.5) {
					const FName Tile(*(FString(TEXT("skin:")) + Server->ShopSkins[Last].Id));
					if (MenuCheck::Has(Menu, Tile)) {
						MenuCheck::Tap(Menu, Tile);
					}
					else {
						Test->TestTrue(TEXT("The last skin's hero can be tapped in the row of heroes"), MenuCheck::Tap(Menu, FName(*FString::Printf(TEXT("skinsof:%s"), *Wearer.ToString()))));
					}
					Enter(EPhase::ByTile);
				}
				return false;
			}
			if (StoreStageMesh(World) != StoreSkinMesh(Server->ShopSkins[Last].Id) || Elapsed() < 1.5) {
				return Elapsed() > 30.0 ? Fail(TEXT("A tap on a skin's tile did not bring it onto the stage in 30 s")) : false;
			}
			Test->TestEqual(TEXT("A tap on a skin's tile shows that skin"), MenuCheck::Line(Menu, TEXT("name")), Server->ShopSkins[Last].Name.ToUpper());
			Test->TestTrue(TEXT("It says which hero it is a skin of"), MenuCheck::Line(Menu, TEXT("count")).EndsWith(FString(TEXT("FOR ")) + USenniteHeroes::FindOrDefault(Wearer).Name));
			UE_LOG(LogStoreCheck, Display, TEXT("GAMECHECK skins: %s on the stage as %s; %s"), *Server->ShopSkins[Last].Id, *StoreStageMesh(World), *MenuCheck::Line(Menu, TEXT("count")));
			GameCheck::Shot(TEXT("Store"), TEXT("skins_last.png"));
			Enter(EPhase::DBucks);
			return false;
		}

		case EPhase::DBucks: {
			if (Menu->Page != ESenniteMenuPage::Swap) {
				if (Elapsed() > 1.0) {
					MenuCheck::Tap(Menu, TEXT("tab:swap"));
					Enter(EPhase::DBucks);
				}
				return false;
			}
			if (Server->SwapTokens.Num() < 2 || !MenuCheck::Has(Menu, TEXT("swap:from")) || Elapsed() < 1.0) {
				return Elapsed() > 60.0 ? Fail(TEXT("SWAP did not show what the publisher's vaults hold in 60 s")) : false;
			}
			if (Step == 0) {
				bool bWallet = false;
				Test->TestEqual(TEXT("The store names the section open"), MenuCheck::Line(Menu, TEXT("section")), FString(TEXT("SWAP")));
				Test->TestEqual(TEXT("It opens paying ERA"), MenuCheck::Line(Menu, TEXT("pay")), FString(TEXT("ERA")));
				Test->TestEqual(TEXT("for the game's D-bucks"), MenuCheck::Line(Menu, TEXT("get")), FString(TEXT("D-BUCKS")));
				Test->TestEqual(TEXT("With no wallet connected it says so"), MenuCheck::Line(Menu, TEXT("holds")), FString(TEXT("NO WALLET CONNECTED")));
				Test->TestTrue(TEXT("and offers to connect one"), MenuCheck::Has(Menu, TEXT("wallet"), &bWallet) && bWallet);
				Test->TestFalse(TEXT("No price is asked with no wallet"), MenuCheck::Has(Menu, TEXT("price")) || MenuCheck::Has(Menu, TEXT("trade")));
				Test->TestEqual(TEXT("No amount is typed yet"), MenuCheck::Line(Menu, TEXT("amount")), FString());
				Test->TestEqual(TEXT("The player's hero is back on the stage"), StoreStageMesh(World), USenniteHeroes::FindOrDefault(USenniteProfile::Get().PlayingHero(Menu)).Mesh.ToString());
				UE_LOG(LogStoreCheck, Display, TEXT("GAMECHECK swap: %d things to swap, %d vault(s); pay %s, get %s"), Server->SwapTokens.Num(), Server->SwapVaults.Num(), *MenuCheck::Line(Menu, TEXT("pay")), *MenuCheck::Line(Menu, TEXT("get")));
				// an amount, by the keypad
				MenuCheck::Tap(Menu, TEXT("num:2"));
			}
			else if (Step == 1) {
				Test->TestEqual(TEXT("A tap on the keypad types the amount"), MenuCheck::Line(Menu, TEXT("amount")), FString(TEXT("2")));
				MenuCheck::Tap(Menu, TEXT("num:5"));
			}
			else if (Step == 2) {
				Test->TestEqual(TEXT("and the next digit"), MenuCheck::Line(Menu, TEXT("amount")), FString(TEXT("25")));
				MenuCheck::Tap(Menu, TEXT("num:del"));
			}
			else if (Step == 3) {
				Test->TestEqual(TEXT("DEL takes the last digit off"), MenuCheck::Line(Menu, TEXT("amount")), FString(TEXT("2")));
				GameCheck::Shot(TEXT("Store"), TEXT("swap.png"));
			}
			else if (Step == 4) {
				MenuCheck::Tap(Menu, TEXT("swap:flip"));
			}
			else if (Step == 5) {
				Test->TestEqual(TEXT("SWITCH changes the two sides over"), MenuCheck::Line(Menu, TEXT("pay")), FString(TEXT("D-BUCKS")));
				Test->TestEqual(TEXT("and what was paid is got"), MenuCheck::Line(Menu, TEXT("get")), FString(TEXT("ERA")));
				bool bPoint = true;
				MenuCheck::Has(Menu, TEXT("num:."), &bPoint);
				Test->TestFalse(TEXT("D-bucks are whole: the point is not there to type"), bPoint);
				GameCheck::Shot(TEXT("Store"), TEXT("swap_switched.png"));
			}
			else {
				Enter(EPhase::Back);
				return false;
			}
			++Step;
			Enter(EPhase::DBucks);
			return false;
		}

		case EPhase::Back:
			if (Menu->Page != ESenniteMenuPage::Main) {
				if (Elapsed() > 0.3) {
					MenuCheck::Tap(Menu, TEXT("back"));
					Enter(EPhase::Back);
				}
				return false;
			}
			if (Elapsed() < 0.5) {
				return false;
			}
			Test->TestTrue(TEXT("BACK goes to the first screen"), MenuCheck::Has(Menu, TEXT("play")));
			Test->TestTrue(TEXT("BASE EDITOR can be tapped"), MenuCheck::Tap(Menu, TEXT("base")));
			Enter(EPhase::Base);
			return false;

		case EPhase::Base: {
			const ASenniteBaseEditor* Editor = ASenniteBaseEditor::Find(World);
			if (World == nullptr || UGameplayStatics::GetCurrentLevelName(World) != TEXT("TestRange") || Editor == nullptr || !Editor->bEditing || Editor->Parcel.IsEmpty() || Elapsed() < 3.0) {
				return Elapsed() > 120.0 ? Fail(TEXT("BASE EDITOR did not open the test range on the planning view of a parcel in 120 s")) : false;
			}
			Test->TestTrue(TEXT("BASE EDITOR opens the planning view"), ASenniteBaseEditor::IsPlanning(World));
			UE_LOG(LogStoreCheck, Display, TEXT("GAMECHECK base: planning %s on %s"), *Editor->Parcel, *UGameplayStatics::GetCurrentLevelName(World));
			GameCheck::Shot(TEXT("Store"), TEXT("base.png"));
			UE_LOG(LogStoreCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;
		}

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteStoreTest, "Sennite.Menu.Store",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteStoreTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStoreRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
