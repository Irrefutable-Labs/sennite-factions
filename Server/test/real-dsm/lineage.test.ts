/**
 * The faction lineage on DSM with no game attached (spec section 13, build step 2; Stage 1).
 *
 * The publisher's account and four players' wallets are real `dsm-app-host` accounts on the storage
 * set. Each change is signed by a member's own wallet and written at the successor cell of the
 * faction's state; what holds a cell is read from the publisher's account, never taken from a
 * wallet's word.
 *
 * Run with `npm run test:dsm` (see hosts.ts for what it needs).
 */
import { randomBytes } from "node:crypto";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { hex } from "../../src/faction/canonical.js";
import { encodeChange, type Change } from "../../src/faction/change.js";
import type { FactionState } from "../../src/faction/state.js";
import { Refused } from "../../src/publisher/publisher.js";
import { player, stage, type Stage } from "./stage.js";

const PARCEL = "north-bluff";
const instance = (): string => hex(randomBytes(16));

/** North Bluff's first corner: every position below is given from it. */
const [OX, OY] = [-8160, -8160];

const tower = (instanceId: string, x: number, y: number): Change => ({
  kind: "place",
  instanceId,
  prefabId: "watchtower",
  x: OX + x,
  y: OY + y,
  rotation: 0,
  config: {},
});

let s: Stage;
let first: FactionState;
let factionId: string;

beforeAll(async () => {
  s = await stage("lineage", ["alice", "bob", "carol", "dave"]);
});

afterAll(async () => {
  await s?.stop();
});

describe("one faction, one mirrored base", () => {
  it("is created from the publisher's signed parcel assignment, once per parcel", async () => {
    const members = ["alice", "bob", "carol"].map((n) => player(s, n).member);
    const founding = s.publisher.founding(PARCEL, members, hex(randomBytes(16)));
    expect(await s.publisher.assignment(PARCEL)).toBeUndefined();
    first = await s.publisher.assign(founding);
    factionId = first.factionId;

    const held = await s.publisher.assignment(PARCEL);
    expect(held).toMatchObject({ parcelId: PARCEL, factionId, address: first.assignment });
    expect(first.members).toEqual(members);
    expect(first.storageSet).toHaveLength(5);

    // The parcel is taken: a second faction cannot be assigned it, and copying the founding gives it no second parcel.
    const again = s.publisher.founding(PARCEL, [player(s, "dave").member], hex(randomBytes(16)));
    await expect(s.publisher.assign(again)).rejects.toThrow(/already assigned/);
    expect((await s.publisher.assignment(PARCEL))?.factionId).toBe(factionId);
  });

  it("A places a prefab: the change is final at the successor cell and two mirrors arrive at the same R_1", async () => {
    const a = instance();
    const before = (await s.publisher.follow(factionId)).mirror.commitment;
    const placed = await s.publisher.change(factionId, player(s, "alice").connection, tower(a, 480, 480));
    expect(placed.taken).toBe(true);

    const { mirror } = await s.publisher.follow(factionId);
    expect(mirror.height).toBe(1);
    expect(mirror.commitment).not.toBe(before);
    expect(mirror.state.placed).toMatchObject([{ instanceId: a, prefabId: "watchtower", x: OX + 480, y: OY + 480 }]);
    expect(mirror.lineage[0]).toMatchObject({ valid: true, signerKey: player(s, "alice").member.signingKey, parent: before });

    // A second mirror, started from the assignment in storage, reads the same cell and holds the same state.
    const other = await s.publisher.mirrorAt(PARCEL);
    await other.walk(s.publisher.cells);
    expect(other.commitment).toBe(mirror.commitment);
    expect(other.state).toEqual(mirror.state);
  });

  it("A and B place in the same spot at once: exactly one structure results", async () => {
    const [ia, ib] = [instance(), instance()];
    const [alice, bob] = [player(s, "alice"), player(s, "bob")];
    const [ra, rb] = await Promise.all([
      s.publisher.propose(factionId, alice.connection, tower(ia, 1440, 480)),
      s.publisher.propose(factionId, bob.connection, tower(ib, 1440, 480)),
    ]);
    const [pa, pb] = [await s.publisher.settle(factionId, ra), await s.publisher.settle(factionId, rb)];
    expect([pa.taken, pb.taken].filter(Boolean)).toHaveLength(1);

    const { mirror } = await s.publisher.follow(factionId);
    expect(mirror.height).toBe(2);
    const there = mirror.state.placed.filter((p) => p.x === OX + 1440 && p.y === OY + 480);
    expect(there).toHaveLength(1);
    expect(there[0]?.instanceId).toBe(pa.taken ? ia : ib);

    // The one whose change counted for nothing signs again against the new state: the spot is taken.
    const loser = pa.taken ? bob : alice;
    await expect(s.publisher.propose(factionId, loser.connection, tower(instance(), 1440, 480))).rejects.toThrow(Refused);
    expect((await s.publisher.follow(factionId)).mirror.height).toBe(2);
  });

  it("a non-member's write counts for nothing", async () => {
    const dave = player(s, "dave");
    const { mirror } = await s.publisher.follow(factionId);
    const { name, writers } = mirror.successorCell();
    const payload = encodeChange({ factionId, parent: mirror.commitment, change: tower(instance(), 2400, 480) });

    // The server refuses the proposal, and the publisher's account refuses to ask a wallet that is not one of the cell's writers.
    await expect(s.publisher.propose(factionId, dave.connection, tower(instance(), 2400, 480))).rejects.toThrow(Refused);
    await expect(s.publisher.cells.ask(dave.connection, name, writers, payload)).rejects.toThrow(/not one of the cell's writers/);

    // Naming itself a writer puts its write in another cell: the same name under another writer set.
    const withDave = [...writers, Buffer.from(dave.member.signingKey, "hex")];
    const ref = await s.publisher.cells.ask(dave.connection, name, withDave, payload);
    const written = await s.app.wait(ref);
    expect(written.fact).toBe("written");

    // The successor cell is untouched: the mirror has not moved, and a member's change still takes it.
    const after = await s.publisher.follow(factionId);
    expect(after.mirror.commitment).toBe(mirror.commitment);
    expect(after.mirror.state.placed).toHaveLength(2);
    const c = instance();
    const placed = await s.publisher.settle(factionId, await s.publisher.propose(factionId, player(s, "carol").connection, tower(c, 2400, 480)));
    expect(placed.taken).toBe(true);
    const member = await s.app.result(s.publisher.cells.refs(name, writers)[0]!);
    expect(hex(member.state!.cell)).not.toBe(hex(written.state!.cell));
    expect((await s.publisher.follow(factionId)).mirror.state.placed.map((p) => p.instanceId)).toContain(c);
  });

  it("a member's change that breaks a rule holds its cell and changes nothing; the lineage advances", async () => {
    const { mirror } = await s.publisher.follow(factionId);
    const before = { commitment: mirror.commitment, height: mirror.height, placed: [...mirror.state.placed] };
    const { name, writers } = mirror.successorCell();
    // Outside the parcel boundary. The server would refuse to propose it; this is a member's wallet signing it anyway.
    const invalid = encodeChange({ factionId, parent: mirror.commitment, change: tower(instance(), -480, 480) });
    const ref = await s.publisher.cells.ask(player(s, "bob").connection, name, writers, invalid);
    expect((await s.app.wait(ref)).fact).toBe("written");

    const after = (await s.publisher.follow(factionId)).mirror;
    expect(after.height).toBe(before.height + 1);
    expect(after.commitment).not.toBe(before.commitment);
    expect(after.state.placed).toEqual(before.placed);
    expect(after.lineage.at(-1)).toMatchObject({
      valid: false,
      why: "the placement is not inside the parcel boundary",
      signerKey: player(s, "bob").member.signingKey,
    });

    // The next change goes to the successor cell of the new state, and counts.
    const next = await s.publisher.change(factionId, player(s, "alice").connection, tower(instance(), 3360, 480));
    expect(next.taken).toBe(true);
  });

  it("a member who missed several changes walks the cells and arrives at the same state", async () => {
    const live = (await s.publisher.follow(factionId)).mirror;
    expect(live.height).toBeGreaterThanOrEqual(5);

    // Carol's mirror, as she left it after the first change.
    const carols = await s.publisher.mirrorAt(PARCEL);
    const firstCell = carols.successorCell();
    const reading = await s.publisher.cells.read(firstCell.name, firstCell.writers);
    if (reading.cell !== "held") throw new Error("the first cell is not held");
    carols.accept(reading);
    expect(carols.height).toBe(1);
    expect(carols.commitment).not.toBe(live.commitment);

    expect(await carols.walk(s.publisher.cells)).toEqual({ at: "unread" });
    expect(carols.commitment).toBe(live.commitment);
    expect(carols.state).toEqual(live.state);
    expect(carols.lineage).toEqual(live.lineage);
  });

  it("a server started again resumes from storage and its record, on the same lineage", async () => {
    const live = (await s.publisher.follow(factionId)).mirror;
    const again = s.restarted();
    const resumed = (await again.follow(factionId)).mirror;
    expect(resumed.commitment).toBe(live.commitment);
    expect(resumed.state).toEqual(live.state);
  });

  it("a member alone can build: no change waits on an absent teammate", async () => {
    await Promise.all(["bob", "carol", "dave"].map((n) => player(s, n).host.stop()));
    const solo = instance();
    const placed = await s.publisher.change(factionId, player(s, "alice").connection, tower(solo, 480, 1440));
    expect(placed.taken).toBe(true);
    expect((await s.publisher.follow(factionId)).mirror.state.placed.map((p) => p.instanceId)).toContain(solo);
  });
});
