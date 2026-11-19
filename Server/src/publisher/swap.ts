// Swapping one object for another through the publisher's vaults (spec 10.5).
//
// The publisher's account opens vaults: each holds reserves of two objects and
// trades one for the other at the price those reserves set, less its fee. A
// player's wallet can swap any object in those vaults for any other the vaults
// connect: ERA for D-bucks, D-bucks back for ERA, and whatever else the
// publisher opens a vault for.
//
// Nothing here sets a price or decides a trade. What can be swapped is read
// from the account's own vaults. A price is the wallet's answer to DSM's own
// quote through those vaults, and it is information, never a promise. A trade
// is the wallet's own swap, for no less than the least the player was shown;
// what the wallet holds afterwards is what it then proves.

import { ask, type AppAccount, type Connection, type ObjectInfo } from "dsm-app-client";
import { Refused } from "./refused.js";

/** An object the publisher's vaults trade. */
export interface SwapToken {
  /** The object's id (Base32 Crockford): what a swap names it by, since two issuers may pick one symbol. */
  id: string;
  symbol: string;
  /** What the game calls it: an object's own name, in the game's words where it has some. */
  name: string;
  /** How many digits an amount of it has after the point. */
  decimals: number;
}

/** One of the publisher's open vaults, as a screen shows it. */
export interface SwapVault {
  a: string;
  b: string;
  /** Each side's reserve, in the object's own units. */
  reserveA: string;
  reserveB: string;
  /** The vault's fee, in hundredths of a percent. */
  feeBps: number;
}

/** What can be swapped now: the objects in the publisher's open vaults, and the vaults. */
export interface SwapBoard {
  tokens: SwapToken[];
  vaults: SwapVault[];
}

/** What a swap is asked with: the two objects by id, and the amount paid in the first one's own units. */
export interface SwapAsk {
  from: string;
  to: string;
  amount: string;
}

/** A price: what `amountIn` of `from` would get in `to` through the vaults now. */
export interface SwapPrice {
  from: SwapToken;
  to: SwapToken;
  /** In each object's own units, as the account renders them. */
  amountIn: string;
  amountOut: string;
  /** What it would get, in the smallest unit of `to`. */
  out: bigint;
  /** How many vaults the trade goes through. */
  hops: number;
}

/** A vault as the board reads it: the part of the account's own record of a vault that says what it trades. */
export interface OpenVault {
  a: { object: string; symbol: string; display: string };
  b: { object: string; symbol: string; display: string };
  feeBps: number;
  active: boolean;
}

/** How much less than the price shown a trade may give and still be carried out, in hundredths of a percent. */
export const SLIP_BPS = 300;

/** How long a reading of the vaults is good for. */
const FRESH_MS = 10_000;

/**
 * The board of what can be swapped: every object in an open vault, once, in
 * the order the vaults name them, and the open vaults. A closed vault trades
 * nothing and is left out.
 */
export function boardOf(vaults: readonly OpenVault[], describe: (object: string, symbol: string) => { name: string; decimals: number }): SwapBoard {
  const tokens: SwapToken[] = [];
  const open: SwapVault[] = [];
  for (const vault of vaults) {
    if (!vault.active) continue;
    for (const side of [vault.a, vault.b]) {
      if (!tokens.some((t) => t.id === side.object)) tokens.push({ id: side.object, symbol: side.symbol, ...describe(side.object, side.symbol) });
    }
    open.push({ a: vault.a.object, b: vault.b.object, reserveA: vault.a.display, reserveB: vault.b.display, feeBps: vault.feeBps });
  }
  return { tokens, vaults: open };
}

/**
 * An amount a player typed, of an object with `decimals` digits after the
 * point: digits, at most that many after a point, and above zero. Refused
 * otherwise; nothing is rounded for the player.
 */
export function amountOf(typed: unknown, decimals: number): string {
  if (typeof typed !== "string" || !/^[0-9]{1,12}(\.[0-9]{1,18})?$/.test(typed)) throw new Refused("the amount to swap is not a number");
  const [whole = "", fraction = ""] = typed.split(".");
  if (fraction.length > decimals) {
    throw new Refused(decimals === 0 ? "that is swapped in whole units" : `that is swapped to ${decimals} digit${decimals === 1 ? "" : "s"} after the point`);
  }
  if (/^0*$/.test(whole) && /^0*$/.test(fraction)) throw new Refused("the amount to swap is not above zero");
  // as the player typed it, less zeros that say nothing
  const lead = whole.replace(/^0+(?=[0-9])/, "");
  return fraction.length > 0 ? `${lead}.${fraction}` : lead;
}

/** The two objects of a swap, which the board must both list, and which must differ. */
export function sidesOf(board: SwapBoard, from: unknown, to: unknown): { from: SwapToken; to: SwapToken } {
  const find = (id: unknown): SwapToken | undefined => board.tokens.find((t) => t.id === id);
  const [paid, got] = [find(from), find(to)];
  if (paid === undefined || got === undefined) throw new Refused("the publisher has no vault open for that");
  if (paid.id === got.id) throw new Refused("that is swapped for something else, not for itself");
  return { from: paid, to: got };
}

/**
 * The least a trade may give, in the smallest unit: the price the player was
 * shown, less SLIP_BPS of it. The vaults' price moves with every trade, so a
 * little less than shown is taken; much less is not, and never nothing.
 */
export function leastOf(shown: bigint): bigint {
  if (shown <= 0n) throw new Refused("the price shown gives nothing");
  const least = (shown * BigInt(10_000 - SLIP_BPS)) / 10_000n;
  return least > 0n ? least : 1n;
}

export class Swap {
  #board: { at: number; board: SwapBoard; objects: Map<string, ObjectInfo> } | undefined;

  constructor(
    private readonly app: AppAccount,
    /** The game's own currency: the name the game calls it by is its name here. */
    private readonly currency: { symbol: string; name: string },
  ) {}

  /** What can be swapped now, read from the account's own vaults. */
  async board(): Promise<SwapBoard> {
    return (await this.read()).board;
  }

  private async read(): Promise<{ board: SwapBoard; objects: Map<string, ObjectInfo> }> {
    if (this.#board !== undefined && Date.now() - this.#board.at < FRESH_MS) return this.#board;
    const vaults = await this.app.vaults();
    const objects = new Map<string, ObjectInfo>();
    for (const vault of vaults) {
      if (!vault.active) continue;
      for (const side of [vault.a, vault.b]) {
        if (!objects.has(side.object)) objects.set(side.object, await this.app.object(side.object));
      }
    }
    const board = boardOf(vaults, (object, symbol) => {
      const info = objects.get(object);
      if (info === undefined) throw new Error(`the vaults name ${symbol}, which the account does not know`);
      return { name: symbol === this.currency.symbol ? this.currency.name : info.name || symbol, decimals: info.decimals };
    });
    this.#board = { at: Date.now(), board, objects };
    return this.#board;
  }

  /** What a request asks for, checked against the vaults: the two objects and the amount, or Refused. */
  async asked(request: Record<string, unknown>): Promise<{ from: SwapToken; to: SwapToken; amount: string }> {
    const { board } = await this.read();
    const sides = sidesOf(board, request.from, request.to);
    return { ...sides, amount: amountOf(request.amount, sides.from.decimals) };
  }

  private async info(token: SwapToken): Promise<ObjectInfo> {
    const known = (await this.read()).objects.get(token.id);
    if (known === undefined) throw new Refused("the publisher has no vault open for that");
    return known;
  }

  /**
   * What `amount` of `from` would get in `to` through the publisher's vaults
   * now: the wallet's answer to DSM's quote. Refused, in DSM's words, when the
   * vaults have no route between the two or cannot fill it.
   */
  async price(connection: Connection, from: SwapToken, to: SwapToken, amount: string): Promise<SwapPrice> {
    const [paid, got] = [await this.info(from), await this.info(to)];
    const quoted = await this.app.wait(await this.app.request(connection, ask.price(paid, got, amount)));
    if (quoted.price === undefined) throw new Refused(quoted.reason || quoted.detail || "the wallet gave no price");
    if (quoted.price.amountOut <= 0n) throw new Refused(`${amount} ${from.name} gets nothing in ${to.name} now`);
    return {
      from,
      to,
      amountIn: await this.app.display(paid, quoted.price.amountIn),
      amountOut: await this.app.display(got, quoted.price.amountOut),
      out: quoted.price.amountOut,
      hops: quoted.price.hops,
    };
  }

  /**
   * The wallet swaps `amount` of `from` for no less than `atLeast` of `to`
   * (in the smallest unit of `to`). The wallet's answer is a notification;
   * what it holds afterwards is what it then proves.
   */
  async trade(
    connection: Connection,
    from: SwapToken,
    to: SwapToken,
    amount: string,
    atLeast: bigint,
    say: (stage: string, onWallet: boolean) => void = () => {},
  ): Promise<{ ok: boolean; why: string }> {
    const [paid, got] = [await this.info(from), await this.info(to)];
    const ref = await this.app.request(connection, ask.trade(paid, got, amount, atLeast));
    // no trade is in the grant: it waits in the wallet for the player
    const answered = await until(async () => {
      const r = await this.app.result(ref);
      if (r.answered && r.outcome !== "awaiting_approval") return r;
      say("approve the swap in your DSM wallet", true);
      return undefined;
    });
    // the reserves moved with the trade: the next reading of the vaults is a new one
    this.#board = undefined;
    if (answered === undefined) return { ok: false, why: "the wallet did not answer the swap" };
    return answered.outcome === "carried_out" ? { ok: true, why: "" } : { ok: false, why: answered.reason || answered.detail || `the wallet's answer is ${answered.outcome}` };
  }
}

/** `probe` until it gives something, for up to four minutes. */
async function until<T>(probe: () => Promise<T | undefined>): Promise<T | undefined> {
  const deadline = Date.now() + 240_000;
  for (;;) {
    const got = await probe();
    if (got !== undefined || Date.now() > deadline) return got;
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
}
