/**
 * Contributions on DSM with no game attached (spec section 13, build step 3; Stage 2).
 *
 * The publisher's account issues two resources with a fixed supply and gives some to the members'
 * wallets. A member commits materials to a structure in their own escrow vault, whose two outcomes
 * both pay them. Funding is never recorded: it is read, each time, from the vaults the publisher's
 * account finds on the structure's cells.
 *
 * Every balance here is read from the wallet's own account, the number its own screen shows.
 *
 * Run with `npm run test:dsm` (see hosts.ts for what it needs).
 */
import { randomBytes } from "node:crypto";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { hex, unhex } from "../../src/faction/canonical.js";
import type { Change } from "../../src/faction/change.js";
import { NOTICE_MS, Publisher, type Structure } from "../../src/publisher/publisher.js";
import { until } from "./hosts.js";
import { player, stage, type Player, type Stage } from "./stage.js";
import { addContact, decideAndRelease, holds, lockEscrow, send } from "./wallets.js";

const PARCEL = "east-shore";
const STEEL = "STEEL";
const WOOD = "WOOD";
const SUPPLY = 1_000_000n;
const tower = hex(randomBytes(16));

let s: Stage;
let factionId: string;
/** Each player's vaults on the tower, in the order they were committed. */
const vaults: Record<string, Uint8Array[]> = { alice: [], bob: [], carol: [], dave: [] };

async function give(to: Player, symbol: string, amount: bigint): Promise<void> {
  const before = (await holds(to.wallet, symbol)).available;
  await until(`the publisher gives ${to.name} ${amount} ${symbol}`, 240_000, async () => {
    await s.app.give(to.connection, symbol, amount.toString());
    return true;
  });
  await until(`${to.name}'s wallet takes in ${amount} ${symbol}`, 240_000, async () => {
    return (await holds(to.wallet, symbol)).available === before + amount ? true : null;
  });
}

/** Every unit of `symbol` there is: what the publisher and every wallet can spend, and what each holds in vaults. */
async function everyUnit(symbol: string): Promise<bigint> {
  const publisher = (await s.app.owned()).find((o) => o.object.symbol === symbol);
  let total = publisher?.amount ?? 0n;
  for (const p of Object.values(s.players)) {
    const held = await holds(p.wallet, symbol);
    total += held.available + held.locked;
  }
  return total;
}

async function theTower(): Promise<Structure> {
  const base = await s.publisher.base(factionId);
  const found = base.structures.find((x) => x.instanceId === tower);
  if (found === undefined) throw new Error("the tower is not placed");
  return found;
}

const steel = (x: Structure) => x.funding.resources.find((r) => r.resource === STEEL);

async function commit(who: string, amount: string): Promise<void> {
  const p = player(s, who);
  const result = await s.publisher.contribute(factionId, p.connection, tower, STEEL, amount);
  expect(result.fact, `${who}'s commitment: ${result.reason || result.detail}`).toBe("contributed");
  (vaults[who] as Uint8Array[]).push(...(result.contribution?.contributions ?? []));
}

async function takeBack(who: string, which: Uint8Array[]): Promise<void> {
  const result = await s.publisher.takeBack(player(s, who).connection, which);
  expect(result.fact, `${who}'s withdrawal: ${result.reason || result.detail}`).toBe("taken_back");
  vaults[who] = (vaults[who] as Uint8Array[]).filter((v) => !which.some((w) => hex(w) === hex(v)));
}

beforeAll(async () => {
  s = await stage("contributions", ["alice", "bob", "carol", "dave"]);
});

afterAll(async () => {
  await s?.stop();
});

describe("contributions", () => {
  it("two resources with capped supply are distributed to the members' wallets", async () => {
    for (const symbol of [STEEL, WOOD]) {
      const issued = (await s.app.owned()).find((o) => o.object.symbol === symbol);
      expect(issued?.object.supply).toBe(SUPPLY.toString());
      expect(issued?.amount).toBe(SUPPLY);
    }
    await give(player(s, "alice"), STEEL, 200n);
    await give(player(s, "bob"), STEEL, 100n);
    await give(player(s, "carol"), STEEL, 250n);
    await give(player(s, "dave"), STEEL, 100n);
    await give(player(s, "alice"), WOOD, 50n);
    expect(await everyUnit(STEEL)).toBe(SUPPLY);
    expect(await everyUnit(WOOD)).toBe(SUPPLY);
  });

  it("A places a watchtower with nothing committed: it needs resources and is only a plan", async () => {
    const members = ["alice", "bob", "carol"].map((n) => player(s, n).member);
    factionId = (await s.publisher.assign(s.publisher.founding(PARCEL, members, hex(randomBytes(16))))).factionId;
    const place: Change = { kind: "place", instanceId: tower, prefabId: "watchtower", x: -8160 + 480, y: 3360 + 480, rotation: 0, config: {} };
    expect((await s.publisher.change(factionId, player(s, "alice").connection, place)).taken).toBe(true);

    const planned = await theTower();
    expect(planned.standing).toBe(false);
    expect(planned.funding.status).toBe("needs resources");
    expect(steel(planned)).toEqual({ resource: STEEL, needed: 300, funded: 0, shortfall: 300 });
  });

  it("A commits 200 steel and B commits 100: it is fully funded and stands in play, and who committed what is plain", async () => {
    await commit("alice", "200");
    const partly = await theTower();
    expect(partly.standing).toBe(false);
    expect(steel(partly)).toMatchObject({ funded: 200, shortfall: 100 });

    await commit("bob", "100");
    const built = await theTower();
    expect(built.funding.status).toBe("fully funded");
    expect(built.standing).toBe(true);
    expect(built.funding.contributions.map((c) => [c.owner, c.amount, c.counts]).sort()).toEqual(
      [
        [player(s, "alice").member.signingKey, 200, true],
        [player(s, "bob").member.signingKey, 100, true],
      ].sort(),
    );

    // The steel is still each contributor's own, and it is in the vault: not in what they can spend.
    expect(await holds(player(s, "alice").wallet, STEEL)).toEqual({ available: 0n, locked: 200n });
    expect(await holds(player(s, "bob").wallet, STEEL)).toEqual({ available: 0n, locked: 100n });
    expect(await everyUnit(STEEL)).toBe(SUPPLY);
  });

  it("A tries to send the committed 200 steel to C: the transfer fails, because the units are in the vault", async () => {
    const [alice, carol] = [player(s, "alice"), player(s, "carol")];
    await addContact(alice.wallet, "carol", carol.member);
    await addContact(carol.wallet, "alice", alice.member);
    const before = await holds(carol.wallet, STEEL);

    const attempt = await send(alice.wallet, carol.member, STEEL, "200");
    expect(attempt.sent, attempt.message).toBe(false);

    expect(await holds(alice.wallet, STEEL)).toEqual({ available: 0n, locked: 200n });
    expect(await holds(carol.wallet, STEEL)).toEqual(before);
    expect((await theTower()).funding.status).toBe("fully funded");
  });

  it("A withdraws: the tower stands and needs 200 steel; C commits 200 and it is fully funded again", async () => {
    await takeBack("alice", vaults.alice as Uint8Array[]);
    expect(await holds(player(s, "alice").wallet, STEEL)).toEqual({ available: 200n, locked: 0n });

    const short = await theTower();
    expect(short.standing).toBe(true);
    expect(short.funding.status).toBe("needs resources");
    expect(steel(short)).toEqual({ resource: STEEL, needed: 300, funded: 100, shortfall: 200 });

    await commit("carol", "200");
    const restored = await theTower();
    expect(restored.standing).toBe(true);
    expect(restored.funding.status).toBe("fully funded");
    expect(await everyUnit(STEEL)).toBe(SUPPLY);
  });

  it("the faction replaces the tower with a cheaper prefab; the surplus is withdrawn", async () => {
    const replace: Change = { kind: "replace", instanceId: tower, prefabId: "lookout", config: {} };
    expect((await s.publisher.change(factionId, player(s, "bob").connection, replace)).taken).toBe(true);

    // The existing commitments keep counting against the smaller recipe; what is beyond it counts for nothing.
    const cheaper = await theTower();
    expect(cheaper.prefabId).toBe("lookout");
    expect(cheaper.standing).toBe(true);
    expect(steel(cheaper)).toEqual({ resource: STEEL, needed: 150, funded: 150, shortfall: 0 });

    // C takes the 200 back and commits the 50 the smaller recipe still needs.
    await takeBack("carol", vaults.carol as Uint8Array[]);
    expect(steel(await theTower())).toMatchObject({ funded: 100, shortfall: 50 });
    await commit("carol", "50");
    const funded = await theTower();
    expect(funded.funding.status).toBe("fully funded");
    expect(funded.standing).toBe(true);
    expect(await holds(player(s, "carol").wallet, STEEL)).toEqual({ available: 200n, locked: 50n });
    expect(await everyUnit(STEEL)).toBe(SUPPLY);
  });

  it("only members fund a structure: a vault a non-member commits counts for nothing", async () => {
    const before = steel(await theTower());
    await commit("dave", "100");
    const after = await theTower();
    expect(steel(after)).toEqual(before);
    const daves = after.funding.contributions.find((c) => c.owner === player(s, "dave").member.signingKey);
    expect(daves).toMatchObject({ amount: 100, counts: false, why: "its owner is not a member of the faction" });
  });

  it("a vault on the right cell with the wrong terms, and a claimed contribution with no vault, count for nothing", async () => {
    const [alice, carol] = [player(s, "alice"), player(s, "carol")];
    const publisher = await s.publisher.party();
    const before = await theTower();

    // In her own wallet, outside the game, A locks 100 steel on her own cell for the tower: the same
    // two outcomes decided by the same two keys, but the publisher's outcome pays C.
    const steelId = (await s.app.object(STEEL)).raw;
    const forged = await lockEscrow(alice.wallet, Publisher.commitment(factionId, tower), steelId, "100", [
      { outcome: "return", decidedBy: { signatureAlg: publisher.signatureAlg, key: publisher.signingKey }, pays: carol.member },
      {
        outcome: "withdraw",
        decidedBy: { signatureAlg: publisher.signatureAlg, key: unhex(alice.member.signingKey, "key") },
        pays: alice.member,
      },
    ]);
    // Her cell for the tower is the one the server recorded when she first committed: the cell
    // follows from what is committed to and the two keys, so her new vault is found there.
    const read = await theTower();
    const found = read.funding.contributions.find((c) => c.vault === hex(forged.vault));
    expect(found).toMatchObject({ amount: 100, counts: false, why: "one of its outcomes pays someone other than its owner" });
    expect(steel(read)).toEqual(steel(before));

    // A claim of a contribution, pointing at A's cell and naming a vault that does not exist.
    s.publisher.contributions.record(factionId, tower, alice.member.signingKey, forged.cell, [new Uint8Array(randomBytes(32))]);
    expect(steel(await theTower())).toEqual(steel(before));

    // A takes her 100 back by her own outcome.
    await decideAndRelease(alice.wallet, forged.vault, "withdraw");
    expect(await holds(alice.wallet, STEEL)).toEqual({ available: 200n, locked: 0n });
    expect(await everyUnit(STEEL)).toBe(SUPPLY);
  });

  it("B leaves: his materials stay committed and counted for 48 hours, then the publisher returns them to him", async () => {
    const bob = player(s, "bob");
    expect((await s.publisher.change(factionId, bob.connection, { kind: "leave" })).taken).toBe(true);
    const state = (await s.publisher.follow(factionId)).mirror.state;
    expect(state.members.map((m) => m.signingKey)).not.toContain(bob.member.signingKey);

    // The notice is running: B's vault still counts, marked as leaving, and nothing is returned yet.
    const during = await theTower();
    expect(during.funding.status).toBe("fully funded");
    expect(during.funding.contributions.find((c) => c.owner === bob.member.signingKey)).toMatchObject({ amount: 100, counts: true, leaving: true });
    s.clock.advance(NOTICE_MS - 60_000);
    expect(await s.publisher.tick()).toEqual([]);

    // The 48 hours have run: the publisher's key decides when B gets them back, and B's wallet takes them.
    s.clock.advance(120_000);
    const returned = await s.publisher.tick();
    expect(returned).toEqual((vaults.bob as Uint8Array[]).map((v) => hex(v)));
    await takeBack("bob", vaults.bob as Uint8Array[]);
    expect(await holds(bob.wallet, STEEL)).toEqual({ available: 100n, locked: 0n });

    const after = await theTower();
    expect(after.standing).toBe(true);
    expect(after.funding.status).toBe("needs resources");
    expect(steel(after)).toEqual({ resource: STEEL, needed: 150, funded: 50, shortfall: 100 });
    expect(await s.publisher.tick()).toEqual([]);
  });

  it("the faction dismantles the structure; every contributor releases in full and supply is what it was", async () => {
    expect((await s.publisher.change(factionId, player(s, "carol").connection, { kind: "dismantle", instanceId: tower })).taken).toBe(true);
    expect((await s.publisher.base(factionId)).structures).toEqual([]);

    await takeBack("carol", vaults.carol as Uint8Array[]);
    await takeBack("dave", vaults.dave as Uint8Array[]);

    expect(await holds(player(s, "alice").wallet, STEEL)).toEqual({ available: 200n, locked: 0n });
    expect(await holds(player(s, "bob").wallet, STEEL)).toEqual({ available: 100n, locked: 0n });
    expect(await holds(player(s, "carol").wallet, STEEL)).toEqual({ available: 250n, locked: 0n });
    expect(await holds(player(s, "dave").wallet, STEEL)).toEqual({ available: 100n, locked: 0n });
    expect(await everyUnit(STEEL)).toBe(SUPPLY);
    expect(await everyUnit(WOOD)).toBe(SUPPLY);
  });
});
