#!/bin/zsh
# The menu check (Source/Sennite/Private/Tests/MenuTest.cpp), by taps on the screen: the game opens on
# the menu, HEROES shows each hero turning on the stage, a bought hero cannot be chosen on a game with no
# shop, CHOOSE keeps a free hero in the profile on disk, and PLAY puts the player in the arena as it.
# Runs with a profile of its own (MenuCheck), new each run. Screenshots and log in Saved/Menu/.
cd "$(dirname "$0")/.."
rm -f Saved/SaveGames/MenuCheck.sav
SENNITE_MAP=/Game/Maps/Menu exec Tools/run_game_check.sh Sennite.Menu.ChooseAndPlay ChooseAndPlay Menu -ProfileSlot=MenuCheck
