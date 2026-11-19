// The menu a player opens while playing: Escape on a keyboard, the phone's back button, or MENU on
// the touch screen. The game goes on behind it (others may be playing), and while it is open the hero
// takes nothing the player presses.
//
// MENU: RESUME; SETTINGS; STORE and BASE EDITOR, which open the base editor (SenniteBaseEditor.h) on the
// shop's page or the parcel, where there is a publisher's server and so an editor; MAIN MENU, which
// leaves the game for the first screen; QUIT GAME. Leaving and quitting ask once more first.
//
// SETTINGS: how far the mouse or a finger turns the view, whether up looks down, how loud the game
// is, how loud the menu's songs are in it, and the graphics quality. The first screen's SETTINGS is
// this page, opened on its own. Each takes effect as it is changed and is kept on this machine
// between runs (USenniteProfile).
//
// Drawn on the canvas in the menus' style (SenniteUi.h), sized to the screen as the HUD is; every
// button works by a tap or a click, and Escape or the back button goes back.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "SenniteMenu.h"
#include "UObject/Object.h"
#include "SenniteGameMenu.generated.h"

class APlayerController;
class UCanvas;
struct FInputKeyEventArgs;

UENUM()
enum class ESenniteGameMenuPage : uint8
{
	Main,
	Settings,
	/** Asked once more before leaving for the main menu, or quitting the game. */
	Leave,
	Quit,
};

UCLASS()
class SENNITE_API USenniteGameMenu : public UObject
{
	GENERATED_BODY()

public:
	void Bind(APlayerController* InController);

	bool IsOpen() const { return bOpen; }
	ESenniteGameMenuPage Page = ESenniteGameMenuPage::Main;

	void Open();
	void Close();
	/** Open on SETTINGS alone, as the first screen does: BACK closes it. */
	void OpenSettings();

	/**
	 * A key, a mouse button or an axis of the player's. True if the menu took it: the keys that open
	 * it while it is closed, and everything while it is open.
	 */
	bool Key(const FInputKeyEventArgs& Params);

	/** A touch on the screen. True if the menu took it: every touch while it is open. */
	bool Touch(ETouchType::Type Type, const FVector2D& Where);

	/** Draw the menu on the HUD's canvas, over everything else. Nothing while it is closed. */
	void Draw(UCanvas* Canvas);

	/** Do what a button does. Public so a check can name what it tapped; the input goes through the same buttons. */
	void Act(FName Action);

	/** The buttons the last frame drew, and each line of text it drew as "name=text": what a check reads. */
	TArray<FSenniteMenuButton> Buttons;
	TArray<FString> LastDrawn;

	/** Where on the screen a button was drawn: its middle, where a finger taps it. False if the last frame drew no such button. */
	bool Centre(FName Action, FVector2D& Out) const;

	/** The player's settings, put into effect: the volume and, if they chose one, the graphics quality. Looking reads its own as it turns. */
	static void ApplySettings(const UWorld* World);

	/** The graphics quality in effect now: 0 (low) to 3 (epic) and above; -1 when the device's own mix of levels is. */
	static int32 QualityNow();

private:
	/** The enabled button at a point of the screen, or none; or with bGreyedOut, the greyed-out one there. */
	FName ButtonAt(const FVector2D& Where, bool bGreyedOut = false) const;
	void Press(const FVector2D& Where);
	void Release(const FVector2D& Where);

	void DrawMain(UCanvas* Canvas);
	void DrawSettings(UCanvas* Canvas);
	void DrawAsk(UCanvas* Canvas, const TCHAR* Question, const TCHAR* Under, FName Yes, const TCHAR* YesLabel);

	void Heading(UCanvas* Canvas, const FString& Title, const FString& Under);
	void Button(UCanvas* Canvas, FName Action, const FString& Label, float X, float Y, float Width, float Height, bool bEnabled = true, bool bPrimary = false, int32 Arrow = 0);
	void Line(UCanvas* Canvas, const TCHAR* Name, const FString& Words, float X, float Y, float Points, const FLinearColor& Colour, float Align = 0.0f);

	UPROPERTY()
	TObjectPtr<APlayerController> Controller;

	bool bOpen = false;
	/** Opened on SETTINGS from the first screen: there is no menu of the game's behind it to go back to. */
	bool bSettingsOnly = false;
	/** The button a press began on, by tap or click; it acts when the press ends on the same button. */
	FName Pressed;
	/** The greyed-out button a press began on: it is heard refused when the press ends on it. */
	FName PressedGreyedOut;
	FName Hovered;
	/** What the menu says about the last thing asked. */
	FString Note;
	float S = 1.0f;
	/** Where the canvas the menu was last drawn on begins on the screen (SenniteUi::CanvasOrigin): a touch or the mouse is on the canvas at its place on the screen less this. */
	FVector2D Origin = FVector2D::ZeroVector;
	/** Whether the mouse pointer was showing before the menu opened: it is put back as it was. */
	bool bCursorBefore = false;
};
