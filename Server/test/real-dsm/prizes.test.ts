/**
 * Matches pay out materials, on DSM with no game attached (spec 10.6; the rest of build step 5).
 *
 * The publisher's account locks a match's prize in escrow before the match: one vault per member's
 * share, every one bound to the match's one verdict cell. The publisher's key decides once. A
 * winner collects their share in their own wallet; any other outcome pays the share back to the
 * publisher. Nobody else can take a share, the game cannot ask a wallet to collect one, and no
 * unit is created or lost at any step.
 *
 * Every balance here is read from the account that holds it, and every vault from DSM.
 *
 * Run with `npm run test:dsm` (see hosts.ts for what it needs).
 */
import { randomBytes } from "node:crypto";
import { readFileSync } from "node:fs";
import type { Server } from "node:http";
import type { AddressInfo } from "node:net";
import { join } from "node:path";
import { ask, decodeB32, pb } from "dsm-app-client";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { hex, unhex } from "../../src/faction/canonical.js";
import { lockedVaults, ownVaults, releaseVault, verdict } from "../../src/dsm/routes.js";
import { Matches, VOID, type MatchView } from "../../src/publisher/matches.js";
import { Refused, type Publisher } from "../../src/publisher/publisher.js";
import { Service } from "../../src/publisher/service.js";
import { Store, type StoreData } from "../../src/publisher/store.js";
import { until } from "./hosts.js";
import { build, GameServer } from "./servers.js";
import { player, stage, type Stage } from "./stage.js";
import { holds, lockEscrow } from "./wallets.js";

const RESOURCES = ["WOOD", "STONE", "STEEL"] as const;
type Resource = (typeof RESOURCES)[number];
const SUPPLY = 1_000_000n;
const NAMES = ["alice", "bob", "carol"];
/** What carol is given before any match, so that she has something of her own to lock. */
const CAROLS = 50n;

let s: Stage;
/** The publisher's server: the stage's, then one started again on the record a killed server left. */
let pub: Publisher;
/** Alice and Bob's faction, on North Bluff, and Carol's, on East Shore. */
let first: string;
let second: string;
/** The vault carol locks of her own on match-2's verdict cell. */
let carols: Uint8Array;

const key = (name: string): string => player(s, name).member.signingKey;
const record = (): StoreData => JSON.parse(readFileSync(s.storeFile, "utf8")) as StoreData;

/** The vault the server locked as `name`'s share of `resource` in a match. */
function vaultOf(matchId: string, name: string, resource: Resource): Uint8Array {
  const share = record().matches[matchId]?.shares.find((x) => x.signingKey === key(name) && x.resource === resource);
  if (share === undefined || share.vault === "") throw new Error(`${name} has no ${resource} share locked in ${matchId}`);
  return unhex(share.vault, "a vault");
}

/** What the publisher's account can spend of each material. */
async function publishers(): Promise<Record<Resource, bigint>> {
  const out = {} as Record<Resource, bigint>;
  for (const r of RESOURCES) out[r] = (await holds(s.app, r)).available;
  return out;
}

/** Every unit of every material is with the publisher, in a wallet, or in one of their Active vaults. */
async function conserved(after: string): Promise<void> {
  for (const r of RESOURCES) {
    let total = 0n;
    for (const account of [s.app, ...NAMES.map((n) => player(s, n).wallet)]) {
      const held = await holds(account, r);
      total += held.available + held.locked;
    }
    expect(total, `every unit of ${r} after ${after}`).toBe(SUPPLY);
  }
}

/**
 * A decided match once every share the verdict pays the publisher is back with it. A share that
 * does not come back at the first asking is asked for again, as the server's clock does each minute.
 */
async function settled(matchId: string): Promise<MatchView> {
  await until(`every share ${matchId} pays the publisher is back with it`, 240_000, async () => ((await pub.matches.tick()) === 0 ? true : null));
  return pub.matches.read(matchId);
}

const states = (view: MatchView, name: string): string[] => view.shares.filter((x) => x.signingKey === key(name)).map((x) => `${x.resource} ${x.amount} ${x.state}`);

/** The Active vaults DSM reads on a match's verdict cell. */
async function onCell(view: MatchView): Promise<pb.EscrowVaultV1[]> {
  const read = await lockedVaults(s.app, decodeB32(view.cell));
  expect(read.complete).toBe(true);
  return read.vaults.filter((v) => v.status === pb.SofiVaultStatus.ACTIVE);
}

beforeAll(async () => {
  s = await stage("prizes", NAMES);
  pub = s.publisher;
  const carol = player(s, "carol");
  await until("the publisher gives carol steel of her own", 240_000, async () => {
    await s.app.give(carol.connection, "STEEL", CAROLS.toString());
    return true;
  });
  await until("carol's wallet takes the steel in", 240_000, async () => ((await holds(carol.wallet, "STEEL")).available === CAROLS ? true : null));
  first = (await pub.assign(pub.founding("north-bluff", [player(s, "alice").member, player(s, "bob").member], hex(randomBytes(16))))).factionId;
  second = (await pub.assign(pub.founding("east-shore", [carol.member], hex(randomBytes(16))))).factionId;
});

afterAll(async () => {
  await s?.stop();
});

describe("a match's prize", () => {
  it("is locked before the match: one vault per member's share, out of the publisher's own holdings, on one verdict cell", async () => {
    await conserved("the factions are founded");
    const before = await publishers();

    await expect(pub.matches.open("match-1", [first])).rejects.toThrow(Refused);
    await expect(pub.matches.open("match-1", [first, first])).rejects.toThrow(Refused);
    await expect(pub.matches.open("match-1", [first, "00".repeat(32)])).rejects.toThrow(Refused);
    await expect(pub.matches.open("match 1", [first, second])).rejects.toThrow(Refused);
    expect(await publishers()).toEqual(before);

    const view = await pub.matches.open("match-1", [first, second]);
    expect(view).toMatchObject({ matchId: "match-1", factions: [first, second], outcome: "", opened: true, others: 0 });
    // The prize is 60 wood, 40 stone and 25 steel to the winning faction. Two members share it
    // equally in whole units; the odd unit of steel is never locked. One member has it all.
    expect(states(view, "alice")).toEqual(["WOOD 30 locked", "STONE 20 locked", "STEEL 12 locked"]);
    expect(states(view, "bob")).toEqual(["WOOD 30 locked", "STONE 20 locked", "STEEL 12 locked"]);
    expect(states(view, "carol")).toEqual(["WOOD 60 locked", "STONE 40 locked", "STEEL 25 locked"]);
    expect(await publishers()).toEqual({ WOOD: before.WOOD - 120n, STONE: before.STONE - 80n, STEEL: before.STEEL - 49n });

    // DSM's own read of the cell: nine Active vaults, each the publisher's, each paying its member
    // on their faction's outcome and the publisher on every other, each decided by the publisher's key alone.
    const publisher = await pub.party();
    const vaults = await onCell(view);
    expect(vaults).toHaveLength(9);
    for (const vault of vaults) {
      expect(hex(vault.ownerDeviceId)).toBe(hex(publisher.deviceId));
      expect(vault.outcomes.map((o) => new TextDecoder().decode(o.outcome)).sort()).toEqual([first, second, VOID].sort());
      for (const o of vault.outcomes) {
        expect(o.signers.map((k) => hex(k.publicKey))).toEqual([hex(publisher.signingKey)]);
      }
      const others = vault.outcomes.filter((o) => hex(o.recipientDeviceId) !== hex(publisher.deviceId));
      expect(others).toHaveLength(1);
    }

    // Asked again, the same match locks nothing more; with other factions it is refused.
    expect((await pub.matches.open("match-1", [second, first])).shares).toEqual(view.shares);
    await expect(pub.matches.open("match-1", [second, "11".repeat(32)])).rejects.toThrow(Refused);
    expect(await onCell(view)).toHaveLength(9);
    expect(await publishers()).toEqual({ WOOD: before.WOOD - 120n, STONE: before.STONE - 80n, STEEL: before.STEEL - 49n });
    await conserved("match-1 is opened");
  });

  it("pays nobody until the match is decided", async () => {
    await expect(releaseVault(player(s, "alice").wallet, vaultOf("match-1", "alice", "STEEL"))).rejects.toThrow();
    await expect(releaseVault(s.app, vaultOf("match-1", "carol", "STEEL"))).rejects.toThrow();
    for (const name of NAMES) expect(await pub.matches.waiting(key(name))).toEqual([]);
    expect(await onCell(await pub.matches.read("match-1"))).toHaveLength(9);
  });

  it("settles on one verdict: the publisher's key decides once, the winners' shares wait for them and the rest comes back", async () => {
    const before = await publishers();
    await expect(pub.matches.decide("match-1", "nobody")).rejects.toThrow(Refused);
    await expect(pub.matches.decide("match-9", first)).rejects.toThrow(Refused);

    expect((await pub.matches.decide("match-1", first)).outcome).toBe(first);
    const view = await settled("match-1");
    expect(states(view, "alice")).toEqual(["WOOD 30 waiting", "STONE 20 waiting", "STEEL 12 waiting"]);
    expect(states(view, "bob")).toEqual(["WOOD 30 waiting", "STONE 20 waiting", "STEEL 12 waiting"]);
    expect(states(view, "carol")).toEqual(["WOOD 60 returned", "STONE 40 returned", "STEEL 25 returned"]);
    expect(await publishers()).toEqual({ WOOD: before.WOOD + 60n, STONE: before.STONE + 40n, STEEL: before.STEEL + 25n });

    const waiting = await pub.matches.waiting(key("alice"));
    expect(waiting.map((w) => [w.matchId, w.cell, w.resource, w.amount])).toEqual([
      ["match-1", view.cell, "WOOD", 30],
      ["match-1", view.cell, "STONE", 20],
      ["match-1", view.cell, "STEEL", 12],
    ]);
    expect(await pub.matches.waiting(key("carol"))).toEqual([]);

    // A second decision changes nothing: the server refuses it, and DSM keeps the first verdict
    // whatever the publisher's key signs afterwards.
    await expect(pub.matches.decide("match-1", second)).rejects.toThrow(Refused);
    await expect(pub.matches.decide("match-1", VOID)).rejects.toThrow(Refused);
    const bobs = vaultOf("match-1", "bob", "WOOD");
    const route = "escrow.adjudicate";
    await s.app.ingress
      .call("invoke", route, new pb.EscrowOutcomeRequest({ vaultId: bobs, outcome: new TextEncoder().encode(second) }))
      .catch(() => undefined);
    expect(await verdict(s.app, bobs)).toEqual({ outcome: first, final: true });
    expect(states(await pub.matches.read("match-1"), "bob")).toEqual(["WOOD 30 waiting", "STONE 20 waiting", "STEEL 12 waiting"]);
    await conserved("match-1 is decided");
  });

  it("is collected by a winner in their own wallet, once; nobody else can take a winner's share", async () => {
    const alice = player(s, "alice");
    const steel = vaultOf("match-1", "alice", "STEEL");

    // Not the loser, not another winner, and not the publisher that locked it.
    await expect(releaseVault(player(s, "carol").wallet, steel)).rejects.toThrow();
    await expect(releaseVault(player(s, "bob").wallet, steel)).rejects.toThrow();
    await expect(releaseVault(s.app, steel)).rejects.toThrow();
    expect((await holds(alice.wallet, "STEEL")).available).toBe(0n);

    for (const r of RESOURCES) expect(await releaseVault(alice.wallet, vaultOf("match-1", "alice", r))).toBe(true);
    await until("alice's wallet holds what she won", 240_000, async () => {
      const got = await Promise.all(RESOURCES.map(async (r) => (await holds(alice.wallet, r)).available));
      return got.join() === "30,20,12" ? true : null;
    });
    // A share is released once.
    await expect(releaseVault(alice.wallet, steel)).rejects.toThrow();
    expect((await holds(alice.wallet, "STEEL")).available).toBe(12n);

    // The publisher's account reads each release on its own time.
    const view = await until("the publisher's account reads alice's shares collected", 240_000, async () => {
      const read = await pub.matches.read("match-1");
      return states(read, "alice").every((x) => x.endsWith("collected")) ? read : null;
    });
    expect(states(view, "alice")).toEqual(["WOOD 30 collected", "STONE 20 collected", "STEEL 12 collected"]);
    expect(states(view, "bob")).toEqual(["WOOD 30 waiting", "STONE 20 waiting", "STEEL 12 waiting"]);
    expect(await pub.matches.waiting(key("alice"))).toEqual([]);
    await conserved("alice collected");
  });

  it("gives each player's wallet what its droid mined, once, out of the publisher's own holdings", async () => {
    const [alice, carol] = [player(s, "alice"), player(s, "carol")];
    const before = await publishers();
    const [aliceWood, carolStone] = [(await holds(alice.wallet, "WOOD")).available, (await holds(carol.wallet, "STONE")).available];

    // More than the match's arena held was not mined: the report is refused whole, and nothing is given.
    await expect(pub.matches.mined("match-1", [{ signingKey: key("alice"), resource: "WOOD", units: 121 }])).rejects.toThrow(Refused);
    expect(record().matches["match-1"]?.mined).toBeUndefined();

    // The winner and the loser both mined: what a droid took is its player's whoever won.
    const mined = await pub.matches.mined("match-1", [
      { signingKey: key("alice"), resource: "WOOD", units: 70 },
      { signingKey: key("carol"), resource: "STONE", units: 9 },
    ]);
    expect(mined.map((m) => [m.resource, m.units, m.given])).toEqual([
      ["WOOD", 70, true],
      ["STONE", 9, true],
    ]);
    await until("alice's wallet holds the wood her droid mined", 240_000, async () => ((await holds(alice.wallet, "WOOD")).available === aliceWood + 70n ? true : null));
    await until("carol's wallet holds the stone her droid mined", 240_000, async () => ((await holds(carol.wallet, "STONE")).available === carolStone + 9n ? true : null));
    const after = { WOOD: before.WOOD - 70n, STONE: before.STONE - 9n, STEEL: before.STEEL };
    await until("the publisher's account holds that much less", 240_000, async () => (JSON.stringify(await publishers(), (_k, v) => String(v)) === JSON.stringify(after, (_k, v) => String(v)) ? true : null));

    // The first report stands: told again, or told otherwise, nothing more is given.
    await pub.matches.mined("match-1", [{ signingKey: key("bob"), resource: "WOOD", units: 50 }]);
    await pub.matches.tick();
    expect(await publishers()).toEqual(after);
    expect((await holds(alice.wallet, "WOOD")).available).toBe(aliceWood + 70n);
    await conserved("what was mined is given");
  });

  it("cannot be collected at the game's asking: a connected wallet collects only a two-player staked match", async () => {
    const bob = player(s, "bob");
    const steel = vaultOf("match-1", "bob", "STEEL");
    const ref = await s.app.request(bob.connection, ask.match.collect([steel]));
    // The grant covers no collecting, so the request waits for the player; bob approves it himself.
    const pending = await until("bob's wallet shows the game's request", 240_000, async () => {
      await bob.wallet.sync();
      return (await bob.wallet.pending()).find((p) => p.seq === ref.seq);
    });
    await bob.wallet.respond(pending.rawConnection, pending.seq, "approve");
    const result = await until("bob's wallet answers the game's request", 240_000, async () => {
      await bob.wallet.sync();
      const r = await s.app.result(ref);
      return r.answered && r.outcome !== "awaiting_approval" ? r : null;
    });
    expect(result.outcome).toBe("failed");
    expect(`${result.reason} ${result.detail}`).toContain("is not a match this application decides with this wallet as a player");
    expect(result.fact).toBe("none");
    expect((await holds(bob.wallet, "STEEL")).available).toBe(0n);

    // In his own wallet he collects all of it.
    for (const r of RESOURCES) expect(await releaseVault(bob.wallet, vaultOf("match-1", "bob", r))).toBe(true);
    await until("bob's wallet holds what he won", 240_000, async () => {
      const got = await Promise.all(RESOURCES.map(async (r) => (await holds(bob.wallet, r)).available));
      return got.join() === "30,20,12" ? true : null;
    });
    const all = await until("the publisher's account reads bob's shares collected", 240_000, async () => {
      const read = await pub.matches.read("match-1");
      return states(read, "bob").every((x) => x.endsWith("collected")) ? read : null;
    });
    expect(all.shares.map((x) => x.state).sort()).toEqual([
      ...Array<string>(6).fill("collected"),
      ...Array<string>(3).fill("returned"),
    ]);
    await conserved("bob collected");
  });

  it("is locked once when the server is killed while it opens a match and one is started again on its record", async () => {
    const before = await publishers();
    const lockedBefore = (await until("the account's vaults are read", 240_000, () => ownVaults(s.app))).length;
    build();
    // The server as a process of its own, on the same account and the same record.
    const server = await GameServer.start("prizes", s.dir, s.publisherHost, s.dir);
    const secret = (): string => readFileSync(join(s.dir, "match-secret"), "utf8");
    try {
      const asked = server.call("POST", "/matches/match-2", secret(), { factions: [first, second] }).catch(() => undefined);
      // The kill lands just after the server recorded its third share: the fourth is then being
      // locked, asked of the account and not recorded.
      const recordedNow = (): number => record().matches["match-2"]?.shares.filter((x) => x.vault !== "").length ?? 0;
      const deadline = Date.now() + 240_000;
      while (recordedNow() < 3) {
        if (Date.now() > deadline) throw new Error("the server did not record three shares of match-2 in 240 s");
        await new Promise((resolve) => setTimeout(resolve, 10));
      }
      await server.kill();
      await asked;
      const left = record().matches["match-2"];
      const recorded = left?.shares.filter((x) => x.vault !== "").length ?? 0;
      // What the account itself locked, once whatever was in flight at the kill has settled.
      await new Promise((resolve) => setTimeout(resolve, 5_000));
      const locked = (await until("the account's vaults are read", 240_000, () => ownVaults(s.app))).length - lockedBefore;
      console.log(`[prizes] the kill left ${recorded} of 9 shares recorded; the account had locked ${locked}`);
      expect(left?.opened).toBe(false);
      expect(recorded).toBeLessThan(9);

      await server.start();
      const opened = await server.call("POST", "/matches/match-2", secret(), { factions: [first, second] });
      expect(opened.status, JSON.stringify(opened.body)).toBe(201);
      expect(opened.body.opened).toBe(true);
      expect((opened.body.shares as { state: string }[]).map((x) => x.state)).toEqual(Array<string>(9).fill("locked"));
    } finally {
      await server.stop();
    }

    // From here the publisher's server is one started on the record the process left.
    pub = s.restarted();
    const view = await pub.matches.read("match-2");
    expect(states(view, "alice")).toEqual(["WOOD 30 locked", "STONE 20 locked", "STEEL 12 locked"]);
    expect(states(view, "carol")).toEqual(["WOOD 60 locked", "STONE 40 locked", "STEEL 25 locked"]);
    // Exactly the prize left the publisher's holdings, in exactly nine vaults: no share twice.
    expect(await onCell(view)).toHaveLength(9);
    expect((await until("the account's vaults are read", 240_000, () => ownVaults(s.app))).length).toBe(lockedBefore + 9);
    expect(await publishers()).toEqual({ WOOD: before.WOOD - 120n, STONE: before.STONE - 80n, STEEL: before.STEEL - 49n });
    // Another match is another verdict cell.
    expect(view.cell).not.toBe((await pub.matches.read("match-1")).cell);
    await conserved("match-2 is opened");
  });

  it("is only what the publisher locked: a vault someone else locks on the match's cell is no part of it", async () => {
    const carol = player(s, "carol");
    const publisher = await pub.party();
    const decidedBy = { signatureAlg: publisher.signatureAlg, key: publisher.signingKey };
    const view = await pub.matches.read("match-2");
    // Carol locks 5 of her own steel under the match's own bytes and outcomes, every outcome paying herself.
    const hers = await lockEscrow(
      carol.wallet,
      Matches.external("match-2"),
      (await s.app.object("STEEL")).raw,
      "5",
      [...[first, second].sort(), VOID].map((outcome) => ({ outcome, decidedBy, pays: carol.member })),
    );
    carols = hers.vault;
    expect(hex(hers.cell)).toBe(hex(decodeB32(view.cell)));
    expect(await onCell(view)).toHaveLength(10);

    const after = await pub.matches.read("match-2");
    expect(after.others).toBe(1);
    expect(after.shares).toEqual(view.shares);
    expect(await pub.matches.waiting(key("carol"))).toEqual([]);
    await conserved("carol locked a vault of her own on the cell");
  });

  it("goes back to the publisher whole when the match has no winner", async () => {
    const opened = { WOOD: 120n, STONE: 80n, STEEL: 49n };
    const before = await publishers();
    expect((await pub.matches.decide("match-2", VOID)).outcome).toBe(VOID);
    const view = await settled("match-2");
    expect(view.shares.map((x) => x.state)).toEqual(Array<string>(9).fill("returned"));
    expect(await publishers()).toEqual({ WOOD: before.WOOD + opened.WOOD, STONE: before.STONE + opened.STONE, STEEL: before.STEEL + opened.STEEL });
    for (const name of NAMES) expect(await pub.matches.waiting(key(name))).toEqual([]);
    // No wallet can take a share of a match nobody won.
    await expect(releaseVault(player(s, "alice").wallet, vaultOf("match-2", "alice", "STEEL"))).rejects.toThrow();
    // Carol's own vault is hers alone: the same verdict pays it back to her, and to nobody else.
    await expect(releaseVault(s.app, carols)).rejects.toThrow();
    expect(await releaseVault(player(s, "carol").wallet, carols)).toBe(true);
    await until("carol's wallet holds her steel again", 240_000, async () => ((await holds(player(s, "carol").wallet, "STEEL")).available === CAROLS ? true : null));
    await conserved("match-2 had no winner");
  });

  it("is opened and decided only by the publisher's match server, never by a player's game", async () => {
    const SECRET = hex(randomBytes(32));
    const server: Server = await new Service({ publisher: pub, world: s.world, store: new Store(s.storeFile), matchSecret: SECRET }).listen(0, "127.0.0.1");
    const base = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
    const call = async (method: string, path: string, token: string, body?: unknown) => {
      const headers: Record<string, string> = { authorization: `Bearer ${token}` };
      if (body !== undefined) headers["content-type"] = "application/json";
      const response = await fetch(base + path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
      return { status: response.status, body: (await response.json()) as Record<string, unknown> };
    };
    try {
      const before = await publishers();
      // No token, a token that is not the secret, and the secret of another launch.
      for (const token of ["", "a-players-token", hex(randomBytes(32))]) {
        expect((await call("POST", "/matches/match-3", token, { factions: [first, second] })).status).toBe(401);
        expect((await call("POST", "/matches/match-1/outcome", token, { outcome: second })).status).toBe(401);
        expect((await call("GET", "/matches/match-1", token)).status).toBe(401);
        expect((await call("POST", "/matches/match-1/mined", token, { mined: [{ signingKey: key("bob"), resource: "WOOD", units: 50 }] })).status).toBe(401);
      }
      expect(await publishers()).toEqual(before);
      expect(Object.keys(record().matches).sort()).toEqual(["match-1", "match-2"]);

      const read = await call("GET", "/matches/match-1", SECRET);
      expect(read.status).toBe(200);
      expect(read.body.outcome).toBe(first);
      expect((await call("POST", "/matches/match-1/outcome", SECRET, { outcome: second })).status).toBe(409);
    } finally {
      server.close();
    }
  });
});
