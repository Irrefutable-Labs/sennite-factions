# The publisher's server

Sennite: Factions plugs into DSM the way any app does (DSM's `BUILDING_APPS.md`): the game stays an
ordinary game, the publisher's server has a DSM account of its own through `dsm-app-host`, and each
player connects their DSM wallet to it with a connect code. No DSM code is linked into the game.
The server acts only on facts its own account established; a wallet's answer is a notification.

What is here:

| Folder | What it is |
|---|---|
| `src/faction/` | The faction state machine with no game and no account attached: the committed state, the kinds of change, the validity rules of spec section 7, and the mirror that walks a lineage. |
| `src/dsm/` | What the server asks of the publisher's account: cell writes and their facts, contributions and the funding read, authored objects. |
| `src/publisher/` | The publisher's server: parcel assignments, proposing members' changes, the 48-hour clock, what stands in play, and what the game client asks of it over HTTP (`service.ts`). `main.ts` starts it. |
| `world/` | The publisher's game assets: the prefab catalogue, the parcel registry, what the account issues. |
| `vendor/` | DSM's TypeScript client, packed from DSM `main` at `2d109bb58` (`clients/typescript`, `npm pack`). |
| `docs/dsm-gaps.md` | What the specification needs that DSM's app surface does not offer yet. |

## Checks

The rules on their own:

```bash
npm test
```

The suites on DSM. They run real `dsm-app-host` accounts (the publisher's and each player's wallet)
on a real storage set, and refuse to run without one:

```bash
DSM_APP_HOST_BIN=<dsm checkout>/target/release/dsm-app-host DSM_ENV_CONFIG=<dsm checkout>/.dsm-dev/net/dsm_env_config.toml npm run test:dsm
```

In a DSM checkout, `cargo build --release --locked -p dsm-app-host` builds the host and
`make nodes-up` starts five local storage nodes on PostgreSQL and writes that env config.

The game itself, with the same two settings and Unreal Engine on this machine. It runs the Unreal
game headless on the test range against this server, with a real wallet answering:

```bash
DSM_APP_HOST_BIN=<...> DSM_ENV_CONFIG=<...> ../Tools/check_base_editor.sh
```

## Running it for the game

The publisher's account is a `dsm-app-host` (DSM's `BUILDING_APPS.md` says how to run one; in a DSM
checkout, `make app-dev` or `make app-dev-local`). Then:

```bash
npm run build
```

```bash
DSM_APP_HOST=http://127.0.0.1:8787 DSM_APP_HOST_SECRET_FILE=<ingress-secret-dir>/ingress-token DSM_ENV_CONFIG=<env config> npm start
```

It serves the game on `http://127.0.0.1:8790`. The game finds it by `PublisherUrl` in
`Config/DefaultGame.ini` (`[/Script/Sennite.SennitePublisher]`) or `-PublisherUrl=` on its command
line. In the game, B opens the base editor; it shows a connect code for the player's DSM wallet
(with no phone, DSM's terminal wallet: `make dev-wallet`, then `make dev-wallet-connect CODE=...`).
