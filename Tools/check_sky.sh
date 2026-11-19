#!/bin/zsh
# The sky check (Source/Sennite/Private/Tests/SkyTest.cpp): the arena map is started once under each
# sky, and each run holds that the world is under that sky and that a match's sky comes from its seed
# as often as its weight says. Pictures of each land in Saved/Sky/, the logs in Saved/Sky_<name>/.
#   Tools/check_sky.sh            every sky
#   Tools/check_sky.sh NIGHT      one
cd "$(dirname "$0")"
failed=0
skies=("$@")
if (( ${#skies} == 0 )); then
  skies=(DAY DUSK NIGHT)
fi
for sky in $skies; do
  if SENNITE_MAP="/Game/Maps/Arena?Seed=${SENNITE_SEED:-11}?Bases=2?Connector=Canyon" ./run_game_check.sh Sennite.Sky Looks "Sky_$sky" "-Sky=$sky" > "../Saved/sky_$sky.out" 2>&1; then
    echo "passed  $sky: $(grep -o 'GAMECHECK sky.*' ../Saved/sky_$sky.out)"
  else
    echo "FAILED  $sky"
    grep "GAMECHECK\|Error" "../Saved/sky_$sky.out" | cut -c1-300
    failed=1
  fi
done
exit $failed
