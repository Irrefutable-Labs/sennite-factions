// The in-game menu: see SenniteGameMenu.h.

#include "SenniteGameMenu.h"
#include "AudioDevice.h"
#include "Engine/Canvas.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Scalability.h"
#include "SenniteBaseEditor.h"
#include "SenniteHeroes.h"
#include "SennitePlayerController.h"
#include "SennitePublisher.h"
#include "SenniteTouch.h"
#include "SenniteUi.h"

namespace
{
	const TCHAR* FirstScreenMap = TEXT("/Game/Maps/Menu");

	/** How far one press of an arrow changes how the view turns, and the least and most it can be. */
	constexpr float SensitivityStep = 0.1f;
	constexpr float SensitivityLeast = 0.2f;
	constexpr float SensitivityMost = 3.0f;
	constexpr float VolumeStep = 0.1f;
	/** The graphics qualities the menu offers: low, medium, high, epic. */
	constexpr int32 QualityMost = 3;

	const TCHAR* QualityName(int32 Level)
	{
		static const TCHAR* Names[] = { TEXT("LOW"), TEXT("MEDIUM"), TEXT("HIGH"), TEXT("EPIC"), TEXT("CINEMATIC") };
		return Level >= 0 && Level < UE_ARRAY_COUNT(Names) ? Names[Level] : TEXT("DEVICE");
	}

	/** A tenth, exactly: steps added up do not drift. */
	float Tenths(float Value)
	{
		return FMath::RoundToFloat(Value * 10.0f) / 10.0f;
	}
}

void USenniteGameMenu::Bind(APlayerController* InController)
{
	Controller = InController;
}

int32 USenniteGameMenu::QualityNow()
{
	return Scalability::GetQualityLevels().GetSingleQualityLevel();
}

void USenniteGameMenu::ApplySettings(const UWorld* World)
{
	const USenniteProfile& Profile = USenniteProfile::Get();
	if (World != nullptr) {
		// a game run without sound has no audio device: there is nothing to turn down
		if (FAudioDeviceHandle Device = World->GetAudioDevice()) {
			Device->SetTransientPrimaryVolume(FMath::Clamp(Profile.Volume, 0.0f, 1.0f));
		}
	}
	// the device's own quality stands until the player chooses one
	if (Profile.Quality >= 0 && QualityNow() != Profile.Quality) {
		Scalability::FQualityLevels Levels;
		Levels.SetFromSingleQualityLevel(FMath::Clamp(Profile.Quality, 0, QualityMost));
		// forced: a phone's device profile has already set its own levels, and the player's choice is over them
		Scalability::SetQualityLevels(Levels, true);
	}
}

void USenniteGameMenu::Open()
{
	if (bOpen || Controller == nullptr) {
		return;
	}
	bOpen = true;
	Page = ESenniteGameMenuPage::Main;
	Note.Reset();
	Pressed = NAME_None;
	// what the player was holding is let go: the hero does not walk on, or keep firing, behind the menu
	Controller->FlushPressedKeys();
	if (ASennitePlayerController* Player = Cast<ASennitePlayerController>(Controller)) {
		if (USenniteTouchControls* Touch = Player->GetTouchControls()) {
			Touch->ReleaseAll();
		}
	}
	bCursorBefore = Controller->bShowMouseCursor;
	Controller->bShowMouseCursor = true;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Controller->SetInputMode(Mode);
}

void USenniteGameMenu::Close()
{
	if (!bOpen || Controller == nullptr) {
		return;
	}
	bOpen = false;
	Pressed = NAME_None;
	Buttons.Reset();
	LastDrawn.Reset();
	Controller->bShowMouseCursor = bCursorBefore;
	if (!bCursorBefore) {
		Controller->SetInputMode(FInputModeGameOnly());
	}
}

bool USenniteGameMenu::Key(const FInputKeyEventArgs& Params)
{
	const bool bBack = Params.Key == EKeys::Escape || Params.Key == EKeys::Android_Back;
	if (!bOpen) {
		if (!bBack) {
			return false;
		}
		if (Params.Event == IE_Pressed) {
			Open();
		}
		return true;
	}
	if (Params.Key == EKeys::LeftMouseButton && (Params.Event == IE_Pressed || Params.Event == IE_Released)) {
		float X = 0.0f;
		float Y = 0.0f;
		if (Controller->GetMousePosition(X, Y)) {
			Params.Event == IE_Pressed ? Press(FVector2D(X, Y)) : Release(FVector2D(X, Y));
		}
		return true;
	}
	if (bBack && Params.Event == IE_Pressed) {
		Act(TEXT("back"));
	}
	// everything else stops here: the hero takes nothing while the menu is open
	return true;
}

bool USenniteGameMenu::Touch(ETouchType::Type Type, const FVector2D& Where)
{
	if (!bOpen) {
		return false;
	}
	if (Type == ETouchType::Began) {
		Press(Where);
	}
	else if (Type == ETouchType::Ended) {
		Release(Where);
	}
	return true;
}

FName USenniteGameMenu::ButtonAt(const FVector2D& Where) const
{
	// Where is on the screen; the buttons are where the canvas drew them
	for (const FSenniteMenuButton& Each : Buttons) {
		if (Each.bEnabled && Each.Box.IsInside(Where - Origin)) {
			return Each.Action;
		}
	}
	return NAME_None;
}

bool USenniteGameMenu::Centre(FName Action, FVector2D& Out) const
{
	for (const FSenniteMenuButton& Each : Buttons) {
		if (Each.Action == Action) {
			Out = Each.Box.GetCenter() + Origin;
			return true;
		}
	}
	return false;
}

void USenniteGameMenu::Press(const FVector2D& Where)
{
	Pressed = ButtonAt(Where);
}

void USenniteGameMenu::Release(const FVector2D& Where)
{
	const FName Action = ButtonAt(Where);
	const FName Began = Pressed;
	Pressed = NAME_None;
	// a press acts when it ends on the button it began on: a finger that slides off changes its mind
	if (!Action.IsNone() && Action == Began) {
		Act(Action);
	}
}

void USenniteGameMenu::Act(FName Action)
{
	if (Controller == nullptr) {
		return;
	}
	const FString Name = Action.ToString();
	USenniteProfile& Profile = USenniteProfile::Get();
	UWorld* World = Controller->GetWorld();
	Note.Reset();
	if (Name == TEXT("resume")) {
		Close();
	}
	else if (Name == TEXT("settings")) {
		Page = ESenniteGameMenuPage::Settings;
	}
	else if (Name == TEXT("store") || Name == TEXT("base")) {
		if (ASenniteBaseEditor* Editor = ASenniteBaseEditor::Find(World)) {
			Close();
			Editor->Show(Name == TEXT("store"));
		}
	}
	else if (Name == TEXT("mainmenu")) {
		Page = ESenniteGameMenuPage::Leave;
	}
	else if (Name == TEXT("quit")) {
		Page = ESenniteGameMenuPage::Quit;
	}
	else if (Name == TEXT("leave")) {
		APlayerController* Player = Controller;
		Close();
		// a player in someone else's game leaves it; a player whose machine runs the game ends it for all in it
		if (Player->GetNetMode() == NM_Client) {
			Player->ClientTravel(FirstScreenMap, TRAVEL_Absolute);
		}
		else {
			UGameplayStatics::OpenLevel(Player, FName(FirstScreenMap));
		}
	}
	else if (Name == TEXT("quitnow")) {
		Controller->ConsoleCommand(TEXT("quit"));
	}
	else if (Name == TEXT("back") || Name == TEXT("stay")) {
		if (Page == ESenniteGameMenuPage::Main) {
			Close();
		}
		else {
			Page = ESenniteGameMenuPage::Main;
		}
	}
	else {
		// a setting: changed, kept, and in effect at once
		if (Name == TEXT("sens-") || Name == TEXT("sens+")) {
			Profile.LookSensitivity = FMath::Clamp(Tenths(Profile.LookSensitivity + (Name == TEXT("sens+") ? SensitivityStep : -SensitivityStep)), SensitivityLeast, SensitivityMost);
		}
		else if (Name == TEXT("invert")) {
			Profile.bInvertLook = !Profile.bInvertLook;
		}
		else if (Name == TEXT("vol-") || Name == TEXT("vol+")) {
			Profile.Volume = FMath::Clamp(Tenths(Profile.Volume + (Name == TEXT("vol+") ? VolumeStep : -VolumeStep)), 0.0f, 1.0f);
		}
		else if (Name == TEXT("quality-") || Name == TEXT("quality+")) {
			// from what is in effect now; a device's own mix of levels counts as medium
			const int32 Now = QualityNow();
			const int32 From = Now >= 0 ? FMath::Min(Now, QualityMost) : 1;
			Profile.Quality = FMath::Clamp(From + (Name == TEXT("quality+") ? 1 : -1), 0, QualityMost);
		}
		else {
			return;
		}
		Profile.Save();
		ApplySettings(World);
	}
}

// ---------------------------------------------------------------------------------------------
// Drawing

void USenniteGameMenu::Line(UCanvas* Canvas, const TCHAR* Name, const FString& Words, float X, float Y, float Points, const FLinearColor& Colour, float Align)
{
	SenniteUi::Text(Canvas, Words, X * S, Y * S, Points, S, Colour, Align);
	LastDrawn.Add(FString::Printf(TEXT("%s=%s"), Name, *Words));
}

void USenniteGameMenu::Heading(UCanvas* Canvas, const FString& Title, const FString& Under)
{
	// a page's name over a short gold rule, as the first screen's is
	Line(Canvas, TEXT("title"), Title, 48.0f, 34.0f, 30, FLinearColor::White);
	SenniteUi::Plate(Canvas, FBox2D(FVector2D(48.0f * S, 84.0f * S), FVector2D(144.0f * S, 87.0f * S)), 0.0f, SenniteUi::Gold, SenniteUi::Gold);
	if (!Under.IsEmpty()) {
		Line(Canvas, TEXT("under"), Under, 50.0f, 96.0f, 11, SenniteUi::Dim);
	}
}

void USenniteGameMenu::Button(UCanvas* Canvas, FName Action, const FString& Label, float X, float Y, float Width, float Height, bool bEnabled, bool bPrimary, int32 Arrow)
{
	const FBox2D Box(FVector2D(X * S, Y * S), FVector2D((X + Width) * S, (Y + Height) * S));
	const SenniteUi::EButton State = !bEnabled ? SenniteUi::EButton::Disabled
		: Action == Pressed ? SenniteUi::EButton::Pressed
		: Action == Hovered ? SenniteUi::EButton::Hover
		: bPrimary ? SenniteUi::EButton::Primary : SenniteUi::EButton::Plain;
	const FLinearColor Ink = SenniteUi::ButtonPlate(Canvas, Box, S, State);
	const FVector2D Middle = Box.GetCenter();
	if (Arrow != 0) {
		// an arrow button: the arrow is the button
		SenniteUi::Chevron(Canvas, Middle, FMath::Min(Width, Height) * 0.42f * S, Arrow < 0, Ink, 6.0f * S);
	}
	else {
		const float High = SenniteUi::Measure(Label, 18, S).Y;
		SenniteUi::Text(Canvas, Label, Middle.X, Middle.Y - High * 0.5f, 18, S, Ink, 0.5f);
	}
	LastDrawn.Add(FString::Printf(TEXT("button:%s=%s"), *Action.ToString(), *Label));
	FSenniteMenuButton Each;
	Each.Action = Action;
	Each.Text = Label;
	Each.Box = Box;
	Each.bEnabled = bEnabled;
	Buttons.Add(Each);
}

void USenniteGameMenu::Draw(UCanvas* Canvas)
{
	if (!bOpen || Canvas == nullptr || Controller == nullptr) {
		return;
	}
	S = Canvas->ClipY / 720.0f;
	Origin = SenniteUi::CanvasOrigin(Canvas);
	// the button under the mouse, from where the last frame drew them
	Hovered = NAME_None;
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (Controller->GetMousePosition(MouseX, MouseY)) {
		Hovered = ButtonAt(FVector2D(MouseX, MouseY));
	}
	Buttons.Reset();
	LastDrawn.Reset();
	// the game is seen behind the menu, dimmed, and darker under the buttons
	// the shade covers the whole screen, past the safe area the canvas is drawn in
	const FVector2D ScreenMin(-Origin.X, -Origin.Y);
	const FVector2D ScreenMax(Canvas->ClipX + Canvas->SafeZonePadEX, Canvas->ClipY + Canvas->SafeZonePadEY);
	const FLinearColor Shade(0.0f, 0.0f, 0.0f, 0.45f);
	SenniteUi::Fade(Canvas, FBox2D(ScreenMin, ScreenMax), Shade, Shade, false);
	SenniteUi::Fade(Canvas, FBox2D(ScreenMin, FVector2D(620.0f * S, ScreenMax.Y)), FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), false);
	switch (Page) {
	case ESenniteGameMenuPage::Settings:
		DrawSettings(Canvas);
		break;
	case ESenniteGameMenuPage::Leave:
		DrawAsk(Canvas, TEXT("LEAVE THE GAME?"), Controller->GetNetMode() == NM_ListenServer ? TEXT("You are hosting: the game ends for everyone in it.") : TEXT("You go back to the main menu."), TEXT("leave"), TEXT("LEAVE"));
		break;
	case ESenniteGameMenuPage::Quit:
		DrawAsk(Canvas, TEXT("QUIT THE GAME?"), TEXT("The game closes."), TEXT("quitnow"), TEXT("QUIT"));
		break;
	default:
		DrawMain(Canvas);
		break;
	}
	if (!Note.IsEmpty()) {
		Line(Canvas, TEXT("note"), Note, 50.0f, 668.0f, 12, SenniteUi::Gold);
	}
}

void USenniteGameMenu::DrawMain(UCanvas* Canvas)
{
	Heading(Canvas, TEXT("MENU"), FString());
	const UWorld* World = Controller->GetWorld();
	const UGameInstance* Game = World != nullptr ? World->GetGameInstance() : nullptr;
	const USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
	// the store and the base are the base editor's pages: there is one where there is a publisher's server, outside a match
	const bool bEditor = ASenniteBaseEditor::Find(World) != nullptr;
	const float X = 48.0f;
	float Y = 124.0f;
	const float Step = 70.0f;
	const float High = 58.0f;
	Button(Canvas, TEXT("resume"), TEXT("RESUME"), X, Y, 320.0f, High, true, true);
	Button(Canvas, TEXT("settings"), TEXT("SETTINGS"), X, Y += Step, 320.0f, High);
	Button(Canvas, TEXT("store"), TEXT("STORE"), X, Y += Step, 320.0f, High, bEditor);
	Button(Canvas, TEXT("base"), TEXT("BASE EDITOR"), X, Y += Step, 320.0f, High, bEditor);
	Button(Canvas, TEXT("mainmenu"), TEXT("MAIN MENU"), X, Y += Step, 320.0f, High);
	Button(Canvas, TEXT("quit"), TEXT("QUIT GAME"), X, Y += Step, 320.0f, High);
	if (!bEditor) {
		Line(Canvas, TEXT("why"), Server != nullptr && Server->IsOn() ? TEXT("THE STORE AND YOUR BASE ARE NOT OPEN DURING A MATCH") : TEXT("THE STORE AND YOUR BASE NEED THE PUBLISHER'S SERVER"),
			X + 2.0f, Y + High + 16.0f, 11, SenniteUi::Dim);
	}
}

void USenniteGameMenu::DrawSettings(UCanvas* Canvas)
{
	Heading(Canvas, TEXT("SETTINGS"), FString());
	const USenniteProfile& Profile = USenniteProfile::Get();
	const float X = 48.0f;
	const float Arrow = 60.0f;
	const float Value = 200.0f;
	const float High = 52.0f;
	const float Step = 96.0f;
	float Y = 116.0f;
	// a row: what it sets, then less, its value, more
	auto Row = [&](const TCHAR* Name, const TCHAR* Title, const FString& Shown, bool bLess, bool bMore) {
		Line(Canvas, *FString::Printf(TEXT("label:%s"), Name), Title, X + 2.0f, Y, 12, SenniteUi::Gold);
		Button(Canvas, FName(*FString::Printf(TEXT("%s-"), Name)), FString(), X, Y + 24.0f, Arrow, High, bLess, false, -1);
		SenniteUi::Panel(Canvas, FBox2D(FVector2D((X + Arrow + 8.0f) * S, (Y + 24.0f) * S), FVector2D((X + Arrow + Value - 8.0f) * S, (Y + 24.0f + High) * S)), S);
		const float TextHigh = SenniteUi::Measure(Shown, 18, S).Y / S;
		Line(Canvas, Name, Shown, X + Arrow + Value * 0.5f, Y + 24.0f + (High - TextHigh) * 0.5f, 18, FLinearColor::White, 0.5f);
		Button(Canvas, FName(*FString::Printf(TEXT("%s+"), Name)), FString(), X + Arrow + Value, Y + 24.0f, Arrow, High, bMore, false, 1);
		Y += Step;
	};
	Row(TEXT("sens"), TEXT("LOOK SENSITIVITY"), FString::Printf(TEXT("%.1f"), Profile.LookSensitivity), Profile.LookSensitivity > SensitivityLeast + 0.01f, Profile.LookSensitivity < SensitivityMost - 0.01f);
	// up looks down, or not: one button that says which
	Line(Canvas, TEXT("label:invert"), TEXT("INVERT LOOK"), X + 2.0f, Y, 12, SenniteUi::Gold);
	Button(Canvas, TEXT("invert"), Profile.bInvertLook ? TEXT("ON") : TEXT("OFF"), X, Y + 24.0f, 2.0f * Arrow + Value, High);
	Y += Step;
	Row(TEXT("vol"), TEXT("VOLUME"), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Profile.Volume * 100.0f)), Profile.Volume > 0.01f, Profile.Volume < 0.99f);
	const int32 Quality = QualityNow();
	Row(TEXT("quality"), TEXT("GRAPHICS QUALITY"), QualityName(Quality), Quality != 0, Quality < QualityMost);
	Button(Canvas, TEXT("back"), TEXT("BACK"), X, Y + 12.0f, 2.0f * Arrow + Value, 58.0f);
}

void USenniteGameMenu::DrawAsk(UCanvas* Canvas, const TCHAR* Question, const TCHAR* Under, FName Yes, const TCHAR* YesLabel)
{
	Heading(Canvas, Question, Under);
	Button(Canvas, Yes, YesLabel, 48.0f, 150.0f, 320.0f, 58.0f, true, true);
	Button(Canvas, TEXT("stay"), TEXT("STAY"), 48.0f, 220.0f, 320.0f, 58.0f);
}
