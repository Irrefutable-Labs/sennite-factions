#include "SenniteSounds.h"

#include "AmmunitionActor.h"
#include "BuildingActor.h"
#include "HealingActor.h"
#include "MaterialActor.h"
#include "ProjectileActor.h"
#include "SenniteCharacter.h"
#include "SenniteMatch.h"
#include "SennitePlayerState.h"
#include "SennitePublisher.h"
#include "SenniteStructure.h"
#include "StormActor.h"
#include "WeaponActor.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
	// the take of each sound played last, so the next is another
	TMap<FName, int32> LastTake;

	// A hero slower than StandingSpeed is standing; faster than RunningSpeed it is running. (Named so that no check's
	// own Run or Standing hides them where the files are compiled together.)
	constexpr float StandingSpeed = 20.0f;
	constexpr float RunningSpeed = 600.0f;
	// A fall faster than this, in cm a second, is one the hero's voice answers on landing.
	constexpr float HardLanding = 900.0f;
	// How long after the wallet connects its D-bucks and passes arriving are not heard as bought: they are what it had.
	constexpr double WalletSettles = 4.0;
	// Nothing is heard being built in a map's first seconds (what stands there is being laid), nor more than one piece at a time.
	constexpr double LaidAtStart = 3.0;
	constexpr double BuiltApart = 0.25;

	UWorld* WorldOf(const UObject* In)
	{
		UWorld* World = GEngine != nullptr && In != nullptr ? GEngine->GetWorldFromContextObject(In, EGetWorldErrorMode::ReturnNull) : nullptr;
		return World != nullptr && World->GetNetMode() != NM_DedicatedServer ? World : nullptr;
	}

	const TCHAR* Made(int32 MaterialType)
	{
		return MaterialType == 1 ? TEXT("stone") : MaterialType == 2 ? TEXT("steel") : TEXT("wood");
	}

	FName Named(const TCHAR* Kind, const TCHAR* Of)
	{
		return FName(*FString::Printf(TEXT("%s.%s"), Kind, Of));
	}
}

const FSenniteSound* USenniteSounds::Find(FName Name)
{
	for (const FSenniteSound& Each : GetDefault<USenniteSounds>()->Sounds) {
		if (Each.Name == Name) {
			return &Each;
		}
	}
	return nullptr;
}

USoundBase* USenniteSounds::Take(const FSenniteSound& Sound)
{
	const int32 Count = Sound.Takes.Num();
	if (Count == 0) {
		return nullptr;
	}
	int32 Index = 0;
	if (Count > 1) {
		int32& Last = LastTake.FindOrAdd(Sound.Name, INDEX_NONE);
		if (Last == INDEX_NONE) {
			Index = FMath::RandRange(0, Count - 1);
		}
		else {
			// any take but the last one
			Index = FMath::RandRange(0, Count - 2);
			if (Index >= Last) {
				++Index;
			}
		}
		Last = Index;
	}
	return Cast<USoundBase>(Sound.Takes[Index].TryLoad());
}

USoundAttenuation* USenniteSounds::Falloff(FName Carries)
{
	const USenniteSounds* Set = GetDefault<USenniteSounds>();
	const FSoftObjectPath& Path = Carries == TEXT("Far") ? Set->FarFalloff : Carries == TEXT("Shot") ? Set->ShotFalloff : Set->NearFalloff;
	return Cast<USoundAttenuation>(Path.TryLoad());
}

void USenniteSounds::Count(FName Name)
{
	++Counts.FindOrAdd(Name);
}

void USenniteSounds::Play(const UObject* In, FName Name, bool bOutlivesScreen)
{
	UWorld* World = WorldOf(In);
	const FSenniteSound* Sound = World != nullptr ? Find(Name) : nullptr;
	if (Sound == nullptr) {
		return;
	}
	if (USenniteSounds* Here = World->GetSubsystem<USenniteSounds>()) {
		Here->Count(Name);
	}
	if (USoundBase* Wave = Take(*Sound)) {
		if (bOutlivesScreen) {
			UGameplayStatics::SpawnSound2D(World, Wave, Sound->Volume, 1.0f, 0.0f, nullptr, bOutlivesScreen);
		}
		else {
			UGameplayStatics::PlaySound2D(World, Wave, Sound->Volume);
		}
	}
}

void USenniteSounds::PlayAt(const UObject* In, FName Name, const FVector& Where)
{
	UWorld* World = WorldOf(In);
	const FSenniteSound* Sound = World != nullptr ? Find(Name) : nullptr;
	if (Sound == nullptr) {
		return;
	}
	USenniteSounds* Here = World->GetSubsystem<USenniteSounds>();
	if (Here != nullptr) {
		Here->Count(Name);
	}
	if (USoundBase* Wave = Take(*Sound)) {
		UGameplayStatics::PlaySoundAtLocation(World, Wave, Where, FRotator::ZeroRotator, Sound->Volume, 1.0f, 0.0f,
			Here != nullptr ? Here->Falloff(Sound->Carries) : nullptr);
	}
}

void USenniteSounds::Voice(const ASenniteCharacter* Hero, FName Kind)
{
	if (Hero == nullptr) {
		return;
	}
	const USenniteSounds* Set = GetDefault<USenniteSounds>();
	const TCHAR* Whose = Set->WomensVoices.Contains(Hero->HeroId) ? TEXT("voice.f") : Set->MensVoices.Contains(Hero->HeroId) ? TEXT("voice.m") : nullptr;
	if (Whose != nullptr) {
		PlayAt(Hero, Named(Whose, *Kind.ToString()), Hero->GetActorLocation() + FVector(0.0, 0.0, 60.0));
	}
}

FName USenniteSounds::ButtonSound(FName Action)
{
	const FString Name = Action.ToString();
	if (Action.IsNone()) {
		return NAME_None;
	}
	if (Name == TEXT("prev") || Name == TEXT("next")) {
		// the arrows that turn to the next hero, skin or gun
		return TEXT("ui.turn");
	}
	if (Name.StartsWith(TEXT("tab:"))) {
		// a section of the store
		return TEXT("ui.tab");
	}
	if (Name.StartsWith(TEXT("hero:")) || Name.StartsWith(TEXT("gun:")) || Name.StartsWith(TEXT("skinsof:")) || Name.StartsWith(TEXT("skin:")) || Name == TEXT("swap:from") || Name == TEXT("swap:to")) {
		// one of a row
		return TEXT("ui.pick");
	}
	if (Name == TEXT("back") || Name == TEXT("leave") || Name == TEXT("stay") || Name == TEXT("resume")) {
		return TEXT("ui.back");
	}
	if (Name.StartsWith(TEXT("num:")) || Name.StartsWith(TEXT("key:"))) {
		// a digit of a keypad
		return TEXT("ui.key");
	}
	if (Name.EndsWith(TEXT("-")) || Name.EndsWith(TEXT("+")) || Name == TEXT("invert") || Name == TEXT("swap:flip")) {
		// a setting a step less or more
		return TEXT("ui.step");
	}
	for (const TCHAR* Done : { TEXT("choose"), TEXT("buy"), TEXT("buygun"), TEXT("buyskin"), TEXT("wear"), TEXT("takeoff"), TEXT("dbucks"), TEXT("wallet"), TEXT("price"), TEXT("trade"), TEXT("connect"), TEXT("start") }) {
		if (Name == Done) {
			// a thing done
			return TEXT("ui.confirm");
		}
	}
	return TEXT("ui.press");
}

void USenniteSounds::Button(const UObject* In, FName Action)
{
	// a press is heard to its end though the button changes the screen (PLAY, START, LEAVE)
	const FName Sound = ButtonSound(Action);
	if (!Sound.IsNone()) {
		Play(In, Sound, true);
	}
}

TArray<FSoftObjectPath> USenniteSounds::Takes(FName Name)
{
	const FSenniteSound* Sound = Find(Name);
	return Sound != nullptr ? Sound->Takes : TArray<FSoftObjectPath>();
}

int32 USenniteSounds::TakesNamed(FName Name)
{
	const FSenniteSound* Sound = Find(Name);
	return Sound != nullptr ? Sound->Takes.Num() : 0;
}

int32 USenniteSounds::TakesThatLoad(FName Name)
{
	int32 Count = 0;
	if (const FSenniteSound* Sound = Find(Name)) {
		for (const FSoftObjectPath& Each : Sound->Takes) {
			Count += Cast<USoundBase>(Each.TryLoad()) != nullptr ? 1 : 0;
		}
	}
	return Count;
}

TArray<FName> USenniteSounds::Names()
{
	TArray<FName> All;
	for (const FSenniteSound& Each : GetDefault<USenniteSounds>()->Sounds) {
		All.Add(Each.Name);
	}
	return All;
}

int32 USenniteSounds::Heard(const UObject* In, FName Name)
{
	const UWorld* World = WorldOf(In);
	const USenniteSounds* Here = World != nullptr ? World->GetSubsystem<USenniteSounds>() : nullptr;
	const int32* Count = Here != nullptr ? Here->Counts.Find(Name) : nullptr;
	return Count != nullptr ? *Count : 0;
}

int32 USenniteSounds::HeardBeginning(const UObject* In, const FString& With)
{
	const UWorld* World = WorldOf(In);
	const USenniteSounds* Here = World != nullptr ? World->GetSubsystem<USenniteSounds>() : nullptr;
	int32 Count = 0;
	if (Here != nullptr) {
		for (const TPair<FName, int32>& Each : Here->Counts) {
			Count += Each.Key.ToString().StartsWith(With) ? Each.Value : 0;
		}
	}
	return Count;
}

bool USenniteSounds::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USenniteSounds::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USenniteSounds, STATGROUP_Tickables);
}

void USenniteSounds::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() == NM_DedicatedServer) {
		return;
	}
	// every recording is loaded now, so the first step or shot does not wait for its file
	for (const FSenniteSound& Each : Sounds) {
		if (Each.Name.ToString().StartsWith(TEXT("air."))) {
			// a long one that goes round is loaded when a map asks for it
			continue;
		}
		for (const FSoftObjectPath& Path : Each.Takes) {
			if (UObject* Wave = Path.TryLoad()) {
				Loaded.Add(Wave);
			}
		}
	}
	for (const FSoftObjectPath* Path : { &NearFalloff, &FarFalloff, &ShotFalloff }) {
		if (UObject* Fades = Path->TryLoad()) {
			Loaded.Add(Fades);
		}
	}
	SpawnedHandle = InWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &USenniteSounds::Spawned));
	DestroyedHandle = InWorld.AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &USenniteSounds::Destroyed));
	// the air of the place, where a map has one
	const FName* Here = Ambience.Find(UGameplayStatics::GetCurrentLevelName(&InWorld, true));
	Air(Here != nullptr ? *Here : AmbienceElsewhere, AirLoop, AmbienceVolume);
}

void USenniteSounds::Deinitialize()
{
	if (UWorld* World = GetWorld()) {
		World->RemoveOnActorSpawnedHandler(SpawnedHandle);
		World->RemoveOnActorDestroyedHandler(DestroyedHandle);
	}
	Super::Deinitialize();
}

void USenniteSounds::Air(FName Name, TObjectPtr<UAudioComponent>& Loop, float Volume)
{
	UWorld* World = GetWorld();
	const FSenniteSound* Sound = Find(Name);
	USoundBase* Wave = Sound != nullptr && World != nullptr ? Take(*Sound) : nullptr;
	if (Wave == nullptr) {
		return;
	}
	Count(Name);
	Loop = UGameplayStatics::CreateSound2D(World, Wave, Sound->Volume * Volume, 1.0f, 0.0f, nullptr, false, false);
	if (Loop != nullptr) {
		Loop->FadeIn(2.0f, Sound->Volume * Volume);
	}
}

void USenniteSounds::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (World == nullptr || !World->HasBegunPlay() || World->GetNetMode() == NM_DedicatedServer) {
		return;
	}
	for (TActorIterator<ASenniteCharacter> Each(World); Each; ++Each) {
		HearHero(*Each, Heroes.FindOrAdd(*Each), DeltaTime);
	}
	for (auto Each = Heroes.CreateIterator(); Each; ++Each) {
		if (!Each.Key().IsValid()) {
			Each.RemoveCurrent();
		}
	}

	// a piece spawned a moment ago is built, unless it is the outline a player is still placing
	const double Now = World->GetTimeSeconds();
	for (int32 Index = NewPieces.Num() - 1; Index >= 0; --Index) {
		if (Now - NewPieces[Index].Value < 0.15) {
			continue;
		}
		const AActor* Piece = NewPieces[Index].Key.Get();
		const ABuildingActor* Built = Cast<ABuildingActor>(Piece);
		const bool bLately = BuiltLately.ContainsByPredicate([Now](const TPair<FVector, double>& Was) { return Now - Was.Value < BuiltApart; });
		if (Piece != nullptr && (Built == nullptr || !Built->IsPreview) && Now > LaidAtStart && !bLately) {
			PlayAt(this, TEXT("piece.place"), Piece->GetActorLocation());
			BuiltLately.Emplace(Piece->GetActorLocation(), Now);
		}
		NewPieces.RemoveAtSwap(Index);
	}
	BuiltLately.RemoveAll([Now](const TPair<FVector, double>& Was) { return Now - Was.Value > 1.0; });

	HearStorm(DeltaTime);
	HearMatch();
	HearWallet();
}

FName USenniteSounds::GroundUnder(const ASenniteCharacter* Hero) const
{
	const UWorld* World = GetWorld();
	const FVector At = Hero->GetActorLocation();
	const float Half = Hero->GetCapsuleComponent() != nullptr ? Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SenniteStep), false, Hero);
	if (World == nullptr || !World->LineTraceSingleByChannel(Hit, At, At - FVector(0.0, 0.0, Half + 60.0), ECC_Visibility, Params)) {
		return TEXT("grass");
	}
	if (const ABuildingActor* Piece = Cast<ABuildingActor>(Hit.GetActor())) {
		return Piece->MaterialType == 1 ? TEXT("rock") : Piece->MaterialType == 2 ? TEXT("metal") : TEXT("wood");
	}
	if (Cast<ASenniteStructure>(Hit.GetActor()) != nullptr) {
		return TEXT("wood");
	}
	if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Hit.GetComponent())) {
		const FString Name = Mesh->GetStaticMesh() != nullptr ? Mesh->GetStaticMesh()->GetName() : FString();
		if (Name.Contains(TEXT("Rock")) || Name.Contains(TEXT("Cliff")) || Name.Contains(TEXT("Stone"))) {
			return TEXT("rock");
		}
	}
	return TEXT("grass");
}

void USenniteSounds::HearHero(ASenniteCharacter* Hero, FSeen& Seen, float DeltaTime)
{
	const UCharacterMovementComponent* Move = Hero->GetCharacterMovement();
	const FVector At = Hero->GetActorLocation();
	const FVector Going = Hero->GetVelocity();
	const bool bInAir = Move != nullptr && Move->IsFalling();
	const bool bOnGround = Move != nullptr && Move->IsMovingOnGround();
	AActor* Held = Hero->CurrentWeapon;
	if (Seen.bFirst) {
		Seen.bFirst = false;
		Seen.At = At;
		Seen.Health = Hero->Health;
		Seen.bInAir = bInAir;
		Seen.Held = Held;
		return;
	}
	const float Half = Hero->GetCapsuleComponent() != nullptr ? Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
	const FVector Feet = At - FVector(0.0, 0.0, Half);

	// leaving the ground by a jump, and meeting it again
	if (!Seen.bInAir && bInAir && Going.Z > 150.0) {
		PlayAt(Hero, Named(TEXT("jump"), *GroundUnder(Hero).ToString()), Feet);
		if (FMath::FRand() < 0.5f) {
			Voice(Hero, TEXT("jump"));
		}
	}
	if (bInAir) {
		Seen.Fell = FMath::Max(Seen.Fell, float(-Going.Z));
	}
	else if (Seen.bInAir) {
		PlayAt(Hero, Named(TEXT("land"), *GroundUnder(Hero).ToString()), Feet);
		if (Seen.Fell > HardLanding) {
			Voice(Hero, TEXT("land"));
		}
		Seen.Fell = 0.0f;
		Seen.Walked = 0.0f;
	}

	// a step each stride walked on the ground (a hero moved a long way in one frame was put there, not walking)
	const float Speed = Going.Size2D();
	const float Went = FVector::Dist2D(At, Seen.At);
	// A hero that floats over the ground has no steps to hear. The owner, 2026-11-19: "He's floating, so he shouldn't
	// make footstep noises."
	if (bOnGround && Speed > StandingSpeed && Went < 300.0f && !Hero->bHeroFloats) {
		const bool bRunning = Speed > RunningSpeed;
		const float Stride = bRunning ? RunStride : WalkStride;
		Seen.Walked += Went;
		if (Seen.Walked >= Stride) {
			Seen.Walked = 0.0f;
			PlayAt(Hero, FName(*FString::Printf(TEXT("step.%s.%s"), *GroundUnder(Hero).ToString(), bRunning ? TEXT("run") : TEXT("walk"))), Feet);
		}
	}
	else if (Speed <= StandingSpeed) {
		// standing: the first step comes half a stride after setting off
		Seen.Walked = WalkStride * 0.5f;
	}

	// hurt, and fallen
	if (Hero->Health < Seen.Health - 0.5f) {
		if (Hero->Health <= 0.0f) {
			PlayAt(Hero, TEXT("fell"), At);
			Voice(Hero, TEXT("pain"));
		}
		else {
			PlayAt(Hero, TEXT("body.hit"), At);
			Voice(Hero, TEXT("hit"));
		}
	}

	// another weapon in hand
	if (Held != Seen.Held.Get() && Held != nullptr) {
		PlayAt(Hero, TEXT("weapon.switch"), At);
	}

	Seen.At = At;
	Seen.Health = Hero->Health;
	Seen.bInAir = bInAir;
	Seen.Held = Held;
}

void USenniteSounds::Spawned(AActor* Actor)
{
	if (Actor == nullptr || GetWorld() == nullptr) {
		return;
	}
	if (Actor->IsA<ABuildingActor>() || Actor->IsA<ASenniteStructure>()) {
		NewPieces.Emplace(Actor, GetWorld()->GetTimeSeconds());
	}
	else if (Actor->IsA<AStormActor>() && GetDefault<AStormActor>()->bEnabled) {
		Play(this, TEXT("storm.alarm"));
	}
}

void USenniteSounds::Destroyed(AActor* Actor)
{
	UWorld* World = GetWorld();
	if (Actor == nullptr || World == nullptr || World->bIsTearingDown || !World->HasBegunPlay()) {
		return;
	}
	const FVector At = Actor->GetActorLocation();
	if (const ABuildingActor* Piece = Cast<ABuildingActor>(Actor)) {
		// It fell: on the server because its health ran out, on another machine because the server took it away.
		// One taken away whole was already heard going (piece.remove).
		const bool bFell = !Piece->IsPreview && !Piece->bTakenAway && (Piece->HasAuthority() ? Piece->Health <= 0.0f : Piece->GetLocalRole() == ROLE_SimulatedProxy);
		if (bFell) {
			PlayAt(this, Named(TEXT("piece.break"), Made(Piece->MaterialType)), At);
		}
	}
	else if (Actor->IsA<ASenniteStructure>()) {
		PlayAt(this, TEXT("piece.break.wood"), At);
	}
	else if (const AMaterialActor* Node = Cast<AMaterialActor>(Actor)) {
		PlayAt(this, Named(TEXT("piece.break"), Made(Node->MaterialType)), At);
	}
	else if (const AProjectileActor* Bullet = Cast<AProjectileActor>(Actor)) {
		// where a bullet ended is where it landed, if something stands there: one that flew out its range ended in the air
		const FVector Along = Bullet->GetVelocity().IsNearlyZero() ? Bullet->GetActorForwardVector() : Bullet->GetVelocity().GetSafeNormal();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SenniteBulletLanded), false, Bullet);
		Params.AddIgnoredActor(Bullet->WeaponHolder);
		if (World->LineTraceSingleByChannel(Hit, At - Along * 200.0, At + Along * 150.0, ECC_Visibility, Params)) {
			if (const ABuildingActor* Struck = Cast<ABuildingActor>(Hit.GetActor())) {
				PlayAt(this, Named(TEXT("piece.hit"), Made(Struck->MaterialType)), Hit.ImpactPoint);
			}
			else if (Hit.GetActor() != nullptr && Hit.GetActor()->IsA<AMaterialActor>()) {
				PlayAt(this, TEXT("harvest"), Hit.ImpactPoint);
			}
			else if (Cast<ASenniteCharacter>(Hit.GetActor()) == nullptr) {
				// a hero struck is heard from its health
				PlayAt(this, TEXT("bolt.hit"), Hit.ImpactPoint);
			}
		}
	}
	else if (Actor->IsA<AAmmunitionActor>()) {
		PlayAt(this, TEXT("pickup.rounds"), At);
	}
	else if (const AHealingActor* Item = Cast<AHealingActor>(Actor)) {
		// one lying in the world, taken: not one used up in a hand
		if (Item->Holder == nullptr) {
			PlayAt(this, TEXT("pickup.item"), At);
		}
	}
	else if (const AWeaponActor* Gun = Cast<AWeaponActor>(Actor)) {
		if (Gun->Holder == nullptr) {
			PlayAt(this, TEXT("pickup.weapon"), At);
		}
	}
}

void USenniteSounds::HearStorm(float DeltaTime)
{
	UWorld* World = GetWorld();
	const AStormActor* Storm = AStormActor::Find(World);
	const APlayerController* Own = World->GetFirstPlayerController();
	const APawn* Me = Own != nullptr ? Own->GetPawn() : nullptr;
	// outside the storm's wall the player stands in it
	const bool bIn = Storm != nullptr && Me != nullptr && FVector::Dist2D(Me->GetActorLocation(), Storm->GetActorLocation()) > Storm->GetRadius();
	if (bIn == bInStorm) {
		return;
	}
	bInStorm = bIn;
	if (bIn) {
		if (StormLoop == nullptr) {
			Air(TEXT("air.storm"), StormLoop, 1.0f);
		}
		else {
			StormLoop->FadeIn(1.5f);
		}
	}
	else if (StormLoop != nullptr) {
		StormLoop->FadeOut(1.5f, 0.0f);
	}
}

void USenniteSounds::HearMatch()
{
	UWorld* World = GetWorld();
	const ASenniteMatch* Match = ASenniteMatch::Find(World);
	const uint8 State = Match != nullptr ? uint8(Match->State) : uint8(EMatchState::Waiting);
	if (!bMatchSeen) {
		// what the match was when this machine came in is not news
		bMatchSeen = true;
		MatchState = State;
		return;
	}
	if (State == MatchState) {
		return;
	}
	MatchState = State;
	if (Match == nullptr) {
		return;
	}
	if (Match->State == EMatchState::Playing) {
		Play(this, TEXT("match.start"));
	}
	else if (Match->State == EMatchState::Over) {
		const APlayerController* Own = World->GetFirstPlayerController();
		const ASennitePlayerState* Mine = Own != nullptr ? Own->GetPlayerState<ASennitePlayerState>() : nullptr;
		const bool bWon = Mine != nullptr && !Mine->FactionId.IsEmpty() && Match->Outcome == Mine->FactionId;
		Play(this, bWon ? TEXT("match.won") : TEXT("match.lost"));
	}
}

void USenniteSounds::HearWallet()
{
	UWorld* World = GetWorld();
	const UGameInstance* Game = World->GetGameInstance();
	const USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
	if (Server == nullptr) {
		return;
	}
	const int32 Has = Server->OwnedHeroes.Num() + Server->OwnedSkins.Num();
	static double ConnectedAt = -1000.0;
	const double Now = FPlatformTime::Seconds();
	if (bWalletSeen) {
		if (Server->bConnected && !bConnected) {
			ConnectedAt = Now;
			Play(this, TEXT("wallet.connected"));
		}
		else if (Server->bConnected && Now - ConnectedAt > WalletSettles) {
			if (Has > Owned) {
				Play(this, TEXT("ui.bought"));
			}
			else if (Server->DBucks > DBucks) {
				Play(this, TEXT("ui.coins"));
			}
		}
	}
	bWalletSeen = true;
	bConnected = Server->bConnected;
	DBucks = Server->DBucks;
	Owned = Has;
}
