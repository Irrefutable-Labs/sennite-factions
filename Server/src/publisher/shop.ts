// D-bucks, the publisher's vault and the shop (spec 10.5).
//
// D-bucks are an object the publisher's account issues with a fixed maximum
// supply. Players get them from the publisher's SoFi vault, which sells them
// for ERA at a price its reserves set and a fee the publisher chooses. The
// shop sells skins for D-bucks: the wallet sends the price, the publisher's
// account accepts it, and only then gives the skin, an object of which one
// exists, to the wallet. The skin is issued, and taken up by the wallet, while
// the player approves the payment: once it lands only the give is left. From there the skin is the player's:
// they can send it to anyone, and the game has no say. A hero is sold the
// same way: an object of which one exists, the wallet's to keep or send.
//
// The shop records what it issued and which payment bought it. It never
// records who owns a skin: that is what a wallet proves.

import { ask, type AppAccount, type Connection, type Grant, type ObjectInfo, type RequestRef } from "dsm-app-client";
import { hex } from "../faction/canonical.js";
import { Refused } from "./refused.js";
import type { AskedTrade, MaterialTrade, Store } from "./store.js";
import type { HeroForSale, MaterialPack, ShopWorld, SkinForSale, WeaponForSale } from "./world.js";

const VAULT_LABEL = "sennite d-bucks";

/** A skin a wallet proved it owns. */
export interface OwnedSkin {
  skinId: string;
  name: string;
  /** The object's id, as people read it. */
  object: string;
  serial: number;
}

/** A hero a wallet proved it owns. */
export interface OwnedHero {
  heroId: string;
  name: string;
  /** The object's id, as people read it. */
  object: string;
  serial: number;
}

/** A gun a wallet proved it owns. */
export interface OwnedWeapon {
  weaponId: string;
  name: string;
  /** The object's id, as people read it. */
  object: string;
  serial: number;
}

/** What a wallet proved it holds that the shop cares about. */
export interface Purse {
  /** D-bucks, in whole units. */
  dbucks: number;
  /** ERA, as the account renders it. */
  era: string;
  skins: OwnedSkin[];
  heroes: OwnedHero[];
  weapons: OwnedWeapon[];
}

/**
 * Why the wallet did not carry out what it was asked, for the player to read: the two answers a player brings
 * about themselves in plain words, anything else as the wallet put it.
 */
export function plainly(result: { outcome: string; reason: string; detail: string }, what: string): string {
  const said = result.reason || result.detail;
  if (result.outcome === "declined") return "you declined it in your wallet";
  if (/insufficient funds|balance underflow/i.test(said)) return `your wallet does not hold ${what}`;
  return said || `the wallet's answer is ${result.outcome}`;
}

/** What a sale is waiting on, said for the game to tell the player: `onWallet` when it is the player's approval in the wallet. */
export type Say = (stage: string, onWallet: boolean) => void;

/** How many payments the shop may have open in one wallet, waiting for the player, at once: those asked within COUNTED_FOR_MS. */
const MAX_OPEN_PER_WALLET = 3;
const COUNTED_FOR_MS = 60 * 60_000;
/** And how many it may have open in all, however long ago they were asked. */
const MAX_OPEN_IN_ALL_PER_WALLET = 12;
/** An asking nobody answered in this long is forgotten: thirty days. One approved after that buys nothing. */
const FORGOTTEN_AFTER_MS = 30 * 24 * 60 * 60_000;

/** How long the shop waits for a wallet to take up an object it is about to be given, at one look. */
const ACCEPT_WAIT_MS = 30_000;

/** The first letters of the symbol an item sold is issued under, by its kind. */
const PREFIX = { skin: "SK", hero: "HR", weapon: "WP" } as const;

export class Shop {
  /** The payments whose sale is being finished now, so that two looks at one sale do not both issue it. */
  readonly #finishing = new Set<string>();
  #sweeping = false;
  /** How many sales to each wallet are at the point of asking it, between taking a place and recording what was asked. */
  readonly #asking = new Map<string, number>();
  /** "connection:seq" of a payment asked -> the item being issued and taken up for it while the player approves. */
  readonly #preparing = new Map<string, Promise<ObjectInfo | undefined>>();
  /** The prepared items an open sale has in hand: no other sale takes them. */
  readonly #reserved = new Set<string>();
  /** "wallet:object" of each prepared item a wallet has taken up: its give needs no second asking. */
  readonly #takenUp = new Set<string>();
  /** "wallet:object" of each prepared item a wallet was asked to take up and has not answered yet: the give waits on that asking, not a new one. */
  readonly #takingUp = new Map<string, RequestRef>();

  /**
   * `timing`: how long a sale's own request to the server goes on waiting for the wallet before it answers the
   * game that it still waits (the sale itself has no end but the player's answer), and how often it looks.
   */
  constructor(
    private readonly app: AppAccount,
    private readonly store: Store,
    readonly world: ShopWorld,
    private readonly timing: { waitMs: number; pollMs: number } = { waitMs: 30 * 60_000, pollMs: 700 },
  ) {}

  get symbol(): string {
    return this.world.currency.symbol;
  }

  /** Issue D-bucks if they are not issued yet, and open the vault that sells them for ERA if it is not open. */
  async open(): Promise<void> {
    const owned = await this.app.owned();
    if (!owned.some((o) => o.object.symbol === this.symbol)) {
      await this.app.issue({ symbol: this.symbol, name: this.world.currency.name, supply: this.world.currency.supply });
    }
    if (!(await this.app.vaults()).some((v) => v.label === VAULT_LABEL && v.active)) {
      await this.app.openVault({
        a: "ERA",
        reserveA: this.world.vault.era,
        b: this.symbol,
        reserveB: this.world.vault.dbucks,
        feeBps: this.world.vault.feeBps,
        label: VAULT_LABEL,
      });
    }
  }

  /**
   * What the shop asks a wallet to grant for paying and trading: nothing. Every
   * payment of D-bucks and every trade of ERA is outside the grant, so each one
   * waits in the player's wallet until the player approves it there. The owner,
   * 2026-11-19, of a grant that let the game take up to 5,000 D-bucks a request
   * without asking: "I didn't come up with that fucking stupid idea to have that
   * on in this game, the 5,000 limit. No, because it's real money. Yeah, you got
   * to go to the wallet".
   */
  grants(): Grant[] {
    return [];
  }

  // The shop's ids are lower case. A game may send one in another case: Unreal keeps a name in the case it first
  // met it in, so its hero "rampage" can come as "Rampage". The id is the same id whatever its case.
  skin(skinId: string): SkinForSale {
    const found = this.world.skins.find((k) => k.id === skinId.toLowerCase());
    if (found === undefined) throw new Refused(`the shop has no skin ${skinId}`);
    return found;
  }

  hero(heroId: string): HeroForSale {
    const found = this.world.heroes.find((h) => h.id === heroId.toLowerCase());
    if (found === undefined) throw new Refused(`the shop has no hero ${heroId}`);
    return found;
  }

  weapon(weaponId: string): WeaponForSale {
    const found = this.world.weapons.find((w) => w.id === weaponId.toLowerCase());
    if (found === undefined) throw new Refused(`the shop has no gun ${weaponId}`);
    return found;
  }

  /**
   * The materials the shop trades, a pack of each: what it holds, what it costs, and what selling it back
   * pays. `named` gives a resource's name as the publisher's account issued it.
   */
  packs(named: (symbol: string) => string = (symbol) => symbol): MaterialPack[] {
    const materials = this.world.materials;
    if (materials === undefined) return [];
    return Object.entries(materials.price).map(([symbol, price]) => ({
      symbol,
      name: named(symbol),
      units: materials.pack,
      price: Number(price),
      buyBack: Math.floor((Number(price) * materials.buyBackPercent) / 100),
    }));
  }

  /** A pack of the material `symbol`, as the shop trades it now. A game may send the symbol in another case. */
  pack(symbol: string): MaterialPack {
    const found = this.packs().find((p) => p.symbol === symbol.toUpperCase());
    if (found === undefined) throw new Refused(`the shop trades no ${symbol}`);
    return found;
  }

  /**
   * Sell a wallet a pack of a material: the wallet sends the pack's price in D-bucks, and once the
   * publisher's account has accepted that transfer, and only then, the shop gives it the pack.
   */
  async buyMaterial(connection: Connection, symbol: string, say?: Say): Promise<{ ok: true; trade: MaterialTrade; given: boolean } | { ok: false; why: string }> {
    const pack = this.pack(symbol);
    return this.trade(connection, { way: "bought", resource: pack.symbol, units: pack.units, dbucks: pack.price, wallet: hex(connection.opponent.signingKey) }, say);
  }

  /**
   * Buy a pack of a material back from a wallet: the wallet sends the pack, and once the publisher's
   * account has accepted that transfer, and only then, the shop gives it what a pack sold back pays. The
   * shop is the only buyer the game has for a material (owner, 2026-11-22).
   */
  async sellMaterial(connection: Connection, symbol: string, say?: Say): Promise<{ ok: true; trade: MaterialTrade; given: boolean } | { ok: false; why: string }> {
    const pack = this.pack(symbol);
    return this.trade(connection, { way: "sold", resource: pack.symbol, units: pack.units, dbucks: pack.buyBack, wallet: hex(connection.opponent.signingKey) }, say);
  }

  /** What a material trade asks the wallet to send, and what the shop then owes it. */
  private halves(trade: MaterialTrade): { sent: { object: string; amount: number }; owed: { object: string; amount: number } } {
    const [material, dbucks] = [{ object: trade.resource, amount: trade.units }, { object: this.symbol, amount: trade.dbucks }];
    return trade.way === "bought" ? { sent: dbucks, owed: material } : { sent: material, owed: dbucks };
  }

  /**
   * A material trade, as a sale is made: the wallet is asked to send its half, which is in no grant and
   * waits in the wallet for the player; the trade is recorded with what it was asked at before the shop
   * waits; and the shop gives its half only once the account reports the wallet's transfer received. A
   * trade the player approves after the shop stopped waiting is carried out by `sweep`.
   *
   * `given` says whether the shop's half is given already. The account can give a wallet something only
   * once that wallet has finished taking in what it last sent, which it does when it is next open: a
   * trade whose first half landed and whose second cannot be given yet is owed, and `sweep` gives it.
   */
  private async trade(connection: Connection, trade: MaterialTrade, say: Say = () => {}): Promise<{ ok: true; trade: MaterialTrade; given: boolean } | { ok: false; why: string }> {
    await this.settleTrades(connection);
    const record = this.store.data.shop;
    const lately = Date.now() - COUNTED_FOR_MS;
    const theirs = [...Object.values(record.asked).filter((a) => a.buyer === trade.wallet), ...Object.values(record.trades).filter((t) => t.wallet === trade.wallet)];
    const open = theirs.filter((a) => (a.at ?? Date.now()) > lately).length;
    if (open + (this.#asking.get(trade.wallet) ?? 0) >= MAX_OPEN_PER_WALLET || theirs.length >= MAX_OPEN_IN_ALL_PER_WALLET) {
      return { ok: false, why: "payments already wait in your DSM wallet: approve or decline them there first" };
    }
    const { sent } = this.halves(trade);
    const what = trade.way === "bought" ? `${trade.units} ${trade.resource} for ${trade.dbucks} D-bucks` : `${trade.units} ${trade.resource} sold for ${trade.dbucks} D-bucks`;
    // what the wallet shows its player under the amount it is asked to send: what it gets for it
    const memo = trade.way === "bought" ? `${trade.units} ${trade.resource} from the shop` : `${trade.dbucks} D-bucks from the shop`;
    this.#asking.set(trade.wallet, (this.#asking.get(trade.wallet) ?? 0) + 1);
    let ref: RequestRef;
    let key: string;
    try {
      ref = await this.app.request(connection, ask.send(sent.object, String(sent.amount), memo));
      key = `${ref.connection}:${ref.seq}`;
      record.trades[key] = { ...trade, connection: ref.connection, seq: String(ref.seq), at: Date.now() };
      this.store.save();
    } finally {
      const left = (this.#asking.get(trade.wallet) ?? 1) - 1;
      if (left > 0) this.#asking.set(trade.wallet, left);
      else this.#asking.delete(trade.wallet);
    }

    const deadline = Date.now() + this.timing.waitMs;
    for (;;) {
      const result = await this.app.result(ref);
      if (result.fact === "received" && result.received !== undefined) {
        say(trade.way === "bought" ? `paid: the shop is giving you the ${trade.resource}` : `the shop has your ${trade.resource}: it is paying you`, false);
        const done = await this.landed(connection, key, hex(result.received));
        return done === "none" ? { ok: false, why: "that transfer has already been traded on" } : { ok: true, trade, given: done === "given" };
      }
      if (result.outcome === "declined" || result.outcome === "failed") {
        delete record.trades[key];
        this.store.save();
        return { ok: false, why: plainly(result, `the ${sent.amount} ${sent.object === this.symbol ? "D-bucks" : sent.object} to send`) };
      }
      if (result.answered && result.outcome === "carried_out") say("sent: the shop is taking it in", false);
      else say(`approve sending ${what} in your DSM wallet`, true);
      if (Date.now() > deadline) {
        return { ok: false, why: `${what} still waits in your DSM wallet: approve it there and it is carried out` };
      }
      await new Promise((resolve) => setTimeout(resolve, this.timing.pollMs));
    }
  }

  /**
   * The wallet's half of the trade asked under `key` landed as `transfer`: record what the shop owes and
   * give it. `none` when that asking is no longer open, or the transfer has begun a trade already; `owed`
   * when what the shop owes could not be given yet, and is given at a later look.
   */
  private async landed(connection: Connection, key: string, transfer: string): Promise<"given" | "owed" | "none"> {
    const record = this.store.data.shop;
    const asked: AskedTrade | undefined = record.trades[key];
    if (asked === undefined || Object.hasOwn(record.traded, transfer)) {
      delete record.trades[key];
      this.store.save();
      return "none";
    }
    if (hex(connection.opponent.signingKey) !== asked.wallet) throw new Error("the connection is not the wallet that was asked to trade");
    // The asking is closed, the transfer marked used and what is owed recorded, before anything is awaited.
    delete record.trades[key];
    record.traded[transfer] = "";
    record.owed[transfer] = { way: asked.way, resource: asked.resource, units: asked.units, dbucks: asked.dbucks, wallet: asked.wallet, connection: asked.connection };
    this.store.save();
    try {
      await this.pay(connection, transfer);
    } catch (e) {
      console.error(`[publisher] what a material trade owes is not given yet (${asked.resource} ${asked.way}): ${e instanceof Error ? e.message : String(e)}`);
    }
    return Object.hasOwn(record.owed, transfer) ? "owed" : "given";
  }

  /**
   * Give a wallet what a material trade owes it, once. That the shop is giving is written down before it
   * gives; a give the account refused is known not to be sent, and is tried again; a trade found already
   * marked as being given is left for the publisher (OwedTrade).
   */
  private async pay(connection: Connection, transfer: string): Promise<void> {
    const record = this.store.data.shop;
    const trade = record.owed[transfer];
    if (trade === undefined) throw new Error(`no trade is owed for transfer ${transfer}`);
    if (hex(connection.opponent.signingKey) !== trade.wallet) throw new Error("the connection is not the wallet the trade is owed to");
    if (trade.giving || this.#finishing.has(transfer)) return;
    this.#finishing.add(transfer);
    try {
      const { owed } = this.halves(trade);
      const info = await this.app.object(owed.object);
      // the wallet takes the object up if it does not hold it yet; one that does not answer soon is asked again at the next look
      await this.app.wait(await this.app.request(connection, ask.accept(info)), { until: "answered", timeoutMs: ACCEPT_WAIT_MS });
      trade.giving = true;
      this.store.save();
      try {
        await this.app.give(connection, info, String(owed.amount));
      } catch (e) {
        delete trade.giving;
        this.store.save();
        throw e;
      }
      delete record.owed[transfer];
      this.store.save();
    } finally {
      this.#finishing.delete(transfer);
    }
  }

  /** Give this wallet what every material trade of its own still owes it. */
  async settleTrades(connection: Connection): Promise<void> {
    const wallet = hex(connection.opponent.signingKey);
    for (const [transfer, trade] of Object.entries(this.store.data.shop.owed)) {
      if (trade.wallet === wallet) await this.pay(connection, transfer);
    }
  }

  /** The material trades cut off while the shop was giving: each is the publisher's to settle. */
  cutOff(): { transfer: string; trade: MaterialTrade }[] {
    return Object.entries(this.store.data.shop.owed)
      .filter(([, trade]) => trade.giving)
      .map(([transfer, { way, resource, units, dbucks, wallet }]) => ({ transfer, trade: { way, resource, units, dbucks, wallet } }));
  }

  /** What the vault would give in D-bucks for `era` now: information from the vault's public state, never a fact. */
  async price(connection: Connection, era: string): Promise<number> {
    const quoted = await this.app.wait(await this.app.request(connection, ask.price("ERA", this.symbol, era)));
    if (quoted.price === undefined) throw new Refused(quoted.reason || quoted.detail || "the wallet gave no price");
    return Number(quoted.price.amountOut);
  }

  /**
   * The wallet trades `era` for D-bucks through the publisher's vault, taking
   * no less than `atLeast`. The wallet's answer is a notification; what it
   * holds afterwards is what it then proves.
   */
  async buyDbucks(connection: Connection, era: string, atLeast: number, say: Say = () => {}): Promise<{ ok: boolean; why: string }> {
    const ref = await this.app.request(connection, ask.trade("ERA", this.symbol, era, String(atLeast)));
    // no trade is in the grant: it waits in the wallet for the player
    const answered = await until(async () => {
      const r = await this.app.result(ref);
      if (r.answered && r.outcome !== "awaiting_approval") return r;
      say("approve the trade of ERA for D-bucks in your DSM wallet", true);
      return undefined;
    });
    if (answered === undefined) return { ok: false, why: "the wallet did not answer the trade" };
    return answered.outcome === "carried_out" ? { ok: true, why: "" } : { ok: false, why: plainly(answered, `the ${era} ERA to trade`) };
  }

  /**
   * Sell a skin: the wallet sends the price in D-bucks; once the publisher's
   * account has accepted that transfer, and only then, the shop issues the
   * skin and gives it to the wallet. A payment buys one skin, once.
   */
  async buySkin(connection: Connection, skinId: string, say?: Say): Promise<{ ok: true; skin: OwnedSkin } | { ok: false; why: string }> {
    const skin = this.skin(skinId);
    const sold = await this.sell(connection, skin, "skin", say);
    return sold.ok ? { ok: true, skin: { skinId: skin.id, name: skin.name, object: sold.object, serial: sold.serial } } : sold;
  }

  /** Sell a hero, as a skin is sold: paid for first, then issued, one of it, and given. */
  async buyHero(connection: Connection, heroId: string, say?: Say): Promise<{ ok: true; hero: OwnedHero } | { ok: false; why: string }> {
    const hero = this.hero(heroId);
    const sold = await this.sell(connection, hero, "hero", say);
    return sold.ok ? { ok: true, hero: { heroId: hero.id, name: hero.name, object: sold.object, serial: sold.serial } } : sold;
  }

  /** Sell a gun, as a hero is sold: paid for first, then issued, one of it, and given. */
  async buyWeapon(connection: Connection, weaponId: string, say?: Say): Promise<{ ok: true; weapon: OwnedWeapon } | { ok: false; why: string }> {
    const weapon = this.weapon(weaponId);
    const sold = await this.sell(connection, weapon, "weapon", say);
    return sold.ok ? { ok: true, weapon: { weaponId: weapon.id, name: weapon.name, object: sold.object, serial: sold.serial } } : sold;
  }

  /**
   * The wallet is asked to send the item's price in D-bucks. No payment is in
   * the grant, so the request waits in the wallet until the player approves or
   * declines it there. Once the publisher's account has accepted that transfer,
   * and only then, the shop issues the item, an object of which one exists,
   * records it, and gives it to the wallet. A payment buys one item, once.
   *
   * The account reports the fact "received" only for a transfer that landed
   * paying what was asked: this object, this amount (dsm-app-host's
   * landed_payment). An answer from the wallet saying it paid grants nothing.
   *
   * What was asked is recorded, with its buyer and what it is for, before the
   * shop waits; and the shop does not stop honouring it when it stops waiting.
   * A player may approve it a minute later or a day later, with the game closed
   * or the server started anew: `sweep` finds the payment landed and carries
   * the sale out. Only a decline, a failure, the wallet ending the connection or
   * thirty days with no answer ends it. The owner, 2026-11-19, of a shop that gave up after four minutes
   * and let a payment approved later buy nothing: "The late approval: the shop
   * waits for 4 minutes. That's no good, and you need to fix that."
   *
   * A sale paid for is recorded as pending before anything is issued, and
   * stays pending until the item is given: one cut off part way (the server
   * stopped, the wallet did not take the item up) is finished by `sweep` too.
   *
   * `say` is told what the sale is waiting on, for the game to tell the player:
   * the wallet (the player's approval) or the shop (its own work).
   */
  private async sell(
    connection: Connection,
    item: { id: string; name: string; price: string },
    kind: "skin" | "hero" | "weapon",
    say: Say = () => {},
  ): Promise<{ ok: true; object: string; serial: number } | { ok: false; why: string }> {
    await this.settle(connection);
    // What is asked stays open until the player answers it, so a wallet may have only a few open at once: what it
    // is asked for cannot be piled up without end by a game asking again and again.
    // Only what was asked lately counts: an asking the wallet never shows its player must not shut that wallet
    // out of the shop for good. The place is taken before anything is awaited, so two sales begun together
    // cannot both take the last one.
    const buyer = hex(connection.opponent.signingKey);
    const lately = Date.now() - COUNTED_FOR_MS;
    const theirs = [...Object.values(this.store.data.shop.asked).filter((a) => a.buyer === buyer), ...Object.values(this.store.data.shop.trades).filter((t) => t.wallet === buyer)];
    const open = theirs.filter((a) => (a.at ?? Date.now()) > lately).length;
    // and, however old they are, a wallet has only so many open in all: they do not pile up over the days
    if (open + (this.#asking.get(buyer) ?? 0) >= MAX_OPEN_PER_WALLET || theirs.length >= MAX_OPEN_IN_ALL_PER_WALLET) {
      return { ok: false, why: "payments already wait in your DSM wallet: approve or decline them there first" };
    }
    this.#asking.set(buyer, (this.#asking.get(buyer) ?? 0) + 1);
    let ref: RequestRef;
    let key: string;
    let staged: ObjectInfo | undefined;
    try {
      // The item first, so that the wallet takes it up before it puts the payment before the player: once the payment
      // lands only the give is left (the owner, 2026-11-25: "Why does the store take so long? It needs to be faster.").
      staged = await this.stage(connection, kind, item.id, item.name);
      ref = await this.app.request(connection, ask.send(this.symbol, item.price, `${kind}: ${item.name}`));
      key = `${ref.connection}:${ref.seq}`;
      this.store.data.shop.asked[key] = { connection: ref.connection, seq: String(ref.seq), kind, itemId: item.id, buyer, at: Date.now() };
      this.store.save();
      if (staged !== undefined) this.#preparing.set(key, this.prepared(connection, key, staged));
    } catch (e) {
      // no payment was asked: the item staged for it is left for the next sale of it
      if (staged !== undefined) this.#reserved.delete(staged.id);
      throw e;
    } finally {
      const left = (this.#asking.get(buyer) ?? 1) - 1;
      if (left > 0) this.#asking.set(buyer, left);
      else this.#asking.delete(buyer);
    }

    const deadline = Date.now() + this.timing.waitMs;
    for (;;) {
      const result = await this.app.result(ref);
      if (result.fact === "received" && result.received !== undefined) {
        say(`paid: the shop is issuing your ${kind}`, false);
        const sold = await this.paid(connection, key, hex(result.received));
        return sold ?? { ok: false, why: `that payment has already bought a ${kind}` };
      }
      if (result.outcome === "declined" || result.outcome === "failed") {
        delete this.store.data.shop.asked[key];
        this.store.save();
        this.unprepare(key);
        return { ok: false, why: plainly(result, `the ${item.price} D-bucks the ${kind} costs`) };
      }
      if (result.answered && result.outcome === "carried_out") say("paid: the shop is taking the payment in", false);
      else say(`approve the payment for the ${kind} in your DSM wallet`, true);
      if (Date.now() > deadline) {
        // Not an end: the request still waits in the wallet, and the sale is carried out when it is approved.
        return { ok: false, why: `the payment for the ${kind} still waits in your DSM wallet: approve it there and the ${kind} is yours` };
      }
      await new Promise((resolve) => setTimeout(resolve, this.timing.pollMs));
    }
  }

  /**
   * A payment landed for what was asked under `key`: record the sale as pending
   * and finish it. Undefined when that asking is no longer open (another look
   * at it got here first) or the payment has bought something already.
   */
  private async paid(connection: Connection, key: string, payment: string): Promise<{ ok: true; object: string; serial: number } | undefined> {
    const record = this.store.data.shop;
    // the item issued for this asking while the player approved, if it is ready or nearly so
    const preparing = this.#preparing.get(key);
    const ready = preparing !== undefined ? await preparing : undefined;
    const asked = record.asked[key];
    if (asked === undefined || Object.hasOwn(record.paid, payment)) {
      delete record.asked[key];
      this.store.save();
      return undefined;
    }
    // the item goes to the wallet that was asked and paid: never to another, whatever connection is in hand
    if (hex(connection.opponent.signingKey) !== asked.buyer) throw new Error("the connection is not the wallet that was asked to pay");
    // The asking is closed, the item taken, the payment marked used and the sale recorded as pending, before anything is
    // awaited. The item is the one prepared for this asking; else one prepared for the same item that no open sale holds
    // (its payment was declined, or the server started anew); else a new one is issued when the sale is finished.
    delete record.asked[key];
    this.#preparing.delete(key);
    const held = (id: string): boolean => {
      const p = record.prepared[id];
      return p !== undefined && p.kind === asked.kind && p.itemId === asked.itemId;
    };
    const object =
      ready !== undefined && held(ready.id) ? ready.id : Object.keys(record.prepared).find((id) => held(id) && !this.#reserved.has(id));
    let serial: number;
    let symbol: string;
    if (object !== undefined) {
      ({ serial, symbol } = record.prepared[object]!);
      delete record.prepared[object];
      this.#reserved.delete(object);
      this.record(asked.kind, object, asked.itemId, serial, symbol);
      record.paid[payment] = object;
    } else {
      serial = record.serial += 1;
      symbol = `${PREFIX[asked.kind]}${String(serial).padStart(6, "0")}`;
      record.paid[payment] = "";
    }
    record.pending[payment] = { kind: asked.kind, itemId: asked.itemId, serial, symbol, buyer: asked.buyer, object: object ?? "" };
    this.store.save();
    const given = await this.finish(connection, payment);
    return { ok: true, object: given, serial };
  }

  /** Write down which item of the shop an issued object is: from then on a wallet holding it is shown to own that item. */
  private record(kind: "skin" | "hero" | "weapon", object: string, itemId: string, serial: number, symbol: string): void {
    const record = this.store.data.shop;
    if (kind === "skin") record.skins[object] = { skinId: itemId, serial, symbol };
    else if (kind === "hero") record.heroes[object] = { heroId: itemId, serial, symbol };
    else record.weapons[object] = { weaponId: itemId, serial, symbol };
  }

  /**
   * Before the payment is asked: an item of what it buys, issued (or one prepared before and not sold) and held for
   * this sale, and the wallet asked to take it up. The wallet answers what it is asked in turn, so asked first the
   * taking up is answered at once, while the player still has the payment before them; asked after the payment, it
   * would wait for the player and the payment to be sent. Nothing is given here: the give waits for the payment.
   * Undefined when no item could be made ready; the sale then issues and hands the item over once paid.
   */
  private async stage(connection: Connection, kind: "skin" | "hero" | "weapon", itemId: string, name: string): Promise<ObjectInfo | undefined> {
    const record = this.store.data.shop;
    const buyer = hex(connection.opponent.signingKey);
    let reserved: string | undefined;
    try {
      // The serial is taken and the item recorded as prepared before anything is awaited, as a sale's is.
      let info: ObjectInfo;
      const spare = Object.entries(record.prepared).find(([id, p]) => p.kind === kind && p.itemId === itemId && !this.#reserved.has(id));
      if (spare !== undefined) {
        reserved = spare[0];
        this.#reserved.add(reserved);
        info = await this.app.object(reserved);
      } else {
        const serial = (record.serial += 1);
        const symbol = `${PREFIX[kind]}${String(serial).padStart(6, "0")}`;
        this.store.save();
        info = await this.app.issue({ symbol, name: `${name} #${serial}`, supply: "1", description: `Sennite: Factions ${kind} ${itemId}` });
        reserved = info.id;
        this.#reserved.add(reserved);
        record.prepared[info.id] = { kind, itemId, serial, symbol };
        this.store.save();
      }
      const held = `${buyer}:${info.id}`;
      if (!this.#takenUp.has(held) && !this.#takingUp.has(held)) this.#takingUp.set(held, await this.app.request(connection, ask.accept(info)));
      return info;
    } catch (e) {
      console.error(`[publisher] preparing a ${kind} (${itemId}) ahead of its payment: ${e instanceof Error ? e.message : String(e)}`);
      if (reserved !== undefined) this.#reserved.delete(reserved);
      return undefined;
    }
  }

  /**
   * While the player approves the payment asked under `key`: the wallet's answer to taking up the item staged for it.
   * An answer later than this wait is still the one the give waits on (deliver), and the item stays this sale's.
   */
  private async prepared(connection: Connection, key: string, info: ObjectInfo): Promise<ObjectInfo> {
    const held = `${hex(connection.opponent.signingKey)}:${info.id}`;
    const asking = this.#takingUp.get(held);
    if (asking !== undefined) {
      try {
        const taken = await this.app.wait(asking, { until: "answered", timeoutMs: ACCEPT_WAIT_MS });
        this.#takingUp.delete(held);
        if (taken.outcome === "carried_out") this.#takenUp.add(held);
      } catch {
        // not answered yet: the give waits on this same asking
      }
    }
    // a sale that ended while this was under way leaves the item for the next sale of it
    if (!Object.hasOwn(this.store.data.shop.asked, key)) this.#reserved.delete(info.id);
    return info;
  }

  /** An asking that ended unpaid: what was being prepared for it is left for the next sale of the same item. */
  private unprepare(key: string): void {
    const preparing = this.#preparing.get(key);
    this.#preparing.delete(key);
    void preparing?.then((info) => {
      if (info !== undefined) this.#reserved.delete(info.id);
    });
  }

  /** Issue a pending sale's item if it is not issued yet, record it, give it to the buyer, and close the sale. */
  private async finish(connection: Connection, payment: string): Promise<string> {
    const record = this.store.data.shop;
    const sale = record.pending[payment];
    if (sale === undefined) throw new Error(`no pending sale for payment ${payment}`);
    if (hex(connection.opponent.signingKey) !== sale.buyer) throw new Error("the connection is not the wallet that paid for the sale");
    if (this.#finishing.has(payment)) throw new Refused("that sale is being finished now");
    this.#finishing.add(payment);
    try {
      const item = sale.kind === "skin" ? this.skin(sale.itemId) : sale.kind === "hero" ? this.hero(sale.itemId) : this.weapon(sale.itemId);
      let info: ObjectInfo;
      if (sale.object === "") {
        // The symbol is the sale's own, taken with its serial. If the server stopped after the object was issued and
        // before that was written down, the account holds it already: it is that object, and none is issued again.
        // Only one the account still holds, and that no sale is recorded for: an object sold before is never the one.
        const sold = (id: string): boolean => Object.hasOwn(record.skins, id) || Object.hasOwn(record.heroes, id) || Object.hasOwn(record.weapons, id);
        const issued = (await this.app.owned()).find((o) => o.object.symbol === sale.symbol && o.amount > 0n && !sold(o.object.id))?.object;
        info = issued ?? (await this.app.issue({ symbol: sale.symbol, name: `${item.name} #${sale.serial}`, supply: "1", description: `Sennite: Factions ${sale.kind} ${item.id}` }));
        sale.object = info.id;
        this.record(sale.kind, info.id, sale.itemId, sale.serial, sale.symbol);
        record.paid[payment] = info.id;
        this.store.save();
      } else {
        info = await this.app.object(sale.object);
      }
      await this.deliver(connection, info);
      delete record.pending[payment];
      this.store.save();
      return info.id;
    } finally {
      this.#finishing.delete(payment);
    }
  }

  /** Finish every sale to this wallet that was paid for and cut off before its item was given. */
  async settle(connection: Connection): Promise<void> {
    const buyer = hex(connection.opponent.signingKey);
    for (const [payment, sale] of Object.entries(this.store.data.shop.pending)) {
      if (sale.buyer === buyer && !this.#finishing.has(payment)) await this.finish(connection, payment);
    }
  }

  /**
   * Look at everything the shop asked a wallet to pay for and has not seen the
   * end of, and at every sale paid for and not given: carry out what has been
   * paid, forget what was declined, failed, or asked of a wallet that has since
   * ended its connection. The server runs this every few seconds, so a payment
   * approved at any time buys what it was for.
   */
  async sweep(): Promise<void> {
    // one look at a time: a look that takes longer than the time between looks is not joined by another
    if (this.#sweeping) return;
    this.#sweeping = true;
    try {
      const record = this.store.data.shop;
      for (const [key, asked] of Object.entries(record.asked)) {
        try {
          const result = await this.app.result({ connection: asked.connection, seq: BigInt(asked.seq) });
          const ended = (): void => {
            delete record.asked[key];
            this.store.save();
            this.unprepare(key);
          };
          // A payment that landed is carried out whatever else the wallet said of it: that is looked at first.
          if (result.fact === "received" && result.received !== undefined) {
            await this.paid(await this.app.connection(asked.connection), key, hex(result.received));
            continue;
          }
          if (result.outcome === "declined" || result.outcome === "failed") {
            ended();
            continue;
          }
          // Not answered. An asking written before its time was kept is as old as this look at it.
          if (asked.at === undefined) {
            asked.at = Date.now();
            this.store.save();
          }
          // It is over when the account says its wallet ended the connection, or it is older than a wallet can be
          // thought to still show its player. An account that could not be asked says neither: the asking stays,
          // and the next look asks again (the error is written by the catch below).
          const connection = await this.app.connection(asked.connection);
          if (!connection.connected || Date.now() - asked.at > FORGOTTEN_AFTER_MS) ended();
        } catch (e) {
          console.error(`[publisher] the shop's look at what it asked (${asked.kind} ${asked.itemId}): ${e instanceof Error ? e.message : String(e)}`);
        }
      }
      for (const [key, asked] of Object.entries(record.trades)) {
        try {
          const result = await this.app.result({ connection: asked.connection, seq: BigInt(asked.seq) });
          const ended = (): void => {
            delete record.trades[key];
            this.store.save();
          };
          if (result.fact === "received" && result.received !== undefined) {
            await this.landed(await this.app.connection(asked.connection), key, hex(result.received));
            continue;
          }
          if (result.outcome === "declined" || result.outcome === "failed") {
            ended();
            continue;
          }
          if (asked.at === undefined) {
            asked.at = Date.now();
            this.store.save();
          }
          const connection = await this.app.connection(asked.connection);
          if (!connection.connected || Date.now() - asked.at > FORGOTTEN_AFTER_MS) ended();
        } catch (e) {
          console.error(`[publisher] the shop's look at a material trade it asked (${asked.resource} ${asked.way}): ${e instanceof Error ? e.message : String(e)}`);
        }
      }
      const connections = Object.keys(record.pending).length + Object.keys(record.owed).length > 0 ? await this.app.connections() : [];
      for (const [transfer, trade] of Object.entries(record.owed)) {
        if (trade.giving) continue;
        // over the connection the trade was asked on; a trade recorded without one, over each of the wallet's
        // connections in turn, the newest first: the wallet answers only on the one its game uses now
        const theirs = connections.filter((c) => c.connected && hex(c.opponent.signingKey) === trade.wallet);
        const over = trade.connection !== undefined ? theirs.filter((c) => c.id === trade.connection) : [...theirs].reverse();
        for (const connection of over) {
          try {
            await this.pay(connection, transfer);
          } catch (e) {
            console.error(`[publisher] giving what a material trade owes (${trade.resource} ${trade.way}): ${e instanceof Error ? e.message : String(e)}`);
          }
          if (!Object.hasOwn(record.owed, transfer) || record.owed[transfer]?.giving) break;
        }
      }
      for (const [payment, sale] of Object.entries(record.pending)) {
        const connection = connections.find((c) => c.connected && hex(c.opponent.signingKey) === sale.buyer);
        if (connection === undefined || this.#finishing.has(payment)) continue;
        try {
          await this.finish(connection, payment);
        } catch (e) {
          console.error(`[publisher] finishing a ${sale.kind} paid for (${sale.itemId}): ${e instanceof Error ? e.message : String(e)}`);
        }
      }
    } finally {
      this.#sweeping = false;
    }
  }

  /** Hand a wallet an object the shop issued: the wallet takes the object up, then the account gives it. */
  private async deliver(connection: Connection, object: ObjectInfo): Promise<void> {
    const taken = `${hex(connection.opponent.signingKey)}:${object.id}`;
    if (!this.#takenUp.has(taken)) {
      // the asking made while the player approved, if it is still open; else a new one
      const asking = this.#takingUp.get(taken) ?? (await this.app.request(connection, ask.accept(object)));
      await this.app.wait(asking, { until: "answered" });
      this.#takingUp.delete(taken);
    }
    await this.app.give(connection, object, "1");
    this.#takenUp.delete(taken);
  }

  /** The skins among what a wallet proved it owns: `proven` is object id (Base32 Crockford) to amount. */
  skinsAmong(proven: Map<string, bigint>): OwnedSkin[] {
    const owned: OwnedSkin[] = [];
    for (const [object, sold] of Object.entries(this.store.data.shop.skins)) {
      if ((proven.get(object) ?? 0n) > 0n) {
        owned.push({ skinId: sold.skinId, name: this.skin(sold.skinId).name, object, serial: sold.serial });
      }
    }
    return owned.sort((a, b) => a.serial - b.serial);
  }

  /** The heroes among what a wallet proved it owns: `proven` is object id (Base32 Crockford) to amount. */
  heroesAmong(proven: Map<string, bigint>): OwnedHero[] {
    const owned: OwnedHero[] = [];
    for (const [object, sold] of Object.entries(this.store.data.shop.heroes)) {
      if ((proven.get(object) ?? 0n) > 0n) {
        owned.push({ heroId: sold.heroId, name: this.hero(sold.heroId).name, object, serial: sold.serial });
      }
    }
    return owned.sort((a, b) => a.serial - b.serial);
  }

  /** The guns among what a wallet proved it owns: `proven` is object id (Base32 Crockford) to amount. */
  weaponsAmong(proven: Map<string, bigint>): OwnedWeapon[] {
    const owned: OwnedWeapon[] = [];
    for (const [object, sold] of Object.entries(this.store.data.shop.weapons)) {
      if ((proven.get(object) ?? 0n) > 0n) {
        owned.push({ weaponId: sold.weaponId, name: this.weapon(sold.weaponId).name, object, serial: sold.serial });
      }
    }
    return owned.sort((a, b) => a.serial - b.serial);
  }

  /** What the shop reads from a wallet's proof: its D-bucks, its ERA, its skins, its heroes and its guns. */
  async purse(proven: Map<string, bigint>): Promise<Purse> {
    const dbucks = await this.app.object(this.symbol);
    const era = await this.app.object("ERA");
    return {
      dbucks: Number(proven.get(dbucks.id) ?? 0n),
      era: await this.app.display(era, proven.get(era.id) ?? 0n),
      skins: this.skinsAmong(proven),
      heroes: this.heroesAmong(proven),
      weapons: this.weaponsAmong(proven),
    };
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
