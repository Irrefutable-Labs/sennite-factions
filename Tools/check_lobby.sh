#!/bin/zsh
# The lobby check (Source/Sennite/Private/Tests/LobbyTest.cpp): two copies of the game on this machine,
# neither with a window, each starting on the menu with a profile of its own, new each run, and doing
# everything by taps on the screen. One taps HOST; the other taps JOIN, types the host's address on the
# keypad, joins, and chooses another hero in the lobby; the host sees it and taps START; both reach the
# arena together, each as their own hero. Logs (host.log, joiner.log) and screenshots in Saved/Lobby/.
cd "$(dirname "$0")/.."
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PORT="${SENNITE_PORT:-7791}"
OUT="$PWD/Saved/Lobby"
mkdir -p "$OUT"
touch "$OUT/.started"
rm -f Saved/SaveGames/LobbyHost.sav Saved/SaveGames/LobbyJoiner.sav
COMMON=(-game -windowed -ResX=1280 -ResY=720 -RenderOffscreen -unattended -nosplash -nosound -notexturestreaming -stdout -TestExit="Automation Test Queue Empty")

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/Sennite.uproject" -port=$PORT -ProfileSlot=LobbyHost $COMMON \
  -ExecCmds="Automation RunTests Sennite.Lobby.Host; Quit" > "$OUT/host.log" 2>&1 &
host=$!

# the joining player starts once the host's lobby is open
for i in {1..120}; do
  grep -q "GAMECHECK lobby open" "$OUT/host.log" 2>/dev/null && break
  sleep 1
done

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/Sennite.uproject" -ProfileSlot=LobbyJoiner -LobbyAddress=127.0.0.1:$PORT $COMMON \
  -ExecCmds="Automation RunTests Sennite.Lobby.Joiner; Quit" > "$OUT/joiner.log" 2>&1
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
  echo "CHECK PASSED: Sennite.Lobby"
else
  echo "CHECK FAILED: Sennite.Lobby (see $OUT/host.log and $OUT/joiner.log)"
  exit 1
fi
