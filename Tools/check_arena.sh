#!/bin/zsh
# The arena check (Source/Sennite/Private/Tests/ArenaTest.cpp): the arena map is started once for each
# kind of connector, two bases joined by that kind. Each run holds the seeding to its promises over
# hundreds of seeds, then has the hero cross the connector to the far base with the real keys and
# mouse. Pictures of each kind land in Saved/Arena/, the logs in Saved/Arena_<kind>/.
#   Tools/check_arena.sh            every kind
#   Tools/check_arena.sh Canyon     one kind
# Exit status 0 only if every kind asked for passed.
cd "$(dirname "$0")"
failed=0
kinds=("$@")
if (( ${#kinds} == 0 )); then
  kinds=(Bridges Tunnel Canyon TreeTop Floating Traffic)
fi
for kind in $kinds; do
  if SENNITE_MAP="/Game/Maps/Arena?Seed=${SENNITE_SEED:-11}?Bases=2?Connector=$kind" ./run_game_check.sh Sennite.Arena SeededAndCrossable "Arena_$kind" > "../Saved/arena_$kind.out" 2>&1; then
    echo "passed  $kind: $(grep -o 'GAMECHECK crossed.*' ../Saved/arena_$kind.out)"
  else
    echo "FAILED  $kind"
    grep "GAMECHECK\|Error" "../Saved/arena_$kind.out" | cut -c1-300
    failed=1
  fi
done
exit $failed
