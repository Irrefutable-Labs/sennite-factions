// Combat check: two copies of the game on one machine, one hosting the test range and one joining.
// Each opens the crate by its start with the real keys, has its rifle, and aims with the mouse.
//
// The joining player (Sennite.Combat.Joiner) shoots the host twice: each shot spends a round, takes
// 25 from the host's health and shows a hit marker. Then the host shoots back: the joining player's
// health falls 25 a shot with a red flash each time, and at nothing their hero is gone and they are
// a spectator, with the HUD saying so and one player left.
//
// The host (Sennite.Combat.Host) is hurt twice, for 25 each and no more, then takes the rifle from
// its crate and shoots the joining player until they fall: four shots, one kill on the host's count,
// the joining player a spectator on the server, one hero left, and the fallen player's weapons in a
// crate left where they fell, with nothing lying about.
//
// Tools/check_combat.sh starts both and reads both results.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "ProjectileActor.h"
#include "SenniteCharacter.h"
#include "SenniteHUD.h"
#include "SennitePlayerState.h"
#include "SenniteSpectator.h"
#include "SenniteWeapons.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "WeaponActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogCombatCheck, Log, All);

namespace
{
	ASenniteCharacter* OtherHeroOf(UWorld* World, const ASenniteCharacter* Own)
	{
		for (TActorIterator<ASenniteCharacter> It(World); It; ++It) {
			if (*It != Own && !It->IsActorBeingDestroyed()) {
				return *It;
			}
		}
		return nullptr;
	}

	int32 HeroCount(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ASenniteCharacter> It(World); It; ++It) {
			Count += It->IsActorBeingDestroyed() ? 0 : 1;
		}
		return Count;
	}

	class FCombatRun : public IAutomationLatentCommand
	{
	public:
		FCombatRun(FAutomationTestBase* InTest, bool bInHost) : Test(InTest), bHost(bInHost), RealStart(FPlatformTime::Seconds()) {}
		virtual bool Update() override { return bHost ? UpdateHost() : UpdateJoiner(); }

	private:
		enum class EPhase { WaitForPawn, WaitForOther, GetRifle, Aim, Shoot, Shot, BeHurt, Fallen, WaitToBeHurt, KillAim, KillShot, AfterKill, WaitForLeave, Left, Done };

		bool UpdateHost();
		bool UpdateJoiner();
		void Enter(EPhase Next, UWorld* World) { Phase = Next; PhaseStart = World->GetTimeSeconds(); Step = 0; }
		double Elapsed(UWorld* World) const { return World->GetTimeSeconds() - PhaseStart; }
		bool Fail(const FString& Why) { Test->AddError(Why); return true; }
		void Tap(APlayerController* Controller, const FKey& InKey) const
		{
			GameCheck::Key(Controller, InKey, IE_Pressed);
			GameCheck::Key(Controller, InKey, IE_Released);
		}
		/** Note what the HUD drew this frame that only shows for a moment. */
		void Watch(const ASenniteHUD* Hud)
		{
			bSawHitMarker |= Hud->LastDrawn.Contains(TEXT("hitmarker=1"));
			bSawHurt |= Hud->LastDrawn.Contains(TEXT("hurt=1"));
		}
		/** Each new value of this player's own health, in the order seen. */
		void NoteHealth(float Health)
		{
			if (HealthSeen.Num() == 0 || HealthSeen.Last() != Health) {
				HealthSeen.Add(Health);
			}
		}
		FString HealthTrail() const
		{
			return FString::JoinBy(HealthSeen, TEXT(" "), [](float Value) { return FString::FromInt(FMath::RoundToInt(Value)); });
		}

		FAutomationTestBase* Test;
		bool bHost;
		double RealStart;
		EPhase Phase = EPhase::WaitForPawn;
		double PhaseStart = 0.0;
		int32 Step = 0;
		int32 Shots = 0;
		GameCheck::FCrateRun Crate;
		int32 BoltFrames = 0;
		float BoltFar = 0.0f;
		bool bSawHitMarker = false;
		bool bSawHurt = false;
		TArray<float> HealthSeen;
		double LeftAt = 0.0;
	};

	bool FCombatRun::UpdateJoiner()
	{
		if (Phase == EPhase::Left) {
			return FPlatformTime::Seconds() - LeftAt > 1.5;
		}
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		ASenniteHUD* Hud = Controller ? Cast<ASenniteHUD>(Controller->GetHUD()) : nullptr;
		ASenniteCharacter* Other = World ? OtherHeroOf(World, Hero) : nullptr;

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || Hud == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 120.0 ? Fail(TEXT("The joining player got no hero within two minutes")) : false;
			}
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			Enter(EPhase::WaitForOther, World);
			return false;
		}
		if (State == nullptr || Hud == nullptr) {
			return Fail(TEXT("The joining player's state or HUD went away during the check"));
		}
		Watch(Hud);
		// bullets in flight on the joining player's own machine: its own and the host's, sent by the server
		for (TActorIterator<AProjectileActor> It(World); It; ++It) {
			if (It->Bolt != nullptr && It->Bolt->GetStaticMesh() != nullptr && It->Bolt->IsVisible() && !It->IsHidden()) {
				++BoltFrames;
				BoltFar = FMath::Max(BoltFar, It->Flown);
			}
		}
		if (Hero != nullptr) {
			NoteHealth(Hero->Health);
		}

		switch (Phase) {
		case EPhase::WaitForOther:
			if (Other == nullptr || Elapsed(World) < 1.5) {
				return Elapsed(World) > 30.0 ? Fail(TEXT("The joining player never saw the host's hero")) : false;
			}
			// A walks to the left, to the crate by the second start
			Enter(EPhase::GetRifle, World);
			return false;

		case EPhase::GetRifle:
			if (Hero == nullptr) {
				return Fail(TEXT("The joining player's hero went away before they had a rifle"));
			}
			if (!Crate.Step(World, Controller, Hero, EKeys::A)) {
				return false;
			}
			if (!Crate.Failure.IsEmpty()) {
				return Fail(Crate.Failure);
			}
			// what the crate gave arrives from the server
			if (!State->EquippedWeapons.Contains(1) && Elapsed(World) < 12.0) {
				return false;
			}
			Test->TestTrue(TEXT("The crate the joining player opened gave them the rifle"), State->EquippedWeapons.Contains(1));
			Enter(EPhase::Aim, World);
			return false;

		case EPhase::Aim:
			if (Hero == nullptr || Other == nullptr) {
				return Fail(TEXT("A hero went away before the joining player could aim"));
			}
			// the rifle has to arrive in hand from the server before there is a muzzle to aim from
			if (Hero->CurrentWeapon == nullptr || Elapsed(World) < 1.0) {
				return Elapsed(World) > 10.0 ? Fail(TEXT("The rifle never came to the joining player's hand")) : false;
			}
			if (!GameCheck::AimStep(Controller, Hero->CurrentWeapon->GetMuzzleLocation(), Other->GetActorLocation())) {
				return Elapsed(World) > 12.0 ? Fail(TEXT("The joining player could not bring the aim onto the host")) : false;
			}
			if (Shots == 0) {
				Test->TestEqual(TEXT("The joining player's rifle starts full"), Hero->CurrentWeapon->CurrentBulletCount, GetDefault<ASenniteRifle>()->MagazineSize);
				Test->TestEqual(TEXT("The host starts at full health, as the joining player sees it"), Other->Health, 100.0f);
			}
			Tap(Controller, EKeys::LeftMouseButton);
			++Shots;
			Enter(EPhase::Shoot, World);
			return false;

		case EPhase::Shoot:
			// time for the shot to go to the server, land, and the result to come back
			if (Elapsed(World) < 0.9) {
				return false;
			}
			if (Hero == nullptr || Other == nullptr || Hero->CurrentWeapon == nullptr) {
				return Fail(TEXT("A hero or the rifle went away while the joining player was shooting"));
			}
			Test->TestEqual(FString::Printf(TEXT("Shot %d spends a round of the joining player's rifle"), Shots), Hero->CurrentWeapon->CurrentBulletCount, GetDefault<ASenniteRifle>()->MagazineSize - Shots);
			Test->TestEqual(FString::Printf(TEXT("Shot %d takes 25 from the host's health, as the joining player sees it"), Shots), Other->Health, 100.0f - 25.0f * Shots);
			UE_LOG(LogCombatCheck, Display, TEXT("GAMECHECK joiner: shot %d: rounds %d, host's health %.0f, hit marker %d"), Shots, Hero->CurrentWeapon->CurrentBulletCount, Other->Health, bSawHitMarker ? 1 : 0);
			if (Shots < 2) {
				Enter(EPhase::Aim, World);
				return false;
			}
			Test->TestTrue(TEXT("The joining player's HUD shows a hit marker when a shot lands"), bSawHitMarker);
			Test->TestTrue(TEXT("The joining player's HUD shows the rounds left"), Hud->LastDrawn.Contains(FString::Printf(TEXT("ammo=%d"), GetDefault<ASenniteRifle>()->MagazineSize - 2)));
			GameCheck::Shot(TEXT("Combat"), TEXT("joiner_shot_host.png"));
			HealthSeen.Reset();
			bSawHurt = false;
			Enter(EPhase::BeHurt, World);
			return false;

		case EPhase::BeHurt:
			// the host shoots back until the joining player's hero is gone
			if (Hero != nullptr) {
				return Elapsed(World) > 60.0 ? Fail(FString::Printf(TEXT("The host never brought the joining player down (health seen: %s)"), *HealthTrail())) : false;
			}
			Test->TestTrue(TEXT("The joining player's health fell 25 a shot: 100, 75, 50, 25"), HealthTrail() == TEXT("100 75 50 25") || HealthTrail() == TEXT("100 75 50 25 0"));
			Test->TestTrue(TEXT("The joining player's HUD flashed red when they were hurt"), bSawHurt);
			Test->TestTrue(TEXT("The joining player's machine saw bullets in flight, as bolts"), BoltFrames > 0 && BoltFar > 100.0f);
			UE_LOG(LogCombatCheck, Display, TEXT("GAMECHECK joiner: saw bolts in flight on %d frames, up to %.0f cm flown"), BoltFrames, BoltFar);
			UE_LOG(LogCombatCheck, Display, TEXT("GAMECHECK joiner: own health seen %s, hurt flash %d"), *HealthTrail(), bSawHurt ? 1 : 0);
			Enter(EPhase::Fallen, World);
			return false;

		case EPhase::Fallen:
			// the server says so, the player's own machine hears it a little later: wait for the word, not for a set time
			if (Elapsed(World) < 1.5 || (Elapsed(World) < 8.0 && !(State->IsSpectator() && Controller->GetPawn() != nullptr && Controller->GetPawn()->IsA<ASenniteSpectator>()
				&& Hud->LastDrawn.Contains(TEXT("SPECTATING"))))) {
				return false;
			}
			Test->TestTrue(TEXT("The fallen player is a spectator"), State->IsSpectator());
			Test->TestTrue(TEXT("The fallen player watches as a spectator pawn"), Controller->GetPawn() != nullptr && Controller->GetPawn()->IsA<ASenniteSpectator>());
			Test->TestEqual(TEXT("One hero is left in the world"), HeroCount(World), 1);
			Test->TestTrue(TEXT("The fallen player's HUD says they are spectating"), Hud->LastDrawn.Contains(TEXT("spectating=SPECTATING")));
			Test->TestTrue(TEXT("The fallen player's HUD shows one player left"), Hud->LastDrawn.Contains(TEXT("alive=1 ALIVE")));
			UE_LOG(LogCombatCheck, Display, TEXT("GAMECHECK joiner: fallen; spectator %d, heroes left %d, hud: %s"), State->IsSpectator() ? 1 : 0, HeroCount(World), *FString::Join(Hud->LastDrawn, TEXT(" ")));
			GameCheck::Shot(TEXT("Combat"), TEXT("joiner_spectating.png"));
			Enter(EPhase::Done, World);
			return false;

		case EPhase::Done:
			if (Elapsed(World) < 0.8) {
				return false;
			}
			UE_LOG(LogCombatCheck, Display, TEXT("GAMECHECK finished"));
			Controller->ConsoleCommand(TEXT("disconnect"));
			LeftAt = FPlatformTime::Seconds();
			Phase = EPhase::Left;
			return false;

		default:
			return true;
		}
	}

	bool FCombatRun::UpdateHost()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		ASennitePlayerState* State = Controller ? Cast<ASennitePlayerState>(Controller->PlayerState) : nullptr;
		ASenniteHUD* Hud = Controller ? Cast<ASenniteHUD>(Controller->GetHUD()) : nullptr;
		ASenniteCharacter* Other = Hero ? OtherHeroOf(World, Hero) : nullptr;

		if (Phase == EPhase::WaitForPawn) {
			if (Hero == nullptr || State == nullptr || Hud == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround()) {
				return FPlatformTime::Seconds() - RealStart > 60.0 ? Fail(TEXT("The host got no hero")) : false;
			}
			Controller->SetControlRotation(FRotator(-12.0, Hero->GetActorRotation().Yaw, 0.0));
			Enter(EPhase::WaitForOther, World);
			return false;
		}
		if (Hero == nullptr || State == nullptr || Hud == nullptr) {
			return Fail(TEXT("The host's hero or HUD went away during the check"));
		}
		Watch(Hud);
		NoteHealth(Hero->Health);

		switch (Phase) {
		case EPhase::WaitForOther:
			if (Other == nullptr || !Other->GetCharacterMovement()->IsMovingOnGround()) {
				return Elapsed(World) > 150.0 ? Fail(TEXT("No second player joined the host")) : false;
			}
			Enter(EPhase::WaitToBeHurt, World);
			return false;

		case EPhase::WaitToBeHurt:
			// the joining player shoots twice; a little longer shows a third hit is not counted
			if (Hero->Health > 50.0f) {
				return Elapsed(World) > 60.0 ? Fail(FString::Printf(TEXT("The joining player's shots did not bring the host to 50 (health seen: %s)"), *HealthTrail())) : false;
			}
			if (Step == 0) {
				Step = 1;
				PhaseStart = World->GetTimeSeconds();
				return false;
			}
			if (Elapsed(World) < 1.5) {
				return false;
			}
			Test->TestTrue(TEXT("Each of the joining player's two shots took 25 from the host: 100, 75, 50"), HealthTrail() == TEXT("100 75 50"));
			Test->TestTrue(TEXT("The host's HUD flashed red when hurt"), bSawHurt);
			Test->TestTrue(TEXT("The host's HUD shows the health left"), Hud->LastDrawn.Contains(TEXT("health=50")));
			UE_LOG(LogCombatCheck, Display, TEXT("GAMECHECK host: own health seen %s, hurt flash %d"), *HealthTrail(), bSawHurt ? 1 : 0);
			// D walks to the right, to the crate by the first start
			Enter(EPhase::GetRifle, World);
			return false;

		case EPhase::GetRifle:
			if (!Crate.Step(World, Controller, Hero, EKeys::D)) {
				return false;
			}
			if (!Crate.Failure.IsEmpty()) {
				return Fail(Crate.Failure);
			}
			Test->TestTrue(TEXT("The crate the host opened gave it the rifle"), State->EquippedWeapons.Contains(1));
			bSawHitMarker = false;
			Enter(EPhase::KillAim, World);
			return false;

		case EPhase::KillAim:
			if (Other == nullptr) {
				Enter(EPhase::AfterKill, World);
				return false;
			}
			if (Hero->CurrentWeapon == nullptr || Elapsed(World) < 0.6) {
				return Elapsed(World) > 10.0 ? Fail(TEXT("The rifle never came to the host's hand")) : false;
			}
			if (Shots >= 8) {
				return Fail(FString::Printf(TEXT("Eight shots did not bring the joining player down (their health %.0f)"), Other->Health));
			}
			if (!GameCheck::AimStep(Controller, Hero->CurrentWeapon->GetMuzzleLocation(), Other->GetActorLocation())) {
				return Elapsed(World) > 12.0 ? Fail(TEXT("The host could not bring the aim onto the joining player")) : false;
			}
			Tap(Controller, EKeys::LeftMouseButton);
			++Shots;
			Enter(EPhase::KillShot, World);
			return false;

		case EPhase::KillShot:
			if (Elapsed(World) < 0.6) {
				return false;
			}
			UE_LOG(LogCombatCheck, Display, TEXT("GAMECHECK host: shot %d: joining player's health %s"), Shots, Other ? *FString::FromInt(FMath::RoundToInt(Other->Health)) : TEXT("gone"));
			Enter(EPhase::KillAim, World);
			return false;

		case EPhase::AfterKill:
			if (Elapsed(World) < 1.0) {
				return false;
			}
			{
				const ASennitePlayerState* TheirState = nullptr;
				for (APlayerState* Player : World->GetGameState()->PlayerArray) {
					if (Player != State) {
						TheirState = Cast<ASennitePlayerState>(Player);
					}
				}
				Test->TestEqual(TEXT("Four shots of 25 bring down a player with 100 health"), Shots, 4);
				Test->TestEqual(TEXT("The host is credited with one kill"), State->KillCount, 1);
				Test->TestEqual(TEXT("One hero is left on the server"), HeroCount(World), 1);
				Test->TestTrue(TEXT("The fallen player is a spectator on the server"), TheirState != nullptr && TheirState->IsSpectator());
				int32 Watchers = 0;
				bool bOutOfTheWay = true;
				for (TActorIterator<ASenniteSpectator> It(World); It; ++It) {
					++Watchers;
					const UCapsuleComponent* Body = It->GetCapsuleComponent();
					bOutOfTheWay &= Body->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore && Body->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Ignore
						&& Body->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore && !Body->GetGenerateOverlapEvents();
				}
				// the fallen player's weapons are in a crate left where they fell; nothing lies about
				int32 Left = 0;
				bool bHoldsTheirs = false, bWalkedThrough = false;
				for (TActorIterator<ASenniteCrate> It(World); It; ++It) {
					if (It->bLeft) {
						++Left;
						// the two free guns the joining player's crate gave them and neither bought one, and no rounds
						bHoldsTheirs = (It->Guns & 30) == 6 && !It->bRounds && !It->bOpen;
						const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(It->GetComponentByClass(UStaticMeshComponent::StaticClass()));
						bWalkedThrough = Body != nullptr && Body->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Overlap;
					}
				}
				Test->TestEqual(TEXT("The fallen player left one crate where they fell"), Left, 1);
				Test->TestTrue(TEXT("It holds the guns they had, closed"), bHoldsTheirs);
				Test->TestTrue(TEXT("Heroes walk through it"), bWalkedThrough);
				Test->TestEqual(TEXT("No weapon lies about where they fell"), GameCheck::LyingAbout(World), 0);
				Test->TestEqual(TEXT("The fallen player has one spectator on the server"), Watchers, 1);
				Test->TestTrue(TEXT("The spectator is in nobody's way: players, bullets and aim pass through it"), bOutOfTheWay);
				Test->TestTrue(TEXT("The host's HUD showed a hit marker"), bSawHitMarker);
				Test->TestTrue(TEXT("The host's HUD shows the kill"), Hud->LastDrawn.Contains(TEXT("kills=1 KILLS")));
				Test->TestTrue(TEXT("The host's HUD shows one player left"), Hud->LastDrawn.Contains(TEXT("alive=1 ALIVE")));
				Test->TestEqual(TEXT("The host's rifle spent four rounds"), Hero->CurrentWeapon ? Hero->CurrentWeapon->CurrentBulletCount : -1, GetDefault<ASenniteRifle>()->MagazineSize - 4);
				UE_LOG(LogCombatCheck, Display, TEXT("GAMECHECK host: kill after %d shots; kills %d, heroes left %d, their spectator %d, hud: %s"), Shots, State->KillCount, HeroCount(World),
					TheirState && TheirState->IsSpectator() ? 1 : 0, *FString::Join(Hud->LastDrawn, TEXT(" ")));
			}
			GameCheck::Shot(TEXT("Combat"), TEXT("host_after_kill.png"));
			Enter(EPhase::WaitForLeave, World);
			return false;

		case EPhase::WaitForLeave:
			// the joining player leaves when their half is done
			if (World->GetAuthGameMode() == nullptr || World->GetAuthGameMode()->GetNumPlayers() > 1) {
				return Elapsed(World) > 60.0 ? Fail(TEXT("The joining player never left")) : false;
			}
			UE_LOG(LogCombatCheck, Display, TEXT("GAMECHECK host: the joining player left"));
			UE_LOG(LogCombatCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
			return true;

		default:
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteCombatHostTest, "Sennite.Combat.Host", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteCombatHostTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCombatRun(this, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteCombatJoinerTest, "Sennite.Combat.Joiner", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteCombatJoinerTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCombatRun(this, false));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
