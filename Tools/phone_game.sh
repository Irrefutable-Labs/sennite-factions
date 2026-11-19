#!/bin/zsh
# Points the game on a phone (on adb) at the publisher's server on this computer's Wi-Fi
# (Tools/phone_publisher.sh), by the command-line file the game reads at start; nothing is built into
# the APK. Starts the game. With "off", takes the file away: the game plays with no faction bases.
#   Tools/phone_game.sh [off]
#   SENNITE_LAN_IP     this computer's address on the Wi-Fi (default: en0's)
cd "$(dirname "$0")/.." || exit 2
ADB="${ANDROID_HOME:-$HOME/Library/Android/sdk}/platform-tools/adb"
PKG=com.irrefutablelabs.sennitefactions
FILE=/storage/emulated/0/Android/data/$PKG/files/UnrealGame/Sennite/UECommandLine.txt
"$ADB" shell am force-stop $PKG
if [[ "$1" == "off" ]]; then
  "$ADB" shell rm -f "$FILE"
  echo "the game on the phone has no publisher's server"
  exit 0
fi
IP="${SENNITE_LAN_IP:-$(ipconfig getifaddr en0)}"
[[ -n "$IP" ]] || { echo "no Wi-Fi address: set SENNITE_LAN_IP" >&2; exit 2; }
# the file stands in for the APK's own command line: the project first, as the APK's has it
LINE="-project=\"../../../Sennite/Sennite.uproject\" -PublisherUrl=http://$IP:8790"
TMP=$(mktemp)
print -r -- "$LINE" > "$TMP"
# A game newly installed has not made its folder yet, and a folder made from here is the shell's, which the
# game cannot read: the game is started once to make its own, then stopped.
if ! "$ADB" shell "[ -d '${FILE:h}' ]"; then
  "$ADB" shell am start -n $PKG/com.epicgames.unreal.SplashActivity >/dev/null
  for i in {1..60}; do "$ADB" shell "[ -d '${FILE:h}' ]" && break; sleep 1; done
  "$ADB" shell am force-stop $PKG
  "$ADB" shell "[ -d '${FILE:h}' ]" || { echo "the game did not make its folder on the phone (${FILE:h})" >&2; rm -f "$TMP"; exit 1; }
fi
"$ADB" push "$TMP" "$FILE" >/dev/null || { rm -f "$TMP"; exit 1; }
rm -f "$TMP"
"$ADB" shell am start -n $PKG/com.epicgames.unreal.SplashActivity >/dev/null
echo "the game on the phone reaches the publisher's server at http://$IP:8790"
