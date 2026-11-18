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
# and the player collects it in their own wallet. In a sixth (MatchTest.cpp), a match between two
# factions is played to its end three times in the arena, each as three games: the publisher's
# match server, which has no player of its own, and two players' games, each let in by a ticket for
# the faction its wallet is a member of. Each faction's built structures stand on its base, the
# same on both machines; a player takes a rifle from a weapon crate. Once the time runs out and
# nobody wins; once a faction is first to the kills to win, its fallen opponent returning on its own
# base; once a faction's base is wiped and it is out. The match server tells the publisher's server
# how each ended. A third game, whose wallet is in neither faction, joins one of them: it is given
# no ticket and no hero, and is put out. In a seventh (HeroShopTest.cpp), a player whose wallet holds
# D-bucks buys a hero in the character select, chooses it, hosts a lobby, and comes in as it once the
# lobby's server has checked the hero pass with the publisher's server; then plays it on the test range.
# The publisher's account and every wallet are real dsm-app-host accounts on a
# storage set; nothing stands in for DSM. Screenshots and logs in Saved/BaseEditor/,
# Saved/BasePeople/, Saved/BaseShop/, Saved/BaseSession/, Saved/BasePrize/ and Saved/Match_Time/,
# Saved/Match_Kills/, Saved/Match_Wipe/.
#
# SENNITE_TOUCH=1 plays the base editor and people checks by touch, as on a phone: each key the check
# presses is a tap on the button that presses it, and the cursor is moved by a tap on the map:
# SENNITE_TOUCH=1 npx vitest run --config vitest.game.config.ts test/game/base-editor.test.ts test/game/people.test.ts
# The shop check (shop.test.ts) reads it too, only to show the touch buttons over the shop; it presses keys.
#
# Needs, as the suites on DSM do (Server/README.md):
#   DSM_APP_HOST_BIN   the dsm-app-host binary
#   DSM_ENV_CONFIG     the env config of the storage set it runs on
cd "$(dirname "$0")/../Server" || exit 2
[[ -d node_modules ]] || npm ci --no-audit --no-fund || exit 2
exec npx vitest run --config vitest.game.config.ts
