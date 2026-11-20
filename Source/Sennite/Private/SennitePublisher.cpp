#include "SennitePublisher.h"
#include "SenniteUi.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJavaEnv.h"
#include "Android/AndroidJNI.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSennitePublisher, Log, All);

namespace
{
	/** Where the game keeps its token for the server between runs: it resumes the player's place, nothing more. */
	FString TokenFile()
	{
		// -PublisherPlayerFile= names another: two games on one machine are two players
		FString Named;
		if (FParse::Value(FCommandLine::Get(), TEXT("PublisherPlayerFile="), Named) && !Named.IsEmpty()) {
			return Named;
		}
		return FPaths::ProjectSavedDir() / TEXT("Publisher") / TEXT("player.json");
	}

	FString ErrorOf(int32 Status, const TSharedPtr<FJsonObject>& Json)
	{
		FString Error;
		if (Json.IsValid() && Json->TryGetStringField(TEXT("error"), Error)) {
			return Error;
		}
		return Status == 0 ? FString(TEXT("the publisher's server cannot be reached")) : FString::Printf(TEXT("the publisher's server answered %d"), Status);
	}

	/** A new instance_id: 16 random bytes as lowercase hex. */
	FString NewInstanceId()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
	}
}

void USennitePublisher::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString FromCommandLine;
	if (FParse::Value(FCommandLine::Get(), TEXT("PublisherUrl="), FromCommandLine)) {
		PublisherUrl = FromCommandLine;
	}
	while (PublisherUrl.EndsWith(TEXT("/"))) {
		PublisherUrl.LeftChopInline(1);
	}
	if (!IsOn()) {
		return;
	}
	FString SecretFile;
	if (FParse::Value(FCommandLine::Get(), TEXT("MatchSecretFile="), SecretFile)) {
		if (FFileHelper::LoadFileToString(MatchSecret, *SecretFile)) {
			MatchSecret.TrimStartAndEndInline();
		}
		if (MatchSecret.IsEmpty()) {
			UE_LOG(LogSennitePublisher, Error, TEXT("No match server's secret could be read from %s"), *SecretFile);
		}
	}
	// the token of an earlier run, if it was for this server
	FString Saved;
	TSharedPtr<FJsonObject> Json;
	if (FFileHelper::LoadFileToString(Saved, *TokenFile()) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Saved), Json) && Json.IsValid()) {
		if (Json->GetStringField(TEXT("url")) == PublisherUrl) {
			Token = Json->GetStringField(TEXT("player"));
			ConnectCode = Json->GetStringField(TEXT("code"));
			Json->TryGetStringField(TEXT("wearing"), Wearing);
		}
	}
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USennitePublisher::Tick), 0.1f);
	UE_LOG(LogSennitePublisher, Display, TEXT("The publisher's server is %s"), *PublisherUrl);
}

void USennitePublisher::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

const FSennitePrefab* USennitePublisher::FindPrefab(const FString& Id) const
{
	return Prefabs.FindByPredicate([&Id](const FSennitePrefab& P) { return P.Id == Id; });
}

const FSenniteParcel* USennitePublisher::FindParcel(const FString& Id) const
{
	return Parcels.FindByPredicate([&Id](const FSenniteParcel& P) { return P.Id == Id; });
}

void USennitePublisher::AskMatchBases(const FString& MatchId, TFunction<void(bool, const FString&, const TArray<FSenniteMatchBaseView>&)> Done)
{
	Send(TEXT("GET"), FString::Printf(TEXT("/matches/%s/bases"), *MatchId), nullptr, [Done = MoveTemp(Done)](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		TArray<FSenniteMatchBaseView> Bases;
		const TArray<TSharedPtr<FJsonValue>>* Listed = nullptr;
		if (Status != 200 || !Json.IsValid() || !Json->TryGetArrayField(TEXT("bases"), Listed)) {
			Done(false, ErrorOf(Status, Json), Bases);
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Listed) {
			const TSharedPtr<FJsonObject> Entry = Value->AsObject();
			if (!Entry.IsValid()) {
				continue;
			}
			FSenniteMatchBaseView& Base = Bases.AddDefaulted_GetRef();
			Base.FactionId = Entry->GetStringField(TEXT("factionId"));
			Base.ParcelId = Entry->GetStringField(TEXT("parcelId"));
			const TArray<TSharedPtr<FJsonValue>>* Corners = nullptr;
			if (Entry->TryGetArrayField(TEXT("boundary"), Corners)) {
				for (const TSharedPtr<FJsonValue>& Corner : *Corners) {
					const TArray<TSharedPtr<FJsonValue>>& At = Corner->AsArray();
					if (At.Num() == 2) {
						Base.Boundary.Add(FIntPoint(static_cast<int32>(At[0]->AsNumber()), static_cast<int32>(At[1]->AsNumber())));
					}
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Built = nullptr;
			if (Entry->TryGetArrayField(TEXT("structures"), Built)) {
				for (const TSharedPtr<FJsonValue>& One : *Built) {
					const TSharedPtr<FJsonObject> Structure = One->AsObject();
					if (!Structure.IsValid()) {
						continue;
					}
					FSenniteMatchStructureView& View = Base.Structures.AddDefaulted_GetRef();
					View.InstanceId = Structure->GetStringField(TEXT("instanceId"));
					View.PrefabId = Structure->GetStringField(TEXT("prefabId"));
					View.X = Structure->GetIntegerField(TEXT("x"));
					View.Y = Structure->GetIntegerField(TEXT("y"));
					View.Rotation = Structure->GetIntegerField(TEXT("rotation"));
					View.Width = Structure->GetIntegerField(TEXT("width"));
					View.Depth = Structure->GetIntegerField(TEXT("depth"));
				}
			}
		}
		Done(true, FString(), Bases);
	}, MatchSecret);
}

void USennitePublisher::ReportDestroyed(const FString& InFactionId, const FString& InstanceId, TFunction<void(bool, const FString&)> Done)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("instanceId"), InstanceId);
	Send(TEXT("POST"), FString::Printf(TEXT("/factions/%s/destroyed"), *InFactionId), Body, [Done = MoveTemp(Done)](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		Done(Status == 200, Status == 200 ? FString() : ErrorOf(Status, Json));
	}, MatchSecret);
}

void USennitePublisher::AskMatchTicket(const FString& MatchId, TFunction<void(bool, const FString&, const FString&)> Done)
{
	Send(TEXT("POST"), FString::Printf(TEXT("/matches/%s/tickets"), *MatchId), MakeShared<FJsonObject>(), [Done = MoveTemp(Done)](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		FString Ticket;
		if (Status != 201 || !Json.IsValid() || !Json->TryGetStringField(TEXT("ticket"), Ticket)) {
			Done(false, ErrorOf(Status, Json), FString());
			return;
		}
		Done(true, FString(), Ticket);
	});
}

void USennitePublisher::EnterMatch(const FString& MatchId, const FString& Ticket, TFunction<void(bool, const FString&, const FString&, const FString&)> Done)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("ticket"), Ticket);
	Send(TEXT("POST"), FString::Printf(TEXT("/matches/%s/entries"), *MatchId), Body, [Done = MoveTemp(Done)](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		FString InFaction;
		FString InWallet;
		if (Status != 200 || !Json.IsValid() || !Json->TryGetStringField(TEXT("factionId"), InFaction) || !Json->TryGetStringField(TEXT("wallet"), InWallet)) {
			Done(false, ErrorOf(Status, Json), FString(), FString());
			return;
		}
		Done(true, FString(), InFaction, InWallet);
	}, MatchSecret);
}

void USennitePublisher::ReportOutcome(const FString& MatchId, const FString& Outcome, TFunction<void(bool, const FString&)> Done)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("outcome"), Outcome);
	// the publisher's key decides the prize, and the server answers once that is final: it is given time
	Send(TEXT("POST"), FString::Printf(TEXT("/matches/%s/outcome"), *MatchId), Body, [Done = MoveTemp(Done)](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		Done(Status == 200, Status == 200 ? FString() : ErrorOf(Status, Json));
	}, MatchSecret, 300.0f);
}

void USennitePublisher::Send(const TCHAR* Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, FAnswer Done, const FString& Bearer, float Patience)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(PublisherUrl + Path);
	Request->SetVerb(Verb);
	if (Patience > 0.0f) {
		Request->SetTimeout(Patience);
		Request->SetActivityTimeout(Patience);
	}
	const FString& Carries = Bearer.IsEmpty() ? Token : Bearer;
	if (!Carries.IsEmpty()) {
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Carries);
	}
	if (Body.IsValid()) {
		FString Text;
		FJsonSerializer::Serialize(Body.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(Text);
	}
	Request->OnProcessRequestComplete().BindWeakLambda(this, [this, Done = MoveTemp(Done)](FHttpRequestPtr, FHttpResponsePtr Response, bool bReached) {
		TSharedPtr<FJsonObject> Json;
		int32 Status = 0;
		const bool bWas = bReachable;
		bReachable = bReached && Response.IsValid();
		// the server answers again: what was said about not reaching it is no longer so
		if (bReachable && !bWas && !bBusy && Message == ErrorOf(0, nullptr)) {
			Message.Reset();
		}
		if (bReachable) {
			Status = Response->GetResponseCode();
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json);
		}
		Done(Status, Json);
	});
	Request->ProcessRequest();
}

bool USennitePublisher::Tick(float)
{
	// the publisher's match server is no player's game: it has no wallet to connect and no base to plan
	if (IsMatchServer()) {
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	if (!bWorld) {
		if (!bWorldAsked && Now >= NextWorld) {
			AskWorld();
		}
		return true;
	}
	if (Token.IsEmpty()) {
		if (!bPlayerAsked) {
			AskPlayer();
		}
		return true;
	}
	if (!bMeAsked && Now >= NextMe) {
		AskMe();
	}
	if (!bShop && !bShopAsked) {
		AskShop();
	}
	if (BasesAsked == 0 && Now >= NextBases) {
		AskBases();
	}
	// a skin the shop gave reaches the wallet on the wallet's own time: until its proof lists the skin, the game
	// asks again every few seconds, a bounded number of times
	if ((!OnItsWay.IsEmpty() || !HeroOnItsWay.IsEmpty()) && !ProofPending() && Now >= NextArrival) {
		if ((OnItsWay.IsEmpty() || Owns(OnItsWay)) && (HeroOnItsWay.IsEmpty() || OwnsHero(HeroOnItsWay))) {
			OnItsWay.Reset();
			HeroOnItsWay.Reset();
		}
		else if (ArrivalProofs >= 60) {
			OnItsWay.Reset();
			HeroOnItsWay.Reset();
		}
		else {
			++ArrivalProofs;
			NextArrival = Now + 5.0;
			bProofWanted = true;
		}
	}
	// the publisher's server answers a hero pass only from a proof less than an hour old: prove again every half hour
	if (bConnected && bMaterialsKnown && Now >= NextReprove) {
		if (NextReprove > 0.0) {
			bProofWanted = true;
		}
		NextReprove = Now + 1800.0;
	}
	// once the wallet is connected the game asks it, once, what it holds: the skin a player owns is on their character
	if (bConnected && !bProvedOnConnect) {
		bProvedOnConnect = true;
		bProofWanted = true;
	}
	if (bConnected && bProofWanted && !bProving) {
		StartProof();
	}
	if (!PendingProof.IsEmpty() && !bProofAsked && Now >= NextProof) {
		AskProof();
	}
	if (!PendingRequest.IsEmpty() && !bRequestAsked && Now >= NextRequest) {
		AskRequest();
	}
	if (bConnected && !FactionId.IsEmpty() && !bFreeAsked && Now >= NextFree) {
		AskFree();
	}
	// a match's prize goes to a faction's members: a wallet in no faction has none to ask about
	if (bConnected && !FactionId.IsEmpty() && !bPrizesAsked && Now >= NextPrizes) {
		AskPrizes();
	}
	return true;
}

void USennitePublisher::AskWorld()
{
	bWorldAsked = true;
	Send(TEXT("GET"), TEXT("/world"), nullptr, [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		bWorldAsked = false;
		if (Status == 200 && Json.IsValid()) {
			ReadWorld(*Json);
			return;
		}
		Message = ErrorOf(Status, Json);
		NextWorld = FPlatformTime::Seconds() + 2.0;
	});
}

void USennitePublisher::ReadWorld(const FJsonObject& Json)
{
	const TSharedPtr<FJsonObject>* Catalogue = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Json.TryGetObjectField(TEXT("catalogue"), Catalogue) || !Json.TryGetArrayField(TEXT("parcels"), List)) {
		Message = TEXT("the publisher's server sent no world");
		return;
	}
	Grid = (*Catalogue)->GetIntegerField(TEXT("grid"));
	Resources.Reset();
	(*Catalogue)->TryGetStringArrayField(TEXT("resources"), Resources);
	Prefabs.Reset();
	for (const TSharedPtr<FJsonValue>& Value : (*Catalogue)->GetArrayField(TEXT("prefabs"))) {
		const TSharedPtr<FJsonObject> Entry = Value->AsObject();
		FSennitePrefab Prefab;
		Prefab.Id = Entry->GetStringField(TEXT("id"));
		Prefab.Name = Entry->GetStringField(TEXT("name"));
		const TSharedPtr<FJsonObject> Footprint = Entry->GetObjectField(TEXT("footprint"));
		Prefab.Width = Footprint->GetIntegerField(TEXT("width"));
		Prefab.Depth = Footprint->GetIntegerField(TEXT("depth"));
		// in the catalogue's order of resources, so every game lists a recipe the same way
		const TSharedPtr<FJsonObject> Recipe = Entry->GetObjectField(TEXT("recipe"));
		for (const FString& Resource : Resources) {
			int32 Amount = 0;
			if (Recipe->TryGetNumberField(Resource, Amount)) {
				Prefab.Recipe.Emplace(Resource, Amount);
			}
		}
		Prefabs.Add(MoveTemp(Prefab));
	}
	Parcels.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *List) {
		const TSharedPtr<FJsonObject> Entry = Value->AsObject();
		FSenniteParcel Parcel;
		Parcel.Id = Entry->GetStringField(TEXT("id"));
		Parcel.FactionId = Entry->GetStringField(TEXT("factionId"));
		for (const TSharedPtr<FJsonValue>& Corner : Entry->GetArrayField(TEXT("boundary"))) {
			const TArray<TSharedPtr<FJsonValue>>& Pair = Corner->AsArray();
			if (Pair.Num() == 2) {
				Parcel.Boundary.Emplace(FMath::RoundToInt32(Pair[0]->AsNumber()), FMath::RoundToInt32(Pair[1]->AsNumber()));
			}
		}
		Parcels.Add(MoveTemp(Parcel));
	}
	bWorld = true;
	UE_LOG(LogSennitePublisher, Display, TEXT("The publisher's world: %d prefabs, %d parcels"), Prefabs.Num(), Parcels.Num());
}

void USennitePublisher::AskPlayer()
{
	bPlayerAsked = true;
	Send(TEXT("POST"), TEXT("/players"), nullptr, [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		bPlayerAsked = false;
		if (Status != 201 || !Json.IsValid()) {
			Message = ErrorOf(Status, Json);
			return;
		}
		Token = Json->GetStringField(TEXT("player"));
		ConnectCode = Json->GetStringField(TEXT("code"));
		// a new player at the server has chosen nothing to wear yet
		Wearing.Reset();
		SaveToken();
	});
}

void USennitePublisher::SaveToken() const
{
	const TSharedRef<FJsonObject> Keep = MakeShared<FJsonObject>();
	Keep->SetStringField(TEXT("url"), PublisherUrl);
	Keep->SetStringField(TEXT("player"), Token);
	Keep->SetStringField(TEXT("code"), ConnectCode);
	Keep->SetStringField(TEXT("wearing"), Wearing);
	FString Text;
	FJsonSerializer::Serialize(Keep, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *TokenFile());
}

void USennitePublisher::RefreshShop()
{
	if (IsOn() && !bShopAsked) {
		AskShop();
	}
}

void USennitePublisher::AskShop()
{
	bShopAsked = true;
	Send(TEXT("GET"), TEXT("/shop"), nullptr, [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		bShopAsked = false;
		const TArray<TSharedPtr<FJsonValue>>* Skins = nullptr;
		if (Status != 200 || !Json.IsValid() || !Json->TryGetArrayField(TEXT("skins"), Skins)) {
			return;
		}
		ShopSkins.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *Skins) {
			const TSharedPtr<FJsonObject> One = Value->AsObject();
			FSenniteSkinForSale Skin;
			Skin.Id = One->GetStringField(TEXT("id"));
			Skin.Name = One->GetStringField(TEXT("name"));
			Skin.Price = FCString::Atoi(*One->GetStringField(TEXT("price")));
			ShopSkins.Add(MoveTemp(Skin));
		}
		const TSharedPtr<FJsonObject>* Currency = nullptr;
		if (Json->TryGetObjectField(TEXT("currency"), Currency)) {
			(*Currency)->TryGetStringField(TEXT("symbol"), CurrencySymbol);
		}
		ShopHeroes.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Heroes = nullptr;
		if (Json->TryGetArrayField(TEXT("heroes"), Heroes)) {
			for (const TSharedPtr<FJsonValue>& Value : *Heroes) {
				const TSharedPtr<FJsonObject> One = Value->AsObject();
				FSenniteHeroForSale Hero;
				Hero.Id = One->GetStringField(TEXT("id"));
				Hero.Name = One->GetStringField(TEXT("name"));
				Hero.Price = FCString::Atoi(*One->GetStringField(TEXT("price")));
				ShopHeroes.Add(MoveTemp(Hero));
			}
		}
		ShopWeapons.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Weapons = nullptr;
		if (Json->TryGetArrayField(TEXT("weapons"), Weapons)) {
			for (const TSharedPtr<FJsonValue>& Value : *Weapons) {
				// an entry that is not a gun with an id, a name and a price is not listed
				const TSharedPtr<FJsonObject>* One = nullptr;
				FSenniteHeroForSale Gun;
				FString Price;
				if (!Value.IsValid() || !Value->TryGetObject(One) || !(*One)->TryGetStringField(TEXT("id"), Gun.Id) || Gun.Id.IsEmpty()
					|| !(*One)->TryGetStringField(TEXT("name"), Gun.Name) || !(*One)->TryGetStringField(TEXT("price"), Price)) {
					continue;
				}
				Gun.Price = FCString::Atoi(*Price);
				ShopWeapons.Add(MoveTemp(Gun));
			}
		}
		bShop = true;
	});
}

void USennitePublisher::AskSwap()
{
	if (bSwapAsked || !IsOn()) {
		return;
	}
	bSwapAsked = true;
	Send(TEXT("GET"), TEXT("/shop/swap"), nullptr, [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		bSwapAsked = false;
		const TArray<TSharedPtr<FJsonValue>>* Tokens = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Vaults = nullptr;
		if (Status != 200 || !Json.IsValid() || !Json->TryGetArrayField(TEXT("tokens"), Tokens) || !Json->TryGetArrayField(TEXT("vaults"), Vaults)) {
			return;
		}
		// an entry that is not whole (no id, no number of digits, a vault with a side missing) is not listed
		SwapTokens.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *Tokens) {
			const TSharedPtr<FJsonObject>* One = nullptr;
			FSenniteSwapToken Held;
			double Decimals = 0.0;
			if (!Value.IsValid() || !Value->TryGetObject(One) || !(*One)->TryGetStringField(TEXT("id"), Held.Id) || Held.Id.IsEmpty()
				|| !(*One)->TryGetStringField(TEXT("symbol"), Held.Symbol) || !(*One)->TryGetStringField(TEXT("name"), Held.Name)
				|| !(*One)->TryGetNumberField(TEXT("decimals"), Decimals) || Decimals < 0.0 || Decimals > 18.0) {
				continue;
			}
			Held.Decimals = int32(Decimals);
			SwapTokens.Add(MoveTemp(Held));
		}
		SwapVaults.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *Vaults) {
			const TSharedPtr<FJsonObject>* One = nullptr;
			FSenniteSwapVault Vault;
			double FeeBps = 0.0;
			if (!Value.IsValid() || !Value->TryGetObject(One) || !(*One)->TryGetStringField(TEXT("a"), Vault.A) || !(*One)->TryGetStringField(TEXT("b"), Vault.B)
				|| !(*One)->TryGetStringField(TEXT("reserveA"), Vault.ReserveA) || !(*One)->TryGetStringField(TEXT("reserveB"), Vault.ReserveB)
				|| !(*One)->TryGetNumberField(TEXT("feeBps"), FeeBps)) {
				continue;
			}
			Vault.FeeBps = int32(FeeBps);
			SwapVaults.Add(MoveTemp(Vault));
		}
		bSwapKnown = true;
	});
}

const FSenniteSwapVault* USennitePublisher::FindSwapVault(const FString& One, const FString& Other) const
{
	return SwapVaults.FindByPredicate([&One, &Other](const FSenniteSwapVault& V) { return (V.A == One && V.B == Other) || (V.A == Other && V.B == One); });
}

FString USennitePublisher::HoldsOf(const FSenniteSwapToken& Of) const
{
	if (!bConnected || !bMaterialsKnown) {
		return FString();
	}
	if (Of.Symbol == TEXT("ERA")) {
		return Era;
	}
	if (!CurrencySymbol.IsEmpty() && Of.Symbol == CurrencySymbol) {
		return FString::FromInt(DBucks);
	}
	if (const int32* Held = Materials.Find(Of.Symbol)) {
		return FString::FromInt(*Held);
	}
	return FString();
}

void USennitePublisher::AskSwapPrice(const FString& From, const FString& To, const FString& Amount)
{
	if (bBusy) {
		return;
	}
	SwapPrice = FSenniteSwapPrice();
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("from"), From);
	Body->SetStringField(TEXT("to"), To);
	Body->SetStringField(TEXT("amount"), Amount);
	Ask(TEXT("/shop/swap/price"), Body, TEXT("asking your wallet for the price..."));
	bPricing = bBusy;
}

void USennitePublisher::Swap(const FString& From, const FString& To, const FString& Amount)
{
	if (bBusy || !PricedFor(From, To, Amount)) {
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("from"), From);
	Body->SetStringField(TEXT("to"), To);
	Body->SetStringField(TEXT("amount"), Amount);
	Body->SetStringField(TEXT("shown"), SwapPrice.Out);
	AskToPay(TEXT("/shop/swap/trades"), Body, TEXT("approve the swap in your DSM wallet (Apps, Waiting), then come back"));
	bSwapping = bBusy;
	bProveAfter = bBusy;
}

bool USennitePublisher::OpenConnectCode() const
{
	if (ConnectCode.IsEmpty()) {
		return false;
	}
	FPlatformApplicationMisc::ClipboardCopy(*ConnectCode);
#if PLATFORM_ANDROID || PLATFORM_IOS
	// on a phone the code is a link the DSM wallet opens
	FPlatformProcess::LaunchURL(*ConnectCode, nullptr, nullptr);
#endif
	return true;
}

FString USennitePublisher::DBucksLine(bool& bKnown) const
{
	bKnown = bConnected && bMaterialsKnown;
	if (!bConnected) {
		return FString(TEXT("NO WALLET CONNECTED"));
	}
	if (bMaterialsKnown) {
		return FString::Printf(TEXT("%s D-BUCKS"), *SenniteUi::Grouped(DBucks));
	}
	// nothing proved yet: no amount is made up for it
	return FString(ProofPending() ? TEXT("ASKING YOUR WALLET ...") : TEXT("NOT PROVED YET"));
}

bool USennitePublisher::OpenWalletPlain()
{
	bool bOpened = false;
#if PLATFORM_ANDROID
	// Sennite_UPL.xml: the wallet's launch intent
	if (JNIEnv* Env = FAndroidApplication::GetJavaEnv()) {
		static jmethodID Open = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_SenniteOpenWallet", "()Z", true);
		if (Open != nullptr) {
			bOpened = FJavaWrapper::CallBooleanMethod(Env, FJavaWrapper::GameActivityThis, Open);
		}
	}
#endif
	WalletOpens += bOpened ? 1 : 0;
	UE_LOG(LogSennitePublisher, Display, TEXT("The game %s the DSM wallet"), bOpened ? TEXT("opened") : TEXT("could not open"));
	return bOpened;
}

void USennitePublisher::BuyDBucks(const FString& InEra)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("era"), InEra);
	AskToPay(TEXT("/shop/dbucks"), Body, TEXT("approve the trade of ERA for D-bucks in your DSM wallet (Apps, Waiting), then come back"));
	bProveAfter = bBusy;
}

void USennitePublisher::BuySkin(const FString& SkinId)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("skinId"), SkinId.ToLower());
	if (bBusy) {
		return;
	}
	AskToPay(TEXT("/shop/skins"), Body, TEXT("approve the payment for the skin in your DSM wallet (Apps, Waiting), then come back"));
	bProveAfter = bBusy;
	BuyingSkin = SkinId;
}

void USennitePublisher::BuyHero(const FString& HeroId)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	// The shop's ids are lower case. A hero's id is a name here, and a name is written in the case Unreal first met
	// it in ("Rampage", from the hero's own assets), so it is sent as the shop has it.
	Body->SetStringField(TEXT("heroId"), HeroId.ToLower());
	if (bBusy) {
		return;
	}
	AskToPay(TEXT("/shop/heroes"), Body, TEXT("approve the payment for the hero in your DSM wallet (Apps, Waiting), then come back"));
	bProveAfter = bBusy;
	BuyingHero = HeroId;
}

void USennitePublisher::BuyWeapon(const FString& WeaponId)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("weaponId"), WeaponId.ToLower());
	if (bBusy) {
		return;
	}
	AskToPay(TEXT("/shop/weapons"), Body, TEXT("approve the payment for the gun in your DSM wallet (Apps, Waiting), then come back"));
	bProveAfter = bBusy;
	// bought and given as a hero is: it is the wallet's once the wallet proves it, in the same list
	BuyingHero = WeaponId;
}

void USennitePublisher::AskHeroPass(const FString& ForAudience, TFunction<void(const FString&)> Done)
{
	if (!IsOn() || !bConnected || !IsAudience(ForAudience)) {
		Done(FString());
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("audience"), ForAudience);
	Send(TEXT("POST"), TEXT("/players/me/hero-pass"), Body, [Done = MoveTemp(Done)](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		FString Pass;
		Done(Status == 201 && Json.IsValid() && Json->TryGetStringField(TEXT("pass"), Pass) && IsHeroPass(Pass) ? Pass : FString());
	});
}

bool USennitePublisher::IsHeroPass(const FString& Pass)
{
	if (Pass.Len() != 32) {
		return false;
	}
	for (const TCHAR Each : Pass) {
		if (!((Each >= TEXT('0') && Each <= TEXT('9')) || (Each >= TEXT('a') && Each <= TEXT('f')))) {
			return false;
		}
	}
	return true;
}

void USennitePublisher::CheckHeroPass(const FString& Pass, TFunction<void(bool, const TArray<FString>&, const FString&, const FString&)> Done)
{
	if (!IsOn() || !IsHeroPass(Pass)) {
		Done(false, TArray<FString>(), FString(), FString());
		return;
	}
	// the pass is good only for this server: it asks with what it is, its own token or the match server's secret
	Send(TEXT("GET"), FString::Printf(TEXT("/hero-passes/%s"), *Pass), nullptr, [Done = MoveTemp(Done)](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		TArray<FString> Heroes;
		FString SigningKey;
		FString Wallet;
		const TArray<TSharedPtr<FJsonValue>>* Listed = nullptr;
		if (Status != 200 || !Json.IsValid() || !Json->TryGetArrayField(TEXT("heroes"), Listed)
			|| !Json->TryGetStringField(TEXT("signingKey"), SigningKey) || !Json->TryGetStringField(TEXT("wallet"), Wallet)) {
			Done(false, Heroes, FString(), FString());
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Listed) {
			Heroes.Add(Value->AsString());
		}
		Done(true, Heroes, SigningKey, Wallet);
	}, IsMatchServer() ? MatchSecret : FString(), 10.0f);
}

void USennitePublisher::Wear(const FString& SkinId)
{
	Wearing = SkinId;
	SaveToken();
}

void USennitePublisher::AskMe()
{
	bMeAsked = true;
	Send(TEXT("GET"), TEXT("/players/me"), nullptr, [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		bMeAsked = false;
		NextMe = FPlatformTime::Seconds() + (bConnected ? 3.0 : 1.0);
		if (Status == 401) {
			// the server does not know this token (another server, or its record was reset): start as a new player
			Token.Reset();
			ConnectCode.Reset();
			bConnected = false;
			return;
		}
		if (Status != 200 || !Json.IsValid()) {
			return;
		}
		bConnected = Json->GetBoolField(TEXT("connected"));
		// what this game is as the audience of a hero pass, when it is a game server
		FString Named;
		if (Json->TryGetStringField(TEXT("audience"), Named) && IsHeroPass(Named)) {
			Audience = Named;
		}
		Wallet = bConnected ? Json->GetStringField(TEXT("wallet")) : FString();
	});
}

void USennitePublisher::AskBases()
{
	for (const FSenniteParcel& Parcel : Parcels) {
		++BasesAsked;
		const FString Id = Parcel.Id;
		Send(TEXT("GET"), FString::Printf(TEXT("/parcels/%s/base"), *Id), nullptr, [this, Id](int32 Status, const TSharedPtr<FJsonObject>& Json) {
			--BasesAsked;
			NextBases = FPlatformTime::Seconds() + 1.5;
			if (Status == 200 && Json.IsValid()) {
				ReadBase(Id, *Json);
			}
		});
	}
}

void USennitePublisher::ReadBase(const FString& InParcelId, const FJsonObject& Json)
{
	FSenniteBaseView Base;
	++BasesRead;
	Base.bKnown = true;
	Base.bAssigned = Json.GetBoolField(TEXT("assigned"));
	if (Base.bAssigned) {
		Base.FactionId = Json.GetStringField(TEXT("factionId"));
		Base.bMember = Json.GetBoolField(TEXT("member"));
		Base.bInvited = Json.GetBoolField(TEXT("invited"));
		Base.Members = Json.GetIntegerField(TEXT("members"));
		const TSharedPtr<FJsonObject>* People = nullptr;
		if (Json.TryGetObjectField(TEXT("people"), People)) {
			for (const TSharedPtr<FJsonValue>& Value : (*People)->GetArrayField(TEXT("members"))) {
				const TSharedPtr<FJsonObject> One = Value->AsObject();
				FSennitePerson Person;
				Person.SigningKey = One->GetStringField(TEXT("signingKey"));
				Person.Wallet = One->GetStringField(TEXT("wallet"));
				Person.bYou = One->GetBoolField(TEXT("you"));
				Person.Votes = One->GetIntegerField(TEXT("votes"));
				Person.bVoted = One->GetBoolField(TEXT("voted"));
				Base.People.Add(MoveTemp(Person));
			}
			for (const TSharedPtr<FJsonValue>& Value : (*People)->GetArrayField(TEXT("invited"))) {
				const TSharedPtr<FJsonObject> One = Value->AsObject();
				FSennitePerson Person;
				Person.SigningKey = One->GetStringField(TEXT("signingKey"));
				Person.Wallet = One->GetStringField(TEXT("wallet"));
				Base.Invited.Add(MoveTemp(Person));
			}
		}
		Base.Height = Json.GetIntegerField(TEXT("height"));
		const TSharedPtr<FJsonObject>* Building = nullptr;
		if (Json.TryGetObjectField(TEXT("building"), Building) && !(*Building)->GetBoolField(TEXT("open"))) {
			Base.Building = (*Building)->GetStringField(TEXT("message"));
		}
		for (const TSharedPtr<FJsonValue>& Value : Json.GetArrayField(TEXT("structures"))) {
			const TSharedPtr<FJsonObject> Entry = Value->AsObject();
			FSenniteStructureView View;
			View.InstanceId = Entry->GetStringField(TEXT("instanceId"));
			View.PrefabId = Entry->GetStringField(TEXT("prefabId"));
			View.X = Entry->GetIntegerField(TEXT("x"));
			View.Y = Entry->GetIntegerField(TEXT("y"));
			View.Rotation = Entry->GetIntegerField(TEXT("rotation"));
			View.bStanding = Entry->GetBoolField(TEXT("standing"));
			View.bDestroyed = Entry->GetBoolField(TEXT("destroyed"));
			const TSharedPtr<FJsonObject> Funding = Entry->GetObjectField(TEXT("funding"));
			View.Status = Funding->GetStringField(TEXT("status"));
			const TArray<TSharedPtr<FJsonValue>>* Needs = nullptr;
			if (Funding->TryGetArrayField(TEXT("resources"), Needs)) {
				for (const TSharedPtr<FJsonValue>& NeedValue : *Needs) {
					const TSharedPtr<FJsonObject> Need = NeedValue->AsObject();
					View.Needs.Add({ Need->GetStringField(TEXT("resource")), (int32)Need->GetIntegerField(TEXT("needed")),
						(int32)Need->GetIntegerField(TEXT("funded")), (int32)Need->GetIntegerField(TEXT("shortfall")) });
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Committed = nullptr;
			if (Funding->TryGetArrayField(TEXT("contributions"), Committed)) {
				for (const TSharedPtr<FJsonValue>& CommittedValue : *Committed) {
					const TSharedPtr<FJsonObject> One = CommittedValue->AsObject();
					FSenniteCommitted Line;
					Line.Owner = One->GetStringField(TEXT("owner"));
					Line.Resource = One->GetStringField(TEXT("resource"));
					Line.Amount = One->GetIntegerField(TEXT("amount"));
					Line.bMine = One->GetBoolField(TEXT("mine"));
					Line.bLeaving = One->GetBoolField(TEXT("leaving"));
					View.Committed.Add(MoveTemp(Line));
				}
			}
			Base.Structures.Add(MoveTemp(View));
		}
	}
	if (FSenniteParcel* Parcel = Parcels.FindByPredicate([&InParcelId](const FSenniteParcel& P) { return P.Id == InParcelId; })) {
		Parcel->FactionId = Base.FactionId;
	}
	Bases.Add(InParcelId, MoveTemp(Base));

	// the wallet's faction is the one whose base says it is a member: known the moment that base is read
	FactionId.Reset();
	ParcelId.Reset();
	for (const TPair<FString, FSenniteBaseView>& Entry : Bases) {
		if (Entry.Value.bMember) {
			FactionId = Entry.Value.FactionId;
			ParcelId = Entry.Key;
		}
	}
}

void USennitePublisher::AskFree()
{
	bFreeAsked = true;
	Send(TEXT("GET"), TEXT("/players/free"), nullptr, [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		bFreeAsked = false;
		NextFree = FPlatformTime::Seconds() + 2.0;
		const TArray<TSharedPtr<FJsonValue>>* Players = nullptr;
		if (Status != 200 || !Json.IsValid() || !Json->TryGetArrayField(TEXT("players"), Players)) {
			return;
		}
		FreePlayers.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *Players) {
			const TSharedPtr<FJsonObject> One = Value->AsObject();
			FSennitePerson Person;
			Person.Wallet = One->GetStringField(TEXT("wallet"));
			FreePlayers.Add(MoveTemp(Person));
		}
	});
}

void USennitePublisher::AskPrizes()
{
	bPrizesAsked = true;
	Send(TEXT("GET"), TEXT("/players/me/prizes"), nullptr, [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		bPrizesAsked = false;
		NextPrizes = FPlatformTime::Seconds() + 5.0;
		const TArray<TSharedPtr<FJsonValue>>* Waiting = nullptr;
		if (Status != 200 || !Json.IsValid() || !Json->TryGetArrayField(TEXT("prizes"), Waiting)) {
			return;
		}
		Prizes.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *Waiting) {
			const TSharedPtr<FJsonObject> One = Value->AsObject();
			FSennitePrize Prize;
			Prize.MatchId = One->GetStringField(TEXT("matchId"));
			Prize.Cell = One->GetStringField(TEXT("cell"));
			Prize.Resource = One->GetStringField(TEXT("resource"));
			Prize.Amount = One->GetIntegerField(TEXT("amount"));
			Prizes.Add(MoveTemp(Prize));
		}
		++PrizesRead;
	});
}

void USennitePublisher::AskRequest()
{
	bRequestAsked = true;
	Send(TEXT("GET"), TEXT("/requests/") + PendingRequest, nullptr, [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		bRequestAsked = false;
		NextRequest = FPlatformTime::Seconds() + 0.5;
		if (Status != 200 || !Json.IsValid()) {
			if (Status == 404) {
				PendingRequest.Reset();
				bBusy = false;
				bOnWallet = false;
				bPricing = false;
				bSwapping = false;
				bLastOk = false;
				Message = TEXT("the publisher's server no longer knows the request");
			}
			return;
		}
		if (Json->GetStringField(TEXT("state")) != TEXT("done")) {
			// what it is waiting on, as the server says: the player in their wallet, or the shop's own work
			FString Stage;
			if (Json->TryGetStringField(TEXT("stage"), Stage) && !Stage.IsEmpty()) {
				Message = Stage;
				bool bOn = false;
				bOnWallet = Json->TryGetBoolField(TEXT("onWallet"), bOn) && bOn;
			}
			return;
		}
		PendingRequest.Reset();
		bBusy = false;
		bOnWallet = false;
		bLastOk = Json->GetBoolField(TEXT("ok"));
		UE_LOG(LogSennitePublisher, Display, TEXT("What was asked of the wallet is done, %s: %s"), bLastOk ? TEXT("carried out") : TEXT("not carried out"), *Json->GetStringField(TEXT("message")));
		if (!BuyingSkin.IsEmpty()) {
			// bought and given: it is the wallet's once the wallet has taken it in and proves it
			if (bLastOk) {
				OnItsWay = BuyingSkin;
				ArrivalProofs = 0;
				NextArrival = FPlatformTime::Seconds() + 2.0;
			}
			BuyingSkin.Reset();
		}
		if (!BuyingHero.IsEmpty()) {
			// the same for a hero
			if (bLastOk) {
				HeroOnItsWay = BuyingHero;
				ArrivalProofs = 0;
				NextArrival = FPlatformTime::Seconds() + 2.0;
			}
			BuyingHero.Reset();
		}
		Message = Json->GetStringField(TEXT("message"));
		if (bPricing) {
			// a price: what the wallet said the swap would give now
			bPricing = false;
			const TSharedPtr<FJsonObject>* Price = nullptr;
			if (bLastOk && Json->TryGetObjectField(TEXT("price"), Price)) {
				// a price is taken whole or not at all: one with a part missing is no price to swap at
				FSenniteSwapPrice Given;
				double Hops = 0.0;
				if ((*Price)->TryGetStringField(TEXT("from"), Given.From) && (*Price)->TryGetStringField(TEXT("to"), Given.To) && (*Price)->TryGetStringField(TEXT("amount"), Given.Amount)
					&& (*Price)->TryGetStringField(TEXT("amountIn"), Given.AmountIn) && (*Price)->TryGetStringField(TEXT("amountOut"), Given.AmountOut)
					&& (*Price)->TryGetStringField(TEXT("out"), Given.Out) && (*Price)->TryGetNumberField(TEXT("hops"), Hops)) {
					Given.Hops = int32(Hops);
					SwapPrice = Given;
				}
			}
		}
		if (bSwapping) {
			// swapped or not, the price shown was the vaults' before it: the next swap asks again, and the reserves are read again
			bSwapping = false;
			SwapPrice = FSenniteSwapPrice();
			AskSwap();
		}
		if (bProveAfter) {
			// what the wallet holds changed with this: ask it again, without holding the player up
			bProveAfter = false;
			bProofWanted = true;
		}
		// what changed shows at once, not at the next round
		NextBases = 0.0;
		NextMe = 0.0;
		NextFree = 0.0;
	});
}

void USennitePublisher::Ask(const FString& Path, const TSharedPtr<FJsonObject>& Body, const TCHAR* Waiting)
{
	if (bBusy) {
		return;
	}
	bBusy = true;
	bLastOk = false;
	Message = Waiting;
	Send(TEXT("POST"), Path, Body, [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		FString Request;
		if (Status == 202 && Json.IsValid() && Json->TryGetStringField(TEXT("request"), Request)) {
			PendingRequest = Request;
			NextRequest = 0.0;
			if (bOpenWalletWhenAsked) {
				// the server has the request: now the wallet, where the player approves it
				bOpenWalletWhenAsked = false;
				Message = WordsWhenAsked;
				bOnWallet = true;
				OpenWalletToApprove();
			}
			return;
		}
		bOpenWalletWhenAsked = false;
		bBusy = false;
		bPricing = false;
		bSwapping = false;
		// nothing was started: a skin or hero that was to be bought is not being bought
		BuyingSkin.Reset();
		BuyingHero.Reset();
		Message = ErrorOf(Status, Json);
		UE_LOG(LogSennitePublisher, Warning, TEXT("The shop did not take what was asked: %s"), *Message);
	});
}

void USennitePublisher::AskToPay(const FString& Path, const TSharedPtr<FJsonObject>& Body, const TCHAR* Waiting)
{
	if (bBusy) {
		return;
	}
	// Until the server has taken the request nothing waits in the wallet, so nothing says it does: a request the
	// server refuses (no wallet connected, the server not reached) never sent the player to their wallet for nothing.
	Ask(Path, Body, TEXT("asking the shop..."));
	// No payment and no trade is in the grant: each waits in the wallet for the player's approval. So the game goes to
	// the wallet with it, as soon as the server has taken the request (Ask), and the player approves it there. The
	// owner, 2026-11-19: "when you hit fucking buy, it goes to the wallet, and it goes to where it asks you to approve".
	bOpenWalletWhenAsked = bBusy;
	WordsWhenAsked = Waiting;
}

bool USennitePublisher::OpenWalletToApprove()
{
	bool bOpened = false;
#if PLATFORM_ANDROID
	// Sennite_UPL.xml: the wallet, opened on the connect code it is connected to this game by
	if (JNIEnv* Env = !ConnectCode.IsEmpty() ? FAndroidApplication::GetJavaEnv() : nullptr) {
		static jmethodID Open = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_SenniteOpenWalletLink", "(Ljava/lang/String;)Z", true);
		if (Open != nullptr) {
			auto Link = FJavaHelper::ToJavaString(Env, ConnectCode);
			bOpened = FJavaWrapper::CallBooleanMethod(Env, FJavaWrapper::GameActivityThis, Open, *Link);
		}
	}
#endif
	if (bOpened) {
		++WalletOpens;
		UE_LOG(LogSennitePublisher, Display, TEXT("The game opened the DSM wallet on its connect code, for the player to approve what it asked"));
	}
	else {
		// no code kept, or a wallet that does not take the link: the wallet as it opens by itself
		bOpened = OpenWalletPlain();
	}
	bWalletHere = bOpened;
	return bOpened;
}

void USennitePublisher::ProveMaterials()
{
	if (!bConnected) {
		return;
	}
	bProofWanted = true;
	bProofByHand = true;
	if (!bBusy) {
		Message = TEXT("waiting for the wallet to prove what it holds...");
		bLastOk = false;
	}
}

void USennitePublisher::StartProof()
{
	bProofWanted = false;
	bProving = true;
	Send(TEXT("POST"), TEXT("/players/me/materials"), MakeShared<FJsonObject>(), [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		FString Request;
		if (Status == 202 && Json.IsValid() && Json->TryGetStringField(TEXT("request"), Request)) {
			PendingProof = Request;
			NextProof = 0.0;
			return;
		}
		bProving = false;
		if (bProofByHand && !bBusy) {
			Message = ErrorOf(Status, Json);
		}
		bProofByHand = false;
	});
}

void USennitePublisher::AskProof()
{
	bProofAsked = true;
	Send(TEXT("GET"), TEXT("/requests/") + PendingProof, nullptr, [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		bProofAsked = false;
		NextProof = FPlatformTime::Seconds() + 0.5;
		const bool bGone = Status == 404;
		if (!bGone && (Status != 200 || !Json.IsValid() || Json->GetStringField(TEXT("state")) != TEXT("done"))) {
			return;
		}
		PendingProof.Reset();
		bProving = false;
		const TSharedPtr<FJsonObject>* Proven = nullptr;
		const bool bProved = !bGone && Json->TryGetObjectField(TEXT("materials"), Proven);
		if (bProved) {
			ReadHoldings(*Json);
		}
		if (bProofByHand && !bBusy) {
			bLastOk = bProved;
			Message = bGone ? FString(TEXT("the publisher's server no longer knows the request")) : Json->GetStringField(TEXT("message"));
		}
		bProofByHand = false;
	});
}

void USennitePublisher::ReadHoldings(const FJsonObject& Json)
{
	const TSharedPtr<FJsonObject> Proven = Json.GetObjectField(TEXT("materials"));
	Materials.Reset();
	for (const FString& Resource : Resources) {
		Materials.Add(Resource, Proven->GetIntegerField(Resource));
	}
	DBucks = Json.GetIntegerField(TEXT("dbucks"));
	Era = Json.GetStringField(TEXT("era"));
	OwnedSkins.Reset();
	Inventory.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Skins = nullptr;
	if (Json.TryGetArrayField(TEXT("skins"), Skins)) {
		for (const TSharedPtr<FJsonValue>& Value : *Skins) {
			const TSharedPtr<FJsonObject> One = Value->AsObject();
			FSenniteOwnedSkin Skin;
			Skin.SkinId = One->GetStringField(TEXT("skinId"));
			Skin.Name = One->GetStringField(TEXT("name"));
			Skin.Serial = One->GetIntegerField(TEXT("serial"));
			OwnedSkins.AddUnique(Skin.SkinId);
			Inventory.Add(MoveTemp(Skin));
		}
	}
	OwnedHeroes.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Heroes = nullptr;
	if (Json.TryGetArrayField(TEXT("heroes"), Heroes)) {
		for (const TSharedPtr<FJsonValue>& Value : *Heroes) {
			OwnedHeroes.AddUnique(Value->AsObject()->GetStringField(TEXT("heroId")));
		}
	}
	// the guns the wallet owns, in the same list: they are sold and owned as heroes are, and no gun has a hero's id
	const TArray<TSharedPtr<FJsonValue>>* Weapons = nullptr;
	if (Json.TryGetArrayField(TEXT("weapons"), Weapons)) {
		for (const TSharedPtr<FJsonValue>& Value : *Weapons) {
			const TSharedPtr<FJsonObject>* One = nullptr;
			FString WeaponId;
			if (Value.IsValid() && Value->TryGetObject(One) && (*One)->TryGetStringField(TEXT("weaponId"), WeaponId) && !WeaponId.IsEmpty()) {
				OwnedHeroes.AddUnique(WeaponId);
			}
		}
	}
	bMaterialsKnown = true;
}

void USennitePublisher::Found(const FString& InParcelId)
{
	if (bBusy) {
		return;
	}
	bBusy = true;
	bLastOk = false;
	Message = TEXT("founding a faction...");
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("parcelId"), InParcelId);
	Send(TEXT("POST"), TEXT("/factions"), Body, [this](int32 Status, const TSharedPtr<FJsonObject>& Json) {
		bBusy = false;
		bLastOk = Status == 201;
		Message = bLastOk ? FString(TEXT("the faction is founded")) : ErrorOf(Status, Json);
		NextBases = 0.0;
		NextMe = 0.0;
	});
}

void USennitePublisher::SendChangeTo(const FString& InFactionId, const TSharedRef<FJsonObject>& Change, const TCHAR* Waiting)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("change"), Change);
	Ask(FString::Printf(TEXT("/factions/%s/changes"), *InFactionId), Body, Waiting);
}

void USennitePublisher::SendChange(const TSharedRef<FJsonObject>& Change)
{
	if (FactionId.IsEmpty()) {
		Message = TEXT("the wallet is not a member of a faction");
		return;
	}
	SendChangeTo(FactionId, Change, TEXT("waiting for the wallet to sign the change..."));
}

void USennitePublisher::Invite(const FString& InWallet)
{
	if (FactionId.IsEmpty()) {
		Message = TEXT("the wallet is not a member of a faction");
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("wallet"), InWallet);
	Ask(FString::Printf(TEXT("/factions/%s/invitations"), *FactionId), Body, TEXT("waiting for the wallet to sign the invitation..."));
}

void USennitePublisher::Uninvite(const FString& SigningKey)
{
	const TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
	Change->SetStringField(TEXT("kind"), TEXT("uninvite"));
	Change->SetStringField(TEXT("signingKey"), SigningKey);
	SendChange(Change);
}

void USennitePublisher::Accept(const FString& InFactionId)
{
	const TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
	Change->SetStringField(TEXT("kind"), TEXT("accept"));
	SendChangeTo(InFactionId, Change, TEXT("waiting for the wallet to sign the acceptance..."));
}

void USennitePublisher::Vote(const FString& SigningKey)
{
	const TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
	Change->SetStringField(TEXT("kind"), TEXT("vote"));
	Change->SetStringField(TEXT("signingKey"), SigningKey);
	SendChange(Change);
}

void USennitePublisher::Unvote(const FString& SigningKey)
{
	const TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
	Change->SetStringField(TEXT("kind"), TEXT("unvote"));
	Change->SetStringField(TEXT("signingKey"), SigningKey);
	SendChange(Change);
}

void USennitePublisher::Leave()
{
	const TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
	Change->SetStringField(TEXT("kind"), TEXT("leave"));
	SendChange(Change);
}

void USennitePublisher::Place(const FString& PrefabId, int32 X, int32 Y, int32 Rotation)
{
	const TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
	Change->SetStringField(TEXT("kind"), TEXT("place"));
	Change->SetStringField(TEXT("instanceId"), NewInstanceId());
	Change->SetStringField(TEXT("prefabId"), PrefabId);
	Change->SetNumberField(TEXT("x"), X);
	Change->SetNumberField(TEXT("y"), Y);
	Change->SetNumberField(TEXT("rotation"), Rotation);
	Change->SetObjectField(TEXT("config"), MakeShared<FJsonObject>());
	SendChange(Change);
}

void USennitePublisher::Replace(const FString& InstanceId, const FString& PrefabId)
{
	const TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
	Change->SetStringField(TEXT("kind"), TEXT("replace"));
	Change->SetStringField(TEXT("instanceId"), InstanceId);
	Change->SetStringField(TEXT("prefabId"), PrefabId);
	Change->SetObjectField(TEXT("config"), MakeShared<FJsonObject>());
	SendChange(Change);
}

void USennitePublisher::Dismantle(const FString& InstanceId)
{
	const TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
	Change->SetStringField(TEXT("kind"), TEXT("dismantle"));
	Change->SetStringField(TEXT("instanceId"), InstanceId);
	SendChange(Change);
}

void USennitePublisher::Confirm(const FString& InstanceId)
{
	const TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
	Change->SetStringField(TEXT("kind"), TEXT("confirm"));
	Change->SetStringField(TEXT("instanceId"), InstanceId);
	SendChange(Change);
}

void USennitePublisher::Commit(const FString& InstanceId, const FString& Resource, int32 Amount)
{
	if (FactionId.IsEmpty()) {
		Message = TEXT("the wallet is not a member of a faction");
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("instanceId"), InstanceId);
	Body->SetStringField(TEXT("resource"), Resource);
	Body->SetNumberField(TEXT("amount"), Amount);
	Ask(FString::Printf(TEXT("/factions/%s/commitments"), *FactionId), Body, TEXT("waiting for the wallet to commit..."));
	bProveAfter = bBusy;
}

void USennitePublisher::Withdraw(const FString& InstanceId)
{
	// the faction of the parcel the structure is on: a member who left still takes back what is theirs
	FString Faction = FactionId;
	for (const TPair<FString, FSenniteBaseView>& Base : Bases) {
		if (Base.Value.Structures.ContainsByPredicate([&InstanceId](const FSenniteStructureView& S) { return S.InstanceId == InstanceId; })) {
			Faction = Base.Value.FactionId;
		}
	}
	if (Faction.IsEmpty()) {
		Message = TEXT("no faction holds that structure");
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("instanceId"), InstanceId);
	Ask(FString::Printf(TEXT("/factions/%s/withdrawals"), *Faction), Body, TEXT("waiting for the wallet to take the materials back..."));
	bProveAfter = bBusy;
}
