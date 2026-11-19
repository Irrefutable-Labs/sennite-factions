// Joining and removal (spec 7.2), with no game and no account attached. Each person's part is
// their own signed change: a member invites and the invited wallet accepts; each vote is the voter's.

import { describe, expect, it } from "vitest";
import { encodeChange, type Change } from "../../src/faction/change.js";
import { Mirror } from "../../src/faction/mirror.js";
import { judge } from "../../src/faction/validity.js";
import { catalogue, founding, instance, member, parcel, step } from "./world.js";

const [a, b, c, d, e] = [member(1), member(2), member(3), member(4), member(5)];
const land = parcel("north-bluff");
const assignment = "ab".repeat(32);
let steps = 0;

const fresh = (...members: (typeof a)[]): Mirror => Mirror.at(founding(members), assignment, catalogue, land);

function payload(mirror: Mirror, change: Change): Uint8Array {
  return encodeChange({ factionId: mirror.state.factionId, parent: mirror.commitment, change });
}

const verdict = (mirror: Mirror, signerKey: string, change: Change) =>
  judge(mirror.state, mirror.commitment, catalogue, land, signerKey, payload(mirror, change));

/** The change holds the successor cell: the mirror takes it, valid or not. */
function take(mirror: Mirror, signerKey: string, change: Change) {
  return mirror.accept({ signerKey, payload: payload(mirror, change), step: step(++steps % 250) });
}

const keys = (mirror: Mirror): string[] => mirror.state.members.map((m) => m.signingKey);
const writers = (mirror: Mirror): string[] => mirror.successorCell().writers.map((w) => Buffer.from(w).toString("hex"));

describe("joining", () => {
  it("takes a member's invitation and then the invited wallet's own acceptance", () => {
    const mirror = fresh(a, b);
    expect(take(mirror, a.signingKey, { kind: "invite", member: d })).toMatchObject({ valid: true });
    // Invited is not a member: listed apart, and able to write the successor cell only to accept.
    expect(keys(mirror)).toEqual([a.signingKey, b.signingKey]);
    expect(mirror.state.invited).toEqual([d]);
    expect(writers(mirror)).toEqual([a.signingKey, b.signingKey, d.signingKey]);

    expect(take(mirror, d.signingKey, { kind: "accept" })).toMatchObject({ valid: true });
    expect(keys(mirror)).toEqual([a.signingKey, b.signingKey, d.signingKey]);
    expect(mirror.state.invited).toEqual([]);
  });

  it("is never one person's act: nobody is a member without their own acceptance, or without an invitation", () => {
    const mirror = fresh(a, b);
    // No invitation: a stranger's acceptance is nothing, and a member cannot accept for them.
    expect(verdict(mirror, d.signingKey, { kind: "accept" })).toEqual({ valid: false, why: "the signer is not invited" });
    take(mirror, a.signingKey, { kind: "invite", member: d });
    expect(verdict(mirror, a.signingKey, { kind: "accept" })).toEqual({ valid: false, why: "the signer is not invited" });
    expect(keys(mirror)).not.toContain(d.signingKey);
  });

  it("lets the invited do nothing but accept", () => {
    const mirror = fresh(a, b);
    take(mirror, a.signingKey, { kind: "invite", member: d });
    const place: Change = { kind: "place", instanceId: instance(1), prefabId: "wall", x: land.boundary[0]![0] + 240, y: land.boundary[0]![1] + 240, rotation: 0, config: {} };
    for (const change of [place, { kind: "invite", member: e }, { kind: "vote", signingKey: a.signingKey }, { kind: "leave" }] as Change[]) {
      expect(verdict(mirror, d.signingKey, change)).toMatchObject({ valid: false, why: "the signer is not a member the state lists" });
    }
    // If such a write holds the cell anyway, the lineage advances, nothing of the base changes, and
    // the invitation is used up: an invited wallet has one write, and cannot spend cell after cell.
    const before = mirror.state;
    expect(take(mirror, d.signingKey, place)).toMatchObject({ valid: false });
    expect(mirror.state).toEqual({ ...before, invited: [] });
    expect(writers(mirror)).toEqual([a.signingKey, b.signingKey]);
    expect(verdict(mirror, d.signingKey, { kind: "accept" })).toEqual({ valid: false, why: "the signer is not invited" });
  });

  it("refuses an invitation of a key that is no device's signing key", () => {
    const mirror = fresh(a, b);
    // Such a key in the state would leave the faction with a successor cell nobody can write.
    for (const signingKey of ["", "07", "07".repeat(63), "07".repeat(65)]) {
      expect(() => encodeChange({ factionId: mirror.state.factionId, parent: mirror.commitment, change: { kind: "invite", member: { ...d, signingKey } } })).not.toThrow();
      expect(verdict(mirror, a.signingKey, { kind: "invite", member: { ...d, signingKey } })).toMatchObject({ valid: false });
    }
    expect(mirror.state.invited).toEqual([]);
  });

  it("refuses an invitation of a member or of someone already invited, and one from a non-member", () => {
    const mirror = fresh(a, b);
    expect(verdict(mirror, a.signingKey, { kind: "invite", member: b })).toEqual({ valid: false, why: "the invited is already a member" });
    expect(verdict(mirror, c.signingKey, { kind: "invite", member: d })).toEqual({ valid: false, why: "the signer is not a member the state lists" });
    take(mirror, a.signingKey, { kind: "invite", member: d });
    expect(verdict(mirror, b.signingKey, { kind: "invite", member: d })).toEqual({ valid: false, why: "the invited is already invited" });
  });

  it("lets any member withdraw an invitation: the acceptance then counts for nothing", () => {
    const mirror = fresh(a, b);
    take(mirror, a.signingKey, { kind: "invite", member: d });
    expect(take(mirror, b.signingKey, { kind: "uninvite", signingKey: d.signingKey })).toMatchObject({ valid: true });
    expect(mirror.state.invited).toEqual([]);
    expect(writers(mirror)).toEqual([a.signingKey, b.signingKey]);
    expect(verdict(mirror, d.signingKey, { kind: "accept" })).toMatchObject({ valid: false });
    expect(verdict(mirror, a.signingKey, { kind: "uninvite", signingKey: d.signingKey })).toEqual({ valid: false, why: "nobody with that key is invited" });
  });

  it("takes a new invitation for someone who left: an old one is used up", () => {
    const mirror = fresh(a, b);
    take(mirror, a.signingKey, { kind: "invite", member: d });
    take(mirror, d.signingKey, { kind: "accept" });
    take(mirror, d.signingKey, { kind: "leave" });
    expect(keys(mirror)).toEqual([a.signingKey, b.signingKey]);
    expect(verdict(mirror, d.signingKey, { kind: "accept" })).toEqual({ valid: false, why: "the signer is not invited" });
  });
});

describe("removal by majority", () => {
  it("removes a member at once when more than half of the others have voted", () => {
    const mirror = fresh(a, b, c, d);
    // The others are three: one vote is not more than half, two are.
    expect(take(mirror, a.signingKey, { kind: "vote", signingKey: d.signingKey })).toMatchObject({ valid: true });
    expect(keys(mirror)).toContain(d.signingKey);
    expect(mirror.state.votes).toEqual([{ target: d.signingKey, voters: [a.signingKey] }]);
    expect(take(mirror, b.signingKey, { kind: "vote", signingKey: d.signingKey })).toMatchObject({ valid: true });
    expect(keys(mirror)).toEqual([a.signingKey, b.signingKey, c.signingKey]);
    expect(mirror.state.votes).toEqual([]);
    // Out at once: the removed member's key no longer writes the successor cell.
    expect(writers(mirror)).not.toContain(d.signingKey);
    expect(verdict(mirror, d.signingKey, { kind: "leave" })).toMatchObject({ valid: false });
  });

  it("needs both of the others in a faction of three, and the other alone in a faction of two", () => {
    const three = fresh(a, b, c);
    take(three, a.signingKey, { kind: "vote", signingKey: c.signingKey });
    expect(keys(three)).toContain(c.signingKey);
    take(three, b.signingKey, { kind: "vote", signingKey: c.signingKey });
    expect(keys(three)).toEqual([a.signingKey, b.signingKey]);

    const two = fresh(a, b);
    take(two, a.signingKey, { kind: "vote", signingKey: b.signingKey });
    expect(keys(two)).toEqual([a.signingKey]);
  });

  it("counts each member's vote once, never the member's own on themselves, and never a non-member's", () => {
    const mirror = fresh(a, b, c, d);
    take(mirror, a.signingKey, { kind: "vote", signingKey: d.signingKey });
    expect(verdict(mirror, a.signingKey, { kind: "vote", signingKey: d.signingKey })).toEqual({
      valid: false,
      why: "the signer has already voted to remove that member",
    });
    expect(verdict(mirror, d.signingKey, { kind: "vote", signingKey: d.signingKey })).toMatchObject({ valid: false });
    expect(verdict(mirror, e.signingKey, { kind: "vote", signingKey: d.signingKey })).toMatchObject({ valid: false });
    expect(verdict(mirror, b.signingKey, { kind: "vote", signingKey: e.signingKey })).toEqual({ valid: false, why: "nobody with that key is a member" });
    expect(keys(mirror)).toContain(d.signingKey);
  });

  it("lets a member take a vote back before it is used", () => {
    const mirror = fresh(a, b, c, d);
    take(mirror, a.signingKey, { kind: "vote", signingKey: d.signingKey });
    expect(take(mirror, a.signingKey, { kind: "unvote", signingKey: d.signingKey })).toMatchObject({ valid: true });
    expect(mirror.state.votes).toEqual([]);
    take(mirror, b.signingKey, { kind: "vote", signingKey: d.signingKey });
    expect(keys(mirror)).toContain(d.signingKey);
    expect(verdict(mirror, c.signingKey, { kind: "unvote", signingKey: d.signingKey })).toMatchObject({ valid: false });
  });

  it("drops the votes of a member who goes, and counts again when the faction shrinks", () => {
    const mirror = fresh(a, b, c, d, e);
    // On d: a and b have voted. The others are four, so two is not more than half.
    take(mirror, a.signingKey, { kind: "vote", signingKey: d.signingKey });
    take(mirror, b.signingKey, { kind: "vote", signingKey: d.signingKey });
    expect(keys(mirror)).toContain(d.signingKey);
    // e leaves: the others are now three, and two is more than half. d is out with no further vote.
    take(mirror, e.signingKey, { kind: "leave" });
    expect(keys(mirror)).toEqual([a.signingKey, b.signingKey, c.signingKey]);

    const other = fresh(a, b, c, d);
    take(other, a.signingKey, { kind: "vote", signingKey: d.signingKey });
    // The voter leaves: the vote goes with them.
    take(other, a.signingKey, { kind: "leave" });
    expect(other.state.votes).toEqual([]);
    expect(keys(other)).toEqual([b.signingKey, c.signingKey, d.signingKey]);
  });

  it("gives two mirrors that took the same changes the same members, invited and votes", () => {
    const changes: [string, Change][] = [
      [a.signingKey, { kind: "invite", member: d }],
      [b.signingKey, { kind: "vote", signingKey: c.signingKey }],
      [d.signingKey, { kind: "accept" }],
      [d.signingKey, { kind: "vote", signingKey: c.signingKey }],
      [a.signingKey, { kind: "invite", member: e }],
    ];
    const [one, two] = [fresh(a, b, c), fresh(a, b, c)];
    for (const mirror of [one, two]) for (const [i, [key, change]] of changes.entries()) mirror.accept({ signerKey: key, payload: payload(mirror, change), step: step(i + 1) });
    expect(two.commitment).toBe(one.commitment);
    expect(two.state).toEqual(one.state);
    // b and d are two of the three others of c: c is out.
    expect(keys(one)).toEqual([a.signingKey, b.signingKey, d.signingKey]);
    expect(one.state.invited).toEqual([e]);
  });
});
