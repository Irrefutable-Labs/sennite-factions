// The publisher's world files (spec 4): the prefab catalogue, the parcel
// registry and what the publisher's account issues. Ordinary game assets;
// none of it is written into DSM.

import { readFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { readCatalogue, type Catalogue } from "../faction/catalogue.js";
import { readParcels, type Parcel } from "../faction/parcel.js";

/** A resource the publisher's account issues, with its fixed maximum supply (spec 8.5). */
export interface ResourceIssue {
  symbol: string;
  name: string;
  /** The whole supply that will ever exist, in whole units. */
  supply: string;
}

export interface Economy {
  resources: ResourceIssue[];
  /** What the grant lets the game commit of each resource without asking the player again. */
  grant: { perRequest: string; total: string };
}

export interface World {
  catalogue: Catalogue;
  parcels: Map<string, Parcel>;
  economy: Economy;
}

/** The world folder shipped beside the server. */
export const WORLD_DIR = fileURLToPath(new URL("../../world", import.meta.url));

export function loadWorld(dir: string = WORLD_DIR): World {
  const bytes = (name: string): Uint8Array => new Uint8Array(readFileSync(join(dir, name)));
  const catalogue = readCatalogue(bytes("catalogue.json"));
  const economy = JSON.parse(new TextDecoder().decode(bytes("economy.json"))) as Economy;
  for (const resource of catalogue.resources) {
    if (!economy.resources.some((r) => r.symbol === resource)) throw new Error(`nothing issues the catalogue's resource ${resource}`);
  }
  return { catalogue, parcels: readParcels(bytes("parcels.json")), economy };
}

/**
 * The member ids of the storage set an env config names: each node's
 * register incarnation, in the config's order. A faction commits them when it
 * is created (spec 5.1).
 */
export function storageSetOf(envConfigFile: string): string[] {
  const text = readFileSync(envConfigFile, "utf8");
  const ids = [...text.matchAll(/^register_incarnation\s*=\s*"([^"]+)"\s*$/gm)].map((m) => m[1] as string);
  if (ids.length !== 5) throw new Error(`${envConfigFile} names ${ids.length} storage members; a storage set has five`);
  return ids;
}
