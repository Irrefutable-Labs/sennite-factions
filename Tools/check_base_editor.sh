#!/bin/zsh
# The base editor checks (Source/Sennite/Private/Tests/BaseEditorTest.cpp and BasePeopleTest.cpp,
# Server/test/game; the others are beside them). In the game, a player opens the planning view, connects a DSM wallet by the
# code the game shows, founds a faction on a parcel, places a watchtower as a plan, commits steel
# from the wallet, and the tower stands in play. Then, in a second run, a player invited by another
# joins from the game, votes that member out, invites them back and withdraws the invitation. In a
# third, a player trades ERA for D-bucks through the publisher's vault, buys a skin and wears it,
# then sells it to another player from their own wallet, and it comes off the hero. In a fourth,
# the publisher's server is killed while a player plans: the game goes on showing the base, says
# there is no live session, and builds on the same base when a server is started again. In a fifth,
# the player's faction wins a match: the game says a prize is waiting and where the wallet finds it,
# and the player collects it in their own wallet. In a sixth (MatchBasesTest.cpp), two games play a
# match in the arena between two factions: the publisher's match server hosts it and a player joins;
# each faction's built structures stand on its base of the arena, the same on both machines; the
# host shoots one down, it is gone for both, and the publisher's server records it. The publisher's
# account and every wallet are real dsm-app-host accounts on a storage set; nothing stands in for
# DSM. Screenshots and logs in Saved/BaseEditor/, Saved/BasePeople/, Saved/BaseShop/,
# Saved/BaseSession/, Saved/BasePrize/ and Saved/MatchBases/.
#
# Needs, as the suites on DSM do (Server/README.md):
#   DSM_APP_HOST_BIN   the dsm-app-host binary
#   DSM_ENV_CONFIG     the env config of the storage set it runs on
cd "$(dirname "$0")/../Server" || exit 2
[[ -d node_modules ]] || npm ci --no-audit --no-fund || exit 2
exec npx vitest run --config vitest.game.config.ts
