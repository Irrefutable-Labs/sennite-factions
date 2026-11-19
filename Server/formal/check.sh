#!/bin/bash
# Checks the two formal models of spec 12.4 with TLC, the TLA+ model checker.
#
#   TLA_TOOLS_JAR=/path/to/tla2tools.jar Server/formal/check.sh
#
# Three kinds of run, one line each:
#   holds     every property holds in every state TLC reaches.
#   red       one rule is left out of the model (`Broken`), and the property that rule is there for
#             must fail. A property that stays green with its rule gone checks nothing.
#   reached   a case the models are about (a race, a stale write, a removal by vote, a shortfall)
#             is given to TLC as something that never happens, and TLC must find it happening.
#
# CELLS is how many successor cells a lineage run goes through: 3 unless set (under two minutes
# for everything on an 18-core Mac); CELLS=4 takes about six minutes.
#
# Needs Java 11 or later (`JAVA`, else `java`) and tla2tools.jar (github.com/tlaplus/tlaplus).
# Logs and configurations go to a folder under $TMPDIR, named at the end.

set -uo pipefail
cd "$(dirname "$0")" || exit 2
JAR=${TLA_TOOLS_JAR:?set TLA_TOOLS_JAR to tla2tools.jar}
JAVA=${JAVA:-java}
CELLS=${CELLS:-3}
OUT=$(mktemp -d "${TMPDIR:-/tmp}/sennite-formal-XXXXXX")
bad=0
count=0

# run <name> <module> <holds | red:Property | reached:Property> <configuration>
run() {
  local name=$1 module=$2 expect=$3 cfg=$4
  local log="$OUT/$name.log"
  count=$((count + 1))
  printf '%s\n' "$cfg" > "$OUT/$name.cfg"
  "$JAVA" -XX:+UseParallelGC -cp "$JAR" tlc2.TLC -config "$OUT/$name.cfg" -metadir "$OUT/states-$name" \
    -workers auto -deadlock -noGenerateSpecTE "$module.tla" > "$log" 2>&1
  local states
  states=$(grep -oE '[0-9]+ distinct states found' "$log" | tail -1)
  local got="error (see $log)"
  if grep -q 'No error has been found' "$log"; then
    got=holds
  elif grep -qE 'Invariant [A-Za-z]+ is violated|Action property [A-Za-z]+ is violated' "$log"; then
    got="fails:$(grep -oE '(Invariant|Action property) [A-Za-z]+ is violated' "$log" | head -1 | awk '{print $(NF-2)}')"
  elif grep -q 'Temporal properties were violated' "$log"; then
    got="fails:$(grep -oE '^PROPERTY +[A-Za-z]+' "$OUT/$name.cfg" | awk '{print $2}')"
  fi
  local want=$expect
  case $expect in
    red:*) want="fails:${expect#red:}" ;;
    reached:*) want="fails:${expect#reached:}" ;;
  esac
  if [ "$got" = "$want" ]; then
    printf 'ok    %-8s %-58s %s\n' "${expect%%:*}" "$name" "$states"
  else
    printf 'WRONG %-8s %-58s wanted %s, got %s\n' "${expect%%:*}" "$name" "$want" "$got"
    bad=$((bad + 1))
  fi
}

# ---- the lineage ---------------------------------------------------------------------------

# Members 1 and 2, and key 3, which the first state does not list; one spot on the parcel.
building() { # <Broken> <Track>
  cat <<EOF
CONSTANTS
  Keys = {1, 2, 3}
  Founders = {1, 2}
  Instances = {i1}
  Prefabs = {p1, p2}
  MaxMembers = 3
  Kinds = {"place", "replace", "dismantle", "confirm", "junk"}
  Mirrors = {m1, m2}
  MaxHeight = $CELLS
  MaxOutages = 1
  Honest = {1}
  Track = $2
  None = None
  Broken = "$1"
EOF
}

# Members 1, 2 and 3, and key 4, who may be invited: leaving, inviting, accepting, voting.
people() { # <Broken>
  cat <<EOF
CONSTANTS
  Keys = {1, 2, 3, 4}
  Founders = {1, 2, 3}
  Instances = {i1}
  Prefabs = {p1}
  MaxMembers = 4
  Kinds = {"leave", "invite", "uninvite", "accept", "vote", "unvote", "junk"}
  Mirrors = {m1, m2}
  MaxHeight = $CELLS
  MaxOutages = 0
  Honest = {1}
  Track = FALSE
  None = None
  Broken = "$1"
EOF
}

# Members 1 and 2, and key 3, who may be invited: joining and building together.
joining() { # <Broken>
  cat <<EOF
CONSTANTS
  Keys = {1, 2, 3}
  Founders = {1, 2}
  Instances = {i1}
  Prefabs = {p1}
  MaxMembers = 3
  Kinds = {"invite", "accept", "leave", "place", "dismantle", "junk"}
  Mirrors = {m1, m2}
  MaxHeight = $CELLS
  MaxOutages = 0
  Honest = {1}
  Track = FALSE
  None = None
  Broken = "$1"
EOF
}

# Member 1 alone, key 2 which the state does not list, and a leader seat lost twice.
alone() {
  cat <<EOF
CONSTANTS
  Keys = {1, 2}
  Founders = {1}
  Instances = {i1}
  Prefabs = {p1}
  MaxMembers = 2
  Kinds = {"place"}
  Mirrors = {m1, m2}
  MaxHeight = 2
  MaxOutages = 2
  Honest = {1}
  Track = FALSE
  None = None
  Broken = "none"
EOF
}

SAFE="SPECIFICATION Spec
INVARIANT MirrorsAgree
INVARIANT MirrorsHoldTheCells
INVARIANT OccupantsAreWriters
INVARIANT OnlyMembersChangeTheBase
PROPERTY OneSuccessor"

run lineage-building FactionLineage holds "$SAFE
$(building none FALSE)"
run lineage-people FactionLineage holds "$SAFE
$(people none)"
run lineage-joining-and-building FactionLineage holds "$SAFE
$(joining none)"
run lineage-a-member-alone-builds FactionLineage holds "SPECIFICATION LiveSpec
PROPERTY Built
$(alone)"

run lineage-red-the-last-arrival-holds-the-cell FactionLineage red:OneSuccessor "SPECIFICATION Spec
PROPERTY OneSuccessor
$(building 'the last arrival holds the cell' FALSE)"
run lineage-red-a-non-members-write-is-recognized FactionLineage red:OccupantsAreWriters "SPECIFICATION Spec
INVARIANT OccupantsAreWriters
$(building "a non-member's write is recognized" FALSE)"
run lineage-red-a-mirror-takes-what-the-host-sends FactionLineage red:MirrorsHoldTheCells "SPECIFICATION Spec
INVARIANT MirrorsHoldTheCells
$(building 'a mirror takes what the host sends as evidence' FALSE)"
run lineage-red-not-established-is-taken-as-invalid FactionLineage red:MirrorsAgree "SPECIFICATION Spec
INVARIANT MirrorsAgree
$(building 'not established is taken as invalid' FALSE)"
run lineage-red-an-invited-wallet-writes-as-a-member FactionLineage red:OnlyMembersChangeTheBase "SPECIFICATION Spec
INVARIANT OnlyMembersChangeTheBase
$(joining 'an invited wallet writes as a member')"
run lineage-red-a-change-waits-on-the-host FactionLineage red:Built "SPECIFICATION WaitsOnHostSpec
PROPERTY Built
$(alone)"

for case in NoRace NoNonMemberWrite NoStaleWrite NoInvalidOccupant NoMirrorCaughtUp NoWaitAtALeader; do
  run "lineage-reached-$case" FactionLineage "reached:$case" "SPECIFICATION Spec
INVARIANT $case
$(building none TRUE)"
done
for case in NoInvitationUsedUp NoRemovalByVote NoJoin; do
  run "lineage-reached-$case" FactionLineage "reached:$case" "SPECIFICATION Spec
INVARIANT $case
$(people none)"
done

# ---- supply --------------------------------------------------------------------------------

# Two members and a third wallet, two units each; a prefab that needs 1 and one that needs 3.
supply() { # <Broken>
  cat <<EOF
CONSTANTS
  Wallets = {a, b, x}
  Founders = {a, b}
  Instances = {i1}
  Prefabs = {1, 3}
  Start = 2
  Amounts = {1, 3}
  MaxVaults = 2
  Broken = "$1"
EOF
}

run supply FactionSupply holds "SPECIFICATION Spec
INVARIANT Conserved
INVARIANT NothingSpentTwice
INVARIANT FundingIsInVaults
PROPERTY ReleasePaysWhomItNames
$(supply none)"

run supply-red-a-contribution-leaves-the-units-in-the-wallet FactionSupply red:Conserved "SPECIFICATION Spec
INVARIANT Conserved
$(supply 'a contribution leaves the units in the wallet')"
run supply-red-a-vault-releases-twice FactionSupply red:Conserved "SPECIFICATION Spec
INVARIANT Conserved
$(supply 'a vault releases twice')"
run supply-red-a-release-pays-whoever-releases FactionSupply red:ReleasePaysWhomItNames "SPECIFICATION Spec
PROPERTY ReleasePaysWhomItNames
$(supply 'a release pays whoever releases')"
run supply-red-a-wallet-sends-units-it-has-committed FactionSupply red:NothingSpentTwice "SPECIFICATION Spec
INVARIANT NothingSpentTwice
$(supply 'a wallet sends units it has committed')"
run supply-red-a-claim-counts-as-funding FactionSupply red:FundingIsInVaults "SPECIFICATION Spec
INVARIANT FundingIsInVaults
$(supply 'a claim counts as funding')"

for case in NothingFullyFunded NoShortfallAfterWithdrawal NoRestoredAfterWithdrawal NoLeaverCounting \
  NoGoneMemberVault NoCommitOfReceivedUnits NotAllReturned; do
  run "supply-reached-$case" FactionSupply "reached:$case" "SPECIFICATION Spec
INVARIANT $case
$(supply none)"
done

echo
if [ "$bad" -eq 0 ]; then
  echo "all $count runs are as they should be (logs: $OUT)"
else
  echo "$bad of $count runs are not as they should be (logs: $OUT)"
  exit 1
fi
