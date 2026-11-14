#!/bin/zsh
# Starts a match on this machine as three copies of the game, none with a window: the publisher's
# match server, which has no player of its own, and two players' games that join it over the
# loopback address. Each player's game runs its half of the match check
# (Source/Sennite/Private/Tests/MatchTest.cpp): Sennite.Match.Attacker and Sennite.Match.Defender.
#   Tools/run_match_check.sh <Kills|Wipe|Time> <match id>
# It needs, from what started it (Server/test/game/match.test.ts):
#   SENNITE_PUBLISHER_URL       the publisher's server
#   SENNITE_MATCH_SECRET_FILE   the match server's secret
#   SENNITE_ATTACKER_FILE       the attacker's game's place at the publisher's server
#   SENNITE_DEFENDER_FILE       the defender's
#   SENNITE_SERVER_ARGS         more arguments for the match server alone (the match's rules)
#   SENNITE_OUTSIDER_FILE       optional: a third game's place, whose wallet is in no faction of the
#                               match; it runs Sennite.Match.Outsider, and must be refused and put out
# Logs (server.log, attacker.log, defender.log) and screenshots land in Saved/Match_<what>/.
# Exit status 0 only if both players' halves passed and the match server closed by itself.
cd "$(dirname "$0")/.."
WHAT="$1"; MATCH="$2"
if [[ -z "$WHAT" || -z "$MATCH" || -z "$SENNITE_PUBLISHER_URL" || -z "$SENNITE_MATCH_SECRET_FILE" || -z "$SENNITE_ATTACKER_FILE" || -z "$SENNITE_DEFENDER_FILE" ]]; then
  echo "usage: run_match_check.sh <Kills|Wipe|Time> <match id>, with the SENNITE_ settings its header names" >&2
  exit 2
fi
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PORT="${SENNITE_PORT:-7788}"
OUT="$PWD/Saved/Match_$WHAT"
mkdir -p "$OUT"
rm -f "$OUT"/*.log
touch "$OUT/.started"
PLAYER=(-game -windowed -ResX=1280 -ResY=720 -RenderOffscreen -unattended -nosplash -nosound -notexturestreaming -stdout -TestExit="Automation Test Queue Empty")

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/Sennite.uproject" "/Game/Maps/Arena?Seed=${SENNITE_SEED:-11}?Bases=2?Connector=Bridges?Match=$MATCH" -server -port=$PORT \
  -unattended -nosplash -nosound -stdout -PublisherUrl="$SENNITE_PUBLISHER_URL" -MatchSecretFile="$SENNITE_MATCH_SECRET_FILE" ${=SENNITE_SERVER_ARGS} > "$OUT/server.log" 2>&1 &
server=$!

# the players join once the match server is up
for i in {1..120}; do
  grep -q "Engine is initialized" "$OUT/server.log" 2>/dev/null && break
  sleep 1
done

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/Sennite.uproject" "127.0.0.1:$PORT" $PLAYER -PublisherUrl="$SENNITE_PUBLISHER_URL" -PublisherPlayerFile="$SENNITE_ATTACKER_FILE" \
  -MatchCheck=$WHAT -ExecCmds="Automation RunTests Sennite.Match.Attacker; Quit" > "$OUT/attacker.log" 2>&1 &
attacker=$!
if [[ -n "$SENNITE_OUTSIDER_FILE" ]]; then
  "$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/Sennite.uproject" "127.0.0.1:$PORT" $PLAYER -PublisherUrl="$SENNITE_PUBLISHER_URL" -PublisherPlayerFile="$SENNITE_OUTSIDER_FILE" \
    -MatchCheck=$WHAT -ExecCmds="Automation RunTests Sennite.Match.Outsider; Quit" > "$OUT/outsider.log" 2>&1 &
  outsider=$!
fi
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/Sennite.uproject" "127.0.0.1:$PORT" $PLAYER -PublisherUrl="$SENNITE_PUBLISHER_URL" -PublisherPlayerFile="$SENNITE_DEFENDER_FILE" \
  -MatchCheck=$WHAT -ExecCmds="Automation RunTests Sennite.Match.Defender; Quit" > "$OUT/defender.log" 2>&1
echo "defender exit $?"
wait $attacker
echo "attacker exit $?"
if [[ -n "$outsider" ]]; then
  wait $outsider
  echo "outsider exit $?"
fi

# the match server closes by itself a while after the publisher's server recorded the outcome
closed=0
for i in {1..150}; do
  if ! kill -0 $server 2>/dev/null; then closed=1; break; fi
  sleep 1
done
if (( ! closed )); then
  echo "the match server did not close by itself: stopping it"
  kill $server 2>/dev/null
fi
wait $server 2>/dev/null

grep -o "LogSenniteMatch[A-Za-z]*: .*" "$OUT/server.log"
sides=(attacker defender)
[[ -n "$outsider" ]] && sides+=(outsider)
for side in $sides; do
  grep -o "GAMECHECK .*" "$OUT/$side.log"
  grep "LogAutomationController.*\(Error\|Test Completed\|Fail\)" "$OUT/$side.log" | sed 's/^\[[^]]*\]\[[ 0-9]*\]//'
done
find "$OUT" -maxdepth 1 -name "*.png" -newer "$OUT/.started"

if [[ -n "$outsider" ]] && ! { grep -q "Test Completed. Result={Success} Name={Outsider}" "$OUT/outsider.log" && grep -q "GAMECHECK finished" "$OUT/outsider.log"; }; then
  echo "CHECK FAILED: match $WHAT: the outsider (see $OUT/outsider.log)"
  exit 1
fi
if (( closed )) && grep -q "the match server closes" "$OUT/server.log" \
  && grep -q "Test Completed. Result={Success} Name={Attacker}" "$OUT/attacker.log" && grep -q "GAMECHECK finished" "$OUT/attacker.log" \
  && grep -q "Test Completed. Result={Success} Name={Defender}" "$OUT/defender.log" && grep -q "GAMECHECK finished" "$OUT/defender.log"; then
  echo "CHECK PASSED: match $WHAT"
else
  echo "CHECK FAILED: match $WHAT (see $OUT/server.log, attacker.log and defender.log)"
  exit 1
fi
