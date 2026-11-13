// The publisher's world files and a founding, for the rule suites.

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { readCatalogue, type Catalogue } from "../../src/faction/catalogue.js";
import { readParcels, type Parcel } from "../../src/faction/parcel.js";
import type { Founding, Member } from "../../src/faction/state.js";

const world = (name: string): Uint8Array =>
  new Uint8Array(readFileSync(fileURLToPath(new URL(`../../world/${name}`, import.meta.url))));

export const catalogue: Catalogue = readCatalogue(world("catalogue.json"));
export const parcels: Map<string, Parcel> = readParcels(world("parcels.json"));

export function parcel(id: string): Parcel {
  const found = parcels.get(id);
  if (found === undefined) throw new Error(`no parcel ${id}`);
  return found;
}

/** A member whose ids are the byte `n` repeated: distinct, well-formed, and nobody's real key. */
export function member(n: number): Member {
  const byte = n.toString(16).padStart(2, "0");
  return { genesis: byte.repeat(32), deviceId: (n + 100).toString(16).padStart(2, "0").repeat(32), signingKey: byte.repeat(32) };
}

export function founding(members: Member[], parcelId = "north-bluff"): Founding {
  return {
    storageSet: ["seat-1", "seat-2", "seat-3", "seat-4", "seat-5"],
    parcelId,
    catalogue: catalogue.version,
    members,
    nonce: "00".repeat(16),
  };
}

export const instance = (n: number): string => n.toString(16).padStart(32, "0");

/** A step as a cell would give it: 32 bytes, here the byte `n`. */
export const step = (n: number): Uint8Array => new Uint8Array(32).fill(n);
