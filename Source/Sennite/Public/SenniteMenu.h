// The game's first screen, its character select and its lobby, on the Menu map (Tools/build_menu.py).
//
// MENU: PLAY opens the test range on this machine; STORE opens the store; BASE EDITOR opens the test
// range on the planning view of the player's base (where there is a publisher's server, and so bases);
// HOST opens a lobby others on the network can join; JOIN takes an address, typed on the screen's own
// keypad or the keyboard; QUIT.
//
// STORE: what the publisher's shop sells, a section each, named along the top: HEROES, SKINS, D-BUCKS.
// Top right, beside a wallet, is how many D-bucks the player's wallet proved it holds (on a game with a shop).
//
// HEROES: the hero turns on the stage, its name and what is done with it to the left, every hero on a
// tile along the bottom. A tile shows that hero and the arrows go through the list; CHOOSE makes the hero the
// player's, kept on this machine between runs (USenniteProfile) and sent to every server they join.
// A hero that is not free is bought with D-bucks in the publisher's shop and can be chosen once the
// player's wallet proves it owns it. The one button under its name is the next thing to do toward that:
// CONNECT YOUR WALLET (on a phone the DSM wallet opens on the connect code), GET D-BUCKS (a trade of ERA
// from the wallet) while the wallet holds too few, then BUY.
//
// SKINS: each skin the shop sells, on the hero it is a skin of, turning on the stage. One not owned is
// bought as a hero is; one the wallet proves it owns is worn or taken off (it is on the hero in play only
// while the wallet proves it owns it).
//
// D-BUCKS: what the wallet proved it holds, and a trade of ERA from the wallet for more.
//
// LOBBY: the Menu map hosted with ?listen?game=lobby. Every player's hero stands on the stage, and
// the list says who is in and as which hero. Anyone can change hero (HEROES, the store's section); the host starts, and
// everyone travels to the test range together.
//
// Everything is drawn on the canvas, sized to the screen as the HUD is, and every button works by a
// tap or a click; the arrow keys, Enter and Escape work too.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "SenniteMenu.generated.h"

class ACameraActor;
class ASkeletalMeshActor;
class ASennitePlayerState;
class USennitePublisher;

UENUM()
enum class ESenniteMenuPage : uint8
{
	Main,
	/** The store's sections. */
	Heroes,
	Skins,
	DBucks,
	Join,
	Lobby,
};

/** A button as the last frame drew it: what it does, where it is on the screen (in the screen's own measure, as taps and clicks come), and whether it does anything now. */
struct FSenniteMenuButton
{
	FName Action;
	FString Text;
	FBox2D Box;
	bool bEnabled = true;
};

/** The lobby's game state: that this world is a lobby, which every player's game can see. */
UCLASS()
class SENNITE_API ASenniteLobbyState : public AGameStateBase
{
	GENERATED_BODY()
};

/** A player at the menu or in the lobby: what they press, which page they are on, and the stage they look at. */
UCLASS()
class SENNITE_API ASenniteMenuController : public APlayerController
{
	GENERATED_BODY()

public:
	ASenniteMenuController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual bool InputTouch(const FTouchId TouchId, const ETouchType::Type Type, const FVector2D& TouchLocation, const float Force, const uint64 Timestamp) override;

	/** Whether this world is a lobby. */
	bool InLobby() const;

	ESenniteMenuPage Page = ESenniteMenuPage::Main;

	/** The hero the character select shows, by its place in the list. */
	int32 Browse = 0;

	/** The skin the store's skins section shows, by its place in the shop's list. */
	int32 BrowseSkin = 0;

	/** Whether a page is one of the store's sections. */
	static bool InStore(ESenniteMenuPage Which) { return Which == ESenniteMenuPage::Heroes || Which == ESenniteMenuPage::Skins || Which == ESenniteMenuPage::DBucks; }

	/** The address typed on the join page. */
	FString Address;

	/** What the menu says about the last thing asked (a hero that cannot be chosen, an address that is not one). */
	FString Note;

	/** The note says what to do in the wallet to connect it: it goes once the wallet is connected. */
	bool bNoteForWallet = false;

	/** Do what a button does. Public so a check can name what it tapped; the input goes through the same buttons. */
	void Act(FName Action);

	/** The players in the lobby, in the order they joined, and the hero each one is. */
	TArray<const ASennitePlayerState*> LobbyPlayers() const;

	/** The heroes standing on the stage now, by hero id (a shop skin as "skin:" and its id): what a check reads to see the stage is what the page says. */
	TArray<FName> StageHeroes() const;

	/** How far the turntable has turned, in degrees. */
	float Spin = 0.0f;

	/** The local addresses others on this network can join this machine at (host only). */
	TArray<FString> LocalAddresses() const;

private:
	/** The button a press began on, by tap or click; it acts when the press ends on the same button. */
	FName Pressed;

public:
	/** The button held down now, if any: the menu draws it pressed. */
	FName HeldButton() const { return Pressed; }

private:

	/** A press or a tap at a point of the screen. */
	void Press(const FVector2D& Where);
	void Release(const FVector2D& Where);

	/** Set up the stage: the heroes to stand on it (one turning, or the lobby's row) and the camera framing them. */
	void UpdateStage(float DeltaTime);
	void StandHeroes(const TArray<FName>& Heroes);

	UPROPERTY()
	TObjectPtr<ACameraActor> StageCamera;

	UPROPERTY()
	TArray<TObjectPtr<ASkeletalMeshActor>> Figures;

	TArray<FName> FigureHeroes;
};

/** The menu's pages, drawn on the canvas. */
UCLASS()
class SENNITE_API ASenniteMenuHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** The buttons the last frame drew. */
	TArray<FSenniteMenuButton> Buttons;

	/** Each line of text the last frame drew, as "name=text": what a check reads. */
	TArray<FString> LastDrawn;

	/** The enabled button at a point of the screen (the screen's own measure, as a touch or the mouse gives it), or none. */
	FName ButtonAt(const FVector2D& Where) const;

	/** Where on the screen a button was drawn: its middle, where a finger taps it. False if the last frame drew no such button. */
	bool Centre(FName Action, FVector2D& Out) const;

private:
	void DrawMain(ASenniteMenuController* Menu);
	void DrawHeroes(ASenniteMenuController* Menu);
	void DrawSkins(ASenniteMenuController* Menu);
	void DrawDBucks(ASenniteMenuController* Menu);

	/** The top of every section of the store: BACK, the sections' names, and the wallet's D-bucks top right. */
	void StoreTop(ASenniteMenuController* Menu);

	/** One thing the shop sells, as the button toward having it needs to know it. */
	struct FForSale
	{
		/** What such things are called, as "HEROES". */
		const TCHAR* Kind = TEXT("");
		/** What the button that buys it does. */
		FName Buy;
		/** Whether the shop lists it, and its price in D-bucks. */
		bool bListed = false;
		int32 Price = 0;
		/** Whether it is sold and given, and the wallet has not proved it owns it yet. */
		bool bOnItsWay = false;
	};

	/**
	 * The one button under something not had yet, the next thing to do toward having it (connect the wallet,
	 * get D-bucks, buy), with what that is said under it. True if it said something under the button.
	 */
	bool NextStep(const USennitePublisher* Server, const FForSale& Thing, float Left, float Y, float Wide);

	/** What the shop says of a request the wallet has not settled, or of something bought that is on its way. */
	void ShopWord(const USennitePublisher* Server, bool bOnItsWay, float X, float Y);
	void DrawJoin(ASenniteMenuController* Menu);
	void DrawLobby(ASenniteMenuController* Menu);

	/**
	 * A button, on a 720-line screen's measure: a plate with its text in the middle. bPrimary is the one
	 * thing the page is for. Arrow is -1 or 1 for an arrow pointing left or right: before the word, or,
	 * for a button whose text is one mark, in its place.
	 */
	void Button(FName Action, const FString& Text, float X, float Y, float Width, float Height, bool bEnabled = true, float Points = 18.0f, bool bPrimary = false, int32 Arrow = 0);

	/** A line of text at a point size for a 720-line screen. Align is where X is in it: 0 left, 0.5 middle, 1 right. Recorded under its name. */
	void Text(const TCHAR* Name, const FString& Line, float X, float Y, float Points, const FLinearColor& Colour, float Align = 0.0f, bool bBold = true);

	/** A page's name at its top left: First in white, Second in gold, and a line under them. */
	void Heading(const FString& First, const FString& Second, const FString& Under);

	/** The button under the mouse, and the one held down, this frame. */
	FName Hovered;
	FName Held;

	/** The whole screen, in the canvas's own measure: on a phone the canvas starts inside the screen's safe zone. */
	FBox2D Screen() const;

	/**
	 * How far the canvas starts in from the screen's top left corner: nothing on a desktop, a phone's safe
	 * zone on a phone. A tap or a click comes in the screen's measure, so each button is kept where it is
	 * on the screen: where it was drawn, moved by this.
	 */
	FVector2D Inset = FVector2D::ZeroVector;

	/** How wide a line of text is drawn at a point size, on the screen. */
	float Width(const FString& Line, float Points) const;

	float S = 1.0f;
	TArray<FString> Drawing;
};
