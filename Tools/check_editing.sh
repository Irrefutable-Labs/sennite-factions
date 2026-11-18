#!/bin/zsh
# The editing and damage check (Source/Sennite/Private/Tests/EditTest.cpp): the hero builds a wood
# wall, shoots it (the HUD shows the damage and the wall's health, its tiles are knocked out of line),
# edits a door into it with G and the fire button, shoots and walks through the door, puts the tiles
# back from the other side, builds a wall on top of it (held up) and tries one on nothing (not built),
# and brings the first wall down, which takes the wall it held with it. Screenshots and log in Saved/Editing/.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Editing EditAndDamage Editing
