// A match between factions: who plays for whom, the score, and how it ends.
//
// A match is played in an arena assembled for it (SenniteArena.h, SenniteMatchBases.h), on the
// publisher's match server: a game with no player of its own, started with the match on the map's
// address (?Match=<id>), the publisher's server (-PublisherUrl=) and the match server's secret
// (-MatchSecretFile=).
//
// A player plays for the faction their wallet is a member of. Their game asks the publisher's
// server for a ticket into the match and hands it to the match server, which asks the publisher's
// server whose it is. Until then the player has no hero. A hero starts on its faction's base, and
// one that falls returns there a little later with nothing it picked up.
//
// A match ends in one of three ways:
//   a faction reaches the kills to win;
//   the time runs out: the faction with the most kills wins, and nobody does when the most is shared;
//   a faction's base is wiped, every structure of it destroyed: that faction is out, and the last
//   faction left wins.
// The match server tells the publisher's server who won, or that nobody did, and the publisher's
// key decides the prize (spec 10.6). Nothing here reaches DSM.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SenniteMatch.generated.h"

class ASenniteCharacter;
class ASennitePlayerController;

/** One faction of a match. */
USTRUCT()
struct FSenniteMatchFaction
{
	GENERATED_BODY()

	UPROPERTY()
	FString FactionId;

	/** Which base of the arena is its own: its place among the match's factions. */
	UPROPERTY()
	int32 Base = 0;

	/** Heroes of other factions its players have brought down. */
	UPROPERTY()
	int32 Kills = 0;

	/** How many of its players are in the match. */
	UPROPERTY()
	int32 Players = 0;

	/** Its base was wiped: it is out of the match. */
	UPROPERTY()
	bool bOut = false;
};

UENUM()
enum class EMatchState : uint8
{
	/** The bases are not set yet, or players of two factions are not in yet. Nothing counts. */
	Waiting,
	Playing,
	Over,
	/** The match cannot be played; Why says why. */
	Failed
};

/** Whether a match is over, who won ("void": nobody) and why, in words a player reads. */
struct FSenniteMatchVerdict
{
	bool bOver = false;
	FString Outcome;
	FString Why;
};

UCLASS(Config = Game)
class SENNITE_API ASenniteMatch : public AActor
{
	GENERATED_BODY()

public:
	ASenniteMatch();

	/** The match of a world, or none when the world is not a match's. */
	static ASenniteMatch* Find(const UWorld* World);

	/** The outcome that pays nobody: the match had no winner. */
	static const TCHAR* Void() { return TEXT("void"); }

	/**
	 * The rules of how a match ends, on their own. KillsToWin and whether the time has run out are
	 * given; a KillsToWin of nothing means no number of kills ends the match.
	 */
	static FSenniteMatchVerdict Judge(const TArray<FSenniteMatchFaction>& Factions, int32 KillsToWin, bool bTimeUp);

	/** The first faction to this many kills wins. Nothing: kills end no match. A rule of the game; set in DefaultGame.ini. */
	UPROPERTY(Config)
	int32 KillsToWin = 10;

	/** How long a match is played, from when players of two factions are in. Nothing: no time limit. Set in DefaultGame.ini. */
	UPROPERTY(Config)
	float MatchSeconds = 600.0f;

	/** How long after a hero falls it returns on its faction's base. Set in DefaultGame.ini. */
	UPROPERTY(Config)
	float RespawnSeconds = 5.0f;

	/** How long a game that joined is given to show a ticket before it is put out. Set in DefaultGame.ini. */
	UPROPERTY(Config)
	float EnterSeconds = 60.0f;

	/** How long after the publisher's server recorded the outcome the match server closes. Set in DefaultGame.ini. */
	UPROPERTY(Config)
	float CloseSeconds = 30.0f;

	UPROPERTY(Replicated)
	FString MatchId;

	UPROPERTY(Replicated)
	TArray<FSenniteMatchFaction> Factions;

	UPROPERTY(Replicated)
	EMatchState State = EMatchState::Waiting;

	/** The bases are set on the arena: a game can ask to enter. */
	UPROPERTY(Replicated)
	bool bBases = false;

	/** The kills to win and the length of the match, as the match server has them. */
	UPROPERTY(Replicated)
	int32 Target = 0;

	UPROPERTY(Replicated)
	float Length = 0.0f;

	/** When the time runs out, by the match server's clock, which every machine in the match follows. */
	UPROPERTY(Replicated)
	double EndsAt = 0.0;

	/** Who won, or "void"; empty until the match is over. */
	UPROPERTY(Replicated)
	FString Outcome;

	/** Why the match is over, or why it cannot be played. */
	UPROPERTY(Replicated)
	FString Why;

	/** The publisher's server has recorded the outcome. */
	UPROPERTY(Replicated)
	bool bRecorded = false;

	/** On this machine: what was last said about this player entering the match. */
	FString Said;

	const FSenniteMatchFaction* FactionOf(const FString& FactionId) const;

	/** How long the match still has; nothing when it has no time limit or is not being played. */
	double SecondsLeft() const;

	/** Where a faction's heroes start: on its base, clear of what is built there. On the match server. */
	FTransform StartOf(int32 Base) const;

	/** Whether a shot by Shooter counts against something of a faction: only while playing, across factions that are still in. */
	bool Harms(const ASenniteCharacter* Shooter, const FString& TargetFaction) const;

	// Everything below is the match server's.

	/** The bases are set: the match's factions in order, and where each built structure stands with the ground it covers. */
	void BasesPlaced(const TArray<FString>& InFactions, const TArray<TPair<FVector, float>>& Built);

	/** The match cannot be played. */
	void Fail(const FString& InWhy);

	/** A game that joined shows a ticket. */
	void Enter(ASennitePlayerController* Player, const FString& Ticket);

	/** A player left. */
	void Left(AController* Player);

	/**
	 * A hero fell. True when the match takes its player from here (they return on their base, or
	 * watch because their faction is out); false when the match has nothing to say.
	 */
	bool Fell(ASenniteCharacter* Victim, ASenniteCharacter* Killer);

	/** Every structure of a faction's base is destroyed. */
	void BaseWiped(const FString& FactionId);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	FSenniteMatchFaction* Find(const FString& FactionId);
	double ServerSeconds() const;
	/** Put a player's hero on their faction's base. */
	void Spawn(ASennitePlayerController* Player);
	/** Start the clock once players of two factions are in. */
	void MaybeStart();
	/** Hold the match against its rules, and end it when they say so. */
	void JudgeNow();
	void End(const FString& InOutcome, const FString& InWhy);
	void Report(int32 Tries);
	/** On a player's machine: ask for a ticket and show it to the match server. */
	void AskToEnter();

	/** Where each base's heroes start. */
	TArray<FTransform> Starts;

	/** A player's machine: a ticket is being asked for, when to ask next, and how many were asked for. */
	bool bAsking = false;
	double NextAsk = 0.0;
	int32 Asked = 0;
};
