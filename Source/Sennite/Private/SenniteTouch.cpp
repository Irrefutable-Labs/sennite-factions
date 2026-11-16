// The touch controls: see SenniteTouch.h.

#include "SenniteTouch.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HealingActor.h"
#include "InputKeyEventArgs.h"
#include "Misc/CoreDelegates.h"
#include "SenniteCharacter.h"
#include "SenniteCrate.h"
#include "SennitePlayerState.h"
#include "WeaponActor.h"
#include "Widgets/Input/SVirtualJoystick.h"

namespace
{
	/** Mouse counts a drag of one line, on a screen 720 lines high, is worth when looking round. */
	constexpr float LookCountsPerLine = 0.6f;
	/** The stick takes a finger that comes down this far across the screen from the left, and below the top bars. */
	constexpr float StickZoneAcross = 0.42f;
	constexpr float StickZoneTop = 120.0f;
	/** Half the height of the slots along the bottom. */
	constexpr float SlotHalfHeight = 24.0f;
	/** Pushed this far, and this straight ahead, the stick runs. */
	constexpr float RunPush = 0.97f;
	constexpr float RunAhead = 0.85f;
}

bool USenniteTouchControls::Wanted()
{
	return SVirtualJoystick::ShouldDisplayTouchInterface();
}

const TArray<USenniteTouchControls::FButton>& USenniteTouchControls::Buttons()
{
	// Sizes and places for a screen 720 lines high, scaled to the screen; clear of what the HUD draws
	// (health bottom left, the weapon bottom right, the materials on the right, kills top right).
	static const TArray<FButton> All = {
		// the right thumb's cluster
		{ TEXT("FIRE"), EKeys::LeftMouseButton, FVector2D(-115.0, -255.0), 56.0f, false },
		{ TEXT("JUMP"), EKeys::SpaceBar, FVector2D(-255.0, -160.0), 44.0f, false },
		{ TEXT("RELOAD"), EKeys::R, FVector2D(-245.0, -265.0), 32.0f, false },
		{ TEXT("AIM"), EKeys::RightMouseButton, FVector2D(-225.0, -360.0), 32.0f, false },
		{ TEXT("OPEN"), EKeys::E, FVector2D(-385.0, -160.0), 34.0f, false },
		// building, top right under the kills: EDIT (G) at the left end, beside a piece of the player's side or while editing one
		{ TEXT("EDIT"), EKeys::G, FVector2D(-365.0, 92.0), 26.0f, false },
		{ TEXT("WALL"), EKeys::X, FVector2D(-300.0, 92.0), 26.0f, false },
		{ TEXT("RAMP"), EKeys::C, FVector2D(-235.0, 92.0), 26.0f, false },
		{ TEXT("FLOOR"), EKeys::F, FVector2D(-170.0, 92.0), 26.0f, false },
		// what to hold, along the bottom: X is from the middle of the screen
		{ TEXT("RIFLE"), EKeys::Two, FVector2D(-141.0, -62.0), 44.0f, true },
		{ TEXT("SHOTGUN"), EKeys::Three, FVector2D(-47.0, -62.0), 44.0f, true },
		{ TEXT("BANDAGE"), EKeys::Four, FVector2D(47.0, -62.0), 44.0f, true },
		{ TEXT("POTION"), EKeys::Five, FVector2D(141.0, -62.0), 44.0f, true },
	};
	return All;
}

void USenniteTouchControls::Bind(APlayerController* InController)
{
	Controller = InController;
	// a phone that puts the game away takes the fingers off the screen without saying so
	FCoreDelegates::ApplicationWillDeactivateDelegate.AddWeakLambda(this, [this]() { ReleaseAll(); });
}

FVector2D USenniteTouchControls::ViewportSize() const
{
	FVector2D Size(1280.0, 720.0);
	if (const ULocalPlayer* Local = Controller ? Controller->GetLocalPlayer() : nullptr) {
		if (Local->ViewportClient != nullptr) {
			Local->ViewportClient->GetViewportSize(Size);
		}
	}
	return Size;
}

FVector2D USenniteTouchControls::Centre(const FButton& Button, const FVector2D& Viewport) const
{
	const double S = Viewport.Y / 720.0;
	const double X = Button.bSlot ? Viewport.X * 0.5 + Button.Spot.X * S : (Button.Spot.X < 0.0 ? Viewport.X + Button.Spot.X * S : Button.Spot.X * S);
	const double Y = Button.Spot.Y < 0.0 ? Viewport.Y + Button.Spot.Y * S : Button.Spot.Y * S;
	return FVector2D(X, Y);
}

bool USenniteTouchControls::Hits(const FButton& Button, const FVector2D& Viewport, const FVector2D& At) const
{
	const double S = Viewport.Y / 720.0;
	const FVector2D Middle = Centre(Button, Viewport);
	if (Button.bSlot) {
		return FMath::Abs(At.X - Middle.X) <= Button.Radius * S && FMath::Abs(At.Y - Middle.Y) <= SlotHalfHeight * S;
	}
	return FVector2D::Distance(At, Middle) <= Button.Radius * S;
}

FVector2D USenniteTouchControls::StickRest(const FVector2D& Viewport) const
{
	const double S = Viewport.Y / 720.0;
	return FVector2D(175.0 * S, Viewport.Y - 250.0 * S);
}

bool USenniteTouchControls::Shows(const FButton& Button) const
{
	const ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
	if (Hero == nullptr) {
		return false;
	}
	if (Button.Key == EKeys::E) {
		// the same test the HUD makes before it says how to open a crate
		const ASenniteCrate* Rules = GetDefault<ASenniteCrate>();
		return ASenniteCrate::Nearest(Hero->GetWorld(), Hero->GetActorLocation(), Rules->Reach + Rules->Across, true) != nullptr;
	}
	if (Button.Key == EKeys::G) {
		// what G would start editing, or the edit G would finish
		int32 Tile = INDEX_NONE;
		return Hero->GetEditingPiece() != nullptr || Hero->EditablePieceUnderAim(Tile) != nullptr;
	}
	return true;
}

FString USenniteTouchControls::LabelOf(const FButton& Button) const
{
	const ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
	const ASennitePlayerState* State = Controller ? Controller->GetPlayerState<ASennitePlayerState>() : nullptr;
	const bool bBuilding = State != nullptr && State->InBuildMode;
	// editing a piece: FIRE marks the tile in sight, AIM puts every tile back, EDIT finishes
	if (Hero != nullptr && Hero->GetEditingPiece() != nullptr) {
		if (Button.Key == EKeys::LeftMouseButton) {
			return TEXT("TILE");
		}
		if (Button.Key == EKeys::RightMouseButton) {
			return TEXT("RESET");
		}
		if (Button.Key == EKeys::G) {
			return TEXT("DONE");
		}
	}
	if (Button.Key == EKeys::LeftMouseButton && Hero != nullptr) {
		if (bBuilding) {
			return TEXT("BUILD");
		}
		if (Hero->CurrentHealingItem != nullptr) {
			return TEXT("USE");
		}
		if (Hero->CurrentWeapon == nullptr || Hero->CurrentWeapon->WeaponType == 0) {
			return TEXT("SWING");
		}
	}
	if (Button.Key == EKeys::RightMouseButton && bBuilding) {
		return TEXT("MAT");
	}
	return Button.Name;
}

void USenniteTouchControls::Key(const FKey& InKey, EInputEvent Event) const
{
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(InKey, Event, Event == IE_Released ? 0.0f : 1.0f, -1,
		IPlatformInputDeviceMapper::Get().GetDefaultInputDevice()));
}

void USenniteTouchControls::Axis(const FKey& InKey, float Value) const
{
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(InKey, IE_Axis, Value, 1,
		IPlatformInputDeviceMapper::Get().GetDefaultInputDevice()));
}

void USenniteTouchControls::Look(const FVector2D& Drag, float Scale) const
{
	// as the mouse would: right turns right, up looks up
	if (!Drag.IsNearlyZero()) {
		Axis(EKeys::MouseX, Drag.X / Scale * LookCountsPerLine);
		Axis(EKeys::MouseY, -Drag.Y / Scale * LookCountsPerLine);
	}
}

void USenniteTouchControls::Run(bool bRun)
{
	if (bRun == bRunning) {
		return;
	}
	bRunning = bRun;
	// running is W with the left shift key held, as on the keyboard
	if (bRun) {
		Key(EKeys::W, IE_Pressed);
		Key(EKeys::LeftShift, IE_Pressed);
	}
	else {
		Key(EKeys::LeftShift, IE_Released);
		Key(EKeys::W, IE_Released);
		// letting go of W stops the walk; the stick still held walks on from the next frame
		bWalkAgain = bStickHeld;
	}
}

bool USenniteTouchControls::Touch(const FTouchId& Id, ETouchType::Type Type, const FVector2D& At)
{
	if (Controller == nullptr) {
		return false;
	}
	const FVector2D Viewport = ViewportSize();
	const float S = Viewport.Y / 720.0f;
	FFinger* Finger = Fingers.FindByPredicate([&Id](const FFinger& Each) { return Each.Id == Id; });

	if (Type == ETouchType::Began) {
		if (Finger != nullptr) {
			return true;
		}
		FFinger New;
		New.Id = Id;
		New.Last = At;
		const TArray<FButton>& All = Buttons();
		for (int32 Index = 0; Index < All.Num(); ++Index) {
			if (Shows(All[Index]) && Hits(All[Index], Viewport, At)) {
				New.Use = EUse::Button;
				New.Button = Index;
				break;
			}
		}
		if (New.Use == EUse::Button) {
			const bool bAlreadyDown = Fingers.ContainsByPredicate([&New](const FFinger& Each) { return Each.Use == EUse::Button && Each.Button == New.Button; });
			if (!bAlreadyDown) {
				Key(All[New.Button].Key, IE_Pressed);
			}
		}
		else if (!bStickHeld && At.X < Viewport.X * StickZoneAcross && At.Y > StickZoneTop * S) {
			// the stick is where the thumb comes down
			New.Use = EUse::Stick;
			bStickHeld = true;
			StickOrigin = At;
			Stick = FVector2D::ZeroVector;
			if (ASenniteCharacter* Hero = Cast<ASenniteCharacter>(Controller->GetPawn())) {
				Hero->StartWalking();
			}
		}
		Fingers.Add(New);
		return true;
	}

	if (Finger == nullptr) {
		// a finger that came down before the controls were there: theirs all the same
		return true;
	}

	if (Type == ETouchType::Ended) {
		const FFinger Gone = *Finger;
		Fingers.RemoveAll([&Id](const FFinger& Each) { return Each.Id == Id; });
		if (Gone.Use == EUse::Stick) {
			bStickHeld = false;
			Stick = FVector2D::ZeroVector;
			Run(false);
			bWalkAgain = false;
			Axis(EKeys::Gamepad_LeftX, 0.0f);
			Axis(EKeys::Gamepad_LeftY, 0.0f);
			if (ASenniteCharacter* Hero = Cast<ASenniteCharacter>(Controller->GetPawn())) {
				Hero->StopWalking();
			}
		}
		else if (Gone.Use == EUse::Button) {
			const bool bStillDown = Fingers.ContainsByPredicate([&Gone](const FFinger& Each) { return Each.Use == EUse::Button && Each.Button == Gone.Button; });
			if (!bStillDown) {
				Key(Buttons()[Gone.Button].Key, IE_Released);
			}
		}
		return true;
	}

	// the finger moved, or is resting
	if (Finger->Use == EUse::Stick) {
		Stick = ((At - StickOrigin) / (StickReach * S)).GetClampedToMaxSize(1.0);
	}
	else if (Finger->Use == EUse::Look || (Finger->Use == EUse::Button && Buttons()[Finger->Button].Key == EKeys::LeftMouseButton)) {
		// the fire button aims too: a thumb on it can drag to follow a target while firing
		Look(At - Finger->Last, S);
	}
	Finger->Last = At;
	return true;
}

void USenniteTouchControls::Tick()
{
	if (Controller == nullptr) {
		return;
	}
	ASenniteCharacter* Hero = Cast<ASenniteCharacter>(Controller->GetPawn());
	if (bWalkAgain) {
		bWalkAgain = false;
		if (Hero != nullptr && bStickHeld) {
			Hero->StartWalking();
		}
	}
	if (!bStickHeld) {
		return;
	}
	// up the screen is ahead; pushed all the way and straight ahead, the hero runs
	Run(Stick.Size() >= RunPush && -Stick.Y >= RunAhead);
	Axis(EKeys::Gamepad_LeftX, Stick.X);
	Axis(EKeys::Gamepad_LeftY, bRunning ? 0.0f : -Stick.Y);
}

void USenniteTouchControls::ReleaseAll()
{
	if (Controller == nullptr) {
		return;
	}
	while (Fingers.Num() > 0) {
		Touch(Fingers.Last().Id, ETouchType::Ended, Fingers.Last().Last);
	}
}

bool USenniteTouchControls::Where(const FString& Name, const FVector2D& Viewport, FVector2D& OutCentre) const
{
	if (Name == TEXT("STICK")) {
		OutCentre = StickRest(Viewport);
		return true;
	}
	for (const FButton& Button : Buttons()) {
		if (Name == Button.Name) {
			OutCentre = Centre(Button, Viewport);
			return true;
		}
	}
	return false;
}

void USenniteTouchControls::Draw(UCanvas* Canvas)
{
	LastDrawn.Reset();
	if (Canvas == nullptr || Controller == nullptr) {
		return;
	}
	const FVector2D Viewport(Canvas->ClipX, Canvas->ClipY);
	const float S = Canvas->ClipY / 720.0f;
	UFont* Font = GEngine->GetLargeFont();
	const ASenniteCharacter* Hero = Cast<ASenniteCharacter>(Controller->GetPawn());
	const ASennitePlayerState* State = Controller->GetPlayerState<ASennitePlayerState>();
	if (Hero == nullptr || State == nullptr || Font == nullptr) {
		return;
	}
	const FLinearColor Rest(0.0f, 0.0f, 0.0f, 0.3f);
	const FLinearColor Held(1.0f, 1.0f, 1.0f, 0.35f);
	const FLinearColor Chosen(0.9f, 0.75f, 0.2f, 0.45f);
	const FLinearColor Edge(1.0f, 1.0f, 1.0f, 0.55f);
	const FLinearColor Dim(1.0f, 1.0f, 1.0f, 0.35f);

	auto Disc = [Canvas](const FVector2D& At, float Radius, const FLinearColor& Colour) {
		FCanvasNGonItem Item(At, FVector2D(Radius, Radius), 32, Colour);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	};
	auto Ring = [Canvas, S](const FVector2D& At, float Radius, const FLinearColor& Colour) {
		const int32 Sides = 32;
		for (int32 Side = 0; Side < Sides; ++Side) {
			const double A = 2.0 * PI * Side / Sides;
			const double B = 2.0 * PI * (Side + 1) / Sides;
			FCanvasLineItem Line(At + Radius * FVector2D(FMath::Cos(A), FMath::Sin(A)), At + Radius * FVector2D(FMath::Cos(B), FMath::Sin(B)));
			Line.SetColor(Colour);
			Line.LineThickness = 2.0f * S;
			Line.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Line);
		}
	};
	auto Text = [Canvas, Font, S](const FVector2D& At, const FString& Words, int32 Points, const FLinearColor& Colour) {
		FCanvasTextItem Item(At, FText::FromString(Words), Font, Colour);
		Item.Scale = FVector2D(Points * 1.4f * S / FMath::Max(1.0f, Font->GetMaxCharHeight()));
		Item.bCentreX = true;
		Item.bCentreY = true;
		Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
		Canvas->DrawItem(Item);
	};

	// the stick: where it rests until a thumb comes down on the left, then under the thumb
	const FVector2D Base = bStickHeld ? StickOrigin : StickRest(Viewport);
	Disc(Base, StickReach * S, Rest);
	Ring(Base, StickReach * S, Edge);
	Disc(Base + Stick * StickReach * S, 34.0f * S, bStickHeld ? Held : FLinearColor(1.0f, 1.0f, 1.0f, 0.2f));
	LastDrawn.Add(FString::Printf(TEXT("STICK=%.2f,%.2f"), Stick.X, Stick.Y));

	const TArray<FButton>& All = Buttons();
	for (int32 Index = 0; Index < All.Num(); ++Index) {
		const FButton& Button = All[Index];
		if (!Shows(Button)) {
			continue;
		}
		const bool bDown = Fingers.ContainsByPredicate([Index](const FFinger& Each) { return Each.Use == EUse::Button && Each.Button == Index; });
		// what is in hand, or the piece being placed, is marked; what the player does not have is dimmed
		bool bInHand = false;
		bool bHave = true;
		if (Button.Key == EKeys::Two || Button.Key == EKeys::Three) {
			const int32 Kind = Button.Key == EKeys::Two ? 1 : 2;
			bHave = State->EquippedWeapons.Contains(Kind);
			bInHand = !State->InBuildMode && Hero->CurrentHealingItem == nullptr && Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType == Kind;
		}
		else if (Button.Key == EKeys::Four || Button.Key == EKeys::Five) {
			const int32 Kind = Button.Key == EKeys::Four ? 0 : 1;
			bHave = State->HealingItemCounts.IsValidIndex(Kind) && State->HealingItemCounts[Kind] > 0;
			bInHand = Hero->CurrentHealingItem != nullptr && Hero->CurrentHealingItem->HealingType == Kind;
		}
		else if (Button.Key == EKeys::G) {
			bInHand = Hero->GetEditingPiece() != nullptr;
		}
		else if (Button.Key == EKeys::X || Button.Key == EKeys::C || Button.Key == EKeys::F) {
			bInHand = State->InBuildMode && State->BuildMode.Equals(Button.Name, ESearchCase::IgnoreCase);
		}
		const FLinearColor Fill = bDown ? Held : (bInHand ? Chosen : Rest);
		const FVector2D At = Centre(Button, Viewport);
		const FString Label = LabelOf(Button);
		if (Button.bSlot) {
			const FVector2D Half(Button.Radius * S, SlotHalfHeight * S);
			FCanvasTileItem Box(At - Half, Half * 2.0, Fill);
			Box.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Box);
			FCanvasBoxItem Outline(At - Half, Half * 2.0);
			Outline.SetColor(Edge);
			Outline.LineThickness = 2.0f * S;
			Canvas->DrawItem(Outline);
			Text(At, Label, 9, bHave ? FLinearColor::White : Dim);
		}
		else {
			Disc(At, Button.Radius * S, Fill);
			Ring(At, Button.Radius * S, Edge);
			Text(At, Label, Button.Radius >= 40.0f ? 13 : 9, FLinearColor::White);
		}
		LastDrawn.Add(FString::Printf(TEXT("%s=%s"), Button.Name, *Label));
	}
}
