#!/bin/zsh
# Starts the game on the test range without a window and runs the first playable check
# (Source/Sennite/Private/Tests/FirstPlayableTest.cpp): the player's pawn is ASenniteCharacter with its
# hero, the hero animates, and the keys and mouse walk, sprint, jump and turn the camera.
# Leaves two screenshots in Saved/FirstPlayable/ and the log in Saved/FirstPlayable/check.log.
# Exit status 0 only if the check ran and passed.
cd "$(dirname "$0")/.."
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
OUT="$PWD/Saved/FirstPlayable"
mkdir -p "$OUT"
rm -f "$OUT"/*.png(N) "$OUT/check.log"

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/Sennite.uproject" /Game/Maps/TestRange -game \
  -windowed -ResX=1280 -ResY=720 -RenderOffscreen -unattended -nosplash -nosound -notexturestreaming -stdout \
  -ExecCmds="Automation RunTests Sennite.FirstPlayable; Quit" -TestExit="Automation Test Queue Empty" \
  > "$OUT/check.log" 2>&1
echo "game exit $?"

grep -o "FIRSTPLAYABLE .*" "$OUT/check.log"
grep "LogAutomationController.*\(Error\|Test Completed\|Fail\)" "$OUT/check.log" | sed 's/^\[[^]]*\]\[[ 0-9]*\]//'
ls "$OUT"/*.png(N)

if grep -q "Test Completed. Result={Success} Name={HeroSpawnsAnimatesAndMoves}" "$OUT/check.log" && grep -q "FIRSTPLAYABLE finished" "$OUT/check.log"; then
  echo "FIRST PLAYABLE CHECK PASSED"
else
  echo "FIRST PLAYABLE CHECK FAILED (see $OUT/check.log)"
  exit 1
fi
