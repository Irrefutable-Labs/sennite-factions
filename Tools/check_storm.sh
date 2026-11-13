#!/bin/zsh
# The storm check (Source/Sennite/Private/Tests/StormTest.cpp): the safe circle holds, shrinks by the
# clock and holds again; the hero takes no damage inside it, loses health every second outside it
# while the HUD warns her, and stops losing it once back inside. Screenshots and log in Saved/Storm/.
#
# The game's own storm needs two players and runs its stages in half minutes. For this run only, the
# game is started with the storm beginning with one player, ten seconds in, and one second a stage.
exec "$(dirname "$0")/run_game_check.sh" Sennite.Storm ShrinksAndHurtsOutside Storm \
  "-ini:Game:[/Script/Sennite.SenniteGameMode]:PlayersToStartStorm=1,[/Script/Sennite.SenniteGameMode]:StormStartDelay=10.0,[/Script/Sennite.StormActor]:StageSeconds=1.0"
