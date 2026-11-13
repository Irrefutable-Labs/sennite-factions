// Match prize check. Run in the game on the test range against a publisher's server on DSM: a player
// founds a faction. Outside the game, the publisher's match server opens a match between that
// faction and another, which locks the prize, and decides it for the player's faction. The game then
// says a prize is waiting, what it is, and where the wallet finds it. The player collects it in
// their own wallet, outside the game; the line goes, and the wallet proves it holds the materials.
//
// The wallet and the server are real ones, driven from outside the game: this check writes the
// connect code the game shows to Saved/BasePrize/connect-code.txt, Saved/BasePrize/founded.txt once
// the faction is founded, and Saved/BasePrize/shown.txt once the game has shown the prize.
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

DEFINE_LOG_CATEGORY_STATIC(LogBasePrizeCheck, Log, All);

namespace
{
	class FBasePrizeRun : public IAutomationLatentCommand
	{
	public:
		explicit FBasePrizeRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForGame, Opened, Connected, Founded, Waiting, Shown, Collected, Proved, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		void Tap(APlayerController* Controller, const FKey& InKey) const
		{
			GameCheck::Key(Controller, InKey, IE_Pressed);
			GameCheck::Key(Controller, InKey, IE_Released);
		}

		/** A line the editor's panel drew last frame, by its name; empty when it drew none. */
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
			UE_LOG(LogBasePrizeCheck, Display, TEXT("GAMECHECK panel %s: %s"), When, *FString::Join(Editor->LastDrawn, TEXT(" | ")));
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForGame;
		double PhaseStart = 0.0;
		int32 Step = 0;
		double LastTry = 0.0;
		int32 ReadWhenFounded = 0;
	};

	bool FBasePrizeRun::Update()
	{
		// a match is opened, decided and collected outside the game while this waits: generous, and bounded
		if (FPlatformTime::Seconds() - RealStart > 900.0) {
			return Fail(TEXT("The match prize check did not finish in 900 s"));
		}
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller != nullptr ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		USennitePublisher* Server = World != nullptr && World->GetGameInstance() != nullptr ? World->GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
		ASenniteBaseEditor* Editor = ASenniteBaseEditor::Find(World);
		const FString Folder = FPaths::ProjectSavedDir() / TEXT("BasePrize");
		if (Phase == EPhase::WaitForGame) {
			if (Server != nullptr && !Server->IsOn()) {
				return Fail(TEXT("No publisher's server is set: run the game with -PublisherUrl= (Tools/check_base_editor.sh does)"));
			}
			if (Hero == nullptr || Editor == nullptr || Server == nullptr || !Server->HasWorld() || Server->ConnectCode.IsEmpty() || World->GetTimeSeconds() < 2.0) {
				return false;
			}
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
				FFileHelper::SaveStringToFile(Server->ConnectCode, *(Folder / TEXT("connect-code.txt")));
				return false;
			}
			if (!Server->bConnected) {
				return Elapsed(World) > 180.0 ? Fail(TEXT("No wallet approved the connect code in 180 s")) : false;
			}
			Enter(EPhase::Connected, World);
			return false;

		case EPhase::Connected:
			if (Elapsed(World) < 1.0 || Base == nullptr || !Base->bKnown) {
				return false;
			}
			Tap(Controller, EKeys::N);
			Enter(EPhase::Founded, World);
			return false;

		case EPhase::Founded:
			if (!(Base != nullptr && Base->bAssigned && Base->bMember) || Server->bBusy) {
				return Elapsed(World) > 90.0 ? Fail(FString::Printf(TEXT("The faction was not founded in 90 s: %s"), *Server->Message)) : false;
			}
			if (Step == 0) {
				Step = 1;
				ReadWhenFounded = Server->PrizesRead;
				return false;
			}
			// the prizes are read from the server once the wallet is in a faction: with no match played there are none
			if (Server->PrizesRead < ReadWhenFounded + 2) {
				return Elapsed(World) > 60.0 ? Fail(TEXT("The game did not read the wallet's prizes in 60 s")) : false;
			}
			Test->TestEqual(TEXT("With no match played the wallet has no prize waiting"), Server->Prizes.Num(), 0);
			Test->TestTrue(TEXT("With no match played the panel shows no prize"), Drawn(Editor, TEXT("prize0")).IsEmpty());
			Log(Editor, TEXT("with the faction founded"));
			// the match is opened and decided, outside the game, once the faction is founded
			FFileHelper::SaveStringToFile(TEXT("founded"), *(Folder / TEXT("founded.txt")));
			Enter(EPhase::Waiting, World);
			return false;

		case EPhase::Waiting:
			// outside the game, the publisher's match server opens a match and decides it for this faction
			if (Server->Prizes.Num() < 3) {
				return Elapsed(World) > 600.0 ? Fail(TEXT("No prize was waiting for the wallet in 600 s")) : false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			{
				const FString Line = Drawn(Editor, TEXT("prize0"));
				Test->TestTrue(TEXT("The panel says a prize is waiting"), Line.StartsWith(TEXT("PRIZE WAITING")));
				Test->TestTrue(TEXT("It says what was won: the whole prize, to the faction's one member"), Line.Contains(TEXT("60 WOOD, 40 STONE, 25 STEEL")));
				Test->TestTrue(TEXT("It names the match"), Line.Contains(TEXT("won in match ") + Server->Prizes[0].MatchId));
				Test->TestTrue(TEXT("It says where the wallet finds it: the match's verdict cell"),
					!Server->Prizes[0].Cell.IsEmpty() && Line.EndsWith(TEXT("verdict cell ") + Server->Prizes[0].Cell));
				Test->TestTrue(TEXT("One line a match"), Drawn(Editor, TEXT("prize1")).IsEmpty());
			}
			Log(Editor, TEXT("with the match won"));
			GameCheck::Shot(TEXT("BasePrize"), TEXT("prize-waiting.png"));
			// the player collects it in their own wallet, outside the game, once the game has shown it
			FFileHelper::SaveStringToFile(TEXT("shown"), *(Folder / TEXT("shown.txt")));
			Enter(EPhase::Shown, World);
			return false;

		case EPhase::Shown:
			if (Server->Prizes.Num() > 0) {
				return Elapsed(World) > 600.0 ? Fail(TEXT("The prize was still waiting 600 s after the game showed it")) : false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestTrue(TEXT("Once it is collected the panel shows no prize"), Drawn(Editor, TEXT("prize0")).IsEmpty());
			Enter(EPhase::Collected, World);
			return false;

		case EPhase::Collected:
			// the wallet proves what it holds: the materials it won are its own now
			if (Server->bMaterialsKnown && !Server->ProofPending()
				&& Server->Materials.FindRef(TEXT("WOOD")) == 60 && Server->Materials.FindRef(TEXT("STONE")) == 40 && Server->Materials.FindRef(TEXT("STEEL")) == 25) {
				Enter(EPhase::Proved, World);
				return false;
			}
			if (Elapsed(World) > 300.0) {
				return Fail(FString::Printf(TEXT("The wallet did not prove it holds the prize in 300 s: %s"), *Server->Message));
			}
			if (!Server->ProofPending() && !Server->bBusy && World->GetTimeSeconds() - LastTry > 5.0) {
				LastTry = World->GetTimeSeconds();
				Tap(Controller, EKeys::M);
			}
			return false;

		case EPhase::Proved:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Test->TestEqual(TEXT("The panel shows the materials the wallet proved"), Drawn(Editor, TEXT("materials")), FString(TEXT("WOOD 60   STONE 40   STEEL 25")));
			Test->TestTrue(TEXT("And no prize waiting"), Drawn(Editor, TEXT("prize0")).IsEmpty());
			Log(Editor, TEXT("with the prize collected"));
			GameCheck::Shot(TEXT("BasePrize"), TEXT("collected.png"));
			UE_LOG(LogBasePrizeCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteBasePrizeTest, "Sennite.BasePrize.WonAndCollected",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteBasePrizeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBasePrizeRun(this));
	return true;
}

#endif
