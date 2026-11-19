#!/bin/zsh
# The healing check (Source/Sennite/Private/Tests/HealingTest.cpp): the hero has bandages and potions
# from the range's crate, is hurt by the storm, and with the real keys drinks a potion for shield and
# uses a bandage for health; counts, the item in her hand and the HUD follow. Screenshots and log in
# Saved/Healing/.
#
# The game has no storm for now (bEnabled is off in DefaultGame.ini). For this run only it is on,
# so there is something to be hurt by; it starts as the game's own would, with two players, so here
# it never shrinks.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Healing HurtShieldAndHeal Healing \
  "-ini:Game:[/Script/Sennite.StormActor]:bEnabled=True"
