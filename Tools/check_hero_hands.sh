#!/bin/zsh
# The hero hands check (Source/Sennite/Private/Tests/HeroHandsTest.cpp): the hero takes the rifle and
# becomes each hero of the list in turn; each holds it in the right hand pointing ahead, with its own
# built-in weapon hidden, and fires with its own firing animation. It also logs each hero's measured
# grip and palm (GAMECHECK <id> measured). Screenshots, bone lists and log in Saved/HeroHands/.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Heroes.Hands Hands HeroHands
