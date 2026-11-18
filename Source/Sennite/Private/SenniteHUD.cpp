// Fill out your copyright notice in the Description page of Project Settings.

#include "SenniteHUD.h"
#include "Engine/Canvas.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "SenniteBaseEditor.h"
#include "SenniteCharacter.h"
#include "SenniteCrate.h"
#include "SenniteMatch.h"
#include "SennitePlayerState.h"
#include "SennitePlayerController.h"
#include "SenniteTouch.h"
#include "Engine/Font.h"
#include "BuildingActor.h"
#include "HealingActor.h"
#include "StormActor.h"
#include "WeaponActor.h"

ASenniteHUD::ASenniteHUD()
{
}

void ASenniteHUD::DrawHUD()
{
	Super::DrawHUD();
	// the planning view has its own panel: the fight's crosshair and status are not drawn over it
	if (!ASenniteBaseEditor::IsPlanning(GetWorld())) {
		DrawCrosshair();
		DrawStatus();
		DrawMatch();
	}
	ASenniteBaseEditor::DrawOn(this, Canvas);
	// on a device played by touch, the stick and buttons over the rest; in the planning view, the editor's buttons
	if (ASennitePlayerController* Player = Cast<ASennitePlayerController>(PlayerOwner)) {
		if (USenniteTouchControls* Touch = Player->GetTouchControls()) {
			Touch->Draw(Canvas);
		}
	}
}

FSenniteHudReadout ASenniteHUD::ReadState() const
{
	FSenniteHudReadout Readout;
	const ASenniteCharacter* Hero = Cast<ASenniteCharacter>(GetOwningPawn());
	const ASennitePlayerState* State = PlayerOwner ? Cast<ASennitePlayerState>(PlayerOwner->PlayerState) : nullptr;
	// players left and kills are the same whether the player is alive or watching
	if (State != nullptr) {
		Readout.Kills = State->KillCount;
		Readout.bSpectating = State->IsSpectator();
	}
	if (const AGameStateBase* Game = GetWorld()->GetGameState()) {
		for (const APlayerState* Player : Game->PlayerArray) {
			Readout.Alive += Player != nullptr && !Player->IsSpectator() ? 1 : 0;
		}
	}
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
	if (const AHealingActor* Item = Hero->CurrentHealingItem) {
		Readout.bHoldingItem = true;
		Readout.ItemType = Item->HealingType;
		Readout.ItemCount = State->HealingItemCounts.IsValidIndex(Item->HealingType) ? State->HealingItemCounts[Item->HealingType] : 0;
	}
	if (const AStormActor* Storm = Hero->CurrentStorm) {
		Readout.bInStorm = Hero->InStorm;
		Readout.StormDamage = Storm->Damage;
	}
	Readout.bEditing = Hero->GetEditingPiece() != nullptr;
	if (const ABuildingActor* Piece = Hero->PieceInSight()) {
		Readout.PieceName = Piece->Describe();
		Readout.PieceHealth = Piece->Health;
		Readout.PieceMax = Piece->MaxHealth();
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
	// every size is for a screen 720 lines high and scales with the screen, phone or desktop
	const float S = Canvas->ClipY / 720.0f;
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const FLinearColor White = FLinearColor::White;
	const FLinearColor Dim(1.0f, 1.0f, 1.0f, 0.7f);
	const double Clock = GetWorld()->GetTimeSeconds();
	if (!Now.bValid) {
		// no hero: a player who has fallen and is watching still sees who is left
		if (Now.bSpectating) {
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), W * 0.5f - 130.0f * S, 16.0f * S, 260.0f * S, 34.0f * S);
			Label(TEXT("spectating"), TEXT("SPECTATING"), W * 0.5f - 52.0f * S, 24.0f * S, 14, White);
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), W - 222.0f * S, 16.0f * S, 198.0f * S, 34.0f * S);
			Label(TEXT("alive"), FString::Printf(TEXT("%d ALIVE"), Now.Alive), W - 210.0f * S, 24.0f * S, 14, White);
			Label(TEXT("kills"), FString::Printf(TEXT("%d KILLS"), Now.Kills), W - 110.0f * S, 24.0f * S, 14, White);
		}
		LastDrawn = Drawing;
		return;
	}

	// just hurt: red at the edges of the screen
	if (Clock < HurtUntil) {
		const FLinearColor Red(0.6f, 0.0f, 0.0f, 0.35f);
		const float Edge = 26.0f * S;
		DrawRect(Red, 0.0f, 0.0f, W, Edge);
		DrawRect(Red, 0.0f, H - Edge, W, Edge);
		DrawRect(Red, 0.0f, Edge, Edge, H - 2.0f * Edge);
		DrawRect(Red, W - Edge, Edge, Edge, H - 2.0f * Edge);
		Drawing.Add(TEXT("hurt=1"));
	}
	// just hit someone: four ticks slanting in toward the aim point
	if (Clock < HitMarkerUntil) {
		const float X = W * 0.5f;
		const float Y = H * 0.5f;
		const float In = 7.0f * S;
		const float Out = 16.0f * S;
		for (const FVector2D& Way : { FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, 1), FVector2D(-1, -1) }) {
			DrawLine(X + Way.X * In, Y + Way.Y * In, X + Way.X * Out, Y + Way.Y * Out, White, 2.5f * S);
		}
		Drawing.Add(TEXT("hitmarker=1"));
	}
	// just hit a built piece: what it took, by the aim
	if (Clock < PieceHitUntil) {
		Label(TEXT("piecehit"), FString::FromInt(FMath::RoundToInt(PieceHitDamage)), W * 0.5f + 14.0f * S, H * 0.5f - 30.0f * S, 14, FLinearColor(0.55f, 0.8f, 1.0f));
	}
	// a built piece in sight: what it is and what it has left
	if (!Now.PieceName.IsEmpty()) {
		const float PieceW = 120.0f * S;
		const float PieceX = W * 0.5f - PieceW * 0.5f;
		const float PieceY = H * 0.5f + 26.0f * S;
		const float Left = Now.PieceMax > 0.0f ? Now.PieceHealth / Now.PieceMax : 0.0f;
		Bar(PieceX, PieceY, PieceW, 5.0f * S, Left, Left > 0.5f ? FLinearColor(0.55f, 0.8f, 1.0f) : Left > 0.25f ? FLinearColor(1.0f, 0.7f, 0.1f) : FLinearColor(0.9f, 0.15f, 0.1f));
		Label(TEXT("pieceaim"), FString::Printf(TEXT("%s %d/%d"), *Now.PieceName, FMath::RoundToInt(Now.PieceHealth), FMath::RoundToInt(Now.PieceMax)),
			PieceX, PieceY + 8.0f * S, 10, FLinearColor(1.0f, 1.0f, 1.0f, 0.85f));
	}
	// editing a piece: what the buttons do
	if (Now.bEditing) {
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), W * 0.5f - 200.0f * S, H * 0.70f, 400.0f * S, 34.0f * S);
		Label(TEXT("edit"), TEXT("EDITING   FIRE: CUT OR PUT BACK A TILE   AIM: RESET   G: DONE"), W * 0.5f - 188.0f * S, H * 0.70f + 9.0f * S, 11, FLinearColor::White);
	}

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

	// bottom right, in the weapon's place: the healing item in hand and how many are left
	if (Now.bHoldingItem) {
		const TCHAR* Names[] = { TEXT("BANDAGE"), TEXT("POTION") };
		const float ItemX = W - 210.0f * S;
		DrawRect(Plate, ItemX - 12.0f * S, H - 110.0f * S, 150.0f * S, 86.0f * S);
		Label(TEXT("item"), Now.ItemType >= 0 && Now.ItemType < 2 ? Names[Now.ItemType] : TEXT("ITEM"), ItemX, H - 104.0f * S, 12, Dim);
		Label(TEXT("itemcount"), FString::FromInt(Now.ItemCount), ItemX, H - 86.0f * S, 34, White);
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

void ASenniteHUD::DrawMatch()
{
	const float S = Canvas->ClipY / 720.0f;
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const FLinearColor White = FLinearColor::White;
	const FLinearColor Shade(0.0f, 0.0f, 0.0f, 0.35f);
	// beside a closed weapon crate: how to open it
	if (const ASenniteCharacter* Hero = Cast<ASenniteCharacter>(GetOwningPawn())) {
		const ASenniteCrate* Rules = GetDefault<ASenniteCrate>();
		if (ASenniteCrate::Nearest(GetWorld(), Hero->GetActorLocation(), Rules->Reach + Rules->Across, true) != nullptr) {
			DrawRect(Shade, W * 0.5f - 110.0f * S, H * 0.62f, 220.0f * S, 34.0f * S);
			Label(TEXT("crate"), TEXT("E   OPEN THE CRATE"), W * 0.5f - 92.0f * S, H * 0.62f + 8.0f * S, 14, White);
		}
	}
	const ASenniteMatch* Match = ASenniteMatch::Find(GetWorld());
	if (Match == nullptr) {
		LastDrawn = Drawing;
		return;
	}
	const ASennitePlayerState* State = PlayerOwner ? Cast<ASennitePlayerState>(PlayerOwner->PlayerState) : nullptr;
	const FString Mine = State != nullptr ? State->FactionId : FString();
	const FSenniteMatchFaction* Own = Match->FactionOf(Mine);
	// the player's own faction first, then the others by the start of their names
	FString Score = Own != nullptr ? FString::Printf(TEXT("YOUR FACTION %d"), Own->Kills) : FString();
	for (const FSenniteMatchFaction& Faction : Match->Factions) {
		if (&Faction != Own) {
			Score += FString::Printf(TEXT("%s%s %d%s"), Score.IsEmpty() ? TEXT("") : TEXT("    "), *Faction.FactionId.Left(6).ToUpper(), Faction.Kills, Faction.bOut ? TEXT(" OUT") : TEXT(""));
		}
	}
	FString Line;
	FString Under;
	if (Match->State == EMatchState::Failed) {
		Line = TEXT("THIS MATCH CANNOT BE PLAYED");
		Under = Match->Why;
	}
	else if (Mine.IsEmpty()) {
		Line = TEXT("ENTERING THE MATCH");
		Under = Match->Said;
	}
	else if (Match->State == EMatchState::Waiting) {
		Line = TEXT("WAITING FOR ANOTHER FACTION'S PLAYERS");
		Under = Score;
	}
	else if (Match->State == EMatchState::Playing) {
		const int32 Left = FMath::CeilToInt32(Match->SecondsLeft());
		Line = Match->Length > 0.0f ? FString::Printf(TEXT("%02d:%02d    %s"), Left / 60, Left % 60, *Score) : Score;
		Under = Own != nullptr && Own->bOut ? FString(TEXT("YOUR BASE IS WIPED. YOUR FACTION IS OUT."))
			: Match->Target > 0 ? FString::Printf(TEXT("FIRST TO %d KILLS, OR THE LAST BASE STANDING"), Match->Target) : FString(TEXT("THE LAST BASE STANDING"));
	}
	else {
		Line = Match->Outcome == ASenniteMatch::Void() ? TEXT("NOBODY WON") : Match->Outcome == Mine ? TEXT("YOUR FACTION WON") : TEXT("YOUR FACTION LOST");
		Under = FString::Printf(TEXT("%s    %s"), *Match->Why.ToUpper(), *Score);
	}
	DrawRect(Shade, W * 0.5f - 270.0f * S, 58.0f * S, 540.0f * S, Under.IsEmpty() ? 32.0f * S : 54.0f * S);
	Label(TEXT("match"), Line, W * 0.5f - 258.0f * S, 64.0f * S, 14, White);
	if (!Under.IsEmpty()) {
		Label(TEXT("matchnote"), Under, W * 0.5f - 258.0f * S, 88.0f * S, 10, FLinearColor(1.0f, 1.0f, 1.0f, 0.8f));
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
	// drawn on the canvas for a quarter of a second (DrawStatus)
	HitMarkerUntil = GetWorld()->GetTimeSeconds() + 0.25;
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

void ASenniteHUD::DrawPieceHit(float Damage) {
	// drawn on the canvas for a little under half a second (DrawStatus)
	PieceHitUntil = GetWorld()->GetTimeSeconds() + 0.45;
	PieceHitDamage = Damage;
}

void ASenniteHUD::DrawBloodSplash() {
	// drawn on the canvas for a little under half a second (DrawStatus)
	HurtUntil = GetWorld()->GetTimeSeconds() + 0.4;
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