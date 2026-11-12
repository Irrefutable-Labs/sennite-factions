// Fill out your copyright notice in the Description page of Project Settings.

#include "LobbyHUD.h"
#include "Engine/Canvas.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"

ALobbyHUD::ALobbyHUD()
{

}

void ALobbyHUD::DrawHUD()
{
	Super::DrawHUD();
	DrawCrosshair();
}


void ALobbyHUD::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (PlayerController) {
		// change the flags back to their default values after going from main menu to lobby
		PlayerController->bShowMouseCursor = false;
		PlayerController->bEnableClickEvents = false;
		PlayerController->bEnableMouseOverEvents = false;
		PlayerController->SetInputMode(FInputModeGameOnly());
	}
	DrawGameUI();
}

void ALobbyHUD::DrawCrosshair() {
	// four ticks around the centre of the screen, with a gap at the aim point
	const float X = Canvas->ClipX * 0.5f;
	const float Y = Canvas->ClipY * 0.5f;
	const float Gap = 5.0f;
	const float Tick = 9.0f;
	const float Thickness = 2.0f;
	const FLinearColor Colour = FLinearColor::White;
	DrawLine(X - Gap - Tick, Y, X - Gap, Y, Colour, Thickness);
	DrawLine(X + Gap, Y, X + Gap + Tick, Y, Colour, Thickness);
	DrawLine(X, Y - Gap - Tick, X, Y - Gap, Colour, Thickness);
	DrawLine(X, Y + Gap, X, Y + Gap + Tick, Colour, Thickness);
}

void ALobbyHUD::DrawGameUI(){
	if (PlayersJoinedCountWidgetClass != nullptr)
	{
		CurrentWidget = CreateWidget<UUserWidget>(GetWorld(), PlayersJoinedCountWidgetClass);

		if (CurrentWidget)
		{
			CurrentWidget->AddToViewport();
		}
	}

	if (WaitingForPlayersWidgetClass != nullptr)
	{
		CurrentWidget = CreateWidget<UUserWidget>(GetWorld(), WaitingForPlayersWidgetClass);

		if (CurrentWidget)
		{
			CurrentWidget->AddToViewport();
		}
	}
}