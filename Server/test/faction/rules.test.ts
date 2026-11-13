// The validity rules of spec section 7, with no game and no account attached.

import { describe, expect, it } from "vitest";
import { encodeChange, type Change } from "../../src/faction/change.js";
import { Mirror } from "../../src/faction/mirror.js";
import { contains, inside } from "../../src/faction/parcel.js";
import { firstCommitment, firstState } from "../../src/faction/state.js";
import { judge } from "../../src/faction/validity.js";
import { catalogue, founding, instance, member, parcel, step } from "./world.js";

const [a, b, c] = [member(1), member(2), member(3)];
const outsider = member(9);
const land = parcel("north-bluff");
const assignment = "ab".repeat(32);

function fresh(): Mirror {
  return Mirror.at(founding([a, b, c]), assignment, catalogue, land);
}

function payload(mirror: Mirror, change: Change): Uint8Array {
  return encodeChange({ factionId: mirror.state.factionId, parent: mirror.commitment, change });
}

function verdict(mirror: Mirror, signerKey: string, change: Change) {
  return judge(mirror.state, mirror.commitment, catalogue, land, signerKey, payload(mirror, change));
}

/** The parcel's first corner: every position below is given from it. */
const [OX, OY] = land.boundary[0] as [number, number];

const tower = (n: number, x: number, y: number): Change => ({
  kind: "place",
  instanceId: instance(n),
  prefabId: "watchtower",
  x: OX + x,
  y: OY + y,
  rotation: 0,
  config: {},
});

describe("the parcel boundary", () => {
  it("counts its own edge as inside and the notch as outside", () => {
    const at = (x: number, y: number): [number, number] => [OX + x, OY + y];
    const rect = (minX: number, minY: number, maxX: number, maxY: number) => ({ minX: OX + minX, minY: OY + minY, maxX: OX + maxX, maxY: OY + maxY });
    expect(inside(land.boundary, at(0, 0))).toBe(true);
    expect(inside(land.boundary, at(2400, 2400))).toBe(true);
    expect(inside(land.boundary, at(3600, 3600))).toBe(false);
    expect(contains(land.boundary, rect(0, 0, 960, 960))).toBe(true);
    // Three corners of this rectangle are inside; it covers the notch's corner and reaches into the notch.
    expect(contains(land.boundary, rect(1920, 1920, 2880, 2880))).toBe(false);
    // Every corner of this one is on the boundary, and it lies across the notch.
    expect(contains(land.boundary, rect(2400, 0, 4800, 4800))).toBe(false);
    expect(contains(land.boundary, rect(-480, 0, 480, 960))).toBe(false);
  });
});

describe("placing a prefab", () => {
  it("is valid inside the parcel, on the grid, on free ground, with no funding at all", () => {
    const v = verdict(fresh(), a.signingKey, tower(1, 480, 480));
    expect(v.valid).toBe(true);
    if (v.valid) expect(v.next.placed.map((p) => p.prefabId)).toEqual(["watchtower"]);
  });

  it("is invalid for a prefab the catalogue does not list", () => {
    const v = verdict(fresh(), a.signingKey, { ...tower(1, 480, 480), prefabId: "castle" } as Change);
    expect(v).toEqual({ valid: false, why: "prefab castle is not in the catalogue" });
  });

  it("is invalid outside the parcel boundary, and in the notch the boundary cuts out", () => {
    expect(verdict(fresh(), a.signingKey, tower(1, 0, 480))).toMatchObject({ valid: false, why: "the placement is not inside the parcel boundary" });
    expect(verdict(fresh(), a.signingKey, tower(1, 3840, 3840))).toMatchObject({ valid: false, why: "the placement is not inside the parcel boundary" });
  });

  it("is invalid off the catalogue's grid", () => {
    expect(verdict(fresh(), a.signingKey, tower(1, 500, 480))).toMatchObject({ valid: false, why: "the placement is not on the catalogue's grid" });
  });

  it("is invalid over what is already placed, and valid beside it", () => {
    const mirror = fresh();
    mirror.accept({ signerKey: a.signingKey, payload: payload(mirror, tower(1, 480, 480)), step: step(1) });
    expect(verdict(mirror, b.signingKey, tower(2, 960, 960))).toMatchObject({ valid: false });
    expect(verdict(mirror, b.signingKey, tower(2, 1440, 480)).valid).toBe(true);
  });

  it("is invalid with an instance_id already used", () => {
    const mirror = fresh();
    mirror.accept({ signerKey: a.signingKey, payload: payload(mirror, tower(1, 480, 480)), step: step(1) });
    expect(verdict(mirror, b.signingKey, tower(1, 1440, 480))).toMatchObject({ valid: false, why: `instance ${instance(1)} is already used` });
  });

  it("takes only the configuration the catalogue allows", () => {
    const wall = (config: Record<string, string>): Change => ({
      kind: "place",
      instanceId: instance(5),
      prefabId: "wall",
      x: OX + 240,
      y: OY + 240,
      rotation: 0,
      config,
    });
    expect(verdict(fresh(), a.signingKey, wall({ gate: "open" })).valid).toBe(true);
    expect(verdict(fresh(), a.signingKey, wall({ gate: "ajar" })).valid).toBe(false);
    expect(verdict(fresh(), a.signingKey, wall({ moat: "yes" })).valid).toBe(false);
    // An option named like something every object carries is still no option of the catalogue's.
    expect(verdict(fresh(), a.signingKey, wall({ constructor: "x" }))).toMatchObject({ valid: false, why: "the catalogue gives wall no option constructor" });
  });

  it("turns the footprint with the prefab", () => {
    const workshop = (rotation: 0 | 90): Change => ({
      kind: "place",
      instanceId: instance(6),
      prefabId: "workshop",
      x: OX + (rotation === 0 ? 720 : 480),
      y: OY + (rotation === 0 ? 480 : 720),
      rotation,
      config: {},
    });
    expect(verdict(fresh(), a.signingKey, workshop(0)).valid).toBe(true);
    expect(verdict(fresh(), a.signingKey, workshop(90)).valid).toBe(true);
  });
});

describe("replacing, dismantling and confirming", () => {
  function withTower(): Mirror {
    const mirror = fresh();
    mirror.accept({ signerKey: a.signingKey, payload: payload(mirror, tower(1, 480, 480)), step: step(1) });
    return mirror;
  }

  it("replaces a prefab in place: the instance_id stays, the prefab_id changes", () => {
    const v = verdict(withTower(), c.signingKey, { kind: "replace", instanceId: instance(1), prefabId: "lookout", config: {} });
    expect(v.valid).toBe(true);
    if (v.valid) expect(v.next.placed).toMatchObject([{ instanceId: instance(1), prefabId: "lookout", x: OX + 480, y: OY + 480 }]);
  });

  it("refuses a replacement that does not fit where the instance stands", () => {
    const v = verdict(withTower(), c.signingKey, { kind: "replace", instanceId: instance(1), prefabId: "workshop", config: {} });
    expect(v.valid).toBe(false);
  });

  it("lets any member dismantle any structure, and only one that exists", () => {
    const mirror = withTower();
    const v = verdict(mirror, b.signingKey, { kind: "dismantle", instanceId: instance(1) });
    expect(v.valid).toBe(true);
    if (v.valid) expect(v.next.placed).toEqual([]);
    expect(verdict(mirror, b.signingKey, { kind: "dismantle", instanceId: instance(2) }).valid).toBe(false);
    expect(verdict(mirror, b.signingKey, { kind: "replace", instanceId: instance(2), prefabId: "lookout", config: {} }).valid).toBe(false);
  });

  it("confirms a plan that exists and changes nothing placed", () => {
    const mirror = withTower();
    const v = verdict(mirror, b.signingKey, { kind: "confirm", instanceId: instance(1) });
    expect(v.valid).toBe(true);
    if (v.valid) expect(v.next.placed).toEqual(mirror.state.placed);
    expect(verdict(mirror, b.signingKey, { kind: "confirm", instanceId: instance(7) }).valid).toBe(false);
  });
});

describe("leaving", () => {
  it("removes the signer from the members, and so from the next cell's writers", () => {
    const mirror = fresh();
    mirror.accept({ signerKey: b.signingKey, payload: payload(mirror, { kind: "leave" }), step: step(1) });
    expect(mirror.state.members.map((m) => m.signingKey)).toEqual([a.signingKey, c.signingKey]);
    expect(mirror.successorCell().writers).toHaveLength(2);
    expect(verdict(mirror, b.signingKey, tower(1, 480, 480))).toMatchObject({ valid: false, why: "the signer is not a member the state lists" });
  });
});

describe("what a change must name", () => {
  it("is invalid when signed by a key the state does not list", () => {
    expect(verdict(fresh(), outsider.signingKey, tower(1, 480, 480))).toMatchObject({ valid: false, why: "the signer is not a member the state lists" });
  });

  it("is invalid when it names another faction or another state", () => {
    const mirror = fresh();
    const other = encodeChange({ factionId: "cd".repeat(32), parent: mirror.commitment, change: tower(1, 480, 480) });
    expect(judge(mirror.state, mirror.commitment, catalogue, land, a.signingKey, other)).toMatchObject({ valid: false, why: "the change names another faction" });
    const stale = encodeChange({ factionId: mirror.state.factionId, parent: "ef".repeat(32), change: tower(1, 480, 480) });
    expect(judge(mirror.state, mirror.commitment, catalogue, land, a.signingKey, stale)).toMatchObject({ valid: false, why: "the change names another state" });
  });

  it("is invalid when the payload is not the canonical bytes of a change", () => {
    const mirror = fresh();
    const junk = new TextEncoder().encode("not a change");
    expect(judge(mirror.state, mirror.commitment, catalogue, land, a.signingKey, junk).valid).toBe(false);
    const spaced = new TextEncoder().encode(` ${new TextDecoder().decode(payload(mirror, tower(1, 480, 480)))}`);
    expect(judge(mirror.state, mirror.commitment, catalogue, land, a.signingKey, spaced)).toMatchObject({
      valid: false,
      why: "the payload is not the canonical bytes of its change",
    });
  });
});

describe("the lineage", () => {
  it("starts at the commitment over the first state's fields", () => {
    const f = founding([a, b, c]);
    expect(fresh().commitment).toBe(firstCommitment(firstState(f, assignment)));
    expect(Mirror.at(founding([a, b]), assignment, catalogue, land).commitment).not.toBe(fresh().commitment);
  });

  it("advances past an invalid occupant with the base unchanged", () => {
    const mirror = fresh();
    const before = mirror.commitment;
    const entry = mirror.accept({ signerKey: a.signingKey, payload: payload(mirror, tower(1, 0, 480)), step: step(1) });
    expect(entry).toMatchObject({ valid: false, parent: before });
    expect(mirror.state.placed).toEqual([]);
    expect(mirror.commitment).not.toBe(before);
    expect(mirror.height).toBe(1);
    // The next successor cell is not the one just spent.
    expect(Buffer.from(mirror.successorCell().name).toString("hex")).toBe(mirror.state.factionId + mirror.commitment);
  });

  it("gives two mirrors that took the same occupants the same state and commitment", () => {
    const [one, two] = [fresh(), fresh()];
    const occupants = [
      { signerKey: a.signingKey, change: tower(1, 480, 480), step: step(1) },
      { signerKey: b.signingKey, change: tower(2, 960, 960), step: step(2) },
      { signerKey: c.signingKey, change: tower(3, 1440, 480), step: step(3) },
      { signerKey: b.signingKey, change: { kind: "dismantle", instanceId: instance(1) } as Change, step: step(4) },
    ];
    for (const mirror of [one, two]) {
      for (const o of occupants) mirror.accept({ signerKey: o.signerKey, payload: payload(mirror, o.change), step: o.step });
    }
    expect(two.commitment).toBe(one.commitment);
    expect(two.state).toEqual(one.state);
    expect(one.lineage.map((e) => e.valid)).toEqual([true, false, true, true]);
  });

  it("never returns to an earlier commitment when the base returns to an earlier shape", () => {
    const mirror = fresh();
    const first = mirror.commitment;
    mirror.accept({ signerKey: a.signingKey, payload: payload(mirror, tower(1, 480, 480)), step: step(1) });
    mirror.accept({ signerKey: a.signingKey, payload: payload(mirror, { kind: "dismantle", instanceId: instance(1) }), step: step(2) });
    expect(mirror.state.placed).toEqual([]);
    expect(mirror.commitment).not.toBe(first);
  });

  it("commits to the occupant: another step at the same cell is another lineage", () => {
    const [one, two] = [fresh(), fresh()];
    one.accept({ signerKey: a.signingKey, payload: payload(one, tower(1, 480, 480)), step: step(1) });
    two.accept({ signerKey: a.signingKey, payload: payload(two, tower(1, 480, 480)), step: step(2) });
    expect(two.commitment).not.toBe(one.commitment);
  });
});
