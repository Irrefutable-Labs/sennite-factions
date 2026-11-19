#!/bin/zsh
# The skin look check (Source/Sennite/Private/Tests/SkinLookTest.cpp): the hero is given each skin the
# shop sells in turn, as the game puts a skin on, and each must keep every part of its body. A picture
# of each from in front and from behind lands in Saved/Skins/, with the log.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Skins.Look Look Skins
