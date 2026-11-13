#!/bin/zsh
# The base editor check (Source/Sennite/Private/Tests/BaseEditorTest.cpp, Server/test/game): in the
# game, a player opens the planning view, connects a DSM wallet by the code the game shows, founds a
# faction on a parcel, places a watchtower as a plan, commits steel from the wallet, and the tower
# stands in play. The publisher's account and the player's wallet are real dsm-app-host accounts on
# a storage set; nothing stands in for DSM. Screenshots and log in Saved/BaseEditor/.
#
# Needs, as the suites on DSM do (Server/README.md):
#   DSM_APP_HOST_BIN   the dsm-app-host binary
#   DSM_ENV_CONFIG     the env config of the storage set it runs on
cd "$(dirname "$0")/../Server" || exit 2
[[ -d node_modules ]] || npm ci --no-audit --no-fund || exit 2
exec npx vitest run --config vitest.game.config.ts
