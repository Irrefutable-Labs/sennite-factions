#!/bin/zsh
# The healing check (Source/Sennite/Private/Tests/HealingTest.cpp): the hero picks up bandages and
# potions, is hurt by the storm, and with the real keys drinks a potion for shield and uses a bandage
# for health; counts, the item in her hand and the HUD follow. Screenshots and log in Saved/Healing/.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Healing HurtShieldAndHeal Healing
