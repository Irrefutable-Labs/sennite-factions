/**
 * A material bought from the shop and sold back to it, with a stand-in for the publisher's account: what the
 * shop does with the wallet's answer to "send me your half", whenever it comes, and that it gives its own
 * half once. The transfers themselves are DSM's. The owner, 2026-11-22: materials "should be sold in the
 * shop as well", the shop buys them back at "like a 25% reduction", and "you should only be able to sell it
 * back to the game".
 */
import { describe, expect, it } from "vitest";
import type { AppAccount, Connection, ObjectInfo, RequestRef, Result } from "dsm-app-client";
import { Shop } from "../../src/publisher/shop.js";
import { Store } from "../../src/publisher/store.js";
import { checkMaterials, type ShopWorld } from "../../src/publisher/world.js";

const world = (): ShopWorld => ({
  currency: { symbol: "DBUCKS", name: "D-bucks", supply: "100000000" },
  vault: { era: "10", dbucks: "10000", feeBps: 100 },
  materials: { pack: 50, price: { WOOD: "50", STEEL: "60" }, buyBackPercent: 75 },
  skins: [{ id: "magma", name: "Dekker Magma", price: "800" }],
  heroes: [],
  weapons: [],
});

const connection = { id: "CONN1", connected: true, opponent: { signingKey: new Uint8Array([7, 7, 7]) } } as unknown as Connection;

/** The account as the shop uses it: what it asked the wallet, the results as the test sets them, and what it gave. */
class Account {
  seq = 0n;
  readonly asks: { ref: RequestRef; what: unknown }[] = [];
  readonly results = new Map<string, Result>();
  readonly given: string[] = [];
  /** How many of the next gives the account refuses, as one that could not send. */
  refuseGives = 0;
  readonly objects = ["DBUCKS", "WOOD", "STEEL"].map((symbol) => ({ id: `${symbol}-OBJECT`, symbol, name: symbol, decimals: 0 }) as unknown as ObjectInfo);

  key(ref: RequestRef): string {
    return `${ref.connection}:${ref.seq}`;
  }
  async request(c: Connection, what: unknown): Promise<RequestRef> {
    const ref = { connection: c.id, seq: (this.seq += 1n) };
    this.asks.push({ ref, what });
    this.results.set(this.key(ref), { request: ref, answered: false, outcome: "unanswered", reason: "", fact: "none", detail: "" } as unknown as Result);
    return ref;
  }
  async result(ref: RequestRef): Promise<Result> {
    const found = this.results.get(this.key(ref));
    if (found === undefined) throw new Error("no such request");
    return found;
  }
  /** Used for the wallet taking an object up: answered at once here. */
  async wait(ref: RequestRef): Promise<Result> {
    return { ...(await this.result(ref)), answered: true, outcome: "carried_out" };
  }
  async connection(): Promise<Connection> {
    return connection;
  }
  async connections(): Promise<Connection[]> {
    return [connection];
  }
  async object(ref: string): Promise<ObjectInfo> {
    const found = this.objects.find((o) => o.id === ref || o.symbol === ref);
    if (found === undefined) throw new Error(`no such object ${ref}`);
    return found;
  }
  async give(_c: Connection, object: ObjectInfo, amount: string): Promise<void> {
    if (this.refuseGives > 0) {
      this.refuseGives -= 1;
      throw new Error("the transfer was not sent");
    }
    this.given.push(`${amount} ${object.symbol}`);
  }

  /** What the wallet was asked to send, in the order asked: the sends only, not the taking up of an object. */
  sends(): unknown[] {
    return this.asks.map((a) => a.what).filter((what) => JSON.stringify(what).includes('"send"'));
  }

  /** The wallet's answer to the send asked `nth` (from 0), landing as the transfer `transfer` when it paid. */
  answer(outcome: Result["outcome"], paid: boolean, nth = 0, transfer = 9): void {
    const sent = this.asks.filter((a) => JSON.stringify(a.what).includes('"send"'))[nth]!;
    const key = this.key(sent.ref);
    this.results.set(key, { ...this.results.get(key)!, answered: outcome !== "awaiting_approval", outcome, fact: paid ? "received" : "none", ...(paid ? { received: new Uint8Array([transfer]) } : {}) });
  }
}

const stand = (waitMs: number) => {
  const account = new Account();
  const store = new Store();
  const shop = new Shop(account as unknown as AppAccount, store, world(), { waitMs, pollMs: 5 });
  return { account, store, shop };
};
const soon = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

describe("a pack of a material", () => {
  it("is priced by the shop's file, and sold back for a quarter under, rounded down", () => {
    const { shop } = stand(10);
    expect(shop.packs()).toEqual([
      { symbol: "WOOD", name: "WOOD", units: 50, price: 50, buyBack: 37 },
      { symbol: "STEEL", name: "STEEL", units: 50, price: 60, buyBack: 45 },
    ]);
    expect(shop.pack("steel").symbol).toBe("STEEL");
    expect(() => shop.pack("STONE")).toThrow("the shop trades no STONE");
  });

  it("is given once the wallet's D-bucks have landed, and not before", async () => {
    const { account, store, shop } = stand(2_000);
    const stages: string[] = [];
    const buying = shop.buyMaterial(connection, "WOOD", (stage, onWallet) => stages.push(`${onWallet ? "wallet" : "shop"}: ${stage}`));
    await soon(30);
    account.answer("awaiting_approval", false);
    await soon(30);
    // the wallet was asked for the pack's price in D-bucks, and has been given nothing
    expect(JSON.stringify(account.sends()[0])).toContain("DBUCKS");
    expect(account.given).toEqual([]);
    account.answer("carried_out", true);
    const bought = await buying;
    expect(bought).toEqual({ ok: true, given: true, trade: { way: "bought", resource: "WOOD", units: 50, dbucks: 50, wallet: "070707" } });
    expect(account.given).toEqual(["50 WOOD"]);
    expect(store.data.shop.trades).toEqual({});
    expect(store.data.shop.owed).toEqual({});
    expect(stages.some((s) => s.startsWith("wallet: "))).toBe(true);
    expect(stages.at(-1)!.startsWith("shop: ")).toBe(true);
  });

  it("sold back is paid for once the wallet's material has landed, and not before", async () => {
    const { account, store, shop } = stand(2_000);
    const selling = shop.sellMaterial(connection, "STEEL");
    await soon(30);
    // the wallet was asked for the pack itself, and has been paid nothing
    expect(JSON.stringify(account.sends()[0])).toContain("STEEL");
    expect(account.given).toEqual([]);
    account.answer("carried_out", true);
    const sold = await selling;
    expect(sold).toEqual({ ok: true, given: true, trade: { way: "sold", resource: "STEEL", units: 50, dbucks: 45, wallet: "070707" } });
    expect(account.given).toEqual(["45 DBUCKS"]);
    expect(store.data.shop.owed).toEqual({});
  });

  it("is still traded when the player approves after the shop stopped waiting, and a transfer trades once", async () => {
    const { account, store, shop } = stand(40);
    const sold = await shop.sellMaterial(connection, "WOOD");
    expect(sold.ok).toBe(false);
    expect(Object.keys(store.data.shop.trades)).toHaveLength(1);

    await shop.sweep();
    expect(account.given).toEqual([]);

    account.answer("carried_out", true);
    await shop.sweep();
    expect(account.given).toEqual(["37 DBUCKS"]);
    expect(store.data.shop.trades).toEqual({});
    expect(store.data.shop.owed).toEqual({});

    // however often the shop looks, and whatever asks about that transfer again
    await shop.sweep();
    store.data.shop.trades["CONN1:1"] = { way: "sold", resource: "WOOD", units: 50, dbucks: 37, wallet: "070707", connection: "CONN1", seq: "1" };
    await shop.sweep();
    expect(account.given).toEqual(["37 DBUCKS"]);
    expect(store.data.shop.trades).toEqual({});
  });

  it("is traded at what it was asked at, whatever the shop charges since", async () => {
    const { account, shop } = stand(40);
    await shop.buyMaterial(connection, "WOOD");
    shop.world.materials!.price.WOOD = "500";
    shop.world.materials!.pack = 5;
    account.answer("carried_out", true);
    await shop.sweep();
    expect(account.given).toEqual(["50 WOOD"]);
  });

  it("trades nothing when the player declines, and is forgotten", async () => {
    const { account, store, shop } = stand(2_000);
    const selling = shop.sellMaterial(connection, "WOOD");
    await soon(20);
    account.answer("declined", false);
    expect(await selling).toEqual({ ok: false, why: "you declined it in your wallet" });
    expect(store.data.shop.trades).toEqual({});
    await shop.sweep();
    expect(account.given).toEqual([]);
  });

  it("is given on a later look when the account could not send it", async () => {
    const { account, store, shop } = stand(40);
    await shop.buyMaterial(connection, "STEEL");
    // one look tries a trade that landed, and then everything still owed: both tries are refused
    account.refuseGives = 2;
    account.answer("carried_out", true);
    await shop.sweep();
    // a refused give is known not to be sent: the trade stays owed, and is not marked as cut off
    expect(account.given).toEqual([]);
    expect(Object.values(store.data.shop.owed)).toEqual([{ way: "bought", resource: "STEEL", units: 50, dbucks: 60, wallet: "070707", connection: "CONN1" }]);
    expect(shop.cutOff()).toEqual([]);
    await shop.sweep();
    expect(account.given).toEqual(["50 STEEL"]);
    expect(store.data.shop.owed).toEqual({});
  });

  it("says the shop's half is on its way when the account cannot give it yet, and gives it at a later look", async () => {
    const { account, store, shop } = stand(2_000);
    const selling = shop.sellMaterial(connection, "WOOD");
    await soon(30);
    // the account has the wallet's wood, and cannot send to that wallet until the wallet has taken the transfer in
    account.refuseGives = 1;
    account.answer("carried_out", true);
    const sold = await selling;
    expect(sold).toMatchObject({ ok: true, given: false, trade: { way: "sold", resource: "WOOD", dbucks: 37 } });
    expect(account.given).toEqual([]);
    expect(Object.keys(store.data.shop.owed)).toHaveLength(1);
    await shop.sweep();
    expect(account.given).toEqual(["37 DBUCKS"]);
    expect(store.data.shop.owed).toEqual({});
  });

  it("gives what it owes over the connection the trade was asked on, not another of the same wallet's", async () => {
    const { account, store, shop } = stand(40);
    // the wallet has an older connection the account still counts as connected: nothing asked over it is answered
    const older = { id: "CONN0", connected: true, opponent: connection.opponent } as unknown as Connection;
    account.connections = async () => [older, connection];
    const overs: string[] = [];
    const give = account.give.bind(account);
    account.give = async (c: Connection, object: ObjectInfo, amount: string) => {
      overs.push(c.id);
      return give(c, object, amount);
    };
    await shop.sellMaterial(connection, "WOOD");
    account.refuseGives = 2;
    account.answer("carried_out", true);
    await shop.sweep();
    await shop.sweep();
    expect(account.given).toEqual(["37 DBUCKS"]);
    expect(overs).toEqual(["CONN1", "CONN1", "CONN1"]);
    expect(store.data.shop.owed).toEqual({});
  });

  it("cut off while the shop was giving is never given again by the shop, and is the publisher's to settle", async () => {
    const { account, store, shop } = stand(40);
    // as a server that stopped between sending the give and writing down that it was given
    store.data.shop.traded["09"] = "";
    store.data.shop.owed["09"] = { way: "sold", resource: "WOOD", units: 50, dbucks: 37, wallet: "070707", giving: true };
    const anew = new Shop(account as unknown as AppAccount, store, world(), { waitMs: 40, pollMs: 5 });
    await anew.sweep();
    await anew.settleTrades(connection);
    expect(account.given).toEqual([]);
    expect(anew.cutOff()).toEqual([{ transfer: "09", trade: { way: "sold", resource: "WOOD", units: 50, dbucks: 37, wallet: "070707" } }]);
    expect(shop.cutOff()).toHaveLength(1);
  });

  it("waits with a wallet's other payments: only a few are open in one wallet at once", async () => {
    const { account, store, shop } = stand(10);
    await shop.buySkin(connection, "magma");
    await shop.buyMaterial(connection, "WOOD");
    await shop.sellMaterial(connection, "WOOD");
    expect(Object.keys(store.data.shop.asked).length + Object.keys(store.data.shop.trades).length).toBe(3);
    expect(await shop.sellMaterial(connection, "STEEL")).toMatchObject({ ok: false });
    expect(await shop.buySkin(connection, "magma")).toMatchObject({ ok: false });
    expect(account.sends()).toHaveLength(3);
  });
});

describe("the shop's file", () => {
  const resources = ["WOOD", "STONE", "STEEL"];
  it("is refused when selling a pack back would pay as much as it costs, or nothing", () => {
    expect(() => checkMaterials({ pack: 50, price: { WOOD: "50" }, buyBackPercent: 75 }, resources)).not.toThrow();
    expect(() => checkMaterials({ pack: 50, price: { WOOD: "50" }, buyBackPercent: 100 }, resources)).toThrow("under 100");
    expect(() => checkMaterials({ pack: 50, price: { WOOD: "50" }, buyBackPercent: 0 }, resources)).toThrow("above 0");
    expect(() => checkMaterials({ pack: 50, price: { WOOD: "1" }, buyBackPercent: 75 }, resources)).toThrow("would pay nothing");
  });

  it("is refused when it prices what is no resource, or a pack of nothing", () => {
    expect(() => checkMaterials({ pack: 50, price: { GOLD: "50" }, buyBackPercent: 75 }, resources)).toThrow("GOLD, which is not a resource");
    expect(() => checkMaterials({ pack: 0, price: { WOOD: "50" }, buyBackPercent: 75 }, resources)).toThrow("a pack");
    expect(() => checkMaterials({ pack: 50, price: { WOOD: "0" }, buyBackPercent: 75 }, resources)).toThrow("above zero");
  });
});
