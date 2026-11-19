--------------------------- MODULE FactionLineage ---------------------------
(***************************************************************************)
(* A faction's lineage (spec 6 and 7), for the first, second and fourth    *)
(* items of spec 12.4:                                                     *)
(*                                                                         *)
(*   - at most one successor per faction state;                            *)
(*   - any two mirrors that have read the same cells hold the same state;  *)
(*   - a host, a non-member and a stale writer can each cause delay and    *)
(*     nothing else.                                                       *)
(*                                                                         *)
(* What is taken from DSM and not shown here: a keyed cell is held by the  *)
(* first recognized write to arrive at its leader, and a seat never        *)
(* alters, reorders or loses what it holds (spec 6.7, the storage fault    *)
(* model). `Arrive` is that rule. Everything else is the game's: what a    *)
(* cell's writers are, what a mirror takes from a cell, and the validity   *)
(* rules of Server/src/faction/validity.ts.                                *)
(*                                                                         *)
(* R_n is modelled as the occupants of the cells walked from R_0, in       *)
(* order: the real R_n+1 is a digest over R_n, the occupant's step and the *)
(* resulting state, and is taken to name them one to one.                  *)
(*                                                                         *)
(* There is no host in this model because the host holds nothing: a write  *)
(* is signed for its cell, so carrying one is `Arrive` and `Finalize`, and *)
(* not carrying one is no step at all. What a host sends to a mirror is    *)
(* `HostOffers`, which a mirror never takes.                               *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS
  Keys,         \* every signing key in the run, as numbers: the order of keys is the order of numbers
  Founders,     \* the members R_0 lists
  Instances,    \* the instance ids a base may hold; one stands for one spot on the parcel
  Prefabs,
  MaxMembers,   \* the most members and invited a state lists together
  Kinds,        \* the kinds of change written in this run
  Mirrors,      \* the machines that each walk the lineage
  MaxHeight,    \* how many successor cells the run goes through
  MaxOutages,   \* how many times a leader seat becomes unreachable
  Honest,       \* the members whose own writes and carrying are fair (liveness only)
  Track,        \* whether writes that count for nothing are remembered, for the reachability runs
  None,
  Broken        \* "none", or the one rule this run leaves out

VARIABLES
  occ,       \* each known cell's occupant, or None while it is open
  final,     \* the cells whose occupant is Final
  down,      \* the cells whose leader seat is unreachable now
  outages,
  mirror,    \* each mirror: R_n as the occupants it walked, and the state it holds
  noops      \* which kinds of write have counted for nothing so far

vars == <<occ, final, down, outages, mirror, noops>>

Lowest(S) == CHOOSE k \in S : \A j \in S : k <= j

(***************************************************************************)
(* The committed state (state.ts) and the kinds of change (change.ts).     *)
(* A vote is <<voter, member voted on>>.                                   *)
(***************************************************************************)
First ==
  [members |-> Founders, invited |-> {}, placed |-> [i \in Instances |-> None], votes |-> {}]

Junk == [kind |-> "junk"]

Changes ==
  [kind : {"place", "replace"}, inst : Instances, prefab : Prefabs]
    \cup [kind : {"dismantle", "confirm"}, inst : Instances]
    \cup [kind : {"leave", "accept", "junk"}]
    \cup [kind : {"invite", "uninvite", "vote", "unvote"}, key : Keys]

(***************************************************************************)
(* settled() in validity.ts: a member is removed when more than half of    *)
(* the other members have voted to remove them; the count is repeated      *)
(* until nothing changes, lowest key first.                                *)
(***************************************************************************)
RECURSIVE Settle(_, _)
Settle(members, votes) ==
  LET standing == { v \in votes : v[1] \in members /\ v[2] \in members /\ v[1] # v[2] }
      out == { t \in members : Cardinality({ v \in standing : v[2] = t }) * 2 > Cardinality(members) - 1 }
  IN IF out = {} THEN [members |-> members, votes |-> standing]
     ELSE Settle(members \ {Lowest(out)}, standing)

Settled(s) ==
  LET r == Settle(s.members, s.votes) IN [s EXCEPT !.members = r.members, !.votes = r.votes]

Yes(n) == [valid |-> TRUE, next |-> n]
No(s) == [valid |-> FALSE, next |-> s]

\* applied() in validity.ts. Placement geometry is one rule here: a spot holds one structure.
Applied(s, k, c) ==
  CASE c.kind = "place" ->
         IF s.placed[c.inst] = None THEN Yes([s EXCEPT !.placed[c.inst] = c.prefab]) ELSE No(s)
    [] c.kind = "replace" ->
         IF s.placed[c.inst] # None THEN Yes([s EXCEPT !.placed[c.inst] = c.prefab]) ELSE No(s)
    [] c.kind = "dismantle" ->
         IF s.placed[c.inst] # None THEN Yes([s EXCEPT !.placed[c.inst] = None]) ELSE No(s)
    [] c.kind = "confirm" ->
         IF s.placed[c.inst] # None THEN Yes(s) ELSE No(s)
    [] c.kind = "leave" ->
         Yes(Settled([s EXCEPT !.members = @ \ {k}]))
    [] c.kind = "invite" ->
         IF /\ c.key \notin s.members
            /\ c.key \notin s.invited
            /\ Cardinality(s.members \cup s.invited) < MaxMembers
           THEN Yes([s EXCEPT !.invited = @ \cup {c.key}]) ELSE No(s)
    [] c.kind = "uninvite" ->
         IF c.key \in s.invited THEN Yes([s EXCEPT !.invited = @ \ {c.key}]) ELSE No(s)
    [] c.kind = "vote" ->
         IF c.key # k /\ c.key \in s.members /\ <<k, c.key>> \notin s.votes
           THEN Yes(Settled([s EXCEPT !.votes = @ \cup {<<k, c.key>>}])) ELSE No(s)
    [] c.kind = "unvote" ->
         IF <<k, c.key>> \in s.votes THEN Yes([s EXCEPT !.votes = @ \ {<<k, c.key>>}]) ELSE No(s)
    [] OTHER -> No(s)

(***************************************************************************)
(* judge() in validity.ts: the verdict on an occupant signed by `k`. An    *)
(* invited wallet may write its acceptance and nothing else; anything else *)
(* it writes changes nothing and uses its invitation up.                   *)
(***************************************************************************)
Judge(s, k, c) ==
  LET asMember == k \in s.members \/ (Broken = "an invited wallet writes as a member" /\ k \in s.invited)
      v == IF c.kind = "accept"
             THEN IF k \in s.invited
                    THEN Yes([s EXCEPT !.members = @ \cup {k}, !.invited = @ \ {k}])
                    ELSE No(s)
             ELSE IF asMember THEN Applied(s, k, c) ELSE No(s)
  IN IF ~v.valid /\ k \in s.invited THEN No([s EXCEPT !.invited = @ \ {k}]) ELSE v

\* The state a lineage arrives at: every mirror computes it the same way from the same occupants.
RECURSIVE StateAt(_)
StateAt(com) ==
  IF com = <<>> THEN First
  ELSE LET w == com[Len(com)]
       IN Judge(StateAt(SubSeq(com, 1, Len(com) - 1)), w.signer, w.change).next

\* The successor cell's writers (writerKeys in state.ts): the members and the invited R_n lists.
Writers(s) == s.members \cup s.invited

\* Spec 6.3: a write is recognized at the successor cell of R_n only under a key R_n lists.
Recognized(s, k) == k \in Writers(s) \/ Broken = "a non-member's write is recognized"

Occupant(c) == IF c \in DOMAIN occ THEN occ[c] ELSE None

\* The cells anyone in the run can name: those a mirror stands at, and those already written to.
Targets == { mirror[m].com : m \in Mirrors } \cup DOMAIN occ

\* What a writer of R_n writes: a change that is valid there, or one that is not (`Junk` stands
\* for every invalid one: each leaves the state as it was).
Offered(s, k) ==
  IF k \in Writers(s)
    THEN { c \in Changes : c.kind \in Kinds /\ (c.kind = "junk" \/ Judge(s, k, c).valid) }
    ELSE {Junk}

(***************************************************************************)
(* A write signed by `k` for cell `c` arrives at the cell's leader. The    *)
(* first recognized one holds the cell. Any other counts for nothing: a    *)
(* non-member's, the loser of a race, a stale writer's at a cell the       *)
(* lineage has passed.                                                     *)
(***************************************************************************)
Arrive(k, c, ch) ==
  /\ c \in Targets
  /\ Len(c) < MaxHeight
  /\ c \notin down
  /\ LET s == StateAt(c)
         takes == /\ Recognized(s, k)
                  /\ (Occupant(c) = None \/ Broken = "the last arrival holds the cell")
     IN /\ ch \in Offered(s, k)
        /\ IF takes
             THEN /\ occ' = [x \in DOMAIN occ \cup {c} |->
                               IF x = c THEN [signer |-> k, change |-> ch] ELSE occ[x]]
                  /\ UNCHANGED noops
             ELSE /\ UNCHANGED occ
                  /\ noops' = IF ~Track THEN noops
                              ELSE noops \cup
                                {IF ~Recognized(s, k) THEN "non-member"
                                 ELSE IF \E m \in Mirrors : Len(mirror[m].com) > Len(c) THEN "stale"
                                 ELSE "race"}
  /\ UNCHANGED <<final, down, outages, mirror>>

\* The occupant's chain is completed along the rest of the route. Any party may carry it (spec 9.4).
Finalize(c) ==
  /\ c \in DOMAIN occ
  /\ occ[c] # None
  /\ c \notin final
  /\ final' = final \cup {c}
  /\ UNCHANGED <<occ, down, outages, mirror, noops>>

LeaderDown(c) ==
  /\ c \in Targets
  /\ c \notin down
  /\ outages < MaxOutages
  /\ down' = down \cup {c}
  /\ outages' = outages + 1
  /\ UNCHANGED <<occ, final, mirror, noops>>

LeaderUp(c) ==
  /\ c \in down
  /\ down' = down \ {c}
  /\ UNCHANGED <<occ, final, outages, mirror, noops>>

Advance(m, w) ==
  mirror' = [mirror EXCEPT ![m] =
               [com |-> Append(@.com, w), st |-> Judge(@.st, w.signer, w.change).next]]

(***************************************************************************)
(* Mirror.walk in mirror.ts, one cell: the mirror reads the successor cell *)
(* of the state it holds and takes its occupant, Final, valid or not. An   *)
(* open cell, an occupant not Final yet and a leader it cannot read all    *)
(* leave it where it is.                                                   *)
(***************************************************************************)
Walk(m) ==
  LET c == mirror[m].com
      established == c \notin down /\ Occupant(c) # None /\ c \in final
      notEstablished == c \in down \/ (Occupant(c) # None /\ c \notin final)
  IN /\ Len(c) < MaxHeight
     /\ mirror[m].st.members # {}
     /\ \/ established /\ Advance(m, occ[c])
        \/ /\ Broken = "not established is taken as invalid"
           /\ notEstablished
           /\ Advance(m, [signer |-> None, change |-> Junk])
     /\ UNCHANGED <<occ, final, down, outages, noops>>

\* The host sends a mirror a change a member signed, or a snapshot built on one (spec 6.6):
\* a convenience, never evidence. A mirror moves only on its own read of the cell.
HostOffers(m) ==
  /\ Broken = "a mirror takes what the host sends as evidence"
  /\ Len(mirror[m].com) < MaxHeight
  /\ \E k \in Writers(mirror[m].st) : \E ch \in Offered(mirror[m].st, k) :
       Advance(m, [signer |-> k, change |-> ch])
  /\ UNCHANGED <<occ, final, down, outages, noops>>

Init ==
  /\ occ = [c \in {<<>>} |-> None]
  /\ final = {}
  /\ down = {}
  /\ outages = 0
  /\ mirror = [m \in Mirrors |-> [com |-> <<>>, st |-> First]]
  /\ noops = {}

Next ==
  \/ \E k \in Keys, c \in Targets, ch \in Changes : Arrive(k, c, ch)
  \/ \E c \in DOMAIN occ : Finalize(c)
  \/ \E c \in Targets : LeaderDown(c) \/ LeaderUp(c)
  \/ \E m \in Mirrors : Walk(m) \/ HostOffers(m)

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
(***************************************************************************)
(* What is checked.                                                        *)
(***************************************************************************)

IsPrefix(a, b) == Len(a) <= Len(b) /\ SubSeq(b, 1, Len(a)) = a

\* 12.4, first item. A cell's occupant, once there, is never another: R_n has one successor.
OneSuccessor ==
  [][\A c \in DOMAIN occ : occ[c] # None => occ'[c] = occ[c]]_vars

\* 12.4, second item. Mirrors that walked the same cells hold the same state, and a mirror that
\* walked fewer holds a state the others passed through.
MirrorsAgree ==
  \A m1, m2 \in Mirrors :
    /\ IsPrefix(mirror[m1].com, mirror[m2].com) \/ IsPrefix(mirror[m2].com, mirror[m1].com)
    /\ mirror[m1].com = mirror[m2].com => mirror[m1].st = mirror[m2].st

\* A mirror holds nothing but what the cells hold, Final: the state it shows is the one the
\* occupants make, whatever a host sent it.
MirrorsHoldTheCells ==
  \A m \in Mirrors :
    /\ mirror[m].st = StateAt(mirror[m].com)
    /\ \A n \in 1..Len(mirror[m].com) :
         LET c == SubSeq(mirror[m].com, 1, n - 1)
         IN c \in DOMAIN occ /\ occ[c] = mirror[m].com[n] /\ c \in final

\* 12.4, fourth item, the non-member: no cell is ever held by a key its state does not list.
OccupantsAreWriters ==
  \A c \in DOMAIN occ : occ[c] # None => occ[c].signer \in Writers(StateAt(c))

\* The members, the base and the votes change only by a member's valid change, or by an invited
\* wallet's own acceptance. Any other occupant spends its cell and changes none of them (spec 7.4).
OnlyMembersChangeTheBase ==
  \A c \in DOMAIN occ :
    occ[c] # None =>
      LET s == StateAt(c)
          v == Judge(s, occ[c].signer, occ[c].change)
          same == /\ v.next.members = s.members
                  /\ v.next.placed = s.placed
                  /\ v.next.votes = s.votes
      IN \/ same
         \/ v.valid /\ occ[c].signer \in s.members
         \/ v.valid /\ occ[c].change.kind = "accept" /\ occ[c].signer \in s.invited

(***************************************************************************)
(* 12.4, fourth item, "delay and nothing else": with a host that carries   *)
(* nothing, a non-member writing, stale writes arriving and the leader     *)
(* seat lost MaxOutages times, a member who carries their own change still *)
(* builds, on every mirror.                                                *)
(***************************************************************************)
HonestWrites == \E k \in Honest, c \in Targets, ch \in Changes : Arrive(k, c, ch)

\* Mirrors keep reading and a lost leader seat comes back; nothing is asked of the host.
Reads ==
  /\ \A m \in Mirrors : WF_vars(Walk(m))
  /\ WF_vars(\E c \in Targets : LeaderUp(c))

\* A member carries their own change to the leader and along the route (spec 9.4).
LiveSpec ==
  /\ Spec
  /\ Reads
  /\ WF_vars(HonestWrites)
  /\ WF_vars(\E c \in DOMAIN occ : Finalize(c))

\* The rule left out: a change waits on the host to carry it. `Built` must fail under this one.
WaitsOnHostSpec == Spec /\ Reads

Built == <>(\A m \in Mirrors : \E i \in Instances : mirror[m].st.placed[i] # None)

-----------------------------------------------------------------------------
(***************************************************************************)
(* Reachability. Each of these is given to TLC as an invariant and must be *)
(* violated: the run it prints is the case happening in this model.        *)
(***************************************************************************)

InvalidAt(c) == occ[c] # None /\ ~Judge(StateAt(c), occ[c].signer, occ[c].change).valid

NoRace == "race" \notin noops
NoNonMemberWrite == "non-member" \notin noops
NoStaleWrite == "stale" \notin noops
NoInvalidOccupant == \A c \in DOMAIN occ : ~InvalidAt(c)
NoInvitationUsedUp == \A c \in DOMAIN occ : ~(InvalidAt(c) /\ occ[c].signer \in StateAt(c).invited)
NoRemovalByVote ==
  \A m \in Mirrors : \A f \in Founders :
    f \in mirror[m].st.members
      \/ \E n \in 1..Len(mirror[m].com) :
           mirror[m].com[n].signer = f /\ mirror[m].com[n].change.kind = "leave"
NoJoin == \A m \in Mirrors : mirror[m].st.members \subseteq Founders
NoMirrorCaughtUp ==
  ~(\E m1, m2 \in Mirrors :
      /\ Len(mirror[m1].com) = MaxHeight
      /\ mirror[m2].com = mirror[m1].com
      /\ m1 # m2)
NoWaitAtALeader ==
  ~(\E m \in Mirrors : mirror[m].com \in down /\ Occupant(mirror[m].com) # None)

=============================================================================
