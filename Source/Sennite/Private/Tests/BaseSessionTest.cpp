// Host killed check. Run in the game on the test range against a publisher's server on DSM: a player
// founds a faction and places a watchtower. Then the publisher's server is killed, outside the game.
// The game goes on showing the base it last read and says plainly that there is no live session; a
// placement tried meanwhile goes nowhere. A server is started again on the record the kill left,
// and the game shows the same base, read anew, and builds on it.
//
// The wallet and the server are real ones, driven from outside the game: this check writes the
// connect code the game shows to Saved/BaseSession/connect-code.txt, Saved/BaseSession/planned.txt
// once it shows the plan, and Saved/BaseSession/tried.txt once the player has tried to build with
// no session.
//
// Tools/check_base_editor.sh runs this with the accounts it needs and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "SenniteBaseEditor.h"
#include "SenniteCharacter.h"
#include "SennitePublisher.h"
#include "SenniteStructure.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogBaseSessionCheck, Log, All);

namespace
{
	class FBaseSessionRun : public IAutomationLatentCommand
	{
	public:
		explicit FBaseSessionRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForGame, Opened, Connected, Founded, Planned, Down, Tried, Back, Built, Done };

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
			UE_LOG(LogBaseSessionCheck, Display, TEXT("GAMECHECK panel %s: %s"), When, *FString::Join(Editor->LastDrawn, TEXT(" | ")));
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForGame;
		double PhaseStart = 0.0;
		int32 Step = 0;
		double LastTry = 0.0;
		FString Tower;
		int32 Height = 0;
		int32 ReadWhenBack = 0;
	};

	bool FBaseSessionRun::Update()
	{
		// a server is killed and another started outside the game while this waits: generous, and bounded
		if (FPlatformTime::Seconds() - RealStart > 900.0) {
			return Fail(TEXT("The host killed check did not finish in 900 s"));
		}
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller != nullptr ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		USennitePublisher* Server = World != nullptr && World->GetGameInstance() != nullptr ? World->GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
		ASenniteBaseEditor* Editor = ASenniteBaseEditor::Find(World);
		const FString Folder = FPaths::ProjectSavedDir() / TEXT("BaseSession");
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
			if (!(Base != nullptr && Base->bAssigned && Base->bMember)) {
				return Elapsed(World) > 90.0 ? Fail(FString::Printf(TEXT("The faction was not founded in 90 s: %s"), *Server->Message)) : false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				Tap(Controller, EKeys::W);
				Tap(Controller, EKeys::W);
				Tap(Controller, EKeys::D);
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			Tap(Controller, EKeys::F);
			Enter(EPhase::Planned, World);
			return false;

		case EPhase::Planned:
			if (Base == nullptr || Base->Structures.Num() == 0 || Editor->Drawn().Num() == 0 || Server->bBusy) {
				return Elapsed(World) > 90.0 ? Fail(FString::Printf(TEXT("The placement did not become a plan in 90 s: %s"), *Server->Message)) : false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			Tower = Base->Structures[0].InstanceId;
			Height = Base->Height;
			Test->TestTrue(TEXT("While the server answers the panel says nothing about the session"), Server->Reachable() && Drawn(Editor, TEXT("session")).IsEmpty());
			Log(Editor, TEXT("with the plan placed"));
			// the server is killed, outside the game, once the game has shown the plan
			FFileHelper::SaveStringToFile(TEXT("planned"), *(Folder / TEXT("planned.txt")));
			Enter(EPhase::Down, World);
			return false;

		case EPhase::Down:
			// outside the game, the publisher's server is killed
			if (Server->Reachable()) {
				return Elapsed(World) > 300.0 ? Fail(TEXT("The publisher's server was not killed in 300 s")) : false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 2.0) {
				return false;
			}
			if (Step == 1) {
				Step = 2;
				Test->TestTrue(TEXT("The panel says there is no live session"), Drawn(Editor, TEXT("session")).StartsWith(TEXT("There is no live session")));
				Test->TestTrue(TEXT("The base is still shown: the same plan"), Base != nullptr && Base->Structures.Num() == 1 && Base->Structures[0].InstanceId == Tower);
				Test->TestTrue(TEXT("The plan is still drawn on the parcel"), Editor->Drawn().FindRef(Tower) != nullptr);
				Log(Editor, TEXT("with the server killed"));
				GameCheck::Shot(TEXT("BaseSession"), TEXT("no-session.png"));
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			// the player tries to build all the same: three cells across, clear of the tower
			Tap(Controller, EKeys::D);
			Tap(Controller, EKeys::D);
			Tap(Controller, EKeys::D);
			Tap(Controller, EKeys::F);
			Enter(EPhase::Tried, World);
			return false;

		case EPhase::Tried:
			if (Step == 0) {
				if (Elapsed(World) < 1.5 || Server->bBusy) {
					return Elapsed(World) > 120.0 ? Fail(FString::Printf(TEXT("The placement tried with no session did not come back in 120 s: %s"), *Server->Message)) : false;
				}
				Step = 1;
				Test->TestFalse(TEXT("A placement tried with no session is not taken"), Server->bLastOk);
				Test->TestEqual(TEXT("The player is told the server cannot be reached"), Server->Message, FString(TEXT("the publisher's server cannot be reached")));
				Test->TestTrue(TEXT("Nothing was added to the base"), Base != nullptr && Base->Structures.Num() == 1 && Editor->Drawn().Num() == 1);
				Log(Editor, TEXT("after trying to build with no session"));
				// a server is started again, outside the game, once the player has tried
				FFileHelper::SaveStringToFile(TEXT("tried"), *(Folder / TEXT("tried.txt")));
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (!Server->Reachable()) {
				return Elapsed(World) > 300.0 ? Fail(TEXT("No server answered again in 300 s")) : false;
			}
			ReadWhenBack = Server->BasesRead;
			Enter(EPhase::Back, World);
			return false;

		case EPhase::Back:
			// the base is read anew from the server that took over: nothing shown from here on is the game's memory
			if (Server->BasesRead < ReadWhenBack + 2 * FMath::Max(1, Server->Parcels.Num()) || Base == nullptr || !Server->Reachable()) {
				return Elapsed(World) > 120.0 ? Fail(TEXT("The base was not read again in 120 s after a server answered")) : false;
			}
			if (Step == 0) {
				Step = 1;
				Test->TestTrue(TEXT("With a session again the panel's message is gone"), Drawn(Editor, TEXT("session")).IsEmpty());
				Test->TestTrue(TEXT("The same base, read anew: the same plan"), Base->Structures.Num() == 1 && Base->Structures[0].InstanceId == Tower);
				Test->TestEqual(TEXT("The same base, read anew: the same number of changes"), Base->Height, Height);
				Test->TestTrue(TEXT("The player is still a member, with the same wallet"), Base->bMember && Server->bConnected);
				Log(Editor, TEXT("with a server again"));
			}
			if (Base->Structures.Num() == 2 && !Server->bBusy) {
				Enter(EPhase::Built, World);
				return false;
			}
			if (Elapsed(World) > 300.0) {
				return Fail(FString::Printf(TEXT("The placement was not taken in 300 s after a server answered: %s"), *Server->Message));
			}
			if (!Server->bBusy && World->GetTimeSeconds() - LastTry > 5.0) {
				LastTry = World->GetTimeSeconds();
				Tap(Controller, EKeys::F);
			}
			return false;

		case EPhase::Built:
			if (Elapsed(World) < 1.5) {
				return false;
			}
			Test->TestTrue(TEXT("The first plan is still part of the base"), Base != nullptr && Base->Structures.Num() == 2
				&& Base->Structures.ContainsByPredicate([this](const FSenniteStructureView& One) { return One.InstanceId == Tower; }));
			Test->TestEqual(TEXT("Both plans are drawn on the parcel"), Editor->Drawn().Num(), 2);
			Log(Editor, TEXT("with the placement taken by the server that took over"));
			GameCheck::Shot(TEXT("BaseSession"), TEXT("session-again.png"));
			UE_LOG(LogBaseSessionCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteBaseSessionTest, "Sennite.BaseSession.HostKilledAndBack",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteBaseSessionTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBaseSessionRun(this));
	return true;
}

#endif
