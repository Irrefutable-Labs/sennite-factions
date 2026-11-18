#!/bin/zsh
# The store check (Source/Sennite/Private/Tests/StoreTest.cpp), by taps on the screen: on a game with a
# publisher's server the first screen's STORE shows the shop's skins on the stage and what a wallet is
# asked for, and BASE EDITOR opens the test range on the planning view.
# Needs a publisher's server running: SENNITE_PUBLISHER_URL, or the one Tools/phone_publisher.sh starts
# on this computer. The check is a player of its own on it, new each run, with no wallet connected.
# Screenshots and log in Saved/Store/. Not part of check_all.sh, which runs with no server.
cd "$(dirname "$0")/.."
URL="${SENNITE_PUBLISHER_URL:-http://127.0.0.1:8790}"
if ! curl -s -m 5 "$URL/shop" | grep -q '"skins"'; then
  echo "no publisher's server answers at $URL (start one: Tools/phone_publisher.sh)" >&2
  exit 2
fi
mkdir -p Saved/Store
rm -f Saved/SaveGames/StoreCheck.sav Saved/Store/player.json
SENNITE_MAP=/Game/Maps/Menu exec Tools/run_game_check.sh Sennite.Menu.Store Store Store -ProfileSlot=StoreCheck "-PublisherUrl=$URL" \
  "-PublisherPlayerFile=$PWD/Saved/Store/player.json" -SafeZoneCheck "-ini:Engine:[SystemSettings]:r.DebugSafeZone.TitleRatio=0.9"
