// Base editor check. Run in the game on the test range against a publisher's server on DSM: B opens
// the planning view and the game shows a connect code; once a DSM wallet has approved it, N founds
// a faction on the parcel, W A S D move the cursor, F places a watchtower as a plan that needs
// resources, M has the wallet prove what it holds, and C commits the steel: the publisher's server
// then says the tower stands, and it is solid in play. The hero does not move while planning.
//
// The wallet is a real one, driven from outside the game: this check writes the connect code the
// game shows to Saved/BaseEditor/connect-code.txt for it, as a player would read it off the screen.
//
// Tools/check_base_editor.sh runs this with the accounts it needs and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "SenniteBaseEditor.h"
#include "SenniteCharacter.h"
#include "SenniteHUD.h"
#include "SennitePublisher.h"
#include "SenniteStructure.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogBaseEditorCheck, Log, All);

namespace
{
	class FBaseEditorRun : public IAutomationLatentCommand
	{
	public:
		explicit FBaseEditorRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForGame, Opened, Connected, Founded, Cursor, Planned, Proven, Built, Play, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		/** Press a key as the player would: on the keyboard, or played by touch, by tapping the button for it. */
		void Tap(APlayerController* Controller, const FKey& InKey) const
		{
			if (!GameCheck::Press(Controller, InKey)) {
				Test->AddError(FString::Printf(TEXT("No control on the touch screen presses %s now"), *InKey.ToString()));
			}
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

		void Shows(const ASenniteBaseEditor* Editor, const TCHAR* Name, const TCHAR* Text, const TCHAR* What)
		{
			Test->TestEqual(FString::Printf(TEXT("The panel shows %s"), What), Drawn(Editor, Name), FString(Text));
		}

		void Log(const ASenniteBaseEditor* Editor, const TCHAR* When)
		{
			UE_LOG(LogBaseEditorCheck, Display, TEXT("GAMECHECK panel %s: %s"), When, *FString::Join(Editor->LastDrawn, TEXT(" | ")));
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForGame;
		double PhaseStart = 0.0;
		int32 Step = 0;
		FVector HeroStood = FVector::ZeroVector;
		FIntPoint CursorStart = FIntPoint::ZeroValue;
		double LastProof = 0.0;
	};

	bool FBaseEditorRun::Update()
	{
		// the wallet answers on its own time, over the storage nodes: generous, and bounded
		if (FPlatformTime::Seconds() - RealStart > 420.0) {
			return Fail(TEXT("The base editor check did not finish in 420 s"));
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
			Test->TestTrue(TEXT("The publisher's catalogue has prefabs"), Server->Prefabs.Num() > 0);
			Test->TestTrue(TEXT("The publisher's registry has parcels"), Server->Parcels.Num() > 0);
			Test->TestFalse(TEXT("The planning view is closed until asked for"), Editor->bEditing);
			HeroStood = Hero->GetActorLocation();
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
				Test->TestTrue(TEXT("The view is from above the parcel, not the hero's"), Controller->GetViewTarget() != Hero);
				Test->TestTrue(TEXT("The panel draws (needs a renderer: run without -nullrhi)"), Editor->LastDrawn.Num() > 0);
				Shows(Editor, TEXT("wallet"), TEXT("WALLET NOT CONNECTED"), TEXT("that no wallet is connected"));
				Test->TestTrue(TEXT("The panel shows the connect code"), Server->ConnectCode.StartsWith(Drawn(Editor, TEXT("code0"))) && !Drawn(Editor, TEXT("code0")).IsEmpty());
				Log(Editor, TEXT("before a wallet connects"));
				GameCheck::Shot(TEXT("BaseEditor"), TEXT("connect.png"));
				// as a player reads the code off the screen into their wallet
				FFileHelper::SaveStringToFile(Server->ConnectCode, *(FPaths::ProjectSavedDir() / TEXT("BaseEditor") / TEXT("connect-code.txt")));
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
			Test->TestTrue(TEXT("The panel names the connected wallet"), Drawn(Editor, TEXT("wallet")) == TEXT("WALLET ") + Server->Wallet.Left(8) && !Server->Wallet.IsEmpty());
			Shows(Editor, TEXT("faction"), TEXT("UNASSIGNED PARCEL   N: found a faction here"), TEXT("an unassigned parcel"));
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
			Test->TestEqual(TEXT("The wallet's faction is the parcel's"), Server->FactionId, Base->FactionId);
			Shows(Editor, TEXT("faction"), TEXT("YOUR FACTION   1 member   0 changes"), TEXT("the player's own faction"));
			CursorStart = Editor->Cursor;
			if (GameCheck::Touching(Controller)) {
				// played by touch, the cursor goes to the cell tapped on the map: two cells up the map and one across
				const FIntPoint Target = CursorStart + FIntPoint(2 * Server->Grid, Server->Grid);
				const FVector2D Middle(Target.X + Server->Grid / 2, Target.Y + Server->Grid / 2);
				FHitResult Ground;
				World->LineTraceSingleByObjectType(Ground, FVector(Middle, 20000.0), FVector(Middle, -20000.0), FCollisionObjectQueryParams(ECC_WorldStatic));
				FVector2D OnScreen;
				if (!Controller->ProjectWorldLocationToScreen(FVector(Middle, Ground.bBlockingHit ? Ground.ImpactPoint.Z : 0.0), OnScreen)) {
					return Fail(TEXT("The cell to tap is not on the screen"));
				}
				GameCheck::Tap(OnScreen);
			}
			else {
				Tap(Controller, EKeys::W);
				Tap(Controller, EKeys::W);
				Tap(Controller, EKeys::D);
			}
			Enter(EPhase::Cursor, World);
			return false;

		case EPhase::Cursor: {
			if (Elapsed(World) < 0.5) {
				return false;
			}
			const FSennitePrefab* Tower = Editor->Chosen();
			Test->TestTrue(TEXT("The first prefab of the catalogue is chosen"), Tower != nullptr && Tower->Id == Server->Prefabs[0].Id);
			Test->TestTrue(GameCheck::Touching(Controller) ? TEXT("A tap on the map moves the cursor to the cell tapped, two up the map and one across") : TEXT("W W D moves the cursor two cells up the map and one across"), Editor->Cursor == CursorStart + FIntPoint(2 * Server->Grid, Server->Grid));
			Test->TestTrue(TEXT("The hero stands where she was while the keys plan"), Hero->GetActorLocation().Equals(HeroStood, 5.0));
			Test->TestEqual(TEXT("Nothing is drawn on the parcel before anything is placed"), Editor->Drawn().Num(), 0);
			Log(Editor, TEXT("with the cursor placed"));
			GameCheck::Shot(TEXT("BaseEditor"), TEXT("cursor.png"));
			Tap(Controller, EKeys::F);
			Enter(EPhase::Planned, World);
			return false;
		}

		case EPhase::Planned: {
			if (Base == nullptr || Base->Structures.Num() == 0 || Editor->Drawn().Num() == 0) {
				return Elapsed(World) > 90.0 ? Fail(FString::Printf(TEXT("The placement did not become a plan in 90 s: %s"), *Server->Message)) : false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			const FSenniteStructureView& Plan = Base->Structures[0];
			const FSennitePrefab* Tower = Server->FindPrefab(Plan.PrefabId);
			Test->TestEqual(TEXT("One structure is planned"), Base->Structures.Num(), 1);
			Test->TestTrue(TEXT("The plan is the chosen prefab"), Tower != nullptr && Editor->Chosen() != nullptr && Tower->Id == Editor->Chosen()->Id);
			if (Tower != nullptr) {
				Test->TestTrue(TEXT("The plan's near corner is the cursor"), FIntPoint(Plan.X - Tower->Width / 2, Plan.Y - Tower->Depth / 2) == Editor->Cursor);
			}
			Test->TestFalse(TEXT("A plan with nothing committed does not stand"), Plan.bStanding);
			Test->TestTrue(TEXT("The cursor is on the plan"), Editor->Selected() != nullptr && Editor->Selected()->InstanceId == Plan.InstanceId);
			Shows(Editor, TEXT("status"), TEXT("NEEDS RESOURCES"), TEXT("that the plan needs resources"));
			Test->TestTrue(TEXT("The panel names the plan as a plan"), Drawn(Editor, TEXT("structure")).EndsWith(TEXT("PLAN")));
			Test->TestEqual(TEXT("The panel shows the whole recipe unmet"), Drawn(Editor, TEXT("need0")),
				Tower != nullptr && Tower->Recipe.Num() > 0 ? FString::Printf(TEXT("%s 0 of %d   needs %d"), *Tower->Recipe[0].Key, Tower->Recipe[0].Value, Tower->Recipe[0].Value) : FString());
			const ASenniteStructure* Actor = Editor->Drawn().FindRef(Plan.InstanceId);
			Test->TestTrue(TEXT("The plan is drawn on the parcel"), Actor != nullptr && Actor->Panels.Num() > 0);
			if (Actor != nullptr && Actor->Panels.Num() > 0) {
				Test->TestFalse(TEXT("The plan is drawn as a plan, not as built"), Actor->bStanding);
				Test->TestTrue(TEXT("A plan stops nothing"), Actor->Panels[0]->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
			}
			Log(Editor, TEXT("with the plan placed"));
			GameCheck::Shot(TEXT("BaseEditor"), TEXT("plan.png"));
			Enter(EPhase::Proven, World);
			return false;
		}

		case EPhase::Proven: {
			// the wallet's steel arrives on its own time: M asks the wallet to prove what it holds until the steel shows
			const FSenniteStructureView* Plan = Editor->Selected();
			const int32 Needs = Plan != nullptr && Plan->Needs.Num() > 0 ? Plan->Needs[0].Shortfall : 0;
			const FString Resource = Plan != nullptr && Plan->Needs.Num() > 0 ? Plan->Needs[0].Resource : FString();
			if (Server->bMaterialsKnown && Needs > 0 && Server->Materials.FindRef(Resource) >= Needs) {
				if (Server->bBusy || Server->ProofPending()) {
					return false;
				}
				Test->TestTrue(TEXT("The panel shows what the wallet proved it holds"), Drawn(Editor, TEXT("materials")).Contains(FString::Printf(TEXT("%s %d"), *Resource, Server->Materials.FindRef(Resource))));
				Shows(Editor, TEXT("commit"), *FString::Printf(TEXT("C: commit %d %s   (1: resource, Z: amount)"), Needs, *Resource), TEXT("the commit button for all it needs"));
				Log(Editor, TEXT("with the wallet's materials proven"));
				Tap(Controller, EKeys::C);
				Enter(EPhase::Built, World);
				return false;
			}
			if (Elapsed(World) > 180.0) {
				return Fail(FString::Printf(TEXT("The wallet did not prove it holds the steel in 180 s: %s"), *Server->Message));
			}
			if (!Server->bBusy && !Server->ProofPending() && World->GetTimeSeconds() - LastProof > 3.0) {
				LastProof = World->GetTimeSeconds();
				Tap(Controller, EKeys::M);
			}
			return false;
		}

		case EPhase::Built: {
			const FSenniteStructureView* Tower = Editor->Selected();
			// the commitment has settled, and so has the proof the game asks for after it
			if (Tower == nullptr || !Tower->bStanding || Server->bBusy || Server->ProofPending()) {
				return Elapsed(World) > 120.0 ? Fail(FString::Printf(TEXT("The funded plan was not built in 120 s: %s"), *Server->Message)) : false;
			}
			if (Elapsed(World) < 1.5) {
				return false;
			}
			if (Step == 1) {
				if (Elapsed(World) < 2.5) {
					return false;
				}
				Tap(Controller, EKeys::B);
				Enter(EPhase::Play, World);
				return false;
			}
			Shows(Editor, TEXT("status"), TEXT("FULLY FUNDED"), TEXT("that the structure is fully funded"));
			Test->TestTrue(TEXT("The panel names it as standing"), Drawn(Editor, TEXT("structure")).EndsWith(TEXT("STANDING")));
			Test->TestTrue(TEXT("The panel shows the player's own commitment"), Drawn(Editor, TEXT("committed0")).StartsWith(TEXT("you committed ")));
			Test->TestEqual(TEXT("What the wallet proves it holds went down by what it committed"), Server->Materials.FindRef(Tower->Needs[0].Resource), 0);
			const ASenniteStructure* Actor = Editor->Drawn().FindRef(Tower->InstanceId);
			Test->TestTrue(TEXT("The structure is drawn as built"), Actor != nullptr && Actor->bStanding && Actor->Panels.Num() > 0);
			if (Step == 0) {
				Step = 1;
				Log(Editor, TEXT("with the tower built"));
				GameCheck::Shot(TEXT("BaseEditor"), TEXT("built.png"));
			}
			return false;
		}

		case EPhase::Play: {
			if (Elapsed(World) < 1.5) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				// for the picture only: the hero's camera is turned to where the structure stands, some 80 m off
				if (Base != nullptr && Base->Structures.Num() > 0) {
					const FVector To(Base->Structures[0].X, Base->Structures[0].Y, 480.0);
					FRotator Look = (To - Hero->GetActorLocation()).Rotation();
					Look.Pitch = -4.0;
					Controller->SetControlRotation(Look);
				}
				return false;
			}
			if (Elapsed(World) < 2.5) {
				return false;
			}
			Test->TestFalse(TEXT("B closes the planning view"), Editor->bEditing);
			Test->TestTrue(TEXT("The view is the hero's again"), Controller->GetViewTarget() == Hero);
			Test->TestEqual(TEXT("The panel is gone in play"), Editor->LastDrawn.Num(), 0);
			Test->TestTrue(TEXT("The hero stood where she was through all of it"), Hero->GetActorLocation().Equals(HeroStood, 5.0));
			Test->TestEqual(TEXT("The built structure is still drawn in play"), Editor->Drawn().Num(), 1);
			if (Base != nullptr && Base->Structures.Num() > 0) {
				// in play it is solid: something falling on it from above lands on its roof
				const FSenniteStructureView& Tower = Base->Structures[0];
				FHitResult Hit;
				const bool bHit = World->LineTraceSingleByChannel(Hit, FVector(Tower.X, Tower.Y, 5000.0), FVector(Tower.X, Tower.Y, -100.0), ECC_Visibility);
				Test->TestTrue(TEXT("The built structure is solid in play"), bHit && Cast<ASenniteStructure>(Hit.GetActor()) != nullptr && Hit.ImpactPoint.Z > 400.0);
			}
			GameCheck::Shot(TEXT("BaseEditor"), TEXT("play.png"));
			UE_LOG(LogBaseEditorCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;
		}

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteBaseEditorTest, "Sennite.BaseEditor.PlanFundAndBuild",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteBaseEditorTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBaseEditorRun(this));
	return true;
}

#endif
