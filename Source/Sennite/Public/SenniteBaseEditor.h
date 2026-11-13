// Faction bases in the world, and the editor a faction's members plan theirs in (spec 2.3, 7.6).
//
// In play, every machine draws the structures the publisher's server says stand on each parcel:
// solid, for everyone. B opens the editor: a planning view from above the parcel, where a member
// sees the faction's plans too, places a prefab from the publisher's catalogue, commits materials
// from their wallet to a plan, takes them back, replaces, dismantles and rebuilds.
//
// Nothing here decides anything. A placement is a change the member's own wallet signs; a
// commitment is a vault the wallet locks; whether a plan stands in play is the publisher's server's
// answer. The game sends the choice and draws what comes back.
//
// Keys in the editor: W A S D move the cursor one grid cell, Q and E choose the prefab, R turns it,
// F places it, N founds a faction on an unassigned parcel, Tab goes to the next parcel. On a
// structure: C commits, 1 chooses which resource, Z how much, T takes back, V replaces it with the
// chosen prefab, X dismantles, G rebuilds one a battle destroyed. O copies the connect code, and M
// has the wallet prove what it holds. J accepts a faction's invitation.
//
// P opens the faction's people: its members, those it has invited, and the players in no faction.
// W and S choose one; I invites, X withdraws an invitation, K votes to remove a member, U takes the
// vote back, and L, on yourself, leaves the faction.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "SenniteBaseEditor.generated.h"

class ACameraActor;
class AHUD;
class ASenniteStructure;
class UCanvas;
class UInputComponent;
class UStaticMeshComponent;
class USennitePublisher;
struct FSenniteBaseView;
struct FSennitePerson;
struct FSennitePrefab;
struct FSenniteStructureView;

UCLASS()
class SENNITE_API ASenniteBaseEditor : public AActor
{
	GENERATED_BODY()

public:
	ASenniteBaseEditor();

	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** The editor of a world, if the world has one. */
	static ASenniteBaseEditor* Find(const UWorld* World);

	/** Draw the editor's panel. Called by the game's HUD each frame. */
	static void DrawOn(AHUD* Hud, UCanvas* Canvas);

	/** Whether the planning view is open in a world: the fight's own HUD is not drawn over it. */
	static bool IsPlanning(const UWorld* World);

	/** Whether the planning view is open. */
	bool bEditing = false;
	/** The parcel being planned. */
	FString Parcel;
	/** The near corner of the grid cell the cursor is on, in map units. */
	FIntPoint Cursor = FIntPoint::ZeroValue;
	int32 PrefabIndex = 0;
	/** Quarter turns of the prefab to place, in degrees. */
	int32 Rotation = 0;
	/** Which of the selected structure's unmet needs the commit button commits to. */
	int32 NeedIndex = 0;
	/** Which amount the commit button commits: all that is needed, or a part. */
	int32 AmountIndex = 0;

	/** Whether the panel shows the faction's people instead of the parcel. */
	bool bPeople = false;
	/** Which person is chosen on the people page. */
	int32 PersonIndex = 0;

	/** One row of the people page: a member, someone invited, or a player in no faction. */
	struct FPersonRow
	{
		enum class EKind { Member, Invited, Free };
		EKind Kind = EKind::Member;
		const FSennitePerson* Person = nullptr;
	};
	/** The people page's rows, in the order drawn. */
	TArray<FPersonRow> PeopleRows() const;
	/** The row chosen, or null when there is none. */
	bool ChosenPerson(FPersonRow& Row) const;

	/** Each line the last frame drew, as "name=text". */
	TArray<FString> LastDrawn;

	/** The structure the cursor is on, if any. */
	const FSenniteStructureView* Selected() const;
	/** The prefab chosen to place. */
	const FSennitePrefab* Chosen() const;
	/** The structures drawn in the world now, by instance_id. */
	const TMap<FString, ASenniteStructure*>& Drawn() const { return Structures; }

private:
	USennitePublisher* Publisher() const;
	const FSenniteBaseView* Base() const;

	void BindToggle();
	void Open();
	void Close();
	void Toggle();
	void MoveUp();
	void MoveDown();
	void MoveLeft();
	void MoveRight();
	void Move(int32 DX, int32 DY);
	void NextPrefab();
	void PreviousPrefab();
	void Turn();
	void Place();
	void Found();
	void NextParcel();
	void Commit();
	void NextNeed();
	void NextAmount();
	void TakeBack();
	void ReplaceSelected();
	void Dismantle();
	void Rebuild();
	void CopyCode();
	void ProveMaterials();
	void TogglePeople();
	void Join();
	void InvitePerson();
	void VotePerson();
	void UnvotePerson();
	void LeaveFaction();

	void ChooseParcel(const FString& Id);
	void SyncStructures();
	void UpdateGhost();
	void UpdateCursorTile();
	void BuildOutline();
	void ClearOutline();
	double GroundAt(double X, double Y) const;
	/** What the commit button would commit now: the resource and the amount; false when nothing is needed. */
	bool CommitChoice(FString& Resource, int32& Amount) const;

	UPROPERTY()
	UInputComponent* ToggleInput = nullptr;

	UPROPERTY()
	UInputComponent* EditorInput = nullptr;

	UPROPERTY()
	ACameraActor* Camera = nullptr;

	UPROPERTY()
	TMap<FString, ASenniteStructure*> Structures;

	/** What each drawn structure was built from: rebuilt only when it changes. */
	TMap<FString, FString> Signatures;

	UPROPERTY()
	ASenniteStructure* Ghost = nullptr;

	FString GhostSignature;

	UPROPERTY()
	TArray<UStaticMeshComponent*> Outline;

	/** The grid cell the cursor is on, marked on the ground. */
	UPROPERTY()
	UStaticMeshComponent* CursorTile = nullptr;
};

/** Puts the base editor in every game world when a publisher's server is set. */
UCLASS()
class SENNITE_API USenniteBaseWorld : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
};
