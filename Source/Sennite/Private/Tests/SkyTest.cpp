// Sky check. Run in the game on the arena map, once for each sky (-Sky=DAY, DUSK, NIGHT):
// - which sky a match has comes from its arena's seed, each as often as its weight says over many seeds,
//   and the same seed always gives the same sky;
// - the world is under the sky asked for: its sun stands at that sky's height with that sky's light,
//   and the stars are out only under a sky that has them, on a dome inside the sky's own;
// - the sky is still drawn on a dome a phone draws it on;
// - pictures across the land and up at the sky land in Saved/Sky/, for eyes.
//
// Tools/check_sky.sh runs this for every sky and reads the result.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameCheck.h"
#include "SenniteArena.h"
#include "SenniteCharacter.h"
#include "SenniteSky.h"
#include "Camera/CameraActor.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkyCheck, Log, All);

namespace
{
	class FSkyRun : public IAutomationLatentCommand
	{
	public:
		explicit FSkyRun(FAutomationTestBase* InTest) : Test(InTest), RealStart(FPlatformTime::Seconds()) {}

		virtual bool Update() override;

	private:
		enum class EPhase { Wait, Across, Turn, Up, Done };

		bool Fail(const FString& Why) { Test->AddError(Why); return true; }

		void Look(APlayerController* Controller, const ASenniteCharacter* Hero, double Pitch)
		{
			if (Onlooker.IsValid()) {
				const FRotator Facing(Pitch, Yaw, 0.0);
				Onlooker->SetActorLocationAndRotation(Hero->GetActorLocation() + FVector(0.0, 0.0, 120.0) - FRotator(0.0, Yaw, 0.0).Vector() * 420.0, Facing);
				Controller->SetViewTarget(Onlooker.Get());
			}
		}

		FAutomationTestBase* Test;
		double RealStart;
		EPhase Phase = EPhase::Wait;
		double PhaseStart = 0.0;
		FString Name;
		double Yaw = 0.0;
		TWeakObjectPtr<ACameraActor> Onlooker;
	};

	bool FSkyRun::Update()
	{
		UWorld* World = GameCheck::FindGameWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
		if (Phase == EPhase::Wait) {
			TActorIterator<ASenniteArena> Arena(World);
			if (Hero == nullptr || !Hero->GetCharacterMovement()->IsMovingOnGround() || !Arena || !Arena->IsBuilt() || USenniteSky::Now(World).IsNone()) {
				return FPlatformTime::Seconds() - RealStart > 90.0 ? Fail(TEXT("No hero on a built arena under a sky after 90 s")) : false;
			}
			const TArray<FSenniteSkyLook>& Skies = GetDefault<USenniteSky>()->Skies;
			Test->TestTrue(TEXT("There is more than one sky"), Skies.Num() >= 2);
			// over many seeds each sky comes as often as its weight says, and a seed always gives the same one
			float Total = 0.0f;
			for (const FSenniteSkyLook& Each : Skies) {
				Total += Each.Weight;
			}
			TMap<FName, int32> Seen;
			const int32 Seeds = 6000;
			for (int32 Seed = 0; Seed < Seeds; ++Seed) {
				const FSenniteSkyLook* Look = USenniteSky::For(Seed * 7919 + 13);
				if (Look == nullptr || Look != USenniteSky::For(Seed * 7919 + 13)) {
					return Fail(TEXT("A seed did not give one sky, the same each time"));
				}
				++Seen.FindOrAdd(Look->Name);
			}
			FString Shares;
			for (const FSenniteSkyLook& Each : Skies) {
				const float Share = float(Seen.FindRef(Each.Name)) / Seeds;
				Test->TestTrue(FString::Printf(TEXT("%s comes as often as its weight says"), *Each.Name.ToString()), FMath::Abs(Share - Each.Weight / Total) < 0.03f);
				Shares += FString::Printf(TEXT(" %s %.0f%%"), *Each.Name.ToString(), Share * 100.0f);
			}
			// this world's sky: the one asked for, or its seed's
			const FSenniteSkyLook* Want = USenniteSky::For(Arena->GetPlan().Seed);
			FString Asked;
			if (FParse::Value(FCommandLine::Get(), TEXT("Sky="), Asked)) {
				Want = USenniteSky::Named(FName(*Asked));
			}
			if (Want == nullptr) {
				return Fail(TEXT("The sky asked for is not listed"));
			}
			Name = Want->Name.ToString();
			Test->TestEqual(TEXT("The world is under the sky asked for"), USenniteSky::Now(World), Want->Name);
			TActorIterator<ADirectionalLight> Sun(World);
			if (!Sun) {
				return Fail(TEXT("The arena has no sun"));
			}
			const float Height = -Sun->GetActorRotation().Pitch;
			const float Light = Sun->GetLightComponent()->Intensity;
			if (!Want->bAsBuilt) {
				Test->TestTrue(TEXT("The sun stands at this sky's height"), FMath::Abs(Height - Want->SunHeight) < 0.5f);
				Test->TestTrue(TEXT("The sun has this sky's light"), FMath::Abs(Light - Want->SunIntensity) < 0.01f);
			}
			Test->TestTrue(TEXT("The one light that casts shadows still does"), Sun->GetLightComponent()->CastShadows);
			int32 StarDomes = 0;
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It) {
				StarDomes += It->ActorHasTag(TEXT("SkyStars")) ? 1 : 0;
			}
			Test->TestEqual(TEXT("The stars are out under a sky that has them, and only then"), StarDomes, Want->Stars > 0.0f && !Want->bAsBuilt ? 1 : 0);
			Test->TestTrue(TEXT("The sky is drawn on a dome a phone draws it on"), GameCheck::HasSkyDome(World));
			UE_LOG(LogSkyCheck, Display, TEXT("GAMECHECK sky: %s; sun %.0f degrees up, light %.2f; star domes %d; over %d seeds:%s"), *Name, Height, Light, StarDomes, Seeds, *Shares);
			// across the land, the sun or moon ahead and to one side
			// from a camera of the check's own, standing behind the hero at head height
			const FVector ToSun = -Sun->GetActorForwardVector();
			Yaw = ToSun.Rotation().Yaw + 35.0;
			Onlooker = World->SpawnActor<ACameraActor>();
			Look(Controller, Hero, 4.0);
			Phase = EPhase::Across;
			PhaseStart = FPlatformTime::Seconds();
			return false;
		}
		if (FPlatformTime::Seconds() - PhaseStart < 1.5) {
			return false;
		}
		if (Phase == EPhase::Across) {
			// the picture is taken at the end of the frame: the camera turns after it
			GameCheck::Shot(TEXT("Sky"), *FString::Printf(TEXT("%s_across.png"), *Name));
			Phase = EPhase::Turn;
			PhaseStart = FPlatformTime::Seconds() - 1.2;
			return false;
		}
		if (Phase == EPhase::Turn) {
			Yaw += 150.0;
			Look(Controller, Hero, 38.0);
			Phase = EPhase::Up;
			PhaseStart = FPlatformTime::Seconds();
			return false;
		}
		if (Phase == EPhase::Up) {
			GameCheck::Shot(TEXT("Sky"), *FString::Printf(TEXT("%s_up.png"), *Name));
			UE_LOG(LogSkyCheck, Display, TEXT("GAMECHECK finished"));
			Phase = EPhase::Done;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenniteSkyTest, "Sennite.Sky.Looks",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSenniteSkyTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSkyRun(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
