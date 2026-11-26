#!/bin/zsh
# Makes the game's Android app icon from the owner's Sennite logo (Brand/sennite_logo.png, the split moon), into
# Build/Android/res, which Unreal lays over its own Android resources when it packages (Tools/package_android.sh).
#   Tools/make_app_icon.sh
# Two kinds: the plain icon at every density (drawable*/icon.png), for older phones and launchers; and an adaptive
# icon (drawable-anydpi-v26/icon.xml, Android 8 and later): the moon in the middle of its own night-blue, the
# launcher's mask cutting only the blue round it, so the moon is never clipped.
set -e
cd "$(dirname "$0")/.."
SOURCE=Brand/sennite_logo.png
RES=Build/Android/res
BLUE=03041D   # the logo's own background, at its edges
[[ -f "$SOURCE" ]] || { echo "the logo is not there: $SOURCE" >&2; exit 2; }

for pair in drawable:48 drawable-ldpi:36 drawable-mdpi:48 drawable-hdpi:72 drawable-xhdpi:96 drawable-xxhdpi:144 drawable-xxxhdpi:192; do
  dir=${pair%%:*}; size=${pair##*:}
  mkdir -p "$RES/$dir"
  sips -s format png -z $size $size "$SOURCE" --out "$RES/$dir/icon.png" >/dev/null
done

# adaptive: 108 units across, the middle 72 shown; the moon fills 64 of them, the rest the logo's blue
mkdir -p "$RES/drawable-nodpi" "$RES/drawable-anydpi-v26" "$RES/values"
sips -s format png -z 256 256 "$SOURCE" --out "$RES/drawable-nodpi/icon_foreground_moon.png" >/dev/null
sips -p 432 432 --padColor $BLUE "$RES/drawable-nodpi/icon_foreground_moon.png" --out "$RES/drawable-nodpi/icon_foreground.png" >/dev/null
rm "$RES/drawable-nodpi/icon_foreground_moon.png"
cat > "$RES/values/sennite_icon.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<!-- Made by Tools/make_app_icon.sh: the Sennite logo's background, behind the adaptive icon's moon. -->
<resources>
    <color name="sennite_icon_background">#$BLUE</color>
</resources>
XML
cat > "$RES/drawable-anydpi-v26/icon.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<!-- Made by Tools/make_app_icon.sh: the game's icon on Android 8 and later. -->
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/sennite_icon_background" />
    <foreground android:drawable="@drawable/icon_foreground" />
</adaptive-icon>
XML
echo "the app icon is in $RES"
