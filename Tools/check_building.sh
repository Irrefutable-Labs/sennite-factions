#!/bin/zsh
# The building check (Source/Sennite/Private/Tests/BuildingTest.cpp): the hero previews and builds a
# wall, a ramp and a floor on the build grid with the real keys, each costs its price once and is
# solid, and she runs up the ramp she built. Screenshots and log in Saved/Building/.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Building PreviewBuildAndClimb Building
