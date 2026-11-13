/**
 * D-bucks, the publisher's vault, and a skin bought and resold, on DSM with no game attached
 * (spec 10.5; build step 5).
 *
 * The publisher's account issues D-bucks with a fixed supply and opens a vault that sells them for
 * ERA. A player's wallet trades ERA for D-bucks through that vault, pays D-bucks to the shop, and
 * is given a skin: an object of which one exists. The player then sells it on to another player,
 * wallet to wallet, with the game no party to the sale. Who owns the skin is what a wallet proves.
 *
 * Every balance here is read from the wallet's own account.
 *
 * Run with `npm run test:dsm` (see hosts.ts for what it needs).
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { until } from "./hosts.js";
import { player, stage, type Stage } from "./stage.js";
import { becomeContacts, send, spendable, takeUp } from "./wallets.js";

const DBUCKS = "DBUCKS";
let s: Stage;
/** The skin Alice buys: its object id and the symbol it was issued under. */
let skin = { object: "", symbol: "" };

const dbucksOf = (name: string): Promise<bigint> => spendable(player(s, name).wallet, DBUCKS);
const eraOf = (name: string): Promise<bigint> => spendable(player(s, name).wallet, "ERA");
const publishers = async (symbol: string): Promise<bigint> => (await s.app.owned()).find((o) => o.object.symbol === symbol)?.amount ?? 0n;
const vault = async () => (await s.app.vaults()).find((v) => v.active && [v.a.symbol, v.b.symbol].includes(DBUCKS));
const reserve = async (symbol: string): Promise<bigint> => {
  const v = await vault();
  if (v === undefined) throw new Error("the publisher's vault is not open");
  return v.a.symbol === symbol ? v.a.reserve : v.b.reserve;
};

async function purse(name: string) {
  const holdings = await s.publisher.holdings(player(s, name).connection);
  if (holdings === undefined) throw new Error(`${name}'s wallet proved nothing`);
  return holdings;
}

/** Trade ERA for D-bucks through the publisher's vault, as the game's shop does it; how many D-bucks arrived. */
async function buyDbucks(name: string, era: string): Promise<bigint> {
  const p = player(s, name);
  const before = await dbucksOf(name);
  const quoted = await s.publisher.shop.price(p.connection, era);
  expect(quoted).toBeGreaterThan(0);
  const traded = await s.publisher.shop.buyDbucks(p.connection, era, Math.floor(quoted * 0.97));
  expect(traded.ok, traded.why).toBe(true);
  return until(`${name}'s wallet holds the D-bucks it traded for`, 240_000, async () => {
    const now = await dbucksOf(name);
    return now > before ? now - before : null;
  });
}

beforeAll(async () => {
  s = await stage("shop", ["alice", "bob"]);
});

afterAll(async () => {
  await s?.stop();
});

describe("D-bucks and the shop", () => {
  it("D-bucks are issued with a fixed supply, and the publisher's vault sells them for ERA", async () => {
    const world = s.world.shop;
    const issued = (await s.app.owned()).find((o) => o.object.symbol === DBUCKS);
    expect(issued?.object.supply).toBe(world.currency.supply);
    const open = await vault();
    expect(open).toBeDefined();
    expect(open?.feeBps).toBe(world.vault.feeBps);
    expect(await reserve(DBUCKS)).toBe(BigInt(world.vault.dbucks));
    // What is not in the vault is the publisher's: the whole supply is in one of the two.
    expect((await publishers(DBUCKS)) + (await reserve(DBUCKS))).toBe(BigInt(world.currency.supply));
  });

  it("a player's wallet trades ERA for D-bucks through the vault, at the price its reserves set", async () => {
    const [eraBefore, vaultEra, vaultDbucks] = [await eraOf("alice"), await reserve("ERA"), await reserve(DBUCKS)];
    const got = await buyDbucks("alice", "2");

    // The D-bucks came out of the vault and the ERA went into it: the publisher's account earned the trade.
    expect(got).toBeGreaterThan(1000n);
    expect(await reserve(DBUCKS)).toBe(vaultDbucks - got);
    expect((await reserve("ERA")) - vaultEra).toBe(eraBefore - (await eraOf("alice")));
    expect((await purse("alice")).dbucks).toBe(Number(got));
  });

  it("the shop sells a skin for D-bucks: one object, issued once the payment is accepted, given to the wallet", async () => {
    const [dbucksBefore, takings] = [await dbucksOf("alice"), await publishers(DBUCKS)];
    const sold = await s.publisher.shop.buySkin(player(s, "alice").connection, "magma");
    if (!sold.ok) throw new Error(sold.why);
    expect(sold.skin).toMatchObject({ skinId: "magma", name: "Dekker Magma", serial: 1 });

    // The player paid the price, and the publisher keeps the proceeds.
    expect(dbucksBefore - (await dbucksOf("alice"))).toBe(800n);
    await until("the publisher's account holds the proceeds", 240_000, async () => ((await publishers(DBUCKS)) === takings + 800n ? true : null));

    // The skin is the player's own object, of which one exists, and the wallet proves it.
    const owned = await until("alice's wallet holds the skin", 240_000, async () => {
      return (await player(s, "alice").wallet.owned()).find((o) => o.object.id === sold.skin.object && o.amount === 1n);
    });
    expect(owned.object).toMatchObject({ kind: "unique", supply: "1" });
    skin = { object: owned.object.id, symbol: owned.object.symbol };
    expect((await purse("alice")).skins).toEqual([{ skinId: "magma", name: "Dekker Magma", object: skin.object, serial: 1 }]);
  });

  it("a wallet with no D-bucks is sold nothing: no payment, no skin", async () => {
    expect(await dbucksOf("bob")).toBe(0n);
    const refused = await s.publisher.shop.buySkin(player(s, "bob").connection, "frost");
    expect(refused.ok).toBe(false);
    expect(s.publisher.shop.skinsAmong(new Map([[skin.object, 1n]]))).toHaveLength(1);
    expect((await purse("bob")).skins).toEqual([]);
    expect(Object.keys(s.store.data.shop.skins)).toEqual([skin.object]);
  });

  it("the player sells the skin on to another player, wallet to wallet: the game is no party to it", async () => {
    const [alice, bob] = [player(s, "alice"), player(s, "bob")];
    const bought = await buyDbucks("bob", "2");
    expect(bought).toBeGreaterThanOrEqual(900n);
    const [aliceBefore, bobBefore, takings, record] = [await dbucksOf("alice"), await dbucksOf("bob"), await publishers(DBUCKS), JSON.stringify(s.store.data.shop)];

    // Seller and buyer are contacts; the buyer's wallet takes up the skin's object so it can hold it.
    await becomeContacts({ wallet: alice.wallet, is: alice.member, name: "alice" }, { wallet: bob.wallet, is: bob.member, name: "bob" });
    await takeUp(bob.host, skin.object);

    // The buyer sends D-bucks, the seller sends the skin.
    await until("bob sends alice 900 D-bucks", 240_000, async () => {
      const sent = await send(bob.wallet, alice.member, DBUCKS, "900");
      if (!sent.sent) throw new Error(sent.message);
      return true;
    });
    await until("alice's wallet takes in the 900 D-bucks", 240_000, async () => ((await dbucksOf("alice")) === aliceBefore + 900n ? true : null));
    await until("alice sends bob the skin", 240_000, async () => {
      const sent = await send(alice.wallet, bob.member, skin.symbol, "1");
      if (!sent.sent) throw new Error(sent.message);
      return true;
    });
    await until("bob's wallet takes in the skin", 240_000, async () => {
      return (await bob.wallet.owned()).find((o) => o.object.id === skin.object && o.amount === 1n);
    });

    // The seller, not the publisher, received the proceeds; the shop sold nothing and recorded nothing.
    expect(await dbucksOf("bob")).toBe(bobBefore - 900n);
    expect(await publishers(DBUCKS)).toBe(takings);
    expect(JSON.stringify(s.store.data.shop)).toBe(record);

    // Next time each of them is in the game, it asks the wallet what it owns: the skin is the buyer's.
    expect((await purse("bob")).skins).toEqual([{ skinId: "magma", name: "Dekker Magma", object: skin.object, serial: 1 }]);
    expect((await purse("alice")).skins).toEqual([]);
  });
});
