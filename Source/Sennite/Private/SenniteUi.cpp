#include "SenniteUi.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Texture2D.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
	/** The font a line is set in: the engine's Roboto at the size it is shown, so it is drawn crisp. */
	FSlateFontInfo FontOf(float Points, float Scale, bool bBold)
	{
		// a point here is 1.4 pixels of line on a 720-line screen, as the HUD's labels are; Slate's sizes are at 96 to the inch
		const float Pixels = Points * 1.4f * Scale;
		return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), FMath::Max(1.0f, Pixels * 0.70f));
	}

	bool HasSlate()
	{
		return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer() != nullptr;
	}

	/** The corners of a plate with its top left and bottom right cut, in order round it. */
	TArray<FVector2D> CornersOf(const FBox2D& Box, float Cut)
	{
		const FVector2D A = Box.Min;
		const FVector2D B = Box.Max;
		const float C = FMath::Min(Cut, FMath::Min(B.X - A.X, B.Y - A.Y) * 0.5f);
		return { FVector2D(A.X + C, A.Y), FVector2D(B.X, A.Y), FVector2D(B.X, B.Y - C), FVector2D(B.X - C, B.Y), FVector2D(A.X, B.Y), FVector2D(A.X, A.Y + C) };
	}

	void Triangles(UCanvas* Canvas, const TArray<FVector2D>& Points, TFunctionRef<FLinearColor(const FVector2D&)> ColourAt)
	{
		TArray<FCanvasUVTri> List;
		for (int32 Index = 1; Index + 1 < Points.Num(); ++Index) {
			FCanvasUVTri Tri;
			Tri.V0_Pos = Points[0];
			Tri.V1_Pos = Points[Index];
			Tri.V2_Pos = Points[Index + 1];
			Tri.V0_Color = ColourAt(Points[0]);
			Tri.V1_Color = ColourAt(Points[Index]);
			Tri.V2_Color = ColourAt(Points[Index + 1]);
			List.Add(Tri);
		}
		// the canvas's own white square: the corners' colours are the fill
		if (Canvas->DefaultTexture == nullptr || Canvas->DefaultTexture->GetResource() == nullptr) {
			return;
		}
		FCanvasTriangleItem Item(List, Canvas->DefaultTexture->GetResource());
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}
}

FVector2D SenniteUi::CanvasOrigin(const UCanvas* Canvas)
{
	// the engine moves the HUD's canvas in from the screen's left and top by the safe area's padding (UCanvas::ApplySafeZoneTransform)
	return Canvas != nullptr ? FVector2D(Canvas->SafeZonePadX, Canvas->SafeZonePadY) : FVector2D::ZeroVector;
}

FVector2D SenniteUi::Measure(const FString& Line, float Points, float Scale, bool bBold)
{
	if (HasSlate()) {
		return FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Line, FontOf(Points, Scale, bBold)));
	}
	// a game that draws nothing measures with the engine's own font: the sizes only need to be sizes
	UFont* Font = GEngine != nullptr ? GEngine->GetLargeFont() : nullptr;
	if (Font == nullptr) {
		return FVector2D::ZeroVector;
	}
	const float Stretch = Points * 1.4f * Scale / FMath::Max(1.0f, float(Font->GetMaxCharHeight()));
	return FVector2D(Font->GetStringSize(*Line) * Stretch, Font->GetMaxCharHeight() * Stretch);
}

void SenniteUi::Text(UCanvas* Canvas, const FString& Line, float X, float Y, float Points, float Scale, const FLinearColor& Colour, float Align, bool bBold)
{
	if (Canvas == nullptr || Line.IsEmpty()) {
		return;
	}
	X -= Measure(Line, Points, Scale, bBold).X * Align;
	if (HasSlate()) {
		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Line), FontOf(Points, Scale, bBold), Colour);
		// the canvas draws a text item only if it names an engine font as well, though the font above is the one used
		Item.Font = GEngine != nullptr ? GEngine->GetLargeFont() : nullptr;
		Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f * Colour.A), FVector2D(0.0f, FMath::Max(1.0f, 1.5f * Scale)));
		Canvas->DrawItem(Item);
		return;
	}
	if (UFont* Font = GEngine != nullptr ? GEngine->GetLargeFont() : nullptr) {
		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Line), Font, Colour);
		Item.Scale = FVector2D(Points * 1.4f * Scale / FMath::Max(1.0f, float(Font->GetMaxCharHeight())));
		Canvas->DrawItem(Item);
	}
}

void SenniteUi::Plate(UCanvas* Canvas, const FBox2D& Box, float Cut, const FLinearColor& Top, const FLinearColor& Bottom)
{
	if (Canvas == nullptr) {
		return;
	}
	const float High = FMath::Max(1.0f, float(Box.Max.Y - Box.Min.Y));
	Triangles(Canvas, CornersOf(Box, Cut), [&](const FVector2D& At) { return FMath::Lerp(Top, Bottom, float(At.Y - Box.Min.Y) / High); });
}

void SenniteUi::Outline(UCanvas* Canvas, const FBox2D& Box, float Cut, const FLinearColor& Colour, float Thick)
{
	if (Canvas == nullptr) {
		return;
	}
	// each side is a thin quad lying just inside the plate, so the edge is as faint as its colour says
	const TArray<FVector2D> Points = CornersOf(Box, Cut);
	const FVector2D Middle = Box.GetCenter();
	for (int32 Index = 0; Index < Points.Num(); ++Index) {
		const FVector2D From = Points[Index];
		const FVector2D To = Points[(Index + 1) % Points.Num()];
		const FVector2D Along = (To - From).GetSafeNormal();
		FVector2D Inward(-Along.Y, Along.X);
		if (FVector2D::DotProduct(Inward, Middle - From) < 0.0) {
			Inward = -Inward;
		}
		Triangles(Canvas, { From, To, To + Inward * Thick, From + Inward * Thick }, [&](const FVector2D&) { return Colour; });
	}
}

void SenniteUi::Fade(UCanvas* Canvas, const FBox2D& Box, const FLinearColor& From, const FLinearColor& To, bool bDownward)
{
	if (Canvas == nullptr) {
		return;
	}
	const FVector2D Size = Box.Max - Box.Min;
	const TArray<FVector2D> Points = { Box.Min, FVector2D(Box.Max.X, Box.Min.Y), Box.Max, FVector2D(Box.Min.X, Box.Max.Y) };
	Triangles(Canvas, Points, [&](const FVector2D& At) {
		const float Along = bDownward ? float(At.Y - Box.Min.Y) / FMath::Max(1.0f, float(Size.Y)) : float(At.X - Box.Min.X) / FMath::Max(1.0f, float(Size.X));
		return FMath::Lerp(From, To, Along);
	});
}

void SenniteUi::Chevron(UCanvas* Canvas, const FVector2D& Centre, float Size, bool bLeft, const FLinearColor& Colour, float Thick)
{
	if (Canvas == nullptr) {
		return;
	}
	const float Way = bLeft ? -1.0f : 1.0f;
	const FVector2D Tip = Centre + FVector2D(Way * Size * 0.25f, 0.0f);
	// two strokes meeting at the tip, each a thin quad so its ends are square and it is as thick on any screen
	for (const float Side : { -1.0f, 1.0f }) {
		const FVector2D End = Centre + FVector2D(-Way * Size * 0.25f, Side * Size * 0.5f);
		const FVector2D Along = (Tip - End).GetSafeNormal();
		const FVector2D Across(-Along.Y * Thick * 0.5f, Along.X * Thick * 0.5f);
		const FVector2D Past = Tip + Along * Thick * 0.35f;
		Triangles(Canvas, { End - Across, Past - Across, Past + Across, End + Across }, [&](const FVector2D&) { return Colour; });
	}
}

FLinearColor SenniteUi::ButtonPlate(UCanvas* Canvas, const FBox2D& Box, float Scale, EButton State)
{
	const float Cut = 12.0f * Scale;
	const float Thin = FMath::Max(1.0f, 1.5f * Scale);
	const FBox2D Bar(Box.Min, FVector2D(Box.Min.X + 4.0f * Scale, Box.Max.Y));
	switch (State) {
	case EButton::Primary:
		Plate(Canvas, Box, Cut, Gold, GoldDeep);
		Outline(Canvas, Box, Cut, FLinearColor(1.0f, 0.93f, 0.7f, 0.7f), Thin);
		return Ink;
	case EButton::Pressed:
		Plate(Canvas, Box, Cut, GoldDeep, Gold);
		Outline(Canvas, Box, Cut, FLinearColor(1.0f, 1.0f, 1.0f, 0.9f), Thin * 1.5f);
		return Ink;
	case EButton::Hover:
		Plate(Canvas, Box, Cut, FLinearColor(0.16f, 0.19f, 0.26f, 0.95f), FLinearColor(0.07f, 0.085f, 0.13f, 0.95f));
		Outline(Canvas, Box, Cut, Gold, Thin * 1.5f);
		Plate(Canvas, Bar, 0.0f, Gold, Gold);
		return FLinearColor::White;
	case EButton::Disabled:
		Plate(Canvas, Box, Cut, PlateTop.CopyWithNewOpacity(0.45f), PlateBottom.CopyWithNewOpacity(0.45f));
		Outline(Canvas, Box, Cut, Edge.CopyWithNewOpacity(0.08f), Thin);
		return FLinearColor(1.0f, 1.0f, 1.0f, 0.38f);
	default:
		Plate(Canvas, Box, Cut, PlateTop, PlateBottom);
		Outline(Canvas, Box, Cut, Edge, Thin);
		Plate(Canvas, Bar, 0.0f, Gold, GoldDeep);
		return FLinearColor::White;
	}
}

void SenniteUi::Panel(UCanvas* Canvas, const FBox2D& Box, float Scale)
{
	const float Cut = 14.0f * Scale;
	Plate(Canvas, Box, Cut, FLinearColor(0.045f, 0.058f, 0.090f, 0.88f), FLinearColor(0.012f, 0.016f, 0.028f, 0.88f));
	Outline(Canvas, Box, Cut, Edge, FMath::Max(1.0f, 1.5f * Scale));
	// the gold edge runs down the left side below the cut corner
	Plate(Canvas, FBox2D(FVector2D(Box.Min.X, Box.Min.Y + Cut), FVector2D(Box.Min.X + 3.0f * Scale, Box.Max.Y)), 0.0f, Gold, GoldDeep);
}
