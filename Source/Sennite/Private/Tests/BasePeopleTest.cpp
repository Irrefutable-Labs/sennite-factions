// Base people check. Run in the game on the test range against a publisher's server on DSM, with a
// faction another player founded: the game shows a connect code; once this player's wallet is
// connected and the other player has invited it, the parcel shows the invitation and J joins. P
// opens the faction's people. K votes to remove the other member, who in a faction of two is out
// at once; I invites them back from the players in no faction; X withdraws that invitation.
//
// Both wallets are real ones, driven from outside the game. This check writes the connect code the
// game shows to Saved/BasePeople/connect-code.txt, as a player would read it off the screen.
//
// Tools/check_base_editor.sh runs this with the accounts it needs and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "SenniteBaseEditor.h"
#include "SenniteCharacter.h"
#include "SennitePublisher.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogBasePeopleCheck, Log, All);

namespace
{
	class FBasePeopleRun : public IAutomationLatentCommand
	{
	public:
		explicit FBasePeopleRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForGame, Opened, Invited, Joined, People, Voted, InvitedBack, Withdrawn, Done };

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
			UE_LOG(LogBasePeopleCheck, Display, TEXT("GAMECHECK panel %s: %s"), When, *FString::Join(Editor->LastDrawn, TEXT(" | ")));
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForGame;
		double PhaseStart = 0.0;
		int32 Step = 0;
		double LastTab = 0.0;
		/** The other member's wallet, as the panel names it. */
		FString Other;
	};

	bool FBasePeopleRun::Update()
	{
		if (FPlatformTime::Seconds() - RealStart > 420.0) {
			return Fail(TEXT("The base people check did not finish in 420 s"));
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
			Tap(Controller, EKeys::B);
			Enter(EPhase::Opened, World);
			return false;
		}
		if (Hero == nullptr || Editor == nullptr || Server == nullptr) {
			return Fail(TEXT("The hero, the editor or the publisher's client went away during the check"));
		}
		const FSenniteBaseView* Base = Server->Bases.Find(Editor->Parcel);

		switch (Phase) {
		case EPhase::Opened:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				Test->TestTrue(TEXT("B opens the planning view"), Editor->bEditing);
				// as a player reads the code off the screen into their wallet
				FFileHelper::SaveStringToFile(Server->ConnectCode, *(FPaths::ProjectSavedDir() / TEXT("BasePeople") / TEXT("connect-code.txt")));
				return false;
			}
			if (!Server->bConnected) {
				return Elapsed(World) > 180.0 ? Fail(TEXT("No wallet approved the connect code in 180 s")) : false;
			}
			Enter(EPhase::Invited, World);
			return false;

		case EPhase::Invited:
			// Tab goes round the parcels until the one whose faction has invited this wallet is in view
			if (!(Base != nullptr && Base->bKnown && Base->bInvited)) {
				if (Elapsed(World) > 180.0) {
					return Fail(TEXT("No faction invited the wallet in 180 s"));
				}
				if (World->GetTimeSeconds() - LastTab > 2.0) {
					LastTab = World->GetTimeSeconds();
					Tap(Controller, EKeys::Tab);
				}
				return false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestEqual(TEXT("The panel shows the invitation and how to join"), Drawn(Editor, TEXT("faction")), FString(TEXT("YOU ARE INVITED   J: join")));
			Test->TestFalse(TEXT("Invited is not a member"), Base->bMember);
			Test->TestEqual(TEXT("The invited is told nothing of the faction's people"), Base->People.Num(), 0);
			Tap(Controller, EKeys::P);
			Log(Editor, TEXT("when invited"));
			GameCheck::Shot(TEXT("BasePeople"), TEXT("invited.png"));
			Tap(Controller, EKeys::J);
			Enter(EPhase::Joined, World);
			return false;

		case EPhase::Joined:
			if (!(Base != nullptr && Base->bMember && Base->Members == 2 && Base->People.Num() == 2) || Server->bBusy) {
				return Elapsed(World) > 90.0 ? Fail(FString::Printf(TEXT("The wallet did not join in 90 s: %s"), *Server->Message)) : false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestFalse(TEXT("P opens nothing for someone who is only invited"), Editor->bPeople);
			Test->TestTrue(TEXT("The panel shows the faction as the player's, with two members"), Drawn(Editor, TEXT("faction")).StartsWith(TEXT("YOUR FACTION   2 members")));
			Test->TestEqual(TEXT("The wallet's faction is the parcel's"), Server->FactionId, Base->FactionId);
			Tap(Controller, EKeys::P);
			Enter(EPhase::People, World);
			return false;

		case EPhase::People: {
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestTrue(TEXT("P opens the faction's people"), Editor->bPeople);
			Test->TestEqual(TEXT("The panel is on its people page"), Drawn(Editor, TEXT("page")), FString(TEXT("PEOPLE")));
			// the founder is listed first and chosen; this player is the second member
			const FSennitePerson& Founder = Base->People[0];
			const FSennitePerson& Me = Base->People[1];
			Test->TestTrue(TEXT("The other member is listed, not as you"), !Founder.bYou && Me.bYou);
			Other = Founder.Wallet.Left(8);
			Test->TestEqual(TEXT("The other member's row is the chosen one"), Drawn(Editor, TEXT("member0")), FString::Printf(TEXT("> %s   member"), *Other));
			Test->TestEqual(TEXT("This player's row says so"), Drawn(Editor, TEXT("member1")), FString::Printf(TEXT("  YOU  %s   member"), *Me.Wallet.Left(8)));
			Test->TestTrue(TEXT("The panel offers a vote on the other member"), Drawn(Editor, TEXT("keys")).StartsWith(TEXT("K: vote to remove")));
			Log(Editor, TEXT("on the people page"));
			GameCheck::Shot(TEXT("BasePeople"), TEXT("people.png"));
			Tap(Controller, EKeys::K);
			Enter(EPhase::Voted, World);
			return false;
		}

		case EPhase::Voted: {
			// in a faction of two the other member alone is the majority: the vote removes them at once
			const bool bOut = Base != nullptr && Base->Members == 1 && Base->People.Num() == 1 && !Server->bBusy;
			const bool bFree = Server->FreePlayers.ContainsByPredicate([this](const FSennitePerson& P) { return P.Wallet.Left(8) == Other; });
			if (!bOut || !bFree) {
				return Elapsed(World) > 90.0 ? Fail(FString::Printf(TEXT("The vote did not remove the member in 90 s: %s"), *Server->Message)) : false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				Tap(Controller, EKeys::S);
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestTrue(TEXT("The player is the faction's one member"), Base->People[0].bYou);
			Test->TestEqual(TEXT("The removed member is listed among the players in no faction, chosen"), Drawn(Editor, TEXT("free0")), FString::Printf(TEXT("> %s   in no faction"), *Other));
			Test->TestTrue(TEXT("The panel offers to invite them"), Drawn(Editor, TEXT("keys")).StartsWith(TEXT("I: invite")));
			Log(Editor, TEXT("after the vote"));
			Tap(Controller, EKeys::I);
			Enter(EPhase::InvitedBack, World);
			return false;
		}

		case EPhase::InvitedBack:
			if (!(Base != nullptr && Base->Invited.Num() == 1) || Server->bBusy) {
				return Elapsed(World) > 90.0 ? Fail(FString::Printf(TEXT("The invitation did not show in 90 s: %s"), *Server->Message)) : false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestEqual(TEXT("They are listed as invited, not as a member"), Drawn(Editor, TEXT("invited0")), FString::Printf(TEXT("> %s   invited"), *Other));
			Test->TestEqual(TEXT("The faction still has one member"), Base->Members, 1);
			Test->TestTrue(TEXT("The panel offers to withdraw the invitation"), Drawn(Editor, TEXT("keys")).StartsWith(TEXT("X: withdraw the invitation")));
			Log(Editor, TEXT("with the invitation standing"));
			GameCheck::Shot(TEXT("BasePeople"), TEXT("invited-back.png"));
			Tap(Controller, EKeys::X);
			Enter(EPhase::Withdrawn, World);
			return false;

		case EPhase::Withdrawn:
			if (!(Base != nullptr && Base->Invited.Num() == 0) || Server->bBusy) {
				return Elapsed(World) > 90.0 ? Fail(FString::Printf(TEXT("The invitation was not withdrawn in 90 s: %s"), *Server->Message)) : false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				Tap(Controller, EKeys::P);
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestFalse(TEXT("P goes back to the parcel"), Editor->bPeople);
			Test->TestTrue(TEXT("The faction is the player's alone"), Drawn(Editor, TEXT("faction")).StartsWith(TEXT("YOUR FACTION   1 member")));
			Log(Editor, TEXT("at the end"));
			Tap(Controller, EKeys::B);
			UE_LOG(LogBasePeopleCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteBasePeopleTest, "Sennite.BasePeople.JoinVoteAndInvite",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteBasePeopleTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBasePeopleRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
