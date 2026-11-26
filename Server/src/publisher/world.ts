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

/**
 * The building materials the shop sells and buys back (owner, 2026-11-22: "they should be sold in the shop
 * as well"; "Server should offer to buy it back at a reduced rate ... like a 25% reduction"; "you should
 * only be able to sell it back to the game"). A pack of a material is sold for its price in D-bucks, and
 * bought back for `buyBackPercent` hundredths of that price, rounded down.
 */
export interface MaterialsForSale {
  /** How many whole units of a material one pack holds. */
  pack: number;
  /** resource symbol -> a pack's price in D-bucks, in whole units. A resource with no price is not sold. */
  price: Record<string, string>;
  /** What a pack sold back pays, in hundredths of its price: 75 pays a quarter under. Always under 100. */
  buyBackPercent: number;
}

/** A pack of a material as the shop trades it now: what it holds, what it costs, and what selling it back pays. */
export interface MaterialPack {
  symbol: string;
  name: string;
  units: number;
  /** In D-bucks, whole units. */
  price: number;
  buyBack: number;
}

/** The game's currency, the publisher's vault that sells it for ERA, and the shop (spec 10.5). */
export interface ShopWorld {
  /** D-bucks: an object the publisher's account issues with a fixed maximum supply. */
  currency: ResourceIssue;
  /** What the publisher's vault is opened with, and its fee in hundredths of a percent. */
  vault: { era: string; dbucks: string; feeBps: number };
  skins: SkinForSale[];
  heroes: HeroForSale[];
  weapons: WeaponForSale[];
  /** The materials the shop sells in packs and buys back; a shop with none trades no materials. */
  materials?: MaterialsForSale;
}

/**
 * What a match pays (spec 10.6): for each material, the whole units the
 * winning faction's members share. Every unit of it is new supply entering
 * circulation, out of the material's fixed cap.
 */
export interface MatchWorld {
  prize: Record<string, number>;
  /**
   * What a match's arena holds to mine (owner, 2026-11-22): for each material, the whole units its deposits
   * hold between them. A player's mining droid takes from them, and what it took is given to the player's
   * wallet when the match is over. It is the most a match can pay out in mining, whoever mines it; every
   * unit of it is new supply entering circulation, as a prize's is. A match with none has nothing to mine.
   */
  mining?: Record<string, number>;
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
  if (shop.materials !== undefined) checkMaterials(shop.materials, catalogue.resources);
  const matches = JSON.parse(new TextDecoder().decode(bytes("matches.json"))) as MatchWorld;
  for (const [resource, amount] of Object.entries(matches.prize)) {
    if (!catalogue.resources.includes(resource)) throw new Error(`a match's prize names ${resource}, which is not a resource`);
    if (!Number.isSafeInteger(amount) || amount <= 0) throw new Error(`a match's prize of ${resource} is not a whole number of units above zero`);
  }
  for (const [resource, amount] of Object.entries(matches.mining ?? {})) {
    if (!catalogue.resources.includes(resource)) throw new Error(`a match's mining names ${resource}, which is not a resource`);
    if (!Number.isSafeInteger(amount) || amount <= 0) throw new Error(`a match's mining of ${resource} is not a whole number of units above zero`);
  }
  return { catalogue, parcels: readParcels(bytes("parcels.json")), economy, shop, matches };
}

/**
 * What the shop's file says of materials, or an error naming what is wrong with it. A pack sold back pays
 * less than it costs and at least one D-buck: a shop that paid as much as it charged, or more, would hand
 * out D-bucks to anyone who bought and sold in a circle.
 */
export function checkMaterials(materials: MaterialsForSale, resources: readonly string[]): void {
  if (!Number.isSafeInteger(materials.pack) || materials.pack <= 0) throw new Error("a pack of a material is not a whole number of units above zero");
  if (!Number.isSafeInteger(materials.buyBackPercent) || materials.buyBackPercent <= 0 || materials.buyBackPercent >= 100) {
    throw new Error("what the shop pays for a pack sold back is not a whole percent of its price above 0 and under 100");
  }
  for (const [resource, price] of Object.entries(materials.price)) {
    if (!resources.includes(resource)) throw new Error(`the shop prices ${resource}, which is not a resource`);
    if (!/^[1-9][0-9]{0,8}$/.test(price)) throw new Error(`the shop's price for a pack of ${resource} is not a whole number of D-bucks above zero`);
    if (Math.floor((Number(price) * materials.buyBackPercent) / 100) < 1) throw new Error(`a pack of ${resource} sold back would pay nothing`);
  }
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
