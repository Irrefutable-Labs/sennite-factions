// The touch controls: see SenniteTouch.h.

#include "SenniteTouch.h"
#include "SenniteUi.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/PlatformTime.h"
#include "HealingActor.h"
#include "InputKeyEventArgs.h"
#include "Misc/CoreDelegates.h"
#include "SenniteBaseEditor.h"
#include "SenniteCharacter.h"
#include "SenniteCrate.h"
#include "SennitePlayerState.h"
#include "SennitePublisher.h"
#include "SenniteSounds.h"
#include "WeaponActor.h"
#include "Widgets/Input/SVirtualJoystick.h"

namespace
{
	/** Mouse counts a drag of one line, on a screen 720 lines high, is worth when looking round. */
	constexpr float LookCountsPerLine = 0.6f;
	/** The stick takes a finger that comes down this far across the screen from the left, and below the top bars. */
	constexpr float StickZoneAcross = 0.42f;
	constexpr float StickZoneTop = 120.0f;
	/** The wheel when open: how far its places are from its middle, how big each is, and the middle where a finger lets go and nothing changes. */
	constexpr float WheelReach = 132.0f;
	/** The places go round from this far either side of the top, in degrees, leaving the bottom clear for the wheel's own button. */
	constexpr float WheelSpread = 150.0f;
	constexpr float PlaceRadius = 34.0f;
	constexpr float WheelMiddleRadius = 44.0f;
	/** A finger slid this far off the wheel's button opens it at once. */
	constexpr float WheelSlide = 24.0f;
	/** Pushed this far, and this straight ahead, the stick runs. */
	constexpr float RunPush = 0.97f;
	constexpr float RunAhead = 0.85f;
	/** The planning view's grid of buttons: each cell's half-width and half-height, the gap between cells, and the grid's margins from the right and the top. */
	constexpr float CellHalfWidth = 46.0f;
	constexpr float CellHalfHeight = 24.0f;
	constexpr float CellGap = 6.0f;
	constexpr float CellsRight = 16.0f;
	constexpr float CellsTop = 16.0f;
	constexpr int32 CellColumns = 4;
	/** At most this many key presses move the cursor to a tapped cell. */
	constexpr int32 MostSteps = 64;
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
		// the base editor (B), left of the building row: only where there is a publisher's server and so an editor
		{ TEXT("BASE"), EKeys::B, FVector2D(-430.0, 92.0), 26.0f, false },
		// the in-game menu (Escape), top left, clear of everything: there with or without a hero
		{ TEXT("MENU"), EKeys::Escape, FVector2D(58.0, 50.0), 26.0f, false },
		// what to hold: one button, the wheel, at the bottom in the middle (X is from the middle of the screen)
		{ TEXT("WHEEL"), EKeys::Invalid, FVector2D(0.0, -78.0), 40.0f, true },
	};
	return All;
}

const TArray<USenniteTouchControls::FWheelPlace>& USenniteTouchControls::WheelPlaces()
{
	// the hero's own weapon, the guns, the blades, then what heals, each on the key the keyboard holds it with
	static const TArray<FWheelPlace> All = {
		{ TEXT("OWN"), EKeys::One, 0, INDEX_NONE },
		{ TEXT("RIFLE"), EKeys::Two, 1, INDEX_NONE },
		{ TEXT("SHOTGUN"), EKeys::Three, 2, INDEX_NONE },
		{ TEXT("ENERGY"), EKeys::Six, 3, INDEX_NONE },
		{ TEXT("SNIPER"), EKeys::Seven, 4, INDEX_NONE },
		{ TEXT("AXE"), EKeys::Eight, 5, INDEX_NONE },
		{ TEXT("KATANA"), EKeys::Nine, 6, INDEX_NONE },
		{ TEXT("BANDAGE"), EKeys::Four, INDEX_NONE, 0 },
		{ TEXT("POTION"), EKeys::Five, INDEX_NONE, 1 },
		// the mining robot: let go on it, and it goes to the deposit the player aims at
		{ TEXT("ROBOT"), EKeys::T, INDEX_NONE, INDEX_NONE, true },
	};
	return All;
}

const TArray<USenniteTouchControls::FButton>& USenniteTouchControls::EditorButtons()
{
	// The editor's keys (SenniteBaseEditor.h), in a grid down the right side, clear of the editor's panel
	// on the left. Spot is the cell's column and row.
	static const TArray<FButton> All = {
		{ TEXT("CLOSE"), EKeys::B, FVector2D(0, 0), CellHalfWidth, false, true },
		{ TEXT("WALLET"), EKeys::O, FVector2D(1, 0), CellHalfWidth, false, true },
		{ TEXT("PROVE"), EKeys::M, FVector2D(2, 0), CellHalfWidth, false, true },
		{ TEXT("PARCEL"), EKeys::Tab, FVector2D(3, 0), CellHalfWidth, false, true },
		{ TEXT("PEOPLE"), EKeys::P, FVector2D(0, 1), CellHalfWidth, false, true },
		{ TEXT("SHOP"), EKeys::H, FVector2D(1, 1), CellHalfWidth, false, true },
		{ TEXT("INVENTORY"), EKeys::I, FVector2D(2, 1), CellHalfWidth, false, true },
		{ TEXT("JOIN"), EKeys::J, FVector2D(3, 1), CellHalfWidth, false, true },
		{ TEXT("UP"), EKeys::W, FVector2D(0, 2), CellHalfWidth, false, true },
		{ TEXT("DOWN"), EKeys::S, FVector2D(1, 2), CellHalfWidth, false, true },
		{ TEXT("LEFT"), EKeys::A, FVector2D(2, 2), CellHalfWidth, false, true },
		{ TEXT("RIGHT"), EKeys::D, FVector2D(3, 2), CellHalfWidth, false, true },
		{ TEXT("< PREFAB"), EKeys::Q, FVector2D(0, 3), CellHalfWidth, false, true },
		{ TEXT("PREFAB >"), EKeys::E, FVector2D(1, 3), CellHalfWidth, false, true },
		{ TEXT("TURN"), EKeys::R, FVector2D(2, 3), CellHalfWidth, false, true },
		{ TEXT("PLACE"), EKeys::F, FVector2D(3, 3), CellHalfWidth, false, true },
		{ TEXT("FOUND"), EKeys::N, FVector2D(0, 4), CellHalfWidth, false, true },
		{ TEXT("COMMIT"), EKeys::C, FVector2D(1, 4), CellHalfWidth, false, true },
		{ TEXT("RESOURCE"), EKeys::One, FVector2D(2, 4), CellHalfWidth, false, true },
		{ TEXT("AMOUNT"), EKeys::Z, FVector2D(3, 4), CellHalfWidth, false, true },
		{ TEXT("TAKE BACK"), EKeys::T, FVector2D(0, 5), CellHalfWidth, false, true },
		{ TEXT("REPLACE"), EKeys::V, FVector2D(1, 5), CellHalfWidth, false, true },
		{ TEXT("DISMANTLE"), EKeys::X, FVector2D(2, 5), CellHalfWidth, false, true },
		{ TEXT("REBUILD"), EKeys::G, FVector2D(3, 5), CellHalfWidth, false, true },
		{ TEXT("VOTE"), EKeys::K, FVector2D(0, 6), CellHalfWidth, false, true },
		{ TEXT("UNVOTE"), EKeys::U, FVector2D(1, 6), CellHalfWidth, false, true },
		{ TEXT("LEAVE"), EKeys::L, FVector2D(2, 6), CellHalfWidth, false, true },
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
	// the canvas the controls are drawn on, once they have been: on a phone it is the screen's safe area
	if (!DrawnSize.IsZero()) {
		return DrawnSize;
	}
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
	if (Button.bCell) {
		const double Step = 2.0 * CellHalfWidth + CellGap;
		return FVector2D(Viewport.X - (CellsRight + CellHalfWidth + (CellColumns - 1 - Button.Spot.X) * Step) * S,
			(CellsTop + CellHalfHeight + Button.Spot.Y * (2.0 * CellHalfHeight + CellGap)) * S);
	}
	const double X = Button.bWheel ? Viewport.X * 0.5 + Button.Spot.X * S : (Button.Spot.X < 0.0 ? Viewport.X + Button.Spot.X * S : Button.Spot.X * S);
	const double Y = Button.Spot.Y < 0.0 ? Viewport.Y + Button.Spot.Y * S : Button.Spot.Y * S;
	return FVector2D(X, Y);
}

bool USenniteTouchControls::Hits(const FButton& Button, const FVector2D& Viewport, const FVector2D& At) const
{
	const double S = Viewport.Y / 720.0;
	const FVector2D Middle = Centre(Button, Viewport);
	if (Button.bCell) {
		return FMath::Abs(At.X - Middle.X) <= CellHalfWidth * S && FMath::Abs(At.Y - Middle.Y) <= CellHalfHeight * S;
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
	if (Button.Key == EKeys::Escape) {
		// the menu is the player's whether they are playing or watching
		return true;
	}
	const ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
	if (Hero == nullptr) {
		return false;
	}
	if (Button.Key == EKeys::E) {
		// the same test the HUD makes before it says how to open a crate
		const ASenniteCrate* Rules = GetDefault<ASenniteCrate>();
		return ASenniteCrate::Nearest(Hero->GetWorld(), Hero->GetActorLocation(), Rules->Reach + Rules->Across, true) != nullptr;
	}
	if (Button.Key == EKeys::B) {
		// the base editor is in a world only when a publisher's server is set
		return ASenniteBaseEditor::Find(Hero->GetWorld()) != nullptr;
	}
	if (Button.Key == EKeys::RightMouseButton && LabelOf(Button) == TEXT("AIM")
		&& ((Hero->bHeroMelee && Hero->CurrentWeapon == nullptr) || (Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->bMelee))) {
		// fists, what a hero swings, an axe and a katana are not aimed: there is no AIM until a gun is in hand. The owner,
		// 2026-11-19, of a melee weapon: "aim (which should be disabled when it's a melee weapon)".
		return false;
	}
	if (Button.Key == EKeys::G) {
		// what G would start editing, or the edit G would finish
		int32 Tile = INDEX_NONE;
		return Hero->GetEditingPiece() != nullptr || Hero->EditablePieceUnderAim(Tile) != nullptr;
	}
	return true;
}

bool USenniteTouchControls::Loads() const
{
	// only a gun reloads: the hero's own weapon, a bandage or a potion has nothing to load. While a piece is being
	// edited the button is REMOVE, which takes the piece away whatever is in hand.
	const ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
	return Hero != nullptr && (Hero->GetEditingPiece() != nullptr || (Hero->CurrentWeapon != nullptr && Hero->CurrentWeapon->WeaponType > 0 && !Hero->CurrentWeapon->bMelee));
}

bool USenniteTouchControls::Has(const FWheelPlace& Place) const
{
	const ASennitePlayerState* State = Controller ? Controller->GetPlayerState<ASennitePlayerState>() : nullptr;
	if (State == nullptr) {
		return false;
	}
	if (Place.bRobot) {
		// a robot shot down is gone until the next game: it is not on the wheel
		return !State->bRobotDown && Controller->GetPawn() != nullptr;
	}
	if (Place.Healing != INDEX_NONE) {
		return State->HealingItemCounts.IsValidIndex(Place.Healing) && State->HealingItemCounts[Place.Healing] > 0;
	}
	// the hero's own weapon is always the hero's
	return Place.Weapon == 0 || State->EquippedWeapons.Contains(Place.Weapon);
}

bool USenniteTouchControls::InHand(const FWheelPlace& Place) const
{
	const ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
	const ASennitePlayerState* State = Controller ? Controller->GetPlayerState<ASennitePlayerState>() : nullptr;
	if (Hero == nullptr || State == nullptr || State->InBuildMode || Place.bRobot) {
		return false;
	}
	if (Place.Healing != INDEX_NONE) {
		return Hero->CurrentHealingItem != nullptr && Hero->CurrentHealingItem->HealingType == Place.Healing;
	}
	if (Hero->CurrentHealingItem != nullptr) {
		return false;
	}
	const int32 Held = Hero->CurrentWeapon != nullptr ? Hero->CurrentWeapon->WeaponType : 0;
	return Held == Place.Weapon;
}

FVector2D USenniteTouchControls::WheelMiddle(const FVector2D& Viewport) const
{
	// round the wheel's button, moved in from the screen's edges until the whole wheel is on the screen
	const double S = Viewport.Y / 720.0;
	const double Room = (WheelReach + PlaceRadius + 8.0) * S;
	const FVector2D Button = Centre(Buttons().Last(), Viewport);
	return FVector2D(FMath::Clamp(Button.X, Room, FMath::Max(Room, Viewport.X - Room)), FMath::Clamp(Button.Y, Room, FMath::Max(Room, Viewport.Y - Room)));
}

TArray<int32> USenniteTouchControls::Shown() const
{
	TArray<int32> Places;
	for (int32 Index = 0; Index < WheelPlaces().Num(); ++Index) {
		if (Has(WheelPlaces()[Index])) {
			Places.Add(Index);
		}
	}
	return Places;
}

FVector2D USenniteTouchControls::PlaceCentre(int32 Index, const FVector2D& Viewport) const
{
	// what the player has, side by side round the middle and centred on its top, one place apart, clockwise in the
	// wheel's order; the bottom, where the wheel's button is, stays clear even with everything on it
	const double S = Viewport.Y / 720.0;
	const TArray<int32> Places = Shown();
	const int32 At = FMath::Max(0, Places.IndexOfByKey(Index));
	const double Apart = 2.0 * WheelSpread / FMath::Max(1, WheelPlaces().Num() - 1);
	const double Turn = FMath::DegreesToRadians((At - (Places.Num() - 1) * 0.5) * Apart);
	return WheelMiddle(Viewport) + WheelReach * S * FVector2D(FMath::Sin(Turn), -FMath::Cos(Turn));
}

int32 USenniteTouchControls::PlaceUnder(const FVector2D& At, const FVector2D& Viewport) const
{
	// the place nearest the finger; none in the middle, nor on the wheel's own button, where a finger that held it open
	// and never moved lets go
	const double S = Viewport.Y / 720.0;
	const FButton& Wheel = Buttons().Last();
	if (FVector2D::Distance(At, WheelMiddle(Viewport)) < WheelMiddleRadius * S || Hits(Wheel, Viewport, At)) {
		return INDEX_NONE;
	}
	int32 Nearest = INDEX_NONE;
	double Best = TNumericLimits<double>::Max();
	for (const int32 Index : Shown()) {
		const double Distance = FVector2D::Distance(At, PlaceCentre(Index, Viewport));
		if (Distance < Best) {
			Best = Distance;
			Nearest = Index;
		}
	}
	return Nearest;
}

void USenniteTouchControls::NextInHand()
{
	// after what is in hand, the next thing the player has, round again; nothing else had, a tap is refused
	const TArray<FWheelPlace>& Places = WheelPlaces();
	int32 Now = Places.IndexOfByPredicate([this](const FWheelPlace& Place) { return InHand(Place); });
	for (int32 Step = 1; Step < Places.Num(); ++Step) {
		const FWheelPlace& Next = Places[(FMath::Max(Now, 0) + Step) % Places.Num()];
		// a tap goes through what is held in hand: the robot is sent from the open wheel, never by a tap
		if (!Next.bRobot && Has(Next) && !InHand(Next)) {
			USenniteSounds::Play(Controller, TEXT("ui.press"));
			Key(Next.Key, IE_Pressed);
			Key(Next.Key, IE_Released);
			return;
		}
	}
	USenniteSounds::Play(Controller, TEXT("ui.denied"));
}

void USenniteTouchControls::TakePlace(int32 Index)
{
	if (!WheelPlaces().IsValidIndex(Index)) {
		return; // let go in the middle: what is in hand stays
	}
	const FWheelPlace& Place = WheelPlaces()[Index];
	if (!Has(Place)) {
		return; // gone from the player since the wheel opened: nothing comes to hand
	}
	USenniteSounds::Play(Controller, TEXT("ui.press"));
	if (!InHand(Place)) {
		Key(Place.Key, IE_Pressed);
		Key(Place.Key, IE_Released);
	}
}

void USenniteTouchControls::OpenWheel()
{
	if (bWheelOpen) {
		return;
	}
	bWheelOpen = true;
	WheelPick = INDEX_NONE;
	USenniteSounds::Play(Controller, TEXT("ui.open"));
}

bool USenniteTouchControls::AimLatches(const FButton& Button) const
{
	// while it reads AIM: in build mode it changes the material, and while editing it resets, each at a tap
	return Button.Key == EKeys::RightMouseButton && LabelOf(Button) == TEXT("AIM");
}

FString USenniteTouchControls::LabelOf(const FButton& Button) const
{
	const ASenniteCharacter* Hero = Controller ? Cast<ASenniteCharacter>(Controller->GetPawn()) : nullptr;
	const ASennitePlayerState* State = Controller ? Controller->GetPlayerState<ASennitePlayerState>() : nullptr;
	const bool bBuilding = State != nullptr && State->InBuildMode;
	// editing a piece: FIRE marks the tile in sight, AIM puts every tile back, EDIT finishes, RELOAD takes the piece away
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
		if (Button.Key == EKeys::R) {
			return TEXT("REMOVE");
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
			// no gun in hand: the hero's own weapon
			return TEXT("ATTACK");
		}
		if (Hero->CurrentWeapon->bMelee) {
			// an axe or a katana
			return TEXT("SWING");
		}
	}
	if (Button.Key == EKeys::RightMouseButton && bBuilding) {
		return TEXT("MAT");
	}
	if (Button.bWheel) {
		// the wheel names what is in hand; while building, nothing of it is
		for (const FWheelPlace& Place : WheelPlaces()) {
			if (InHand(Place)) {
				return Place.Name;
			}
		}
		return TEXT("HOLD");
	}
	return Button.Name;
}

bool USenniteTouchControls::Planning() const
{
	return Controller != nullptr && ASenniteBaseEditor::IsPlanning(Controller->GetWorld());
}

bool USenniteTouchControls::EditorLive(const FButton& Button) const
{
	const ASenniteBaseEditor* Editor = Controller != nullptr ? ASenniteBaseEditor::Find(Controller->GetWorld()) : nullptr;
	const UGameInstance* Game = Controller != nullptr ? Controller->GetGameInstance() : nullptr;
	const USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
	if (Editor == nullptr || Server == nullptr) {
		return false;
	}
	// the same conditions the editor's own keys go by (SenniteBaseEditor.cpp): what a key would do on the page shown
	const FSenniteBaseView* View = Server->Bases.Find(Editor->Parcel);
	const bool bMember = View != nullptr && View->bMember;
	const bool bPage = Editor->bPeople || Editor->bShop || Editor->bInventory;
	const FSenniteStructureView* Structure = bPage ? nullptr : Editor->Selected();
	const bool bOwnStructure = Structure != nullptr && bMember;
	ASenniteBaseEditor::FPersonRow Row;
	const bool bRow = Editor->bPeople && Editor->ChosenPerson(Row);
	const bool bMemberRow = bRow && Row.Kind == ASenniteBaseEditor::FPersonRow::EKind::Member;
	const FKey& K = Button.Key;
	if (K == EKeys::B || K == EKeys::W || K == EKeys::S) {
		return true;
	}
	if (K == EKeys::O) {
		return !Server->bConnected && !Server->ConnectCode.IsEmpty();
	}
	if (K == EKeys::M) {
		return Server->bConnected;
	}
	if (K == EKeys::Tab) {
		return !bPage && Server->Parcels.Num() > 1;
	}
	if (K == EKeys::P) {
		return Editor->bPeople || bMember;
	}
	if (K == EKeys::H) {
		return Editor->bShop || Server->bConnected;
	}
	if (K == EKeys::I) {
		return Editor->bPeople ? bRow && Row.Kind == ASenniteBaseEditor::FPersonRow::EKind::Free : Editor->bInventory || Server->bConnected;
	}
	if (K == EKeys::J) {
		return View != nullptr && View->bInvited;
	}
	if (K == EKeys::A || K == EKeys::D || K == EKeys::Q || K == EKeys::E || K == EKeys::R) {
		return !bPage;
	}
	if (K == EKeys::F) {
		return Editor->bShop || Editor->bInventory || (!bPage && bMember && Structure == nullptr && Editor->Chosen() != nullptr);
	}
	if (K == EKeys::N) {
		return !bPage && View != nullptr && View->bKnown && !View->bAssigned && Server->bConnected;
	}
	if (K == EKeys::C || K == EKeys::One || K == EKeys::Z) {
		return bOwnStructure && Structure->Needs.ContainsByPredicate([](const FSenniteNeed& Need) { return Need.Shortfall > 0; });
	}
	if (K == EKeys::T) {
		return bOwnStructure && Structure->Committed.ContainsByPredicate([](const FSenniteCommitted& One) { return One.bMine; });
	}
	if (K == EKeys::V) {
		return bOwnStructure && Editor->Chosen() != nullptr && Editor->Chosen()->Id != Structure->PrefabId;
	}
	if (K == EKeys::X) {
		return Editor->bPeople ? bRow && Row.Kind == ASenniteBaseEditor::FPersonRow::EKind::Invited : bOwnStructure;
	}
	if (K == EKeys::G) {
		return bOwnStructure && Structure->bDestroyed;
	}
	if (K == EKeys::K) {
		return bMemberRow && !Row.Person->bYou && !Row.Person->bVoted;
	}
	if (K == EKeys::U) {
		return bMemberRow && Row.Person->bVoted;
	}
	if (K == EKeys::L) {
		return bMemberRow && Row.Person->bYou;
	}
	return true;
}

FString USenniteTouchControls::EditorLabel(const FButton& Button) const
{
	const ASenniteBaseEditor* Editor = Controller != nullptr ? ASenniteBaseEditor::Find(Controller->GetWorld()) : nullptr;
	if (Editor == nullptr) {
		return Button.Name;
	}
	// one key does what the page shown makes of it
	if (Button.Key == EKeys::F && Editor->bShop) {
		return TEXT("BUY");
	}
	if (Button.Key == EKeys::F && Editor->bInventory) {
		const UGameInstance* Game = Controller->GetGameInstance();
		const USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
		const bool bWorn = Server != nullptr && Server->Inventory.IsValidIndex(Editor->InventoryIndex) && Server->Wearing == Server->Inventory[Editor->InventoryIndex].SkinId;
		return bWorn ? TEXT("TAKE OFF") : TEXT("WEAR");
	}
	if (Button.Key == EKeys::I && Editor->bPeople) {
		return TEXT("INVITE");
	}
	if (Button.Key == EKeys::X && Editor->bPeople) {
		return TEXT("WITHDRAW");
	}
	if ((Button.Key == EKeys::P && Editor->bPeople) || (Button.Key == EKeys::H && Editor->bShop) || (Button.Key == EKeys::I && Editor->bInventory)) {
		return TEXT("BACK");
	}
	return Button.Name;
}

void USenniteTouchControls::TapMap(const FVector2D& At) const
{
	const ASenniteBaseEditor* Editor = ASenniteBaseEditor::Find(Controller->GetWorld());
	const UGameInstance* Game = Controller->GetGameInstance();
	const USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
	if (Editor == nullptr || Server == nullptr || Server->Grid <= 0 || Editor->bPeople || Editor->bShop || Editor->bInventory) {
		return;
	}
	FVector Origin, Direction;
	if (!Controller->DeprojectScreenPositionToWorld(At.X, At.Y, Origin, Direction) || Direction.Z > -0.01) {
		return;
	}
	// where the line through the tap meets the ground under the cursor: the view is from straight above
	double Ground = 0.0;
	FHitResult Hit;
	if (Controller->GetWorld()->LineTraceSingleByObjectType(Hit, FVector(Editor->Cursor.X, Editor->Cursor.Y, 20000.0), FVector(Editor->Cursor.X, Editor->Cursor.Y, -20000.0),
			FCollisionObjectQueryParams(ECC_WorldStatic))) {
		Ground = Hit.ImpactPoint.Z;
	}
	const FVector Point = Origin + Direction * ((Ground - Origin.Z) / Direction.Z);
	// the editor's own keys move the cursor a cell at a time: W up the map (+X), S down, D across (+Y), A back
	const int32 Up = FMath::FloorToInt32((Point.X - Editor->Cursor.X) / Server->Grid);
	const int32 Across = FMath::FloorToInt32((Point.Y - Editor->Cursor.Y) / Server->Grid);
	for (int32 Step = 0; Step < FMath::Min(FMath::Abs(Up), MostSteps); ++Step) {
		Key(Up > 0 ? EKeys::W : EKeys::S, IE_Pressed);
		Key(Up > 0 ? EKeys::W : EKeys::S, IE_Released);
	}
	for (int32 Step = 0; Step < FMath::Min(FMath::Abs(Across), MostSteps); ++Step) {
		Key(Across > 0 ? EKeys::D : EKeys::A, IE_Pressed);
		Key(Across > 0 ? EKeys::D : EKeys::A, IE_Released);
	}
}

void USenniteTouchControls::EndStick()
{
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

bool USenniteTouchControls::Touch(const FTouchId& Id, ETouchType::Type Type, const FVector2D& Screen)
{
	if (Controller == nullptr) {
		return false;
	}
	// the touch on the canvas the controls are drawn on
	const FVector2D At = Screen - DrawnOrigin;
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
		if (Planning()) {
			// the planning view: a button presses the editor's key; anywhere else on the map moves the cursor there
			const TArray<FButton>& Cells = EditorButtons();
			for (int32 Index = 0; Index < Cells.Num(); ++Index) {
				if (Hits(Cells[Index], Viewport, At)) {
					New.Use = EUse::EditorButton;
					New.Button = Index;
					break;
				}
			}
			if (New.Use == EUse::EditorButton) {
				Key(Cells[New.Button].Key, IE_Pressed);
			}
			else {
				New.Use = EUse::Map;
				// the map is the world's, seen through the whole screen
				TapMap(Screen);
			}
			Fingers.Add(New);
			return true;
		}
		const TArray<FButton>& All = Buttons();
		for (int32 Index = 0; Index < All.Num(); ++Index) {
			if (Shows(All[Index]) && Hits(All[Index], Viewport, At)) {
				New.Use = All[Index].bWheel ? EUse::Wheel : EUse::Button;
				New.Button = Index;
				break;
			}
		}
		if (New.Use == EUse::Wheel) {
			// the wheel: a tap takes the next thing in hand; held, or slid off, it opens (in Tick and below)
			if (Fingers.ContainsByPredicate([](const FFinger& Each) { return Each.Use == EUse::Wheel; })) {
				return true; // a second finger on it is nobody's
			}
			bWheelOpen = false;
			WheelPick = INDEX_NONE;
			WheelDownAt = FPlatformTime::Seconds();
		}
		else if (New.Use == EUse::Button) {
			const bool bAlreadyDown = Fingers.ContainsByPredicate([&New](const FFinger& Each) { return Each.Use == EUse::Button && Each.Button == New.Button; });
			if (!bAlreadyDown) {
				// In play a button is heard as the owner's own press recording, the bigger one the menus' buttons play,
				// and not as the small tick. The owner, 2026-11-19, asked whether the buttons in a match should play
				// it: "It should play, not those checks, but the bigger sounds for the menu buttons, the AIM, and shoot
				// and all that, and RAMP." JUMP alone is heard only as the jump (the same day: "The jump button makes an
				// awful sound. It doesn't sound like a jump." and, once it was the jump alone, "The jump sounds good
				// now."). RELOAD with no gun in hand and no piece being edited does nothing, so that tap is heard as refused.
				const FButton& Tapped = All[New.Button];
				bool bRefused = false;
				if (Tapped.Key == EKeys::R) {
					bRefused = !Loads();
				}
				if (bRefused) {
					USenniteSounds::Play(Controller, TEXT("ui.denied"));
				}
				else if (Tapped.Key != EKeys::SpaceBar) {
					USenniteSounds::Play(Controller, TEXT("ui.press"));
				}
			}
			if (AimLatches(All[New.Button])) {
				// a thumb cannot hold AIM and fire too: one tap aims, the next lets go
				const ASenniteCharacter* Hero = Cast<ASenniteCharacter>(Controller->GetPawn());
				New.bLatched = true;
				if (!bAlreadyDown) {
					Key(All[New.Button].Key, Hero != nullptr && Hero->AimedIn ? IE_Released : IE_Pressed);
				}
			}
			else if (!bAlreadyDown) {
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
			if (bStickHeld) {
				EndStick();
			}
		}
		else if (Gone.Use == EUse::EditorButton) {
			Key(EditorButtons()[Gone.Button].Key, IE_Released);
		}
		else if (Gone.Use == EUse::Wheel) {
			// let go: of the open wheel, on a place; of the button, a tap
			if (bWheelOpen) {
				TakePlace(PlaceUnder(At, Viewport));
			}
			else {
				NextInHand();
			}
			bWheelOpen = false;
			WheelPick = INDEX_NONE;
		}
		else if (Gone.Use == EUse::Button) {
			const bool bStillDown = Fingers.ContainsByPredicate([&Gone](const FFinger& Each) { return Each.Use == EUse::Button && Each.Button == Gone.Button; });
			if (!bStillDown && !Gone.bLatched) {
				Key(Buttons()[Gone.Button].Key, IE_Released);
			}
		}
		return true;
	}

	// the finger moved, or is resting
	if (Finger->Use == EUse::Wheel) {
		// slid off the wheel's button it opens at once; open, the place under the finger is marked
		if (!bWheelOpen && FVector2D::Distance(At, Centre(Buttons()[Finger->Button], Viewport)) > WheelSlide * S) {
			OpenWheel();
		}
		if (bWheelOpen) {
			WheelPick = PlaceUnder(At, Viewport);
		}
	}
	else if (Finger->Use == EUse::Stick) {
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
	// a finger held on the wheel opens it
	if (!bWheelOpen && FPlatformTime::Seconds() - WheelDownAt >= WheelHoldSeconds
		&& Fingers.ContainsByPredicate([](const FFinger& Each) { return Each.Use == EUse::Wheel; })) {
		OpenWheel();
	}
	if (Hero == nullptr && bWheelOpen) {
		// the hero is gone: the wheel shuts
		bWheelOpen = false;
		WheelPick = INDEX_NONE;
	}
	if (bWalkAgain) {
		bWalkAgain = false;
		if (Hero != nullptr && bStickHeld) {
			Hero->StartWalking();
		}
	}
	if (bStickHeld && Planning()) {
		// the planning view opened under the thumb: the hero stands while the base is planned
		EndStick();
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
		Touch(Fingers.Last().Id, ETouchType::Ended, Fingers.Last().Last + DrawnOrigin);
	}
}

bool USenniteTouchControls::Where(const FString& Name, const FVector2D& Viewport, FVector2D& OutCentre) const
{
	const FVector2D Size = DrawnSize.IsZero() ? Viewport : DrawnSize;
	if (Name == TEXT("STICK")) {
		OutCentre = StickRest(Size) + DrawnOrigin;
		return true;
	}
	for (const FButton& Button : Buttons()) {
		if (Name == Button.Name) {
			OutCentre = Centre(Button, Size) + DrawnOrigin;
			return true;
		}
	}
	for (const FButton& Button : EditorButtons()) {
		if (Name == Button.Name) {
			OutCentre = Centre(Button, Size) + DrawnOrigin;
			return true;
		}
	}
	for (const int32 Index : Shown()) {
		if (Name == WheelPlaces()[Index].Name) {
			OutCentre = PlaceCentre(Index, Size) + DrawnOrigin;
			return true;
		}
	}
	return false;
}

bool USenniteTouchControls::WhereKey(const FKey& InKey, FVector2D& OutCentre) const
{
	if (Controller == nullptr) {
		return false;
	}
	const FVector2D Viewport = ViewportSize();
	const bool bPlanning = Planning();
	for (const FButton& Button : bPlanning ? EditorButtons() : Buttons()) {
		// every button of the planning view is on the screen, a dimmed one too
		if (Button.Key == InKey && (bPlanning || Shows(Button))) {
			OutCentre = Centre(Button, Viewport) + DrawnOrigin;
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
	DrawnOrigin = SenniteUi::CanvasOrigin(Canvas);
	DrawnSize = FVector2D(Canvas->ClipX, Canvas->ClipY);
	if (Planning()) {
		DrawEditor(Canvas);
		return;
	}
	const FVector2D Viewport(Canvas->ClipX, Canvas->ClipY);
	const float S = Canvas->ClipY / 720.0f;
	UFont* Font = GEngine->GetLargeFont();
	const ASenniteCharacter* Hero = Cast<ASenniteCharacter>(Controller->GetPawn());
	const ASennitePlayerState* State = Controller->GetPlayerState<ASennitePlayerState>();
	if (Font == nullptr) {
		return;
	}
	// a player who is watching has no hero to move: only MENU is theirs to tap
	const bool bPlaying = Hero != nullptr && State != nullptr;
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
	// a button's word, set crisp and centred on it, as the menus' text is (SenniteUi)
	auto Text = [Canvas, S](const FVector2D& At, const FString& Words, int32 Points, const FLinearColor& Colour) {
		SenniteUi::Text(Canvas, Words, At.X, At.Y - SenniteUi::Measure(Words, Points, S).Y * 0.5f, Points, S, Colour, 0.5f);
	};

	// the stick: where it rests until a thumb comes down on the left, then under the thumb
	if (bPlaying) {
		const FVector2D Base = bStickHeld ? StickOrigin : StickRest(Viewport);
		Disc(Base, StickReach * S, Rest);
		Ring(Base, StickReach * S, Edge);
		Disc(Base + Stick * StickReach * S, 34.0f * S, bStickHeld ? Held : FLinearColor(1.0f, 1.0f, 1.0f, 0.2f));
		LastDrawn.Add(FString::Printf(TEXT("STICK=%.2f,%.2f"), Stick.X, Stick.Y));
	}

	const TArray<FButton>& All = Buttons();
	for (int32 Index = 0; Index < All.Num(); ++Index) {
		const FButton& Button = All[Index];
		if (!Shows(Button) || (!bPlaying && Button.Key != EKeys::Escape)) {
			continue;
		}
		bool bDown = Fingers.ContainsByPredicate([Index](const FFinger& Each) { return Each.Use == EUse::Button && Each.Button == Index; });
		// what is in hand, or the piece being placed, is marked; what the player does not have is dimmed
		bool bInHand = false;
		bool bHave = true;
		if (Button.bWheel) {
			bDown = bDown || Fingers.ContainsByPredicate([](const FFinger& Each) { return Each.Use == EUse::Wheel; });
		}
		else if (Button.Key == EKeys::G) {
			bInHand = Hero->GetEditingPiece() != nullptr;
		}
		else if (Button.Key == EKeys::R) {
			// RELOAD stays where it is with no gun in hand, dimmed: there is nothing to load
			bHave = Loads();
		}
		else if (AimLatches(Button)) {
			// AIM stays lit while the hero is aiming: a tap aims, the next tap lets go
			bInHand = Hero->AimedIn;
		}
		else if (Button.Key == EKeys::X || Button.Key == EKeys::C || Button.Key == EKeys::F) {
			bInHand = State->InBuildMode && State->BuildMode.Equals(Button.Name, ESearchCase::IgnoreCase);
		}
		const FLinearColor Fill = bDown ? Held : (bInHand ? Chosen : Rest);
		const FVector2D At = Centre(Button, Viewport);
		const FString Label = LabelOf(Button);
		if (Button.bWheel) {
			// the wheel's button names what is in hand, its edge gold, with a small mark that it opens
			Disc(At, Button.Radius * S, Fill);
			Ring(At, Button.Radius * S, SenniteUi::Gold);
			Text(At, Label, 11, FLinearColor::White);
			Text(At + FVector2D(0.0, 24.0 * S), TEXT("..."), 9, Dim);
		}
		else {
			Disc(At, Button.Radius * S, Fill);
			Ring(At, Button.Radius * S, bHave ? Edge : Dim);
			Text(At, Label, Button.Radius >= 40.0f ? 13 : 9, bHave ? FLinearColor::White : Dim);
		}
		LastDrawn.Add(FString::Printf(TEXT("%s=%s"), Button.Name, bHave ? *Label : TEXT("dimmed")));
	}

	// the open wheel: what the player has, round its middle; what is in hand edged in gold, the place under the finger lit
	if (bWheelOpen && bPlaying) {
		const FVector2D Middle = WheelMiddle(Viewport);
		Disc(Middle, (WheelReach + PlaceRadius + 6.0f) * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
		Ring(Middle, WheelMiddleRadius * S, Dim);
		for (const int32 Index : Shown()) {
			const FWheelPlace& Place = WheelPlaces()[Index];
			const bool bHeld = InHand(Place);
			const FVector2D At = PlaceCentre(Index, Viewport);
			Disc(At, PlaceRadius * S, Index == WheelPick ? Held : bHeld ? Chosen : Rest);
			Ring(At, PlaceRadius * S, bHeld ? SenniteUi::Gold : Edge);
			Text(At, Place.Name, 9, FLinearColor::White);
			LastDrawn.Add(FString::Printf(TEXT("WHEEL:%s=%s"), Place.Name, Place.Name));
		}
	}
}

void USenniteTouchControls::DrawEditor(UCanvas* Canvas)
{
	const FVector2D Viewport(Canvas->ClipX, Canvas->ClipY);
	const float S = Canvas->ClipY / 720.0f;
	const TArray<FButton>& Cells = EditorButtons();
	for (int32 Index = 0; Index < Cells.Num(); ++Index) {
		const FButton& Button = Cells[Index];
		const bool bLive = EditorLive(Button);
		const bool bDown = Fingers.ContainsByPredicate([Index](const FFinger& Each) { return Each.Use == EUse::EditorButton && Each.Button == Index; });
		const FVector2D At = Centre(Button, Viewport);
		const FVector2D Half(CellHalfWidth * S, CellHalfHeight * S);
		// a cell is a menu button: pressed while a finger is on it, faint while it does nothing
		const FLinearColor Ink = SenniteUi::ButtonPlate(Canvas, FBox2D(At - Half, At + Half), S,
			!bLive ? SenniteUi::EButton::Disabled : bDown ? SenniteUi::EButton::Pressed : SenniteUi::EButton::Plain);
		const FString Label = EditorLabel(Button);
		SenniteUi::Text(Canvas, Label, At.X, At.Y - SenniteUi::Measure(Label, 9, S).Y * 0.5f, 9, S, Ink, 0.5f);
		LastDrawn.Add(FString::Printf(TEXT("%s=%s%s"), Button.Name, *Label, bLive ? TEXT("") : TEXT(" (off)")));
	}
}
