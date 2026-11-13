/**
 * Joining, removal by majority, a second faction and a trade between factions, on DSM with no game
 * attached (spec 7.2 and Stage 3).
 *
 * A join is two people's signed changes: a member's invitation and the invited wallet's own
 * acceptance. A removal is the voters' own signed changes. Every one of them is written by that
 * person's own wallet at the faction's successor cell; the publisher's account reads what holds it.
 *
 * Run with `npm run test:dsm` (see hosts.ts for what it needs).
 */
import { randomBytes } from "node:crypto";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { hex } from "../../src/faction/canonical.js";
import type { Change } from "../../src/faction/change.js";
import { NOTICE_MS, Refused, type Structure } from "../../src/publisher/publisher.js";
import { until } from "./hosts.js";
import { player, stage, type Player, type Stage } from "./stage.js";
import { becomeContacts, holds, send } from "./wallets.js";

const STEEL = "STEEL";
const SUPPLY = 1_000_000n;
const tower = hex(randomBytes(16));
const lookout = hex(randomBytes(16));

let s: Stage;
/** The first faction, on North Bluff, and the second, on East Shore. */
let first: string;
let second: string;

const key = (name: string): string => player(s, name).member.signingKey;
const members = async (factionId: string): Promise<string[]> => (await s.publisher.follow(factionId)).mirror.state.members.map((m) => m.signingKey);

async function change(factionId: string, who: string, what: Change): Promise<void> {
  const done = await s.publisher.change(factionId, player(s, who).connection, what);
  expect(done.taken, `${who}'s ${what.kind}: ${done.taken ? "" : done.why}`).toBe(true);
}

async function give(to: Player, amount: bigint): Promise<void> {
  const before = (await holds(to.wallet, STEEL)).available;
  await until(`the publisher gives ${to.name} ${amount} steel`, 240_000, async () => {
    await s.app.give(to.connection, STEEL, amount.toString());
    return true;
  });
  await until(`${to.name}'s wallet takes in ${amount} steel`, 240_000, async () => ((await holds(to.wallet, STEEL)).available === before + amount ? true : null));
}

async function commit(factionId: string, who: string, instanceId: string, amount: string): Promise<void> {
  const result = await s.publisher.contribute(factionId, player(s, who).connection, instanceId, STEEL, amount);
  expect(result.fact, `${who}'s commitment: ${result.reason || result.detail}`).toBe("contributed");
}

async function structure(factionId: string, instanceId: string): Promise<Structure> {
  const found = (await s.publisher.base(factionId)).structures.find((x) => x.instanceId === instanceId);
  if (found === undefined) throw new Error("the structure is not placed");
  return found;
}

const steel = (x: Structure) => x.funding.resources.find((r) => r.resource === STEEL);

async function everyUnit(): Promise<bigint> {
  let total = (await s.app.owned()).find((o) => o.object.symbol === STEEL)?.amount ?? 0n;
  for (const p of Object.values(s.players)) {
    const held = await holds(p.wallet, STEEL);
    total += held.available + held.locked;
  }
  return total;
}

beforeAll(async () => {
  s = await stage("membership", ["alice", "bob", "carol", "dave"]);
});

afterAll(async () => {
  await s?.stop();
});

describe("joining and removal", () => {
  it("a member invites and the invited wallet accepts: only then is it a member, on every mirror", async () => {
    first = (await s.publisher.assign(s.publisher.founding("north-bluff", [player(s, "alice").member, player(s, "bob").member], hex(randomBytes(16))))).factionId;
    const carol = player(s, "carol");

    // Not invited: the server refuses her acceptance, and her wallet is not one of the cell's writers.
    await expect(s.publisher.propose(first, carol.connection, { kind: "accept" })).rejects.toThrow(Refused);

    await change(first, "alice", { kind: "invite", member: carol.member });
    expect(await members(first)).toEqual([key("alice"), key("bob")]);
    expect((await s.publisher.follow(first)).mirror.state.invited).toEqual([carol.member]);

    // Invited is not a member: she can build nothing yet.
    const place: Change = { kind: "place", instanceId: tower, prefabId: "watchtower", x: -8160 + 480, y: -8160 + 480, rotation: 0, config: {} };
    await expect(s.publisher.propose(first, carol.connection, place)).rejects.toThrow(Refused);

    await change(first, "carol", { kind: "accept" });
    expect(await members(first)).toEqual([key("alice"), key("bob"), key("carol")]);
    await change(first, "carol", place);

    // A mirror started from the assignment in storage walks the same cells to the same members.
    const other = await s.publisher.mirrorAt("north-bluff");
    await other.walk(s.publisher.cells);
    const live = (await s.publisher.follow(first)).mirror;
    expect(other.commitment).toBe(live.commitment);
    expect(other.state).toEqual(live.state);
    expect(live.lineage.map((e) => [e.change?.kind, e.signerKey])).toEqual([
      ["invite", key("alice")],
      ["accept", key("carol")],
      ["place", key("carol")],
    ]);
  });

  it("more than half of the others vote a member out: she is out at once and her materials stop counting at once", async () => {
    await give(player(s, "alice"), 150n);
    await give(player(s, "bob"), 100n);
    await give(player(s, "carol"), 100n);
    await commit(first, "bob", tower, "100");
    await commit(first, "carol", tower, "100");
    expect(steel(await structure(first, tower))).toMatchObject({ funded: 200, shortfall: 100 });

    // The others of Carol are two: one vote is not more than half.
    await change(first, "alice", { kind: "vote", signingKey: key("carol") });
    expect(await members(first)).toContain(key("carol"));
    await change(first, "bob", { kind: "vote", signingKey: key("carol") });
    expect(await members(first)).toEqual([key("alice"), key("bob")]);

    // No notice period: the shortfall is real at once, and her vault is listed as no member's.
    const after = await structure(first, tower);
    expect(steel(after)).toMatchObject({ funded: 100, shortfall: 200 });
    expect(after.funding.contributions.find((c) => c.owner === key("carol"))).toMatchObject({
      amount: 100,
      counts: false,
      leaving: false,
      why: "its owner is not a member of the faction",
    });
    expect(await s.publisher.tick()).toEqual([]);

    // Nothing holds her materials: she takes them back by her own outcome whenever she likes.
    const back = await s.publisher.withdraw(first, player(s, "carol").connection, tower);
    expect(back?.fact, back?.reason || back?.detail).toBe("taken_back");
    expect(await holds(player(s, "carol").wallet, STEEL)).toEqual({ available: 100n, locked: 0n });
    // She is no member: she can change nothing of the base.
    await expect(s.publisher.propose(first, player(s, "carol").connection, { kind: "dismantle", instanceId: tower })).rejects.toThrow(Refused);
  });
});

describe("a second faction and trade", () => {
  it("a second faction has its own parcel and its own lineage", async () => {
    second = (await s.publisher.assign(s.publisher.founding("east-shore", [player(s, "dave").member], hex(randomBytes(16))))).factionId;
    expect(second).not.toBe(first);
    const place: Change = { kind: "place", instanceId: lookout, prefabId: "lookout", x: -8160 + 480, y: 3360 + 480, rotation: 0, config: {} };
    const before = (await s.publisher.follow(first)).mirror.commitment;
    await change(second, "dave", place);

    // Nothing orders one faction's changes against the other's: the first lineage has not moved.
    expect((await s.publisher.follow(first)).mirror.commitment).toBe(before);
    expect((await s.publisher.follow(second)).mirror.height).toBe(1);
    // A member of the first faction is no member of the second.
    await expect(s.publisher.propose(second, player(s, "alice").connection, { kind: "dismantle", instanceId: lookout })).rejects.toThrow(Refused);
  });

  it("a player in one faction sends materials to a player in the other; they count only once the holding is proven", async () => {
    const [alice, dave] = [player(s, "alice"), player(s, "dave")];
    expect(await s.publisher.materials(dave.connection)).toMatchObject({ STEEL: 0 });

    // The trade is between two wallets: two contacts, an ordinary transfer, no faction and no host involved.
    await becomeContacts({ wallet: alice.wallet, is: alice.member, name: "alice" }, { wallet: dave.wallet, is: dave.member, name: "dave" });
    const lineages = [(await s.publisher.follow(first)).mirror.commitment, (await s.publisher.follow(second)).mirror.commitment];
    await until("alice sends dave 50 steel", 240_000, async () => {
      const sent = await send(alice.wallet, dave.member, STEEL, "50");
      if (!sent.sent) throw new Error(sent.message);
      return true;
    });
    await until("dave's wallet takes in 50 steel", 240_000, async () => ((await holds(dave.wallet, STEEL)).available === 50n ? true : null));

    // Spendable ownership moved exactly once: 50 left one wallet, 50 arrived in the other, and no unit was made or lost.
    expect(await holds(alice.wallet, STEEL)).toEqual({ available: 100n, locked: 0n });
    expect(await everyUnit()).toBe(SUPPLY);
    expect([(await s.publisher.follow(first)).mirror.commitment, (await s.publisher.follow(second)).mirror.commitment]).toEqual(lineages);

    // The game treats them as usable only from the wallet's proof of what it holds.
    expect(await s.publisher.materials(dave.connection)).toMatchObject({ STEEL: 50 });
    await commit(second, "dave", lookout, "50");
    expect(steel(await structure(second, lookout))).toMatchObject({ needed: 150, funded: 50, shortfall: 100 });
  });

  it("B leaves the first faction and joins the second: his materials follow after the 48 hours", async () => {
    const bob = player(s, "bob");
    await change(first, "bob", { kind: "leave" });
    expect(await members(first)).toEqual([key("alice")]);

    // The first base shows what B's leaving will cost it: his vault still counts, marked as leaving.
    const leaving = await structure(first, tower);
    expect(leaving.funding.contributions.find((c) => c.owner === key("bob"))).toMatchObject({ amount: 100, counts: true, leaving: true });

    // He joins the second faction the way anyone joins: a member invites, he accepts.
    await change(second, "dave", { kind: "invite", member: bob.member });
    await change(second, "bob", { kind: "accept" });
    expect(await members(second)).toEqual([key("dave"), key("bob")]);
    expect(await members(first)).not.toContain(key("bob"));

    // The 48 hours run: the publisher returns his vault, he takes the steel back and commits it to the second base.
    s.clock.advance(NOTICE_MS + 60_000);
    expect(await s.publisher.tick()).toHaveLength(1);
    const back = await s.publisher.withdraw(first, bob.connection, tower);
    expect(back?.fact, back?.reason || back?.detail).toBe("taken_back");
    expect(steel(await structure(first, tower))).toMatchObject({ funded: 0, shortfall: 300 });
    await commit(second, "bob", lookout, "100");
    const funded = await structure(second, lookout);
    expect(funded.funding.status).toBe("fully funded");
    expect(funded.standing).toBe(true);
    expect(await everyUnit()).toBe(SUPPLY);
  });
});
