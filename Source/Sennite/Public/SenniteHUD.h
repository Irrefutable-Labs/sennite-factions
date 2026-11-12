// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SenniteHUD.generated.h"

class UUserWidget;
class UTexture2D;
class UWidgetAnimation;

/**
 * 
 */
UCLASS()
class SENNITE_API ASenniteHUD : public AHUD
{
	GENERATED_BODY()
	
public:
	ASenniteHUD();

	/* Primary draw call for the HUD */
	virtual void DrawHUD() override;

	virtual void BeginPlay() override;

	void DrawCrosshair();

	void DrawHitMarker();

	void DrawGameUI();

	void DrawBloodSplash();

	void DrawSettingsMenu();

private:

	UPROPERTY(EditAnywhere, Category = "Health")
	TSubclassOf<UUserWidget> HealthWidgetClass;

	UPROPERTY(EditAnywhere, Category = "Materials")
	TSubclassOf<UUserWidget> MaterialsWidgetClass;

	UPROPERTY(EditAnywhere, Category = "Items")
	TSubclassOf<UUserWidget> ItemsWidgetClass;

	UPROPERTY(EditAnywhere, Category = "Items")
	TSubclassOf<UUserWidget> KillsWidgetClass;

	UPROPERTY(EditAnywhere, Category = "HitMarker")
	TSubclassOf<UUserWidget> HitMarkerWidgetClass;

	UPROPERTY(EditAnywhere, Category = "MainMenu")
	TSubclassOf<UUserWidget> MainMenuWidgetClass;

	UPROPERTY(EditAnywhere, Category = "Count")
	TSubclassOf<UUserWidget> CountWidgetClass;

	UPROPERTY(EditAnywhere, Category = "BloodEffect")
	TSubclassOf<UUserWidget> BloodEffectWidgetClass;

	UPROPERTY(EditAnywhere, Category = "SettingsMenu")
	TSubclassOf<UUserWidget> SettingsMenuWidgetClass;

	UPROPERTY(EditAnywhere, Category = "BuildingHotkeys")
	TSubclassOf<UUserWidget> BuildingHotkeysWidgetClass;

	UPROPERTY(EditAnywhere, Category = "Widget")
	UUserWidget* CurrentWidget;

	UPROPERTY(EditAnywhere, Category = "Widget")
	UUserWidget* SettingsMenuWidget;

	UPROPERTY(EditAnywhere, Category = "Animations")
	UWidgetAnimation* HitMarkerAnimation;

};
