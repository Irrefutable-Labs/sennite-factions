#!/bin/zsh
# The first playable check (Source/Sennite/Private/Tests/FirstPlayableTest.cpp): the player's pawn is
# ASenniteCharacter with its hero, the hero animates, and the keys and mouse walk, sprint, jump and
# turn the camera. Screenshots and log in Saved/FirstPlayable/.
exec "$(dirname "$0")/run_game_check.sh" Sennite.FirstPlayable HeroSpawnsAnimatesAndMoves FirstPlayable
