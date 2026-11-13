#include "SennitePublisher.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogSennitePublisher, Log, All);

namespace
{
	/** Where the game keeps its token for the server between runs: it resumes the player's place, nothing more. */
	FString TokenFile()
	{
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
	// the token of an earlier run, if it was for this server
	FString Saved;
	TSharedPtr<FJsonObject> Json;
	if (FFileHelper::LoadFileToString(Saved, *TokenFile()) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Saved), Json) && Json.IsValid()) {
		if (Json->GetStringField(TEXT("url")) == PublisherUrl) {
			Token = Json->GetStringField(TEXT("player"));
			ConnectCode = Json->GetStringField(TEXT("code"));
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

void USennitePublisher::Send(const TCHAR* Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, FAnswer Done)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(PublisherUrl + Path);
	Request->SetVerb(Verb);
	if (!Token.IsEmpty()) {
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Token);
	}
	if (Body.IsValid()) {
		FString Text;
		FJsonSerializer::Serialize(Body.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(Text);
	}
	Request->OnProcessRequestComplete().BindWeakLambda(this, [Done = MoveTemp(Done)](FHttpRequestPtr, FHttpResponsePtr Response, bool bReached) {
		TSharedPtr<FJsonObject> Json;
		int32 Status = 0;
		if (bReached && Response.IsValid()) {
			Status = Response->GetResponseCode();
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json);
		}
		Done(Status, Json);
	});
	Request->ProcessRequest();
}

bool USennitePublisher::Tick(float)
{
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
	if (BasesAsked == 0 && Now >= NextBases) {
		AskBases();
	}
	if (!PendingRequest.IsEmpty() && !bRequestAsked && Now >= NextRequest) {
		AskRequest();
	}
	if (bConnected && !FactionId.IsEmpty() && !bFreeAsked && Now >= NextFree) {
		AskFree();
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
		const TSharedRef<FJsonObject> Keep = MakeShared<FJsonObject>();
		Keep->SetStringField(TEXT("url"), PublisherUrl);
		Keep->SetStringField(TEXT("player"), Token);
		Keep->SetStringField(TEXT("code"), ConnectCode);
		FString Text;
		FJsonSerializer::Serialize(Keep, TJsonWriterFactory<>::Create(&Text));
		FFileHelper::SaveStringToFile(Text, *TokenFile());
	});
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
				bLastOk = false;
				Message = TEXT("the publisher's server no longer knows the request");
			}
			return;
		}
		if (Json->GetStringField(TEXT("state")) != TEXT("done")) {
			return;
		}
		PendingRequest.Reset();
		bBusy = false;
		bLastOk = Json->GetBoolField(TEXT("ok"));
		const TSharedPtr<FJsonObject>* Proven = nullptr;
		if (Json->TryGetObjectField(TEXT("materials"), Proven)) {
			Materials.Reset();
			for (const FString& Resource : Resources) {
				Materials.Add(Resource, (*Proven)->GetIntegerField(Resource));
			}
			bMaterialsKnown = true;
			// a proof asked for after a commitment or a withdrawal leaves that one's message standing
			if (!bProveAfter) {
				Message = Json->GetStringField(TEXT("message"));
			}
			bProveAfter = false;
		}
		else {
			Message = Json->GetStringField(TEXT("message"));
			if (bProveAfter) {
				const FString Kept = Message;
				const bool bKeptOk = bLastOk;
				ProveMaterials();
				Message = Kept;
				bLastOk = bKeptOk;
			}
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
			return;
		}
		bBusy = false;
		Message = ErrorOf(Status, Json);
	});
}

void USennitePublisher::ProveMaterials()
{
	if (!bConnected) {
		return;
	}
	Ask(TEXT("/players/me/materials"), MakeShared<FJsonObject>(), TEXT("waiting for the wallet to prove what it holds..."));
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
