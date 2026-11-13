#!/bin/zsh
# Runs every in-game check, one game start each, and says which passed.
# Exit status 0 only if all of them passed.
cd "$(dirname "$0")"
failed=0
for check in check_first_playable.sh check_weapons.sh check_building.sh check_hud.sh check_storm.sh check_healing.sh; do
  if ./$check > /dev/null 2>&1; then
    echo "passed  $check"
  else
    echo "FAILED  $check"
    failed=1
  fi
done
exit $failed
