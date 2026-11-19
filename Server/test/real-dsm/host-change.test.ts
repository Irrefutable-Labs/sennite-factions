/**
 * Host change and the old host (spec Stage 4; sections 6.5, 9.3, 9.4 and 11).
 *
 * In this build the publisher's server is what the specification calls the host: it runs the session
 * the players' games talk to, asks each member's wallet for that member's change, and reads what
 * holds each cell. It signs nothing: every change is signed and written by a member's own wallet,
 * so a server, old or new, can do no more to a base than ask a member.
 *
 * Each server here is the real program (`node dist/publisher/main.js`) as a process of its own, on
 * the publisher's account. The game is this file's HTTP calls. Servers are stopped, killed, run two
 * at once and started again on an old record; the base is read back through each.
 *
 * A server's record names the writes it asked. The publisher's account cannot read a cell's occupant
 * for a write the server did not ask (docs/dsm-gaps.md, 1), so a handoff carries the record, and a
 * server that meets another server's change says it is behind and builds nothing until it is handed
 * the record.
 *
 * Run with `npm run test:dsm` (see hosts.ts for what it needs).
 */
import { randomBytes } from "node:crypto";
import { cpSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { hex, unhex } from "../../src/faction/canonical.js";
import { decodeChange } from "../../src/faction/change.js";
import { BEHIND } from "../../src/publisher/publisher.js";
import type { AskedWrite, StoreData } from "../../src/publisher/store.js";
import { until } from "./hosts.js";
import { build, GameServer } from "./servers.js";
import { accounts, GAME, type Accounts } from "./stage.js";
import { holds } from "./wallets.js";

const PARCEL = "north-bluff";
const STEEL = "STEEL";
const [OX, OY] = [-8160, -8160];
const OPEN = { open: true, message: "" };

type Json = Record<string, any>;

const place = (prefabId: string, x: number, y: number) => ({
  kind: "place",
  instanceId: hex(randomBytes(16)),
  prefabId,
  x: OX + x,
  y: OY + y,
  rotation: 0,
  config: {},
});

let a: Accounts;
let first: GameServer;
const started: GameServer[] = [];
const game: Record<string, string> = {};
let changes = "";

const base = async (server: GameServer, token = ""): Promise<Json> => (await server.call("GET", `/parcels/${PARCEL}/base`, token)).body;
/** The structures of a base, by instance_id, in one order whatever order they are listed in. */
const ids = (b: Json | string[]): string[] => (Array.isArray(b) ? [...b] : (b.structures as Json[]).map((x) => x.instanceId as string)).sort();

async function server(name: string, dataDir: string): Promise<GameServer> {
  const one = await GameServer.start(name, dataDir, a.publisherHost, a.dir);
  started.push(one);
  return one;
}

/** The record of `from`, handed to a server named `name`: a copy, in a folder of its own. */
function handedOver(from: GameServer, name: string): string {
  const dir = join(a.dir, `server-${name}`);
  cpSync(from.dataDir, dir, { recursive: true });
  return dir;
}

const record = (of: GameServer): StoreData => JSON.parse(readFileSync(of.record, "utf8")) as StoreData;
const askedCount = (of: GameServer): number => Object.values(record(of).asked).flat().length;

/** The write a server asked for the placement of `instanceId`. */
function askedFor(of: GameServer, instanceId: string): AskedWrite {
  for (const one of Object.values(record(of).asked).flat()) {
    const read = decodeChange(unhex(one.payload, "the asked payload"));
    if (read.ok && read.signedOver.change.kind === "place" && read.signedOver.change.instanceId === instanceId) return one;
  }
  throw new Error(`the server ${of.name} asked no write that places ${instanceId}`);
}

/** Who owns what: each wallet's steel, spendable and committed. */
async function ownership(): Promise<Record<string, { available: bigint; locked: bigint }>> {
  const out: Record<string, { available: bigint; locked: bigint }> = {};
  for (const [name, w] of Object.entries(a.wallets)) out[name] = await holds(await w.host.wallet(), STEEL);
  return out;
}

/** A player opens the game at `at`: it shows a connect code, and their wallet approves it. */
async function enter(at: GameServer, name: string): Promise<void> {
  const entered = await at.call("POST", "/players");
  expect(entered.status).toBe(201);
  const token = entered.body.player as string;
  const wallet = a.wallets[name]!.wallet;
  const preview = await wallet.preview(entered.body.code as string);
  expect(preview.name).toBe(GAME);
  await wallet.approve(preview);
  await until(`${name}'s game sees the wallet connected`, 240_000, async () => {
    const r = await at.call("GET", "/players/me", token);
    return r.body.connected === true ? r.body : null;
  });
  game[name] = token;
}

const tower = place("watchtower", 480, 480);
/** The base as the first server showed it when it stopped. */
let atHandoff: Json;
/** The server the players' games are on. */
let live: GameServer;

beforeAll(async () => {
  a = await accounts("host-change", ["alice", "bob"]);
  build();
  first = await server("first", join(a.dir, "server-first"));
  await enter(first, "alice");
  await enter(first, "bob");
  const founded = await first.call("POST", "/factions", game.alice, { parcelId: PARCEL });
  expect(founded.status, JSON.stringify(founded.body)).toBe(201);
  changes = `/factions/${founded.body.factionId}/changes`;
  const faction = `/factions/${founded.body.factionId}`;

  expect(await first.settled(game.alice!, `${faction}/invitations`, { wallet: await a.wallets.bob!.wallet.id() })).toMatchObject({ ok: true });
  expect(await first.settled(game.bob!, changes, { change: { kind: "accept" } })).toMatchObject({ ok: true });
  expect(await first.settled(game.alice!, changes, { change: tower })).toMatchObject({ ok: true });

  const alice = a.wallets.alice!.wallet;
  const id = await alice.id();
  await until("the publisher gives alice 300 steel", 240_000, async () => {
    await a.app.give(id, STEEL, "300");
    return true;
  });
  await until("alice's wallet takes in 300 steel", 240_000, async () => ((await holds(alice, STEEL)).available === 300n ? true : null));
  expect(await first.settled(game.alice!, `${faction}/commitments`, { instanceId: tower.instanceId, resource: STEEL, amount: 300 })).toMatchObject({ ok: true });
  expect((await base(first, game.alice)).structures).toMatchObject([{ instanceId: tower.instanceId, standing: true }]);
});

afterAll(async () => {
  await Promise.all(started.map((one) => one.kill()));
  await a?.stop();
});

describe("host change", () => {
  it("a planned handoff: the next server takes over from the record handed to it, and nothing about who owns what changes", async () => {
    atHandoff = await base(first, game.alice);
    const owned = await ownership();
    expect(owned).toEqual({ alice: { available: 0n, locked: 300n }, bob: { available: 0n, locked: 0n } });
    await first.stop();

    const second = await server("second", handedOver(first, "second"));
    // The same base, to the byte of what a member's game is sent, and the same places in the game.
    expect(await base(second, game.alice)).toEqual(atHandoff);
    expect(atHandoff.building).toEqual(OPEN);
    expect((await second.call("GET", "/players/me", game.bob)).body).toMatchObject({ connected: true, faction: { parcelId: PARCEL } });
    expect(await ownership()).toEqual(owned);

    // Building goes on through it.
    const lookout = place("lookout", 2400, 480);
    expect(await second.settled(game.bob!, changes, { change: lookout })).toMatchObject({ ok: true });
    const after = await base(second, game.alice);
    expect(after.height).toBe(atHandoff.height + 1);
    expect(ids(after)).toEqual(ids([tower.instanceId, lookout.instanceId]));
    expect(await ownership()).toEqual(owned);
    live = second;
  });

  it("an unplanned one: the server is killed with a member's change in flight, and the change is not lost", async () => {
    const before = await base(live, game.alice);
    // Alice's wallet is away, so what the game asks of it is still in flight when the server dies.
    await a.wallets.alice!.host.stop();
    const wall = place("wall", 1680, 240);
    const asked = askedCount(live);
    await live.send(game.alice!, changes, { change: wall });
    await until("the server has asked the wallet for the change", 60_000, async () => (askedCount(live) > asked ? true : null));
    await live.kill();

    // A server started on the record as the kill left it. The change is no part of the base yet.
    const third = await server("third", live.dataDir);
    expect(await base(third, game.alice)).toMatchObject({ commitment: before.commitment, height: before.height });

    // The wallet returns and writes what it was asked: the change is found at its cell, with no server having carried it there.
    await a.wallets.alice!.host.start();
    const after = await until("the change in flight is part of the base", 600_000, async () => {
      const b = await base(third, game.alice);
      return b.height === before.height + 1 ? b : null;
    });
    expect(ids(after)).toEqual(ids([...ids(before), wall.instanceId]));
    live = third;
  });

  it("two servers at once, both carrying changes: one lineage results", async () => {
    const fourth = await server("fourth", handedOver(live, "fourth"));
    const start = await base(live, game.alice);
    expect(await base(fourth, game.alice)).toEqual(start);

    // Alice's game is on one server and Bob's on the other. Each places a structure at the same moment.
    const [mine, his] = [place("lookout", 3360, 480), place("lookout", 480, 1440)];
    const [sentA, sentB] = await Promise.all([live.send(game.alice!, changes, { change: mine }), fourth.send(game.bob!, changes, { change: his })]);
    const [ra, rb] = await Promise.all([live.outcome(game.alice!, sentA), fourth.outcome(game.bob!, sentB)]);
    expect([ra.ok, rb.ok].filter(Boolean), JSON.stringify([ra, rb])).toHaveLength(1);
    const [winner, loser, won, lost, refusal] = ra.ok ? [live, fourth, mine, his, rb] : [fourth, live, his, mine, ra];
    expect(refusal.message).toBe("another change holds the cell");

    // The publisher's account read one cell for both writes: one of them holds it, final.
    const [w, l] = await Promise.all(
      [askedFor(winner, won.instanceId), askedFor(loser, lost.instanceId)].map((one) => a.app.result({ connection: one.connection, seq: BigInt(one.seq) })),
    );
    expect([w!.fact, l!.fact]).toEqual(["written", "none"]);
    expect(hex(l!.state!.cell)).toBe(hex(w!.state!.cell));
    expect(hex(l!.state!.step!)).toBe(hex(w!.state!.step!));

    // One server is one change ahead. The other shows the base it held, says it is behind, and builds nothing.
    const ahead = await base(winner, game.alice);
    expect(ahead.height).toBe(start.height + 1);
    expect(ids(ahead)).toEqual(ids([...ids(start), won.instanceId]));
    expect(ahead.building).toEqual(OPEN);
    const behind = await base(loser, game.alice);
    expect(behind).toMatchObject({ commitment: start.commitment, height: start.height, building: { open: false, message: BEHIND } });
    expect(ids(behind)).toEqual(ids(start));
    expect(await loser.settled(game.alice!, changes, { change: place("wall", 1680, 1200) })).toMatchObject({ ok: false, message: BEHIND });
    expect((await base(winner, game.alice)).commitment).toBe(ahead.commitment);

    // Handed the record of the server that is ahead, it arrives at the same state: there was never a second base.
    await loser.stop();
    const again = await server(`${loser.name}-again`, handedOver(winner, `${loser.name}-again`));
    expect(await base(again, game.alice)).toEqual(ahead);
    await again.stop();
    live = winner;
  });

  it("the old server writing after a handoff: its write counts for nothing, and it holds an earlier state of the same lineage", async () => {
    const now = await base(live, game.bob);
    // The first server comes back on the record it stopped with. It is several changes behind and cannot know it.
    await first.start();
    const old = await base(first, game.bob);
    expect(old.building).toEqual(OPEN);
    expect(old.commitment).toBe(atHandoff.commitment);
    expect(old.height).toBeLessThan(now.height);
    expect(ids(old)).toEqual(ids(atHandoff));

    // A game still on it sends a change. Bob's wallet signs it against the old state and writes at a cell that is already held.
    const late = place("wall", 1680, 720);
    expect(await first.settled(game.bob!, changes, { change: late })).toMatchObject({ ok: false, message: "another change holds the cell" });
    const after = await base(live, game.bob);
    expect(after).toEqual(now);
    expect(ids(after)).not.toContain(late.instanceId);

    // From its own reading of that cell the old server now knows it is behind, and shows no other base.
    expect(await base(first, game.bob)).toMatchObject({ commitment: atHandoff.commitment, building: { open: false, message: BEHIND } });
    await first.stop();
  });
});
