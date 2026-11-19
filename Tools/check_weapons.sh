#!/bin/zsh
# The weapons check (Source/Sennite/Private/Tests/WeaponsTest.cpp): the hero opens the range's
# crate, has its rifle and shotgun with nothing laid on the ground, holds each in her hand in place
# of her own weapon, and fires. Screenshots and log in
# Saved/Weapons/.
#
# A crate is full again 45 s after it is opened. For this run only it is 10 s, so the check can open
# the same crate again once its player owns the guns bought in the store.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Weapons OpenHoldAndFire Weapons \
  "-ini:Game:[/Script/Sennite.SenniteCrate]:RestockSeconds=10.0"
