// Weapon shop check. Run in the game on the Menu map with a publisher's server (-PublisherUrl=) and a
// wallet outside the game that approves the game's connect code (Server/test/game/weapons.test.ts is
// that wallet), by taps on the screen as a phone gives them:
// - STORE, WEAPONS: the first gun the shop sells, not owned; CONNECT YOUR WALLET hands the code over;
// - a new wallet holds too few D-bucks: GET D-BUCKS trades ERA for them;
// - BUY: the wallet pays, the gun is issued and given, and the wallet proves it owns it: the store
//   marks it bought, and the other gun stays for sale;
// - a hero pass made out to this game speaks for the gun, in the list a game server is given;
// - BACK, PLAY: on the test range the game shows its server a pass, the server asks the publisher's
//   server what it speaks for, and the gun is the player's there; she walks to a crate and opens it,
//   and the crate gives her the free guns and the gun she bought, and keeps the one she did not.
//
// Server/test/game/weapons.test.ts runs this and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MenuCheck.h"
#include "SenniteCharacter.h"
#include "SennitePlayerState.h"
#include "SennitePublisher.h"
#include "SenniteUi.h"
#include "WeaponActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
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
		enum class EPhase { WaitForMenu, Open, Connect, GetDBucks, TapBuy, Bought, Pass, Checked, Other, ToMenu, InPlay, OpenCrate, InHand, Done };

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
		GameCheck::FCrateRun Crate;
		/** The bit of the bought gun's weapon slot, and of every gun bought in the store. */
		int32 GunBit = 0;
		int32 BoughtBits = 0;
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
		if (Phase > EPhase::Open && Phase <= EPhase::Other && (Menu == nullptr || Menu->Page != ESenniteMenuPage::Weapons)) {
			return Fail(TEXT("The store's weapons closed"));
		}
		APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller != nullptr ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller != nullptr ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;

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
			const TSharedRef<int32> Answered = PassState;
			TWeakObjectPtr<USennitePublisher> Asker(Server);
			Server->AskHeroPass(Server->Audience, [Asker, SpeaksFor, Answered](const FString& Pass) {
				USennitePublisher* Still = Asker.Get();
				if (Still == nullptr || Pass.IsEmpty()) {
					*Answered = -1;
					return;
				}
				Still->CheckHeroPass(Pass, [SpeaksFor, Answered](bool bOk, const TArray<FString>& Heroes, const FString&, const FString&) {
					*SpeaksFor = Heroes;
					*Answered = bOk ? 1 : -1;
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
			{
				// what a game server makes of that list (ASenniteCharacter::BoughtGunsAmong, as ASennitePlayerState::ServerClaimGuns
				// sets GunsOwned from it): this gun is the player's from a crate, and no other bought gun is
				const int32 Theirs = ASenniteCharacter::BoughtGunsAmong(*PassSpeaksFor);
				Test->TestTrue(TEXT("From that list a game server makes the bought gun the player's from a crate, and no other bought gun"),
					Theirs != 0 && Theirs == ASenniteCharacter::BoughtGunsAmong({ Gun }));
			}
			UE_LOG(LogWeaponShopCheck, Display, TEXT("GAMECHECK the hero pass speaks for: %s"), *FString::Join(*PassSpeaksFor, TEXT(", ")));
			Enter(EPhase::Other);
			return false;

		case EPhase::Other: {
			// the other gun the shop sells is still for sale, and not the wallet's
			if (Server->ShopWeapons.Num() < 2) {
				Enter(EPhase::ToMenu);
				return false;
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
			Enter(EPhase::ToMenu);
			return false;
		}

		case EPhase::ToMenu:
			// out of the store and into play, by the first screen's own buttons
			if (Menu != nullptr && Menu->Page != ESenniteMenuPage::Main) {
				if (Elapsed() > 0.5) {
					MenuCheck::Tap(Menu, TEXT("back"));
					Enter(EPhase::ToMenu);
				}
				return false;
			}
			if (Menu != nullptr) {
				if (Elapsed() > 0.5) {
					Test->TestTrue(TEXT("PLAY can be tapped"), MenuCheck::Tap(Menu, TEXT("play")));
					Enter(EPhase::ToMenu);
					PhaseStart += 3.0;
				}
				return false;
			}
			Enter(EPhase::InPlay);
			return false;

		case EPhase::InPlay: {
			// on the range: the game shows its server a pass, and the server makes the bought gun the player's from what the publisher's server says
			GunBit = ASenniteCharacter::BoughtGunsAmong({ Gun });
			TArray<FString> Sold;
			for (const FSenniteHeroForSale& Each : Server->ShopWeapons) {
				Sold.Add(Each.Id);
			}
			BoughtBits = ASenniteCharacter::BoughtGunsAmong(Sold);
			if (Hero == nullptr || State == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround() || State->GunsOwned == 0 || Elapsed() < 3.0) {
				return Elapsed() > 180.0 ? Fail(FString::Printf(TEXT("In play the server did not make the bought gun the player's in 180 s (guns owned: %d)"), State != nullptr ? State->GunsOwned : -1)) : false;
			}
			Test->TestEqual(TEXT("PLAY opens the test range"), UGameplayStatics::GetCurrentLevelName(World), FString(TEXT("TestRange")));
			Test->TestTrue(TEXT("The gun bought is one of the game's guns"), GunBit != 0);
			Test->TestEqual(TEXT("In play the server holds the bought gun as the player's, and no other bought gun"), State->GunsOwned, GunBit);
			Test->TestFalse(TEXT("She has no gun before a crate is opened"), State->EquippedWeapons.Contains(1) || State->EquippedWeapons.Contains(2));
			UE_LOG(LogWeaponShopCheck, Display, TEXT("GAMECHECK in play: the server took the pass, guns owned %d"), State->GunsOwned);
			Enter(EPhase::OpenCrate);
			return false;
		}

		case EPhase::OpenCrate: {
			if (Hero == nullptr || State == nullptr) {
				return Fail(TEXT("The hero left the range"));
			}
			// D walks her to the right, to the crate; E opens it
			if (!Crate.Step(World, Controller, Hero, EKeys::D)) {
				return Elapsed() > 120.0 ? Fail(TEXT("The crate was not opened in 120 s")) : false;
			}
			if (!Crate.Failure.IsEmpty()) {
				return Fail(Crate.Failure);
			}
			const int32 Slot = FMath::FloorLog2(uint32(FMath::Max(1, GunBit)));
			Test->TestTrue(TEXT("The crate gives her the free guns"), State->EquippedWeapons.Contains(1) && State->EquippedWeapons.Contains(2));
			Test->TestTrue(TEXT("The crate gives her the gun she bought"), State->EquippedWeapons.Contains(Slot));
			Test->TestEqual(TEXT("with the rounds a crate gives for it"), State->EquippedWeaponsAmmunition[Slot], GetDefault<AWeaponActor>(Hero->WeaponClasses[Slot])->CrateRounds);
			for (int32 Other = 0; Other < Hero->WeaponClasses.Num(); ++Other) {
				if ((BoughtBits & (1 << Other)) != 0 && Other != Slot) {
					Test->TestTrue(FString::Printf(TEXT("The crate keeps the bought gun in slot %d, which she does not own, and its rounds"), Other),
						!State->EquippedWeapons.Contains(Other) && State->EquippedWeaponsAmmunition[Other] == 0);
				}
			}
			// the key of the bought gun's slot brings it to hand (slot 3 is on 6, slot 4 on 7)
			GameCheck::Key(Controller, Slot == 4 ? EKeys::Seven : EKeys::Six, IE_Pressed);
			GameCheck::Key(Controller, Slot == 4 ? EKeys::Seven : EKeys::Six, IE_Released);
			Enter(EPhase::InHand);
			return false;
		}

		case EPhase::InHand: {
			if (Hero == nullptr || State == nullptr) {
				return Fail(TEXT("The hero left the range"));
			}
			const int32 Slot = FMath::FloorLog2(uint32(FMath::Max(1, GunBit)));
			if (Elapsed() < 1.5 || Hero->CurrentWeapon == nullptr || Hero->CurrentWeapon->GetClass() != Hero->WeaponClasses[Slot]) {
				return Elapsed() > 30.0 ? Fail(FString::Printf(TEXT("The bought gun did not come to hand in 30 s (in hand: %s)"), *GetNameSafe(Hero->CurrentWeapon))) : false;
			}
			Test->TestEqual(TEXT("The gun in her hand is the one she bought"), Hero->CurrentWeapon->ShopId, Gun);
			UE_LOG(LogWeaponShopCheck, Display, TEXT("GAMECHECK on the range the crate gave %s: it is in her hand as %s, with %d rounds"), *Gun, *Hero->CurrentWeapon->DisplayName,
				State->EquippedWeaponsAmmunition[Slot]);
			GameCheck::Shot(TEXT("WeaponShop"), TEXT("in_hand.png"));
			UE_LOG(LogWeaponShopCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;
		}

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteWeaponShopTest, "Sennite.GunShop.BuyAndOwn",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteWeaponShopTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FWeaponShopRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
