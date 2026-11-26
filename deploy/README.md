# Where the publisher's side runs: OVH

Players reach the publisher over the internet, not on anyone's Wi-Fi. Everything below runs on the games host
`dsm-games-1` (OVH Public Cloud, BHS5, 148.113.137.67), which also runs Wildstate and Open Boxes; the owner's
computer runs none of it.

| What | Where players and wallets reach it | On the host |
| --- | --- | --- |
| The publisher's server (`Server/`: shop, factions, matches) | `https://sennite.148.113.137.67.sslip.io` (shared Caddy in `/opt/https`, Let's Encrypt) | container `sennite-publisher-1`, `127.0.0.1:8790` |
| The publisher's DSM account (`dsm-app-host`, built from DSM main) | wallet relay `https://148.113.137.67:8445` | container `sennite-account-1`, ingress `127.0.0.1:8788` (8787 is Wildstate's) |
| The match server, one per match (Linux dedicated server) | `148.113.137.67:7788`-`7797` UDP, typed in JOIN A LOBBY | container `sennite-match-<match id>`, image `sennite-match:latest` |

The APK names the publisher's server itself: `SENNITE_PUBLISHER_URL=https://sennite.148.113.137.67.sslip.io Tools/package_android.sh`.

## Redeploy

- **The publisher's server and account:** `Tools/deploy_ovh.sh [revision]` from a checkout of this repository
  (committed revisions only). It rebuilds the server's image from `git archive`, recreates the server and leaves the
  account running. The first deploy also needs `DSM_ENV_CONFIG=<DSM main>/dsm_client/frontend/public/dsm_env_config.toml`,
  funds the new account from the beta faucet and adds the HTTPS name and firewall ports.
- **The match server:** `Tools/build_linux_match_server.sh` from a full checkout (Content too), the same one the APK
  is packaged from: the engine refuses a game whose build differs from the server's. It builds on a Linux machine with
  a source-built Unreal Engine 5.8.4 (`BUILD_HOST`; Unreal builds dedicated servers only from source) and ships the
  image to the games host.
- **A match:** `Tools/ovh_match.sh <match id> [factions] [port]` starts its match server; `--list`, `--stop <id>`.

SSH to both machines: `ssh -i ~/.ssh/dsm-deploy ubuntu@<ip>`, allowed from the operator's address only (ufw).

## On the host

`/opt/sennite/`: `compose.yml`, `.env` (image tags, public IP), `network/dsm_env_config.toml` (the beta storage set),
`account/` (the publisher's DSM account: keys and `RECOVERY_MNEMONIC`; hot wallet, test balances only; never replace
it with an older copy and never run a second copy of it), `ingress/` (the ingress secret, new at each launch),
`server/` (`publisher.json`, the server's record, and `match-secret`, new at each launch).

From `/opt/sennite`: `sudo docker compose -p sennite ps`, `logs --tail 50 publisher`, `restart publisher`.

The account on OVH was made fresh on 2026-11-25 (`DVJJ3RT3…`): nothing of the accounts that ran on the owner's
computer was copied, so wallets connect to it as to a new app.
