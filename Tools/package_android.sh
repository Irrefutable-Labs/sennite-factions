#!/bin/zsh
# Builds, cooks and packages the game for Android phones: one APK with the menu, the arena PLAY opens and the test range the bases stand on,
# played by touch. Long (the first run cooks every asset the two maps use).
# The publisher's server the game uses is written into the APK itself, so a phone the game is newly installed on
# reaches it too (a server address left on the phone, Tools/phone_game.sh, is lost when the game is uninstalled):
#   SENNITE_PUBLISHER_URL   the server (default: this computer's Wi-Fi address, port 8790, as Tools/phone_publisher.sh serves it);
#                           none for an APK with no server, no store and no faction bases
#   Tools/package_android.sh                 the APK, in Saved/Phone/
#   Tools/package_android.sh -iterate        cook only what changed since the last run
# Then, with the phone on adb (USB, or wireless: adb pair / adb connect):
#   adb install -r <the APK this prints>
# Needs the engine's Android support (Epic Games Launcher: UE 5.8, Options, Target Platforms,
# Android), and on this computer the Android SDK with the NDK the engine asks for
# (Engine/Config/Android/Android_SDK.json: r27c, 27.2.12479018) and a Java runtime (Android Studio's).
cd "$(dirname "$0")/.."
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
export NDKROOT="${NDKROOT:-$ANDROID_HOME/ndk/27.2.12479018}"
export NDK_ROOT="$NDKROOT"
export JAVA_HOME="${JAVA_HOME:-/Applications/Android Studio.app/Contents/jbr/Contents/Home}"

for need in "$ANDROID_HOME/platform-tools/adb" "$NDKROOT/source.properties" "$JAVA_HOME/bin/java"; do
  if [[ ! -e "$need" ]]; then
    echo "missing: $need" >&2
    exit 2
  fi
done
if [[ ! -d "$UE/Engine/Build/Android/Java" ]]; then
  echo "The engine has no Android support: in the Epic Games Launcher, UE 5.8, Options, Target Platforms, tick Android." >&2
  exit 2
fi

SERVER="${SENNITE_PUBLISHER_URL:-http://$(ipconfig getifaddr en0 2>/dev/null):8790}"
if [[ "$SERVER" == "none" || "$SERVER" == "http://:8790" ]]; then
  GAME_LINE=()
  echo "the APK names no publisher's server"
else
  GAME_LINE=("-cmdline=-PublisherUrl=$SERVER")
  echo "the APK reaches the publisher's server at $SERVER"
fi

OUT="$PWD/Saved/Phone"
mkdir -p "$OUT"
touch "$OUT/.started"
"$UE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$PWD/Sennite.uproject" -platform=Android -cookflavor=ASTC \
  -clientconfig=Development -build -cook -stage -package -pak -archive -archivedirectory="$OUT" \
  -map=/Game/Maps/Menu+/Game/Maps/Arena+/Game/Maps/TestRange -nop4 -utf8output -unattended -nocompileeditor "${GAME_LINE[@]}" "$@" 2>&1 | tee "$OUT/package.log"
rc=${pipestatus[1]}
echo "package exit $rc"
APK=$(find "$OUT" -name "*.apk" -newer "$OUT/.started" | head -1)
if [[ $rc -ne 0 || -z "$APK" ]]; then
  echo "PACKAGE FAILED (see $OUT/package.log)"
  exit 1
fi
echo "APK: $APK"
echo "install: adb install -r \"$APK\""
