// The controls a player on a phone or tablet plays with: a stick to move with under the left thumb,
// the right side of the screen to look with, and buttons for the rest. A touch on a button presses
// the key that button stands for, through the player controller, as the keyboard would: the game's
// own key bindings decide what happens, so a touch player plays by the same rules as a keyboard one.

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

	/** A touch on the screen, at a point on the game's viewport. True if the controls took it. */
	bool Touch(const FTouchId& Id, ETouchType::Type Type, const FVector2D& At);

	/** Each frame: the stick, as the left stick of a gamepad, and running when it is pushed all the way ahead. */
	void Tick();

	/** Draw the controls on the HUD's canvas. */
	void Draw(UCanvas* Canvas);

	/** Let go of every key and the stick: the player's fingers are no longer on the screen. */
	void ReleaseAll();

	/** Where a control is on a viewport of the given size, by the name drawn on it (FIRE, JUMP, RIFLE, STICK, ...). False if there is none. */
	bool Where(const FString& Name, const FVector2D& Viewport, FVector2D& OutCentre) const;

	/** What the last frame drew, as "name=label" ("FIRE=FIRE", "AIM=MAT", "STICK=0.00,0.70", ...). */
	TArray<FString> LastDrawn;

	/** How far a finger has to drag the stick for it to be pushed all the way, on a screen 720 lines high. */
	static constexpr float StickReach = 80.0f;

private:
	/** A button: the name it goes by, the key it presses, and where it sits on a screen 720 lines high. */
	struct FButton
	{
		const TCHAR* Name;
		FKey Key;
		/** Centre: a negative X is measured from the right edge, a negative Y from the bottom edge. */
		FVector2D Spot;
		/** Round buttons have a radius; the slots along the bottom are boxes of this half-width and a fixed half-height. */
		float Radius;
		bool bSlot;
	};

	static const TArray<FButton>& Buttons();

	/** What a finger on the screen is doing. */
	enum class EUse : uint8 { Stick, Look, Button };

	struct FFinger
	{
		FTouchId Id;
		EUse Use = EUse::Look;
		int32 Button = INDEX_NONE;
		FVector2D Last = FVector2D::ZeroVector;
	};

	/** Which buttons show now: OPEN only beside a closed crate, EDIT only by a piece to edit or while editing, the rest while the player has a hero. */
	bool Shows(const FButton& Button) const;
	/** The label a button carries now (FIRE becomes BUILD in build mode and TILE while editing, AIM becomes MAT or RESET, EDIT becomes DONE, ...). */
	FString LabelOf(const FButton& Button) const;
	FVector2D Centre(const FButton& Button, const FVector2D& Viewport) const;
	bool Hits(const FButton& Button, const FVector2D& Viewport, const FVector2D& At) const;
	FVector2D StickRest(const FVector2D& Viewport) const;
	FVector2D ViewportSize() const;

	void Key(const FKey& InKey, EInputEvent Event) const;
	void Axis(const FKey& InKey, float Value) const;
	void Look(const FVector2D& Drag, float Scale) const;
	void Run(bool bRun);

	UPROPERTY()
	TObjectPtr<APlayerController> Controller;

	TArray<FFinger> Fingers;

	/** The stick: where its finger came down, and how far it is pushed, -1..1 each way (Y down the screen). */
	bool bStickHeld = false;
	FVector2D StickOrigin = FVector2D::ZeroVector;
	FVector2D Stick = FVector2D::ZeroVector;
	bool bRunning = false;
	/** After a run ends, the walk is taken up again on the next frame, once the run's keys have been let go. */
	bool bWalkAgain = false;
};
