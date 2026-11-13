#!/bin/zsh
# The weapons check (Source/Sennite/Private/Tests/WeaponsTest.cpp): the hero walks over the rifle and
# the shotgun, holds each in her hand in place of her own weapon, and fires. Screenshots and log in
# Saved/Weapons/.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Weapons PickUpHoldAndFire Weapons
