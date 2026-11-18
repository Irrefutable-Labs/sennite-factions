// The controls a player on a phone or tablet plays with: a stick to move with under the left thumb,
// the right side of the screen to look with, and buttons for the rest. A touch on a button presses
// the key that button stands for, through the player controller, as the keyboard would: the game's
// own key bindings decide what happens, so a touch player plays by the same rules as a keyboard one.
//
// In the planning view of a base the controls are the editor's: a grid of buttons down the right side,
// each pressing one of the editor's keys (CLOSE is B, WALLET is O, PLACE is F, VOTE is K, ...), named
// for what the key does on the page shown (PLACE is BUY in the shop, DISMANTLE is WITHDRAW among the
// people). A button whose key would do nothing now is dimmed. A tap on the map moves the cursor to the
// cell tapped, by pressing the editor's arrow keys (W A S D) as many times as it takes.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/Object.h"
#include "SenniteTouch.generated.h"

class APlayerController;
class UCanvas;

UCLASS()
class SENNITE_API USenniteTouchControls : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Whether this game shows the touch controls: on a device played by touch, or where the input
	 * settings say to always show a touch interface (bAlwaysShowTouchInterface).
	 */
	static bool Wanted();

	void Bind(APlayerController* InController);

	/** A touch on the screen, at a point on the game's viewport (the screen's own measure). True if the controls took it. */
	bool Touch(const FTouchId& Id, ETouchType::Type Type, const FVector2D& At);

	/** Each frame: the stick, as the left stick of a gamepad, and running when it is pushed all the way ahead. */
	void Tick();

	/** Draw the controls on the HUD's canvas. */
	void Draw(UCanvas* Canvas);

	/** Let go of every key and the stick: the player's fingers are no longer on the screen. */
	void ReleaseAll();

	/** Where on the screen a control is, by the name drawn on it (FIRE, JUMP, RIFLE, STICK, ...): where a finger taps it. The size given is used until the controls have been drawn once. False if there is none. */
	bool Where(const FString& Name, const FVector2D& Viewport, FVector2D& OutCentre) const;

	/** Where, on the viewport now, the control is that presses a key: in play one that is shown; in the planning view any, dimmed or not. False if there is none. */
	bool WhereKey(const FKey& InKey, FVector2D& OutCentre) const;

	/** What the last frame drew, as "name=label" ("FIRE=FIRE", "AIM=MAT", "STICK=0.00,0.70", ...); in the planning view a dimmed button as "name=label (off)". */
	TArray<FString> LastDrawn;

	/** How far a finger has to drag the stick for it to be pushed all the way, on a screen 720 lines high. */
	static constexpr float StickReach = 80.0f;

private:
	/** A button: the name it goes by, the key it presses, and where it sits on a screen 720 lines high. */
	struct FButton
	{
		const TCHAR* Name;
		FKey Key;
		/** Centre: a negative X is measured from the right edge, a negative Y from the bottom edge. For a cell of the editor's grid: its column (0 to 3, left to right) and row. */
		FVector2D Spot;
		/** Round buttons have a radius; the slots along the bottom are boxes of this half-width and a fixed half-height. */
		float Radius;
		bool bSlot;
		/** A cell of the planning view's grid of buttons, down the right side. */
		bool bCell = false;
	};

	static const TArray<FButton>& Buttons();
	/** The planning view's buttons. */
	static const TArray<FButton>& EditorButtons();

	/** What a finger on the screen is doing. */
	enum class EUse : uint8 { Stick, Look, Button, EditorButton, Map };

	struct FFinger
	{
		FTouchId Id;
		EUse Use = EUse::Look;
		int32 Button = INDEX_NONE;
		FVector2D Last = FVector2D::ZeroVector;
	};

	/** Which buttons show now: OPEN only beside a closed crate, RELOAD only with a gun in hand, EDIT only by a piece to edit or while editing, the rest while the player has a hero. */
	bool Shows(const FButton& Button) const;
	/** The label a button carries now (FIRE becomes BUILD in build mode and TILE while editing, AIM becomes MAT or RESET, EDIT becomes DONE, ...). */
	FString LabelOf(const FButton& Button) const;
	FVector2D Centre(const FButton& Button, const FVector2D& Viewport) const;
	bool Hits(const FButton& Button, const FVector2D& Viewport, const FVector2D& At) const;
	FVector2D StickRest(const FVector2D& Viewport) const;
	FVector2D ViewportSize() const;

	/** Whether the planning view of a base is open: the editor's buttons are the controls. */
	bool Planning() const;
	/** Whether an editor button's key would do anything now; the label it carries on the page shown. */
	bool EditorLive(const FButton& Button) const;
	FString EditorLabel(const FButton& Button) const;
	/** A tap on the map in the planning view: the cursor goes to the cell tapped. */
	void TapMap(const FVector2D& At) const;
	void DrawEditor(UCanvas* Canvas);
	/** The stick's thumb is off it: the hero stops. */
	void EndStick();

	void Key(const FKey& InKey, EInputEvent Event) const;
	void Axis(const FKey& InKey, float Value) const;
	void Look(const FVector2D& Drag, float Scale) const;
	void Run(bool bRun);

	UPROPERTY()
	TObjectPtr<APlayerController> Controller;

	/**
	 * Where the canvas the controls were last drawn on begins on the screen, and its size. On a phone the HUD
	 * is drawn inside the screen's safe area: a touch, in the screen's measure, is on the canvas at the touch less this.
	 */
	FVector2D DrawnOrigin = FVector2D::ZeroVector;
	FVector2D DrawnSize = FVector2D::ZeroVector;

	TArray<FFinger> Fingers;

	/** The stick: where its finger came down, and how far it is pushed, -1..1 each way (Y down the screen). */
	bool bStickHeld = false;
	FVector2D StickOrigin = FVector2D::ZeroVector;
	FVector2D Stick = FVector2D::ZeroVector;
	bool bRunning = false;
	/** After a run ends, the walk is taken up again on the next frame, once the run's keys have been let go. */
	bool bWalkAgain = false;
};
