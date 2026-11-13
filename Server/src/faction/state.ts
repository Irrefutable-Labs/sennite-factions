// The committed faction state (spec 5.1): who the members are, which parcel
// and catalogue version the base belongs to, which storage set it lives on,
// and what is placed where. Funding, contributions, recipes, the host and
// anything real-time are not state (spec 5.2, 5.3).

import { canonical, digest, hex, isHex, unhex, type Canonical } from "./canonical.js";

/** A member: genesis, device id and signing key, each as lowercase hex. */
export interface Member {
  genesis: string;
  deviceId: string;
  signingKey: string;
}

export type Rotation = 0 | 90 | 180 | 270;

/** A placed prefab: this is the base. */
export interface Placed {
  /** instance_id: 16 bytes, lowercase hex. */
  instanceId: string;
  /** prefab_id, from the catalogue. */
  prefabId: string;
  /** The centre of what it covers, in map units. */
  x: number;
  y: number;
  rotation: Rotation;
  /** Any configuration the catalogue allows. */
  config: Record<string, string>;
}

/** What a faction is created from. Its digest is the faction_id. */
export interface Founding {
  /** The five member ids of the storage set S, fixed at creation. */
  storageSet: string[];
  parcelId: string;
  /** The content hash of the catalogue the base is built against. */
  catalogue: string;
  members: Member[];
  /** 16 bytes the founders choose, so two factions of the same people differ. */
  nonce: string;
}

export interface FactionState {
  factionId: string;
  storageSet: string[];
  parcelId: string;
  /** The address of the publisher's signed parcel assignment (spec 4.3). */
  assignment: string;
  catalogue: string;
  /** In the order they were listed: ties in host selection break by it (spec 9.2). */
  members: Member[];
  /** In ascending instance_id. */
  placed: Placed[];
}

/** The most members a faction lists: a cell's writer set holds no more keys. */
export const MAX_MEMBERS = 64;

function memberValue(m: Member): Canonical {
  return { deviceId: m.deviceId, genesis: m.genesis, signingKey: m.signingKey };
}

export function checkMember(m: Member): void {
  if (!isHex(m.genesis, 32)) throw new Error("a member's genesis is not 32 bytes of hex");
  if (!isHex(m.deviceId, 32)) throw new Error("a member's device id is not 32 bytes of hex");
  if (!isHex(m.signingKey) || m.signingKey.length > 128) throw new Error("a member's signing key is not 1 to 64 bytes of hex");
}

export function foundingValue(f: Founding): Canonical {
  return {
    catalogue: f.catalogue,
    members: f.members.map(memberValue),
    nonce: f.nonce,
    parcelId: f.parcelId,
    storageSet: f.storageSet,
  };
}

export function checkFounding(f: Founding): void {
  if (f.storageSet.length !== 5 || f.storageSet.some((id) => id.length === 0)) {
    throw new Error("a faction's storage set is five member ids");
  }
  if (new Set(f.storageSet).size !== 5) throw new Error("a faction's storage set names a member twice");
  if (!isHex(f.catalogue, 32)) throw new Error("the catalogue version is not a content hash");
  if (!isHex(f.nonce, 16)) throw new Error("the founding nonce is not 16 bytes of hex");
  if (f.parcelId.length === 0) throw new Error("the founding names no parcel");
  if (f.members.length === 0 || f.members.length > MAX_MEMBERS) {
    throw new Error(`a faction is founded by 1 to ${MAX_MEMBERS} members`);
  }
  f.members.forEach(checkMember);
  if (new Set(f.members.map((m) => m.signingKey)).size !== f.members.length) {
    throw new Error("the founding lists a member twice");
  }
}

/** Read a founding back from the value `foundingValue` gives. Anything else is refused. */
export function readFounding(value: unknown): Founding {
  if (typeof value !== "object" || value === null || Array.isArray(value)) throw new Error("not a founding");
  const raw = value as Record<string, unknown>;
  const strings = (v: unknown, what: string): string[] => {
    if (!Array.isArray(v) || v.some((x) => typeof x !== "string")) throw new Error(`the founding's ${what} is not a list of names`);
    return v as string[];
  };
  const text = (v: unknown, what: string): string => {
    if (typeof v !== "string") throw new Error(`the founding's ${what} is missing`);
    return v;
  };
  if (!Array.isArray(raw.members)) throw new Error("the founding lists no members");
  const founding: Founding = {
    storageSet: strings(raw.storageSet, "storage set"),
    parcelId: text(raw.parcelId, "parcel"),
    catalogue: text(raw.catalogue, "catalogue version"),
    members: (raw.members as Record<string, unknown>[]).map((m) => ({
      genesis: text(m.genesis, "member genesis"),
      deviceId: text(m.deviceId, "member device id"),
      signingKey: text(m.signingKey, "member signing key"),
    })),
    nonce: text(raw.nonce, "nonce"),
  };
  checkFounding(founding);
  return founding;
}

/** The canonical bytes of a founding: what the publisher's assignment carries. */
export function foundingBytes(f: Founding): Uint8Array {
  return canonical(foundingValue(f));
}

/** faction_id: fixed at creation, the digest of what the faction was founded from. */
export function factionId(f: Founding): string {
  return hex(digest("sennite/faction/id/v1", foundingBytes(f)));
}

/** The first state R_0 of the faction `f` founds, under the assignment at `assignment`. */
export function firstState(f: Founding, assignment: string): FactionState {
  checkFounding(f);
  if (!isHex(assignment, 32)) throw new Error("the assignment's address is not 32 bytes of hex");
  return {
    factionId: factionId(f),
    storageSet: [...f.storageSet],
    parcelId: f.parcelId,
    assignment,
    catalogue: f.catalogue,
    members: f.members.map((m) => ({ ...m })),
    placed: [],
  };
}

function stateValue(s: FactionState): Canonical {
  return {
    assignment: s.assignment,
    catalogue: s.catalogue,
    factionId: s.factionId,
    members: s.members.map(memberValue),
    parcelId: s.parcelId,
    placed: s.placed.map((p) => ({
      config: p.config,
      instanceId: p.instanceId,
      prefabId: p.prefabId,
      rotation: p.rotation,
      x: p.x,
      y: p.y,
    })),
    storageSet: s.storageSet,
  };
}

/** The canonical bytes of a state. */
export function stateBytes(s: FactionState): Uint8Array {
  return canonical(stateValue(s));
}

function fieldsDigest(s: FactionState): Uint8Array {
  return digest("sennite/faction/state/v1", stateBytes(s));
}

/** R_0: the commitment over the first state's fields (spec 5.1). */
export function firstCommitment(s: FactionState): string {
  return hex(digest("sennite/faction/R0/v1", fieldsDigest(s)));
}

/**
 * R_n+1: it commits to R_n, to the occupant of R_n's successor cell (through
 * the cell's step, which DSM derives from the cell and the exact bytes that
 * hold it) and to the state that results. It advances when the occupant is
 * invalid and the base is unchanged, so the next successor cell is never the
 * one just spent (spec 7.4).
 */
export function nextCommitment(parent: string, step: Uint8Array, next: FactionState): string {
  if (step.length !== 32) throw new Error("a cell's step is 32 bytes");
  return hex(digest("sennite/faction/Rn/v1", unhex(parent, "R_n"), step, fieldsDigest(next)));
}

/**
 * What the application names the successor cell of R_n: faction_id then R_n,
 * 64 bytes. DSM derives the cell from this name, the publisher's account and
 * the cell's writers (spec 6.2; DSM Amendment A35).
 */
export function successorCellName(factionIdHex: string, commitment: string): Uint8Array {
  const name = new Uint8Array(64);
  name.set(unhex(factionIdHex, "faction_id"), 0);
  name.set(unhex(commitment, "R_n"), 32);
  return name;
}

/** The successor cell's writers: the signing keys of the members R_n lists (spec 6.3). */
export function writerKeys(s: FactionState): Uint8Array[] {
  return s.members.map((m) => unhex(m.signingKey, "a member's signing key"));
}

export function memberBySigningKey(s: FactionState, signingKey: string): Member | undefined {
  return s.members.find((m) => m.signingKey === signingKey);
}
