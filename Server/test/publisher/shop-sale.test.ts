/**
 * A sale, with a stand-in for the publisher's account: what the shop does with the wallet's answer to "send
 * me the price", whenever that answer comes. The payment itself is DSM's: test/real-dsm/shop.test.ts runs
 * it on DSM. Here the account's answers are set by the test, as a player approving early, late or not at all.
 * While the player approves, the item is issued and taken up by the wallet; only the give waits for the payment.
 */
import { describe, expect, it } from "vitest";
import type { AppAccount, Connection, ObjectInfo, RequestRef, Result } from "dsm-app-client";
import { plainly, Shop } from "../../src/publisher/shop.js";
import { Store } from "../../src/publisher/store.js";
import type { ShopWorld } from "../../src/publisher/world.js";

const world: ShopWorld = {
  currency: { symbol: "DBUCKS", name: "D-bucks", supply: "100000000" },
  vault: { era: "10", dbucks: "10000", feeBps: 100 },
  skins: [{ id: "magma", name: "Dekker Magma", price: "800" }],
  heroes: [{ id: "rampage", name: "Rampage", price: "1200" }],
  weapons: [{ id: "sniper", name: "Sniper", price: "900" }],
};

const connection = { id: "CONN1", connected: true, opponent: { signingKey: new Uint8Array([7, 7, 7]) } } as unknown as Connection;

/** The account as the shop uses it: requests, their results as the test sets them, and what was issued and given. */
class Account {
  seq = 0n;
  readonly asks: { ref: RequestRef; what: unknown }[] = [];
  readonly results = new Map<string, Result>();
  readonly issued: ObjectInfo[] = [];
  readonly given: string[] = [];

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
  async issue(options: { symbol: string; name: string }): Promise<ObjectInfo> {
    const info = { id: `OBJ${this.issued.length + 1}`, raw: new Uint8Array([this.issued.length + 1]), symbol: options.symbol, name: options.name, decimals: 0, kind: "unique" } as ObjectInfo;
    this.issued.push(info);
    return info;
  }
  /** What the account holds: here, what it issued. */
  async owned(): Promise<{ object: ObjectInfo; amount: bigint }[]> {
    return this.issued.map((object) => ({ object, amount: 1n }));
  }
  async connections(): Promise<Connection[]> {
    return [connection];
  }
  async object(id: string): Promise<ObjectInfo> {
    const found = this.issued.find((o) => o.id === id);
    if (found === undefined) throw new Error("no such object");
    return found;
  }
  async give(_c: Connection, object: ObjectInfo): Promise<void> {
    this.given.push(object.id);
  }

  /** The wallet's answer to the one payment asked for (the latest asked, when a sale was asked again). */
  answer(outcome: Result["outcome"], paid: boolean): void {
    const payment = this.asks.filter((a) => (a.what as { kind: string }).kind === "send").at(-1)!;
    const key = this.key(payment.ref);
    const was = this.results.get(key)!;
    this.results.set(key, { ...was, answered: outcome !== "awaiting_approval", outcome, fact: paid ? "received" : "none", ...(paid ? { received: new Uint8Array([9, 9]) } : {}) });
  }
}

const stand = (waitMs: number) => {
  const account = new Account();
  const store = new Store();
  const shop = new Shop(account as unknown as AppAccount, store, world, { waitMs, pollMs: 5 });
  return { account, store, shop };
};
const soon = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));
/** What the shop asked the wallet of one kind: "send" (the price) or "accept" (taking an item up). */
const asked = (account: Account, kind: "send" | "accept") => account.asks.filter((a) => (a.what as { kind: string }).kind === kind);

describe("a sale", () => {
  it("waits in the wallet for the player, and is carried out when the player approves", async () => {
    const { account, store, shop } = stand(2_000);
    const stages: string[] = [];
    const selling = shop.buyHero(connection, "Rampage", (stage, awaiting) => stages.push(`${awaiting ? "wallet" : "shop"}: ${stage}`));
    await soon(30);
    account.answer("awaiting_approval", false);
    await soon(30);
    // while the player approves, the hero is issued and the wallet takes it up; nothing is given before the payment
    expect(account.issued.map((o) => o.symbol)).toEqual(["HR000001"]);
    expect(asked(account, "accept")).toHaveLength(1);
    // the wallet answers in turn: the taking up is asked before the payment, so it is not held behind the player
    expect(account.asks.map((a) => (a.what as { kind: string }).kind)).toEqual(["accept", "send"]);
    expect(account.given).toEqual([]);
    expect(store.data.shop.prepared).toEqual({ OBJ1: { kind: "hero", itemId: "rampage", serial: 1, symbol: "HR000001" } });
    expect(store.data.shop.heroes).toEqual({});
    account.answer("carried_out", true);
    const sold = await selling;
    expect(sold).toMatchObject({ ok: true, hero: { heroId: "rampage", serial: 1 } });
    expect(account.issued.map((o) => o.symbol)).toEqual(["HR000001"]);
    expect(account.given).toEqual(["OBJ1"]);
    // once paid, only the give: the wallet is not asked to take the hero up a second time
    expect(asked(account, "accept")).toHaveLength(1);
    expect(store.data.shop.heroes).toEqual({ OBJ1: { heroId: "rampage", serial: 1, symbol: "HR000001" } });
    expect(store.data.shop.prepared).toEqual({});
    expect(store.data.shop.asked).toEqual({});
    expect(store.data.shop.pending).toEqual({});
    // the player is told it waits in their wallet, and after approving that the shop has it in hand
    expect(stages.some((s) => s.startsWith("wallet: "))).toBe(true);
    expect(stages.at(-1)!.startsWith("shop: ")).toBe(true);
  });

  it("is still carried out when the player approves after the shop stopped waiting", async () => {
    const { account, store, shop } = stand(40);
    const sold = await shop.buyHero(connection, "rampage");
    expect(sold.ok).toBe(false);
    expect(Object.keys(store.data.shop.asked)).toHaveLength(1);
    // the hero is ready in the wallet's hands, and not given
    expect(account.issued).toHaveLength(1);
    expect(account.given).toEqual([]);

    await shop.sweep();
    expect(account.issued).toHaveLength(1);
    expect(account.given).toEqual([]);

    account.answer("carried_out", true);
    await shop.sweep();
    expect(account.issued.map((o) => o.symbol)).toEqual(["HR000001"]);
    expect(account.given).toEqual(["OBJ1"]);
    expect(store.data.shop.heroes).toEqual({ OBJ1: { heroId: "rampage", serial: 1, symbol: "HR000001" } });
    expect(store.data.shop.asked).toEqual({});

    // a payment buys once, however often the shop looks
    await shop.sweep();
    expect(account.issued).toHaveLength(1);
    expect(account.given).toHaveLength(1);
  });

  it("is finished by a shop started anew, from the record", async () => {
    const { account, store, shop } = stand(40);
    await shop.buySkin(connection, "magma");
    account.answer("carried_out", true);
    const anew = new Shop(account as unknown as AppAccount, store, world, { waitMs: 40, pollMs: 5 });
    await anew.sweep();
    expect(account.given).toEqual(["OBJ1"]);
    expect(store.data.shop.skins).toEqual({ OBJ1: { skinId: "magma", serial: 1, symbol: "SK000001" } });
  });

  it("does not issue an item twice when the shop stopped between issuing it and writing that down", async () => {
    const { account, store, shop } = stand(40);
    // as a server that stopped there: the sale is pending with its symbol, the object is issued, and the record does not say so
    store.data.shop.serial = 1;
    store.data.shop.paid["0909"] = "";
    store.data.shop.pending["0909"] = { kind: "hero", itemId: "rampage", serial: 1, symbol: "HR000001", buyer: "070707", object: "" };
    await account.issue({ symbol: "HR000001", name: "Rampage #1" });
    await shop.sweep();
    expect(account.issued).toHaveLength(1);
    expect(account.given).toEqual(["OBJ1"]);
    expect(store.data.shop.pending).toEqual({});
  });

  it("asks one wallet for only a few payments at once", async () => {
    const { account, store, shop } = stand(10);
    for (let i = 0; i < 3; i += 1) await shop.buyHero(connection, "rampage");
    expect(Object.keys(store.data.shop.asked)).toHaveLength(3);
    const fourth = await shop.buyHero(connection, "rampage");
    expect(fourth).toMatchObject({ ok: false });
    expect(asked(account, "send")).toHaveLength(3);
  });

  it("keeps what it asked when the account cannot be asked about its connection", async () => {
    const { account, store, shop } = stand(10);
    await shop.buyHero(connection, "rampage");
    const real = account.connection.bind(account);
    account.connection = async () => {
      throw new Error("the account did not answer");
    };
    await shop.sweep();
    expect(Object.keys(store.data.shop.asked)).toHaveLength(1);
    // and a payment that landed is carried out even if the wallet's answer calls it failed
    account.connection = real;
    account.answer("failed", true);
    await shop.sweep();
    expect(account.given).toEqual(["OBJ1"]);
  });

  it("keeps an asking written before its time was kept", async () => {
    const { account, store, shop } = stand(10);
    await shop.buyHero(connection, "rampage");
    for (const asked of Object.values(store.data.shop.asked)) delete asked.at;
    await shop.sweep();
    expect(Object.keys(store.data.shop.asked)).toHaveLength(1);
    account.answer("carried_out", true);
    await shop.sweep();
    expect(account.given).toEqual(["OBJ1"]);
  });

  it("buys nothing when the player declines, and is forgotten", async () => {
    const { account, store, shop } = stand(2_000);
    const selling = shop.buyWeapon(connection, "sniper");
    await soon(20);
    account.answer("declined", false);
    const sold = await selling;
    expect(sold).toEqual({ ok: false, why: "you declined it in your wallet" });
    expect(store.data.shop.asked).toEqual({});
    await shop.sweep();
    // the gun issued while the player looked stays the account's, given to no one, and sold to nobody
    expect(account.given).toEqual([]);
    expect(store.data.shop.weapons).toEqual({});
    expect(Object.values(store.data.shop.prepared)).toEqual([{ kind: "weapon", itemId: "sniper", serial: 1, symbol: "WP000001" }]);
  });

  it("sells an item prepared for a payment that was declined to the next buyer of it, and issues no other", async () => {
    const { account, store, shop } = stand(2_000);
    const first = shop.buyWeapon(connection, "sniper");
    await soon(20);
    account.answer("declined", false);
    await first;
    const second = shop.buyWeapon(connection, "sniper");
    await soon(20);
    account.answer("carried_out", true);
    expect(await second).toMatchObject({ ok: true, weapon: { weaponId: "sniper", serial: 1 } });
    expect(account.issued.map((o) => o.symbol)).toEqual(["WP000001"]);
    expect(account.given).toEqual(["OBJ1"]);
    expect(store.data.shop.prepared).toEqual({});
    expect(store.data.shop.weapons).toEqual({ OBJ1: { weaponId: "sniper", serial: 1, symbol: "WP000001" } });
  });

  it("says in plain words that the wallet holds too little", () => {
    const failed = { outcome: "failed", reason: "wallet.send: local state update failed: Invalid operation: advance: balance underflow on debit (insufficient funds)", detail: "" };
    expect(plainly(failed, "the 1200 D-bucks the hero costs")).toBe("your wallet does not hold the 1200 D-bucks the hero costs");
    expect(plainly({ outcome: "failed", reason: "", detail: "the relay did not answer" }, "x")).toBe("the relay did not answer");
  });
});
