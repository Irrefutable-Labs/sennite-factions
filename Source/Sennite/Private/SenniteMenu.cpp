#include "SenniteMenu.h"
#include "Animation/AnimInstance.h"
#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "IPAddress.h"
#include "Kismet/GameplayStatics.h"
#include "LobbyGameMode.h"
#include "SenniteGameInstance.h"
#include "SenniteHeroes.h"
#include "SennitePlayerState.h"
#include "SennitePublisher.h"
#include "SenniteUi.h"
#include "SocketSubsystem.h"

namespace
{
	const TCHAR* MenuMap = TEXT("/Game/Maps/Menu");
	const TCHAR* PlayMap = TEXT("/Game/Maps/TestRange");
	/** How far apart the lobby's heroes stand, in cm. */
	const float Spacing = 190.0f;
	/** The camera's field of view, across. */
	const float FieldOfView = 60.0f;

	int32 IndexOf(FName Id)
	{
		return FMath::Max(0, USenniteHeroes::All().IndexOfByPredicate([Id](const FSenniteHero& Hero) { return Hero.Id == Id; }));
	}

	const USennitePublisher* Shop(const UObject* Context)
	{
		const UGameInstance* Game = Context->GetWorld() != nullptr ? Context->GetWorld()->GetGameInstance() : nullptr;
		return Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
	}
}

ASenniteMenuController::ASenniteMenuController()
{
	bShowMouseCursor = true;
	// the menu reads taps and clicks itself, as presses on its own buttons; nothing in the world is clicked
	bEnableClickEvents = false;
	bEnableTouchEvents = false;
	// the camera is the stage's, not a pawn's: the menu has no pawn
	bAutoManageActiveCameraTarget = false;
}

void ASenniteMenuController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) {
		return;
	}
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
	// the menu's buttons are the touch interface: no joysticks over it
	ActivateTouchInterface(nullptr);
	Browse = IndexOf(USenniteProfile::Get().PlayingHero(this));
	Page = InLobby() ? ESenniteMenuPage::Lobby : ESenniteMenuPage::Main;
	// back at the menu after a join that failed or a server that went away: say why
	if (USenniteGameInstance* Game = GetGameInstance<USenniteGameInstance>()) {
		Note = Game->TakeFailure();
	}
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	StageCamera = GetWorld()->SpawnActor<ACameraActor>(FVector(-400.0, 0.0, 100.0), FRotator::ZeroRotator, Spawn);
	if (StageCamera != nullptr) {
		StageCamera->SetReplicates(false);
		StageCamera->GetCameraComponent()->SetFieldOfView(FieldOfView);
		StageCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		SetViewTarget(StageCamera);
	}
}

bool ASenniteMenuController::InLobby() const
{
	return GetWorld() != nullptr && GetWorld()->GetGameState<ASenniteLobbyState>() != nullptr;
}

TArray<const ASennitePlayerState*> ASenniteMenuController::LobbyPlayers() const
{
	TArray<const ASennitePlayerState*> Players;
	if (const AGameStateBase* Game = GetWorld() != nullptr ? GetWorld()->GetGameState() : nullptr) {
		for (const APlayerState* Each : Game->PlayerArray) {
			if (const ASennitePlayerState* Player = Cast<ASennitePlayerState>(Each)) {
				Players.Add(Player);
			}
		}
	}
	return Players;
}

TArray<FName> ASenniteMenuController::StageHeroes() const
{
	return FigureHeroes;
}

TArray<FString> ASenniteMenuController::LocalAddresses() const
{
	TArray<FString> Found;
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TArray<TSharedPtr<FInternetAddr>> Adapters;
	if (Sockets == nullptr || !Sockets->GetLocalAdapterAddresses(Adapters)) {
		return Found;
	}
	const int32 Port = GetWorld()->URL.Port;
	for (const TSharedPtr<FInternetAddr>& Adapter : Adapters) {
		const FString Ip = Adapter->ToString(false);
		// IPv4 only, and not this machine's own loopback: what another device on the network can reach
		if (Adapter->GetProtocolType() == FNetworkProtocolTypes::IPv4 && !Ip.StartsWith(TEXT("127.")) && !Ip.StartsWith(TEXT("169.254."))) {
			Found.AddUnique(Port == FURL::UrlConfig.DefaultPort ? Ip : FString::Printf(TEXT("%s:%d"), *Ip, Port));
		}
	}
	return Found;
}

void ASenniteMenuController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController()) {
		return;
	}
	// a client learns it is in a lobby when the lobby's game state reaches it
	if (Page == ESenniteMenuPage::Main && InLobby()) {
		Page = ESenniteMenuPage::Lobby;
	}
	UpdateStage(DeltaTime);
}

void ASenniteMenuController::UpdateStage(float DeltaTime)
{
	TArray<FName> Want;
	if (Page == ESenniteMenuPage::Lobby) {
		for (const ASennitePlayerState* Player : LobbyPlayers()) {
			Want.Add(USenniteHeroes::FindOrDefault(Player->HeroId).Id);
		}
	}
	else if (Page == ESenniteMenuPage::Heroes && USenniteHeroes::All().IsValidIndex(Browse)) {
		Want.Add(USenniteHeroes::All()[Browse].Id);
	}
	else {
		Want.Add(USenniteProfile::Get().PlayingHero(this));
	}
	if (Want != FigureHeroes) {
		StandHeroes(Want);
	}

	// one hero turns on the turntable; the lobby's row faces the camera
	Spin = FMath::Fmod(Spin + 30.0f * DeltaTime, 360.0f);
	const bool bTurn = Figures.Num() == 1;
	float Top = 150.0f;
	for (int32 Index = 0; Index < Figures.Num(); ++Index) {
		ASkeletalMeshActor* Figure = Figures[Index];
		if (Figure == nullptr) {
			continue;
		}
		// the heroes are authored facing +Y; turned a quarter, they face the camera, which looks along +X
		Figure->SetActorRotation(FRotator(0.0, 90.0 + (bTurn ? Spin : 0.0), 0.0));
		if (const USkeletalMesh* Mesh = Figure->GetSkeletalMeshComponent()->GetSkeletalMeshAsset()) {
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			Top = FMath::Max(Top, float(Bounds.Origin.Z + Bounds.BoxExtent.Z));
		}
	}
	if (StageCamera == nullptr) {
		return;
	}
	// frame them all, whole, with the menu's buttons to the left on every page but the character select
	const float Aspect = GEngine != nullptr && GEngine->GameViewport != nullptr && GEngine->GameViewport->Viewport != nullptr && GEngine->GameViewport->Viewport->GetSizeXY().Y > 0
		? float(GEngine->GameViewport->Viewport->GetSizeXY().X) / GEngine->GameViewport->Viewport->GetSizeXY().Y : 16.0f / 9.0f;
	const float HalfAcross = FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5f));
	const float HalfUp = HalfAcross / Aspect;
	const float Row = (Figures.Num() - 1) * Spacing + 160.0f;
	// on the character select the hero's name and buttons lie along the bottom: the hero stands back and
	// above them, whole; elsewhere the buttons are to the left and the hero to the right, and a lobby's row
	// is seen from far enough that the heroes at its ends are whole too
	const bool bSelect = Page == ESenniteMenuPage::Heroes;
	const float Distance = FMath::Max(Top * (bSelect ? 1.78f : 1.2f) / (2.0f * HalfUp), Row * (Figures.Num() > 1 ? 2.1f : 1.25f) / (2.0f * HalfAcross));
	const float Across = 2.0f * Distance * HalfAcross;
	const float Aside = bSelect ? 0.0f : -0.17f * Across;
	const FVector Goal(-Distance, Aside, Top * (bSelect ? 0.30f : 0.52f));
	const FVector Now = FMath::VInterpTo(StageCamera->GetActorLocation(), Goal, DeltaTime, 4.0f);
	StageCamera->SetActorLocationAndRotation(Now, FRotator(bSelect ? 0.0 : -2.0, 0.0, 0.0));
}

void ASenniteMenuController::StandHeroes(const TArray<FName>& Heroes)
{
	for (ASkeletalMeshActor* Figure : Figures) {
		if (Figure != nullptr) {
			Figure->Destroy();
		}
	}
	Figures.Reset();
	FigureHeroes = Heroes;
	USenniteGameInstance* Game = GetGameInstance<USenniteGameInstance>();
	for (int32 Index = 0; Index < Heroes.Num(); ++Index) {
		const FSenniteHero& Hero = USenniteHeroes::FindOrDefault(Heroes[Index]);
		const FVector Where(0.0, (Index - (Heroes.Num() - 1) * 0.5f) * Spacing, 0.0);
		FActorSpawnParameters Spawn;
		Spawn.ObjectFlags |= RF_Transient;
		Spawn.bDeferConstruction = true;
		ASkeletalMeshActor* Figure = GetWorld()->SpawnActor<ASkeletalMeshActor>(Where, FRotator(0.0, 90.0, 0.0), Spawn);
		if (Figure == nullptr) {
			continue;
		}
		// the stage is this player's own: nothing on it goes to anyone else's game
		Figure->SetReplicates(false);
		Figure->FinishSpawning(FTransform(FRotator(0.0, 90.0, 0.0), Where));
		USkeletalMeshComponent* Body = Figure->GetSkeletalMeshComponent();
		Body->SetMobility(EComponentMobility::Movable);
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Hero.Mesh.TryLoad())) {
			Body->SetSkeletalMeshAsset(Mesh);
		}
		if (UClass* Anim = Hero.AnimClass.TryLoadClass<UAnimInstance>()) {
			Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			Body->SetAnimInstanceClass(Anim);
		}
		if (Game != nullptr) {
			Game->KeepHero(Hero.Id);
		}
		Figures.Add(Figure);
	}
}

bool ASenniteMenuController::InputTouch(const FTouchId TouchId, const ETouchType::Type Type, const FVector2D& TouchLocation, const float Force, const uint64 Timestamp)
{
	if (Type == ETouchType::Began) {
		Press(TouchLocation);
		return true;
	}
	if (Type == ETouchType::Ended) {
		Release(TouchLocation);
		return true;
	}
	return Super::InputTouch(TouchId, Type, TouchLocation, Force, Timestamp);
}

bool ASenniteMenuController::InputKey(const FInputKeyEventArgs& Params)
{
	const FKey Key = Params.Key;
	if (Key == EKeys::LeftMouseButton && (Params.Event == IE_Pressed || Params.Event == IE_Released)) {
		float X = 0.0f;
		float Y = 0.0f;
		if (GetMousePosition(X, Y)) {
			Params.Event == IE_Pressed ? Press(FVector2D(X, Y)) : Release(FVector2D(X, Y));
		}
		return true;
	}
	if (Params.Event != IE_Pressed) {
		return Super::InputKey(Params);
	}
	if (Key == EKeys::Escape || Key == EKeys::Android_Back) {
		Act(TEXT("back"));
		return true;
	}
	if (Page == ESenniteMenuPage::Heroes) {
		if (Key == EKeys::Left || Key == EKeys::A) {
			Act(TEXT("prev"));
			return true;
		}
		if (Key == EKeys::Right || Key == EKeys::D) {
			Act(TEXT("next"));
			return true;
		}
		if (Key == EKeys::Enter) {
			Act(TEXT("choose"));
			return true;
		}
	}
	if (Page == ESenniteMenuPage::Join) {
		if (Key == EKeys::Enter) {
			Act(TEXT("connect"));
			return true;
		}
		if (Key == EKeys::BackSpace) {
			Act(TEXT("key:del"));
			return true;
		}
		// what the key types: a digit, a letter, or the marks an address has
		const FString Name = Key.GetFName().ToString();
		FString Typed;
		if (Key == EKeys::Period || Key == EKeys::Decimal) {
			Typed = TEXT(".");
		}
		else if (Key == EKeys::Semicolon) {
			Typed = TEXT(":");
		}
		else if (Key == EKeys::Hyphen || Key == EKeys::Subtract) {
			Typed = TEXT("-");
		}
		else if (Name.Len() == 1 && FChar::IsAlpha(Name[0])) {
			Typed = Name.ToLower();
		}
		else {
			static const FKey Digits[] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
			static const FKey Pad[] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
			for (int32 Digit = 0; Digit < 10; ++Digit) {
				if (Key == Digits[Digit] || Key == Pad[Digit]) {
					Typed = FString::FromInt(Digit);
				}
			}
		}
		if (!Typed.IsEmpty()) {
			Act(FName(*(TEXT("key:") + Typed)));
			return true;
		}
	}
	return Super::InputKey(Params);
}

void ASenniteMenuController::Press(const FVector2D& Where)
{
	const ASenniteMenuHUD* Hud = Cast<ASenniteMenuHUD>(GetHUD());
	Pressed = Hud != nullptr ? Hud->ButtonAt(Where) : NAME_None;
}

void ASenniteMenuController::Release(const FVector2D& Where)
{
	const ASenniteMenuHUD* Hud = Cast<ASenniteMenuHUD>(GetHUD());
	const FName Action = Hud != nullptr ? Hud->ButtonAt(Where) : NAME_None;
	// a press acts when it ends on the button it began on: a finger that slides off changes its mind
	if (!Action.IsNone() && Action == Pressed) {
		Act(Action);
	}
	Pressed = NAME_None;
}

void ASenniteMenuController::Act(FName Action)
{
	const FString Name = Action.ToString();
	const TArray<FSenniteHero>& Heroes = USenniteHeroes::All();
	USenniteProfile& Profile = USenniteProfile::Get();
	if (Name == TEXT("play")) {
		UGameplayStatics::OpenLevel(this, FName(PlayMap));
	}
	else if (Name == TEXT("heroes")) {
		Page = ESenniteMenuPage::Heroes;
		Browse = IndexOf(Profile.PlayingHero(this));
		Note.Reset();
	}
	else if (Name == TEXT("host")) {
		UGameplayStatics::OpenLevel(this, FName(MenuMap), true, TEXT("listen?game=lobby"));
	}
	else if (Name == TEXT("join")) {
		Page = ESenniteMenuPage::Join;
		Address = Profile.LastAddress;
		Note.Reset();
	}
	else if (Name == TEXT("quit")) {
		ConsoleCommand(TEXT("quit"));
	}
	else if (Name == TEXT("prev") || Name == TEXT("next")) {
		Browse = (Browse + (Name == TEXT("next") ? 1 : -1) + Heroes.Num()) % FMath::Max(1, Heroes.Num());
		Note.Reset();
	}
	else if (Name == TEXT("choose") && Heroes.IsValidIndex(Browse)) {
		const FSenniteHero& Hero = Heroes[Browse];
		if (!USenniteHeroes::MayPlay(this, Hero.Id)) {
			Note = TEXT("THIS HERO IS BOUGHT WITH D-BUCKS FIRST");
			return;
		}
		Profile.Hero = Hero.Id;
		Profile.Save();
		if (ASennitePlayerState* State = GetPlayerState<ASennitePlayerState>()) {
			// in the lobby: the server hears it, and everyone sees the new hero on the stage
			const UGameInstance* Game = GetGameInstance();
			USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
			if (Hero.bFree || Server == nullptr) {
				State->ServerChooseHero(Hero.Id, FString());
			}
			else {
				// a hero bought in the shop: with a hero pass made out to this lobby's server
				TWeakObjectPtr<ASennitePlayerState> Chooser(State);
				const FName Id = Hero.Id;
				Server->AskHeroPass(State->HeroAudience, [Chooser, Id](const FString& Pass) {
					if (ASennitePlayerState* Still = Chooser.Get()) {
						Still->ServerChooseHero(Id, Pass);
					}
				});
			}
		}
		Note.Reset();
		Page = InLobby() ? ESenniteMenuPage::Lobby : ESenniteMenuPage::Main;
	}
	else if (Name == TEXT("buy") && Heroes.IsValidIndex(Browse)) {
		const UGameInstance* Game = GetGameInstance();
		USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
		if (Server != nullptr && Server->IsOn() && Server->bConnected && !Server->bBusy) {
			Server->BuyHero(Heroes[Browse].Id.ToString());
		}
	}
	else if (Name == TEXT("back")) {
		if (Page == ESenniteMenuPage::Heroes) {
			Page = InLobby() ? ESenniteMenuPage::Lobby : ESenniteMenuPage::Main;
		}
		else if (Page == ESenniteMenuPage::Join) {
			Page = ESenniteMenuPage::Main;
		}
		Note.Reset();
	}
	else if (Name.StartsWith(TEXT("key:"))) {
		const FString Typed = Name.Mid(4);
		if (Typed == TEXT("del")) {
			Address.LeftChopInline(1);
		}
		else if (Address.Len() < 64) {
			Address += Typed;
		}
	}
	else if (Name == TEXT("connect")) {
		const FString To = Address.TrimStartAndEnd();
		if (To.IsEmpty()) {
			Note = TEXT("TYPE THE HOST'S ADDRESS");
			return;
		}
		Profile.LastAddress = To;
		Profile.Save();
		Note = FString::Printf(TEXT("JOINING %s ..."), *To);
		ClientTravel(To, TRAVEL_Absolute);
	}
	else if (Name == TEXT("start")) {
		if (ALobbyGameMode* Lobby = GetWorld()->GetAuthGameMode<ALobbyGameMode>()) {
			Lobby->StartMatch();
		}
	}
	else if (Name == TEXT("leave")) {
		if (GetNetMode() == NM_Client) {
			ClientTravel(MenuMap, TRAVEL_Absolute);
		}
		else {
			UGameplayStatics::OpenLevel(this, FName(MenuMap));
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Drawing

FName ASenniteMenuHUD::ButtonAt(const FVector2D& Where) const
{
	// Where is on the screen; the buttons are where the canvas drew them
	for (const FSenniteMenuButton& Each : Buttons) {
		if (Each.bEnabled && Each.Box.IsInside(Where - Origin)) {
			return Each.Action;
		}
	}
	return NAME_None;
}

bool ASenniteMenuHUD::Centre(FName Action, FVector2D& Out) const
{
	for (const FSenniteMenuButton& Each : Buttons) {
		if (Each.Action == Action) {
			Out = Each.Box.GetCenter() + Origin;
			return true;
		}
	}
	return false;
}

void ASenniteMenuHUD::Text(const TCHAR* Name, const FString& Line, float X, float Y, float Points, const FLinearColor& Colour, float Align, bool bBold)
{
	SenniteUi::Text(Canvas, Line, X, Y, Points, S, Colour, Align, bBold);
	Drawing.Add(FString::Printf(TEXT("%s=%s"), Name, *Line));
}

float ASenniteMenuHUD::Width(const FString& Line, float Points) const
{
	return SenniteUi::Measure(Line, Points, S).X;
}

void ASenniteMenuHUD::Button(FName Action, const FString& Label, float X, float Y, float Width, float Height, bool bEnabled, float Points, bool bPrimary, int32 Arrow)
{
	const FBox2D Box(FVector2D(X * S, Y * S), FVector2D((X + Width) * S, (Y + Height) * S));
	// what the button looks like says what it will do: nothing, the page's main thing, or what the finger or mouse is on
	const SenniteUi::EButton State = !bEnabled ? SenniteUi::EButton::Disabled
		: Action == Held ? SenniteUi::EButton::Pressed
		: Action == Hovered ? SenniteUi::EButton::Hover
		: bPrimary ? SenniteUi::EButton::Primary : SenniteUi::EButton::Plain;
	const FLinearColor Ink = SenniteUi::ButtonPlate(Canvas, Box, S, State);
	const FVector2D Middle = Box.GetCenter();
	const float High = SenniteUi::Measure(Label, Points, S).Y;
	if (Arrow != 0 && Label.Len() <= 1) {
		// an arrow button: the arrow is the button
		SenniteUi::Chevron(Canvas, Middle, FMath::Min(Width, Height) * 0.42f * S, Arrow < 0, Ink, 6.0f * S);
		Drawing.Add(FString::Printf(TEXT("button:%s=%s"), *Action.ToString(), *Label));
	}
	else if (Arrow != 0) {
		// an arrow before the word, as on BACK
		const float Gap = 16.0f * S;
		const float Wide = SenniteUi::Measure(Label, Points, S).X;
		const float ArrowSize = High * 0.62f;
		const float Left = Middle.X - (Wide + Gap + ArrowSize * 0.5f) * 0.5f;
		SenniteUi::Chevron(Canvas, FVector2D(Left + ArrowSize * 0.25f, Middle.Y), ArrowSize, Arrow < 0, Ink, 3.5f * S);
		Text(*FString::Printf(TEXT("button:%s"), *Action.ToString()), Label, Left + ArrowSize * 0.5f + Gap, Middle.Y - High * 0.5f, Points, Ink);
	}
	else {
		Text(*FString::Printf(TEXT("button:%s"), *Action.ToString()), Label, Middle.X, Middle.Y - High * 0.5f, Points, Ink, 0.5f);
	}
	FSenniteMenuButton Each;
	Each.Action = Action;
	Each.Text = Label;
	Each.Box = Box;
	Each.bEnabled = bEnabled;
	Buttons.Add(Each);
}

void ASenniteMenuHUD::DrawHUD()
{
	Super::DrawHUD();
	ASenniteMenuController* Menu = Cast<ASenniteMenuController>(PlayerOwner);
	// what the mouse is over and what is held down, by where the last frame drew its buttons
	Hovered = NAME_None;
	Held = Menu != nullptr ? Menu->HeldButton() : NAME_None;
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (Menu != nullptr && Menu->GetMousePosition(MouseX, MouseY)) {
		Hovered = ButtonAt(FVector2D(MouseX, MouseY));
	}
	Buttons.Reset();
	Drawing.Reset();
	// every size is for a screen 720 lines high and scales with the screen, phone or desktop
	S = Canvas->ClipY / 720.0f;
	Origin = SenniteUi::CanvasOrigin(Canvas);
	if (Menu == nullptr) {
		return;
	}
	Drawing.Add(FString::Printf(TEXT("page=%d"), int32(Menu->Page)));
	switch (Menu->Page) {
	case ESenniteMenuPage::Main: DrawMain(Menu); break;
	case ESenniteMenuPage::Heroes: DrawHeroes(Menu); break;
	case ESenniteMenuPage::Join: DrawJoin(Menu); break;
	case ESenniteMenuPage::Lobby: DrawLobby(Menu); break;
	}
	if (!Menu->Note.IsEmpty()) {
		const float W = Canvas->ClipX;
		const float Wide = FMath::Max(360.0f * S, Width(Menu->Note, 13) + 56.0f * S);
		SenniteUi::Panel(Canvas, FBox2D(FVector2D(W * 0.5f - Wide * 0.5f, 16.0f * S), FVector2D(W * 0.5f + Wide * 0.5f, 60.0f * S)), S);
		Text(TEXT("note"), Menu->Note, W * 0.5f, 27.0f * S, 13, SenniteUi::Gold, 0.5f);
	}
	LastDrawn = Drawing;
}

void ASenniteMenuHUD::Heading(const FString& First, const FString& Second, const FString& Under)
{
	// a page's name: white, then gold, over a short gold rule, with a line under it
	const float X = 48.0f * S;
	const float Y = 34.0f * S;
	SenniteUi::Text(Canvas, First, X, Y, 30, S, FLinearColor::White);
	const float Wide = SenniteUi::Measure(First, 30, S).X;
	if (!Second.IsEmpty()) {
		SenniteUi::Text(Canvas, Second, X + Wide + 12.0f * S, Y, 30, S, SenniteUi::Gold);
	}
	Drawing.Add(FString::Printf(TEXT("title=%s%s%s"), *First, Second.IsEmpty() ? TEXT("") : TEXT(" "), *Second));
	SenniteUi::Plate(Canvas, FBox2D(FVector2D(X, Y + 50.0f * S), FVector2D(X + 96.0f * S, Y + 53.0f * S)), 0.0f, SenniteUi::Gold, SenniteUi::Gold);
	if (!Under.IsEmpty()) {
		Text(TEXT("under"), Under, X + 2.0f * S, Y + 62.0f * S, 11, SenniteUi::Dim, 0.0f, false);
	}
}

void ASenniteMenuHUD::DrawMain(ASenniteMenuController* Menu)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	// the buttons stand on a shade that fades out toward the hero, and the hero's name on one that rises from the bottom
	// from the screen's own left edge: on a phone the canvas begins past the camera's cutout
	SenniteUi::Fade(Canvas, FBox2D(FVector2D(-Origin.X, -Origin.Y), FVector2D(560.0f * S, H + Canvas->SafeZonePadEY)), FLinearColor(0.0f, 0.0f, 0.0f, 0.62f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), false);
	SenniteUi::Fade(Canvas, FBox2D(FVector2D(0.0f, H - 180.0f * S), FVector2D(W, H)), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.5f), true);
	Heading(TEXT("SENNITE"), TEXT("FACTIONS"), TEXT("BETA"));
	const float X = 48.0f;
	float Y = 156.0f;
	const float Step = 74.0f;
	for (const TPair<const TCHAR*, const TCHAR*>& Each : { TPair<const TCHAR*, const TCHAR*>(TEXT("play"), TEXT("PLAY")), { TEXT("heroes"), TEXT("HEROES") },
			{ TEXT("host"), TEXT("HOST A LOBBY") }, { TEXT("join"), TEXT("JOIN A LOBBY") }, { TEXT("quit"), TEXT("QUIT") } }) {
		Button(Each.Key, Each.Value, X, Y, 320.0f, 62.0f, true, 18, FName(Each.Key) == TEXT("play"));
		Y += Step;
	}
	// bottom right, under the turning hero: who the player is
	const FSenniteHero& Hero = USenniteHeroes::FindOrDefault(USenniteProfile::Get().PlayingHero(Menu));
	const float Right = W - 48.0f * S;
	Text(TEXT("yourhero"), TEXT("YOUR HERO"), Right, H - 132.0f * S, 11, SenniteUi::Gold, 1.0f);
	Text(TEXT("hero"), Hero.Name, Right, H - 108.0f * S, 34, FLinearColor::White, 1.0f);
	SenniteUi::Plate(Canvas, FBox2D(FVector2D(Right - 96.0f * S, H - 46.0f * S), FVector2D(Right, H - 43.0f * S)), 0.0f, SenniteUi::Gold, SenniteUi::Gold);
}

void ASenniteMenuHUD::DrawHeroes(ASenniteMenuController* Menu)
{
	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	const TArray<FSenniteHero>& Heroes = USenniteHeroes::All();
	if (!Heroes.IsValidIndex(Menu->Browse)) {
		return;
	}
	const FSenniteHero& Hero = Heroes[Menu->Browse];
	const FLinearColor White = FLinearColor::White;
	// a shade down from the top and up from the bottom: the hero stands clear between them
	SenniteUi::Fade(Canvas, FBox2D(FVector2D(0.0f, 0.0f), FVector2D(W * S, 120.0f * S)), FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), true);
	SenniteUi::Fade(Canvas, FBox2D(FVector2D(0.0f, (H - 270.0f) * S), FVector2D(W * S, H * S)), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.62f), true);
	Button(TEXT("back"), TEXT("BACK"), 24.0f, 24.0f, 164.0f, 54.0f, true, 16, false, -1);

	// top right: which hero of how many, as a row of marks with this one's lit
	Text(TEXT("count"), FString::Printf(TEXT("%d / %d"), Menu->Browse + 1, Heroes.Num()), (W - 36.0f) * S, 28.0f * S, 13, SenniteUi::Dim, 1.0f);
	const float Pip = 18.0f;
	const float PipGap = 6.0f;
	for (int32 Index = 0; Index < Heroes.Num(); ++Index) {
		const float Left = W - 36.0f - (Heroes.Num() - Index) * (Pip + PipGap) + PipGap;
		const bool bThis = Index == Menu->Browse;
		const FLinearColor Colour = bThis ? SenniteUi::Gold : FLinearColor(1.0f, 1.0f, 1.0f, 0.28f);
		SenniteUi::Plate(Canvas, FBox2D(FVector2D(Left * S, 58.0f * S), FVector2D((Left + Pip) * S, (bThis ? 64.0f : 62.0f) * S)), 0.0f, Colour, Colour);
	}
	Button(TEXT("prev"), TEXT("<"), 24.0f, H * 0.5f - 66.0f, 88.0f, 132.0f, true, 34, false, -1);
	Button(TEXT("next"), TEXT(">"), W - 112.0f, H * 0.5f - 66.0f, 88.0f, 132.0f, true, 34, false, 1);

	Text(TEXT("name"), Hero.Name, W * 0.5f * S, (H - 226.0f) * S, 40, White, 0.5f);
	const USennitePublisher* Server = Shop(Menu);
	const bool bChosen = USenniteProfile::Get().PlayingHero(Menu) == Hero.Id;
	const bool bMay = USenniteHeroes::MayPlay(Menu, Hero.Id);
	FString Status;
	if (Hero.bFree) {
		Status = FString(TEXT("FREE"));
	}
	else if (bMay) {
		Status = FString(TEXT("YOURS"));
	}
	else if (const FSenniteHeroForSale* ForSale = Server != nullptr ? Server->HeroForSale(Hero.Id.ToString()) : nullptr) {
		Status = FString::Printf(TEXT("%d D-BUCKS"), ForSale->Price);
	}
	else {
		Status = FString(TEXT("BOUGHT WITH D-BUCKS IN THE SHOP"));
	}
	// how the hero is had, on a small plate under its name
	{
		const float Wide = Width(Status, 13) + 36.0f * S;
		const FBox2D Tag(FVector2D(W * 0.5f * S - Wide * 0.5f, (H - 160.0f) * S), FVector2D(W * 0.5f * S + Wide * 0.5f, (H - 126.0f) * S));
		SenniteUi::Plate(Canvas, Tag, 7.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		SenniteUi::Outline(Canvas, Tag, 7.0f * S, bMay ? SenniteUi::Edge : SenniteUi::Gold.CopyWithNewOpacity(0.8f), FMath::Max(1.0f, 1.5f * S));
		Text(TEXT("status"), Status, W * 0.5f * S, (H - 152.0f) * S, 13, bMay ? White : SenniteUi::Gold, 0.5f);
	}

	const float BW = 340.0f;
	const float BX = W * 0.5f - BW * 0.5f;
	const float BY = H - 108.0f;
	if (bChosen) {
		Button(TEXT("chosen"), TEXT("CHOSEN"), BX, BY, BW, 64.0f, false);
	}
	else if (bMay) {
		Button(TEXT("choose"), TEXT("CHOOSE"), BX, BY, BW, 64.0f, true, 18, true);
	}
	else if (Server == nullptr || !Server->IsOn()) {
		Button(TEXT("buy"), TEXT("NO SHOP ON THIS GAME"), BX, BY, BW, 64.0f, false, 14);
	}
	else if (!Server->bConnected) {
		Button(TEXT("buy"), TEXT("CONNECT A WALLET TO BUY"), BX, BY, BW, 64.0f, false, 14);
		if (!Server->ConnectCode.IsEmpty()) {
			Text(TEXT("connect"), FString::Printf(TEXT("WALLET CONNECT CODE  %s"), *Server->ConnectCode), W * 0.5f * S, (H - 34.0f) * S, 10, SenniteUi::Dim, 0.5f, false);
		}
	}
	else if (Server->HeroOnItsWay == Hero.Id.ToString() || Server->bBusy) {
		Button(TEXT("buy"), Server->bBusy ? TEXT("BUYING ...") : TEXT("ON ITS WAY TO YOUR WALLET"), BX, BY, BW, 64.0f, false, 14);
	}
	else {
		const FSenniteHeroForSale* ForSale = Server->HeroForSale(Hero.Id.ToString());
		Button(TEXT("buy"), ForSale != nullptr ? FString::Printf(TEXT("BUY  %d D-BUCKS"), ForSale->Price) : FString(TEXT("NOT IN THE SHOP")), BX, BY, BW, 64.0f, ForSale != nullptr, 18, true);
		Text(TEXT("purse"), Server->bMaterialsKnown ? FString::Printf(TEXT("YOUR WALLET: %d D-BUCKS"), Server->DBucks) : FString(TEXT("YOUR WALLET: NOT PROVED YET")),
			W * 0.5f * S, (H - 34.0f) * S, 10, SenniteUi::Dim, 0.5f, false);
	}
	if (Server != nullptr && Server->IsOn() && !Server->Message.IsEmpty() && (Server->bBusy || !Server->HeroOnItsWay.IsEmpty())) {
		Text(TEXT("shop"), Server->Message.ToUpper(), W * 0.5f * S, 92.0f * S, 10, White, 0.5f, false);
	}
}

void ASenniteMenuHUD::DrawJoin(ASenniteMenuController* Menu)
{
	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	const FLinearColor White = FLinearColor::White;
	SenniteUi::Fade(Canvas, FBox2D(FVector2D(0.0f, 0.0f), FVector2D(W * S, H * S)), FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), false);
	Heading(TEXT("JOIN"), TEXT("A LOBBY"), TEXT("THE HOST'S ADDRESS, AS THEIR LOBBY SHOWS IT"));
	// the address being typed, in a field with a gold edge
	const FBox2D Field(FVector2D(48.0f * S, 134.0f * S), FVector2D(568.0f * S, 206.0f * S));
	SenniteUi::Plate(Canvas, Field, 12.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.7f), FLinearColor(0.02f, 0.03f, 0.05f, 0.7f));
	SenniteUi::Outline(Canvas, Field, 12.0f * S, SenniteUi::Gold.CopyWithNewOpacity(0.85f), FMath::Max(1.0f, 1.5f * S));
	const bool bBlink = FMath::Fmod(GetWorld()->GetRealTimeSeconds(), 1.0) < 0.5;
	Text(TEXT("address"), Menu->Address + (bBlink ? TEXT("_") : TEXT(" ")), 68.0f * S, 152.0f * S, 24, White);
	// the address without the blinking mark, for a check to read
	Drawing.Pop();
	Drawing.Add(FString::Printf(TEXT("address=%s"), *Menu->Address));
	Button(TEXT("connect"), TEXT("JOIN"), 48.0f, 228.0f, 250.0f, 64.0f, !Menu->Address.TrimStartAndEnd().IsEmpty(), 18, true);
	Button(TEXT("back"), TEXT("BACK"), 48.0f, H - 94.0f, 200.0f, 60.0f, true, 16, false, -1);

	// the keypad: what an address is typed with on a phone
	static const TCHAR* Keys[] = { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"), TEXT("8"), TEXT("9"), TEXT("."), TEXT("0"), TEXT(":") };
	const float KW = 104.0f;
	const float KH = 86.0f;
	const float Gap = 10.0f;
	const float X0 = W - 36.0f - 3.0f * KW - 2.0f * Gap;
	const float Y0 = 120.0f;
	for (int32 Index = 0; Index < 12; ++Index) {
		Button(FName(*FString::Printf(TEXT("key:%s"), Keys[Index])), Keys[Index], X0 + (Index % 3) * (KW + Gap), Y0 + (Index / 3) * (KH + Gap), KW, KH, true, 28);
	}
	Button(TEXT("key:del"), TEXT("DELETE"), X0, Y0 + 4.0f * (KH + Gap), 3.0f * KW + 2.0f * Gap, 70.0f, !Menu->Address.IsEmpty(), 16);
}

void ASenniteMenuHUD::DrawLobby(ASenniteMenuController* Menu)
{
	const float H = Canvas->ClipY / S;
	const FLinearColor White = FLinearColor::White;
	const bool bHost = GetNetMode() != NM_Client;
	SenniteUi::Fade(Canvas, FBox2D(FVector2D(0.0f, 0.0f), FVector2D(620.0f * S, H * S)), FLinearColor(0.0f, 0.0f, 0.0f, 0.62f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), false);
	Heading(TEXT("LOBBY"), FString(), FString());
	if (bHost) {
		const TArray<FString> Addresses = Menu->LocalAddresses();
		Text(TEXT("addresses"), Addresses.Num() > 0 ? FString::Printf(TEXT("OTHERS JOIN AT  %s"), *FString::Join(Addresses, TEXT("   "))) : FString(TEXT("NO NETWORK: OTHERS CANNOT JOIN")),
			50.0f * S, 96.0f * S, 12, SenniteUi::Gold);
	}
	else {
		Text(TEXT("addresses"), TEXT("THE HOST STARTS WHEN EVERYONE IS IN"), 50.0f * S, 96.0f * S, 12, SenniteUi::Gold);
	}
	// who is in: a row each, the player's name, a mark for the host, and their hero at the row's right
	const TArray<const ASennitePlayerState*> Players = Menu->LobbyPlayers();
	const float Row = 44.0f;
	const float Top = 132.0f;
	const float PanelLeft = 40.0f;
	const float PanelWide = 470.0f;
	const int32 Rows = FMath::Max(1, Players.Num());
	SenniteUi::Panel(Canvas, FBox2D(FVector2D(PanelLeft * S, Top * S), FVector2D((PanelLeft + PanelWide) * S, (Top + 14.0f + Row * Rows) * S)), S);
	for (int32 Index = 0; Index < Players.Num(); ++Index) {
		const ASennitePlayerState* Player = Players[Index];
		const FSenniteHero& Hero = USenniteHeroes::FindOrDefault(Player->HeroId);
		const bool bMe = Player == Menu->PlayerState;
		const float Y = (Top + 14.0f + Index * Row) * S;
		if (Index > 0) {
			SenniteUi::Plate(Canvas, FBox2D(FVector2D((PanelLeft + 16.0f) * S, Y - 5.0f * S), FVector2D((PanelLeft + PanelWide - 16.0f) * S, Y - 4.0f * S)), 0.0f, SenniteUi::Edge, SenniteUi::Edge);
		}
		const FString Who = FString::Printf(TEXT("%s%s"), *Player->GetPlayerName().Left(18), bMe ? TEXT(" (YOU)") : TEXT(""));
		SenniteUi::Text(Canvas, Who, (PanelLeft + 20.0f) * S, Y, 14, S, bMe ? White : SenniteUi::Dim);
		if (Index == 0) {
			SenniteUi::Text(Canvas, TEXT("HOST"), (PanelLeft + 20.0f) * S + SenniteUi::Measure(Who, 14, S).X + 12.0f * S, Y + 4.0f * S, 9, S, SenniteUi::Gold);
		}
		SenniteUi::Text(Canvas, Hero.Name, (PanelLeft + PanelWide - 20.0f) * S, Y, 14, S, SenniteUi::Gold, 1.0f);
		Drawing.Add(FString::Printf(TEXT("player%d=%s  -  %s%s"), Index, *Who, *Hero.Name, Index == 0 ? TEXT("   HOST") : TEXT("")));
	}
	Text(TEXT("count"), FString::Printf(TEXT("%d IN THE LOBBY"), Players.Num()), 52.0f * S, (Top + 26.0f + Row * Rows) * S, 11, SenniteUi::Dim, 0.0f, false);
	Button(TEXT("heroes"), TEXT("HEROES"), 48.0f, H - 244.0f, 300.0f, 62.0f);
	if (bHost) {
		Button(TEXT("start"), TEXT("START"), 48.0f, H - 170.0f, 300.0f, 62.0f, true, 18, true);
	}
	else {
		Text(TEXT("waiting"), TEXT("WAITING FOR THE HOST"), 52.0f * S, (H - 154.0f) * S, 14, SenniteUi::Dim);
	}
	Button(TEXT("leave"), TEXT("LEAVE"), 48.0f, H - 96.0f, 300.0f, 62.0f, true, 16, false, -1);
}
