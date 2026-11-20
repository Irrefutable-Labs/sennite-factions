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

/** One hero the publisher's shop sells, and its price in D-bucks. */
struct FSenniteHeroForSale
{
	FString Id;
	FString Name;
	int32 Price = 0;
};

/** An object the publisher's vaults trade: what a swap is between. */
struct FSenniteSwapToken
{
	/** The object's id, which a swap names it by. */
	FString Id;
	FString Symbol;
	/** What the game calls it. */
	FString Name;
	/** How many digits an amount of it has after the point. */
	int32 Decimals = 0;
};

/** One of the publisher's open vaults: the two objects it trades, each side's reserve in its own units, and its fee. */
struct FSenniteSwapVault
{
	FString A;
	FString B;
	FString ReserveA;
	FString ReserveB;
	/** In hundredths of a percent. */
	int32 FeeBps = 0;
};

/** A price the wallet gave for a swap, and the swap it is the price of. Empty From: no price. */
struct FSenniteSwapPrice
{
	FString From;
	FString To;
	/** The amount asked about, as the player typed it. */
	FString Amount;
	/** What is paid and what it would get, in each object's own units. */
	FString AmountIn;
	FString AmountOut;
	/** What it would get, in the smallest unit: what a swap at this price names. */
	FString Out;
	/** How many vaults the swap goes through. */
	int32 Hops = 0;
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

/** A share of a match's prize the wallet won and has not collected yet. The wallet collects it itself. */
struct FSennitePrize
{
	FString MatchId;
	/** The match's verdict cell, as the wallet's escrow screen takes it: where the wallet finds the share. */
	FString Cell;
	FString Resource;
	int32 Amount = 0;
};

/** One built structure of a faction's base, as a match takes it. */
struct FSenniteMatchStructureView
{
	FString InstanceId;
	FString PrefabId;
	int32 X = 0;
	int32 Y = 0;
	int32 Rotation = 0;
	/** The ground its prefab covers before it is turned, in map units. */
	int32 Width = 0;
	int32 Depth = 0;
};

/** One faction's base, as a match takes it: its parcel's boundary and what stands in play on it. */
struct FSenniteMatchBaseView
{
	FString FactionId;
	FString ParcelId;
	TArray<FIntPoint> Boundary;
	TArray<FSenniteMatchStructureView> Structures;
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

	/**
	 * Whether this game is the publisher's match server: it was started with the match server's
	 * secret (-MatchSecretFile=). A match server is no player's game: it connects no wallet.
	 */
	bool IsMatchServer() const { return !MatchSecret.IsEmpty(); }

	/** What a match is assembled from: each of its factions' parcel and built structures. The match server's to ask. */
	void AskMatchBases(const FString& MatchId, TFunction<void(bool /* bOk */, const FString& /* Said */, const TArray<FSenniteMatchBaseView>&)> Done);

	/** Tell the publisher's server a battle destroyed a built structure. The match server's to say. */
	void ReportDestroyed(const FString& InFactionId, const FString& InstanceId, TFunction<void(bool /* bOk */, const FString& /* Said */)> Done);

	/** Whether this game has a place at the server, which a ticket into a match is asked with. */
	bool HasPlayer() const { return !Token.IsEmpty(); }

	/**
	 * A ticket into a match one of whose factions this player's wallet is a member of: what the game
	 * hands the match server, good once. A player's game's to ask.
	 */
	void AskMatchTicket(const FString& MatchId, TFunction<void(bool /* bOk */, const FString& /* Said */, const FString& /* Ticket */)> Done);

	/** Whose a ticket is and which of the match's factions they play for. The match server's to ask. */
	void EnterMatch(const FString& MatchId, const FString& Ticket, TFunction<void(bool /* bOk */, const FString& /* Said */, const FString& /* FactionId */, const FString& /* Wallet */)> Done);

	/** Tell the publisher's server how a match ended: the faction that won, or "void". The match server's to say. */
	void ReportOutcome(const FString& MatchId, const FString& Outcome, TFunction<void(bool /* bOk */, const FString& /* Said */)> Done);

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
	/**
	 * Hand the connect code to the player's wallet: copied, for a wallet on another device to be given;
	 * on a phone it is a link, and the DSM wallet on the same phone opens on it. False with no code yet.
	 */
	bool OpenConnectCode() const;

	/**
	 * On a phone, open the DSM Wallet app on what this game asked and is waiting for the player to approve. It is
	 * opened on the connect code this game is connected by: a wallet that is connected by that code shows what the
	 * game asked, for the player to approve or decline, and steps back to the game by itself once the player has
	 * (the wallet's own doing, for any app). With no code kept, the wallet is opened as it opens by itself. False
	 * where there is no wallet app to open: not a phone, or none on it. The owner, 2026-11-19: "when you hit
	 * fucking buy, it goes to the wallet, and it goes to where it asks you to approve" and, of a wallet opened on
	 * its home screen: "it should take you right to the approval screen. That's what the Wild state does".
	 */
	bool OpenWalletToApprove();
	/** Whether what the player asked is waiting on the player, in their wallet: it is there for them to approve. */
	bool bOnWallet = false;
	/** Whether the last opening of the wallet found a wallet app on this device to open. */
	bool bWalletHere = false;
	/** How many times the game opened the wallet app. */
	int32 WalletOpens = 0;
	/** How much ERA one trade for D-bucks takes from the wallet, in the shop and on the character select. */
	static const TCHAR* TradeEra() { return TEXT("2"); }
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

	/**
	 * What a screen that sells something says of the wallet's D-bucks: the amount the wallet proved it
	 * holds, or, with none proved, why there is none to show. bKnown is whether it is an amount.
	 */
	FString DBucksLine(bool& bKnown) const;

	/**
	 * The heroes the shop sells, and what the wallet proved it owns of what is sold as a hero is: its heroes
	 * and its guns, by the shop's ids, in one list (no gun has a hero's id). It is the list a game server is
	 * given for a hero pass too.
	 */
	TArray<FSenniteHeroForSale> ShopHeroes;
	TArray<FString> OwnedHeroes;
	bool OwnsHero(const FString& HeroId) const { return OwnedHeroes.Contains(HeroId); }

	/** The guns the shop sells, each with its price in D-bucks: sold and owned as a hero is. The game's free guns are not here. */
	TArray<FSenniteHeroForSale> ShopWeapons;
	const FSenniteHeroForSale* WeaponForSale(const FString& WeaponId) const { return ShopWeapons.FindByPredicate([&WeaponId](const FSenniteHeroForSale& W) { return W.Id == WeaponId; }); }
	/** Whether the wallet proved it owns a gun the shop sells. */
	bool OwnsWeapon(const FString& WeaponId) const { return OwnedHeroes.Contains(WeaponId); }
	/** Buy a gun from the shop with D-bucks. Until the wallet proves it owns it, it is on its way as a hero is (HeroOnItsWay). */
	void BuyWeapon(const FString& WeaponId);
	const FSenniteHeroForSale* HeroForSale(const FString& HeroId) const { return ShopHeroes.FindByPredicate([&HeroId](const FSenniteHeroForSale& H) { return H.Id == HeroId; }); }
	/** Buy a hero from the shop with D-bucks. */
	void BuyHero(const FString& HeroId);
	/** A hero the shop sold and gave that the wallet has not proved it owns yet: it is on its way. */
	FString HeroOnItsWay;

	/**
	 * What this game is called as the audience of a hero pass, as the publisher's server gave it (a digest of
	 * this game's token, never the token): empty until it has. The publisher's match server is "match".
	 */
	FString Audience;

	/** This game's audience as a game server: what it tells the players who join it to ask a hero pass for. */
	FString ServerAudience() const { return IsMatchServer() ? FString(TEXT("match")) : Audience; }

	/** Whether a string is a hero pass, or an audience, as the publisher's server makes them (32 hex digits; or "match"). */
	static bool IsHeroPass(const FString& Pass);
	static bool IsAudience(const FString& ForAudience) { return ForAudience == TEXT("match") || IsHeroPass(ForAudience); }

	/**
	 * A player's game: ask for a hero pass for the game server of that audience, which it has joined. With it
	 * that server alone asks the publisher's server, once, whose wallet this is and which heroes it proved it
	 * owns. Done(empty) if no pass was given (no wallet connected, the server unreachable).
	 */
	void AskHeroPass(const FString& ForAudience, TFunction<void(const FString& /* Pass */)> Done);

	/**
	 * A game server: ask what a hero pass a joined player showed it speaks for. Done(false) if it is no pass for
	 * this server, or the publisher's server cannot be reached.
	 */
	void CheckHeroPass(const FString& Pass, TFunction<void(bool /* bOk */, const TArray<FString>& /* Heroes */, const FString& /* SigningKey */, const FString& /* Wallet */)> Done);
	/** Trade ERA for D-bucks through the publisher's vault. */
	void BuyDBucks(const FString& InEra);

	/**
	 * Swaps through the publisher's vaults. What can be swapped is what the publisher's server reads from its
	 * account's open vaults; a price is the wallet's answer to DSM's own quote; a swap is the wallet's own trade.
	 */
	TArray<FSenniteSwapToken> SwapTokens;
	TArray<FSenniteSwapVault> SwapVaults;
	/** Whether the server has said what can be swapped. */
	bool bSwapKnown = false;
	/** Read what can be swapped, and the vaults' reserves, again. */
	void AskSwap();
	const FSenniteSwapToken* FindSwapToken(const FString& Id) const { return SwapTokens.FindByPredicate([&Id](const FSenniteSwapToken& T) { return T.Id == Id; }); }
	/** The open vault that trades these two directly, either way round; null if none does. */
	const FSenniteSwapVault* FindSwapVault(const FString& One, const FString& Other) const;
	/** What the wallet proved it holds of a token, in the token's own units; empty when it has proved nothing of it. */
	FString HoldsOf(const FSenniteSwapToken& Token) const;
	/** The last price the wallet gave, and the swap it is the price of. */
	FSenniteSwapPrice SwapPrice;
	/** Whether the last price is the price of this swap. */
	bool PricedFor(const FString& From, const FString& To, const FString& Amount) const { return !SwapPrice.From.IsEmpty() && SwapPrice.From == From && SwapPrice.To == To && SwapPrice.Amount == Amount; }
	/** Ask the wallet what an amount of one object would get in another now. */
	void AskSwapPrice(const FString& From, const FString& To, const FString& Amount);
	/** Swap, for no less than a little under the price last given, which must be the price of this swap. */
	void Swap(const FString& From, const FString& To, const FString& Amount);
	/** Buy a skin from the shop with D-bucks. */
	void BuySkin(const FString& SkinId);
	/** Wear an owned skin, or with an empty id, none. */
	void Wear(const FString& SkinId);

	/** What the wallet has won in matches and not collected yet, as the server last read it. */
	TArray<FSennitePrize> Prizes;
	/** How many times the prizes have been read from the server. */
	int32 PrizesRead = 0;

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

	/**
	 * Ask the server for the shop again: what it sells now, as the store is opened. A game asked once when it
	 * started, and kept that list for as long as it ran, so a server restarted with new things for sale went
	 * unseen until the game was started again. The list shown stays until the new one comes.
	 */
	void RefreshShop();

private:
	using FAnswer = TFunction<void(int32 /* Status */, const TSharedPtr<FJsonObject>& /* Json */)>;

	bool Tick(float DeltaTime);
	/** Bearer, when given, is sent in place of the player's token. Patience, when given, is how many seconds the answer is waited for. */
	void Send(const TCHAR* Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, FAnswer Done, const FString& Bearer = FString(), float Patience = 0.0f);
	/** Send something the wallet has to answer; the server names a request to ask about. */
	void Ask(const FString& Path, const TSharedPtr<FJsonObject>& Body, const TCHAR* Waiting);
	void SendChange(const TSharedRef<FJsonObject>& Change);
	void SendChangeTo(const FString& InFactionId, const TSharedRef<FJsonObject>& Change, const TCHAR* Waiting);
	void AskFree();
	void AskPrizes();
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
	/** The match server's secret, when this game was started with one. */
	FString MatchSecret;
	FString PendingRequest;
	bool bRequestAsked = false;
	/** Ask the wallet for a payment or a trade: as Ask, and on a phone the wallet app is opened for the player to approve it, once the server has the request. */
	void AskToPay(const FString& Path, const TSharedPtr<FJsonObject>& Body, const TCHAR* Waiting);
	bool bOpenWalletWhenAsked = false;
	/** What is said once the server has taken that request: that it waits in the wallet for the player. */
	FString WordsWhenAsked;
	/** On a phone, the DSM Wallet app as it opens by itself: for a game that kept no connect code. */
	bool OpenWalletPlain();
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
	double NextPrizes = 0.0;
	bool bPrizesAsked = false;
	bool bShop = false;
	bool bShopAsked = false;
	/** The game's currency, by its symbol, as the shop gave it. */
	FString CurrencySymbol;
	bool bSwapAsked = false;
	/** The request that has not settled yet is a price, or a swap. */
	bool bPricing = false;
	bool bSwapping = false;
	/** When the wallet is next asked to prove what it holds again: a proof older than an hour answers no hero pass. */
	double NextReprove = 0.0;

	/** The skin, or the hero, being bought by the request that has not settled yet. */
	FString BuyingSkin;
	FString BuyingHero;
	double NextArrival = 0.0;
	int32 ArrivalProofs = 0;
	/** Whether the wallet has been asked what it holds since it connected. */
	bool bProvedOnConnect = false;
};
