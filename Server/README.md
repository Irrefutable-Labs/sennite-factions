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
| `src/publisher/` | The publisher's server: parcel assignments, proposing members' changes, the 48-hour clock, what stands in play. |
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
