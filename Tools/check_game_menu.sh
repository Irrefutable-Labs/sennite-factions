#!/bin/zsh
# The in-game menu check (Source/Sennite/Private/Tests/GameMenuTest.cpp), twice: by keys, then with
# the touch controls shown, as on a phone. Escape (or MENU by touch) opens the menu over the game and
# the hero takes nothing while it is open; SETTINGS changes how far the view turns, whether up looks
# down, the volume and the graphics quality, each in effect at once and kept in the profile; the back
# button goes back and RESUME closes it; QUIT GAME and MAIN MENU ask first, and MAIN MENU leaves for
# the first screen. Each run has a profile of its own (GameMenuCheck), new each run. Screenshots and
# logs in Saved/GameMenu/ and Saved/GameMenuTouch/.
# The engine's smoothing of the mouse is off for these runs: with it, how far a count of the mouse
# turns the view changes from frame to frame, and the check compares two turns.
cd "$(dirname "$0")/.."
rm -f Saved/SaveGames/GameMenuCheck.sav
SMOOTH="-ini:Input:[/Script/Engine.InputSettings]:bEnableMouseSmoothing=False"
Tools/run_game_check.sh Sennite.GameMenu OpensSetsAndLeaves GameMenu -ProfileSlot=GameMenuCheck "$SMOOTH" "$@" || exit 1
rm -f Saved/SaveGames/GameMenuCheck.sav
# by touch, and as on a phone with a camera cutout: the HUD is drawn inside a safe area (the engine's
# r.DebugSafeZone.TitleRatio), so a touch is not where the canvas says a button is until the canvas's
# place on the screen is taken off it
Tools/run_game_check.sh Sennite.GameMenu OpensSetsAndLeaves GameMenuTouch -ProfileSlot=GameMenuCheck \
  "-ini:Input:[/Script/Engine.InputSettings]:bAlwaysShowTouchInterface=True" "$SMOOTH" \
  "-ini:Engine:[SystemSettings]:r.DebugSafeZone.TitleRatio=0.85" "$@"
