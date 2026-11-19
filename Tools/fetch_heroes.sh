#!/bin/zsh
# Copies the Fab hero packs the game uses into Content/. The packs are not in this repository
# (about 1.6 GB each); they come from the project the Epic launcher downloaded them into.
#   Tools/fetch_heroes.sh                 the heroes the game uses now (Config/DefaultGame.ini, SenniteHeroes)
#   Tools/fetch_heroes.sh ParagonCrunch   a named pack
cd "$(dirname "$0")/.."
SOURCE="${HERO_SOURCE:-$HOME/Documents/Unreal Projects/ClayLook/Content}"
# zsh does not split a default list into words: name the packs as an array
default=(ParagonDekker ParagonWraith ParagonCrunch ParagonDrongo ParagonHowitzer ParagonRampage ParagonSevarog SciFITrooper_Man_03)
(( $# )) && packs=("$@") || packs=($default)
for pack in $packs; do
  if [[ -d "Content/$pack" ]]; then
    echo "$pack: already in Content/"
  elif [[ -d "$SOURCE/$pack" ]]; then
    cp -Rc "$SOURCE/$pack" "Content/$pack" && echo "$pack: copied from $SOURCE"
  else
    echo "$pack: not found in $SOURCE (add it to a project from Fab, or set HERO_SOURCE)" >&2
    exit 1
  fi
done
