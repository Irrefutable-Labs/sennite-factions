#!/bin/zsh
# The sound check (Source/Sennite/Private/Tests/SoundsTest.cpp): every sound the game names has
# recordings that load, nothing goes round in the background, and the hero's steps, jump, landing and own
# attack and the in-game menu's opening, buttons and closing are each asked for when they happen.
# A run has no sound device, so this cannot say what anything is like to the ear. Log in Saved/Sounds/.
cd "$(dirname "$0")/.."
Tools/run_game_check.sh Sennite.Sounds HeardInTheGame Sounds "$@"
