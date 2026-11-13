// Fill out your copyright notice in the Description page of Project Settings.

#include "SennitePlayerController.h"
#include "SennitePlayerState.h"
#include "SenniteCharacter.h"
#include "SenniteSpectator.h"
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
		TArray<AActor*> StormActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AStormActor::StaticClass(), StormActors);
		if (StormActors.Num() > 0) {
			if (StormActors[0] != nullptr) {
				CurrentStorm = Cast<AStormActor>(StormActors[0]);
			}
		}
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

void ASennitePlayerController::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);
	if (HasAuthority()) {
		if (SpawnAsSpectator && PlayerState && !PlayerState->IsSpectator()) {
			ChangeState(NAME_Spectating);
			ClientGotoState(NAME_Spectating);
			APawn* PlayerPawn = Cast<APawn>(GetWorld()->SpawnActor<ASenniteSpectator>(PlayerSpectatorClass, SpectatorStart(), FRotator(0.0, GetControlRotation().Yaw, 0.0)));
			//SetSpectatorPawn(Pawn);
			Possess(PlayerPawn);
			Cast<ASennitePlayerState>(PlayerState)->SetIsSpectator(true); // ORDER MATTERS HERE, HAS TO BE SET AFTER POSSESSING A PAWN
		}
	}
}

void ASennitePlayerController::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASennitePlayerController, PlayerCount);
	DOREPLIFETIME(ASennitePlayerController, SpectatorCount);
}

void ASennitePlayerController::ServerSwitchToSpectatorMode_Implementation() {
	if (PlayerState) {
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString("Player state is not null"));
		//Cast<ASennitePlayerState>(PlayerState)->SetIsSpectator(true);
		ChangeState(NAME_Spectating);
		ClientGotoState(NAME_Spectating);
		APawn* PlayerPawn = Cast<APawn>(GetWorld()->SpawnActor<ASenniteSpectator>(PlayerSpectatorClass, SpectatorStart(), FRotator(0.0, GetControlRotation().Yaw, 0.0)));
		//SetSpectatorPawn(Pawn);
		Possess(PlayerPawn);
		Cast<ASennitePlayerState>(PlayerState)->SetIsSpectator(true); // ORDER MATTERS HERE, HAS TO BE SET AFTER POSSESSING A PAWN
		/*FString LogMsg = FString("switch to spectator mode ") + FString::FromInt(GetNetMode());
		UE_LOG(LogMyGame, Warning, TEXT("%s"), *LogMsg);
		LogMsg = FString("test ") + FString::FromInt(GetNetMode());
		UE_LOG(LogMyGame, Warning, TEXT("%s"), *LogMsg);*/
		if (Cast<ASennitePlayerState>(PlayerState)->IsSpectator()) {
			/*LogMsg = FString("What a spectator");
			UE_LOG(LogMyGame, Warning, TEXT("%s"), *LogMsg);*/
		}
		else {
			/*LogMsg = FString("What not a spectator");
			UE_LOG(LogMyGame, Warning, TEXT("%s"), *LogMsg);*/
		}
	}
}

bool ASennitePlayerController::ServerSwitchToSpectatorMode_Validate() {
	return true;
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