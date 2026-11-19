// Match checks.
//
// Sennite.Match.Rules: how a match ends, held against the rules on their own (ASenniteMatch::Judge).
//
// Sennite.Crates.OpenAndArm: one game on the arena, no match. The hero starts with a pickaxe; the
// crate key does nothing away from a crate; beside one it opens it, and the hero has what it held:
// the rifle in hand, the shotgun, the rounds, the bandages and the potions, with nothing laid on the
// ground; the hero shoots and reloads; a while later the crate is closed and full again, and opened
// again it gives rounds and packs but no second rifle.
//
// Sennite.Match.Attacker and Sennite.Match.Defender: two players' games in a match on the
// publisher's match server, which has no player of its own. Each game was given its place at the
// publisher's server (-PublisherPlayerFile=), with a wallet that is a member of one of the match's
// factions; each asks for a ticket, enters, and gets a hero on its faction's base. Each prints
// where every base and structure is in its own world, for what started the check to hold against
// each other and against the publisher's server. Then, by -MatchCheck=:
//   Kills  the attacker takes a rifle from a crate, crosses the connector to the defender's base and
//          brings the defender's hero down until its faction has the kills to win. The defender's
//          hero returns on its own base after each fall but the last.
//   Wipe   the attacker crosses and shoots the defender's faction's structure down: that faction is
//          out, and the attacker's wins.
//   Time   nobody is brought down; the time runs out and nobody wins.
// Both see how the match ended and that the publisher's server recorded it, then leave.
//
// Sennite.Match.Outsider: a third game, whose wallet is a member of no faction of the match, joins
// the same match server. The publisher's server gives it no ticket; it gets no hero, its screen
// says why, and the match server puts it out.
//
// Server/test/game/match.test.ts starts all three with the accounts they need and reads the results.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingActor.h"
#include "GameCheck.h"
#include "RouteWalker.h"
#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "SenniteCrate.h"
#include "SenniteHUD.h"
#include "SenniteMatch.h"
#include "SenniteMatchBases.h"
#include "SennitePlayerState.h"
#include "SenniteStructure.h"
#include "SenniteWeapons.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "HealingActor.h"
#include "SenniteHealing.h"
#include "WeaponActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatchCheck, Log, All);

namespace
{
	ASenniteCharacter* OtherHeroInMatch(UWorld* World, const ASenniteCharacter* Own)
	{
		for (TActorIterator<ASenniteCharacter> It(World); It; ++It) {
			if (*It != Own && !It->IsActorBeingDestroyed() && It->Health > 0.0f) {
				return *It;
			}
		}
		return nullptr;
	}

	ASenniteArena* MatchArena(UWorld* World)
	{
		TActorIterator<ASenniteArena> It(World);
		return It ? *It : nullptr;
	}

	void TapKey(APlayerController* Controller, const FKey& InKey)
	{
		GameCheck::Key(Controller, InKey, IE_Pressed);
		GameCheck::Key(Controller, InKey, IE_Released);
	}

	/** Walking to a place with the mouse and the W key. */
	struct FMatchWalk
	{
		bool bForward = false;

		/** One step: face the place and walk. */
		void Toward(APlayerController* Controller, const ASenniteCharacter* Hero, const FVector& Place)
		{
			const double Want = (Place - Hero->GetActorLocation()).Rotation().Yaw;
			const double Error = FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Yaw, Want);
			if (FMath::Abs(Error) > 0.5) {
				GameCheck::Mouse(Controller, EKeys::MouseX, FMath::Clamp(Error / 0.25, -40.0, 40.0));
			}
			Go(Controller, FMath::Abs(Error) < 6.0);
		}

		void Go(APlayerController* Controller, bool bGo)
		{
			if (bGo != bForward) {
				bForward = bGo;
				GameCheck::Key(Controller, EKeys::W, bGo ? IE_Pressed : IE_Released);
			}
		}
	};

	/** The hero goes to the nearest closed crate, opens it with the key, and holds the rifle it gave. */
	struct FMatchArm
	{
		int32 Step = 0;
		double Since = 0.0;
		FMatchWalk Walk;
		TWeakObjectPtr<ASenniteCrate> Crate;
		int32 RoundsBefore = 0;
		/** Why it could not be done; empty while it can. */
		FString Failure;

		/** One step. True once the hero holds the rifle, or once it failed. */
		bool Update(UWorld* World, APlayerController* Controller, ASenniteCharacter* Hero, const ASennitePlayerState* State)
		{
			const double Now = World->GetTimeSeconds();
			const auto Next = [this, Now](int32 To) { Step = To; Since = Now; };
			const auto Gave = [this, Controller](const FString& Why) { Walk.Go(Controller, false); Failure = Why; return true; };
			switch (Step) {
			case 0:
				Crate = ASenniteCrate::Nearest(World, Hero->GetActorLocation(), 3000.0f, true);
				if (!Crate.IsValid()) {
					return Gave(TEXT("no closed weapon crate stands within 30 m of the hero"));
				}
				RoundsBefore = State->EquippedWeaponsAmmunition[1];
				Next(1);
				return false;
			case 1:
				if (!Crate.IsValid()) {
					return Gave(TEXT("the crate went away"));
				}
				if (FVector::Dist2D(Hero->GetActorLocation(), Crate->GetActorLocation()) < 230.0) {
					Walk.Go(Controller, false);
					Next(2);
					return false;
				}
				Walk.Toward(Controller, Hero, Crate->GetActorLocation());
				return Now - Since > 30.0 ? Gave(TEXT("the hero did not reach the crate in 30 s")) : false;
			case 2:
				// let go of the walk for a moment, then the key
				if (Now - Since < 0.3) {
					return false;
				}
				TapKey(Controller, EKeys::E);
				Next(3);
				return false;
			case 3:
				if (!Crate.IsValid() || !Crate->bOpen) {
					return Now - Since > 8.0 ? Gave(TEXT("the crate did not open at the key")) : false;
				}
				Next(4);
				return false;
			case 4: {
				// what the crate held is the hero's as it opens
				if (State->EquippedWeapons.Contains(1)) {
					Next(5);
					return false;
				}
				return Now - Since > 10.0 ? Gave(TEXT("the opened crate did not give the hero its rifle")) : false;
			}
			case 5:
				if (Now - Since < 0.5) {
					return false;
				}
				TapKey(Controller, EKeys::Two);
				Next(6);
				return false;
			default:
				if (Hero->CurrentWeapon == nullptr || Hero->CurrentWeapon->WeaponType != 1) {
					return Now - Since > 8.0 ? Gave(TEXT("the hero does not hold the rifle it took")) : false;
				}
				return true;
			}
		}
	};

	// ---------------------------------------------------------------------------------------------

	class FCratesRun : public IAutomationLatentCommand
	{
	public:
		explicit FCratesRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, FromAfar, Arm, Shoot, Reload, Restock, Again, Done };
		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		FMatchArm Arm;
		int32 RoundsMark = 0;
	};

	bool FCratesRun::Update()
	{
		if (FPlatformTime::Seconds() - RealStart > 300.0) {
			return Fail(TEXT("The crates check did not finish in 300 s"));
		}
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		ASenniteArena* Arena = MatchArena(World);
		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || Arena == nullptr || !Arena->IsBuilt() || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 90.0 ? Fail(TEXT("No hero on an arena: is this the arena map?")) : false;
			}
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			const ASenniteCrate* Rules = GetDefault<ASenniteCrate>();
			int32 Crates = 0;
			int32 Closed = 0;
			for (TActorIterator<ASenniteCrate> It(World); It; ++It) {
				++Crates;
				Closed += It->bOpen ? 0 : 1;
				// each stands on a base, on its ground
				bool bOnBase = false;
				for (const FVector& Base : Arena->GetPlan().Bases) {
					bOnBase |= FVector::Dist2D(It->GetActorLocation(), Base) < Arena->BaseRadius && FMath::IsNearlyEqual(It->GetActorLocation().Z, Base.Z, 1.0);
				}
				Test->TestTrue(TEXT("A crate stands on a base of the arena"), bOnBase);
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK crates: %d on %d bases, %d closed"), Crates, Arena->GetPlan().Bases.Num(), Closed);
			Test->TestEqual(TEXT("Each base has its crates"), Crates, Rules->PerBase * Arena->GetPlan().Bases.Num());
			Test->TestEqual(TEXT("Every crate starts closed"), Closed, Crates);
			Test->TestTrue(TEXT("A hero starts with a pickaxe and nothing else"), State->EquippedWeapons.Num() == 1 && State->EquippedWeapons[0] == 0);
			Enter(EPhase::FromAfar, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr || Arena == nullptr) {
			return Fail(TEXT("The hero or the arena went away during the check"));
		}
		switch (Phase) {
		case EPhase::FromAfar:
			// the key does nothing away from a crate
			if (Step == 0) {
				Step = 1;
				TapKey(Controller, EKeys::E);
				return false;
			}
			if (Elapsed(World) < 1.0) {
				return false;
			}
			for (TActorIterator<ASenniteCrate> It(World); It; ++It) {
				Test->TestFalse(TEXT("No crate opens for a hero who is not beside it"), It->bOpen);
			}
			Enter(EPhase::Arm, World);
			return false;

		case EPhase::Arm:
			if (!Arm.Update(World, Controller, Hero, State)) {
				return false;
			}
			if (!Arm.Failure.IsEmpty()) {
				return Fail(Arm.Failure);
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK crates: opened one; holds a rifle with %d rounds and %d more, %d shells"),
				Hero->CurrentWeapon->CurrentBulletCount, State->EquippedWeaponsAmmunition[1], State->EquippedWeaponsAmmunition[2]);
			Test->TestEqual(TEXT("The crate gave the hero who opened it its rifle rounds"), State->EquippedWeaponsAmmunition[1], Arm.RoundsBefore + GetDefault<ASenniteRifle>()->CrateRounds);
			Test->TestEqual(TEXT("and its shells"), State->EquippedWeaponsAmmunition[2], GetDefault<ASenniteShotgun>()->CrateRounds);
			Test->TestEqual(TEXT("The rifle has a full magazine"), Hero->CurrentWeapon->CurrentBulletCount, Hero->CurrentWeapon->MagazineSize);
			Test->TestTrue(TEXT("The crate gave the shotgun too"), State->EquippedWeapons.Contains(2));
			Test->TestTrue(TEXT("It kept the energy rifle and the sniper: they are bought in the store, and this player bought neither"), !State->EquippedWeapons.Contains(3) && !State->EquippedWeapons.Contains(4));
			Test->TestTrue(TEXT("and gave no rounds for the guns it kept"), State->EquippedWeaponsAmmunition[3] == 0 && State->EquippedWeaponsAmmunition[4] == 0);
			Test->TestEqual(TEXT("and its pack of bandages"), State->HealingItemCounts[0], GetDefault<ASenniteBandage>()->Count);
			Test->TestEqual(TEXT("and its potions"), State->HealingItemCounts[1], GetDefault<ASennitePotion>()->Count);
			Test->TestEqual(TEXT("The opened crate laid nothing on the ground"), GameCheck::LyingAbout(World), 0);
			GameCheck::Shot(TEXT("Crates"), TEXT("crate_opened.png"));
			Enter(EPhase::Shoot, World);
			return false;

		case EPhase::Shoot:
			if (Step == 0) {
				if (Elapsed(World) < 0.6) {
					return false;
				}
				Step = 1;
				TapKey(Controller, EKeys::LeftMouseButton);
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Hero->CurrentWeapon == nullptr || Hero->CurrentWeapon->CurrentBulletCount != Hero->CurrentWeapon->MagazineSize - 1) {
				return Elapsed(World) > 5.0 ? Fail(TEXT("The rifle from the crate did not fire")) : false;
			}
			Enter(EPhase::Reload, World);
			return false;

		case EPhase::Reload:
			if (Step == 0) {
				if (Elapsed(World) < 0.6) {
					return false;
				}
				Step = 1;
				TapKey(Controller, EKeys::R);
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Hero->CurrentWeapon == nullptr || Hero->CurrentWeapon->CurrentBulletCount != Hero->CurrentWeapon->MagazineSize) {
				return Elapsed(World) > 8.0 ? Fail(TEXT("The rifle did not reload from the rounds the crate gave")) : false;
			}
			Test->TestEqual(TEXT("The reload took one round of those the crate gave"), State->EquippedWeaponsAmmunition[1], Arm.RoundsBefore + GetDefault<ASenniteRifle>()->CrateRounds - 1);
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK crates: fired and reloaded; %d rounds more"), State->EquippedWeaponsAmmunition[1]);
			Enter(EPhase::Restock, World);
			return false;

		case EPhase::Restock:
			if (!Arm.Crate.IsValid()) {
				return Fail(TEXT("The crate went away"));
			}
			if (Arm.Crate->bOpen) {
				return Elapsed(World) > GetDefault<ASenniteCrate>()->RestockSeconds + 20.0 ? Fail(TEXT("The crate was not closed and full again in time")) : false;
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK crates: closed and full again after %.0f s"), Elapsed(World));
			// still beside it: the key opens it again
			RoundsMark = State->EquippedWeaponsAmmunition[1];
			TapKey(Controller, EKeys::E);
			Enter(EPhase::Again, World);
			return false;

		case EPhase::Again:
			if (!Arm.Crate.IsValid()) {
				return Fail(TEXT("The crate went away"));
			}
			if (!Arm.Crate->bOpen) {
				return Elapsed(World) > 8.0 ? Fail(TEXT("The crate that was full again did not open at the key")) : false;
			}
			Test->TestEqual(TEXT("Opened again, the crate gives its rifle rounds again"), State->EquippedWeaponsAmmunition[1], RoundsMark + GetDefault<ASenniteRifle>()->CrateRounds);
			Test->TestEqual(TEXT("and its bandages again"), State->HealingItemCounts[0], 2 * GetDefault<ASenniteBandage>()->Count);
			Test->TestEqual(TEXT("A hero with both free guns gets no second one of either: its own weapon and two guns"), State->EquippedWeapons.Num(), 3);
			Test->TestTrue(TEXT("and the rifle is still in hand"), Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == 1);
			Test->TestEqual(TEXT("Nothing lies on the ground by the crate"), GameCheck::LyingAbout(World), 0);
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK crates: opened again; %d rounds more, %d bandages, %d weapons"), State->EquippedWeaponsAmmunition[1], State->HealingItemCounts[0], State->EquippedWeapons.Num());
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}

	// ---------------------------------------------------------------------------------------------

	class FMatchRun : public IAutomationLatentCommand
	{
	public:
		FMatchRun(FAutomationTestBase* InTest, bool bInAttacker) : Test(InTest), bAttacker(bInAttacker), RealStart(FPlatformTime::Seconds())
		{
			FParse::Value(FCommandLine::Get(), TEXT("MatchCheck="), Scenario);
			ShotFolder = FString::Printf(TEXT("Match_%s"), *Scenario);
		}
		virtual bool Update() override;

	private:
		enum class EPhase { Enter, Look, Arm, WaitPlaying, Cross, Approach, Fight, Watch, Over, Recorded, Left, Done };

		void Go(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		const TCHAR* Side() const { return bAttacker ? TEXT("attacker") : TEXT("defender"); }
		const TCHAR* Folder() const { return *ShotFolder; }

		/** Every base and structure of the match, as this machine has it. */
		void Look(UWorld* World, const ASenniteArena* Arena, const ASenniteMatchBases* Bases);
		/** The hero's comings and goings. */
		void Follow(UWorld* World, const ASenniteArena* Arena, const ASenniteMatch* Match, const ASenniteCharacter* Hero, const ASennitePlayerState* State);

		FAutomationTestBase* Test;
		bool bAttacker;
		FString Scenario;
		FString ShotFolder;
		double RealStart;
		EPhase Phase = EPhase::Enter;
		double PhaseStart = 0.0;
		int32 Step = 0;
		FMatchArm Arm;
		FMatchWalk Walk;
		GameCheck::FRouteWalker Walker;
		int32 OwnBase = 0;
		int32 Shots = 0;
		double NextShot = 0.0;
		FString Target;
		bool bHadHero = true;
		double FellAt = 0.0;
		int32 Falls = 0;
		int32 Returns = 0;
		bool bHudSeen = false;
		double LeftAt = 0.0;
	};

	void FMatchRun::Look(UWorld* World, const ASenniteArena* Arena, const ASenniteMatchBases* Bases)
	{
		const FArenaPlan& Plan = Arena->GetPlan();
		for (int32 Base = 0; Base < Plan.Bases.Num(); ++Base) {
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK %s: base %d at %.0f %.0f %.0f"), Side(), Base, Plan.Bases[Base].X, Plan.Bases[Base].Y, Plan.Bases[Base].Z);
		}
		for (const FSenniteMatchStructure& Entry : Bases->Structures) {
			const ASenniteStructure* Actor = Bases->ActorOf(Entry.InstanceId);
			const FString Name = FString::Printf(TEXT("%s %s"), *Entry.PrefabId, *Entry.InstanceId);
			if (!Test->TestNotNull(*FString::Printf(TEXT("%s is in this machine's world"), *Name), Actor)) {
				continue;
			}
			const FVector At = Actor->GetActorLocation();
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK %s: structure %s %s base %d at %.0f %.0f %.0f yaw %.0f"),
				Side(), *Entry.InstanceId, *Entry.PrefabId, Entry.Base, At.X, At.Y, At.Z, Actor->GetActorRotation().Yaw);
			Test->TestTrue(*FString::Printf(TEXT("%s stands, as built"), *Name), Actor->bStanding && Actor->Panels.Num() > 0);
			if (Test->TestTrue(*FString::Printf(TEXT("%s is on a base of the arena"), *Name), Plan.Bases.IsValidIndex(Entry.Base))) {
				Test->TestTrue(*FString::Printf(TEXT("%s is on its faction's base, at the base's height"), *Name),
					FVector::Dist2D(At, Plan.Bases[Entry.Base]) < Arena->BaseRadius && FMath::IsNearlyEqual(At.Z, Plan.Bases[Entry.Base].Z, 1.0));
			}
			// solid: a line from above it to the ground under it stops on it
			FHitResult Hit;
			const bool bStopped = World->LineTraceSingleByChannel(Hit, At + FVector(0.0, 0.0, 5000.0), At - FVector(0.0, 0.0, 50.0), ECC_WorldDynamic);
			Test->TestTrue(*FString::Printf(TEXT("%s is solid"), *Name), bStopped && Hit.GetActor() == Actor);
		}
	}

	void FMatchRun::Follow(UWorld* World, const ASenniteArena* Arena, const ASenniteMatch* Match, const ASenniteCharacter* Hero, const ASennitePlayerState* State)
	{
		if (Hero == nullptr && bHadHero) {
			bHadHero = false;
			FellAt = World->GetTimeSeconds();
			++Falls;
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK %s: its hero fell"), Side());
		}
		else if (Hero != nullptr && !bHadHero && Hero->GetCharacterMovement()->IsMovingOnGround()) {
			bHadHero = true;
			++Returns;
			const double Far = FVector::Dist2D(Hero->GetActorLocation(), Arena->GetPlan().Bases[OwnBase]);
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK %s: its hero returned after %.1f s, %.0f from the middle of its base, health %.0f, %d weapons"),
				Side(), World->GetTimeSeconds() - FellAt, Far, Hero->Health, State->EquippedWeapons.Num());
			Test->TestTrue(TEXT("A hero that fell returns on its own faction's base"), Far < Arena->BaseRadius);
			Test->TestTrue(TEXT("It returns no sooner than the match's rule says"), World->GetTimeSeconds() - FellAt >= Match->RespawnSeconds - 1.0);
			Test->TestEqual(TEXT("It returns whole"), Hero->Health, 100.0f);
			Test->TestTrue(TEXT("It returns with a pickaxe and nothing it picked up"), State->EquippedWeapons.Num() == 1 && State->EquippedWeapons[0] == 0);
		}
		if (!bHudSeen && Match->State == EMatchState::Playing) {
			const APlayerController* Controller = World->GetFirstPlayerController();
			const ASenniteHUD* Hud = Controller != nullptr ? Cast<ASenniteHUD>(Controller->GetHUD()) : nullptr;
			const FString* Line = Hud != nullptr ? Hud->LastDrawn.FindByPredicate([](const FString& Each) { return Each.StartsWith(TEXT("match=")); }) : nullptr;
			if (Line != nullptr) {
				bHudSeen = true;
				UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK %s: hud %s"), Side(), **Line);
			}
		}
	}

	bool FMatchRun::Update()
	{
		if (Phase == EPhase::Left) {
			return FPlatformTime::Seconds() - LeftAt > 1.5;
		}
		if (FPlatformTime::Seconds() - RealStart > 1200.0) {
			return Fail(FString::Printf(TEXT("The %s's half of the match check did not finish in 1200 s"), Side()));
		}
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		ASenniteArena* Arena = MatchArena(World);
		ASenniteMatchBases* Bases = ASenniteMatchBases::Find(World);
		ASenniteMatch* Match = ASenniteMatch::Find(World);

		if (Phase == EPhase::Enter) {
			if (Match != nullptr && Match->State == EMatchState::Failed) {
				return Fail(FString::Printf(TEXT("The match cannot be played: %s"), *Match->Why));
			}
			const bool bIn = Match != nullptr && State != nullptr && !State->FactionId.IsEmpty() && Hero != nullptr && Arena != nullptr && Arena->IsBuilt()
				&& Hero->GetCharacterMovement()->IsMovingOnGround();
			if (!bIn) {
				return FPlatformTime::Seconds() - RealStart > 180.0
					? Fail(FString::Printf(TEXT("The %s did not enter the match with a hero in 180 s. The game says: %s"), Side(), Match != nullptr ? *Match->Said : TEXT("this world has no match")))
					: false;
			}
			const FSenniteMatchFaction* Own = Match->FactionOf(State->FactionId);
			if (Own == nullptr) {
				return Fail(TEXT("The faction the player was let in for is not one of the match's"));
			}
			OwnBase = Own->Base;
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK %s: match %s; plays for faction %s on base %d; wallet %s"), Side(), *Match->MatchId, *State->FactionId, OwnBase, *State->Wallet);
			Test->TestTrue(TEXT("The hero starts on its own faction's base"), FVector::Dist2D(Hero->GetActorLocation(), Arena->GetPlan().Bases[OwnBase]) < Arena->BaseRadius);
			Test->TestTrue(TEXT("A hero starts with a pickaxe and nothing else"), State->EquippedWeapons.Num() == 1 && State->EquippedWeapons[0] == 0);
			if (bAttacker && OwnBase != 0) {
				return Fail(TEXT("The attacker's wallet must be of the faction the match names first: the way across starts at its base"));
			}
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			Go(EPhase::Look, World);
			return false;
		}
		if (State == nullptr || Arena == nullptr || Match == nullptr || Bases == nullptr) {
			return Fail(FString::Printf(TEXT("The %s's player, arena or match went away during the check"), Side()));
		}
		Follow(World, Arena, Match, Hero, State);
		// however it comes about, the end of the match is where every phase before it leads
		if (Match->State == EMatchState::Over && Phase != EPhase::Over && Phase != EPhase::Recorded && Phase != EPhase::Done) {
			if (Hero != nullptr) {
				Walk.Go(Controller, false);
				Walker.Forward(Controller, false, false);
			}
			Go(EPhase::Over, World);
			return false;
		}

		switch (Phase) {
		case EPhase::Look: {
			if (Bases->State == EMatchBasesState::Failed) {
				return Fail(FString::Printf(TEXT("The match has no bases: %s"), *Bases->Why));
			}
			bool bAll = Bases->State == EMatchBasesState::Placed && Bases->Structures.Num() > 0;
			for (const FSenniteMatchStructure& Entry : Bases->Structures) {
				bAll = bAll && !Entry.bDestroyed && Bases->ActorOf(Entry.InstanceId) != nullptr;
			}
			if (!bAll || Hero == nullptr) {
				return Elapsed(World) > 120.0 ? Fail(FString::Printf(TEXT("The %s did not have the match's bases, every structure standing, in 120 s"), Side())) : false;
			}
			if (Step == 0) {
				// a frame for what was just built to be in the world's collision
				Step = 1;
				return false;
			}
			Look(World, Arena, Bases);
			for (const FSenniteMatchFaction& Faction : Match->Factions) {
				UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK %s: faction %s base %d"), Side(), *Faction.FactionId, Faction.Base);
			}
			// the hero is clear of what is built on its base, and its crates stand by it
			for (const FSenniteMatchStructure& Entry : Bases->Structures) {
				const double Taken = 0.5 * FMath::Sqrt(static_cast<double>(Entry.Width * Entry.Width + Entry.Depth * Entry.Depth));
				Test->TestTrue(TEXT("The hero does not start inside a structure"), FVector::Dist2D(Hero->GetActorLocation(), Entry.Location) > Taken);
			}
			int32 Near = 0;
			for (TActorIterator<ASenniteCrate> It(World); It; ++It) {
				Near += FVector::Dist2D(It->GetActorLocation(), Hero->GetActorLocation()) < 3000.0 ? 1 : 0;
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK %s: %d crates by its start; first to %d kills, %.0f s"), Side(), Near, Match->Target, Match->Length);
			Test->TestEqual(TEXT("Its base's crates stand by where its heroes start"), Near, GetDefault<ASenniteCrate>()->PerBase);
			GameCheck::Shot(Folder(), bAttacker ? TEXT("attacker_bases.png") : TEXT("defender_bases.png"));
			Go(bAttacker ? EPhase::Arm : EPhase::Watch, World);
			return false;
		}

		case EPhase::Arm:
			if (Hero == nullptr) {
				return Fail(TEXT("The attacker's hero went away before it had a weapon"));
			}
			if (!Arm.Update(World, Controller, Hero, State)) {
				return false;
			}
			if (!Arm.Failure.IsEmpty()) {
				return Fail(Arm.Failure);
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK attacker: opened a crate; holds a rifle with %d rounds and %d more"), Hero->CurrentWeapon->CurrentBulletCount, State->EquippedWeaponsAmmunition[1]);
			Go(EPhase::WaitPlaying, World);
			return false;

		case EPhase::WaitPlaying:
			if (Match->State != EMatchState::Playing) {
				return Elapsed(World) > 300.0 ? Fail(TEXT("The match was not being played 300 s after the attacker was armed: did the defender enter?")) : false;
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK attacker: the match is being played, %.0f s left"), Match->SecondsLeft());
			// with the time as the only thing that can end it, nobody goes anywhere
			Go(Scenario == TEXT("Time") ? EPhase::Watch : EPhase::Cross, World);
			return false;

		case EPhase::Cross:
			if (Hero == nullptr) {
				return Fail(TEXT("The attacker's hero fell on the way across"));
			}
			if (!Walker.Step(Controller, Hero, Arena, 0)) {
				return Elapsed(World) > 420.0 ? Fail(FString::Printf(TEXT("The attacker did not get across in 420 s: stopped at point %d, at %s"), Walker.Next, *Hero->GetActorLocation().ToCompactString())) : false;
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK attacker: crossed to the other base in %.0f s"), Elapsed(World));
			Go(Scenario == TEXT("Kills") ? EPhase::Approach : EPhase::Fight, World);
			return false;

		case EPhase::Approach: {
			if (Hero == nullptr) {
				return Fail(TEXT("The attacker's hero fell on the other base"));
			}
			// toward where the other faction's heroes start, until its hero is 20 m off
			const ASenniteCharacter* Other = OtherHeroInMatch(World, Hero);
			const FVector Their = Other != nullptr ? Other->GetActorLocation() : Arena->GetPlan().Bases[OwnBase == 0 ? 1 : 0];
			if (FVector::Dist2D(Hero->GetActorLocation(), Their) > 2000.0) {
				Walk.Toward(Controller, Hero, Their);
				return Elapsed(World) > 120.0 ? Fail(TEXT("The attacker did not get near the other faction's hero in 120 s")) : false;
			}
			Walk.Go(Controller, false);
			Go(EPhase::Fight, World);
			return false;
		}

		case EPhase::Fight: {
			if (Hero == nullptr) {
				return Fail(TEXT("The attacker's hero fell"));
			}
			if (Elapsed(World) > 400.0 || Shots > 200) {
				return Fail(FString::Printf(TEXT("%d shots in %.0f s did not end the match"), Shots, Elapsed(World)));
			}
			if (Hero->CurrentWeapon == nullptr || World->GetTimeSeconds() < NextShot) {
				return false;
			}
			// the crate gave a shotgun too: the rifle is the one that reaches
			if (Hero->CurrentWeapon->WeaponType != 1) {
				TapKey(Controller, EKeys::Two);
				NextShot = World->GetTimeSeconds() + 1.0;
				return false;
			}
			if (Hero->CurrentWeapon->CurrentBulletCount <= 0) {
				TapKey(Controller, EKeys::R);
				NextShot = World->GetTimeSeconds() + 2.8;
				return false;
			}
			FVector At;
			if (Scenario == TEXT("Wipe")) {
				// the other faction's structure: at a wall of it, a little under the middle of its ground storey
				const FSenniteMatchStructure* Entry = Bases->Structures.FindByPredicate([State](const FSenniteMatchStructure& S) { return S.FactionId != State->FactionId && !S.bDestroyed; });
				if (Entry == nullptr) {
					return false;
				}
				Target = Entry->InstanceId;
				At = Entry->Location + FVector(0.0, 0.0, ABuildingActor::Cell * 0.4);
			}
			else {
				// the other faction's hero, when it is there: it is gone for a while each time it falls
				const ASenniteCharacter* Other = OtherHeroInMatch(World, Hero);
				if (Other == nullptr || !Other->GetCharacterMovement()->IsMovingOnGround()) {
					return false;
				}
				At = Other->GetActorLocation();
			}
			if (!GameCheck::AimStep(Controller, Hero->CurrentWeapon->GetMuzzleLocation(), At, 0.1)) {
				return false;
			}
			if (Step == 0) {
				Step = 1;
				GameCheck::Shot(Folder(), TEXT("attacker_aimed.png"));
				NextShot = World->GetTimeSeconds() + 0.5;
				return false;
			}
			TapKey(Controller, EKeys::LeftMouseButton);
			++Shots;
			NextShot = World->GetTimeSeconds() + 0.4;
			return false;
		}

		case EPhase::Watch:
			// the defender, and the attacker when only the time can end the match: nothing to do but be there
			return Elapsed(World) > 900.0 ? Fail(FString::Printf(TEXT("The match was not over 900 s after the %s had seen its bases"), Side())) : false;

		case EPhase::Over: {
			// a moment for everything the end of the match changed to reach this machine
			if (Elapsed(World) < 1.5) {
				return false;
			}
			FString Score;
			for (const FSenniteMatchFaction& Faction : Match->Factions) {
				Score += FString::Printf(TEXT(" %s %d%s"), *Faction.FactionId, Faction.Kills, Faction.bOut ? TEXT(" out") : TEXT(""));
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK %s: over: %s because %s; kills%s; %d shots, %d falls, %d returns"), Side(), *Match->Outcome, *Match->Why, *Score, Shots, Falls, Returns);
			const FSenniteMatchFaction* Own = Match->FactionOf(State->FactionId);
			const FSenniteMatchFaction* First = Match->Factions.IsValidIndex(0) ? &Match->Factions[0] : nullptr;
			const FSenniteMatchFaction* Second = Match->Factions.IsValidIndex(1) ? &Match->Factions[1] : nullptr;
			if (Own == nullptr || First == nullptr || Second == nullptr) {
				return Fail(TEXT("The match lost its factions"));
			}
			if (Scenario == TEXT("Kills")) {
				Test->TestEqual(TEXT("The attacker's faction won"), Match->Outcome, First->FactionId);
				Test->TestEqual(TEXT("by the kills to win"), Match->Why, FString::Printf(TEXT("first to %d kills"), Match->Target));
				Test->TestEqual(TEXT("It has exactly the kills to win"), First->Kills, Match->Target);
				Test->TestEqual(TEXT("The defender's faction has none"), Second->Kills, 0);
				if (!bAttacker) {
					Test->TestEqual(TEXT("The defender's hero fell once for each kill"), Falls, Match->Target);
					Test->TestEqual(TEXT("and returned after each but the last"), Returns, Match->Target - 1);
				}
			}
			else if (Scenario == TEXT("Wipe")) {
				Test->TestEqual(TEXT("The attacker's faction won"), Match->Outcome, First->FactionId);
				Test->TestEqual(TEXT("as the last with a base standing"), Match->Why, FString(TEXT("the last faction with a base standing")));
				Test->TestTrue(TEXT("The defender's faction is out"), Second->bOut && !First->bOut);
				Test->TestEqual(TEXT("Nobody was brought down"), First->Kills + Second->Kills, 0);
				const FSenniteMatchStructure* Down = Bases->Structures.FindByPredicate([Second](const FSenniteMatchStructure& S) { return S.FactionId == Second->FactionId; });
				Test->TestTrue(TEXT("The defender's faction's structure is destroyed, for this machine too"), Down != nullptr && Down->bDestroyed && Bases->ActorOf(Down->InstanceId) == nullptr);
				if (Down != nullptr) {
					UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK %s: %s %s is gone"), Side(), *Down->PrefabId, *Down->InstanceId);
				}
				if (!bAttacker) {
					Test->TestTrue(TEXT("The defender's hero is gone and its player watches"), Hero == nullptr && State->IsSpectator());
				}
				else {
					Test->TestTrue(TEXT("It took more than one shot"), Shots > 1);
				}
			}
			else {
				Test->TestEqual(TEXT("Nobody won"), Match->Outcome, FString(ASenniteMatch::Void()));
				Test->TestEqual(TEXT("because the time ran out with the kills level"), Match->Why, FString(TEXT("the time ran out with the most kills shared")));
				Test->TestEqual(TEXT("Nobody was brought down"), First->Kills + Second->Kills, 0);
				Test->TestTrue(TEXT("Every structure still stands"), !Bases->Structures.ContainsByPredicate([](const FSenniteMatchStructure& S) { return S.bDestroyed; }));
			}
			Test->TestTrue(TEXT("The HUD showed the match while it was played"), bHudSeen);
			GameCheck::Shot(Folder(), bAttacker ? TEXT("attacker_over.png") : TEXT("defender_over.png"));
			Go(EPhase::Recorded, World);
			return false;
		}

		case EPhase::Recorded:
			if (!Match->bRecorded) {
				return Elapsed(World) > 400.0 ? Fail(TEXT("The publisher's server did not record the outcome in 400 s")) : false;
			}
			if (Step == 0) {
				Step = 1;
				UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK %s: the publisher's server recorded the outcome"), Side());
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			// a moment for the screenshot, then leave the match as a player does
			if (Elapsed(World) < 0.8) {
				return false;
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK finished"));
			Controller->ConsoleCommand(TEXT("disconnect"));
			LeftAt = FPlatformTime::Seconds();
			Phase = EPhase::Left;
			return false;

		default:
			return true;
		}
	}
}

namespace
{
	class FOutsiderRun : public IAutomationLatentCommand
	{
	public:
		explicit FOutsiderRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds())
		{
			FString Scenario;
			FParse::Value(FCommandLine::Get(), TEXT("MatchCheck="), Scenario);
			ShotFolder = FString::Printf(TEXT("Match_%s"), *Scenario);
		}
		virtual bool Update() override;

	private:
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		FAutomationTestBase* Test;
		double RealStart;
		FString ShotFolder;
		int32 Step = 0;
		double Since = 0.0;
	};

	bool FOutsiderRun::Update()
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - RealStart > 600.0) {
			return Fail(TEXT("The outsider's half of the match check did not finish in 600 s"));
		}
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		const ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		const ASenniteMatch* Match = ASenniteMatch::Find(World);
		switch (Step) {
		case 0:
			// in the match's world, told which match it is
			if (Match == nullptr || !Match->bBases || State == nullptr) {
				return Now - RealStart > 180.0 ? Fail(TEXT("The outsider's game did not join a match with its bases set in 180 s")) : false;
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK outsider: joined the match server of match %s"), *Match->MatchId);
			Step = 1;
			Since = Now;
			return false;
		case 1:
			if (Match == nullptr || State == nullptr) {
				return Fail(TEXT("The outsider was put out before its game had asked for a ticket and been refused"));
			}
			if (!Match->Said.Contains(TEXT("member of no faction"))) {
				return Now - Since > 60.0 ? Fail(FString::Printf(TEXT("The outsider's game was not told its wallet is in no faction of the match. It says: %s"), *Match->Said)) : false;
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK outsider: refused: %s"), *Match->Said);
			Step = 2;
			Since = Now;
			return false;
		case 2: {
			// a moment for the screen to say so, then the picture
			if (Now - Since < 1.0) {
				return false;
			}
			if (Match == nullptr || Controller == nullptr || State == nullptr) {
				return Fail(TEXT("The outsider was put out before its screen was looked at"));
			}
			Test->TestNull(TEXT("A player the publisher's server gave no ticket has no hero"), Controller->GetPawn());
			Test->TestTrue(TEXT("and plays for no faction"), State->FactionId.IsEmpty());
			const ASenniteHUD* Hud = Cast<ASenniteHUD>(Controller->GetHUD());
			Test->TestTrue(TEXT("Its screen says why"), Hud != nullptr && Hud->LastDrawn.ContainsByPredicate([](const FString& Each) { return Each.StartsWith(TEXT("matchnote=")) && Each.Contains(TEXT("member of no faction")); }));
			GameCheck::Shot(*ShotFolder, TEXT("outsider_refused.png"));
			Step = 3;
			Since = Now;
			return false;
		}
		default:
			// the match server puts it out: its game is no longer in the match's world
			if (Match != nullptr) {
				return Now - Since > 180.0 ? Fail(TEXT("The match server did not put the outsider out in 180 s")) : false;
			}
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK outsider: put out of the match %.0f s after it was refused"), Now - Since);
			UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK finished"));
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteMatchOutsiderTest, "Sennite.Match.Outsider", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteMatchOutsiderTest::RunTest(const FString& Parameters)
{
	// being put out is what this check is for: the engine says so as an error
	AddExpectedError(TEXT("connection to the host has been lost"), EAutomationExpectedErrorFlags::Contains, 0);
	ADD_LATENT_AUTOMATION_COMMAND(FOutsiderRun(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteMatchRulesTest, "Sennite.Match.Rules", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteMatchRulesTest::RunTest(const FString& Parameters)
{
	struct FCase { const TCHAR* Id; int32 Kills; bool bOut; };
	const auto Of = [](std::initializer_list<FCase> Each) {
		TArray<FSenniteMatchFaction> Factions;
		for (const FCase& One : Each) {
			FSenniteMatchFaction& Faction = Factions.AddDefaulted_GetRef();
			Faction.FactionId = One.Id;
			Faction.Base = Factions.Num() - 1;
			Faction.Kills = One.Kills;
			Faction.bOut = One.bOut;
		}
		return Factions;
	};
	const auto Holds = [this](const TCHAR* What, const FSenniteMatchVerdict& Got, bool bOver, const TCHAR* Outcome, const TCHAR* Why) {
		TestEqual(*FString::Printf(TEXT("%s: over"), What), Got.bOver, bOver);
		TestEqual(*FString::Printf(TEXT("%s: outcome"), What), Got.Outcome, FString(Outcome));
		TestEqual(*FString::Printf(TEXT("%s: why"), What), Got.Why, FString(Why));
	};
	const TCHAR* Void = ASenniteMatch::Void();
	const TCHAR* Last = TEXT("the last faction with a base standing");
	const TCHAR* Most = TEXT("the most kills when the time ran out");
	const TCHAR* Level = TEXT("the time ran out with the most kills shared");

	Holds(TEXT("nothing has happened"), ASenniteMatch::Judge(Of({ { TEXT("a"), 0, false }, { TEXT("b"), 0, false } }), 10, false), false, TEXT(""), TEXT(""));
	Holds(TEXT("one short of the kills to win"), ASenniteMatch::Judge(Of({ { TEXT("a"), 9, false }, { TEXT("b"), 3, false } }), 10, false), false, TEXT(""), TEXT(""));
	Holds(TEXT("the kills to win"), ASenniteMatch::Judge(Of({ { TEXT("a"), 10, false }, { TEXT("b"), 3, false } }), 10, false), true, TEXT("a"), TEXT("first to 10 kills"));
	Holds(TEXT("the second faction reaches them"), ASenniteMatch::Judge(Of({ { TEXT("a"), 3, false }, { TEXT("b"), 10, false } }), 10, false), true, TEXT("b"), TEXT("first to 10 kills"));
	Holds(TEXT("no number of kills ends a match with no kills to win"), ASenniteMatch::Judge(Of({ { TEXT("a"), 50, false }, { TEXT("b"), 0, false } }), 0, false), false, TEXT(""), TEXT(""));
	Holds(TEXT("the time runs out with one ahead"), ASenniteMatch::Judge(Of({ { TEXT("a"), 4, false }, { TEXT("b"), 2, false } }), 10, true), true, TEXT("a"), Most);
	Holds(TEXT("the time runs out level"), ASenniteMatch::Judge(Of({ { TEXT("a"), 2, false }, { TEXT("b"), 2, false } }), 10, true), true, Void, Level);
	Holds(TEXT("the time runs out with nobody brought down"), ASenniteMatch::Judge(Of({ { TEXT("a"), 0, false }, { TEXT("b"), 0, false } }), 10, true), true, Void, Level);
	Holds(TEXT("a base is wiped"), ASenniteMatch::Judge(Of({ { TEXT("a"), 1, false }, { TEXT("b"), 7, true } }), 10, false), true, TEXT("a"), Last);
	Holds(TEXT("one of three is wiped"), ASenniteMatch::Judge(Of({ { TEXT("a"), 1, false }, { TEXT("b"), 5, false }, { TEXT("c"), 9, true } }), 10, false), false, TEXT(""), TEXT(""));
	Holds(TEXT("one of three is wiped and the time runs out"), ASenniteMatch::Judge(Of({ { TEXT("a"), 1, false }, { TEXT("b"), 5, false }, { TEXT("c"), 9, true } }), 10, true), true, TEXT("b"), Most);
	Holds(TEXT("two of three are wiped"), ASenniteMatch::Judge(Of({ { TEXT("a"), 0, true }, { TEXT("b"), 0, true }, { TEXT("c"), 0, false } }), 10, false), true, TEXT("c"), Last);
	Holds(TEXT("a wiped faction's kills count for nothing"), ASenniteMatch::Judge(Of({ { TEXT("a"), 12, true }, { TEXT("b"), 3, false }, { TEXT("c"), 2, false } }), 10, false), false, TEXT(""), TEXT(""));
	Holds(TEXT("every base is wiped"), ASenniteMatch::Judge(Of({ { TEXT("a"), 4, true }, { TEXT("b"), 2, true } }), 10, false), true, Void, TEXT("no faction has a base standing"));
	UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK rules: 14 cases of how a match ends"));
	UE_LOG(LogMatchCheck, Display, TEXT("GAMECHECK finished"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteCratesTest, "Sennite.Crates.OpenAndArm", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteCratesTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCratesRun(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteMatchAttackerTest, "Sennite.Match.Attacker", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteMatchAttackerTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMatchRun(this, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteMatchDefenderTest, "Sennite.Match.Defender", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteMatchDefenderTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMatchRun(this, false));
	return true;
}

#endif
