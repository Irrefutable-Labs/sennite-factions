// Fill out your copyright notice in the Description page of Project Settings.

#include "SennitePlayerController.h"
#include "SennitePlayerState.h"
#include "SenniteCharacter.h"
#include "SenniteSpectator.h"
#include "SenniteMatch.h"
#include "SenniteTouch.h"
#include "SenniteBaseEditor.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "StormActor.h"
#include "Kismet/GameplayStatics.h"

ASennitePlayerController::ASennitePlayerController() {
	/*ASennitePlayerState* State= Cast<ASennitePlayerState>(GetPlayerState());
	if (State) {
		State->SetIsSpectator(true);
	}*/
	//PlayerState->SetIsSpectator(true);
	 PlayerSpectatorClass = ASenniteSpectator::StaticClass();
	 PlayerCount = 0;
	 SpectatorCount = 1;
	 Initialized = false;
	 PlayerSessionId = FString("");
}

void ASennitePlayerController::BeginPlay() {
	Super::BeginPlay();
	if (HasAuthority()) {
		CurrentStorm = AStormActor::Find(GetWorld());
	}
}

FVector ASennitePlayerController::SpectatorStart() const {
	// a little above where the player's hero is, or was
	if (const APawn* Body = GetPawn()) {
		return Body->GetActorLocation() + FVector(0.0, 0.0, 300.0);
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	return ViewLocation;
}

USenniteTouchControls* ASennitePlayerController::GetTouchControls() {
	if (TouchControls == nullptr && IsLocalPlayerController() && USenniteTouchControls::Wanted()) {
		TouchControls = NewObject<USenniteTouchControls>(this);
		TouchControls->Bind(this);
	}
	return TouchControls;
}

bool ASennitePlayerController::InputTouch(const FTouchId TouchId, const ETouchType::Type Type, const FVector2D& TouchLocation, const float Force, const uint64 Timestamp) {
	// in play the stick and buttons; in the planning view of a base, the editor's buttons and the map
	USenniteTouchControls* Controls = GetTouchControls();
	if (Controls != nullptr) {
		return Controls->Touch(TouchId, Type, TouchLocation);
	}
	return Super::InputTouch(TouchId, Type, TouchLocation, Force, Timestamp);
}

void ASennitePlayerController::CreateTouchInterface() {
}

void ASennitePlayerController::PlayerTick(float DeltaTime) {
	// the stick is read before the frame's input is: what it says moves the hero this frame
	if (TouchControls != nullptr) {
		TouchControls->Tick();
	}
	Super::PlayerTick(DeltaTime);
}

void ASennitePlayerController::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);
	// a player the server has marked to join as a watcher
	if (HasAuthority() && SpawnAsSpectator) {
		BecomeSpectator();
	}
}

void ASennitePlayerController::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASennitePlayerController, PlayerCount);
	DOREPLIFETIME(ASennitePlayerController, SpectatorCount);
}

void ASennitePlayerController::BecomeSpectator() {
	if (!HasAuthority() || PlayerState == nullptr || PlayerState->IsSpectator()) {
		return;
	}
	// where to watch from is read while the player still has their hero
	const FVector Where = SpectatorStart();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASenniteSpectator* Watcher = GetWorld()->SpawnActor<ASenniteSpectator>(PlayerSpectatorClass, Where, FRotator(0.0, GetControlRotation().Yaw, 0.0), Spawn);
	if (Watcher == nullptr) {
		UE_LOG(LogTemp, Error, TEXT("No spectator could be made for %s; the player keeps what they had"), *GetName());
		return;
	}
	ChangeState(NAME_Spectating);
	ClientGotoState(NAME_Spectating);
	Possess(Watcher);
	PlayerState->SetIsSpectator(true); // has to be set after possessing a pawn
}

void ASennitePlayerController::ServerEnterMatch_Implementation(const FString& Ticket) {
	if (ASenniteMatch* Match = ASenniteMatch::Find(GetWorld())) {
		Match->Enter(this, Ticket.Left(128));
	}
}

void ASennitePlayerController::ClientMatchSaid_Implementation(const FString& Said) {
	if (ASenniteMatch* Match = ASenniteMatch::Find(GetWorld())) {
		Match->Said = Said;
	}
}

void ASennitePlayerController::ServerGetNumPlayers_Implementation() {
	int Count = 0;
	//FLocalPlayerContext Context(this);
	//UWorld* World = Context.GetWorld();
	/*AGameState* GameState = Cast<AGameState>(GetWorld()->GetGameState());
	if (GameState) {
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("Gamestate is not null"));
		TArray<APlayerState*> States = GameState->PlayerArray;
		for (int i = 0; i < States.Num(); i++) {
			if (States[i]->IsSpectator() == false) {
				Count++;
			}
		}
	}
	else {
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("Gamestate is null"));
	}
	PlayerCount = Count;*/
}

bool ASennitePlayerController::ServerGetNumPlayers_Validate() {
	return true;
}

void ASennitePlayerController::ServerGetNumSpectators_Implementation() {
	int Count = 0;
	//FLocalPlayerContext Context(this);
	//UWorld* World = Context.GetWorld();
	/*AGameState* GameState = Cast<AGameState>(GetWorld()->GetGameState);
	if (GameState) {
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("Gamemode is not null"));
		TArray<APlayerState*> States = GameState->PlayerArray;
		for (int i = 0; i < States.Num(); i++) {
			if (States[i]->IsSpectator() == true) {
				Count++;
			}
		}
	}
	SpectatorCount = Count;*/
}

bool ASennitePlayerController::ServerGetNumSpectators_Validate() {
	return true;
}

void ASennitePlayerController::ServerUpdateCountAfterDeath_Implementation() {
	
}

bool ASennitePlayerController::ServerUpdateCountAfterDeath_Validate() {
	return true;
}

int ASennitePlayerController::GetKillCount() {
	if (PlayerState) {
		ASennitePlayerState* State = Cast<ASennitePlayerState>(PlayerState);
		if (State) {
			return State->KillCount;
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
}

int ASennitePlayerController::GetPlayerCount() {
	ServerGetNumPlayers();
	return PlayerCount;
}

int ASennitePlayerController::GetSpectatorCount() {
	ServerGetNumSpectators();
	return SpectatorCount;
}