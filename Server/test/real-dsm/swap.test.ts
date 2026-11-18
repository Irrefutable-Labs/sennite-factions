/**
 * A swap both ways through the publisher's vault, on DSM with no game attached (spec 10.5).
 *
 * What can be swapped is read from the publisher's account's own vaults. A player's wallet is given
 * DSM's price for ERA in D-bucks and swaps at it; then the price of D-bucks in ERA, and swaps back.
 * Every balance here is read from the wallet's own account, and every reserve from the vault.
 *
 * Run with `npm run test:dsm` (see hosts.ts for what it needs).
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { Refused } from "../../src/publisher/refused.js";
import { leastOf, type SwapToken } from "../../src/publisher/swap.js";
import { until } from "./hosts.js";
import { player, stage, type Stage } from "./stage.js";
import { spendable } from "./wallets.js";

const DBUCKS = "DBUCKS";
let s: Stage;

const dbucksOf = (): Promise<bigint> => spendable(player(s, "alice").wallet, DBUCKS);
const eraOf = (): Promise<bigint> => spendable(player(s, "alice").wallet, "ERA");
const reserve = async (symbol: string): Promise<bigint> => {
  const v = (await s.app.vaults()).find((each) => each.active && [each.a.symbol, each.b.symbol].includes(DBUCKS));
  if (v === undefined) throw new Error("the publisher's vault is not open");
  return v.a.symbol === symbol ? v.a.reserve : v.b.reserve;
};
const token = async (symbol: string): Promise<SwapToken> => {
  const found = (await s.publisher.swap.board()).tokens.find((t) => t.symbol === symbol);
  if (found === undefined) throw new Error(`the board does not list ${symbol}`);
  return found;
};

beforeAll(async () => {
  s = await stage("swap", ["alice"]);
});

afterAll(async () => {
  await s?.stop();
});

describe("a swap through the publisher's vault", () => {
  it("what can be swapped is what the publisher's open vault holds: ERA and D-bucks, with its reserves and fee", async () => {
    const board = await s.publisher.swap.board();
    expect(board.tokens.map((t) => t.symbol).sort()).toEqual([DBUCKS, "ERA"]);
    expect(board.tokens.find((t) => t.symbol === DBUCKS)).toMatchObject({ name: s.world.shop.currency.name, decimals: 0 });
    expect(board.vaults).toHaveLength(1);
    expect(board.vaults[0]?.feeBps).toBe(s.world.shop.vault.feeBps);
    const [era, dbucks] = [await token("ERA"), await token(DBUCKS)];
    const asked = await s.publisher.swap.asked({ from: era.id, to: dbucks.id, amount: "2" });
    expect(asked).toMatchObject({ from: { symbol: "ERA" }, to: { symbol: DBUCKS }, amount: "2" });
    await expect(s.publisher.swap.asked({ from: era.id, to: era.id, amount: "2" })).rejects.toBeInstanceOf(Refused);
    await expect(s.publisher.swap.asked({ from: era.id, to: "STEEL", amount: "2" })).rejects.toBeInstanceOf(Refused);
    await expect(s.publisher.swap.asked({ from: dbucks.id, to: era.id, amount: "1.5" })).rejects.toBeInstanceOf(Refused);
  });

  it("ERA is swapped for D-bucks at DSM's price: the wallet gets no less than the least it was shown", async () => {
    const alice = player(s, "alice");
    const [era, dbucks] = [await token("ERA"), await token(DBUCKS)];
    const [eraBefore, dbucksBefore, vaultEra, vaultDbucks] = [await eraOf(), await dbucksOf(), await reserve("ERA"), await reserve(DBUCKS)];

    const price = await s.publisher.swap.price(alice.connection, era, dbucks, "2");
    expect(price.out).toBeGreaterThan(1000n);
    expect(price.hops).toBe(1);
    expect(price.amountOut).toBe(String(price.out));
    // a price moves nothing
    expect(await reserve(DBUCKS)).toBe(vaultDbucks);
    expect(await dbucksOf()).toBe(dbucksBefore);

    const traded = await s.publisher.swap.trade(alice.connection, era, dbucks, "2", leastOf(price.out));
    expect(traded.ok, traded.why).toBe(true);
    const got = await until("alice's wallet holds the D-bucks it swapped for", 240_000, async () => {
      const now = await dbucksOf();
      return now > dbucksBefore ? now - dbucksBefore : null;
    });
    expect(got).toBeGreaterThanOrEqual(leastOf(price.out));
    // the D-bucks came out of the vault and the ERA went into it
    expect(await reserve(DBUCKS)).toBe(vaultDbucks - got);
    expect((await reserve("ERA")) - vaultEra).toBe(eraBefore - (await eraOf()));
  });

  it("D-bucks are swapped back for ERA through the same vault", async () => {
    const alice = player(s, "alice");
    const [era, dbucks] = [await token("ERA"), await token(DBUCKS)];
    const [eraBefore, dbucksBefore, vaultEra, vaultDbucks] = [await eraOf(), await dbucksOf(), await reserve("ERA"), await reserve(DBUCKS)];
    expect(dbucksBefore).toBeGreaterThanOrEqual(500n);

    const price = await s.publisher.swap.price(alice.connection, dbucks, era, "500");
    expect(price.out).toBeGreaterThan(0n);
    expect(price.amountIn).toBe("500");

    const traded = await s.publisher.swap.trade(alice.connection, dbucks, era, "500", leastOf(price.out));
    expect(traded.ok, traded.why).toBe(true);
    const got = await until("alice's wallet holds the ERA it swapped for", 240_000, async () => {
      const now = await eraOf();
      return now > eraBefore ? now - eraBefore : null;
    });
    expect(got).toBeGreaterThanOrEqual(leastOf(price.out));
    expect(dbucksBefore - (await dbucksOf())).toBe(500n);
    expect(await reserve(DBUCKS)).toBe(vaultDbucks + 500n);
    expect(await reserve("ERA")).toBe(vaultEra - got);
  });

  it("a swap that would give less than its least is not carried out, and moves nothing", async () => {
    const alice = player(s, "alice");
    const [era, dbucks] = [await token("ERA"), await token(DBUCKS)];
    const [eraBefore, dbucksBefore, vaultEra, vaultDbucks] = [await eraOf(), await dbucksOf(), await reserve("ERA"), await reserve(DBUCKS)];
    const price = await s.publisher.swap.price(alice.connection, era, dbucks, "1");

    // ten times what the vault gives: no trade fills it
    const traded = await s.publisher.swap.trade(alice.connection, era, dbucks, "1", price.out * 10n);
    expect(traded.ok).toBe(false);
    expect(traded.why).not.toBe("");
    expect([await eraOf(), await dbucksOf(), await reserve("ERA"), await reserve(DBUCKS)]).toEqual([eraBefore, dbucksBefore, vaultEra, vaultDbucks]);
  });
});
