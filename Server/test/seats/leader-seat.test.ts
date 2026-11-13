/**
 * The leader seat taken offline (spec Stage 4; sections 6.7 and 11).
 *
 * Every successor cell has one leader among the five storage seats, and no other seat stands in for
 * it. This suite takes real seats of the set offline, one process at a time, and brings them back:
 * the base stays visible, building pauses with a plain message, and it resumes when the seat returns.
 *
 * Which seat leads a cell is DSM's to derive; the game never computes it. The suite finds it the way
 * an observer can: the one seat without which the publisher's account cannot read the cell.
 *
 * Run with `npm run test:seats` on a developer's own set (see test/real-dsm/hosts.ts and seats.ts for what it needs).
 */
import { randomBytes } from "node:crypto";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import type { RequestRef } from "dsm-app-client";
import { hex } from "../../src/faction/canonical.js";
import { encodeChange, type Change } from "../../src/faction/change.js";
import { leaderUnread } from "../../src/dsm/cells.js";
import { buildingAt, PAUSED } from "../../src/publisher/publisher.js";
import { until } from "../real-dsm/hosts.js";
import { bringBack, leaderOf, seats, takeOffline, type Seat } from "../real-dsm/seats.js";
import { player, stage, type Stage } from "../real-dsm/stage.js";
import { holds } from "../real-dsm/wallets.js";

const PARCEL = "north-bluff";
const STEEL = "STEEL";
const [OX, OY] = [-8160, -8160];
const OPEN = "no recognized write holds the cell";

const place = (prefabId: string, x: number, y: number): Change => ({
  kind: "place",
  instanceId: hex(randomBytes(16)),
  prefabId,
  x: OX + x,
  y: OY + y,
  rotation: 0,
  config: {},
});

const all: Seat[] = seats();
let s: Stage;
let factionId: string;
const tower = place("watchtower", 480, 480);

/** Ask the wallet of `who`, which is not running, to write `change` at the current successor cell. */
async function probeFrom(who: string, change: Change): Promise<RequestRef> {
  const { mirror } = await s.publisher.follow(factionId);
  const { name, writers } = mirror.successorCell();
  const payload = encodeChange({ factionId, parent: mirror.commitment, change });
  return s.publisher.cells.ask(player(s, who).connection, name, writers, payload);
}

beforeAll(async () => {
  s = await stage("leader-seat", ["alice", "bob"]);
  const [alice, bob] = [player(s, "alice"), player(s, "bob")];
  factionId = (await s.publisher.assign(s.publisher.founding(PARCEL, [alice.member, bob.member], hex(randomBytes(16))))).factionId;
  expect((await s.publisher.change(factionId, alice.connection, tower)).taken).toBe(true);
  // The tower is funded in full, so there is a standing structure to keep in view.
  await until("the publisher gives alice 300 steel", 240_000, async () => {
    await s.app.give(alice.connection, STEEL, "300");
    return true;
  });
  await until("alice's wallet takes in 300 steel", 240_000, async () => ((await holds(alice.wallet, STEEL)).available === 300n ? true : null));
  const committed = await s.publisher.contribute(factionId, alice.connection, tower.kind === "place" ? tower.instanceId : "", STEEL, "300");
  expect(committed.fact, committed.reason || committed.detail).toBe("contributed");
  expect((await s.publisher.base(factionId)).structures).toMatchObject([{ prefabId: "watchtower", standing: true }]);
});

afterAll(async () => {
  // Whatever happened, the set is left with every seat serving.
  for (const seat of all) await bringBack(seat);
  await s?.stop();
});

describe("the leader seat taken offline", () => {
  let leader: Seat;
  let probe: RequestRef;
  const lookout = place("lookout", 2400, 480);
  const wall = place("wall", 1680, 240);

  it("one seat leads the successor cell: without it the cell cannot be read, and no other seat stands in", async () => {
    // Bob's wallet is not running: what the game asks of it waits, and nothing is written meanwhile.
    await player(s, "bob").host.stop();
    probe = await probeFrom("bob", lookout);
    const found = await leaderOf(s.app, probe, all);
    leader = found.leader;
    // Exactly one seat: with any of the other four offline the account read the cell as open.
    expect(Object.values(found.readings).filter((reading) => reading.startsWith(OPEN))).toHaveLength(4);
    expect(leaderUnread(found.readings[leader.name] as string)).toBe(true);
    console.log(`the successor cell's leader is ${leader.name}`, found.readings);
    // With every seat serving again the cell reads as open.
    expect((await s.app.result(probe)).detail.startsWith(OPEN)).toBe(true);
  });

  it("with the leader offline the base stays visible and building pauses, with a plain message", async () => {
    const before = await s.publisher.base(factionId);
    await takeOffline(leader);

    // Visible: the same committed state, the same standing tower.
    const during = await s.publisher.base(factionId);
    expect(during.commitment).toBe(before.commitment);
    expect(during.height).toBe(before.height);
    expect(during.structures.map((x) => [x.instanceId, x.standing])).toEqual(before.structures.map((x) => [x.instanceId, x.standing]));
    // What a member's game is told before anyone tries: building is paused, in plain words.
    expect(buildingAt((await s.publisher.follow(factionId)).end)).toEqual({ open: false, message: PAUSED });

    // Paused: a member's change is signed and carried, the leader does not take it, and nothing holds the cell.
    const paused = await s.publisher.change(factionId, player(s, "alice").connection, wall);
    console.log("alice's change with the leader offline:", paused);
    expect(paused).toMatchObject({ taken: false, paused: true, why: PAUSED, commitment: before.commitment });

    // The same for a member whose wallet comes back meanwhile: its waiting write is not taken either.
    await player(s, "bob").host.start();
    const bobs = await until("bob's wallet answers the waiting write", 240_000, async () => {
      const r = await s.app.result(probe);
      return r.answered ? r : null;
    });
    console.log("bob's waiting write with the leader offline:", { outcome: bobs.outcome, reason: bobs.reason, fact: bobs.fact, detail: bobs.detail });
    expect(bobs.fact).toBe("none");
    expect(bobs.outcome).toBe("failed");
    const still = await s.publisher.base(factionId);
    expect(still.commitment).toBe(before.commitment);
    expect(still.structures).toHaveLength(1);
  });

  it("building resumes when the seat returns, on the same lineage", async () => {
    const before = await s.publisher.base(factionId);
    await bringBack(leader);
    expect(buildingAt((await s.publisher.follow(factionId)).end)).toEqual({ open: true, message: "" });

    const resumed = await s.publisher.change(factionId, player(s, "alice").connection, wall);
    expect(resumed, JSON.stringify(resumed)).toMatchObject({ taken: true });
    const after = await s.publisher.base(factionId);
    expect(after.height).toBe(before.height + 1);
    expect(after.structures.map((x) => x.prefabId).sort()).toEqual(["wall", "watchtower"]);
    // The writes the leader never took count for nothing: the lookout Bob's wallet was asked for is not there.
    expect(after.structures.map((x) => x.instanceId)).not.toContain(lookout.kind === "place" ? lookout.instanceId : "");

    // A mirror started from the assignment in storage walks to the same state.
    const other = await s.publisher.mirrorAt(PARCEL);
    await other.walk(s.publisher.cells);
    const live = (await s.publisher.follow(factionId)).mirror;
    expect(other.commitment).toBe(live.commitment);
    expect(other.state).toEqual(live.state);
  });

  it("with two other seats offline building goes on: finality needs the leader and two more seats, not all five", async () => {
    await player(s, "bob").host.stop();
    const next = await probeFrom("bob", place("lookout", 3360, 480));
    const found = await leaderOf(s.app, next, all);
    console.log(`the next successor cell's leader is ${found.leader.name}`, found.readings);
    const others = all.filter((seat) => seat.name !== found.leader.name).slice(0, 2);
    for (const seat of others) await takeOffline(seat);

    const placed = await s.publisher.change(factionId, player(s, "alice").connection, place("wall", 1680, 1680));
    console.log(`alice's change with ${others.map((o) => o.name).join(" and ")} offline:`, placed);
    expect(placed, JSON.stringify(placed)).toMatchObject({ taken: true });
    for (const seat of others) await bringBack(seat);
    expect((await s.publisher.base(factionId)).structures).toHaveLength(3);
  });
});
