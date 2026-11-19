// Whether a change is valid (spec 7). The verdict comes from three inputs
// only: the parent state R_n, the catalogue version R_n names and the parcel
// R_n names. It never depends on any player's own DSM state, and never on
// funding (spec 7.3), so every mirror reaches the same verdict from the same
// material.

import type { Catalogue, Prefab } from "./catalogue.js";
import { contains, MAX_COORDINATE, overlaps, type Parcel, type Rect } from "./parcel.js";
import { decodeChange, type Change } from "./change.js";
import {
  invitedBySigningKey,
  MAX_MEMBERS,
  memberBySigningKey,
  type FactionState,
  type Placed,
  type RemovalVote,
  type Rotation,
} from "./state.js";

/**
 * A valid change gives the next state. An invalid one changes nothing of the
 * base; `next` is given only when the invalid occupant was an invited wallet's,
 * whose invitation it used up.
 */
export type Verdict = { valid: true; next: FactionState } | { valid: false; why: string; next?: FactionState };

const invalid = (why: string): Verdict => ({ valid: false, why });

/** The ground a prefab covers at a placement. A quarter turn swaps its width and depth. */
export function covered(prefab: Prefab, x: number, y: number, rotation: Rotation): Rect {
  const turned = rotation === 90 || rotation === 270;
  const width = turned ? prefab.footprint.depth : prefab.footprint.width;
  const depth = turned ? prefab.footprint.width : prefab.footprint.depth;
  return { minX: x - width / 2, maxX: x + width / 2, minY: y - depth / 2, maxY: y + depth / 2 };
}

function configAllowed(prefab: Prefab, config: Record<string, string>): string | undefined {
  for (const [option, value] of Object.entries(config)) {
    const allowed = Object.hasOwn(prefab.options, option) ? prefab.options[option] : undefined;
    if (allowed === undefined) return `the catalogue gives ${prefab.id} no option ${option}`;
    if (!allowed.includes(value)) return `the catalogue does not allow ${option} = ${value} on ${prefab.id}`;
  }
  return undefined;
}

/**
 * Why `prefab` cannot stand at this placement against what is already placed
 * (leaving out `except`, the instance being replaced), or undefined when it can.
 */
function placementRefusal(
  catalogue: Catalogue,
  parcel: Parcel,
  placed: Placed[],
  prefab: Prefab,
  at: { x: number; y: number; rotation: Rotation },
  except?: string,
): string | undefined {
  if (Math.abs(at.x) > MAX_COORDINATE || Math.abs(at.y) > MAX_COORDINATE) return "the placement is off the map";
  const rect = covered(prefab, at.x, at.y, at.rotation);
  for (const edge of [rect.minX, rect.minY]) {
    if (!Number.isInteger(edge) || edge % catalogue.grid !== 0) return "the placement is not on the catalogue's grid";
  }
  if (!contains(parcel.boundary, rect)) return "the placement is not inside the parcel boundary";
  for (const other of placed) {
    if (other.instanceId === except) continue;
    const otherPrefab = catalogue.prefabs.get(other.prefabId);
    // A placed prefab the catalogue no longer lists cannot happen under one
    // catalogue version; refuse rather than guess what it covers.
    if (otherPrefab === undefined) return `placed prefab ${other.prefabId} is not in the catalogue`;
    if (overlaps(rect, covered(otherPrefab, other.x, other.y, other.rotation))) {
      return `the placement overlaps ${other.prefabId} ${other.instanceId}`;
    }
  }
  return undefined;
}

const byInstance = (a: Placed, b: Placed): number => (a.instanceId < b.instanceId ? -1 : a.instanceId > b.instanceId ? 1 : 0);

function applied(state: FactionState, catalogue: Catalogue, parcel: Parcel, signerKey: string, change: Change): Verdict {
  const existing = "instanceId" in change ? state.placed.find((p) => p.instanceId === change.instanceId) : undefined;
  switch (change.kind) {
    case "place": {
      const prefab = catalogue.prefabs.get(change.prefabId);
      if (prefab === undefined) return invalid(`prefab ${change.prefabId} is not in the catalogue`);
      if (existing !== undefined) return invalid(`instance ${change.instanceId} is already used`);
      const config = configAllowed(prefab, change.config);
      if (config !== undefined) return invalid(config);
      const refusal = placementRefusal(catalogue, parcel, state.placed, prefab, change);
      if (refusal !== undefined) return invalid(refusal);
      const placed: Placed = {
        instanceId: change.instanceId,
        prefabId: change.prefabId,
        x: change.x,
        y: change.y,
        rotation: change.rotation,
        config: { ...change.config },
      };
      return { valid: true, next: { ...state, placed: [...state.placed, placed].sort(byInstance) } };
    }
    case "replace": {
      if (existing === undefined) return invalid(`instance ${change.instanceId} does not exist`);
      const prefab = catalogue.prefabs.get(change.prefabId);
      if (prefab === undefined) return invalid(`prefab ${change.prefabId} is not in the catalogue`);
      const config = configAllowed(prefab, change.config);
      if (config !== undefined) return invalid(config);
      const refusal = placementRefusal(catalogue, parcel, state.placed, prefab, existing, existing.instanceId);
      if (refusal !== undefined) return invalid(refusal);
      const replaced: Placed = { ...existing, prefabId: change.prefabId, config: { ...change.config } };
      return {
        valid: true,
        next: { ...state, placed: state.placed.map((p) => (p.instanceId === existing.instanceId ? replaced : p)) },
      };
    }
    case "dismantle":
      if (existing === undefined) return invalid(`instance ${change.instanceId} does not exist`);
      return { valid: true, next: { ...state, placed: state.placed.filter((p) => p.instanceId !== existing.instanceId) } };
    case "confirm":
      if (existing === undefined) return invalid(`instance ${change.instanceId} does not exist`);
      return { valid: true, next: state };
    case "leave":
      return { valid: true, next: settled({ ...state, members: state.members.filter((m) => m.signingKey !== signerKey) }) };
    case "invite": {
      const key = change.member.signingKey;
      if (memberBySigningKey(state, key) !== undefined) return invalid("the invited is already a member");
      if (invitedBySigningKey(state, key) !== undefined) return invalid("the invited is already invited");
      if (state.members.length + state.invited.length >= MAX_MEMBERS) return invalid("the faction has no room for another");
      return { valid: true, next: { ...state, invited: [...state.invited, { ...change.member }] } };
    }
    case "uninvite":
      if (invitedBySigningKey(state, change.signingKey) === undefined) return invalid("nobody with that key is invited");
      return { valid: true, next: { ...state, invited: state.invited.filter((m) => m.signingKey !== change.signingKey) } };
    case "accept": {
      // judged before this: the signer is the invited, and nobody else
      const joiner = invitedBySigningKey(state, signerKey);
      if (joiner === undefined) return invalid("the signer is not invited");
      return {
        valid: true,
        next: { ...state, members: [...state.members, joiner], invited: state.invited.filter((m) => m.signingKey !== signerKey) },
      };
    }
    case "vote": {
      const target = change.signingKey;
      if (target === signerKey) return invalid("a member does not vote on themselves: they leave");
      if (memberBySigningKey(state, target) === undefined) return invalid("nobody with that key is a member");
      const standing = state.votes.find((v) => v.target === target);
      if (standing?.voters.includes(signerKey)) return invalid("the signer has already voted to remove that member");
      const voters = [...(standing?.voters ?? []), signerKey].sort();
      const votes = [...state.votes.filter((v) => v.target !== target), { target, voters }].sort(byTarget);
      return { valid: true, next: settled({ ...state, votes }) };
    }
    case "unvote": {
      const target = change.signingKey;
      const standing = state.votes.find((v) => v.target === target);
      if (standing === undefined || !standing.voters.includes(signerKey)) return invalid("the signer has no vote to remove that member");
      const voters = standing.voters.filter((k) => k !== signerKey);
      const votes = [...state.votes.filter((v) => v.target !== target), ...(voters.length > 0 ? [{ target, voters }] : [])].sort(byTarget);
      return { valid: true, next: { ...state, votes } };
    }
  }
}

const byTarget = (a: RemovalVote, b: RemovalVote): number => (a.target < b.target ? -1 : a.target > b.target ? 1 : 0);

/**
 * The state once the votes that stand are counted (spec 7.2): a member is
 * removed when more than half of the other members have voted to remove them.
 * Only members' votes on members count; a vote of someone who is gone, or on
 * someone who is gone, is dropped. Removing one member can complete the vote on
 * another, so the count is repeated until nothing changes, lowest key first.
 */
export function settled(state: FactionState): FactionState {
  let members = state.members;
  let votes = state.votes;
  for (;;) {
    const keys = new Set(members.map((m) => m.signingKey));
    votes = votes
      .filter((v) => keys.has(v.target))
      .map((v) => ({ target: v.target, voters: v.voters.filter((k) => keys.has(k) && k !== v.target) }))
      .filter((v) => v.voters.length > 0)
      .sort(byTarget);
    const removed = votes.find((v) => v.voters.length * 2 > members.length - 1);
    if (removed === undefined) return { ...state, members, votes };
    members = members.filter((m) => m.signingKey !== removed.target);
  }
}

/**
 * The verdict on the occupant of the successor cell of `parent`: the payload
 * that holds it and the key that signed it. An occupant that is not a change,
 * names another faction or another state, was signed by a key the state does
 * not list, or breaks a rule is invalid: the base does not change (spec 7.4).
 */
export function judge(
  state: FactionState,
  parent: string,
  catalogue: Catalogue,
  parcel: Parcel,
  signerKey: string,
  payload: Uint8Array,
): Verdict {
  const verdict = judged(state, parent, catalogue, parcel, signerKey, payload);
  // An invited wallet has one write at the faction's cells: its acceptance. Anything else it
  // writes there counts for nothing and uses the invitation up, so it cannot spend cell after cell.
  if (!verdict.valid && invitedBySigningKey(state, signerKey) !== undefined) {
    return { valid: false, why: verdict.why, next: { ...state, invited: state.invited.filter((m) => m.signingKey !== signerKey) } };
  }
  return verdict;
}

function judged(
  state: FactionState,
  parent: string,
  catalogue: Catalogue,
  parcel: Parcel,
  signerKey: string,
  payload: Uint8Array,
): Verdict {
  if (catalogue.version !== state.catalogue) throw new Error("this is not the catalogue version the state names");
  if (parcel.id !== state.parcelId) throw new Error("this is not the parcel the state names");
  const read = decodeChange(payload);
  if (!read.ok) return invalid(read.why);
  const { factionId, parent: named, change } = read.signedOver;
  if (factionId !== state.factionId) return invalid("the change names another faction");
  if (named !== parent) return invalid("the change names another state");
  // An invited wallet may write its acceptance and nothing else; everything else is a member's to write.
  if (change.kind === "accept") {
    if (invitedBySigningKey(state, signerKey) === undefined) return invalid("the signer is not invited");
  } else if (memberBySigningKey(state, signerKey) === undefined) {
    return invalid("the signer is not a member the state lists");
  }
  return applied(state, catalogue, parcel, signerKey, change);
}
