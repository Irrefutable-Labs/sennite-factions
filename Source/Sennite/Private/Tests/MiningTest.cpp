// Mining check (SenniteMining.h; owner, 2026-11-22). Run in the game on an arena of two bases, which
// stands deposits of wood, stone and steel on each base's side:
// - the deposits hold between them what DefaultGame.ini says an arena with no publisher holds;
// - with the droid not in hand, the fire button sends none;
// - the HoldDroid key takes the droid in hand, in a weapon's place; with no deposit aimed at, the fire
//   button then takes no droid out;
// - aimed at a wood deposit, the fire button takes the player's droid out and sends it there; the droid
//   goes to it and takes wood, a unit at a time: the deposit holds that much less, the player has that
//   much more to build with, and it is counted as mined;
// - taking the hero's own weapon in hand puts the droid away, and the fire button then sends it nowhere;
// - in hand again and aimed at a stone deposit, the fire button sends the same droid there, and it mines
//   stone;
// - the droid goes on mining with its player far away: once it is out, it is out;
// - a deposit is drawn smaller as it is used up, and with its last unit it is destroyed: the player has
//   that unit, and the droid has nothing to mine until it is sent to another;
// - its own player's shots do not count on it; another player's shoot it down, and then the player has
//   no droid for the rest of the game: it is not taken in hand, and none is sent.
// Each shot is landed as a bullet's is (AProjectileActor::Land).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "ProjectileActor.h"
#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "SenniteHUD.h"
#include "SenniteMining.h"
#include "SennitePlayerState.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogMiningCheck, Log, All);

namespace
{
	class FMiningRun : public IAutomationLatentCommand
	{
	public:
		explicit FMiningRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, NotInHand, NoAim, SendToWood, MineWood, PutAway, SendToStone, MineStone, Away, UsedUp, Shots, Down, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		void Tap(APlayerController* Controller, const FKey& InKey) const
		{
			GameCheck::Key(Controller, InKey, IE_Pressed);
			GameCheck::Key(Controller, InKey, IE_Released);
		}
		/** The deposit of a material nearest a place. */
		static ASenniteDeposit* Nearest(UWorld* World, int32 Material, const FVector& From)
		{
			ASenniteDeposit* Found = nullptr;
			double Best = TNumericLimits<double>::Max();
			for (TActorIterator<ASenniteDeposit> It(World); It; ++It) {
				const double Far = FVector::Dist(It->GetActorLocation(), From);
				if (It->Material == Material && Far < Best) {
					Best = Far;
					Found = *It;
				}
			}
			return Found;
		}
		/** Stand the hero a little way off a deposit, on the side its base is, looking at its middle. */
		static void Face(APlayerController* Controller, ASenniteCharacter* Hero, const ASenniteDeposit* Deposit)
		{
			const FVector Back = Deposit->GetActorRotation().Vector().GetSafeNormal2D();
			Hero->SetActorLocation(Deposit->GetActorLocation() + Back * 1500.0 + FVector(0.0, 0.0, 400.0));
			Hero->GetCharacterMovement()->StopMovementImmediately();
			const FVector Middle = Deposit->GetActorLocation() + FVector(0.0, 0.0, Deposit->Height() * 0.5);
			Controller->SetControlRotation((Middle - Hero->GetActorLocation()).Rotation());
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		TWeakObjectPtr<ASenniteDeposit> Wood;
		TWeakObjectPtr<ASenniteDeposit> Stone;
		int32 WoodStart = 0;
		int32 StoneMark = 0;
		/** How tall the stone deposit stood when whole, how many deposits stood, and that the stone one was used up. */
		double StoneTall = 0.0;
		int32 Deposits = 0;
		bool bStoneUsedUp = false;
	};

	bool FMiningRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		ASenniteHUD* Hud = Controller ? Cast<ASenniteHUD>(Controller->GetHUD()) : nullptr;

		if (Phase == EPhase::WaitForPawn) {
			TActorIterator<ASenniteArena> Arena(World);
			if (Hero == nullptr || State == nullptr || Hud == nullptr || !Arena || !Arena->IsBuilt() || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 120.0 ? Fail(TEXT("No hero standing on a built arena after 120 s")) : false;
			}
			const ASenniteDeposit* Rules = GetDefault<ASenniteDeposit>();
			const int32 Bases = Arena->GetPlan().Bases.Num();
			// a deposit of each material on each base's side, holding between them what the game's settings say
			int32 Stood[ASenniteDeposit::Kinds] = { 0, 0, 0 };
			int32 Hold[ASenniteDeposit::Kinds] = { 0, 0, 0 };
			for (TActorIterator<ASenniteDeposit> It(World); It; ++It) {
				Test->TestTrue(TEXT("A deposit is of one of the three materials"), It->Material >= 0 && It->Material < ASenniteDeposit::Kinds);
				Test->TestTrue(TEXT("A deposit wears its model and stands on the ground"), It->Height() > 50.0f);
				Test->TestEqual(TEXT("A deposit holds what it was stood with"), It->Left, It->Held);
				if (It->Material >= 0 && It->Material < ASenniteDeposit::Kinds) {
					Stood[It->Material] += 1;
					Hold[It->Material] += It->Left;
				}
			}
			for (int32 Kind = 0; Kind < ASenniteDeposit::Kinds; ++Kind) {
				Test->TestEqual(FString::Printf(TEXT("%s has a deposit on each base's side"), ASenniteDeposit::NameOf(Kind)), Stood[Kind], Bases * Rules->PerBase);
				Test->TestEqual(FString::Printf(TEXT("The %s deposits hold what the game's settings say"), ASenniteDeposit::NameOf(Kind)), Hold[Kind],
					Rules->Holds.IsValidIndex(Kind) ? Rules->Holds[Kind] : -1);
			}
			UE_LOG(LogMiningCheck, Display, TEXT("GAMECHECK deposits: %d WOOD in %d, %d STONE in %d, %d STEEL in %d"), Hold[0], Stood[0], Hold[1], Stood[1], Hold[2], Stood[2]);
			Wood = Nearest(World, 0, Hero->GetActorLocation());
			Stone = Nearest(World, 1, Hero->GetActorLocation());
			if (!Wood.IsValid() || !Stone.IsValid()) {
				return Fail(TEXT("The arena has no wood or no stone deposit"));
			}
			WoodStart = State->MaterialCounts[0];
			StoneTall = Stone->GetComponentsBoundingBox().GetSize().Z;
			Deposits = Stood[0] + Stood[1] + Stood[2];
			Test->TestFalse(TEXT("A player starts with the droid not in hand"), State->bDroidInHand);
			Test->TestEqual(TEXT("A player starts with no droid out"), ASenniteMiningDroid::Status(State), FString(TEXT("DROID READY")));
			Test->TestTrue(TEXT("A player starts having mined nothing"), State->Mined.Num() == ASenniteDeposit::Kinds && State->Mined[0] + State->Mined[1] + State->Mined[2] == 0);
			Face(Controller, Hero, Wood.Get());
			Enter(EPhase::NotInHand, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr || Hud == nullptr || !Wood.IsValid() || (!Stone.IsValid() && !bStoneUsedUp && Phase != EPhase::UsedUp)) {
			return Fail(TEXT("The hero, the HUD or a deposit went away during the check"));
		}
		ASenniteMiningDroid* Droid = State->Droid;

		switch (Phase) {
		case EPhase::NotInHand:
			// aimed at a wood deposit, with the hero's own weapon in hand
			if (Step == 0 && Elapsed(World) > 1.0) {
				Step = 1;
				Tap(Controller, EKeys::LeftMouseButton);
			}
			if (Elapsed(World) < 2.0) {
				return false;
			}
			Test->TestTrue(TEXT("With the droid not in hand, the fire button sends none"), Droid == nullptr);
			Tap(Controller, EKeys::T);
			// looking straight up, at no deposit
			Controller->SetControlRotation(FRotator(89.0, Hero->GetActorRotation().Yaw, 0.0));
			Enter(EPhase::NoAim, World);
			return false;

		case EPhase::NoAim:
			if (Step == 0 && Elapsed(World) > 0.5) {
				Step = 1;
				Test->TestTrue(TEXT("The HoldDroid key takes the droid in hand"), State->bDroidInHand);
				Tap(Controller, EKeys::LeftMouseButton);
			}
			if (Elapsed(World) < 1.5) {
				return false;
			}
			Test->TestTrue(TEXT("With no deposit aimed at, no droid is taken out"), Droid == nullptr);
			Face(Controller, Hero, Wood.Get());
			Enter(EPhase::SendToWood, World);
			return false;

		case EPhase::SendToWood:
			if (Step == 0 && Elapsed(World) > 1.0) {
				Step = 1;
				Tap(Controller, EKeys::LeftMouseButton);
			}
			if (Elapsed(World) < 2.0) {
				return false;
			}
			if (Droid == nullptr) {
				return Fail(TEXT("With the droid in hand and a wood deposit aimed at, the fire button took no droid out"));
			}
			Test->TestTrue(TEXT("The droid is sent to the deposit aimed at"), Droid->Target == Wood.Get());
			Test->TestTrue(TEXT("The droid is the player's"), Droid->Miner == State);
			Test->TestTrue(TEXT("The droid wears its model"), Droid->GetComponentsBoundingBox().GetSize().GetMax() > 60.0);
			Test->TestEqual(TEXT("The droid starts whole"), Droid->Health, GetDefault<ASenniteMiningDroid>()->StartHealth);
			Enter(EPhase::MineWood, World);
			return false;

		case EPhase::MineWood:
			// the droid at its deposit's side, seen from a step aside of where the player sent it from
			if (Step == 0 && State->Mined[0] >= 1) {
				Step = 1;
				const FVector Aside = Wood->GetActorRotation().Vector().GetSafeNormal2D().RotateAngleAxis(90.0, FVector::UpVector);
				Hero->SetActorLocation(Hero->GetActorLocation() + Aside * 450.0 + FVector(0.0, 0.0, 60.0));
				// looking a little to one side of it, so the hero does not stand in front of what is looked at
				Controller->SetControlRotation((Wood->GetActorLocation() + FVector(0.0, 0.0, 150.0) - Hero->GetActorLocation()).Rotation() + FRotator(0.0, -16.0, 0.0));
				PhaseStart = World->GetTimeSeconds();
			}
			if (Step == 1 && Elapsed(World) > 0.8) {
				Step = 2;
				// low, beside its deposit: not in the air over it
				if (Droid != nullptr) {
					Test->TestTrue(TEXT("The droid hangs just above the ground"), Droid->GetActorLocation().Z - Wood->GetActorLocation().Z < 250.0);
					Test->TestTrue(TEXT("The droid hangs beside its deposit, not over it"), FVector::Dist2D(Droid->GetActorLocation(), Wood->GetActorLocation()) > Wood->Width() * 0.5);
				}
				GameCheck::Shot(TEXT("Mining"), TEXT("mining_wood.png"));
			}
			if (State->Mined[0] < 3) {
				return Elapsed(World) > 60.0 ? Fail(FString::Printf(TEXT("The droid did not mine 3 wood in 60 s: mined %d, %s"), State->Mined[0], *ASenniteMiningDroid::Status(State))) : false;
			}
			Test->TestTrue(TEXT("The droid mines at its deposit"), Droid != nullptr && Droid->bMining);
			Test->TestEqual(TEXT("What is mined comes out of the deposit"), Wood->Left, Wood->Held - State->Mined[0]);
			Test->TestEqual(TEXT("What is mined is the player's to build with"), State->MaterialCounts[0], WoodStart + State->Mined[0]);
			Test->TestEqual(TEXT("The HUD says the droid is mining wood"), ASenniteMiningDroid::Status(State), FString(TEXT("DROID MINING WOOD")));
			UE_LOG(LogMiningCheck, Display, TEXT("GAMECHECK mined %d wood in %.1f s; the deposit holds %d of %d"), State->Mined[0], Elapsed(World), Wood->Left, Wood->Held);
			Face(Controller, Hero, Stone.Get());
			// the hero's own weapon in hand again
			Tap(Controller, EKeys::One);
			Enter(EPhase::PutAway, World);
			return false;

		case EPhase::PutAway:
			if (Step == 0 && Elapsed(World) > 1.0) {
				Step = 1;
				Test->TestFalse(TEXT("Taking a weapon in hand puts the droid away"), State->bDroidInHand);
				Tap(Controller, EKeys::LeftMouseButton);
			}
			if (Elapsed(World) < 2.0) {
				return false;
			}
			Test->TestTrue(TEXT("With a weapon in hand the fire button sends the droid nowhere"), Droid != nullptr && Droid->Target == Wood.Get());
			Tap(Controller, EKeys::T);
			Enter(EPhase::SendToStone, World);
			return false;

		case EPhase::SendToStone:
			if (Step == 0 && Elapsed(World) > 1.0) {
				Step = 1;
				Test->TestTrue(TEXT("The droid is taken in hand again"), State->bDroidInHand);
				Tap(Controller, EKeys::LeftMouseButton);
			}
			if (Elapsed(World) < 2.0) {
				return false;
			}
			Test->TestTrue(TEXT("The same droid is sent on to the stone deposit"), Droid != nullptr && Droid->Target == Stone.Get());
			// far from the droid and its deposit: it goes on without its player
			Hero->SetActorLocation(Hero->GetActorLocation() + Stone->GetActorRotation().Vector().GetSafeNormal2D() * 9000.0 + FVector(0.0, 0.0, 600.0));
			Enter(EPhase::MineStone, World);
			return false;

		case EPhase::MineStone:
			if (State->Mined[1] < 2) {
				return Elapsed(World) > 90.0 ? Fail(FString::Printf(TEXT("The droid did not mine 2 stone in 90 s: mined %d, %s"), State->Mined[1], *ASenniteMiningDroid::Status(State))) : false;
			}
			Test->TestEqual(TEXT("Stone comes out of the stone deposit"), Stone->Left, Stone->Held - State->Mined[1]);
			StoneMark = State->Mined[1];
			Enter(EPhase::Away, World);
			return false;

		case EPhase::Away:
			if (State->Mined[1] < StoneMark + 2) {
				return Elapsed(World) > 30.0 ? Fail(TEXT("With its player far away the droid stopped mining")) : false;
			}
			Test->TestTrue(TEXT("The droid is still out, with its player 90 m off"), Droid != nullptr && FVector::Dist(Droid->GetActorLocation(), Hero->GetActorLocation()) > 6000.0);
			UE_LOG(LogMiningCheck, Display, TEXT("GAMECHECK the droid mined on with its player away: %d stone"), State->Mined[1]);
			Enter(EPhase::UsedUp, World);
			return false;

		case EPhase::UsedUp: {
			if (Step == 0) {
				Step = 1;
				// all but the last two of it taken, as other droids take from the same deposit
				Stone->Take(Stone->Left - 2);
				StoneMark = State->Mined[1];
				const double Tall = Stone->GetComponentsBoundingBox().GetSize().Z;
				Test->TestTrue(FString::Printf(TEXT("A deposit nearly used up is drawn smaller (%.0f of %.0f cm tall)"), Tall, StoneTall), Tall < StoneTall * 0.65 && Tall > StoneTall * 0.4);
				// seen from beside it
				Hero->SetActorLocation(Stone->GetActorLocation() + Stone->GetActorRotation().Vector().GetSafeNormal2D() * 1300.0 + FVector(0.0, 0.0, 400.0));
				Hero->GetCharacterMovement()->StopMovementImmediately();
				Controller->SetControlRotation((Stone->GetActorLocation() + FVector(0.0, 0.0, 100.0) - Hero->GetActorLocation()).Rotation() + FRotator(0.0, -16.0, 0.0));
				GameCheck::Shot(TEXT("Mining"), TEXT("deposit_nearly_used_up.png"));
				return false;
			}
			if (Stone.IsValid()) {
				return Elapsed(World) > 30.0 ? Fail(FString::Printf(TEXT("The droid did not use the stone deposit up in 30 s: it holds %d, %s"), Stone->Left, *ASenniteMiningDroid::Status(State))) : false;
			}
			if (!bStoneUsedUp) {
				bStoneUsedUp = true;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 3.0) {
				return false;
			}
			Test->TestEqual(TEXT("The player has the deposit's last units, and no more than it held"), State->Mined[1], StoneMark + 2);
			int32 Standing = 0;
			for (TActorIterator<ASenniteDeposit> It(World); It; ++It) {
				Standing += 1;
			}
			Test->TestEqual(TEXT("A deposit used up is destroyed"), Standing, Deposits - 1);
			Test->TestTrue(TEXT("The droid has nothing to mine once its deposit is used up"), Droid != nullptr && Droid->Target == nullptr && !Droid->bMining);
			Test->TestEqual(TEXT("The HUD says the droid is idle"), ASenniteMiningDroid::Status(State), FString(TEXT("DROID IDLE")));
			GameCheck::Shot(TEXT("Mining"), TEXT("deposit_used_up.png"));
			UE_LOG(LogMiningCheck, Display, TEXT("GAMECHECK the stone deposit was used up and destroyed: %d of %d deposits stand"), Standing, Deposits);
			Enter(EPhase::Shots, World);
			return false;
		}

		case EPhase::Shots: {
			if (Droid == nullptr) {
				return Fail(TEXT("The droid went away before it was shot at"));
			}
			// its own player's shot stops on it and does not count
			Test->TestTrue(TEXT("A droid stops a bullet"), AProjectileActor::Land(Droid, 1000.0f, 1, Hero));
			Test->TestEqual(TEXT("Its own player's shot does not count on it"), Droid->Health, GetDefault<ASenniteMiningDroid>()->StartHealth);
			// another player's hero: three rifle rounds shoot it down
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ASenniteCharacter* Other = World->SpawnActor<ASenniteCharacter>(Hero->GetClass(), Hero->GetActorLocation() + FVector(600.0, 0.0, 200.0), FRotator::ZeroRotator, Spawn);
			ASennitePlayerState* Theirs = World->SpawnActor<ASennitePlayerState>(State->GetClass(), Spawn);
			if (Other == nullptr || Theirs == nullptr) {
				return Fail(TEXT("Another player's hero could not be made"));
			}
			Other->SetPlayerState(Theirs);
			const int32 Health = Droid->Health;
			AProjectileActor::Land(Droid, 20.0f, 1, Other);
			Test->TestEqual(TEXT("Another player's round takes its damage from the droid"), Droid->Health, Health - 20);
			AProjectileActor::Land(Droid, 20.0f, 1, Other);
			AProjectileActor::Land(Droid, 20.0f, 1, Other);
			Other->Destroy();
			Theirs->Destroy();
			StoneMark = State->Mined[1];
			Face(Controller, Hero, Wood.Get());
			Enter(EPhase::Down, World);
			return false;
		}

		case EPhase::Down: {
			if (Step == 0 && Elapsed(World) > 1.0) {
				Step = 1;
				Test->TestFalse(TEXT("Shot down, the droid is out of its player's hand"), State->bDroidInHand);
				Tap(Controller, EKeys::T);
			}
			if (Step == 1 && Elapsed(World) > 2.0) {
				Step = 2;
				Test->TestFalse(TEXT("A droid shot down is not taken in hand"), State->bDroidInHand);
				Tap(Controller, EKeys::LeftMouseButton);
			}
			if (Elapsed(World) < 4.0) {
				return false;
			}
			Test->TestTrue(TEXT("Shot down, the droid is gone"), State->Droid == nullptr && State->bDroidDown);
			Test->TestEqual(TEXT("The HUD says the droid is down"), ASenniteMiningDroid::Status(State), FString(TEXT("DROID DOWN")));
			Test->TestEqual(TEXT("Nothing more is mined once it is down"), State->Mined[1], StoneMark);
			Test->TestTrue(TEXT("What was mined before stays mined"), State->Mined[0] >= 3 && State->Mined[1] >= 6);
			int32 Out = 0;
			for (TActorIterator<ASenniteMiningDroid> It(World); It; ++It) {
				Out += 1;
			}
			Test->TestEqual(TEXT("Aimed at a deposit again, no other droid is taken out this game"), Out, 0);
			GameCheck::Shot(TEXT("Mining"), TEXT("droid_down.png"));
			Enter(EPhase::Done, World);
			return false;
		}

		case EPhase::Done:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			UE_LOG(LogMiningCheck, Display, TEXT("GAMECHECK finished"));
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteMiningTest, "Sennite.Mining.DroidMinesAndIsShotDown",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteMiningTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMiningRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
