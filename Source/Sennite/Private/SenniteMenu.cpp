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
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "IPAddress.h"
#include "Kismet/GameplayStatics.h"
#include "LobbyGameMode.h"
#include "SenniteBaseEditor.h"
#include "SenniteGameInstance.h"
#include "SenniteHeroes.h"
#include "SennitePlayerState.h"
#include "SennitePublisher.h"
#include "SenniteUi.h"
#include "SenniteWeapons.h"
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

	/** The mesh a shop skin is ([/Script/Sennite.SenniteBaseEditor] in DefaultGame.ini), or null for a skin this game has no mesh for. */
	const FSenniteSkinMesh* SkinMesh(const FString& SkinId)
	{
		return GetDefault<ASenniteBaseEditor>()->Skins.FindByPredicate([&SkinId](const FSenniteSkinMesh& Each) { return Each.Id == SkinId; });
	}

	/**
	 * The hero a shop skin is a skin of: the one the game's list of skins names for it, and for a skin
	 * listed with none, the one whose own mesh has the skin's skeleton, as the game goes by when it
	 * dresses a hero in play. Null if it is no hero's. Found once for each skin.
	 */
	const FSenniteHero* SkinHero(const FString& SkinId)
	{
		if (const FSenniteSkinMesh* Listed = SkinMesh(SkinId); Listed != nullptr && !Listed->Hero.IsNone()) {
			return USenniteHeroes::Find(Listed->Hero);
		}
		static TMap<FString, FName> Known;
		if (const FName* Found = Known.Find(SkinId)) {
			return Found->IsNone() ? nullptr : USenniteHeroes::Find(*Found);
		}
		FName Of;
		const FSenniteSkinMesh* Skin = SkinMesh(SkinId);
		if (const USkeletalMesh* Mesh = Skin != nullptr ? Cast<USkeletalMesh>(Skin->Mesh.TryLoad()) : nullptr) {
			// only the heroes of the skin's own pack are looked at: no other hero is loaded to find this out
			TArray<FString> Parts;
			Skin->Mesh.GetLongPackageName().ParseIntoArray(Parts, TEXT("/"));
			const FString Pack = Parts.Num() > 1 ? FString::Printf(TEXT("/%s/%s/"), *Parts[0], *Parts[1]) : FString();
			for (const FSenniteHero& Hero : USenniteHeroes::All()) {
				if (Pack.IsEmpty() || !Hero.Mesh.GetLongPackageName().StartsWith(Pack)) {
					continue;
				}
				const USkeletalMesh* Own = Cast<USkeletalMesh>(Hero.Mesh.TryLoad());
				if (Own != nullptr && Own->GetSkeleton() == Mesh->GetSkeleton()) {
					Of = Hero.Id;
					break;
				}
			}
		}
		Known.Add(SkinId, Of);
		return Of.IsNone() ? nullptr : USenniteHeroes::Find(Of);
	}

	/**
	 * The gun a shop's id is: the game's own weapon, whose model and name the store shows. Null for an id that
	 * is no gun of this game's.
	 */
	const AWeaponActor* ShopGun(const FString& WeaponId)
	{
		const UClass* Gun = WeaponId == TEXT("energy-rifle") ? ASenniteEnergyRifle::StaticClass() : WeaponId == TEXT("sniper") ? ASenniteSniper::StaticClass() : nullptr;
		return Gun != nullptr ? GetDefault<AWeaponActor>(Gun) : nullptr;
	}

	/** What the stage calls a shop skin standing on it. */
	const TCHAR* SkinOnStage = TEXT("skin:");
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

bool ASenniteMenuController::SwapSides(const USennitePublisher* Server, const FSenniteSwapToken*& From, const FSenniteSwapToken*& To) const
{
	const int32 Count = Server != nullptr ? Server->SwapTokens.Num() : 0;
	if (Count < 2) {
		return false;
	}
	const int32 Paid = FMath::Clamp(SwapFrom, 0, Count - 1);
	// what is got is never what is paid
	const int32 Got = FMath::Clamp(SwapTo, 0, Count - 1) == Paid ? (Paid + 1) % Count : FMath::Clamp(SwapTo, 0, Count - 1);
	From = &Server->SwapTokens[Paid];
	To = &Server->SwapTokens[Got];
	return true;
}

bool ASenniteMenuController::SwapAmountIsOne() const
{
	return !SwapAmount.IsEmpty() && !SwapAmount.EndsWith(TEXT(".")) && FCString::Atod(*SwapAmount) > 0.0;
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
	// what the menu said to do in the wallet is done once the wallet is connected
	if (bNoteForWallet) {
		const USennitePublisher* Server = Shop(this);
		if (Note.IsEmpty() || Server == nullptr) {
			bNoteForWallet = false;
		}
		else if (Server->bConnected) {
			bNoteForWallet = false;
			Note.Reset();
		}
	}
	// the store's swap shows the vaults as they are: they are read again every few seconds while it is open
	if (Page == ESenniteMenuPage::Swap && FPlatformTime::Seconds() >= NextSwapRead) {
		const UGameInstance* Game = GetGameInstance();
		if (USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr) {
			Server->AskSwap();
		}
		NextSwapRead = FPlatformTime::Seconds() + 10.0;
	}
	if (Page == ESenniteMenuPage::Swap && !bSwapOpened) {
		// the first time the vaults are read: what a wallet comes with, ERA, is what is paid, for the first other thing
		const USennitePublisher* Server = Shop(this);
		if (Server != nullptr && Server->SwapTokens.Num() >= 2) {
			bSwapOpened = true;
			SwapFrom = FMath::Max(0, Server->SwapTokens.IndexOfByPredicate([](const FSenniteSwapToken& Held) { return Held.Symbol == TEXT("ERA"); }));
			SwapTo = SwapFrom == 0 ? 1 : 0;
		}
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
	else if (const USennitePublisher* Server = Shop(this); Page == ESenniteMenuPage::Skins && Server != nullptr && Server->ShopSkins.IsValidIndex(BrowseSkin)
		&& SkinHero(Server->ShopSkins[BrowseSkin].Id) != nullptr) {
		// the skin the store shows, on the hero it is a skin of
		Want.Add(FName(*(FString(SkinOnStage) + Server->ShopSkins[BrowseSkin].Id)));
	}
	else {
		Want.Add(USenniteProfile::Get().PlayingHero(this));
	}
	// the store's weapons: the gun shown turns on the stage, in the hero's place
	FString WantGun;
	if (const USennitePublisher* Seller = Shop(this); Page == ESenniteMenuPage::Weapons && Seller != nullptr && Seller->ShopWeapons.IsValidIndex(BrowseWeapon)
		&& ShopGun(Seller->ShopWeapons[BrowseWeapon].Id) != nullptr) {
		WantGun = Seller->ShopWeapons[BrowseWeapon].Id;
		Want.Reset();
	}
	if (Want != FigureHeroes) {
		StandHeroes(Want);
	}
	if (WantGun != StageGunId) {
		StandGun(WantGun);
	}

	// one hero turns on the turntable; the lobby's row faces the camera
	Spin = FMath::Fmod(Spin + 30.0f * DeltaTime, 360.0f);
	if (StageGun != nullptr) {
		// a gun's model has its muzzle along +X: side on to the camera, turning
		StageGun->SetActorRotation(FRotator(0.0, 90.0 + Spin, 0.0));
	}
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
	// the buttons are to the left and the hero to the right on every page. On the character select the row
	// of heroes to pick from lies along the bottom, so the hero stands a little further back, whole above
	// it; a lobby's row is seen from far enough that the heroes at its ends are whole too
	// the skins have two rows along the bottom, the heroes that have skins over the skins of one: a little further back again
	const bool bSkins = Page == ESenniteMenuPage::Skins;
	const bool bSelect = Page == ESenniteMenuPage::Heroes || bSkins || Page == ESenniteMenuPage::Weapons;
	const float Distance = FMath::Max(Top * (bSkins ? 1.6f : bSelect ? 1.38f : 1.2f) / (2.0f * HalfUp), Row * (Figures.Num() > 1 ? 2.1f : 1.25f) / (2.0f * HalfAcross));
	const float Across = 2.0f * Distance * HalfAcross;
	const float Aside = -0.17f * Across;
	const FVector Goal(-Distance, Aside, Top * (bSkins ? 0.4f : bSelect ? 0.47f : 0.52f));
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
		// a hero as it ships, or a shop skin: the hero it is a skin of, in the skin's mesh
		const FString Standing = Heroes[Index].ToString();
		const FString SkinId = Standing.StartsWith(SkinOnStage) ? Standing.Mid(FCString::Strlen(SkinOnStage)) : FString();
		const FSenniteSkinMesh* Skin = SkinId.IsEmpty() ? nullptr : SkinMesh(SkinId);
		const FSenniteHero* Skinned = Skin != nullptr ? SkinHero(SkinId) : nullptr;
		const FSenniteHero& Hero = Skinned != nullptr ? *Skinned : USenniteHeroes::FindOrDefault(Heroes[Index]);
		const FSoftObjectPath Wearing = Skinned != nullptr ? Skin->Mesh : Hero.Mesh;
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
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Wearing.TryLoad())) {
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

void ASenniteMenuController::StandGun(const FString& WeaponId)
{
	if (StageGun != nullptr) {
		StageGun->Destroy();
		StageGun = nullptr;
	}
	StageGunId = WeaponId;
	const AWeaponActor* Gun = WeaponId.IsEmpty() ? nullptr : ShopGun(WeaponId);
	UStaticMesh* Model = Gun != nullptr ? Cast<UStaticMesh>(Gun->MeshAsset.TryLoad()) : nullptr;
	if (Model == nullptr) {
		return;
	}
	// held up where a hero's chest would be, larger than life: a gun is an arm's length, the stage a hero's height
	const FVector Where(0.0, 0.0, 78.0);
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	Spawn.bDeferConstruction = true;
	StageGun = GetWorld()->SpawnActor<AStaticMeshActor>(Where, FRotator(0.0, 90.0, 0.0), Spawn);
	if (StageGun == nullptr) {
		return;
	}
	// the stage is this player's own: nothing on it goes to anyone else's game
	StageGun->SetReplicates(false);
	StageGun->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	StageGun->FinishSpawning(FTransform(FRotator(0.0, 90.0, 0.0), Where, FVector(1.6)));
	StageGun->GetStaticMeshComponent()->SetStaticMesh(Model);
	StageGun->SetActorEnableCollision(false);
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
	if (Page == ESenniteMenuPage::Heroes || Page == ESenniteMenuPage::Skins || Page == ESenniteMenuPage::Weapons) {
		if (Key == EKeys::Left || Key == EKeys::A) {
			Act(TEXT("prev"));
			return true;
		}
		if (Key == EKeys::Right || Key == EKeys::D) {
			Act(TEXT("next"));
			return true;
		}
		if (Key == EKeys::Enter && Page == ESenniteMenuPage::Heroes) {
			Act(TEXT("choose"));
			return true;
		}
	}
	if (Page == ESenniteMenuPage::Swap) {
		if (Key == EKeys::Enter) {
			Act(TEXT("price"));
			return true;
		}
		if (Key == EKeys::BackSpace) {
			Act(TEXT("num:del"));
			return true;
		}
		if (Key == EKeys::Period || Key == EKeys::Decimal) {
			Act(TEXT("num:."));
			return true;
		}
		static const FKey Digits[] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
		static const FKey Pad[] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
		for (int32 Digit = 0; Digit < 10; ++Digit) {
			if (Key == Digits[Digit] || Key == Pad[Digit]) {
				Act(FName(*FString::Printf(TEXT("num:%d"), Digit)));
				return true;
			}
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
	else if (Name == TEXT("store") || Name == TEXT("heroes") || Name == TEXT("tab:heroes")) {
		// the store opens on its heroes; the lobby's HEROES is the same section
		Page = ESenniteMenuPage::Heroes;
		Browse = IndexOf(Profile.PlayingHero(this));
		Note.Reset();
	}
	else if (Name == TEXT("tab:skins")) {
		Page = ESenniteMenuPage::Skins;
		// on the skin worn, if one is
		const USennitePublisher* Server = Shop(this);
		BrowseSkin = Server != nullptr ? FMath::Max(0, Server->ShopSkins.IndexOfByPredicate([Server](const FSenniteSkinForSale& Skin) { return Skin.Id == Server->Wearing; })) : 0;
		Note.Reset();
	}
	else if (Name == TEXT("tab:weapons")) {
		Page = ESenniteMenuPage::Weapons;
		BrowseWeapon = 0;
		Note.Reset();
	}
	else if ((Name == TEXT("prev") || Name == TEXT("next")) && Page == ESenniteMenuPage::Weapons) {
		const USennitePublisher* Server = Shop(this);
		const int32 Count = Server != nullptr ? Server->ShopWeapons.Num() : 0;
		BrowseWeapon = Count > 0 ? (BrowseWeapon + (Name == TEXT("next") ? 1 : -1) + Count) % Count : 0;
		Note.Reset();
	}
	else if (Name.StartsWith(TEXT("gun:"))) {
		// a gun's own tile in the store
		const USennitePublisher* Server = Shop(this);
		const FString Id = Name.Mid(4);
		const int32 At = Server != nullptr ? Server->ShopWeapons.IndexOfByPredicate([&Id](const FSenniteHeroForSale& Gun) { return Gun.Id == Id; }) : INDEX_NONE;
		if (At != INDEX_NONE) {
			BrowseWeapon = At;
			Note.Reset();
		}
	}
	else if (Name == TEXT("buygun")) {
		const UGameInstance* Game = GetGameInstance();
		USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
		if (Server != nullptr && Server->IsOn() && Server->bConnected && !Server->bBusy && Server->ShopWeapons.IsValidIndex(BrowseWeapon)
			&& !Server->OwnsWeapon(Server->ShopWeapons[BrowseWeapon].Id)) {
			Server->BuyWeapon(Server->ShopWeapons[BrowseWeapon].Id);
		}
	}
	else if (Name == TEXT("tab:swap")) {
		Page = ESenniteMenuPage::Swap;
		bSwapSaid = false;
		NextSwapRead = 0.0;
		Note.Reset();
	}
	else if (Name == TEXT("swap:from") || Name == TEXT("swap:to") || Name == TEXT("swap:flip")) {
		const USennitePublisher* Server = Shop(this);
		const int32 Count = Server != nullptr ? Server->SwapTokens.Num() : 0;
		if (Count < 2) {
			return;
		}
		SwapFrom = FMath::Clamp(SwapFrom, 0, Count - 1);
		SwapTo = FMath::Clamp(SwapTo, 0, Count - 1);
		if (Name == TEXT("swap:flip")) {
			Swap(SwapFrom, SwapTo);
		}
		else {
			// the next thing the vaults hold; if that is the other side's, the two change places
			int32& Side = Name == TEXT("swap:from") ? SwapFrom : SwapTo;
			int32& Other = Name == TEXT("swap:from") ? SwapTo : SwapFrom;
			const int32 Was = Side;
			Side = (Side + 1) % Count;
			if (Side == Other) {
				Other = Was;
			}
		}
		// an amount typed with more digits after the point than what is now paid has is typed again
		int32 Point = INDEX_NONE;
		if (SwapAmount.FindChar(TEXT('.'), Point) && SwapAmount.Len() - Point - 1 > Server->SwapTokens[SwapFrom].Decimals) {
			SwapAmount.Reset();
		}
	}
	else if (Name.StartsWith(TEXT("num:"))) {
		const USennitePublisher* Server = Shop(this);
		const FSenniteSwapToken* From = nullptr;
		const FSenniteSwapToken* To = nullptr;
		if (Server == nullptr || !SwapSides(Server, From, To)) {
			return;
		}
		const FString Typed = Name.Mid(4);
		int32 Point = INDEX_NONE;
		const bool bPoint = SwapAmount.FindChar(TEXT('.'), Point);
		if (Typed == TEXT("del")) {
			SwapAmount.LeftChopInline(1);
		}
		else if (Typed == TEXT(".")) {
			// one point, and only in an amount of something that has digits after it
			if (!bPoint && From->Decimals > 0 && SwapAmount.Len() < 9) {
				SwapAmount += SwapAmount.IsEmpty() ? TEXT("0.") : TEXT(".");
			}
		}
		else if (Typed.Len() == 1 && FChar::IsDigit(Typed[0]) && SwapAmount.Len() < 10 && (!bPoint || SwapAmount.Len() - Point - 1 < From->Decimals)) {
			// a whole number does not start with a zero that says nothing
			if (SwapAmount == TEXT("0")) {
				SwapAmount.Reset();
			}
			SwapAmount += Typed;
		}
	}
	else if (Name == TEXT("price") || Name == TEXT("trade")) {
		const UGameInstance* Game = GetGameInstance();
		USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
		const FSenniteSwapToken* From = nullptr;
		const FSenniteSwapToken* To = nullptr;
		if (Server == nullptr || !Server->IsOn() || !Server->bConnected || Server->bBusy || !SwapSides(Server, From, To) || !SwapAmountIsOne()) {
			return;
		}
		bSwapSaid = true;
		if (Name == TEXT("price")) {
			Server->AskSwapPrice(From->Id, To->Id, SwapAmount);
		}
		else {
			Server->Swap(From->Id, To->Id, SwapAmount);
		}
	}
	else if (Name == TEXT("base")) {
		// the player's base is on the map the game is played on: it opens there, on the planning view
		const USennitePublisher* Server = Shop(this);
		if (Server != nullptr && Server->IsOn()) {
			UGameplayStatics::OpenLevel(this, FName(PlayMap), true, ASenniteBaseEditor::OpenOption);
		}
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
	else if ((Name == TEXT("prev") || Name == TEXT("next")) && Page == ESenniteMenuPage::Skins) {
		const USennitePublisher* Server = Shop(this);
		const int32 Count = Server != nullptr ? Server->ShopSkins.Num() : 0;
		BrowseSkin = Count > 0 ? (BrowseSkin + (Name == TEXT("next") ? 1 : -1) + Count) % Count : 0;
		Note.Reset();
	}
	else if (Name == TEXT("prev") || Name == TEXT("next")) {
		Browse = (Browse + (Name == TEXT("next") ? 1 : -1) + Heroes.Num()) % FMath::Max(1, Heroes.Num());
		Note.Reset();
	}
	else if (Name.StartsWith(TEXT("skinsof:"))) {
		// a hero's own skins: the first of them the shop lists
		const USennitePublisher* Server = Shop(this);
		const FName Of(*Name.Mid(8));
		const int32 At = Server != nullptr ? Server->ShopSkins.IndexOfByPredicate([Of](const FSenniteSkinForSale& Skin) {
			const FSenniteHero* Hero = SkinHero(Skin.Id);
			return (Hero != nullptr ? Hero->Id : FName()) == Of;
		}) : INDEX_NONE;
		if (At != INDEX_NONE) {
			BrowseSkin = At;
			Note.Reset();
		}
	}
	else if (Name.StartsWith(SkinOnStage)) {
		// a skin's own tile in the store
		const USennitePublisher* Server = Shop(this);
		const FString Id = Name.Mid(FCString::Strlen(SkinOnStage));
		const int32 At = Server != nullptr ? Server->ShopSkins.IndexOfByPredicate([&Id](const FSenniteSkinForSale& Skin) { return Skin.Id == Id; }) : INDEX_NONE;
		if (At != INDEX_NONE) {
			BrowseSkin = At;
			Note.Reset();
		}
	}
	else if (Name == TEXT("buyskin") || Name == TEXT("wear") || Name == TEXT("takeoff")) {
		const UGameInstance* Game = GetGameInstance();
		USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
		if (Server == nullptr || !Server->IsOn() || !Server->ShopSkins.IsValidIndex(BrowseSkin)) {
			return;
		}
		const FString Id = Server->ShopSkins[BrowseSkin].Id;
		if (Name == TEXT("buyskin")) {
			if (Server->bConnected && !Server->bBusy) {
				Server->BuySkin(Id);
			}
		}
		else if (Server->Owns(Id)) {
			// worn, or taken off: it is on the hero in play only while the wallet proves it owns it
			Server->Wear(Name == TEXT("wear") ? Id : FString());
		}
	}
	else if (Name.StartsWith(TEXT("hero:"))) {
		// a hero's own tile on the character select
		const FName Id(*Name.Mid(5));
		if (USenniteHeroes::Find(Id) != nullptr) {
			Browse = IndexOf(Id);
			Note.Reset();
		}
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
	else if (Name == TEXT("wallet")) {
		// the first thing toward a hero that is bought: the player's wallet, connected to this game
		const UGameInstance* Game = GetGameInstance();
		const USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
		if (Server != nullptr && Server->OpenConnectCode()) {
			bNoteForWallet = true;
#if PLATFORM_ANDROID || PLATFORM_IOS
			Note = TEXT("APPROVE THIS GAME IN YOUR DSM WALLET, THEN COME BACK");
#else
			Note = TEXT("THE CONNECT CODE IS COPIED: READ IT IN YOUR DSM WALLET");
#endif
		}
	}
	else if (Name == TEXT("dbucks")) {
		// D-bucks for a hero the wallet holds too few for: ERA traded through the publisher's vault, as in the shop
		const UGameInstance* Game = GetGameInstance();
		USennitePublisher* Server = Game != nullptr ? Game->GetSubsystem<USennitePublisher>() : nullptr;
		if (Server != nullptr && Server->IsOn() && Server->bConnected && !Server->bBusy) {
			Server->BuyDBucks(USennitePublisher::TradeEra());
			Note.Reset();
		}
	}
	else if (Name == TEXT("back")) {
		if (InStore(Page)) {
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

void ASenniteMenuHUD::Text(const TCHAR* Name, const FString& Line, float X, float Y, float Points, const FLinearColor& Colour, float Align, bool bBold)
{
	SenniteUi::Text(Canvas, Line, X, Y, Points, S, Colour, Align, bBold);
	Drawing.Add(FString::Printf(TEXT("%s=%s"), Name, *Line));
}

FBox2D ASenniteMenuHUD::Screen() const
{
	// On a phone the engine moves the canvas in from the screen's edges, clear of its rounded corners and
	// camera: the buttons and text stay in there, and the shades behind them go on to the edges themselves.
	if (Canvas->SafeZonePadX == 0 && Canvas->SafeZonePadY == 0 && Canvas->SafeZonePadEX == 0 && Canvas->SafeZonePadEY == 0) {
		return FBox2D(FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY));
	}
	const FVector2D Min(-float(Canvas->SafeZonePadX), -float(Canvas->SafeZonePadY));
	return FBox2D(Min, Min + FVector2D(FMath::Max(Canvas->UnsafeSizeX, Canvas->SizeX), FMath::Max(Canvas->UnsafeSizeY, Canvas->SizeY)));
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
	// where it is on the screen, which on a phone is not where it is on the canvas
	Each.Box = Box.ShiftBy(Inset);
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
	Inset = -Screen().Min;
	if (Menu == nullptr) {
		return;
	}
	Drawing.Add(FString::Printf(TEXT("page=%d"), int32(Menu->Page)));
	switch (Menu->Page) {
	case ESenniteMenuPage::Main: DrawMain(Menu); break;
	case ESenniteMenuPage::Heroes: DrawHeroes(Menu); break;
	case ESenniteMenuPage::Skins: DrawSkins(Menu); break;
	case ESenniteMenuPage::Weapons: DrawWeapons(Menu); break;
	case ESenniteMenuPage::Swap: DrawSwap(Menu); break;
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
	const FBox2D Whole = Screen();
	SenniteUi::Fade(Canvas, FBox2D(Whole.Min, FVector2D(560.0f * S, Whole.Max.Y)), FLinearColor(0.0f, 0.0f, 0.0f, 0.62f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), false);
	SenniteUi::Fade(Canvas, FBox2D(FVector2D(Whole.Min.X, H - 180.0f * S), Whole.Max), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.5f), true);
	Heading(TEXT("SENNITE"), TEXT("FACTIONS"), TEXT("BETA"));
	const float X = 48.0f;
	float Y = 140.0f;
	const float Step = 72.0f;
	// a base is planned where there is a publisher's server, which keeps the factions' bases
	const USennitePublisher* Server = Shop(Menu);
	const bool bBases = Server != nullptr && Server->IsOn();
	for (const TPair<const TCHAR*, const TCHAR*>& Each : { TPair<const TCHAR*, const TCHAR*>(TEXT("play"), TEXT("PLAY")), { TEXT("store"), TEXT("STORE") },
			{ TEXT("base"), TEXT("BASE EDITOR") }, { TEXT("host"), TEXT("HOST A LOBBY") }, { TEXT("join"), TEXT("JOIN A LOBBY") }, { TEXT("quit"), TEXT("QUIT") } }) {
		Button(Each.Key, Each.Value, X, Y, 320.0f, 60.0f, bBases || FName(Each.Key) != TEXT("base"), 18, FName(Each.Key) == TEXT("play"));
		Y += Step;
	}
	if (!bBases) {
		Text(TEXT("why"), TEXT("THE BASE EDITOR NEEDS THE PUBLISHER'S SERVER"), (X + 2.0f) * S, (Y + 2.0f) * S, 11, SenniteUi::Dim, 0.0f, false);
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
	// the hero's name and what is done with it stand on a shade at the left that fades out toward the hero,
	// as the menu's buttons do; the row of heroes on one that rises from the bottom
	const FBox2D Whole = Screen();
	SenniteUi::Fade(Canvas, FBox2D(Whole.Min, FVector2D(600.0f * S, Whole.Max.Y)), FLinearColor(0.0f, 0.0f, 0.0f, 0.66f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), false);
	SenniteUi::Fade(Canvas, FBox2D(FVector2D(Whole.Min.X, (H - 190.0f) * S), Whole.Max), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.7f), true);
	StoreTop(Menu);

	const float Left = 48.0f;
	Text(TEXT("count"), FString::Printf(TEXT("HERO %d OF %d"), Menu->Browse + 1, Heroes.Num()), Left * S, 122.0f * S, 12, SenniteUi::Gold);
	// the name, as large as the column lets it be
	const float Room = 440.0f * S;
	const float NamePoints = FMath::Min(54.0f, 54.0f * Room / FMath::Max(1.0f, Width(Hero.Name, 54)));
	Text(TEXT("name"), Hero.Name, Left * S, 142.0f * S, NamePoints, White);
	SenniteUi::Plate(Canvas, FBox2D(FVector2D(Left * S, 226.0f * S), FVector2D((Left + 96.0f) * S, 229.0f * S)), 0.0f, SenniteUi::Gold, SenniteUi::Gold);

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
		const FBox2D Tag(FVector2D(Left * S, 246.0f * S), FVector2D(Left * S + Wide, 280.0f * S));
		SenniteUi::Plate(Canvas, Tag, 7.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		SenniteUi::Outline(Canvas, Tag, 7.0f * S, bMay ? SenniteUi::Edge : SenniteUi::Gold.CopyWithNewOpacity(0.8f), FMath::Max(1.0f, 1.5f * S));
		Text(TEXT("status"), Status, Left * S + 18.0f * S, 254.0f * S, 13, bMay ? White : SenniteUi::Gold);
	}

	// What is done with the hero: chosen, or, for one not had yet, the next thing to do toward having it,
	// with what that is said under the button.
	const float BW = 340.0f;
	const float BY = 304.0f;
	bool bHint = false;
	if (bChosen) {
		Button(TEXT("chosen"), TEXT("CHOSEN"), Left, BY, BW, 66.0f, false);
	}
	else if (bMay) {
		Button(TEXT("choose"), TEXT("CHOOSE"), Left, BY, BW, 66.0f, true, 19, true);
	}
	else {
		FForSale Thing;
		Thing.Kind = TEXT("HEROES");
		Thing.Buy = TEXT("buy");
		const FSenniteHeroForSale* ForSale = Server != nullptr ? Server->HeroForSale(Hero.Id.ToString()) : nullptr;
		Thing.bListed = ForSale != nullptr;
		Thing.Price = ForSale != nullptr ? ForSale->Price : 0;
		Thing.bOnItsWay = Server != nullptr && Server->HeroOnItsWay == Hero.Id.ToString();
		bHint = NextStep(Server, Thing, Left, BY, BW);
	}
	ShopWord(Server, Server != nullptr && !Server->HeroOnItsWay.IsEmpty(), (Left + 2.0f) * S, (BY + 80.0f + (bHint ? 52.0f : 0.0f)) * S);

	// the arrows, either side of the hero
	const float HeroX = W * 0.67f;
	Button(TEXT("prev"), TEXT("<"), HeroX - 330.0f, H * 0.5f - 76.0f, 76.0f, 116.0f, true, 34, false, -1);
	Button(TEXT("next"), TEXT(">"), FMath::Min(HeroX + 254.0f, W - 100.0f), H * 0.5f - 76.0f, 76.0f, 116.0f, true, 34, false, 1);

	// every hero along the bottom, each on its own tile: the one shown in gold, the player's own marked,
	// and one not had yet faint with its price mark
	const float Gap = 8.0f;
	const float TileWide = (W - 2.0f * Left - Gap * (Heroes.Num() - 1)) / FMath::Max(1, Heroes.Num());
	const float TileHigh = 56.0f;
	const float TileTop = H - 82.0f;
	const FName Playing = USenniteProfile::Get().PlayingHero(Menu);
	for (int32 Index = 0; Index < Heroes.Num(); ++Index) {
		const FSenniteHero& Each = Heroes[Index];
		const FName Action(*FString::Printf(TEXT("hero:%s"), *Each.Id.ToString()));
		const float X = Left + Index * (TileWide + Gap);
		const FBox2D Box(FVector2D(X * S, TileTop * S), FVector2D((X + TileWide) * S, (TileTop + TileHigh) * S));
		const bool bHad = USenniteHeroes::MayPlay(Menu, Each.Id);
		const SenniteUi::EButton State = Action == Held ? SenniteUi::EButton::Pressed
			: Index == Menu->Browse ? SenniteUi::EButton::Primary
			: Action == Hovered ? SenniteUi::EButton::Hover : SenniteUi::EButton::Plain;
		FLinearColor Ink = SenniteUi::ButtonPlate(Canvas, Box, S, State);
		if (!bHad && Index != Menu->Browse) {
			Ink = FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);
		}
		const float Points = FMath::Min(13.0f, 13.0f * (TileWide - 22.0f) * S / FMath::Max(1.0f, Width(Each.Name, 13)));
		const float High = SenniteUi::Measure(Each.Name, Points, S).Y;
		SenniteUi::Text(Canvas, Each.Name, Box.GetCenter().X, Box.GetCenter().Y - High * 0.5f, Points, S, Ink, 0.5f);
		if (Each.Id == Playing) {
			// the player's hero: a gold line over its tile
			SenniteUi::Plate(Canvas, FBox2D(FVector2D(Box.Min.X + 14.0f * S, Box.Min.Y - 7.0f * S), FVector2D(Box.Max.X, Box.Min.Y - 4.0f * S)), 0.0f, SenniteUi::Gold, SenniteUi::Gold);
		}
		Drawing.Add(FString::Printf(TEXT("tile%d=%s%s%s"), Index, *Each.Name, Each.Id == Playing ? TEXT("  CHOSEN") : TEXT(""), bHad ? TEXT("") : TEXT("  NOT HAD")));
		FSenniteMenuButton Tile;
		Tile.Action = Action;
		Tile.Text = Each.Name;
		Tile.Box = Box.ShiftBy(Inset);
		Buttons.Add(Tile);
	}
}

void ASenniteMenuHUD::StoreTop(ASenniteMenuController* Menu)
{
	const float W = Canvas->ClipX / S;
	Button(TEXT("back"), TEXT("BACK"), 24.0f, 24.0f, 184.0f, 58.0f, true, 17, false, -1);
	// the store's sections, the open one in gold
	struct FSection
	{
		const TCHAR* Action;
		const TCHAR* Name;
		ESenniteMenuPage Page;
	};
	float X = 232.0f;
	for (const FSection& Each : { FSection{ TEXT("tab:heroes"), TEXT("HEROES"), ESenniteMenuPage::Heroes }, FSection{ TEXT("tab:skins"), TEXT("SKINS"), ESenniteMenuPage::Skins },
			FSection{ TEXT("tab:weapons"), TEXT("WEAPONS"), ESenniteMenuPage::Weapons }, FSection{ TEXT("tab:swap"), TEXT("SWAP"), ESenniteMenuPage::Swap } }) {
		Button(Each.Action, Each.Name, X, 24.0f, 160.0f, 58.0f, true, 16, Menu->Page == Each.Page);
		if (Menu->Page == Each.Page) {
			Drawing.Add(FString::Printf(TEXT("section=%s"), Each.Name));
		}
		X += 168.0f;
	}
	// top right, where things are sold: what the player's wallet holds, as it proved it
	const USennitePublisher* Seller = Shop(Menu);
	if (Seller != nullptr && Seller->IsOn()) {
		bool bKnown = false;
		const FString Holds = Seller->DBucksLine(bKnown);
		SenniteUi::Balance(Canvas, FVector2D((W - 24.0f) * S, 24.0f * S), S, Holds, bKnown);
		Drawing.Add(FString::Printf(TEXT("balance=%s"), *Holds));
	}
}

bool ASenniteMenuHUD::NextStep(const USennitePublisher* Server, const FForSale& Thing, float Left, float Y, float Wide)
{
	const float Under = (Y + 80.0f) * S;
	FString Hint;
	FString Hint2;
	if (Server == nullptr || !Server->IsOn()) {
		Button(Thing.Buy, TEXT("NO SHOP ON THIS GAME"), Left, Y, Wide, 66.0f, false, 14);
		Hint = TEXT("THIS GAME IS NOT CONNECTED TO THE PUBLISHER'S");
		Hint2 = FString::Printf(TEXT("SERVER, WHERE %s ARE SOLD FOR D-BUCKS."), Thing.Kind);
	}
	else if (!Server->bConnected) {
		Button(TEXT("wallet"), TEXT("CONNECT YOUR WALLET"), Left, Y, Wide, 66.0f, !Server->ConnectCode.IsEmpty(), 16, true);
		Hint = FString::Printf(TEXT("%s ARE BOUGHT WITH D-BUCKS FROM YOUR DSM WALLET."), Thing.Kind);
#if PLATFORM_ANDROID || PLATFORM_IOS
		Hint2 = TEXT("THIS OPENS YOUR WALLET: APPROVE THIS GAME THERE.");
#else
		Hint2 = TEXT("THIS COPIES THE CONNECT CODE FOR YOUR WALLET TO READ.");
#endif
	}
	else if (Thing.bOnItsWay) {
		Button(Thing.Buy, TEXT("ON ITS WAY TO YOUR WALLET"), Left, Y, Wide, 66.0f, false, 14);
	}
	else if (Server->bBusy) {
		Button(Thing.Buy, TEXT("WAITING FOR YOUR WALLET ..."), Left, Y, Wide, 66.0f, false, 14);
	}
	else if (!Server->bMaterialsKnown) {
		Button(Thing.Buy, TEXT("ASKING YOUR WALLET ..."), Left, Y, Wide, 66.0f, false, 14);
	}
	else if (!Thing.bListed) {
		Button(Thing.Buy, TEXT("NOT IN THE SHOP"), Left, Y, Wide, 66.0f, false, 14);
	}
	else if (Server->DBucks >= Thing.Price) {
		Button(Thing.Buy, FString::Printf(TEXT("BUY  %s D-BUCKS"), *SenniteUi::Grouped(Thing.Price)), Left, Y, Wide, 66.0f, true, 18, true);
	}
	else {
		// too few D-bucks: a trade of ERA from the wallet brings more, here at one tap; the SWAP section trades any amount
		const bool bEra = FCString::Atod(*Server->Era) >= FCString::Atod(USennitePublisher::TradeEra());
		Button(TEXT("dbucks"), TEXT("GET D-BUCKS"), Left, Y, Wide, 66.0f, bEra, 18, true);
		Hint = FString::Printf(TEXT("YOUR WALLET HAS %s OF THE %s D-BUCKS IT COSTS."), *SenniteUi::Grouped(Server->DBucks), *SenniteUi::Grouped(Thing.Price));
		Hint2 = bEra ? FString::Printf(TEXT("THIS TRADES %s ERA FROM YOUR WALLET FOR D-BUCKS."), USennitePublisher::TradeEra())
			: FString::Printf(TEXT("A TRADE FOR D-BUCKS TAKES %s ERA: YOUR WALLET HAS %s."), USennitePublisher::TradeEra(), Server->Era.IsEmpty() ? TEXT("NONE") : *Server->Era);
	}
	if (!Hint.IsEmpty()) {
		// large enough to read on a phone held at arm's length
		Text(TEXT("hint"), Hint, (Left + 2.0f) * S, Under, 12, FLinearColor::White, 0.0f, false);
		Text(TEXT("hint2"), Hint2, (Left + 2.0f) * S, Under + 24.0f * S, 12, FLinearColor::White, 0.0f, false);
	}
	return !Hint.IsEmpty();
}

void ASenniteMenuHUD::ShopWord(const USennitePublisher* Server, bool bOnItsWay, float X, float Y)
{
	if (Server != nullptr && Server->IsOn() && !Server->Message.IsEmpty() && (Server->bBusy || bOnItsWay)) {
		Text(TEXT("shop"), Server->Message.ToUpper(), X, Y, 12, SenniteUi::Gold, 0.0f, false);
	}
}

void ASenniteMenuHUD::DrawSkins(ASenniteMenuController* Menu)
{
	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	const FLinearColor White = FLinearColor::White;
	// as the heroes' section: the skin's name and what is done with it on a shade at the left, the rows of
	// heroes and skins on one that rises from the bottom
	const FBox2D Whole = Screen();
	SenniteUi::Fade(Canvas, FBox2D(Whole.Min, FVector2D(600.0f * S, Whole.Max.Y)), FLinearColor(0.0f, 0.0f, 0.0f, 0.66f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), false);
	SenniteUi::Fade(Canvas, FBox2D(FVector2D(Whole.Min.X, (H - 250.0f) * S), Whole.Max), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), true);
	StoreTop(Menu);

	const float Left = 48.0f;
	const float BW = 340.0f;
	const float BY = 304.0f;
	const USennitePublisher* Server = Shop(Menu);
	const bool bShop = Server != nullptr && Server->IsOn();
	if (!bShop || !Server->ShopSkins.IsValidIndex(Menu->BrowseSkin)) {
		// no shop on this game, or a shop that has listed no skin: nothing is shown as if it were for sale
		Text(TEXT("name"), TEXT("SKINS"), Left * S, 142.0f * S, 54, White);
		SenniteUi::Plate(Canvas, FBox2D(FVector2D(Left * S, 226.0f * S), FVector2D((Left + 96.0f) * S, 229.0f * S)), 0.0f, SenniteUi::Gold, SenniteUi::Gold);
		if (!bShop) {
			FForSale Thing;
			Thing.Kind = TEXT("SKINS");
			Thing.Buy = TEXT("buyskin");
			NextStep(Server, Thing, Left, BY, BW);
		}
		else {
			Text(TEXT("status"), TEXT("NO SKINS FROM THE SHOP YET"), (Left + 2.0f) * S, 254.0f * S, 13, SenniteUi::Gold);
		}
		return;
	}

	const TArray<FSenniteSkinForSale>& Skins = Server->ShopSkins;
	const FSenniteSkinForSale& Skin = Skins[Menu->BrowseSkin];
	const FSenniteHero* Of = SkinHero(Skin.Id);
	const bool bOwned = Server->bConnected && Server->Owns(Skin.Id);
	const bool bWorn = bOwned && Server->Wearing == Skin.Id;
	Text(TEXT("count"), Of != nullptr ? FString::Printf(TEXT("SKIN %d OF %d   FOR %s"), Menu->BrowseSkin + 1, Skins.Num(), *Of->Name) : FString::Printf(TEXT("SKIN %d OF %d"), Menu->BrowseSkin + 1, Skins.Num()),
		Left * S, 122.0f * S, 12, SenniteUi::Gold);
	const FString Name = Skin.Name.ToUpper();
	const float Room = 440.0f * S;
	const float NamePoints = FMath::Min(54.0f, 54.0f * Room / FMath::Max(1.0f, Width(Name, 54)));
	Text(TEXT("name"), Name, Left * S, 142.0f * S, NamePoints, White);
	SenniteUi::Plate(Canvas, FBox2D(FVector2D(Left * S, 226.0f * S), FVector2D((Left + 96.0f) * S, 229.0f * S)), 0.0f, SenniteUi::Gold, SenniteUi::Gold);

	// how the skin is had, on a small plate under its name
	{
		const FString Status = bWorn ? FString(TEXT("WORN")) : bOwned ? FString(TEXT("YOURS")) : FString::Printf(TEXT("%s D-BUCKS"), *SenniteUi::Grouped(Skin.Price));
		const float Wide = Width(Status, 13) + 36.0f * S;
		const FBox2D Tag(FVector2D(Left * S, 246.0f * S), FVector2D(Left * S + Wide, 280.0f * S));
		SenniteUi::Plate(Canvas, Tag, 7.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		SenniteUi::Outline(Canvas, Tag, 7.0f * S, bOwned ? SenniteUi::Edge : SenniteUi::Gold.CopyWithNewOpacity(0.8f), FMath::Max(1.0f, 1.5f * S));
		Text(TEXT("status"), Status, Left * S + 18.0f * S, 254.0f * S, 13, bOwned ? White : SenniteUi::Gold);
	}

	// what is done with it: worn or taken off once it is the wallet's, and until then the next thing toward having it
	bool bHint = false;
	if (bOwned) {
		Button(bWorn ? TEXT("takeoff") : TEXT("wear"), bWorn ? TEXT("TAKE OFF") : TEXT("WEAR"), Left, BY, BW, 66.0f, true, 19, !bWorn);
		if (Of != nullptr) {
			Text(TEXT("hint"), bWorn ? FString::Printf(TEXT("%s WEARS IT IN PLAY WHILE YOUR WALLET HOLDS IT."), *Of->Name) : FString::Printf(TEXT("WORN, %s PLAYS IN IT."), *Of->Name),
				(Left + 2.0f) * S, (BY + 80.0f) * S, 12, White, 0.0f, false);
			bHint = true;
		}
	}
	else {
		FForSale Thing;
		Thing.Kind = TEXT("SKINS");
		Thing.Buy = TEXT("buyskin");
		Thing.bListed = true;
		Thing.Price = Skin.Price;
		Thing.bOnItsWay = Server->OnItsWay == Skin.Id;
		bHint = NextStep(Server, Thing, Left, BY, BW);
	}
	ShopWord(Server, !Server->OnItsWay.IsEmpty(), (Left + 2.0f) * S, (BY + 80.0f + (bHint ? 52.0f : 0.0f)) * S);

	// the arrows, either side of the hero wearing it
	const float HeroX = W * 0.67f;
	Button(TEXT("prev"), TEXT("<"), HeroX - 330.0f, H * 0.5f - 76.0f, 76.0f, 116.0f, true, 34, false, -1);
	Button(TEXT("next"), TEXT(">"), FMath::Min(HeroX + 254.0f, W - 100.0f), H * 0.5f - 76.0f, 76.0f, 116.0f, true, 34, false, 1);

	// Along the bottom, two rows. The heroes that have skins in the shop, the one whose skins these are in
	// gold; under them that hero's skins, each on its own tile: the one shown in gold, the one worn marked,
	// and one not owned faint.
	const FName Showing = Of != nullptr ? Of->Id : FName();
	TArray<FName> Wearers;
	TArray<int32> Theirs;
	for (int32 Index = 0; Index < Skins.Num(); ++Index) {
		const FSenniteHero* Wearer = SkinHero(Skins[Index].Id);
		const FName Id = Wearer != nullptr ? Wearer->Id : FName();
		Wearers.AddUnique(Id);
		if (Id == Showing) {
			Theirs.Add(Index);
		}
	}
	const float Gap = 8.0f;
	if (Wearers.Num() > 1) {
		const float ChipWide = FMath::Min(170.0f, (W - 2.0f * Left - Gap * (Wearers.Num() - 1)) / Wearers.Num());
		for (int32 Index = 0; Index < Wearers.Num(); ++Index) {
			const FSenniteHero* Wearer = USenniteHeroes::Find(Wearers[Index]);
			const FString Label = Wearer != nullptr ? Wearer->Name : FString(TEXT("OTHER"));
			Button(FName(*FString::Printf(TEXT("skinsof:%s"), *Wearers[Index].ToString())), Label, Left + Index * (ChipWide + Gap), H - 142.0f, ChipWide, 44.0f, true,
				FMath::Min(13.0f, 13.0f * (ChipWide - 22.0f) * S / FMath::Max(1.0f, Width(Label, 13))), Wearers[Index] == Showing);
		}
	}
	const float TileWide = FMath::Min(240.0f, (W - 2.0f * Left - Gap * (Theirs.Num() - 1)) / FMath::Max(1, Theirs.Num()));
	const float TileHigh = 56.0f;
	const float TileTop = H - 82.0f;
	for (int32 Place = 0; Place < Theirs.Num(); ++Place) {
		const int32 Index = Theirs[Place];
		const FSenniteSkinForSale& Each = Skins[Index];
		const FName Action(*(FString(SkinOnStage) + Each.Id));
		const float X = Left + Place * (TileWide + Gap);
		const FBox2D Box(FVector2D(X * S, TileTop * S), FVector2D((X + TileWide) * S, (TileTop + TileHigh) * S));
		const bool bHad = Server->bConnected && Server->Owns(Each.Id);
		const bool bOn = bHad && Server->Wearing == Each.Id;
		const SenniteUi::EButton State = Action == Held ? SenniteUi::EButton::Pressed
			: Index == Menu->BrowseSkin ? SenniteUi::EButton::Primary
			: Action == Hovered ? SenniteUi::EButton::Hover : SenniteUi::EButton::Plain;
		FLinearColor Ink = SenniteUi::ButtonPlate(Canvas, Box, S, State);
		if (!bHad && Index != Menu->BrowseSkin) {
			Ink = FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);
		}
		// the tile says which skin: the hero's name, which the row over it says, is left off
		FString Label = Each.Name.ToUpper();
		if (Of != nullptr && Label.StartsWith(Of->Name + TEXT(" "))) {
			Label.RightChopInline(Of->Name.Len() + 1);
		}
		const float Points = FMath::Min(13.0f, 13.0f * (TileWide - 22.0f) * S / FMath::Max(1.0f, Width(Label, 13)));
		const float High = SenniteUi::Measure(Label, Points, S).Y;
		SenniteUi::Text(Canvas, Label, Box.GetCenter().X, Box.GetCenter().Y - High * 0.5f, Points, S, Ink, 0.5f);
		if (bOn) {
			// the skin worn: a gold line over its tile
			SenniteUi::Plate(Canvas, FBox2D(FVector2D(Box.Min.X + 14.0f * S, Box.Min.Y - 7.0f * S), FVector2D(Box.Max.X, Box.Min.Y - 4.0f * S)), 0.0f, SenniteUi::Gold, SenniteUi::Gold);
		}
		Drawing.Add(FString::Printf(TEXT("tile%d=%s%s%s"), Place, *Label, bOn ? TEXT("  WORN") : TEXT(""), bHad ? TEXT("") : TEXT("  NOT HAD")));
		FSenniteMenuButton Tile;
		Tile.Action = Action;
		Tile.Text = Label;
		Tile.Box = Box.ShiftBy(Inset);
		Buttons.Add(Tile);
	}
}

void ASenniteMenuHUD::DrawWeapons(ASenniteMenuController* Menu)
{
	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	const FLinearColor White = FLinearColor::White;
	// as the heroes' section: the gun's name and what is done with it on a shade at the left, the row of guns
	// on one that rises from the bottom
	const FBox2D Whole = Screen();
	SenniteUi::Fade(Canvas, FBox2D(Whole.Min, FVector2D(600.0f * S, Whole.Max.Y)), FLinearColor(0.0f, 0.0f, 0.0f, 0.66f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), false);
	SenniteUi::Fade(Canvas, FBox2D(FVector2D(Whole.Min.X, (H - 190.0f) * S), Whole.Max), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.7f), true);
	StoreTop(Menu);

	const float Left = 48.0f;
	const float BW = 340.0f;
	const float BY = 304.0f;
	const USennitePublisher* Server = Shop(Menu);
	const bool bShop = Server != nullptr && Server->IsOn();
	if (!bShop || !Server->ShopWeapons.IsValidIndex(Menu->BrowseWeapon)) {
		// no shop on this game, or a shop that has listed no gun: nothing is shown as if it were for sale
		Text(TEXT("name"), TEXT("WEAPONS"), Left * S, 142.0f * S, 54, White);
		SenniteUi::Plate(Canvas, FBox2D(FVector2D(Left * S, 226.0f * S), FVector2D((Left + 96.0f) * S, 229.0f * S)), 0.0f, SenniteUi::Gold, SenniteUi::Gold);
		if (!bShop) {
			FForSale Thing;
			Thing.Kind = TEXT("GUNS");
			Thing.Buy = TEXT("buygun");
			NextStep(Server, Thing, Left, BY, BW);
		}
		else {
			Text(TEXT("status"), TEXT("NO GUNS FROM THE SHOP YET"), (Left + 2.0f) * S, 254.0f * S, 13, SenniteUi::Gold);
		}
		Text(TEXT("free"), TEXT("THE RIFLE AND THE SHOTGUN ARE FREE: EVERY CRATE GIVES THEM."), (Left + 2.0f) * S, (H - 112.0f) * S, 12, White, 0.0f, false);
		return;
	}

	const TArray<FSenniteHeroForSale>& Guns = Server->ShopWeapons;
	const FSenniteHeroForSale& Gun = Guns[Menu->BrowseWeapon];
	const AWeaponActor* Known = ShopGun(Gun.Id);
	const bool bOwned = Server->bConnected && Server->OwnsWeapon(Gun.Id);
	Text(TEXT("count"), FString::Printf(TEXT("GUN %d OF %d"), Menu->BrowseWeapon + 1, Guns.Num()), Left * S, 122.0f * S, 12, SenniteUi::Gold);
	// the name the game calls it in play, where it is one of the game's guns
	const FString Name = Known != nullptr && !Known->DisplayName.IsEmpty() ? Known->DisplayName : Gun.Name.ToUpper();
	const float Room = 440.0f * S;
	const float NamePoints = FMath::Min(54.0f, 54.0f * Room / FMath::Max(1.0f, Width(Name, 54)));
	Text(TEXT("name"), Name, Left * S, 142.0f * S, NamePoints, White);
	SenniteUi::Plate(Canvas, FBox2D(FVector2D(Left * S, 226.0f * S), FVector2D((Left + 96.0f) * S, 229.0f * S)), 0.0f, SenniteUi::Gold, SenniteUi::Gold);

	// how the gun is had, on a small plate under its name
	{
		const FString Status = bOwned ? FString(TEXT("YOURS")) : FString::Printf(TEXT("%s D-BUCKS"), *SenniteUi::Grouped(Gun.Price));
		const float Wide = Width(Status, 13) + 36.0f * S;
		const FBox2D Tag(FVector2D(Left * S, 246.0f * S), FVector2D(Left * S + Wide, 280.0f * S));
		SenniteUi::Plate(Canvas, Tag, 7.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		SenniteUi::Outline(Canvas, Tag, 7.0f * S, bOwned ? SenniteUi::Edge : SenniteUi::Gold.CopyWithNewOpacity(0.8f), FMath::Max(1.0f, 1.5f * S));
		Text(TEXT("status"), Status, Left * S + 18.0f * S, 254.0f * S, 13, bOwned ? White : SenniteUi::Gold);
	}

	// bought once, it is the wallet's; until then, the next thing toward having it
	bool bHint = false;
	if (bOwned) {
		Button(TEXT("bought"), TEXT("BOUGHT"), Left, BY, BW, 66.0f, false);
		Text(TEXT("hint"), TEXT("CRATES GIVE IT TO YOU IN EVERY GAME, WHILE YOUR WALLET HOLDS IT."), (Left + 2.0f) * S, (BY + 80.0f) * S, 12, White, 0.0f, false);
	}
	else {
		FForSale Thing;
		Thing.Kind = TEXT("GUNS");
		Thing.Buy = TEXT("buygun");
		Thing.bListed = true;
		Thing.Price = Gun.Price;
		Thing.bOnItsWay = Server->HeroOnItsWay == Gun.Id;
		bHint = NextStep(Server, Thing, Left, BY, BW);
	}
	ShopWord(Server, !Server->HeroOnItsWay.IsEmpty(), (Left + 2.0f) * S, (BY + 80.0f + (bHint ? 52.0f : bOwned ? 26.0f : 0.0f)) * S);

	// which guns are bought and which are not
	Text(TEXT("free"), TEXT("THE RIFLE AND THE SHOTGUN ARE FREE: EVERY CRATE GIVES THEM. A GUN SOLD HERE COMES OUT OF A CRATE ONLY FOR ITS OWNERS."),
		(Left + 2.0f) * S, (H - 112.0f) * S, 12, White, 0.0f, false);

	// the arrows, either side of the gun
	const float HeroX = W * 0.67f;
	Button(TEXT("prev"), TEXT("<"), HeroX - 330.0f, H * 0.5f - 76.0f, 76.0f, 116.0f, true, 34, false, -1);
	Button(TEXT("next"), TEXT(">"), FMath::Min(HeroX + 254.0f, W - 100.0f), H * 0.5f - 76.0f, 76.0f, 116.0f, true, 34, false, 1);

	// every gun along the bottom, each on its own tile: the one shown in gold, one not owned faint
	const float Gap = 8.0f;
	const float TileWide = FMath::Min(240.0f, (W - 2.0f * Left - Gap * (Guns.Num() - 1)) / FMath::Max(1, Guns.Num()));
	const float TileHigh = 56.0f;
	const float TileTop = H - 82.0f;
	for (int32 Index = 0; Index < Guns.Num(); ++Index) {
		const FSenniteHeroForSale& Each = Guns[Index];
		const FName Action(*(FString(TEXT("gun:")) + Each.Id));
		const float X = Left + Index * (TileWide + Gap);
		const FBox2D Box(FVector2D(X * S, TileTop * S), FVector2D((X + TileWide) * S, (TileTop + TileHigh) * S));
		const bool bHad = Server->bConnected && Server->OwnsWeapon(Each.Id);
		const SenniteUi::EButton State = Action == Held ? SenniteUi::EButton::Pressed
			: Index == Menu->BrowseWeapon ? SenniteUi::EButton::Primary
			: Action == Hovered ? SenniteUi::EButton::Hover : SenniteUi::EButton::Plain;
		FLinearColor Ink = SenniteUi::ButtonPlate(Canvas, Box, S, State);
		if (!bHad && Index != Menu->BrowseWeapon) {
			Ink = FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);
		}
		const AWeaponActor* Theirs = ShopGun(Each.Id);
		const FString Label = Theirs != nullptr && !Theirs->DisplayName.IsEmpty() ? Theirs->DisplayName : Each.Name.ToUpper();
		const float Points = FMath::Min(13.0f, 13.0f * (TileWide - 22.0f) * S / FMath::Max(1.0f, Width(Label, 13)));
		const float High = SenniteUi::Measure(Label, Points, S).Y;
		SenniteUi::Text(Canvas, Label, Box.GetCenter().X, Box.GetCenter().Y - High * 0.5f, Points, S, Ink, 0.5f);
		Drawing.Add(FString::Printf(TEXT("tile%d=%s%s"), Index, *Label, bHad ? TEXT("  YOURS") : TEXT("  NOT HAD")));
		FSenniteMenuButton Tile;
		Tile.Action = Action;
		Tile.Text = Label;
		Tile.Box = Box.ShiftBy(Inset);
		Buttons.Add(Tile);
	}
}

void ASenniteMenuHUD::DrawSwap(ASenniteMenuController* Menu)
{
	const float W = Canvas->ClipX / S;
	const FLinearColor White = FLinearColor::White;
	// the whole screen is shaded, as where an address is typed: the keypad is read over the stage
	SenniteUi::Fade(Canvas, Screen(), FLinearColor(0.0f, 0.0f, 0.0f, 0.66f), FLinearColor(0.0f, 0.0f, 0.0f, 0.4f), false);
	StoreTop(Menu);

	const float Left = 48.0f;
	const float Wide = 542.0f;
	const USennitePublisher* Server = Shop(Menu);
	Text(TEXT("count"), TEXT("SWAP THROUGH THE PUBLISHER'S VAULTS"), Left * S, 102.0f * S, 12, SenniteUi::Gold);

	const FSenniteSwapToken* From = nullptr;
	const FSenniteSwapToken* To = nullptr;
	if (Server == nullptr || !Server->IsOn()) {
		Button(TEXT("price"), TEXT("NO SHOP ON THIS GAME"), Left, 150.0f, 340.0f, 66.0f, false, 14);
		Text(TEXT("hint"), TEXT("THIS GAME IS NOT CONNECTED TO THE PUBLISHER'S"), (Left + 2.0f) * S, 230.0f * S, 12, White, 0.0f, false);
		Text(TEXT("hint2"), TEXT("SERVER, WHOSE VAULTS SWAPS GO THROUGH."), (Left + 2.0f) * S, 254.0f * S, 12, White, 0.0f, false);
		return;
	}
	if (!Menu->SwapSides(Server, From, To)) {
		// nothing is offered that the vaults do not hold
		Text(TEXT("status"), Server->bSwapKnown ? TEXT("THE PUBLISHER HAS NO VAULT OPEN: NOTHING CAN BE SWAPPED NOW") : TEXT("ASKING WHAT CAN BE SWAPPED ..."), (Left + 2.0f) * S, 150.0f * S, 13, SenniteUi::Gold);
		return;
	}

	const auto Field = [this](float X, float Y, float Width, float Height) {
		const FBox2D Box(FVector2D(X * S, Y * S), FVector2D((X + Width) * S, (Y + Height) * S));
		SenniteUi::Plate(Canvas, Box, 10.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.7f), FLinearColor(0.02f, 0.03f, 0.05f, 0.7f));
		SenniteUi::Outline(Canvas, Box, 10.0f * S, SenniteUi::Gold.CopyWithNewOpacity(0.85f), FMath::Max(1.0f, 1.5f * S));
	};
	const FString Paid = From->Name.ToUpper();
	const FString Got = To->Name.ToUpper();
	const bool bAmount = Menu->SwapAmountIsOne();
	const bool bPriced = bAmount && Server->PricedFor(From->Id, To->Id, Menu->SwapAmount);

	// what is paid: which thing, and how much of it, typed on the keypad
	Text(TEXT("paylabel"), TEXT("YOU PAY"), (Left + 2.0f) * S, 128.0f * S, 11, SenniteUi::Gold);
	Button(TEXT("swap:from"), Paid, Left, 148.0f, 210.0f, 64.0f, true, 17, false, 1);
	Field(Left + 222.0f, 148.0f, Wide - 222.0f, 64.0f);
	const bool bBlink = FMath::Fmod(GetWorld()->GetRealTimeSeconds(), 1.0) < 0.5;
	Text(TEXT("amount"), Menu->SwapAmount + (bBlink ? TEXT("_") : TEXT(" ")), (Left + 242.0f) * S, 162.0f * S, 24, White);
	// the amount without the blinking mark, for a check to read
	Drawing.Pop();
	Drawing.Add(FString::Printf(TEXT("amount=%s"), *Menu->SwapAmount));
	Drawing.Add(FString::Printf(TEXT("pay=%s"), *Paid));
	{
		const FString Holds = Server->HoldsOf(*From);
		bool bKnown = false;
		const FString Why = Server->DBucksLine(bKnown);
		Text(TEXT("holds"), !Holds.IsEmpty() ? FString::Printf(TEXT("YOUR WALLET HAS %s %s"), *Holds, *Paid) : bKnown ? FString::Printf(TEXT("YOUR WALLET HAS PROVED NO %s"), *Paid) : Why,
			(Left + 2.0f) * S, 220.0f * S, 12, White, 0.0f, false);
	}

	// the two change places
	Button(TEXT("swap:flip"), TEXT("SWITCH"), Left, 250.0f, 210.0f, 46.0f, true, 14);

	// what is got: which thing, and how much the wallet said the vaults would give
	Text(TEXT("getlabel"), TEXT("YOU GET"), (Left + 2.0f) * S, 312.0f * S, 11, SenniteUi::Gold);
	Button(TEXT("swap:to"), Got, Left, 332.0f, 210.0f, 64.0f, true, 17, false, 1);
	Field(Left + 222.0f, 332.0f, Wide - 222.0f, 64.0f);
	Drawing.Add(FString::Printf(TEXT("get=%s"), *Got));
	if (bPriced) {
		Text(TEXT("gets"), Server->SwapPrice.AmountOut, (Left + 242.0f) * S, 346.0f * S, 24, White);
		Text(TEXT("least"), FString::Printf(TEXT("THE PRICE NOW, THROUGH %d VAULT%s. THE SWAP TAKES NO LESS THAN 97%% OF IT."), Server->SwapPrice.Hops, Server->SwapPrice.Hops == 1 ? TEXT("") : TEXT("S")),
			(Left + 2.0f) * S, 404.0f * S, 12, White, 0.0f, false);
	}
	else {
		Text(TEXT("unpriced"), TEXT("ASK THE PRICE"), (Left + 242.0f) * S, 352.0f * S, 14, SenniteUi::Dim);
	}

	// the price, then the swap at it; with no wallet connected, the way to connect one
	const float BY = 440.0f;
	if (!Server->bConnected) {
		Button(TEXT("wallet"), TEXT("CONNECT YOUR WALLET"), Left, BY, 340.0f, 66.0f, !Server->ConnectCode.IsEmpty(), 16, true);
		Text(TEXT("hint"), TEXT("A SWAP IS YOUR DSM WALLET'S OWN TRADE."), (Left + 2.0f) * S, (BY + 80.0f) * S, 12, White, 0.0f, false);
#if PLATFORM_ANDROID || PLATFORM_IOS
		Text(TEXT("hint2"), TEXT("THIS OPENS YOUR WALLET: APPROVE THIS GAME THERE."), (Left + 2.0f) * S, (BY + 104.0f) * S, 12, White, 0.0f, false);
#else
		Text(TEXT("hint2"), TEXT("THIS COPIES THE CONNECT CODE FOR YOUR WALLET TO READ."), (Left + 2.0f) * S, (BY + 104.0f) * S, 12, White, 0.0f, false);
#endif
	}
	else {
		Button(TEXT("price"), TEXT("PRICE"), Left, BY, 210.0f, 66.0f, bAmount && !Server->bBusy, 18, !bPriced);
		Button(TEXT("trade"), Server->bBusy ? TEXT("WAITING FOR YOUR WALLET ...") : TEXT("SWAP"), Left + 222.0f, BY, Wide - 222.0f, 66.0f, bPriced && !Server->bBusy, Server->bBusy ? 13 : 18, bPriced);
		// the vault that trades the two directly, as it stands
		if (const FSenniteSwapVault* Vault = Server->FindSwapVault(From->Id, To->Id)) {
			const FSenniteSwapToken* A = Server->FindSwapToken(Vault->A);
			const FSenniteSwapToken* B = Server->FindSwapToken(Vault->B);
			Text(TEXT("vault"), FString::Printf(TEXT("THE VAULT HOLDS %s %s AND %s %s. ITS FEE IS %s%%."), *Vault->ReserveA, A != nullptr ? *A->Name.ToUpper() : TEXT(""), *Vault->ReserveB, B != nullptr ? *B->Name.ToUpper() : TEXT(""),
				*FString::SanitizeFloat(Vault->FeeBps / 100.0, 0)), (Left + 2.0f) * S, (BY + 80.0f) * S, 12, SenniteUi::Dim, 0.0f, false);
		}
		else {
			Text(TEXT("vault"), TEXT("NO ONE VAULT HOLDS BOTH: THE PRICE SAYS WHETHER OTHERS LEAD FROM ONE TO THE OTHER."), (Left + 2.0f) * S, (BY + 80.0f) * S, 12, SenniteUi::Dim, 0.0f, false);
		}
		// what the publisher's server says of the price or the swap asked here: waiting, done, or why not
		if (!Server->Message.IsEmpty() && (Server->bBusy || Menu->bSwapSaid)) {
			Text(TEXT("shop"), Server->Message.ToUpper(), (Left + 2.0f) * S, (BY + 104.0f) * S, 12, SenniteUi::Gold, 0.0f, false);
		}
	}

	// the keypad: what an amount is typed with on a phone. The point is for what has digits after it.
	static const TCHAR* Keys[] = { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"), TEXT("8"), TEXT("9"), TEXT("."), TEXT("0"), TEXT("del") };
	const float KW = 104.0f;
	const float KH = 86.0f;
	const float Gap = 10.0f;
	const float X0 = W - 36.0f - 3.0f * KW - 2.0f * Gap;
	const float Y0 = 120.0f;
	for (int32 Index = 0; Index < 12; ++Index) {
		const FString Key(Keys[Index]);
		const bool bDelete = Key == TEXT("del");
		const bool bEnabled = bDelete ? !Menu->SwapAmount.IsEmpty() : Key == TEXT(".") ? From->Decimals > 0 : true;
		Button(FName(*(TEXT("num:") + Key)), bDelete ? FString(TEXT("DEL")) : Key, X0 + (Index % 3) * (KW + Gap), Y0 + (Index / 3) * (KH + Gap), KW, KH, bEnabled, bDelete ? 16 : 28);
	}
}

void ASenniteMenuHUD::DrawJoin(ASenniteMenuController* Menu)
{
	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	const FLinearColor White = FLinearColor::White;
	SenniteUi::Fade(Canvas, Screen(), FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), false);
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
	const FBox2D Whole = Screen();
	SenniteUi::Fade(Canvas, FBox2D(Whole.Min, FVector2D(620.0f * S, Whole.Max.Y)), FLinearColor(0.0f, 0.0f, 0.0f, 0.62f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), false);
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
