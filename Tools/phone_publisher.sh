#!/bin/zsh
# The publisher's side of the game, for phones on this computer's Wi-Fi: the publisher's DSM account
# (a dsm-app-host) with its relay open on the Wi-Fi, so the DSM Wallet app on a phone reaches it, and
# the publisher's server (Server/) open on the Wi-Fi, so the game on a phone reaches it. Both run until
# Ctrl-C. The account and the server's record are kept in Saved/Publisher/ between runs.
#
#   DSM_APP_HOST_BIN   dsm-app-host, built from the DSM commit the storage set runs (the beta set: DSM main)
#   DSM_ENV_CONFIG     that storage set's env config (the beta set: dsm_client/frontend/public/dsm_env_config.toml
#                      of the same DSM checkout)
#   SENNITE_LAN_IP     this computer's address on the Wi-Fi (default: en0's)
#   SENNITE_PUBLISHER_DIR  where the account and the server's record are kept (default Saved/Publisher):
#                      one folder for each storage set, as an account is made on one set
#
# The first run asks the network's faucet for ERA: a new account pays for its own writes. Then, with
# the phone on adb: Tools/phone_game.sh points the game on the phone here. Tools/phone_give.sh gives
# a connected wallet building materials to play with.
#
# The account in Saved/Publisher/host is a hot wallet (DSM's BUILDING_APPS.md): its RECOVERY_MNEMONIC is
# in that folder. Keep only test balances on it.
cd "$(dirname "$0")/.." || exit 2
: "${DSM_APP_HOST_BIN:?set DSM_APP_HOST_BIN to the dsm-app-host binary}"
: "${DSM_ENV_CONFIG:?set DSM_ENV_CONFIG to the storage set's env config}"
IP="${SENNITE_LAN_IP:-$(ipconfig getifaddr en0)}"
[[ -n "$IP" ]] || { echo "no Wi-Fi address: set SENNITE_LAN_IP" >&2; exit 2; }
RUN="${SENNITE_PUBLISHER_DIR:-$PWD/Saved/Publisher}"
mkdir -p "$RUN/host" "$RUN/ingress" "$RUN/server"
rm -f "$RUN/ingress/ingress-token"

"$DSM_APP_HOST_BIN" --data-dir "$RUN/host" --ingress-secret-dir "$RUN/ingress" --env-config "$DSM_ENV_CONFIG" \
  --game-bind 127.0.0.1:8787 --relay-bind 0.0.0.0:8443 --relay-endpoint "https://$IP:8443" --relay-name "$IP" \
  > "$RUN/host.log" 2>&1 &
HOST=$!
trap 'kill $HOST 2>/dev/null' EXIT INT TERM
for i in {1..120}; do
  [[ -s "$RUN/ingress/ingress-token" ]] && break
  kill -0 $HOST 2>/dev/null || { echo "dsm-app-host stopped:" >&2; tail -20 "$RUN/host.log" >&2; exit 1; }
  sleep 1
done
[[ -s "$RUN/ingress/ingress-token" ]] || { echo "dsm-app-host wrote no ingress secret in 120 s (see $RUN/host.log)" >&2; exit 1; }
echo "[phone] the publisher's account: relay https://$IP:8443 (log $RUN/host.log)"

export DSM_APP_HOST=http://127.0.0.1:8787
export DSM_APP_HOST_SECRET_FILE="$RUN/ingress/ingress-token"
cd Server || exit 2
[[ -d node_modules ]] || npm ci --no-audit --no-fund || exit 2
npm run build --silent || exit 1

if [[ ! -e "$RUN/funded" ]]; then
  node --input-type=module -e '
    import { AppAccount, Ingress } from "dsm-app-client";
    const app = new AppAccount(await Ingress.fromSecretFile(process.env.DSM_APP_HOST, process.env.DSM_APP_HOST_SECRET_FILE));
    console.log(`[phone] asking the faucet to fund ${await app.id()}`);
    for (let i = 0; i < 40; i++) {
      const claim = await app.fund().catch((e) => ({ funded: false, message: String(e) }));
      if (claim.funded) { console.log("[phone] funded"); process.exit(0); }
      console.log(`[phone] not yet: ${claim.message}`);
      await new Promise((r) => setTimeout(r, 6000));
    }
    process.exit(1);
  ' || { echo "the faucet did not fund the publisher's account" >&2; exit 1; }
  touch "$RUN/funded"
fi

echo "[phone] the publisher's server: http://$IP:8790"
SENNITE_DATA_DIR="$RUN/server" SENNITE_BIND="0.0.0.0:8790" node dist/publisher/main.js &
SERVER=$!
# both stop together: Ctrl-C, or either one stopping
trap 'kill $SERVER $HOST 2>/dev/null' EXIT INT TERM
while kill -0 $SERVER 2>/dev/null && kill -0 $HOST 2>/dev/null; do
  sleep 1
done
