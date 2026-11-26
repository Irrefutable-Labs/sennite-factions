#!/bin/zsh
# Builds the publisher's match server for Linux and ships it to the games host as the image sennite-match:<rev>.
# Unreal builds a dedicated server only from a source-built engine, so this runs on a Linux build machine that has
# one: Unreal Engine 5.8.4 from github.com/EpicGames/UnrealEngine (tag 5.8.4-release; the GitHub account must be
# linked to an Epic account) in ~/ue, its Setup.sh run once. The build machine needs ~200 GB of disk and 16 GB of RAM.
#
#   Tools/build_linux_match_server.sh            run on the operator's computer, in a full checkout (Content too)
#
#   BUILD_HOST      the build machine (default 148.113.157.139, sennite-build-1, made 2026-11-25 for this)
#   SENNITE_HOST    the games host (default 148.113.137.67)
#   SSH_KEY         (default ~/.ssh/dsm-deploy)
#
# The project is copied as it stands in this checkout (uncommitted changes too): the match server must be the same
# build as the players' games (Tools/package_android.sh), or the engine refuses them at the door.
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="${BUILD_HOST:-148.113.157.139}"
HOST="${SENNITE_HOST:-148.113.137.67}"
KEY="${SSH_KEY:-$HOME/.ssh/dsm-deploy}"
REV=$(git rev-parse --short=9 HEAD)
[[ -z "$(git status --porcelain -- Source Config Content Sennite.uproject)" ]] || REV="$REV-dirty"
B=(ssh -o BatchMode=yes -i "$KEY" "ubuntu@$BUILD")

echo "==> copying the project to $BUILD"
rsync -a --delete -e "ssh -o BatchMode=yes -i $KEY" --exclude .git --exclude Saved --exclude Intermediate --exclude Binaries \
  --exclude DerivedDataCache --exclude Server --exclude 'Build/Android*' ./ "ubuntu@$BUILD:sennite/"

echo "==> building the editor (to cook) and the server, cooking the arena, staging (long the first time)"
"${B[@]}" 'set -e; UE=$HOME/ue; P=$HOME/sennite/Sennite.uproject
  $UE/Engine/Build/BatchFiles/Linux/Build.sh ShaderCompileWorker Linux Development -waitmutex
  $UE/Engine/Build/BatchFiles/Linux/Build.sh SenniteEditor Linux Development -project=$P -waitmutex
  rm -rf $HOME/out
  $UE/Engine/Build/BatchFiles/RunUAT.sh BuildCookRun -project=$P -nop4 -utf8output -unattended \
    -platform=Linux -noclient -server -serverplatform=Linux -servertargetname=SenniteServer -serverconfig=Development \
    -build -cook -map=/Game/Maps/Arena -stage -pak -archive -archivedirectory=$HOME/out -nocompileeditor
  test -x $HOME/out/LinuxServer/Sennite/Binaries/Linux/SenniteServer'

echo "==> shipping sennite-match:$REV to $HOST"
"${B[@]}" 'tar -C $HOME/out -cz LinuxServer' | ssh -o BatchMode=yes -i "$KEY" "ubuntu@$HOST" \
  'rm -rf /tmp/sennite-match && mkdir -p /tmp/sennite-match && tar -xz -C /tmp/sennite-match'
git show HEAD:deploy/match/Dockerfile | ssh -o BatchMode=yes -i "$KEY" "ubuntu@$HOST" "cat > /tmp/sennite-match/Dockerfile && cd /tmp/sennite-match && sudo docker build -q -t sennite-match:$REV -t sennite-match:latest . && rm -rf /tmp/sennite-match"
echo "built sennite-match:$REV on $HOST; start a match with Tools/ovh_match.sh"
