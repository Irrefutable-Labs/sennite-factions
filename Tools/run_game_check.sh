#!/bin/zsh
# Starts the game without a window and runs one in-game check. The map is the test range, or
# SENNITE_MAP (a map's address, options and all).
#   Tools/run_game_check.sh <test filter> <test name> <folder under Saved/> [more arguments for the game]
# The check's screenshots and log (check.log) land in Saved/<folder>/; only screenshots this run
# wrote are listed. Exit status 0 only if the check ran to its end and passed.
cd "$(dirname "$0")/.."
FILTER="$1"; NAME="$2"; FOLDER="$3"
shift 3 2>/dev/null
if [[ -z "$FILTER" || -z "$NAME" || -z "$FOLDER" ]]; then
  echo "usage: run_game_check.sh <test filter> <test name> <folder under Saved/>" >&2
  exit 2
fi
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
OUT="$PWD/Saved/$FOLDER"
mkdir -p "$OUT"
touch "$OUT/.started"

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/Sennite.uproject" "${SENNITE_MAP:-/Game/Maps/TestRange}" -game \
  -windowed -ResX=1280 -ResY=720 -RenderOffscreen -unattended -nosplash -nosound -notexturestreaming -stdout \
  -ExecCmds="Automation RunTests $FILTER; Quit" -TestExit="Automation Test Queue Empty" "$@" \
  > "$OUT/check.log" 2>&1
echo "game exit $?"

grep -o "GAMECHECK .*" "$OUT/check.log"
grep "LogAutomationController.*\(Error\|Test Completed\|Fail\)" "$OUT/check.log" | sed 's/^\[[^]]*\]\[[ 0-9]*\]//'
find "$OUT" -maxdepth 1 -name "*.png" -newer "$OUT/.started"

if grep -q "Test Completed. Result={Success} Name={$NAME}" "$OUT/check.log" && grep -q "GAMECHECK finished" "$OUT/check.log"; then
  echo "CHECK PASSED: $FILTER"
else
  echo "CHECK FAILED: $FILTER (see $OUT/check.log)"
  exit 1
fi
