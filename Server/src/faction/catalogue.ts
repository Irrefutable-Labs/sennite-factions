// The prefab catalogue (spec 4.2): the publisher's game asset. Each entry has
// its prefab_id, the placement rules that apply to it, and its recipe. The
// recipe lives only here; faction state never copies it. A catalogue version
// is the content hash of the catalogue's bytes (spec 4.1).

import { digest, hex } from "./canonical.js";

export interface Prefab {
  /** prefab_id */
  id: string;
  name: string;
  /** The ground it covers, in map units, before rotation. */
  footprint: { width: number; depth: number };
  /** The quantity of each resource it needs, by the resource's symbol. */
  recipe: Record<string, number>;
  /** The configuration the catalogue allows: each option and its allowed values. */
  options: Record<string, string[]>;
}

export interface Catalogue {
  /** The content hash of the bytes this catalogue was read from (lowercase hex). */
  version: string;
  /** Placements snap to this grid, in map units. */
  grid: number;
  /** The resources recipes are counted in, by symbol. */
  resources: string[];
  prefabs: Map<string, Prefab>;
}

function whole(value: unknown, what: string, min: number): number {
  if (typeof value !== "number" || !Number.isSafeInteger(value) || value < min) {
    throw new Error(`the catalogue's ${what} is not a whole number of at least ${min}`);
  }
  return value;
}

function text(value: unknown, what: string): string {
  if (typeof value !== "string" || value.length === 0 || value.length > 64) {
    throw new Error(`the catalogue's ${what} is not a name of 1 to 64 characters`);
  }
  return value;
}

export function catalogueVersion(bytes: Uint8Array): string {
  return hex(digest("sennite/catalogue/v1", bytes));
}

/** Read a catalogue from its bytes. Anything malformed is refused, never repaired. */
export function readCatalogue(bytes: Uint8Array): Catalogue {
  const raw = JSON.parse(new TextDecoder().decode(bytes)) as Record<string, unknown>;
  const grid = whole(raw.grid, "grid", 1);
  if (!Array.isArray(raw.resources) || raw.resources.length === 0) throw new Error("the catalogue names no resources");
  const resources = raw.resources.map((r, i) => text(r, `resource ${i}`));
  if (new Set(resources).size !== resources.length) throw new Error("the catalogue names a resource twice");
  if (!Array.isArray(raw.prefabs) || raw.prefabs.length === 0) throw new Error("the catalogue has no prefabs");
  const prefabs = new Map<string, Prefab>();
  for (const entry of raw.prefabs as Record<string, unknown>[]) {
    const id = text(entry.id, "prefab_id");
    if (prefabs.has(id)) throw new Error(`the catalogue lists prefab ${id} twice`);
    const footprint = entry.footprint as Record<string, unknown> | undefined;
    const width = whole(footprint?.width, `${id} footprint width`, 1);
    const depth = whole(footprint?.depth, `${id} footprint depth`, 1);
    if (width % grid !== 0 || depth % grid !== 0) throw new Error(`${id}'s footprint is not a whole number of grid cells`);
    const recipe: Record<string, number> = {};
    for (const [symbol, amount] of Object.entries((entry.recipe ?? {}) as Record<string, unknown>)) {
      if (!resources.includes(symbol)) throw new Error(`${id}'s recipe names ${symbol}, which is not a resource`);
      recipe[symbol] = whole(amount, `${id} recipe ${symbol}`, 1);
    }
    if (Object.keys(recipe).length === 0) throw new Error(`${id} has no recipe`);
    const options: Record<string, string[]> = {};
    for (const [option, allowed] of Object.entries((entry.options ?? {}) as Record<string, unknown>)) {
      if (!Array.isArray(allowed) || allowed.length === 0) throw new Error(`${id}'s option ${option} allows nothing`);
      options[text(option, `${id} option`)] = allowed.map((a, i) => text(a, `${id} option ${option} value ${i}`));
    }
    prefabs.set(id, { id, name: text(entry.name, `${id} name`), footprint: { width, depth }, recipe, options });
  }
  return { version: catalogueVersion(bytes), grid, resources, prefabs };
}
