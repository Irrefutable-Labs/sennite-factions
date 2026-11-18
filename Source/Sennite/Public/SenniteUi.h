// How the game's menus and panels look, drawn on the canvas: text set crisp at the size it is shown
// (the engine's own Roboto through its font cache, not a small font stretched), plates with two cut
// corners filled top to bottom between two colours, their edges, and the arrows of the arrow buttons.
// The menu (SenniteMenu.cpp) and the base editor's panel (SenniteBaseEditor.cpp) draw with these, so
// they look like one game. Every size given here is in screen pixels; callers scale from a 720-line
// screen, as the HUD does.

#pragma once

#include "CoreMinimal.h"

class UCanvas;

namespace SenniteUi
{
	/** The game's colours. */
	inline const FLinearColor Gold(0.95f, 0.72f, 0.18f, 1.0f);
	inline const FLinearColor GoldDeep(0.80f, 0.50f, 0.08f, 1.0f);
	inline const FLinearColor Ink(0.012f, 0.016f, 0.028f, 1.0f);
	inline const FLinearColor PlateTop(0.085f, 0.105f, 0.150f, 0.90f);
	inline const FLinearColor PlateBottom(0.030f, 0.038f, 0.060f, 0.90f);
	inline const FLinearColor Edge(1.0f, 1.0f, 1.0f, 0.16f);
	inline const FLinearColor Dim(1.0f, 1.0f, 1.0f, 0.72f);

	/** How a button is drawn. */
	enum class EButton : uint8
	{
		Plain,
		/** The one thing the page is for: PLAY, CHOOSE, START, JOIN. */
		Primary,
		/** Under the mouse. */
		Hover,
		/** Held down, by a finger or the mouse. */
		Pressed,
		/** Does nothing now. */
		Disabled,
	};

	/** How wide and high a line of text is drawn, at Points for a 720-line screen scaled by Scale. */
	FVector2D Measure(const FString& Line, float Points, float Scale, bool bBold = true);

	/**
	 * A line of text at Points for a 720-line screen scaled by Scale, with a soft shadow. Align is where
	 * X is in the line: 0 its left end, 0.5 its middle, 1 its right end. Y is its top.
	 */
	void Text(UCanvas* Canvas, const FString& Line, float X, float Y, float Points, float Scale, const FLinearColor& Colour, float Align = 0.0f, bool bBold = true);

	/** A plate whose top left and bottom right corners are cut by Cut, filled from Top at its top to Bottom at its bottom. */
	void Plate(UCanvas* Canvas, const FBox2D& Box, float Cut, const FLinearColor& Top, const FLinearColor& Bottom);

	/** The edge of that plate, a line Thick wide. */
	void Outline(UCanvas* Canvas, const FBox2D& Box, float Cut, const FLinearColor& Colour, float Thick);

	/** A band that fades across the screen from one colour at Box's left to another at its right (or top to bottom). */
	void Fade(UCanvas* Canvas, const FBox2D& Box, const FLinearColor& From, const FLinearColor& To, bool bDownward);

	/** An arrowhead pointing left or right, Size high, centred on Centre. */
	void Chevron(UCanvas* Canvas, const FVector2D& Centre, float Size, bool bLeft, const FLinearColor& Colour, float Thick);

	/** A button's plate, edge and accent for its state. Returns the colour its text is drawn in. */
	FLinearColor ButtonPlate(UCanvas* Canvas, const FBox2D& Box, float Scale, EButton State);

	/** A panel: a dark plate with a gold edge down its left side, for lists and readouts. */
	void Panel(UCanvas* Canvas, const FBox2D& Box, float Scale);
}
