#!/bin/zsh
# The building paused check (Source/Sennite/Private/Tests/BasePauseTest.cpp, Server/test/seats/game).
# In the game, a player founds a faction and places a watchtower. The storage seat that leads the
# faction's next change is then stopped: the game goes on showing the base, says building is
# paused, and a placement the player tries is not taken. The seat is started again, and the game
# places the same structure. The publisher's account, the wallets and the seats are real; nothing
# stands in for DSM. Screenshots and logs in Saved/BasePause/.
#
# It stops and starts storage_node processes, so it runs only on a developer's own set on this
# machine (in a DSM checkout: make nodes-up). Every seat is serving again when it ends.
#
# Needs (Server/README.md):
#   DSM_APP_HOST_BIN       the dsm-app-host binary
#   DSM_ENV_CONFIG         the env config of the developer's own storage set
#   DSM_STORAGE_NODE_BIN   the storage_node binary that set runs
cd "$(dirname "$0")/../Server" || exit 2
[[ -d node_modules ]] || npm ci --no-audit --no-fund || exit 2
exec npx vitest run --config vitest.seats-game.config.ts
