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
	const float Distance = FMath::Max(Top * 1.2f / (2.0f * HalfUp), Row * 1.25f / (2.0f * HalfAcross));
	const float Across = 2.0f * Distance * HalfAcross;
	const float Aside = Page == ESenniteMenuPage::Heroes ? 0.0f : -0.17f * Across;
	const FVector Goal(-Distance, Aside, Top * 0.52f);
	const FVector Now = FMath::VInterpTo(StageCamera->GetActorLocation(), Goal, DeltaTime, 4.0f);
	StageCamera->SetActorLocationAndRotation(Now, FRotator(-2.0, 0.0, 0.0));
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
			State->ServerChooseHero(Hero.Id, Server != nullptr && !Hero.bFree ? Server->TakeHeroPass() : FString());
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
	for (const FSenniteMenuButton& Each : Buttons) {
		if (Each.bEnabled && Each.Box.IsInside(Where)) {
			return Each.Action;
		}
	}
	return NAME_None;
}

bool ASenniteMenuHUD::Centre(FName Action, FVector2D& Out) const
{
	for (const FSenniteMenuButton& Each : Buttons) {
		if (Each.Action == Action) {
			Out = Each.Box.GetCenter();
			return true;
		}
	}
	return false;
}

void ASenniteMenuHUD::Text(const TCHAR* Name, const FString& Line, float X, float Y, int32 Points, const FLinearColor& Colour, bool bCentre)
{
	UFont* Font = GEngine->GetLargeFont();
	if (Font == nullptr) {
		return;
	}
	const float Scale = Points * 1.4f * S / FMath::Max(1.0f, float(Font->GetMaxCharHeight()));
	if (bCentre) {
		X -= Width(Line, Points) * 0.5f;
	}
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Line), Font, Colour);
	Item.Scale = FVector2D(Scale);
	Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
	Canvas->DrawItem(Item);
	Drawing.Add(FString::Printf(TEXT("%s=%s"), Name, *Line));
}

void ASenniteMenuHUD::Button(FName Action, const FString& Label, float X, float Y, float Width, float Height, bool bEnabled, int32 Points)
{
	X *= S;
	Y *= S;
	Width *= S;
	Height *= S;
	const FLinearColor Plate = bEnabled ? FLinearColor(0.02f, 0.03f, 0.05f, 0.72f) : FLinearColor(0.02f, 0.03f, 0.05f, 0.35f);
	DrawRect(Plate, X, Y, Width, Height);
	DrawRect(bEnabled ? FLinearColor(0.95f, 0.75f, 0.2f, 0.9f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.2f), X, Y, 4.0f * S, Height);
	const float TextHeight = Points * 1.4f * S;
	Text(*FString::Printf(TEXT("button:%s"), *Action.ToString()), Label, X + Width * 0.5f, Y + (Height - TextHeight) * 0.5f, Points,
		bEnabled ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.4f), true);
	FSenniteMenuButton Each;
	Each.Action = Action;
	Each.Text = Label;
	Each.Box = FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + Height));
	Each.bEnabled = bEnabled;
	Buttons.Add(Each);
}

void ASenniteMenuHUD::DrawHUD()
{
	Super::DrawHUD();
	Buttons.Reset();
	Drawing.Reset();
	// every size is for a screen 720 lines high and scales with the screen, phone or desktop
	S = Canvas->ClipY / 720.0f;
	ASenniteMenuController* Menu = Cast<ASenniteMenuController>(PlayerOwner);
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
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), W * 0.5f - 330.0f * S, 18.0f * S, 660.0f * S, 40.0f * S);
		Text(TEXT("note"), Menu->Note, W * 0.5f, 26.0f * S, 14, FLinearColor(1.0f, 0.85f, 0.4f), true);
	}
	LastDrawn = Drawing;
}

void ASenniteMenuHUD::DrawMain(ASenniteMenuController* Menu)
{
	const float H = Canvas->ClipY / S;
	const FLinearColor White = FLinearColor::White;
	Text(TEXT("title"), TEXT("SENNITE: FACTIONS"), 48.0f * S, 44.0f * S, 30, White);
	Text(TEXT("subtitle"), TEXT("BETA"), 52.0f * S, 96.0f * S, 12, FLinearColor(0.95f, 0.75f, 0.2f));
	const float X = 48.0f;
	float Y = 150.0f;
	const float Step = 76.0f;
	for (const TPair<const TCHAR*, const TCHAR*>& Each : { TPair<const TCHAR*, const TCHAR*>(TEXT("play"), TEXT("PLAY")), { TEXT("heroes"), TEXT("HEROES") },
			{ TEXT("host"), TEXT("HOST A LOBBY") }, { TEXT("join"), TEXT("JOIN A LOBBY") }, { TEXT("quit"), TEXT("QUIT") } }) {
		Button(Each.Key, Each.Value, X, Y, 300.0f, 62.0f);
		Y += Step;
	}
	// bottom right, under the turning hero: who the player is
	const FSenniteHero& Hero = USenniteHeroes::FindOrDefault(USenniteProfile::Get().PlayingHero(Menu));
	Text(TEXT("hero"), Hero.Name, Canvas->ClipX - 48.0f * S - Width(Hero.Name, 28), (H - 96.0f) * S, 28, White);
	Text(TEXT("yourhero"), TEXT("YOUR HERO"), Canvas->ClipX - 48.0f * S - Width(TEXT("YOUR HERO"), 11), (H - 124.0f) * S, 11, FLinearColor(0.95f, 0.75f, 0.2f));
}

float ASenniteMenuHUD::Width(const FString& Line, int32 Points) const
{
	UFont* Font = GEngine->GetLargeFont();
	float Wide = 0.0f;
	float High = 0.0f;
	if (Font != nullptr) {
		const float Scale = Points * 1.4f * S / FMath::Max(1.0f, float(Font->GetMaxCharHeight()));
		Canvas->TextSize(Font, Line, Wide, High, Scale, Scale);
	}
	return Wide;
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
	const FLinearColor Gold(0.95f, 0.75f, 0.2f);
	Button(TEXT("back"), TEXT("BACK"), 24.0f, 24.0f, 150.0f, 56.0f);
	Text(TEXT("count"), FString::Printf(TEXT("%d / %d"), Menu->Browse + 1, Heroes.Num()), (W - 48.0f) * S, 36.0f * S, 14, White, true);
	Button(TEXT("prev"), TEXT("<"), 24.0f, H * 0.5f - 60.0f, 96.0f, 120.0f, true, 34);
	Button(TEXT("next"), TEXT(">"), W - 120.0f, H * 0.5f - 60.0f, 96.0f, 120.0f, true, 34);

	Text(TEXT("name"), Hero.Name, W * 0.5f * S, (H - 206.0f) * S, 34, White, true);
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
	Text(TEXT("status"), Status, W * 0.5f * S, (H - 150.0f) * S, 14, bMay ? White : Gold, true);

	const float BW = 320.0f;
	const float BX = W * 0.5f - BW * 0.5f;
	const float BY = H - 104.0f;
	if (bChosen) {
		Button(TEXT("chosen"), TEXT("CHOSEN"), BX, BY, BW, 64.0f, false);
	}
	else if (bMay) {
		Button(TEXT("choose"), TEXT("CHOOSE"), BX, BY, BW, 64.0f);
	}
	else if (Server == nullptr || !Server->IsOn()) {
		Button(TEXT("buy"), TEXT("NO SHOP ON THIS GAME"), BX, BY, BW, 64.0f, false, 14);
	}
	else if (!Server->bConnected) {
		Button(TEXT("buy"), TEXT("CONNECT A WALLET TO BUY"), BX, BY, BW, 64.0f, false, 14);
		if (!Server->ConnectCode.IsEmpty()) {
			Text(TEXT("connect"), FString::Printf(TEXT("WALLET CONNECT CODE  %s"), *Server->ConnectCode), W * 0.5f * S, (H - 32.0f) * S, 10, White, true);
		}
	}
	else if (Server->HeroOnItsWay == Hero.Id.ToString() || Server->bBusy) {
		Button(TEXT("buy"), Server->bBusy ? TEXT("BUYING ...") : TEXT("ON ITS WAY TO YOUR WALLET"), BX, BY, BW, 64.0f, false, 14);
	}
	else {
		const FSenniteHeroForSale* ForSale = Server->HeroForSale(Hero.Id.ToString());
		Button(TEXT("buy"), ForSale != nullptr ? FString::Printf(TEXT("BUY  %d D-BUCKS"), ForSale->Price) : FString(TEXT("NOT IN THE SHOP")), BX, BY, BW, 64.0f, ForSale != nullptr);
		Text(TEXT("purse"), Server->bMaterialsKnown ? FString::Printf(TEXT("YOUR WALLET: %d D-BUCKS"), Server->DBucks) : FString(TEXT("YOUR WALLET: NOT PROVED YET")),
			W * 0.5f * S, (H - 32.0f) * S, 10, White, true);
	}
	if (Server != nullptr && Server->IsOn() && !Server->Message.IsEmpty() && (Server->bBusy || !Server->HeroOnItsWay.IsEmpty())) {
		Text(TEXT("shop"), Server->Message.ToUpper(), W * 0.5f * S, 90.0f * S, 10, White, true);
	}
}

void ASenniteMenuHUD::DrawJoin(ASenniteMenuController* Menu)
{
	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	const FLinearColor White = FLinearColor::White;
	Text(TEXT("title"), TEXT("JOIN A LOBBY"), 48.0f * S, 44.0f * S, 26, White);
	Text(TEXT("help"), TEXT("THE HOST'S ADDRESS, AS THEIR LOBBY SHOWS IT"), 50.0f * S, 92.0f * S, 11, FLinearColor(1.0f, 1.0f, 1.0f, 0.75f));
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f), 48.0f * S, 130.0f * S, 520.0f * S, 70.0f * S);
	const bool bBlink = FMath::Fmod(GetWorld()->GetRealTimeSeconds(), 1.0) < 0.5;
	Text(TEXT("address"), Menu->Address + (bBlink ? TEXT("_") : TEXT(" ")), 64.0f * S, 146.0f * S, 22, White);
	// the address without the blinking mark, for a check to read
	Drawing.Pop();
	Drawing.Add(FString::Printf(TEXT("address=%s"), *Menu->Address));
	Button(TEXT("connect"), TEXT("JOIN"), 48.0f, 222.0f, 250.0f, 64.0f, !Menu->Address.TrimStartAndEnd().IsEmpty());
	Button(TEXT("back"), TEXT("BACK"), 48.0f, H - 96.0f, 200.0f, 62.0f);

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
	Button(TEXT("key:del"), TEXT("DELETE"), X0, Y0 + 4.0f * (KH + Gap), 3.0f * KW + 2.0f * Gap, 70.0f, !Menu->Address.IsEmpty());
}

void ASenniteMenuHUD::DrawLobby(ASenniteMenuController* Menu)
{
	const float H = Canvas->ClipY / S;
	const FLinearColor White = FLinearColor::White;
	const FLinearColor Dim(1.0f, 1.0f, 1.0f, 0.75f);
	const FLinearColor Gold(0.95f, 0.75f, 0.2f);
	const bool bHost = GetNetMode() != NM_Client;
	Text(TEXT("title"), TEXT("LOBBY"), 48.0f * S, 40.0f * S, 28, White);
	if (bHost) {
		const TArray<FString> Addresses = Menu->LocalAddresses();
		Text(TEXT("addresses"), Addresses.Num() > 0 ? FString::Printf(TEXT("OTHERS JOIN AT  %s"), *FString::Join(Addresses, TEXT("   "))) : FString(TEXT("NO NETWORK: OTHERS CANNOT JOIN")),
			50.0f * S, 90.0f * S, 12, Gold);
	}
	else {
		Text(TEXT("addresses"), TEXT("THE HOST STARTS WHEN EVERYONE IS IN"), 50.0f * S, 90.0f * S, 12, Gold);
	}
	const TArray<const ASennitePlayerState*> Players = Menu->LobbyPlayers();
	const float Row = 40.0f;
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f), 40.0f * S, 126.0f * S, 440.0f * S, (16.0f + Row * FMath::Max(1, Players.Num())) * S);
	for (int32 Index = 0; Index < Players.Num(); ++Index) {
		const ASennitePlayerState* Player = Players[Index];
		const FSenniteHero& Hero = USenniteHeroes::FindOrDefault(Player->HeroId);
		const bool bMe = Player == Menu->PlayerState;
		const FString Line = FString::Printf(TEXT("%s%s  -  %s%s"), *Player->GetPlayerName().Left(18), bMe ? TEXT(" (YOU)") : TEXT(""), *Hero.Name, Index == 0 ? TEXT("   HOST") : TEXT(""));
		Text(*FString::Printf(TEXT("player%d"), Index), Line, 56.0f * S, (136.0f + Index * Row) * S, 14, bMe ? White : Dim);
	}
	Text(TEXT("count"), FString::Printf(TEXT("%d IN THE LOBBY"), Players.Num()), 50.0f * S, (146.0f + Row * FMath::Max(1, Players.Num())) * S, 11, Dim);
	Button(TEXT("heroes"), TEXT("HEROES"), 48.0f, H - 250.0f, 280.0f, 62.0f);
	if (bHost) {
		Button(TEXT("start"), TEXT("START"), 48.0f, H - 172.0f, 280.0f, 62.0f);
	}
	else {
		Text(TEXT("waiting"), TEXT("WAITING FOR THE HOST"), 52.0f * S, (H - 156.0f) * S, 14, Dim);
	}
	Button(TEXT("leave"), TEXT("LEAVE"), 48.0f, H - 94.0f, 280.0f, 62.0f);
}
