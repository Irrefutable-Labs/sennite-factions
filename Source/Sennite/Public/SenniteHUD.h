// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SenniteHUD.generated.h"

class UUserWidget;
class UTexture2D;
class UWidgetAnimation;

/** What the HUD shows of the player, read from the game each frame. */
struct FSenniteHudReadout
{
	bool bValid = false;
	float Health = 0.0f;
	float Shield = 0.0f;
	int32 Materials[3] = { 0, 0, 0 };
	int32 CurrentMaterial = 0;
	bool bBuildMode = false;
	FString BuildMode;
	bool bHoldingWeapon = false;
	int32 WeaponType = -1;
	int32 Clip = 0;
	int32 Reserve = 0;
	int32 Kills = 0;
	int32 Alive = 0;
};

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

	/** Health and shield, the held weapon's rounds, the building materials, kills and players left: drawn on the canvas, sized to the screen. */
	void DrawStatus();

	/** The player's state as the HUD shows it. */
	FSenniteHudReadout ReadState() const;

	/** What the last frame read, and each line of text it sent to the canvas, as "name=text" (health=100, ammo=30, material0=90, mode=WALL, ...). */
	FSenniteHudReadout LastReadout;
	TArray<FString> LastDrawn;

private:
	/** A line of text at a point size for a 720-line screen, scaled to the screen; recorded under its name. */
	void Label(const TCHAR* Name, const FString& Text, float X, float Y, int32 Points, const FLinearColor& Colour);

	/** A bar filled to a fraction of its width. */
	void Bar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Colour);

	TArray<FString> Drawing;

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
