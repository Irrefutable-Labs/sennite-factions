// Whether a change is valid (spec 7). The verdict comes from three inputs
// only: the parent state R_n, the catalogue version R_n names and the parcel
// R_n names. It never depends on any player's own DSM state, and never on
// funding (spec 7.3), so every mirror reaches the same verdict from the same
// material.

import type { Catalogue, Prefab } from "./catalogue.js";
import { contains, MAX_COORDINATE, overlaps, type Parcel, type Rect } from "./parcel.js";
import { decodeChange, type Change } from "./change.js";
import { memberBySigningKey, type FactionState, type Placed, type Rotation } from "./state.js";

export type Verdict = { valid: true; next: FactionState } | { valid: false; why: string };

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
      return { valid: true, next: { ...state, members: state.members.filter((m) => m.signingKey !== signerKey) } };
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
  if (catalogue.version !== state.catalogue) throw new Error("this is not the catalogue version the state names");
  if (parcel.id !== state.parcelId) throw new Error("this is not the parcel the state names");
  const read = decodeChange(payload);
  if (!read.ok) return invalid(read.why);
  const { factionId, parent: named, change } = read.signedOver;
  if (factionId !== state.factionId) return invalid("the change names another faction");
  if (named !== parent) return invalid("the change names another state");
  if (memberBySigningKey(state, signerKey) === undefined) return invalid("the signer is not a member the state lists");
  return applied(state, catalogue, parcel, signerKey, change);
}
