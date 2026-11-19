// Building paused check. Run in the game on the test range against a publisher's server on DSM: a
// player founds a faction and places a watchtower. Then the storage seat that leads the faction's
// next change is taken offline, outside the game. The base stays on screen as it was, the panel
// says plainly that building is paused, and a placement tried meanwhile is not taken and changes
// nothing. When the seat returns the panel's message goes, and the same placement is taken.
//
// The wallet and the storage seats are real ones, driven from outside the game: this check writes
// the connect code the game shows to Saved/BasePause/connect-code.txt, waits for
// Saved/BasePause/seat-offline.txt (the seat is offline) before it looks for the message, and
// writes Saved/BasePause/tried.txt once the player has tried to build while paused.
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

DEFINE_LOG_CATEGORY_STATIC(LogBasePauseCheck, Log, All);

namespace
{
	class FBasePauseRun : public IAutomationLatentCommand
	{
	public:
		explicit FBasePauseRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForGame, Opened, Connected, Founded, Planned, Paused, Tried, Resumed, Built, Done };

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
			UE_LOG(LogBasePauseCheck, Display, TEXT("GAMECHECK panel %s: %s"), When, *FString::Join(Editor->LastDrawn, TEXT(" | ")));
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForGame;
		double PhaseStart = 0.0;
		int32 Step = 0;
		double LastTry = 0.0;
		FString Tower;
		FString Said;
	};

	bool FBasePauseRun::Update()
	{
		// a seat is found, stopped and started again outside the game while this waits: generous, and bounded
		if (FPlatformTime::Seconds() - RealStart > 900.0) {
			return Fail(TEXT("The building paused check did not finish in 900 s"));
		}
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller != nullptr ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		USennitePublisher* Server = World != nullptr && World->GetGameInstance() != nullptr ? World->GetGameInstance()->GetSubsystem<USennitePublisher>() : nullptr;
		ASenniteBaseEditor* Editor = ASenniteBaseEditor::Find(World);
		const FString Folder = FPaths::ProjectSavedDir() / TEXT("BasePause");
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
			Test->TestTrue(TEXT("While every seat serves the panel says nothing about building"), Base->Building.IsEmpty() && Drawn(Editor, TEXT("building")).IsEmpty());
			Log(Editor, TEXT("with the plan placed"));
			Enter(EPhase::Paused, World);
			return false;

		case EPhase::Paused:
			// outside the game, the seat that leads the faction's next change is found and taken offline. Finding it
			// takes each seat offline for a moment, so the player waits until the seat is said to be offline for good
			if (!FPaths::FileExists(Folder / TEXT("seat-offline.txt")) || Base == nullptr || Base->Building.IsEmpty()) {
				return Elapsed(World) > 420.0 ? Fail(TEXT("The game was not told that building is paused in 420 s")) : false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Step == 1) {
				Step = 2;
				Said = Base->Building;
				Test->TestTrue(TEXT("The message says building is paused"), Said.StartsWith(TEXT("Building is paused")));
				Test->TestEqual(TEXT("The panel shows the message"), Drawn(Editor, TEXT("building")), Said);
				Test->TestTrue(TEXT("The base is still shown: the same plan, in the same place"), Base->Structures.Num() == 1 && Base->Structures[0].InstanceId == Tower);
				Test->TestTrue(TEXT("The plan is still drawn on the parcel"), Editor->Drawn().FindRef(Tower) != nullptr);
				Log(Editor, TEXT("with the leader seat offline"));
				GameCheck::Shot(TEXT("BasePause"), TEXT("paused.png"));
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
			if (Elapsed(World) < 1.5 || Server->bBusy) {
				return Elapsed(World) > 180.0 ? Fail(FString::Printf(TEXT("The placement tried while paused did not settle in 180 s: %s"), *Server->Message)) : false;
			}
			if (Step == 0) {
				Step = 1;
				Test->TestFalse(TEXT("A placement tried while building is paused is not taken"), Server->bLastOk);
				Test->TestEqual(TEXT("The player is told why, in the same words"), Server->Message, Said);
				Test->TestTrue(TEXT("Nothing was added to the base"), Base != nullptr && Base->Structures.Num() == 1 && Editor->Drawn().Num() == 1);
				Log(Editor, TEXT("after trying to build while paused"));
				GameCheck::Shot(TEXT("BasePause"), TEXT("tried.png"));
				// the seat is brought back, outside the game, once the player has tried
				FFileHelper::SaveStringToFile(TEXT("tried"), *(Folder / TEXT("tried.txt")));
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Base == nullptr || !Base->Building.IsEmpty()) {
				return Elapsed(World) > 300.0 ? Fail(TEXT("The game was not told that building is open again in 300 s")) : false;
			}
			Enter(EPhase::Resumed, World);
			return false;

		case EPhase::Resumed:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				Test->TestTrue(TEXT("With the seat back the panel's message is gone"), Drawn(Editor, TEXT("building")).IsEmpty());
			}
			if (Base != nullptr && Base->Structures.Num() == 2 && !Server->bBusy) {
				Enter(EPhase::Built, World);
				return false;
			}
			if (Elapsed(World) > 300.0) {
				return Fail(FString::Printf(TEXT("The same placement was not taken in 300 s after the seat returned: %s"), *Server->Message));
			}
			// the same placement, asked again; a seat just back may need a moment before a change is final
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
			Test->TestTrue(TEXT("The panel says nothing about building"), Drawn(Editor, TEXT("building")).IsEmpty());
			Log(Editor, TEXT("with the seat back and the placement taken"));
			GameCheck::Shot(TEXT("BasePause"), TEXT("resumed.png"));
			UE_LOG(LogBasePauseCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteBasePauseTest, "Sennite.BasePause.PauseAndResume",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteBasePauseTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBasePauseRun(this));
	return true;
}

#endif
