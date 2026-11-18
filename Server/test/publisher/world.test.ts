/**
 * The shop's world as it ships (world/shop.json), with no account and no storage node: what the
 * shop sells is listed once, and the guns it sells are the two the game's crates hold back for
 * the wallets that own them.
 */
import { describe, expect, it } from "vitest";
import { loadWorld } from "../../src/publisher/world.js";

describe("the shop's world", () => {
  const { shop } = loadWorld();

  it("sells the energy rifle and the sniper, by the ids the game knows them by, each at a price in whole D-bucks", () => {
    expect(shop.weapons.map((w) => w.id)).toEqual(["energy-rifle", "sniper"]);
    for (const weapon of shop.weapons) {
      expect(weapon.name).not.toBe("");
      expect(weapon.price).toMatch(/^[1-9][0-9]*$/);
    }
  });

  it("lists no gun under a hero's id: a wallet's heroes and guns are told to a game in one list", () => {
    const ids = [...shop.heroes.map((h) => h.id), ...shop.weapons.map((w) => w.id)];
    expect(new Set(ids).size).toBe(ids.length);
  });

  it("lists every skin, hero and gun once", () => {
    for (const listed of [shop.skins, shop.heroes, shop.weapons]) {
      expect(new Set(listed.map((each) => each.id)).size).toBe(listed.length);
    }
  });
});
