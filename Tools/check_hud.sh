#!/bin/zsh
# The HUD check (Source/Sennite/Private/Tests/HudTest.cpp): the HUD shows health, shield, materials,
# players left and kills, names the piece in build mode, drops a material count when a piece is built,
# and shows the held weapon's rounds, which drop when it is fired. Screenshots and log in Saved/Hud/.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Hud FollowsTheGame Hud
