// Fill out your copyright notice in the Description page of Project Settings.

#include "SenniteHUD.h"
#include "Engine/Canvas.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"

ASenniteHUD::ASenniteHUD()
{
}

void ASenniteHUD::DrawHUD()
{
	Super::DrawHUD();
	DrawCrosshair();
}

void ASenniteHUD::BeginPlay()
{
	Super::BeginPlay();
	/*if (MainMenuWidgetClass != nullptr) {
		CurrentWidget = CreateWidget<UUserWidget>(GetWorld(), MainMenuWidgetClass);

		if (CurrentWidget)
		{
			CurrentWidget->AddToViewport();
		}
	}*/
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (PlayerController) {
		// change the flags back to their default values after going from main menu to fortnite clone
		PlayerController->bShowMouseCursor = false;
		PlayerController->bEnableClickEvents = false;
		PlayerController->bEnableMouseOverEvents = false; 
		PlayerController->SetInputMode(FInputModeGameOnly());
	}
	DrawGameUI();
}

void ASenniteHUD::DrawCrosshair() {
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

void ASenniteHUD::DrawHitMarker() {
	if (HitMarkerWidgetClass != nullptr)
	{
		CurrentWidget = CreateWidget<UUserWidget>(GetWorld(), HitMarkerWidgetClass);
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "hitmarkerclass is not null");
		if (CurrentWidget)
		{
			//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "current widget is not null");
			CurrentWidget->AddToViewport();
			// TODO for later: Implement your own UUserWidget class to retrieve animation from a blueprint and play the animation here
		}
	}
}

void ASenniteHUD::DrawBloodSplash() {
	if (BloodEffectWidgetClass != nullptr) {
		CurrentWidget = CreateWidget<UUserWidget>(GetWorld(), BloodEffectWidgetClass);
		if (CurrentWidget)
		{
			CurrentWidget->AddToViewport();
			// TODO for later: Implement your own UUserWidget class to retrieve animation from a blueprint and play the animation here
		}
	}
}
void ASenniteHUD::DrawGameUI() {
	// remove main screen widget
	/*if (CurrentWidget != nullptr) {
		CurrentWidget->RemoveFromParent();
	}*/
	if (HealthWidgetClass != nullptr)
	{
		CurrentWidget = CreateWidget<UUserWidget>(GetWorld(), HealthWidgetClass);

		if (CurrentWidget)
		{
			CurrentWidget->AddToViewport();
		}
	}

	if (MaterialsWidgetClass != nullptr)
	{
		CurrentWidget = CreateWidget<UUserWidget>(GetWorld(), MaterialsWidgetClass);

		if (CurrentWidget)
		{
			CurrentWidget->AddToViewport();
		}
	}

	if (ItemsWidgetClass != nullptr)
	{
		CurrentWidget = CreateWidget<UUserWidget>(GetWorld(), ItemsWidgetClass);

		if (CurrentWidget)
		{
			CurrentWidget->AddToViewport();
		}
	}

	if (KillsWidgetClass != nullptr)
	{
		CurrentWidget = CreateWidget<UUserWidget>(GetWorld(), KillsWidgetClass);

		if (CurrentWidget)
		{
			CurrentWidget->AddToViewport();
		}
	}

	if (CountWidgetClass != nullptr)
	{
		CurrentWidget = CreateWidget<UUserWidget>(GetWorld(), CountWidgetClass);

		if (CurrentWidget)
		{
			CurrentWidget->AddToViewport();
		}
	}
	if (BuildingHotkeysWidgetClass != nullptr) {
		CurrentWidget = CreateWidget<UUserWidget>(GetWorld(), BuildingHotkeysWidgetClass);

		if (CurrentWidget)
		{
			CurrentWidget->AddToViewport();
		}
	}
}

void ASenniteHUD::DrawSettingsMenu() {
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (PlayerController) {
		// change the flags back to their default values after going from settings menu to fortnite clone
		// or change the flags to ui mode if going from fortnite clone to settings menu
		if (PlayerController->bShowMouseCursor) {
			PlayerController->bShowMouseCursor = false;
			PlayerController->bEnableClickEvents = false;
			PlayerController->bEnableMouseOverEvents = false; 
			PlayerController->SetInputMode(FInputModeGameOnly());
			if (SettingsMenuWidget) {
				SettingsMenuWidget->RemoveFromParent();
			}
		}
		else {
			PlayerController->bShowMouseCursor = true;
			PlayerController->bEnableClickEvents = true;
			PlayerController->bEnableMouseOverEvents = true;
			PlayerController->SetInputMode(FInputModeGameAndUI());
			if (SettingsMenuWidgetClass != nullptr)
			{
				SettingsMenuWidget = CreateWidget<UUserWidget>(GetWorld(), SettingsMenuWidgetClass);

				if (SettingsMenuWidget)
				{
					SettingsMenuWidget->AddToViewport();
				}
			}
		}
	}
}