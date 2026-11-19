---------------------------- MODULE FactionSupply ----------------------------
(***************************************************************************)
(* Resources and contributions (spec 8), for the third item of spec 12.4:  *)
(* no sequence of contribute, withdraw, leave, replace and dismantle       *)
(* creates or destroys a unit; and, of the fourth, that nobody but the     *)
(* one a vault pays ever receives its units.                               *)
(*                                                                         *)
(* One resource is modelled: an escrow vault holds one object, and the     *)
(* argument is the same for each.                                          *)
(*                                                                         *)
(* What is taken from DSM and not shown here: creating a vault debits its  *)
(* owner; a vault is released whole, once, after a verdict, to the         *)
(* identity its outcome pays. `Contribute` and `Release` are those rules.  *)
(* A contribution's two outcomes both pay its owner (spec 8.2), so which   *)
(* key decided, the owner's or the publisher's, changes when the units go  *)
(* back and never to whom: `Decide` is one step for either.                *)
(*                                                                         *)
(* The faction's state moves freely here (anyone leaves, is removed, joins *)
(* at any time; a structure is placed, replaced, dismantled at any time):  *)
(* more than the lineage allows, so what holds here holds for it.          *)
(***************************************************************************)
EXTENDS Integers, Sequences, FiniteSets

CONSTANTS
  Wallets,
  Founders,    \* the wallets that are members at the start
  Instances,
  Prefabs,     \* a prefab is modelled by what its recipe needs of the resource
  Start,       \* what each wallet holds at the start
  Amounts,     \* the amounts committed or sent in one step
  MaxVaults,
  Broken       \* "none", or the one rule this run leaves out

VARIABLES
  bal,       \* each wallet's available units
  vaults,    \* every vault created, in order: owner, whom it pays, the structure, amount, state
  members,
  leavers,   \* those who left by their own change and whose 48-hour notice has not run
  placed,    \* what each instance's prefab needs, or Nothing while none is placed
  claimed    \* the structures someone says they funded, with no vault to show for it

vars == <<bal, vaults, members, leavers, placed, claimed>>

RECURSIVE Sum(_, _)
Sum(f, S) == IF S = {} THEN 0 ELSE LET x == CHOOSE x \in S : TRUE IN f[x] + Sum(f, S \ {x})

Ids == DOMAIN vaults
Locked == [v \in Ids |-> IF vaults[v].active THEN vaults[v].amt ELSE 0]
Supply == Start * Cardinality(Wallets)
Largest == CHOOSE p \in Prefabs : \A q \in Prefabs : q <= p
Nothing == 0

(***************************************************************************)
(* The funding read (spec 8.3; Contributions.funding in contributions.ts): *)
(* a vault counts when it is on the structure's cell, its terms are a      *)
(* contribution's, it is Active, and its owner is a member, or left by     *)
(* their own change with the notice still running.                         *)
(***************************************************************************)
Counts(v, i) ==
  /\ vaults[v].inst = i
  /\ vaults[v].pays = vaults[v].owner
  /\ vaults[v].active
  /\ vaults[v].owner \in members \cup leavers

Committed(i) ==
  Sum([v \in Ids |-> IF Counts(v, i) THEN vaults[v].amt ELSE 0], Ids)
    + (IF Broken = "a claim counts as funding" /\ i \in claimed THEN Largest ELSE 0)

Min(a, b) == IF a < b THEN a ELSE b
Funded(i) == Min(Committed(i), placed[i])

\* A wallet locks units in a vault on a structure's cell. With contribution terms it pays its
\* owner; a vault on the same cell with other terms pays someone else, and never counts.
Contribute(w, i, n, pays) ==
  /\ Len(vaults) < MaxVaults
  /\ bal[w] >= n
  /\ vaults' = Append(vaults, [owner |-> w, pays |-> pays, inst |-> i, amt |-> n,
                               active |-> TRUE, decided |-> FALSE])
  /\ bal' = IF Broken = "a contribution leaves the units in the wallet" THEN bal
            ELSE [bal EXCEPT ![w] = @ - n]
  /\ UNCHANGED <<members, leavers, placed, claimed>>

\* A verdict on the vault's cell: the owner's withdrawal, or the publisher's return.
Decide(v) ==
  /\ ~vaults[v].decided
  /\ vaults' = [vaults EXCEPT ![v].decided = TRUE]
  /\ UNCHANGED <<bal, members, leavers, placed, claimed>>

\* `r` releases the vault. DSM releases it only to the identity its outcome pays, once.
Release(v, r) ==
  /\ vaults[v].decided
  /\ vaults[v].active \/ Broken = "a vault releases twice"
  /\ r = vaults[v].pays \/ Broken = "a release pays whoever releases"
  /\ vaults' = [vaults EXCEPT ![v].active = FALSE]
  /\ bal' = [bal EXCEPT ![r] = @ + vaults[v].amt]
  /\ UNCHANGED <<members, leavers, placed, claimed>>

\* A trade: one wallet sends another units it holds (spec 10.2).
Give(w1, w2, n) ==
  /\ w1 # w2
  /\ \/ bal[w1] >= n
     \/ /\ Broken = "a wallet sends units it has committed"
        /\ \E v \in Ids : vaults[v].owner = w1 /\ vaults[v].active /\ bal[w1] + vaults[v].amt >= n
  /\ bal' = [bal EXCEPT ![w1] = @ - n, ![w2] = @ + n]
  /\ UNCHANGED <<vaults, members, leavers, placed, claimed>>

Leave(w) ==
  /\ w \in members
  /\ members' = members \ {w}
  /\ leavers' = leavers \cup {w}
  /\ UNCHANGED <<bal, vaults, placed, claimed>>

NoticeRuns(w) ==
  /\ w \in leavers
  /\ leavers' = leavers \ {w}
  /\ UNCHANGED <<bal, vaults, members, placed, claimed>>

Removed(w) ==
  /\ w \in members
  /\ members' = members \ {w}
  /\ UNCHANGED <<bal, vaults, leavers, placed, claimed>>

Joins(w) ==
  /\ w \notin members
  /\ members' = members \cup {w}
  /\ leavers' = leavers \ {w}
  /\ UNCHANGED <<bal, vaults, placed, claimed>>

\* Place, replace (p differs) and dismantle (p = Nothing): the instance, and so its cell, stay.
Build(i, p) ==
  /\ placed[i] # p
  /\ placed' = [placed EXCEPT ![i] = p]
  /\ UNCHANGED <<bal, vaults, members, leavers, claimed>>

\* Someone says a structure is funded and shows no vault.
Claim(i) ==
  /\ i \notin claimed
  /\ claimed' = claimed \cup {i}
  /\ UNCHANGED <<bal, vaults, members, leavers, placed>>

Init ==
  /\ bal = [w \in Wallets |-> Start]
  /\ vaults = <<>>
  /\ members = Founders
  /\ leavers = {}
  /\ placed = [i \in Instances |-> Nothing]
  /\ claimed = {}

Next ==
  \/ \E w \in Wallets, i \in Instances, n \in Amounts, pays \in Wallets : Contribute(w, i, n, pays)
  \/ \E v \in Ids : Decide(v)
  \/ \E v \in Ids, r \in Wallets : Release(v, r)
  \/ \E w1, w2 \in Wallets, n \in Amounts : Give(w1, w2, n)
  \/ \E w \in Wallets : Leave(w) \/ NoticeRuns(w) \/ Removed(w) \/ Joins(w)
  \/ \E i \in Instances, p \in Prefabs \cup {Nothing} : Build(i, p)
  \/ \E i \in Instances : Claim(i)

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
(***************************************************************************)
(* What is checked.                                                        *)
(***************************************************************************)

\* 12.4, third item: every unit is in a wallet or in an Active vault, and there are as many as
\* at the start.
Conserved == Sum(bal, Wallets) + Sum(Locked, Ids) = Supply

\* A unit is never in two places: no wallet ever sends what it has locked.
NothingSpentTwice == \A w \in Wallets : bal[w] >= 0

\* A vault's units go to the one it pays and to nobody else, and a released vault stays released.
ReleasePaysWhomItNames ==
  [][\A v \in Ids :
       /\ ~vaults[v].active => ~vaults'[v].active
       /\ vaults[v].active /\ ~vaults'[v].active =>
            \A w \in Wallets :
              bal'[w] = bal[w] + (IF w = vaults[v].pays THEN vaults[v].amt ELSE 0)]_vars

\* What a structure is funded by is in Active vaults on its cell: a claim funds nothing.
FundingIsInVaults ==
  \A i \in Instances :
    placed[i] # Nothing =>
      Funded(i) <= Sum([v \in Ids |-> IF vaults[v].inst = i THEN Locked[v] ELSE 0], Ids)

-----------------------------------------------------------------------------
(***************************************************************************)
(* Reachability. Each of these is given to TLC as an invariant and must be *)
(* violated: the run it prints is the case happening in this model.        *)
(***************************************************************************)

FullyFunded(i) == placed[i] # Nothing /\ Funded(i) = placed[i]
ReleasedOn(i) == \E v \in Ids : vaults[v].inst = i /\ ~vaults[v].active

NothingFullyFunded == \A i \in Instances : ~FullyFunded(i)
\* a withdrawal leaves a standing structure short (acceptance 5)
NoShortfallAfterWithdrawal ==
  \A i \in Instances : ~(placed[i] # Nothing /\ Funded(i) < placed[i] /\ ReleasedOn(i))
\* another member's contribution restores it (acceptance 6)
NoRestoredAfterWithdrawal == \A i \in Instances : ~(FullyFunded(i) /\ ReleasedOn(i))
\* a leaver's vault still counts while the notice runs
NoLeaverCounting ==
  \A i \in Instances : \A v \in Ids : ~(Counts(v, i) /\ vaults[v].owner \in leavers)
\* a member who is gone (removed, or left and the notice ran) still has an Active vault: it counts for nothing
NoGoneMemberVault ==
  \A v \in Ids :
    ~(/\ vaults[v].active
      /\ vaults[v].pays = vaults[v].owner
      /\ vaults[v].owner \notin members \cup leavers
      /\ vaults[v].owner \in Founders)
\* a wallet commits units another wallet sent it (spec 10.2)
NoCommitOfReceivedUnits == \A v \in Ids : vaults[v].amt <= Start
\* everything committed has gone back
NotAllReturned ==
  ~(Len(vaults) = MaxVaults /\ \A v \in Ids : ~vaults[v].active /\ vaults[v].pays = vaults[v].owner)

=============================================================================
