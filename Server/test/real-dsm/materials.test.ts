/**
 * Building materials bought from the shop and sold back to it, on DSM with no game attached (owner,
 * 2026-11-22: materials "should be sold in the shop as well", bought back at "like a 25% reduction", and
 * "you should only be able to sell it back to the game").
 *
 * The publisher's account holds the materials it issued and the D-bucks. A player's wallet pays D-bucks
 * for a pack and is given it; it sends a pack back and is paid a quarter less. Each half is a transfer
 * on DSM: the wallet's waits in the wallet until the player approves it, and the shop gives its half
 * only once the publisher's account has the wallet's.
 *
 * Every balance here is read from the account that holds it. The player's D-bucks are given by the
 * publisher's account at the start: how D-bucks are got for ERA is the shop suite's (shop.test.ts).
 *
 * Run with `npm run test:dsm` (see hosts.ts for what it needs).
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { ask } from "dsm-app-client";
import { until } from "./hosts.js";
import { player, stage, type Stage } from "./stage.js";
import { approving, spendable } from "./wallets.js";

const DBUCKS = "DBUCKS";
let s: Stage;

const walletHas = (name: string, symbol: string): Promise<bigint> => spendable(player(s, name).wallet, symbol);
const publisherHas = async (symbol: string): Promise<bigint> => (await s.app.owned()).find((o) => o.object.symbol === symbol)?.amount ?? 0n;

beforeAll(async () => {
  s = await stage("materials", ["alice", "bob"]);
  // alice starts with 200 D-bucks of the publisher's
  const alice = player(s, "alice");
  const dbucks = await s.app.object(DBUCKS);
  await s.app.wait(await s.app.request(alice.connection, ask.accept(dbucks)), { until: "answered" });
  await s.app.give(alice.connection, dbucks, "200");
  await until("alice's wallet holds the D-bucks she starts with", 240_000, async () => ((await walletHas("alice", DBUCKS)) === 200n ? true : null));
});

afterAll(async () => {
  await s?.stop();
});

describe("materials in the shop", () => {
  it("are priced in packs by the shop's file: a pack sells back for a quarter under what it costs", () => {
    expect(s.publisher.shop.pack("WOOD")).toEqual({ symbol: "WOOD", name: "WOOD", units: 50, price: 50, buyBack: 37 });
    expect(s.publisher.shop.packs().map((p) => p.symbol)).toEqual(["WOOD", "STONE", "STEEL"]);
  });

  it("the shop sells a pack for D-bucks: paid first, then given from what the publisher holds", async () => {
    const [dbucksBefore, woodBefore, takings, stock] = [await walletHas("alice", DBUCKS), await walletHas("alice", "WOOD"), await publisherHas(DBUCKS), await publisherHas("WOOD")];
    const alice = player(s, "alice");

    const bought = await approving(alice.wallet, s.publisher.shop.buyMaterial(alice.connection, "WOOD"));
    if (!bought.ok) throw new Error(bought.why);
    expect(bought.trade).toMatchObject({ way: "bought", resource: "WOOD", units: 50, dbucks: 50 });
    // what the shop owes is given at once, or at its next look once the wallet has taken the payment in
    if (!bought.given) await until("the shop gives the pack it owes", 240_000, async () => (await s.publisher.shop.sweep(), Object.keys(s.store.data.shop.owed).length === 0 ? true : null));

    // The player paid the pack's price and holds the pack; the publisher holds the price and a pack less.
    await until("alice's wallet holds the pack", 240_000, async () => ((await walletHas("alice", "WOOD")) === woodBefore + 50n ? true : null));
    expect(dbucksBefore - (await walletHas("alice", DBUCKS))).toBe(50n);
    await until("the publisher's account holds the price", 240_000, async () => ((await publisherHas(DBUCKS)) === takings + 50n ? true : null));
    expect(await publisherHas("WOOD")).toBe(stock - 50n);
    expect(s.store.data.shop.owed).toEqual({});
    expect(s.store.data.shop.trades).toEqual({});
  });

  it("the shop buys the pack back for a quarter under its price: sent first, then paid", async () => {
    const [dbucksBefore, woodBefore, takings, stock] = [await walletHas("alice", DBUCKS), await walletHas("alice", "WOOD"), await publisherHas(DBUCKS), await publisherHas("WOOD")];
    const alice = player(s, "alice");
    expect(woodBefore).toBeGreaterThanOrEqual(50n);

    const sold = await approving(alice.wallet, s.publisher.shop.sellMaterial(alice.connection, "WOOD"));
    if (!sold.ok) throw new Error(sold.why);
    expect(sold.trade).toMatchObject({ way: "sold", resource: "WOOD", units: 50, dbucks: 37 });
    if (!sold.given) await until("the shop pays what it owes", 240_000, async () => (await s.publisher.shop.sweep(), Object.keys(s.store.data.shop.owed).length === 0 ? true : null));

    // The pack went back to the publisher, and the player was paid 37 D-bucks for what cost 50.
    await until("alice's wallet holds what the pack sold for", 240_000, async () => ((await walletHas("alice", DBUCKS)) === dbucksBefore + 37n ? true : null));
    expect(woodBefore - (await walletHas("alice", "WOOD"))).toBe(50n);
    await until("the publisher's account holds the pack again", 240_000, async () => ((await publisherHas("WOOD")) === stock + 50n ? true : null));
    expect(await publisherHas(DBUCKS)).toBe(takings - 37n);
    expect(s.store.data.shop.owed).toEqual({});
  });

  it("a wallet that holds none of a material is paid nothing for it", async () => {
    const bob = player(s, "bob");
    expect(await walletHas("bob", "STEEL")).toBe(0n);
    const [dbucksBefore, takings] = [await walletHas("bob", DBUCKS), await publisherHas(DBUCKS)];
    const refused = await approving(bob.wallet, s.publisher.shop.sellMaterial(bob.connection, "STEEL"));
    expect(refused.ok).toBe(false);
    expect(await walletHas("bob", DBUCKS)).toBe(dbucksBefore);
    expect(await publisherHas(DBUCKS)).toBe(takings);
    expect(s.store.data.shop.owed).toEqual({});
  });

  it("a wallet with no D-bucks is sold no pack", async () => {
    const bob = player(s, "bob");
    expect(await walletHas("bob", DBUCKS)).toBe(0n);
    const stock = await publisherHas("STONE");
    const refused = await approving(bob.wallet, s.publisher.shop.buyMaterial(bob.connection, "STONE"));
    expect(refused.ok).toBe(false);
    expect(await walletHas("bob", "STONE")).toBe(0n);
    expect(await publisherHas("STONE")).toBe(stock);
  });
});
