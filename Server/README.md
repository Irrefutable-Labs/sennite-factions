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
| `src/publisher/` | The publisher's server: parcel assignments, proposing members' changes, the 48-hour clock, what stands in play, the shop (`shop.ts`: D-bucks, the vault that sells them for ERA, skins), matches and their prizes (`matches.ts`), and what the game client asks of it over HTTP (`service.ts`). `main.ts` starts it. |
| `world/` | The publisher's game assets: the prefab catalogue, the parcel registry, what the account issues, the shop (`shop.json`: D-bucks, the vault's opening reserves and fee, the skins and their prices), and what a match pays (`matches.json`). |
| `vendor/` | DSM's TypeScript client, packed from DSM `main` at `262050ec0` (`clients/typescript`, `npm pack`; 0.1.1-beta.4). It needs a `dsm-app-host` from the same DSM: `wait` holds on the host's `/activity/{after}/wait`. |
| `docs/dsm-gaps.md` | What the specification needs that DSM's app surface does not offer yet. |
| `formal/` | Formal models of the lineage and of supply (spec 12.4), in TLA+, and the script that checks them. |

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

Among them, `host-change.test.ts` runs the server as processes of its own (it builds `dist/` first):
one is stopped and another takes over, one is killed with a member's change in flight, two run at
once, and an old one comes back and writes. `conservation.test.ts` counts every unit after each of
24 drawn acts (contribute, withdraw, leave, replace, dismantle); `SENNITE_SEED` draws another
sequence.

`prizes.test.ts` is a match's prize (spec 10.6): locked before the match as one vault per member's
share on one verdict cell, decided once by the publisher's key, collected by the winners in their
own wallets and by nobody else, and whole again with the publisher when nobody won. In it a server
is killed while it opens a match, and the one started on its record locks no share twice.

The runs that take storage seats offline (spec Stage 4: the leader seat). They stop and start the
set's own `storage_node` processes, so they run only on a developer's own set on this machine
(`make nodes-up`), never on a hosted one, and need the node binary as well:

```bash
DSM_APP_HOST_BIN=<...> DSM_ENV_CONFIG=<...> DSM_STORAGE_NODE_BIN=<dsm checkout>/target/release/storage_node npm run test:seats
```

Every seat is serving again when they end, whatever happened.

The game itself, with the same two settings and Unreal Engine on this machine. It runs the Unreal
game headless against this server, six times, with real wallets answering. Five are on the test
range: the base editor (plan, commit, built in play), the faction's people (join, vote, invite), the
shop (D-bucks, a skin bought, worn and sold on), the server killed while a player plans (the game
shows the base, says there is no live session, and builds on the same base when a server is back),
and a match won (the game says a prize is waiting and where the wallet finds it, and the player
collects it in their own wallet). The sixth is a match in the arena, played to its end three times,
each as three games: the publisher's match server, which has no player of its own, and two players'
games, each let in by a ticket for the faction its wallet is a member of. Both factions' built
structures stand on their bases, the same on both machines, and a player takes a rifle from a
weapon crate. Each of the three is on a different arena, by its seed: floating pieces, a canyon and
a traffic deck between the bases. Once the time runs out and nobody wins, and the prize is the publisher's again; once
a faction is first to the kills to win, and its member has the prize to collect; once a faction's
base is wiped, it is out, and this server has the structure as destroyed. The match server itself
tells this server how each ended. A third game, whose wallet is in neither faction, joins the first
match: it is given no ticket and no hero, and the match server puts it out:

```bash
DSM_APP_HOST_BIN=<...> DSM_ENV_CONFIG=<...> ../Tools/check_base_editor.sh
```

And the game with a seat taken offline, on a developer's own set: the player builds, the leader seat
of the next change is stopped, the game goes on showing the base and says building is paused, and
it builds again when the seat returns:

```bash
DSM_APP_HOST_BIN=<...> DSM_ENV_CONFIG=<...> DSM_STORAGE_NODE_BIN=<...> ../Tools/check_building_paused.sh
```

## Formal models (spec 12.4)

Spec 12.4 lists four things to show, each with a formal model, before the mechanism is called safe.
`formal/` has two TLA+ models, checked by TLC, the TLA+ model checker:

```bash
TLA_TOOLS_JAR=<path>/tla2tools.jar npm run check:formal
```

It needs Java 11 or later (`JAVA` names one that is not on the path) and `tla2tools.jar` from
github.com/tlaplus/tlaplus; a DSM checkout has one in `tla/`.

| 12.4 | Model | What TLC checks |
|---|---|---|
| At most one successor per faction state | `FactionLineage.tla` | `OneSuccessor`: a cell's occupant, once there, is never another. |
| Two mirrors that read the same cells hold the same state | `FactionLineage.tla` | `MirrorsAgree`: mirrors at the same cell hold the same state, and one that is behind holds a state the others passed through. `MirrorsHoldTheCells`: a mirror holds only what the cells hold, Final. |
| No sequence of contribute, withdraw, leave, replace and dismantle creates or destroys a unit | `FactionSupply.tla` | `Conserved`, `NothingSpentTwice`, `ReleasePaysWhomItNames`, `FundingIsInVaults`. |
| A host, a non-member and a stale writer cause delay and nothing else | both | `OccupantsAreWriters` and `OnlyMembersChangeTheBase` with any key writing anything at any cell; `Built`: with a host that carries nothing, a non-member writing, stale writes arriving and the leader seat lost twice, a member who carries their own change still builds, on every mirror. |

The script makes three kinds of run and prints one line for each:

- **holds**: the properties hold in every state TLC reaches.
- **red**: one rule is left out of the model, and the property that rule is there for must fail.
  Eleven rules, one run each (the last arrival holds the cell; a non-member's write is recognized; a
  mirror takes what the host sends; a vault releases twice; and so on).
- **reached**: a case the models are about is given to TLC as something that never happens, and
  TLC must find it happening: a race for a cell, a stale write, a non-member's write, an invalid
  occupant, a removal by vote, a join, a mirror catching up, a shortfall after a withdrawal.

What the models are, and are not:

- **Taken from DSM, not shown here.** A keyed cell is held by the first recognized write to arrive
  at its leader, and a seat never alters or loses it; a vault debits its owner, and is released
  whole, once, to the identity its outcome pays. These are the models' starting rules (`Arrive`,
  `Contribute`, `Release`). Showing them is DSM's.
- **Written by hand from the rules.** `Judge`, `Applied` and `Settled` follow `judge`, `applied` and
  `settled` in `src/faction/validity.ts`; `Walk` follows `Mirror.walk`; `Counts` and `Funded` follow
  `Contributions.funding`. Nothing checks by machine that the TypeScript and the models agree.
- **Small.** Two mirrors, up to three members and one more key, one spot on the parcel, three
  successor cells (`CELLS=4` for four), two vaults of one resource. Placement geometry is one rule:
  a spot holds one structure.
- **Not the negative tests on real devices** that 12.4 also asks for. Those are not done: every
  run of this server so far is on one Mac.

## Matches

The publisher's match server opens and decides matches with the secret the server writes to
`<SENNITE_DATA_DIR>/match-secret` at each launch (the same one it records destruction with):

- `POST /matches/<id>` with `{"factions": [...]}` opens a match and locks its prize. Asked again, it
  locks only what is not locked yet.
- `POST /matches/<id>/outcome` with `{"outcome": "<faction id>"}` or `"void"` decides it.
- `GET /matches/<id>` reads the prize, share by share, as DSM holds it.
- `GET /matches/<id>/bases` gives what the match is assembled from: each faction's parcel and built
  structures, and `mining`, the whole units of each material the arena's deposits hold between them
  (`world/matches.json`).
- `POST /matches/<id>/mined` with `{"mined": [{"signingKey", "resource", "units"}, ...]}`, once the
  match is decided, says what each member's mining droid took. The publisher's account gives each
  member's wallet that much, out of its own holdings: at once to a wallet that is connected, and to
  the others when they next are. Only a member the prize was shared among is paid, only for a material
  the arena held, and all of them together no more of it than the arena held; a report that says
  otherwise is refused whole. The first report recorded stands.

The match server is the game itself: it decides a match by the game's own rules (first to the kills
to win, the time, a base wiped) and says so here when the match ends.

A player's game reads what its wallet has won and not collected at `GET /players/me/prizes`.

## Materials in the shop

The shop sells wood, stone and steel in packs and buys them back (`world/shop.json`, `materials`: how many
units a pack holds, a pack's price in D-bucks for each material, and `buyBackPercent`, what a pack sold
back pays in hundredths of that price; always under 100, so buying and selling in a circle only loses).
`GET /shop` lists the packs. `POST /shop/materials` with `{"symbol": "WOOD", "way": "buy"}` asks the
wallet to send the pack's price in D-bucks, and gives it the pack once the publisher's account has that
transfer; with `"way": "sell"` it asks the wallet to send the pack, and pays it once the account has it.
Either way the wallet's half is in no grant and waits in the wallet for the player, as every payment
does; one approved after the game stopped waiting is carried out when it lands. What a trade was asked
at is kept with it. The shop gives its half once: that it is giving is written down before it gives, and
a trade found cut off at that point is left for the publisher, not given again (`Shop.cutOff`, and
`Matches.minedCutOff` for what was mined).

A match is played in an arena assembled for it (spec 10.3), not on the persistent map:

- `GET /matches/<id>/bases` gives the match server what the match is assembled from: for each
  faction of the match, in the match's order, its parcel's boundary and its built structures, read
  from that faction's latest state. A plan, and a structure a battle destroyed, are not in it.
- `POST /factions/<id>/destroyed` with `{"instanceId": ...}` records that a battle destroyed a built
  structure. It is a plan again, with its funding as it was, until a member rebuilds it.

A player plays for the faction their wallet is a member of, and this server is who says so:

- `POST /matches/<id>/tickets`, from a player's game, gives a ticket when the connected wallet is a
  member of one of the match's factions. It is good once, for two minutes, for that match alone.
  The game shows it to the match server; nothing else of the player's place here crosses the
  match's own wire.
- `POST /matches/<id>/entries` with `{"ticket": ...}`, from the match server, trades it for the
  wallet and the faction it plays for.

The game that is the match server is started on the arena with the match on the map's address and
this server and its secret on the command line:

```bash
UnrealEditor-Cmd Sennite.uproject "/Game/Maps/Arena?Bases=2?Match=<id>" -server -PublisherUrl=http://127.0.0.1:8790 -MatchSecretFile=<SENNITE_DATA_DIR>/match-secret
```

It has no player of its own. It sets each faction's parcel on one base of the arena, in the match's
order, and sends where every structure stands to every game in the match. `?Bases=` is the number
of factions in the match. With no `?Seed=` the match server picks the arena's seed itself, so what
lies between the bases differs from match to match; `?Seed=<n>` has one arena again. A faction with nothing built cannot be in a match (spec 10.3: an empty
parcel does not appear in one). A player's game joins at the match server's address, started with
this server (`-PublisherUrl=`) so it can ask for its ticket. When the match is over the match
server tells this server who won, or `void`, and closes.

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
