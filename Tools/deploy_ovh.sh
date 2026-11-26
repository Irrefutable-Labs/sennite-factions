#!/bin/zsh
# Deploys the publisher's side (the publisher's DSM account and the publisher's server) to the games host on
# OVH, from a committed revision, so any player reaches it over the internet. Run on the operator's computer,
# whose address the host's firewall lets in on SSH.
#
#   Tools/deploy_ovh.sh [revision]        default: HEAD (committed; the working tree is not sent)
#
#   SENNITE_HOST     the games host (default 148.113.137.67, dsm-games-1)
#   SSH_KEY          (default ~/.ssh/dsm-deploy)
#   DSM_ENV_CONFIG   the beta storage set's env config: DSM main's dsm_client/frontend/public/dsm_env_config.toml
#                    (first deploy, or with DSM_ENV_CONFIG set again to change it)
#   DSM_APP_HOST_TAG the dsm-app-host image on the host (default 512759a5d: DSM main, as Wildstate's and Open Boxes')
#
# On the host, /opt/sennite holds account/ (the publisher's DSM account: keys and RECOVERY_MNEMONIC; never
# replace it with an older copy), ingress/, server/ (the server's record and the match secret), network/,
# compose.yml and .env. A redeploy rebuilds the server's image and recreates the server; the account keeps running.
# The match server (Linux, built from Unreal source) is deployed by Tools/deploy_ovh_match_server.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
HOST="${SENNITE_HOST:-148.113.137.67}"
KEY="${SSH_KEY:-$HOME/.ssh/dsm-deploy}"
TAG="${DSM_APP_HOST_TAG:-512759a5d}"
REV=$(git rev-parse --short=9 "${1:-HEAD}^{commit}")
[[ "$HOST" =~ '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$' && "$TAG" =~ '^[A-Za-z0-9._-]+$' ]] || { echo "SENNITE_HOST must be an IPv4 address and DSM_APP_HOST_TAG an image tag" >&2; exit 2; }
SSH=(ssh -o BatchMode=yes -i "$KEY" "ubuntu@$HOST")

echo "==> sending $REV to $HOST"
SRC=$("${SSH[@]}" 'mktemp -d -p "$HOME" sennite-src.XXXXXX')
trap '"${SSH[@]}" "rm -rf $SRC" 2>/dev/null' EXIT
git archive --format=tar "$REV" Server deploy | "${SSH[@]}" "tar -x --no-same-owner -C $SRC"

echo "==> building sennite-publisher:$REV"
"${SSH[@]}" "cd $SRC && sudo docker build -q -f deploy/publisher/Dockerfile -t sennite-publisher:$REV Server"

echo "==> /opt/sennite"
"${SSH[@]}" "sudo install -d -m 755 /opt/sennite /opt/sennite/network && sudo install -d -m 700 -o 10001 -g 10001 /opt/sennite/account /opt/sennite/ingress /opt/sennite/server && sudo install -m 644 $SRC/deploy/compose.yml /opt/sennite/compose.yml"
if [[ -n "${DSM_ENV_CONFIG:-}" ]]; then
  "${SSH[@]}" 'sudo tee /opt/sennite/network/dsm_env_config.toml >/dev/null' < "$DSM_ENV_CONFIG"
fi
"${SSH[@]}" 'test -s /opt/sennite/network/dsm_env_config.toml' || { echo "first deploy: set DSM_ENV_CONFIG" >&2; exit 2; }
"${SSH[@]}" "printf 'DSM_APP_HOST_TAG=%s\nPUBLIC_IP=%s\nSENNITE_REV=%s\n' $TAG $HOST $REV | sudo tee /opt/sennite/.env >/dev/null && sudo chmod 600 /opt/sennite/.env"

echo "==> firewall and HTTPS (once)"
"${SSH[@]}" 'sudo ufw status | grep -q "^8445/tcp" || sudo ufw allow 8445/tcp >/dev/null'
"${SSH[@]}" "grep -q '^sennite\.' /opt/https/Caddyfile || { printf '\nsennite.%s.sslip.io {\n    reverse_proxy 127.0.0.1:8790\n}\n' $HOST | sudo tee -a /opt/https/Caddyfile >/dev/null && cd /opt/https && sudo docker compose -p https restart https; }"

echo "==> starting"
"${SSH[@]}" 'cd /opt/sennite && sudo docker compose -p sennite up -d'

if ! "${SSH[@]}" 'sudo test -e /opt/sennite/server/funded'; then
  echo "==> first start: the faucet funds the publisher's account"
  "${SSH[@]}" 'for i in $(seq 120); do sudo test -s /opt/sennite/ingress/ingress-token && break; sleep 1; done
    cd /opt/sennite && sudo docker compose -p sennite run --rm --no-deps publisher node --input-type=module -e '"'"'
      import { AppAccount, Ingress } from "dsm-app-client";
      const app = new AppAccount(await Ingress.fromSecretFile(process.env.DSM_APP_HOST, process.env.DSM_APP_HOST_SECRET_FILE));
      console.log(`asking the faucet to fund ${await app.id()}`);
      for (let i = 0; i < 40; i++) {
        const claim = await app.fund().catch((e) => ({ funded: false, message: String(e) }));
        if (claim.funded) { console.log("funded"); process.exit(0); }
        console.log(`not yet: ${claim.message}`);
        await new Promise((r) => setTimeout(r, 6000));
      }
      process.exit(1);
    '"'"' && sudo install -o 10001 -g 10001 -m 600 /dev/null /opt/sennite/server/funded && sudo docker compose -p sennite restart publisher'
fi

echo "==> check"
sleep 5
"${SSH[@]}" 'cd /opt/sennite && sudo docker compose -p sennite ps && sudo docker compose -p sennite logs --tail 15 publisher'
for i in $(seq 1 30); do
  code=$(curl -s -o /dev/null -w '%{http_code}' "https://sennite.$HOST.sslip.io/shop" || true)
  [[ "$code" == 200 ]] && break
  sleep 5
done
[[ "$code" == 200 ]] || { echo "https://sennite.$HOST.sslip.io/shop answers $code, not 200: see /opt/https (Caddy) and the publisher's logs" >&2; exit 1; }
echo "publisher: https://sennite.$HOST.sslip.io   wallet relay: https://$HOST:8445"
