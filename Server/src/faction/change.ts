// A change to a faction's state (spec 7.2), and the bytes a member's wallet
// signs and writes at the successor cell. A change names its faction_id and
// the state R_n it was made against (spec 6.3).
//
// Joining and removal are not here. Spec 7.2 has both carry more than one
// signature in the change, and a cell's payload (16,384 bytes) is smaller than
// one signature (49,856 bytes): see docs/dsm-gaps.md.

import { canonical, isHex, type Canonical } from "./canonical.js";
import type { Rotation } from "./state.js";

export type Change =
  | {
      kind: "place";
      instanceId: string;
      prefabId: string;
      x: number;
      y: number;
      rotation: Rotation;
      config: Record<string, string>;
    }
  | { kind: "replace"; instanceId: string; prefabId: string; config: Record<string, string> }
  | { kind: "dismantle"; instanceId: string }
  /** Rebuild a plan that was destroyed in battle, as it is (spec 7.6). */
  | { kind: "confirm"; instanceId: string }
  | { kind: "leave" };

/** A change as it is written: against one state of one faction. */
export interface SignedOver {
  factionId: string;
  /** R_n: the state this change was made against. */
  parent: string;
  change: Change;
}

/** The longest payload a cell's write carries (DSM Amendment A35). */
export const MAX_PAYLOAD_BYTES = 16 * 1024;

function changeValue(c: Change): Canonical {
  switch (c.kind) {
    case "place":
      return {
        config: c.config,
        instanceId: c.instanceId,
        kind: c.kind,
        prefabId: c.prefabId,
        rotation: c.rotation,
        x: c.x,
        y: c.y,
      };
    case "replace":
      return { config: c.config, instanceId: c.instanceId, kind: c.kind, prefabId: c.prefabId };
    case "dismantle":
    case "confirm":
      return { instanceId: c.instanceId, kind: c.kind };
    case "leave":
      return { kind: c.kind };
  }
}

/** The payload of `signedOver`: its canonical bytes. */
export function encodeChange(signedOver: SignedOver): Uint8Array {
  const bytes = canonical({
    change: changeValue(signedOver.change),
    factionId: signedOver.factionId,
    parent: signedOver.parent,
    v: 1,
  });
  if (bytes.length > MAX_PAYLOAD_BYTES) throw new Error("the change is larger than a cell's payload");
  return bytes;
}

type Read = { ok: true; signedOver: SignedOver } | { ok: false; why: string };

const refuse = (why: string): Read => ({ ok: false, why });

function configOf(value: unknown): Record<string, string> | undefined {
  if (typeof value !== "object" || value === null || Array.isArray(value)) return undefined;
  const config: Record<string, string> = {};
  for (const [key, v] of Object.entries(value)) {
    if (typeof v !== "string") return undefined;
    config[key] = v;
  }
  return config;
}

function wholeOf(value: unknown): number | undefined {
  return typeof value === "number" && Number.isSafeInteger(value) ? value : undefined;
}

function changeOf(raw: Record<string, unknown>): Change | string {
  const instanceId = raw.instanceId;
  const instance = (): string | undefined => (isHex(instanceId, 16) ? instanceId : undefined);
  switch (raw.kind) {
    case "place": {
      const id = instance();
      const config = configOf(raw.config);
      const x = wholeOf(raw.x);
      const y = wholeOf(raw.y);
      const rotation = raw.rotation;
      if (id === undefined) return "its instance_id is not 16 bytes of hex";
      if (typeof raw.prefabId !== "string") return "it names no prefab_id";
      if (x === undefined || y === undefined) return "its position is not whole map units";
      if (rotation !== 0 && rotation !== 90 && rotation !== 180 && rotation !== 270) return "its rotation is not a quarter turn";
      if (config === undefined) return "its configuration is not option names and values";
      return { kind: "place", instanceId: id, prefabId: raw.prefabId, x, y, rotation, config };
    }
    case "replace": {
      const id = instance();
      const config = configOf(raw.config);
      if (id === undefined) return "its instance_id is not 16 bytes of hex";
      if (typeof raw.prefabId !== "string") return "it names no prefab_id";
      if (config === undefined) return "its configuration is not option names and values";
      return { kind: "replace", instanceId: id, prefabId: raw.prefabId, config };
    }
    case "dismantle":
    case "confirm": {
      const id = instance();
      if (id === undefined) return "its instance_id is not 16 bytes of hex";
      return { kind: raw.kind, instanceId: id };
    }
    case "leave":
      return { kind: "leave" };
    default:
      return "it is not a kind of change";
  }
}

/**
 * Read a payload as a change. A payload that is not exactly the canonical
 * bytes of a change is not one: every mirror reads the same bytes the same way.
 */
export function decodeChange(payload: Uint8Array): Read {
  let raw: unknown;
  try {
    raw = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(payload));
  } catch {
    return refuse("the payload is not a change");
  }
  if (typeof raw !== "object" || raw === null || Array.isArray(raw)) return refuse("the payload is not a change");
  const fields = raw as Record<string, unknown>;
  if (fields.v !== 1) return refuse("the payload is not a version 1 change");
  if (!isHex(fields.factionId, 32)) return refuse("the change names no faction_id");
  if (!isHex(fields.parent, 32)) return refuse("the change names no state");
  if (typeof fields.change !== "object" || fields.change === null) return refuse("the payload carries no change");
  const change = changeOf(fields.change as Record<string, unknown>);
  if (typeof change === "string") return refuse(`the change is malformed: ${change}`);
  const signedOver: SignedOver = { factionId: fields.factionId, parent: fields.parent, change };
  let again: Uint8Array;
  try {
    again = encodeChange(signedOver);
  } catch {
    return refuse("the payload is not a change");
  }
  if (again.length !== payload.length || again.some((b, i) => b !== payload[i])) {
    return refuse("the payload is not the canonical bytes of its change");
  }
  return { ok: true, signedOver };
}
