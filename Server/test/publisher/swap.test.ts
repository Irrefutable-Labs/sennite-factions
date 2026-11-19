/**
 * The swap's rules on their own, with no account and no storage node: what can be swapped is what
 * the open vaults hold, an amount is what the player typed or it is refused, and a trade takes a
 * little less than the price shown, never nothing.
 *
 * The price and the trade themselves are DSM's: test/real-dsm/swap.test.ts runs them on DSM.
 */
import { describe, expect, it } from "vitest";
import { Refused } from "../../src/publisher/refused.js";
import { amountOf, boardOf, leastOf, sidesOf, SLIP_BPS, type OpenVault } from "../../src/publisher/swap.js";

const ERA = "ERA0000000000000000000000000000000000000000000000000";
const DBUCKS = "DBK0000000000000000000000000000000000000000000000000";
const STEEL = "STL0000000000000000000000000000000000000000000000000";

const vault = (a: [string, string, string], b: [string, string, string], active = true, feeBps = 100): OpenVault => ({
  a: { object: a[0], symbol: a[1], display: a[2] },
  b: { object: b[0], symbol: b[1], display: b[2] },
  feeBps,
  active,
});

const describeObject = (object: string, symbol: string) => ({ name: symbol === "DBUCKS" ? "D-bucks" : symbol, decimals: object === ERA ? 2 : 0 });

describe("what can be swapped", () => {
  it("is every object in an open vault, once, and the open vaults with their reserves and fee", () => {
    const board = boardOf(
      [vault([ERA, "ERA", "10.00"], [DBUCKS, "DBUCKS", "10000"]), vault([ERA, "ERA", "4.00"], [STEEL, "STEEL", "900"], true, 30)],
      describeObject,
    );
    expect(board.tokens).toEqual([
      { id: ERA, symbol: "ERA", name: "ERA", decimals: 2 },
      { id: DBUCKS, symbol: "DBUCKS", name: "D-bucks", decimals: 0 },
      { id: STEEL, symbol: "STEEL", name: "STEEL", decimals: 0 },
    ]);
    expect(board.vaults).toEqual([
      { a: ERA, b: DBUCKS, reserveA: "10.00", reserveB: "10000", feeBps: 100 },
      { a: ERA, b: STEEL, reserveA: "4.00", reserveB: "900", feeBps: 30 },
    ]);
  });

  it("leaves a closed vault out, and what only a closed vault held", () => {
    const board = boardOf([vault([ERA, "ERA", "10.00"], [DBUCKS, "DBUCKS", "10000"]), vault([ERA, "ERA", "0.00"], [STEEL, "STEEL", "0"], false)], describeObject);
    expect(board.tokens.map((t) => t.symbol)).toEqual(["ERA", "DBUCKS"]);
    expect(board.vaults).toHaveLength(1);
  });

  it("is nothing when the publisher has no vault open", () => {
    expect(boardOf([], describeObject)).toEqual({ tokens: [], vaults: [] });
  });

  it("tells two objects of one symbol apart by their ids", () => {
    const other = "DBX0000000000000000000000000000000000000000000000000";
    const board = boardOf([vault([ERA, "ERA", "1.00"], [DBUCKS, "DBUCKS", "5"]), vault([ERA, "ERA", "1.00"], [other, "DBUCKS", "7"])], describeObject);
    expect(board.tokens.map((t) => t.id)).toEqual([ERA, DBUCKS, other]);
  });
});

describe("the two sides of a swap", () => {
  const board = boardOf([vault([ERA, "ERA", "10.00"], [DBUCKS, "DBUCKS", "10000"])], describeObject);

  it("are two objects the vaults hold, either way round", () => {
    expect(sidesOf(board, ERA, DBUCKS)).toMatchObject({ from: { symbol: "ERA" }, to: { symbol: "DBUCKS" } });
    expect(sidesOf(board, DBUCKS, ERA)).toMatchObject({ from: { symbol: "DBUCKS" }, to: { symbol: "ERA" } });
  });

  it("are refused when the publisher has no vault for one of them, or when they are one object", () => {
    expect(() => sidesOf(board, ERA, STEEL)).toThrow(Refused);
    expect(() => sidesOf(board, "ERA", DBUCKS)).toThrow(Refused);
    expect(() => sidesOf(board, undefined, DBUCKS)).toThrow(Refused);
    expect(() => sidesOf(board, ERA, ERA)).toThrow(Refused);
  });
});

describe("an amount to swap", () => {
  it("is what the player typed, less zeros that say nothing", () => {
    expect(amountOf("2", 2)).toBe("2");
    expect(amountOf("2.5", 2)).toBe("2.5");
    expect(amountOf("0.05", 2)).toBe("0.05");
    expect(amountOf("007", 0)).toBe("7");
    expect(amountOf("1200", 0)).toBe("1200");
  });

  it("is refused when it is not a number above zero", () => {
    for (const typed of ["", "abc", "-1", "1e3", "1,5", ".5", "5.", "0", "0.00", " 2", undefined, 2, null]) {
      expect(() => amountOf(typed, 2), String(typed)).toThrow(Refused);
    }
  });

  it("is refused, not rounded, when it has more digits after the point than the object has", () => {
    expect(() => amountOf("1.5", 0)).toThrow("whole units");
    expect(() => amountOf("1.234", 2)).toThrow("2 digits after the point");
    expect(amountOf("1.23", 2)).toBe("1.23");
  });
});

describe("the least a trade may give", () => {
  it("is the price shown less the slip, rounded down", () => {
    expect(SLIP_BPS).toBe(300);
    expect(leastOf(10_000n)).toBe(9_700n);
    expect(leastOf(1_634n)).toBe(1_584n);
    expect(leastOf(33n)).toBe(32n);
  });

  it("is never nothing, and a price that gives nothing is refused", () => {
    expect(leastOf(1n)).toBe(1n);
    expect(() => leastOf(0n)).toThrow(Refused);
    expect(() => leastOf(-5n)).toThrow(Refused);
  });
});
