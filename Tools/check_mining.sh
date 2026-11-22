#!/bin/zsh
# The mining check (Source/Sennite/Private/Tests/MiningTest.cpp): on an arena of two bases, the deposits
# hold what the game's settings say; the player's droid is taken out and sent to a wood deposit with the
# real key, mines it, is sent on to stone, goes on mining with its player far away, is safe from its own
# player's shots and is shot down by another's. Screenshots and log in Saved/Mining/.
SENNITE_MAP="/Game/Maps/Arena?Seed=${SENNITE_SEED:-11}?Bases=2?Connector=TreeTop" exec "$(dirname "$0")/run_game_check.sh" Sennite.Mining DroidMinesAndIsShotDown Mining
