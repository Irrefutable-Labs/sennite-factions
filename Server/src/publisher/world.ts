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

/** A skin the publisher's shop sells (spec 10.5): each one sold is an object of which one exists. */
export interface SkinForSale {
  id: string;
  name: string;
  /** Its price in D-bucks, in whole units. */
  price: string;
}

/** A hero the publisher's shop sells: each one sold is an object of which one exists, as a skin is. The game's free heroes are not sold. */
export interface HeroForSale {
  id: string;
  name: string;
  /** Its price in D-bucks, in whole units. */
  price: string;
}

/**
 * A gun the publisher's shop sells: each one sold is an object of which one exists, as a hero is. A wallet
 * that owns one is given that gun by the game's crates; the game's free guns are not sold.
 */
export interface WeaponForSale {
  id: string;
  name: string;
  /** Its price in D-bucks, in whole units. */
  price: string;
}

/** The game's currency, the publisher's vault that sells it for ERA, and the shop (spec 10.5). */
export interface ShopWorld {
  /** D-bucks: an object the publisher's account issues with a fixed maximum supply. */
  currency: ResourceIssue;
  /** What the publisher's vault is opened with, and its fee in hundredths of a percent. */
  vault: { era: string; dbucks: string; feeBps: number };
  /** What the grant lets the game ask for without asking the player again. */
  grant: { era: { perRequest: string; total: string }; dbucks: { perRequest: string; total: string } };
  skins: SkinForSale[];
  heroes: HeroForSale[];
  weapons: WeaponForSale[];
}

/**
 * What a match pays (spec 10.6): for each material, the whole units the
 * winning faction's members share. Every unit of it is new supply entering
 * circulation, out of the material's fixed cap.
 */
export interface MatchWorld {
  prize: Record<string, number>;
}

export interface World {
  catalogue: Catalogue;
  parcels: Map<string, Parcel>;
  economy: Economy;
  shop: ShopWorld;
  matches: MatchWorld;
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
  const shop = JSON.parse(new TextDecoder().decode(bytes("shop.json"))) as ShopWorld;
  if (new Set(shop.skins.map((k) => k.id)).size !== shop.skins.length) throw new Error("the shop lists a skin twice");
  shop.heroes ??= [];
  if (new Set(shop.heroes.map((h) => h.id)).size !== shop.heroes.length) throw new Error("the shop lists a hero twice");
  shop.weapons ??= [];
  // a wallet's heroes and guns are told to a game in one list, by these ids: no gun has a hero's id, or another gun's
  const owned = [...shop.heroes.map((h) => h.id), ...shop.weapons.map((w) => w.id)];
  if (new Set(owned).size !== owned.length) throw new Error("the shop lists a gun twice, or under a hero's id");
  const matches = JSON.parse(new TextDecoder().decode(bytes("matches.json"))) as MatchWorld;
  for (const [resource, amount] of Object.entries(matches.prize)) {
    if (!catalogue.resources.includes(resource)) throw new Error(`a match's prize names ${resource}, which is not a resource`);
    if (!Number.isSafeInteger(amount) || amount <= 0) throw new Error(`a match's prize of ${resource} is not a whole number of units above zero`);
  }
  return { catalogue, parcels: readParcels(bytes("parcels.json")), economy, shop, matches };
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
