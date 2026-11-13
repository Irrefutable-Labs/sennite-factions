// Fill out your copyright notice in the Description page of Project Settings.

#include "SenniteHUD.h"
#include "Engine/Canvas.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "SenniteCharacter.h"
#include "SennitePlayerState.h"
#include "Engine/Font.h"
#include "StormActor.h"
#include "WeaponActor.h"

ASenniteHUD::ASenniteHUD()
{
}

void ASenniteHUD::DrawHUD()
{
	Super::DrawHUD();
	DrawCrosshair();
	DrawStatus();
}

FSenniteHudReadout ASenniteHUD::ReadState() const
{
	FSenniteHudReadout Readout;
	const ASenniteCharacter* Hero = Cast<ASenniteCharacter>(GetOwningPawn());
	const ASennitePlayerState* State = PlayerOwner ? Cast<ASennitePlayerState>(PlayerOwner->PlayerState) : nullptr;
	if (Hero == nullptr || State == nullptr) {
		return Readout;
	}
	Readout.bValid = true;
	Readout.Health = Hero->Health;
	Readout.Shield = Hero->Shield;
	for (int32 Material = 0; Material < 3; ++Material) {
		Readout.Materials[Material] = State->MaterialCounts.IsValidIndex(Material) ? State->MaterialCounts[Material] : 0;
	}
	Readout.CurrentMaterial = Hero->GetCurrentBuildingMaterial();
	Readout.bBuildMode = State->InBuildMode;
	Readout.BuildMode = State->BuildMode;
	if (const AWeaponActor* Weapon = Hero->CurrentWeapon) {
		Readout.bHoldingWeapon = true;
		Readout.WeaponType = Weapon->WeaponType;
		Readout.Clip = Weapon->CurrentBulletCount;
		Readout.Reserve = State->EquippedWeaponsAmmunition.IsValidIndex(Weapon->WeaponType) ? State->EquippedWeaponsAmmunition[Weapon->WeaponType] : 0;
	}
	Readout.Kills = State->KillCount;
	if (const AStormActor* Storm = Hero->CurrentStorm) {
		Readout.bInStorm = Hero->InStorm;
		Readout.StormDamage = Storm->Damage;
	}
	if (const AGameStateBase* Game = GetWorld()->GetGameState()) {
		for (const APlayerState* Player : Game->PlayerArray) {
			Readout.Alive += Player != nullptr && !Player->IsSpectator() ? 1 : 0;
		}
	}
	return Readout;
}

void ASenniteHUD::Label(const TCHAR* Name, const FString& Text, float X, float Y, int32 Points, const FLinearColor& Colour)
{
	// the engine's own font, scaled to the height asked for on a 720-line screen
	UFont* Font = GEngine->GetLargeFont();
	if (Font == nullptr) {
		return;
	}
	const float Scale = Canvas->ClipY / 720.0f;
	const float Height = Points * 1.4f * Scale;
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Font, Colour);
	Item.Scale = FVector2D(Height / FMath::Max(1.0f, Font->GetMaxCharHeight()));
	Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
	Canvas->DrawItem(Item);
	Drawing.Add(FString::Printf(TEXT("%s=%s"), Name, *Text));
}

void ASenniteHUD::Bar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Colour)
{
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f), X, Y, Width, Height);
	DrawRect(Colour, X, Y, Width * FMath::Clamp(Fraction, 0.0f, 1.0f), Height);
}

void ASenniteHUD::DrawStatus()
{
	Drawing.Reset();
	LastReadout = ReadState();
	const FSenniteHudReadout& Now = LastReadout;
	if (!Now.bValid) {
		LastDrawn = Drawing;
		return;
	}
	// every size is for a screen 720 lines high and scales with the screen, phone or desktop
	const float S = Canvas->ClipY / 720.0f;
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const FLinearColor White = FLinearColor::White;
	const FLinearColor Dim(1.0f, 1.0f, 1.0f, 0.7f);

	const FLinearColor Plate(0.0f, 0.0f, 0.0f, 0.35f);

	// bottom left: shield over health
	const float BarX = 28.0f * S;
	const float BarW = 260.0f * S;
	const float BarH = 14.0f * S;
	DrawRect(Plate, BarX - 10.0f * S, H - 88.0f * S, BarW + 70.0f * S, 64.0f * S);
	Bar(BarX, H - 76.0f * S, BarW, BarH, Now.Shield / 100.0f, FLinearColor(0.04f, 0.18f, 0.85f));
	Label(TEXT("shield"), FString::FromInt(FMath::RoundToInt(Now.Shield)), BarX + BarW + 10.0f * S, H - 80.0f * S, 14, White);
	Bar(BarX, H - 50.0f * S, BarW, BarH, Now.Health / 100.0f, FLinearColor(0.08f, 0.5f, 0.08f));
	Label(TEXT("health"), FString::FromInt(FMath::RoundToInt(Now.Health)), BarX + BarW + 10.0f * S, H - 54.0f * S, 14, White);

	// bottom right: the weapon in hand, the rounds in it, and the rounds carried for it
	if (Now.bHoldingWeapon) {
		const TCHAR* Names[] = { TEXT("PICKAXE"), TEXT("RIFLE"), TEXT("SHOTGUN") };
		const float AmmoX = W - 210.0f * S;
		DrawRect(Plate, AmmoX - 12.0f * S, H - 110.0f * S, 150.0f * S, 86.0f * S);
		Label(TEXT("weapon"), Now.WeaponType >= 0 && Now.WeaponType < 3 ? Names[Now.WeaponType] : TEXT("WEAPON"), AmmoX, H - 104.0f * S, 12, Dim);
		if (Now.WeaponType > 0) {
			Label(TEXT("ammo"), FString::FromInt(Now.Clip), AmmoX, H - 86.0f * S, 34, White);
			Label(TEXT("reserve"), FString::FromInt(Now.Reserve), AmmoX + 84.0f * S, H - 66.0f * S, 16, Dim);
		}
	}

	// right: the three building materials, weakest first, the one in use marked while building
	const FLinearColor Tiers[] = { FLinearColor(0.45f, 0.5f, 0.55f), FLinearColor(0.03f, 0.1f, 0.05f), FLinearColor(0.16f, 0.17f, 0.19f) };
	const float MatX = W - 130.0f * S;
	const float MatY = H * 0.42f;
	DrawRect(Plate, MatX - 16.0f * S, MatY - (Now.bBuildMode ? 38.0f : 8.0f) * S, 112.0f * S, (Now.bBuildMode ? 126.0f : 96.0f) * S);
	if (Now.bBuildMode) {
		Label(TEXT("mode"), Now.BuildMode.ToUpper(), MatX, MatY - 32.0f * S, 14, White);
	}
	for (int32 Material = 0; Material < 3; ++Material) {
		const float RowY = MatY + Material * 30.0f * S;
		if (Now.bBuildMode && Material == Now.CurrentMaterial) {
			DrawRect(White, MatX - 10.0f * S, RowY, 4.0f * S, 20.0f * S);
		}
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f), MatX - 2.0f * S, RowY - 2.0f * S, 24.0f * S, 24.0f * S);
		DrawRect(Tiers[Material], MatX, RowY, 20.0f * S, 20.0f * S);
		Label(*FString::Printf(TEXT("material%d"), Material), FString::FromInt(Now.Materials[Material]), MatX + 32.0f * S, RowY - 1.0f * S, 15,
			Now.Materials[Material] > 0 ? White : Dim);
	}

	// top right: players left and this player's kills
	DrawRect(Plate, W - 222.0f * S, 16.0f * S, 198.0f * S, 34.0f * S);
	Label(TEXT("alive"), FString::Printf(TEXT("%d ALIVE"), Now.Alive), W - 210.0f * S, 24.0f * S, 14, White);
	Label(TEXT("kills"), FString::Printf(TEXT("%d KILLS"), Now.Kills), W - 110.0f * S, 24.0f * S, 14, White);

	// top centre: a warning while the player is outside the safe circle
	if (Now.bInStorm) {
		DrawRect(FLinearColor(0.25f, 0.02f, 0.4f, 0.6f), W * 0.5f - 130.0f * S, 16.0f * S, 260.0f * S, 34.0f * S);
		Label(TEXT("storm"), TEXT("IN THE STORM"), W * 0.5f - 62.0f * S, 24.0f * S, 14, White);
	}

	LastDrawn = Drawing;
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