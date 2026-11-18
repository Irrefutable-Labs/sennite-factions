#!/bin/zsh
# Runs every in-game check that needs no accounts, one game start each (two for the two-player
# checks), and says which passed.
# Exit status 0 only if all of them passed.
cd "$(dirname "$0")"
failed=0
for check in check_first_playable.sh check_weapons.sh check_building.sh check_editing.sh check_hud.sh check_storm.sh check_healing.sh check_touch.sh check_game_menu.sh check_two_players.sh check_menu.sh check_lobby.sh check_hero_hands.sh check_combat.sh check_arena.sh check_match.sh; do
  if ./$check > /dev/null 2>&1; then
    echo "passed  $check"
  else
    echo "FAILED  $check"
    failed=1
  fi
done
exit $failed
