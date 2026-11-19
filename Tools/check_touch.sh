#!/bin/zsh
# The touch check (Source/Sennite/Private/Tests/TouchTest.cpp): with the touch controls shown, as on
# a phone, fingers on the game's viewport turn the hero, walk and run her with the stick, open the
# range's crate with OPEN, change what she holds, fire (held), reload, jump, build, edit, and drink a
# potion. Screenshots and log in Saved/Touch/.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Touch PlaysByTouch Touch \
  "-ini:Input:[/Script/Engine.InputSettings]:bAlwaysShowTouchInterface=True" "$@"
