// The game's line to the publisher's server (Server/ in this repo).
//
// The game stays an ordinary game: it holds no DSM account and no key. It shows the connect code
// the server gives it, sends the choices a player makes in the base editor, and keeps what the
// server last said the world is: the catalogue, the parcels, each parcel's base. What a player's
// wallet signs, commits or takes back is asked of the wallet by the publisher's own DSM account;
// the game only learns what became of it.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SennitePublisher.generated.h"

class FJsonObject;

/** One prefab of the publisher's catalogue. */
struct FSennitePrefab
{
	FString Id;
	FString Name;
	/** The ground it covers before it is turned, in map units. */
	int32 Width = 0;
	int32 Depth = 0;
	/** What it needs of each resource, as "STEEL 300". */
	TArray<TPair<FString, int32>> Recipe;
};

/** One skin the publisher's shop sells, and its price in D-bucks. */
struct FSenniteSkinForSale
{
	FString Id;
	FString Name;
	int32 Price = 0;
};

/** One skin the wallet proved it owns: an object of which one exists. */
struct FSenniteOwnedSkin
{
	/** Which skin of the shop it is. */
	FString SkinId;
	FString Name;
	/** Its number among the skins the shop has sold. */
	int32 Serial = 0;
};

/** One parcel of the publisher's registry. */
struct FSenniteParcel
{
	FString Id;
	TArray<FIntPoint> Boundary;
	/** The faction it is assigned to; empty while it is unassigned. */
	FString FactionId;
};

/** One resource of a structure's recipe and how far it is funded. */
struct FSenniteNeed
{
	FString Resource;
	int32 Needed = 0;
	int32 Funded = 0;
	int32 Shortfall = 0;
};

/** One contribution to a structure. */
struct FSenniteCommitted
{
	FString Owner;
	FString Resource;
	int32 Amount = 0;
	bool bMine = false;
	bool bLeaving = false;
};

/** One structure of a base, as the server shows it to this player. */
struct FSenniteStructureView
{
	FString InstanceId;
	FString PrefabId;
	int32 X = 0;
	int32 Y = 0;
	int32 Rotation = 0;
	/** Whether it stands in play. A plan shows only in the faction's editor. */
	bool bStanding = false;
	/** Whether a battle destroyed it and no member has rebuilt it yet. */
	bool bDestroyed = false;
	/** "fully funded", "needs resources" or "not yet known". */
	FString Status;
	TArray<FSenniteNeed> Needs;
	TArray<FSenniteCommitted> Committed;
};

/** A member of a faction, someone it has invited, or a player in no faction: by their wallet's DSM identity. */
struct FSennitePerson
{
	FString SigningKey;
	/** The wallet's DSM identity, as people read it. */
	FString Wallet;
	bool bYou = false;
	/** How many members have voted to remove this member. */
	int32 Votes = 0;
	/** Whether this player's wallet is one of them. */
	bool bVoted = false;
};

/** One parcel's base, as the server last gave it. */
struct FSenniteBaseView
{
	bool bKnown = false;
	bool bAssigned = false;
	FString FactionId;
	/** Whether this player's wallet is a member of the faction. */
	bool bMember = false;
	/** Whether the faction has invited this player's wallet and it has not accepted yet. */
	bool bInvited = false;
	int32 Members = 0;
	/** The faction's members and those it has invited: only its members are told. */
	TArray<FSennitePerson> People;
	TArray<FSennitePerson> Invited;
	/** How many changes the faction's state has taken. */
	int32 Height = 0;
	/** Why nothing can be built now, in the server's words; empty while building is open. */
	FString Building;
	TArray<FSenniteStructureView> Structures;
};

UCLASS(Config = Game)
class SENNITE_API USennitePublisher : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Where the publisher's server is, e.g. http://127.0.0.1:8790. Set in DefaultGame.ini, or with -PublisherUrl= on the command line. Empty: the game runs with no faction bases. */
	UPROPERTY(Config)
	FString PublisherUrl;

	/** Whether a publisher's server is set at all. */
	bool IsOn() const { return !PublisherUrl.IsEmpty(); }

	/** Whether the server has answered with its world. */
	bool HasWorld() const { return bWorld; }

	/** Whether the last thing asked of the server reached it. While it does not, there is no live session. */
	bool Reachable() const { return bReachable; }

	/** How many times a base has been read from the server. */
	int32 BasesRead = 0;

	int32 Grid = 480;
	TArray<FString> Resources;
	TArray<FSennitePrefab> Prefabs;
	TArray<FSenniteParcel> Parcels;

	/** The connect code this player's wallet reads; empty until the server gave one. */
	FString ConnectCode;
	bool bConnected = false;
	/** The connected wallet's DSM identity, as people read it. */
	FString Wallet;
	/** The faction this wallet is a member of, and its parcel; empty when it is in none. */
	FString FactionId;
	FString ParcelId;

	/** What the wallet proved it holds of each resource, and whether it has proved anything yet. */
	TMap<FString, int32> Materials;
	bool bMaterialsKnown = false;

	/** What the shop sells. */
	TArray<FSenniteSkinForSale> ShopSkins;
	/** What the wallet proved it holds of D-bucks and ERA, and the skins it proved it owns (by the shop's skin id). */
	int32 DBucks = 0;
	FString Era;
	TArray<FString> OwnedSkins;
	/** The same skins, one by one, as the inventory lists them. */
	TArray<FSenniteOwnedSkin> Inventory;
	/** The skin the player chose to wear. It is worn only while the wallet proves it owns it. */
	FString Wearing;
	/** A skin the shop sold and gave that the wallet has not proved it owns yet: it is on its way. */
	FString OnItsWay;

	bool Owns(const FString& SkinId) const { return OwnedSkins.Contains(SkinId); }
	/** Trade ERA for D-bucks through the publisher's vault. */
	void BuyDBucks(const FString& InEra);
	/** Buy a skin from the shop with D-bucks. */
	void BuySkin(const FString& SkinId);
	/** Wear an owned skin, or with an empty id, none. */
	void Wear(const FString& SkinId);

	/** The wallets connected to the game that are in no faction: who a member can invite. */
	TArray<FSennitePerson> FreePlayers;

	/** Each parcel's base, by parcel id. */
	TMap<FString, FSenniteBaseView> Bases;

	/** Whether something the player asked of the wallet has not settled yet. */
	bool bBusy = false;
	/** Whether a proof of what the wallet holds is wanted or in flight. A proof never holds up what the player asks. */
	bool ProofPending() const { return bProofWanted || bProving; }
	/** What became of the last thing asked, in the server's words. */
	FString Message;
	/** Whether the last thing asked was carried out. */
	bool bLastOk = false;

	const FSennitePrefab* FindPrefab(const FString& Id) const;
	const FSenniteParcel* FindParcel(const FString& Id) const;

	/** Ask the wallet to prove what it holds of the resources. */
	void ProveMaterials();
	/** Found a faction of this player's wallet on an unassigned parcel. */
	void Found(const FString& InParcelId);
	/** Place a prefab: its centre in map units and its quarter turns in degrees. */
	void Place(const FString& PrefabId, int32 X, int32 Y, int32 Rotation);
	void Replace(const FString& InstanceId, const FString& PrefabId);
	void Dismantle(const FString& InstanceId);
	/** Rebuild a plan a battle destroyed, as it is. */
	void Confirm(const FString& InstanceId);
	/** Invite a wallet connected to the game into this player's faction. */
	void Invite(const FString& InWallet);
	/** Withdraw an invitation nobody has accepted. */
	void Uninvite(const FString& SigningKey);
	/** Accept a faction's invitation: the wallet's own signature on joining. */
	void Accept(const FString& InFactionId);
	/** Vote to remove a member, or take the vote back. */
	void Vote(const FString& SigningKey);
	void Unvote(const FString& SigningKey);
	/** Leave the faction. */
	void Leave();
	/** Commit materials from the wallet to a structure. */
	void Commit(const FString& InstanceId, const FString& Resource, int32 Amount);
	/** Take back what the wallet still has committed to a structure. */
	void Withdraw(const FString& InstanceId);

private:
	using FAnswer = TFunction<void(int32 /* Status */, const TSharedPtr<FJsonObject>& /* Json */)>;

	bool Tick(float DeltaTime);
	void Send(const TCHAR* Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, FAnswer Done);
	/** Send something the wallet has to answer; the server names a request to ask about. */
	void Ask(const FString& Path, const TSharedPtr<FJsonObject>& Body, const TCHAR* Waiting);
	void SendChange(const TSharedRef<FJsonObject>& Change);
	void SendChangeTo(const FString& InFactionId, const TSharedRef<FJsonObject>& Change, const TCHAR* Waiting);
	void AskFree();
	void AskShop();
	void SaveToken() const;

	void AskWorld();
	void AskPlayer();
	void AskMe();
	void AskBases();
	void AskRequest();
	void ReadWorld(const FJsonObject& Json);
	void ReadBase(const FString& InParcelId, const FJsonObject& Json);

	FTSTicker::FDelegateHandle TickHandle;
	bool bWorld = false;
	bool bReachable = true;
	bool bWorldAsked = false;
	bool bPlayerAsked = false;
	bool bMeAsked = false;
	/** The game's own token for this player at the server. Never DSM authority. */
	FString Token;
	FString PendingRequest;
	bool bRequestAsked = false;
	/** The wallet's holdings changed with what was just asked: prove them again once it settles. */
	bool bProveAfter = false;
	/** A proof is to be asked for as soon as none is in flight. */
	bool bProofWanted = false;
	/** A proof is in flight: asked of the server, not settled yet. */
	bool bProving = false;
	/** The player asked for this proof by hand: its outcome is theirs to read. */
	bool bProofByHand = false;
	FString PendingProof;
	bool bProofAsked = false;
	double NextProof = 0.0;
	void StartProof();
	void AskProof();
	void ReadHoldings(const FJsonObject& Json);
	int32 BasesAsked = 0;
	double NextWorld = 0.0;
	double NextMe = 0.0;
	double NextBases = 0.0;
	double NextRequest = 0.0;
	double NextFree = 0.0;
	bool bFreeAsked = false;
	bool bShop = false;
	bool bShopAsked = false;
	/** The skin being bought by the request that has not settled yet. */
	FString BuyingSkin;
	double NextArrival = 0.0;
	int32 ArrivalProofs = 0;
	/** Whether the wallet has been asked what it holds since it connected. */
	bool bProvedOnConnect = false;
};
