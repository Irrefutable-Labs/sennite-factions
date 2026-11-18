#!/bin/zsh
# The publisher gives building materials to every wallet connected to its account, from what the
# account issued, so players on phones have something to build with:
#   Tools/phone_give.sh STEEL 300
# Needs Tools/phone_publisher.sh running. A wallet takes an object only once it has accepted it
# (the game's connection asks for that); a give before then fails and says so.
cd "$(dirname "$0")/../Server" || exit 2
RESOURCE="${1:?which resource: WOOD, STONE or STEEL}"
AMOUNT="${2:?how much}"
RUN="${SENNITE_PUBLISHER_DIR:-$PWD/../Saved/Publisher}"
DSM_APP_HOST=http://127.0.0.1:8787 DSM_APP_HOST_SECRET_FILE="$RUN/ingress/ingress-token" RESOURCE="$RESOURCE" AMOUNT="$AMOUNT" \
node --input-type=module -e '
  import { AppAccount, Ingress } from "dsm-app-client";
  const app = new AppAccount(await Ingress.fromSecretFile(process.env.DSM_APP_HOST, process.env.DSM_APP_HOST_SECRET_FILE));
  const wallets = (await app.connections()).filter((c) => c.connected);
  if (wallets.length === 0) { console.log("no wallet is connected"); process.exit(1); }
  let failed = 0;
  for (const c of wallets) {
    try {
      await app.give(c.peer, process.env.RESOURCE, process.env.AMOUNT);
      console.log(`gave ${process.env.AMOUNT} ${process.env.RESOURCE} to ${c.peer.slice(0, 8)} (${c.name})`);
    } catch (e) {
      failed++;
      console.log(`${c.peer.slice(0, 8)}: ${e}`);
    }
  }
  process.exit(failed ? 1 : 0);
'
