// The factions' bases in a match.
//
// A match assembles its own arena (SenniteArena.h). The publisher's match server asks the
// publisher's server what the match is assembled from: for each faction playing, its parcel's
// boundary and its built structures, read from that faction's latest state. It sets each parcel on
// one base of the arena, in the order the match names its factions, and sends where every structure
// stands to every machine in the match; each machine puts them in its own world.
//
// A structure shot down in the match is gone for everyone in it, and the match server tells the
// publisher's server, which records it: the only thing of a match that outlasts it. Nothing here
// reaches DSM.
//
// The match is named on the map's address (?Match=<id>). The match server is the game started with
// the match server's secret (-MatchSecretFile=) and the publisher's server (-PublisherUrl=).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SenniteMatchBases.generated.h"

class ASenniteStructure;
struct FSenniteMatchBaseView;

/** One built structure of a faction's base, where the match has set it. */
USTRUCT()
struct FSenniteMatchStructure
{
	GENERATED_BODY()

	/** Which base of the arena it stands on: the place of its faction among the match's factions. */
	UPROPERTY()
	int32 Base = 0;

	UPROPERTY()
	FString FactionId;

	UPROPERTY()
	FString InstanceId;

	UPROPERTY()
	FString PrefabId;

	/** Where it stands in the arena. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	float Yaw = 0.0f;

	/** The ground its prefab covers before it is turned, from the publisher's catalogue. */
	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Depth = 0;

	/** Shot down in this match. */
	UPROPERTY()
	bool bDestroyed = false;
};

UENUM()
enum class EMatchBasesState : uint8
{
	/** The match server has not heard from the publisher's server yet. */
	Asking,
	/** The bases are set on the arena. */
	Placed,
	/** The match has no bases; Why says why. */
	Failed
};

UCLASS(Config = Game)
class SENNITE_API ASenniteMatchBases : public AActor
{
	GENERATED_BODY()

public:
	ASenniteMatchBases();

	/** The match's bases in a world, or none when the world is not a match's. */
	static ASenniteMatchBases* Find(const UWorld* World);

	/** What a structure takes before it is down. A rule of the game; set in DefaultGame.ini. */
	UPROPERTY(Config)
	float StructureHealth = 300.0f;

	UPROPERTY(Replicated)
	FString MatchId;

	UPROPERTY(ReplicatedUsing = OnRep_Structures)
	TArray<FSenniteMatchStructure> Structures;

	UPROPERTY(ReplicatedUsing = OnRep_Structures)
	EMatchBasesState State = EMatchBasesState::Asking;

	UPROPERTY(Replicated)
	FString Why;

	/** How many destroyed structures the publisher's server has recorded. On the match server. */
	int32 Recorded = 0;

	/** The structure this machine built for an instance, while it stands. */
	ASenniteStructure* ActorOf(const FString& InstanceId) const;

	/** A shot landed on a structure. On the match server. */
	void Hit(const ASenniteStructure* Structure, float Damage);

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_Structures();

private:
	void Ask();
	void Place(const TArray<FSenniteMatchBaseView>& Bases);
	void Fail(const FString& InWhy);
	/** Make this machine's world agree with Structures. */
	void Sync();
	void Report(const FString& FactionId, const FString& InstanceId, int32 Tries);

	UPROPERTY()
	TMap<FString, TObjectPtr<ASenniteStructure>> Actors;

	/** What each structure can still take. On the match server. */
	TMap<FString, float> Health;

	int32 Asked = 0;
};
