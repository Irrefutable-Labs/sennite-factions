#!/bin/zsh
# Starts two copies of the game on this machine, neither with a window: one hosts the test range as a
# listen server, the other joins it over the loopback address. Each runs its half of a check.
#   Tools/run_two_player_check.sh <test group> <folder under Saved/>
# runs <test group>.Host on the host and <test group>.Joiner on the joining player.
# Logs (host.log, joiner.log) and screenshots land in Saved/<folder>/.
# Exit status 0 only if both halves ran to their end and passed.
cd "$(dirname "$0")/.."
GROUP="$1"; FOLDER="$2"
if [[ -z "$GROUP" || -z "$FOLDER" ]]; then
  echo "usage: run_two_player_check.sh <test group> <folder under Saved/>" >&2
  exit 2
fi
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PORT="${SENNITE_PORT:-7787}"
OUT="$PWD/Saved/$FOLDER"
mkdir -p "$OUT"
touch "$OUT/.started"
COMMON=(-game -windowed -ResX=1280 -ResY=720 -RenderOffscreen -unattended -nosplash -nosound -notexturestreaming -stdout -TestExit="Automation Test Queue Empty")

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/Sennite.uproject" "/Game/Maps/TestRange?listen" -port=$PORT $COMMON \
  -ExecCmds="Automation RunTests $GROUP.Host; Quit" > "$OUT/host.log" 2>&1 &
host=$!

# the joining player starts once the host is up and its map is loaded
for i in {1..90}; do
  grep -q "Ready to start automation" "$OUT/host.log" 2>/dev/null && break
  sleep 1
done

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/Sennite.uproject" "127.0.0.1:$PORT" $COMMON \
  -ExecCmds="Automation RunTests $GROUP.Joiner; Quit" > "$OUT/joiner.log" 2>&1
echo "joiner exit $?"
wait $host
echo "host exit $?"

for side in host joiner; do
  grep -o "GAMECHECK .*" "$OUT/$side.log"
  grep "LogAutomationController.*\(Error\|Test Completed\|Fail\)" "$OUT/$side.log" | sed 's/^\[[^]]*\]\[[ 0-9]*\]//'
done
find "$OUT" -maxdepth 1 -name "*.png" -newer "$OUT/.started"

if grep -q "Test Completed. Result={Success} Name={Host}" "$OUT/host.log" && grep -q "GAMECHECK finished" "$OUT/host.log" \
  && grep -q "Test Completed. Result={Success} Name={Joiner}" "$OUT/joiner.log" && grep -q "GAMECHECK finished" "$OUT/joiner.log"; then
  echo "CHECK PASSED: $GROUP"
else
  echo "CHECK FAILED: $GROUP (see $OUT/host.log and $OUT/joiner.log)"
  exit 1
fi
