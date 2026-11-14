// Match bases check: two copies of the game on one machine, in the arena. One is the publisher's
// match server and hosts a match the publisher's server has opened between two factions; the other
// joins it and has no line to the publisher's server at all.
//
// The host (Sennite.MatchBases.Host) waits for the joining player, then for the match's bases: each
// faction's built structures, read from the publisher's server, standing on that faction's base of
// the arena. Once the joining player has seen them too, it picks up a rifle with the real keys,
// shoots the structure on its own base with the mouse until it comes down, sees the publisher's
// server record it, and waits for the joining player to leave.
//
// The joining player (Sennite.MatchBases.Joiner) sees the same structures at the same places, each
// one solid, walks a few steps so the host knows it has, then sees the one the host shot go and the
// others stay.
//
// Each side prints where every base and every structure is in its own world, for what started the
// check to hold against each other and against the publisher's server.
//
// The arena has no weapons to pick up yet, so this check lays a rifle by the host's start.
//
// Server/test/game/match-bases.test.ts starts both with the accounts they need and reads both results.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingActor.h"
#include "GameCheck.h"
#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "SenniteMatchBases.h"
#include "SennitePlayerState.h"
#include "SenniteStructure.h"
#include "SenniteWeapons.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatchBasesCheck, Log, All);

namespace
{
	int32 Heroes(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ASenniteCharacter> It(World); It; ++It) {
			Count += It->IsActorBeingDestroyed() ? 0 : 1;
		}
		return Count;
	}

	ASenniteCharacter* OtherHero(UWorld* World, const ASenniteCharacter* Own)
	{
		for (TActorIterator<ASenniteCharacter> It(World); It; ++It) {
			if (*It != Own && !It->IsActorBeingDestroyed()) {
				return *It;
			}
		}
		return nullptr;
	}

	ASenniteArena* ArenaOf(UWorld* World)
	{
		TActorIterator<ASenniteArena> It(World);
		return It ? *It : nullptr;
	}

	class FMatchBasesRun : public IAutomationLatentCommand
	{
	public:
		FMatchBasesRun(FAutomationTestBase* InTest, bool bInHost) : Test(InTest), bHost(bInHost), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override;

	private:
		enum class EPhase { WaitForPawn, WaitForOther, WaitForBases, GetRifle, Shoot, Recorded, WaitForLeave, Walk, WaitForShot, Left, Done };

		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		const TCHAR* Side() const { return bHost ? TEXT("host") : TEXT("joiner"); }
		void Tap(APlayerController* Controller, const FKey& InKey) const
		{
			GameCheck::Key(Controller, InKey, IE_Pressed);
			GameCheck::Key(Controller, InKey, IE_Released);
		}

		/** Every structure the match set, as this machine has it: standing, solid, on its faction's base. */
		void Look(UWorld* World, const ASenniteArena* Arena, const ASenniteMatchBases* Bases);

		FAutomationTestBase* Test;
		bool bHost;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		int32 Shots = 0;
		double NextShot = 0.0;
		FString Target;
		/** On the host: where the joining player's hero was when the host first saw it. On the joining player: where its own walk began. */
		FVector OtherStart = FVector::ZeroVector;
		double LeftAt = 0.0;
	};

	void FMatchBasesRun::Look(UWorld* World, const ASenniteArena* Arena, const ASenniteMatchBases* Bases)
	{
		const FArenaPlan& Plan = Arena->GetPlan();
		for (int32 Base = 0; Base < Plan.Bases.Num(); ++Base) {
			UE_LOG(LogMatchBasesCheck, Display, TEXT("GAMECHECK %s: base %d at %.0f %.0f %.0f"), Side(), Base, Plan.Bases[Base].X, Plan.Bases[Base].Y, Plan.Bases[Base].Z);
		}
		for (const FSenniteMatchStructure& Entry : Bases->Structures) {
			const ASenniteStructure* Actor = Bases->ActorOf(Entry.InstanceId);
			const FString Name = FString::Printf(TEXT("%s %s"), *Entry.PrefabId, *Entry.InstanceId);
			if (!Test->TestNotNull(*FString::Printf(TEXT("%s is in this machine's world"), *Name), Actor)) {
				continue;
			}
			const FVector At = Actor->GetActorLocation();
			UE_LOG(LogMatchBasesCheck, Display, TEXT("GAMECHECK %s: structure %s %s base %d at %.0f %.0f %.0f yaw %.0f"),
				Side(), *Entry.InstanceId, *Entry.PrefabId, Entry.Base, At.X, At.Y, At.Z, Actor->GetActorRotation().Yaw);
			Test->TestTrue(*FString::Printf(TEXT("%s stands, as built"), *Name), Actor->bStanding && Actor->Panels.Num() > 0);
			Test->TestTrue(*FString::Printf(TEXT("%s is on a base of the arena"), *Name), Plan.Bases.IsValidIndex(Entry.Base));
			if (Plan.Bases.IsValidIndex(Entry.Base)) {
				Test->TestTrue(*FString::Printf(TEXT("%s is on its faction's base, at the base's height"), *Name),
					FVector::Dist2D(At, Plan.Bases[Entry.Base]) < Arena->BaseRadius && FMath::IsNearlyEqual(At.Z, Plan.Bases[Entry.Base].Z, 1.0));
			}
			// solid: a line from above it to the ground under it stops on it
			FHitResult Hit;
			const bool bStopped = World->LineTraceSingleByChannel(Hit, At + FVector(0.0, 0.0, 5000.0), At - FVector(0.0, 0.0, 50.0), ECC_WorldDynamic);
			Test->TestTrue(*FString::Printf(TEXT("%s is solid"), *Name), bStopped && Hit.GetActor() == Actor);
		}
	}

	bool FMatchBasesRun::Update()
	{
		if (Phase == EPhase::Left) {
			return FPlatformTime::Seconds() - LeftAt > 1.5;
		}
		if (FPlatformTime::Seconds() - RealStart > 600.0) {
			return Fail(FString::Printf(TEXT("The %s's half of the match bases check did not finish in 600 s"), Side()));
		}
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		ASenniteArena* Arena = ArenaOf(World);
		ASenniteMatchBases* Bases = ASenniteMatchBases::Find(World);

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || Arena == nullptr || !Arena->IsBuilt() || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 90.0 ? Fail(FString::Printf(TEXT("The %s got no hero on an arena: is this the arena map?"), Side())) : false;
			}
			// the keys walk the hero the way the camera faces: face the way the hero does
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			Enter(bHost ? EPhase::WaitForOther : EPhase::WaitForBases, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr || Arena == nullptr) {
			return Fail(FString::Printf(TEXT("The %s's hero or the arena went away during the check"), Side()));
		}

		switch (Phase) {
		case EPhase::WaitForOther:
			// the structure comes down only once the joining player is there to see it stand first
			if (Heroes(World) < 2 || OtherHero(World, Hero) == nullptr) {
				return Elapsed(World) > 180.0 ? Fail(TEXT("No player joined the match in 180 s")) : false;
			}
			OtherStart = OtherHero(World, Hero)->GetActorLocation();
			Enter(EPhase::WaitForBases, World);
			return false;

		case EPhase::WaitForBases: {
			if (Bases == nullptr) {
				return Elapsed(World) > 60.0 ? Fail(FString::Printf(TEXT("The %s's world has no match bases: does the map's address name a match (?Match=)?"), Side())) : false;
			}
			if (Bases->State == EMatchBasesState::Failed) {
				return Fail(FString::Printf(TEXT("The match has no bases: %s"), *Bases->Why));
			}
			bool bAll = Bases->State == EMatchBasesState::Placed && Bases->Structures.Num() > 0;
			for (const FSenniteMatchStructure& Entry : Bases->Structures) {
				bAll = bAll && !Entry.bDestroyed && Bases->ActorOf(Entry.InstanceId) != nullptr;
			}
			if (!bAll) {
				return Elapsed(World) > 120.0 ? Fail(FString::Printf(TEXT("The %s did not have the match's bases, every structure standing, in 120 s"), Side())) : false;
			}
			if (Step == 0) {
				// a frame for what was just built to be in the world's collision
				Step = 1;
				return false;
			}
			if (bHost) {
				Test->TestEqual(TEXT("The bases are of the match the map's address names"), Bases->MatchId, FString(World->URL.GetOption(TEXT("Match="), TEXT(""))));
			}
			else {
				// the joining player's game was given no match: the match server told it
				Test->TestFalse(TEXT("The joining player is told which match it is"), Bases->MatchId.IsEmpty());
			}
			UE_LOG(LogMatchBasesCheck, Display, TEXT("GAMECHECK %s: match %s"), Side(), *Bases->MatchId);
			Look(World, Arena, Bases);
			GameCheck::Shot(TEXT("MatchBases"), bHost ? TEXT("host_bases.png") : TEXT("joiner_bases.png"));
			if (!bHost) {
				Enter(EPhase::Walk, World);
				return false;
			}
			// the host shoots the structure on the base it started on
			const FArenaPlan& Plan = Arena->GetPlan();
			int32 Own = 0;
			for (int32 Base = 1; Base < Plan.Bases.Num(); ++Base) {
				if (FVector::Dist2D(Hero->GetActorLocation(), Plan.Bases[Base]) < FVector::Dist2D(Hero->GetActorLocation(), Plan.Bases[Own])) {
					Own = Base;
				}
			}
			for (const FSenniteMatchStructure& Entry : Bases->Structures) {
				if (Entry.Base == Own && Target.IsEmpty()) {
					Target = Entry.InstanceId;
				}
			}
			if (Target.IsEmpty()) {
				return Fail(FString::Printf(TEXT("No structure stands on base %d, where the host started"), Own));
			}
			// The arena has no weapons to pick up yet: a rifle is laid ahead of the host, as on the test range.
			const FVector Ahead = FRotator(0.0, Controller->GetControlRotation().Yaw, 0.0).Vector();
			const FVector Lies(Hero->GetActorLocation().X + Ahead.X * 250.0, Hero->GetActorLocation().Y + Ahead.Y * 250.0, Plan.Bases[Own].Z + 60.0);
			if (World->SpawnActor<ASenniteRifle>(Lies, FRotator::ZeroRotator) == nullptr) {
				return Fail(TEXT("The check could not lay a rifle by the host's start"));
			}
			Enter(EPhase::GetRifle, World);
			return false;
		}

		case EPhase::GetRifle:
			if (Step == 0) {
				// Nothing comes down before the joining player has seen it stand: that player walks a
				// few steps once it has looked at the bases, and the host waits to see it.
				const ASenniteCharacter* Other = OtherHero(World, Hero);
				if (Other == nullptr || FVector::Dist2D(Other->GetActorLocation(), OtherStart) < 150.0) {
					return Elapsed(World) > 240.0
						? Fail(FString::Printf(TEXT("The joining player did not look at the bases and walk in 240 s (their hero is at %s, and was first seen at %s)"),
							Other != nullptr ? *Other->GetActorLocation().ToCompactString() : TEXT("nowhere"), *OtherStart.ToCompactString()))
						: false;
				}
				UE_LOG(LogMatchBasesCheck, Display, TEXT("GAMECHECK host: saw the joining player walk %.0f"), FVector::Dist2D(Other->GetActorLocation(), OtherStart));
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				GameCheck::Key(Controller, EKeys::W, IE_Pressed);
				return false;
			}
			if (!State->EquippedWeapons.Contains(1) && Elapsed(World) < 8.0) {
				return false;
			}
			GameCheck::Key(Controller, EKeys::W, IE_Released);
			if (!State->EquippedWeapons.Contains(1)) {
				return Fail(TEXT("The host did not pick up the rifle by walking over it"));
			}
			Enter(EPhase::Shoot, World);
			return false;

		case EPhase::Shoot: {
			if (Bases == nullptr) {
				return Fail(TEXT("The match's bases went away during the check"));
			}
			const FSenniteMatchStructure* Entry = Bases->Structures.FindByPredicate([this](const FSenniteMatchStructure& S) { return S.InstanceId == Target; });
			if (Entry == nullptr) {
				return Fail(TEXT("The structure the host was shooting is no longer one of the match's"));
			}
			if (Entry->bDestroyed) {
				UE_LOG(LogMatchBasesCheck, Display, TEXT("GAMECHECK host: %s %s came down after %d shots"), *Entry->PrefabId, *Entry->InstanceId, Shots);
				Test->TestTrue(TEXT("It took more than one shot"), Shots > 1);
				Test->TestNull(TEXT("The structure that came down is gone from the host's world"), Bases->ActorOf(Target));
				for (const FSenniteMatchStructure& Other : Bases->Structures) {
					if (Other.InstanceId != Target) {
						Test->TestTrue(*FString::Printf(TEXT("%s still stands"), *Other.InstanceId), !Other.bDestroyed && Bases->ActorOf(Other.InstanceId) != nullptr);
					}
				}
				GameCheck::Shot(TEXT("MatchBases"), TEXT("host_destroyed.png"));
				Enter(EPhase::Recorded, World);
				return false;
			}
			if (Shots >= 80 || Elapsed(World) > 180.0) {
				return Fail(FString::Printf(TEXT("%d shots in %.0f s did not bring the structure down"), Shots, Elapsed(World)));
			}
			if (Hero->CurrentWeapon == nullptr || World->GetTimeSeconds() < NextShot) {
				return false;
			}
			if (Hero->CurrentWeapon->CurrentBulletCount <= 0) {
				Tap(Controller, EKeys::R);
				NextShot = World->GetTimeSeconds() + 2.8;
				return false;
			}
			// at a wall of it, a little under the middle of its ground storey
			if (!GameCheck::AimStep(Controller, Hero->CurrentWeapon->GetMuzzleLocation(), Entry->Location + FVector(0.0, 0.0, ABuildingActor::Cell * 0.4))) {
				return false;
			}
			if (Step == 0) {
				// the structure as the host sees it over the rifle, before the first shot
				Step = 1;
				GameCheck::Shot(TEXT("MatchBases"), TEXT("host_aimed.png"));
				NextShot = World->GetTimeSeconds() + 0.5;
				return false;
			}
			Tap(Controller, EKeys::LeftMouseButton);
			++Shots;
			NextShot = World->GetTimeSeconds() + 0.4;
			return false;
		}

		case EPhase::Recorded:
			if (Bases == nullptr || Bases->Recorded < 1) {
				return Elapsed(World) > 90.0 ? Fail(TEXT("The publisher's server did not record the destroyed structure in 90 s")) : false;
			}
			UE_LOG(LogMatchBasesCheck, Display, TEXT("GAMECHECK host: the publisher's server recorded %s destroyed"), *Target);
			Enter(EPhase::WaitForLeave, World);
			return false;

		case EPhase::WaitForLeave:
			// the joining player quits once it has seen the structure go
			if (Heroes(World) > 1) {
				return Elapsed(World) > 120.0 ? Fail(TEXT("The joining player never left")) : false;
			}
			UE_LOG(LogMatchBasesCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		case EPhase::Walk:
			// the joining player has seen both bases: it walks a few steps, which the host sees
			if (Step == 0) {
				Step = 1;
				OtherStart = Hero->GetActorLocation();
				GameCheck::Key(Controller, EKeys::W, IE_Pressed);
				return false;
			}
			if (Elapsed(World) < 2.5) {
				return false;
			}
			GameCheck::Key(Controller, EKeys::W, IE_Released);
			UE_LOG(LogMatchBasesCheck, Display, TEXT("GAMECHECK joiner: walked %.0f, from %s to %s"), FVector::Dist2D(Hero->GetActorLocation(), OtherStart),
				*OtherStart.ToCompactString(), *Hero->GetActorLocation().ToCompactString());
			Test->TestTrue(TEXT("The joining player's keys walk their hero"), FVector::Dist2D(Hero->GetActorLocation(), OtherStart) > 150.0);
			Enter(EPhase::WaitForShot, World);
			return false;

		case EPhase::WaitForShot: {
			if (Bases == nullptr) {
				return Fail(TEXT("The match's bases went away during the check"));
			}
			const FSenniteMatchStructure* Down = Bases->Structures.FindByPredicate([](const FSenniteMatchStructure& S) { return S.bDestroyed; });
			if (Down == nullptr) {
				return Elapsed(World) > 400.0 ? Fail(TEXT("The joining player saw no structure come down in 400 s")) : false;
			}
			if (Step == 0) {
				Step = 1;
				return false;
			}
			if (Step == 2) {
				// a few frames for the screenshot, then leave the match as a player does, so the host is
				// told at once and is not left waiting for a connection that has gone
				if (Elapsed(World) < 0.6) {
					return false;
				}
				UE_LOG(LogMatchBasesCheck, Display, TEXT("GAMECHECK finished"));
				Controller->ConsoleCommand(TEXT("disconnect"));
				LeftAt = FPlatformTime::Seconds();
				Phase = EPhase::Left;
				return false;
			}
			Test->TestNull(TEXT("The structure that came down is gone from the joining player's world"), Bases->ActorOf(Down->InstanceId));
			int32 Standing = 0;
			for (const FSenniteMatchStructure& Other : Bases->Structures) {
				if (Other.InstanceId != Down->InstanceId) {
					const ASenniteStructure* Actor = Bases->ActorOf(Other.InstanceId);
					Test->TestTrue(*FString::Printf(TEXT("%s still stands"), *Other.InstanceId), !Other.bDestroyed && Actor != nullptr && Actor->bStanding);
					Standing += Actor != nullptr ? 1 : 0;
				}
			}
			UE_LOG(LogMatchBasesCheck, Display, TEXT("GAMECHECK joiner: %s %s is gone, %d still stand"), *Down->PrefabId, *Down->InstanceId, Standing);
			GameCheck::Shot(TEXT("MatchBases"), TEXT("joiner_destroyed.png"));
			Step = 2;
			PhaseStart = World->GetTimeSeconds();
			return false;
		}

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteMatchBasesHostTest, "Sennite.MatchBases.Host", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteMatchBasesHostTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMatchBasesRun(this, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteMatchBasesJoinerTest, "Sennite.MatchBases.Joiner", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteMatchBasesJoinerTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMatchBasesRun(this, false));
	return true;
}

#endif
