/**
 * No sequence of contribute, withdraw, leave, replace and dismantle creates or destroys a unit
 * (spec 12.4, third item).
 *
 * The contributions suite walks one chosen sequence. This one walks a sequence nobody chose: a
 * seeded draw of those five acts, by three members over two structures, on real accounts. After
 * every act it reads every wallet and the publisher's account and counts every unit of steel there
 * is, spendable and committed, and compares each wallet with what the acts so far should have left
 * it. Each of the five acts is owed once, in a drawn order, so no sequence leaves one out; the rest
 * are drawn freely. The seed is printed; `SENNITE_SEED` runs another sequence, or the same one again.
 *
 * Run with `npm run test:dsm` (see hosts.ts for what it needs).
 */
import { randomBytes } from "node:crypto";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { hex } from "../../src/faction/canonical.js";
import type { Change } from "../../src/faction/change.js";
import { NOTICE_MS } from "../../src/publisher/publisher.js";
import { until } from "./hosts.js";
import { player, stage, type Stage } from "./stage.js";
import { holds } from "./wallets.js";

const PARCEL = "north-bluff";
const STEEL = "STEEL";
const SUPPLY = 1_000_000n;
const EACH = 400n;
const ACTS = 24;
const NAMES = ["alice", "bob", "carol"];
const [OX, OY] = [-8160, -8160];
/** Where the two structures stand: a dismantled one is planned again on the same ground. */
const GROUND: [number, number][] = [
  [480, 480],
  [2400, 480],
];

const seed = Number(process.env.SENNITE_SEED ?? 20261113);

/** A small seeded generator (mulberry32): the same seed draws the same sequence. */
function generator(from: number): () => number {
  let a = from >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const draw = generator(seed);
const pick = <T>(from: T[]): T => from[Math.floor(draw() * from.length)] as T;

let s: Stage;
let factionId: string;
/** What the acts so far should have left: each wallet's spendable steel, and what it has committed to which structure. */
const spendable: Record<string, bigint> = {};
const committed: Record<string, Record<string, bigint>> = {};
/** The two structures, by the ground they stand on. */
const built: { instanceId: string; prefabId: string }[] = [];
const members = new Set(NAMES);
const done: string[] = [];
/** The acts still owed once each, in a drawn order. */
const owed = ["contribute", "withdraw", "replace", "dismantle", "leave"]
  .map((kind) => [draw(), kind] as [number, string])
  .sort((a, b) => a[0] - b[0])
  .map(([, kind]) => kind);

const locked = (who: string): bigint => Object.values(committed[who] ?? {}).reduce((sum, n) => sum + n, 0n);

async function change(who: string, what: Change): Promise<void> {
  const result = await s.publisher.change(factionId, player(s, who).connection, what);
  expect(result.taken, `${who}'s ${what.kind}: ${result.taken ? "" : result.why}`).toBe(true);
}

async function plan(ground: number, prefabId: string, who: string): Promise<void> {
  const instanceId = hex(randomBytes(16));
  const [x, y] = GROUND[ground] as [number, number];
  await change(who, { kind: "place", instanceId, prefabId, x: OX + x, y: OY + y, rotation: 0, config: {} });
  built[ground] = { instanceId, prefabId };
}

async function takeBackAll(who: string, instanceId: string): Promise<void> {
  const held = committed[who]?.[instanceId] ?? 0n;
  if (held === 0n) return;
  const back = await s.publisher.withdraw(factionId, player(s, who).connection, instanceId);
  expect(back?.fact, `${who} takes back from ${instanceId}: ${back?.reason || back?.detail}`).toBe("taken_back");
  spendable[who] = (spendable[who] as bigint) + held;
  delete (committed[who] as Record<string, bigint>)[instanceId];
}

/** Count every unit there is, and compare each wallet with what the acts so far should have left it. */
async function counted(after: string): Promise<void> {
  let total = (await s.app.owned()).find((o) => o.object.symbol === STEEL)?.amount ?? 0n;
  for (const who of NAMES) {
    const held = await holds(player(s, who).wallet, STEEL);
    expect(held, `${who}'s wallet after ${after}`).toEqual({ available: spendable[who], locked: locked(who) });
    total += held.available + held.locked;
  }
  expect(total, `every unit of steel after ${after}`).toBe(SUPPLY);
}

/** One act, drawn among those that can be done now. */
async function act(): Promise<string> {
  const inside = NAMES.filter((n) => members.has(n));
  const kinds: string[] = [];
  const canCommit = inside.filter((n) => (spendable[n] as bigint) >= 50n);
  const holding = NAMES.flatMap((n) => Object.keys(committed[n] ?? {}).map((instanceId) => [n, instanceId] as [string, string])).filter(([n]) => members.has(n));
  if (canCommit.length > 0) kinds.push("contribute", "contribute", "contribute", "contribute");
  if (holding.length > 0) kinds.push("withdraw", "withdraw", "withdraw");
  kinds.push("replace", "replace", "dismantle");
  if (inside.length > 1) kinds.push("leave");

  // An owed act that can be done now is done first; otherwise one is drawn.
  const due = owed.findIndex((kind) => kinds.includes(kind));
  const kind = due >= 0 ? (owed.splice(due, 1)[0] as string) : pick(kinds);
  switch (kind) {
    case "contribute": {
      const who = pick(canCommit);
      const to = pick(built);
      const amount = pick([50n, 100n, 150n].filter((n) => n <= (spendable[who] as bigint)));
      const result = await s.publisher.contribute(factionId, player(s, who).connection, to.instanceId, STEEL, amount.toString());
      expect(result.fact, `${who} commits ${amount}: ${result.reason || result.detail}`).toBe("contributed");
      spendable[who] = (spendable[who] as bigint) - amount;
      (committed[who] ??= {})[to.instanceId] = (committed[who]?.[to.instanceId] ?? 0n) + amount;
      return `${who} commits ${amount} to the ${to.prefabId}`;
    }
    case "withdraw": {
      const [who, instanceId] = pick(holding);
      await takeBackAll(who, instanceId);
      return `${who} withdraws from a structure`;
    }
    case "replace": {
      const ground = Math.floor(draw() * built.length);
      const was = built[ground] as { instanceId: string; prefabId: string };
      const prefabId = was.prefabId === "watchtower" ? "lookout" : "watchtower";
      const who = pick(inside);
      await change(who, { kind: "replace", instanceId: was.instanceId, prefabId, config: {} });
      built[ground] = { instanceId: was.instanceId, prefabId };
      return `${who} replaces the ${was.prefabId} with a ${prefabId}`;
    }
    case "dismantle": {
      const ground = Math.floor(draw() * built.length);
      const was = built[ground] as { instanceId: string; prefabId: string };
      const who = pick(inside);
      await change(who, { kind: "dismantle", instanceId: was.instanceId });
      // Nothing is released by the dismantling itself: the units are where they were until each owner takes them.
      await counted(`${who} dismantles the ${was.prefabId}, before anyone releases`);
      for (const n of NAMES) if (members.has(n)) await takeBackAll(n, was.instanceId);
      await plan(ground, was.prefabId, who);
      return `${who} dismantles the ${was.prefabId}, every contributor releases, and it is planned again`;
    }
    default: {
      // A member with materials committed, when there is one: their leaving is the one that moves units.
      const holders = inside.filter((n) => locked(n) > 0n);
      const who = pick(holders.length > 0 ? holders : inside);
      const leftWith = locked(who);
      await change(who, { kind: "leave" });
      members.delete(who);
      // The 48 hours are running: the leaver's units are committed still, and none has moved.
      await counted(`${who} leaves, during the notice`);
      s.clock.advance(NOTICE_MS + 60_000);
      await s.publisher.tick();
      for (const instanceId of Object.keys(committed[who] ?? {})) await takeBackAll(who, instanceId);
      await counted(`${who} left and took everything back`);
      // Back in, the way anyone joins, so the next acts have three members to draw from.
      const inviter = pick(NAMES.filter((n) => members.has(n)));
      await change(inviter, { kind: "invite", member: player(s, who).member });
      await change(who, { kind: "accept" });
      members.add(who);
      return `${who} leaves with ${leftWith} committed, takes everything back after the 48 hours, and joins again`;
    }
  }
}

beforeAll(async () => {
  s = await stage("conservation", NAMES);
  factionId = (await s.publisher.assign(s.publisher.founding(PARCEL, NAMES.map((n) => player(s, n).member), hex(randomBytes(16))))).factionId;
  for (const who of NAMES) {
    const p = player(s, who);
    await until(`the publisher gives ${who} ${EACH} steel`, 240_000, async () => {
      await s.app.give(p.connection, STEEL, EACH.toString());
      return true;
    });
    await until(`${who}'s wallet takes in ${EACH} steel`, 240_000, async () => ((await holds(p.wallet, STEEL)).available === EACH ? true : null));
    spendable[who] = EACH;
    committed[who] = {};
  }
  await plan(0, "watchtower", "alice");
  await plan(1, "lookout", "bob");
});

afterAll(async () => {
  console.log(`seed ${seed}:\n${done.map((d, i) => `${String(i + 1).padStart(2)}. ${d}`).join("\n")}`);
  await s?.stop();
});

describe("supply is conserved", () => {
  it(`over ${ACTS} drawn acts (seed ${seed}) no unit is created or destroyed, and every wallet holds what the acts left it`, async () => {
    await counted("the materials are handed out");
    for (let i = 0; i < ACTS; i += 1) {
      const what = await act();
      done.push(what);
      await counted(`act ${i + 1}: ${what}`);
    }
    // All five kinds were done: a sequence without one of them shows nothing about it.
    expect(owed).toEqual([]);
  });

  it("when everyone has taken everything back, each wallet holds what it was given", async () => {
    for (const who of NAMES) for (const instanceId of Object.keys(committed[who] ?? {})) await takeBackAll(who, instanceId);
    for (const who of NAMES) expect(await holds(player(s, who).wallet, STEEL)).toEqual({ available: EACH, locked: 0n });
    await counted("everything is taken back");
  });
});
