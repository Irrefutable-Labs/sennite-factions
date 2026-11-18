#!/bin/zsh
# The weapons check (Source/Sennite/Private/Tests/WeaponsTest.cpp): the hero opens the range's
# crate, has its rifle and shotgun with nothing laid on the ground, holds each in her hand in place
# of her own weapon, and fires. Screenshots and log in
# Saved/Weapons/.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Weapons OpenHoldAndFire Weapons
