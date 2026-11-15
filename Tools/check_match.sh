#!/bin/zsh
# Two checks of a match that need one game and no accounts (Source/Sennite/Private/Tests/MatchTest.cpp):
#   how a match ends, held against the rules on their own (kills to win, the time, a base wiped);
#   a weapon crate on the arena: the hero opens it with the real key, takes the rifle it lays out,
#   shoots and reloads with the rounds it gave, and the crate is closed and full again later.
# A whole match, with the publisher's match server and two players' games, is run by
# Tools/check_base_editor.sh, which has the accounts it needs.
# Exit status 0 only if both passed.
cd "$(dirname "$0")"
failed=0
if ./run_game_check.sh Sennite.Match.Rules Rules MatchRules > ../Saved/match_rules.out 2>&1; then
  echo "passed  rules: $(grep -o 'GAMECHECK rules.*' ../Saved/match_rules.out)"
else
  echo "FAILED  rules"; grep "GAMECHECK\|Error" ../Saved/match_rules.out | cut -c1-300; failed=1
fi
if SENNITE_MAP="/Game/Maps/Arena?Seed=${SENNITE_SEED:-11}?Bases=2" ./run_game_check.sh Sennite.Crates OpenAndArm Crates > ../Saved/crates.out 2>&1; then
  echo "passed  crates:"; grep -o 'GAMECHECK crates.*' ../Saved/crates.out | sed 's/^/          /'
else
  echo "FAILED  crates"; grep "GAMECHECK\|Error" ../Saved/crates.out | cut -c1-300; failed=1
fi
exit $failed
