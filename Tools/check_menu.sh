#!/bin/zsh
# The menu check (Source/Sennite/Private/Tests/MenuTest.cpp), by taps on the screen: the game opens on
# the menu, HEROES shows each hero turning on the stage, a bought hero cannot be chosen on a game with no
# shop, CHOOSE keeps a free hero in the profile on disk, and PLAY puts the player on the test range as it.
# Runs with a profile of its own (MenuCheck), new each run. Screenshots and log in Saved/Menu/.
cd "$(dirname "$0")/.."
rm -f Saved/SaveGames/MenuCheck.sav
# The run has a screen with a safe zone, as a phone's has (the engine's own r.DebugSafeZone.TitleRatio):
# the menu is drawn in from the screen's edges, and every tap of this check lands where its button is drawn.
SENNITE_MAP=/Game/Maps/Menu exec Tools/run_game_check.sh Sennite.Menu.ChooseAndPlay ChooseAndPlay Menu -ProfileSlot=MenuCheck \
  -SafeZoneCheck "-ini:Engine:[SystemSettings]:r.DebugSafeZone.TitleRatio=0.9"
