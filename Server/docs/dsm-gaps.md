# What the specification needs that DSM's app surface does not offer yet

Sennite: Factions plugs into DSM the way Wildstate does: the publisher's server holds the publisher's
account through `dsm-app-host`, players connect their wallets by connect code, and the server acts
only on facts its own account established. This file lists each place where the specification
("Faction Territories — Player Experience and Specification", 12 November 2026) needs a call that
surface does not have, read against DSM `main` at `2d109bb58` (Amendment A35 merged). Nothing here is
built around: where a call is missing, the server reports what it could not establish.

Section numbers are the specification's.

## 1. Reading what holds an application cell

**Needed by:** 6.4 step 6 ("every mirror that learns of the change reads the cell"), 6.6 (the walk:
"read the successor cell, take its occupant, apply, repeat"), 7.4, 9.4, the failure table of 11
(a returning member, an old or altered snapshot, an old host writing after a handoff); Acceptance 1, 4
and 8.

**Missing:** a route on the application account's ingress, and a client call, that takes a cell's
name and writer keys and answers what holds the cell: nothing, or the occupant (its signer's key and
its payload, or the signed write's bytes), how far its chain has gone, its step, and what was passed
over. The Rust exists (`dsm_sdk::sdk::app_cell_flow::read`, `Verifier::read_app_cell`) and is called
only inside `connect.app.status`, for a write this account asked for.

**What the server does today** (`src/dsm/cells.ts`): it reads a cell through the status of the writes
it asked there. That is a real reading by the account, taken again every time. Its limits:

- A cell the server asked nothing at is *unread*. The newest state is known only as far as the
  server's own requests reach.
- A cell held by a write the server did not ask for (a member writing with their own software, which
  recognition allows) is *not established*: the account says another writer's write holds it and
  gives the step, but not the occupant. The mirror waits there, and nothing the server can call ends
  the wait.
- "Open", "held but not final" and "could not be read" are told apart only by the words of
  `fact_detail`. That the cell's leader seat was not read (6.7, 11: "the cell waits", building is
  paused) is the word `LeaderUnread` in it; the server pauses building on that word
  (`leaderUnread` in `src/dsm/cells.ts`).

**What it means for hosting (9.3, 9.4, 11).** The server's record of the writes it asked is the only
way it can name a cell's occupant, so:

- *Handoff.* 9.4 says a new host "starts a session from the last Final state". A new server does,
  but only when it is handed the old server's record. Without it, it reads nothing past R_0.
- *Two servers at once.* One lineage results, as 6.5 says. But the server whose member lost a cell
  cannot read the change that won it: it shows the state it held, says it is behind, and refuses to
  ask for more changes until it is handed the other server's record (`BEHIND` in
  `src/publisher/publisher.ts`). With a call that reads a cell's occupant it would simply follow.
- *A server killed between asking a write and recording that it asked.* The request is then in the
  account and not in the record, and the account has no call that lists the requests it made with
  what they asked. If that write takes the cell, a server started again meets a change it cannot
  read, as above. The suites kill the server after the record is written; this window is not shown.
- *An old or altered record.* A server started on an old record holds a true earlier state and
  learns that it is behind at its first write. A record whose asked payload was altered would be
  applied as written: the account compares the cell's occupant with the payload *it* was asked
  with, and the server cannot fetch that payload or the occupant's bytes back to compare. The record
  is the publisher's own file, not something a member or a game can offer; nothing takes a snapshot
  from outside.

- *A server started while a seat is offline.* A running server keeps the base it has read, so the
  base stays visible through an outage. A server started during one reads each past cell again, and
  the account's reading of a cell that is already final waits not only on that cell's leader but
  on the seats its completion names (seen in a run on 13 November 2026: `SeatUnread { position: 1 }`
  for a final cell while that seat was stopped). By the server's code, such a server shows the base
  as far as it could read and says building waits until the seat returns; that case was not run.

**Not available, and not shown:** 9.4, "a change that reached the leader but whose chain is
incomplete can be continued by any party". Nothing on the application account's surface continues
another writer's chain. From reading DSM's write path, the writing wallet's own write of the same
bytes, asked again, would continue it; that was not run here.

## 2. A member's machine reading for itself

**Needed by:** 2.2 ("their device keeps its own mirror of the base"), 6.6 ("a mirror accepts a change
only from its own read of the cell"), 9.1 ("each mirror checks storage itself"), 10.1 ("only a
faction's own members need to verify its lineage").

**Missing:** any way for a member's own side to read a cell. Under DSM Connect the accounts are the
publisher's and each player's wallet; the game client has no account and links no DSM code. So the
publisher's account is the one reader, and a member's game shows what the publisher's server serves.
A request kind a wallet answers from its own read of a cell (under the state grant) would give each
member a reading that is their own wallet's. Whether that, or something else, is what "mirror" means
in the Connect model is the owner's call.

## 3. More than one signature in one change (built another way, with what DSM has)

**The specification says** (7.2): a member joins with "an existing member and the joiner, both
signatures in the change", and a member is removed when "more than half of the members ... sign".

**What stands in the way:** a cell write carries one writer's signature and a payload of at most
16,384 bytes; one SPHINCS+ signature is 49,856 bytes.

**What is built** (owner, 2026-11-13: "Yes, build it"): each person's part is their own signed
change in the faction's lineage.

- Joining: a member writes an invitation naming the joiner. The state then lists the joiner as
  invited, and their key is one of the successor cell's writers. The joiner's wallet writes its own
  acceptance, the one change an invited wallet may make, and is a member from there. Both
  signatures are in the lineage, on two cells.
- Removal: each vote is the voter's own change. The state holds the votes that stand; when more
  than half of the other members have voted, the member is out at once.

Validity still comes from the parent state, the catalogue and the parcel alone, because the
invitation and the votes are in the state. The price is that the committed state holds who is
invited and which votes stand, and that an invited wallet, like a member, can spend a cell with a
change that counts for nothing (7.5); any member can withdraw the invitation.

## 4. Finding a member's contributions to a structure without a first commitment

**Needed by:** 8.2 ("a verdict cell derived from faction_id and the structure's instance_id, so every
commitment to one structure is found at one cell"), 8.3 condition 1, "anyone can check".

**As built in DSM:** each owner's contributions to one commitment sit at that owner's own verdict
cell, because the outcome table names the owner's key. Deriving that cell
(`contribution::verdict_cell`) is Rust only; the account names it only in a `contributed` fact.

**Missing:** a call that gives the verdict cell for what is committed to and an owner's key, or that
lists the contributions to a commitment by owner. Today the server reads a member's cell for a
structure only after that member has committed there once through the game (`escrow.locked` from
then on finds every vault on it, whoever made it). A vault a member locked in their own wallet,
without ever committing through the game, is not found. That can only leave funding lower than it
is, never higher.

## 5. Notes that are not gaps

- `escrow.locked`, `escrow.party`, `authored.publish` and `authored.read` are on the ingress but not
  in the `dsm-app-client` package; the server calls them through `ingress.call` (`src/dsm/routes.ts`).
- An offer connects one wallet: each player reads a connect code of their own.
- The state grant has no caps (12.2 item 3 says "within caps"): a write spends nothing. The offer's
  count of free requests (4,096 here) is the only bound; past it each write waits for the player.
- A cell lives on the storage set the wallet and the account are on. An application cannot name a
  set per faction (5.1 commits S per faction; 3 intends each faction's members to run their own
  seats one day). For the demonstration the publisher runs the five seats, so it is one set.
- The 48-hour hold on a leaver is a game rule (owner, 2026-11-12): the game does not ask a leaver's
  wallet to take back before the notice has run, and the publisher's key returns the vaults when it
  has. The leaver's own `withdraw` outcome exists throughout.
