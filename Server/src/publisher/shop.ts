// D-bucks, the publisher's vault and the shop (spec 10.5).
//
// D-bucks are an object the publisher's account issues with a fixed maximum
// supply. Players get them from the publisher's SoFi vault, which sells them
// for ERA at a price its reserves set and a fee the publisher chooses. The
// shop sells skins for D-bucks: the wallet sends the price, the publisher's
// account accepts it, and only then issues the skin, an object of which one
// exists, and gives it to the wallet. From there the skin is the player's:
// they can send it to anyone, and the game has no say. A hero is sold the
// same way: an object of which one exists, the wallet's to keep or send.
//
// The shop records what it issued and which payment bought it. It never
// records who owns a skin: that is what a wallet proves.

import { ask, type AppAccount, type Connection, type Grant, type ObjectInfo } from "dsm-app-client";
import { hex } from "../faction/canonical.js";
import { Refused } from "./refused.js";
import type { Store } from "./store.js";
import type { HeroForSale, ShopWorld, SkinForSale, WeaponForSale } from "./world.js";

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

/** What a sale is waiting on, said for the game to tell the player: `onWallet` when it is the player's approval in the wallet. */
export type Say = (stage: string, onWallet: boolean) => void;

/** How many payments the shop may have open in one wallet, waiting for the player, at once. */
const MAX_OPEN_PER_WALLET = 3;

/** The first letters of the symbol an item sold is issued under, by its kind. */
const PREFIX = { skin: "SK", hero: "HR", weapon: "WP" } as const;

export class Shop {
  /** The payments whose sale is being finished now, so that two looks at one sale do not both issue it. */
  readonly #finishing = new Set<string>();
  #sweeping = false;

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
    return answered.outcome === "carried_out" ? { ok: true, why: "" } : { ok: false, why: answered.reason || answered.detail || `the wallet's answer is ${answered.outcome}` };
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
   * the sale out. Only a decline, a failure or the wallet ending the connection
   * ends it. The owner, 2026-11-19, of a shop that gave up after four minutes
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
    const buyer = hex(connection.opponent.signingKey);
    if (Object.values(this.store.data.shop.asked).filter((a) => a.buyer === buyer).length >= MAX_OPEN_PER_WALLET) {
      return { ok: false, why: "payments already wait in your DSM wallet: approve or decline them there first" };
    }
    const ref = await this.app.request(connection, ask.send(this.symbol, item.price, `${kind}: ${item.name}`));
    const key = `${ref.connection}:${ref.seq}`;
    this.store.data.shop.asked[key] = { connection: ref.connection, seq: String(ref.seq), kind, itemId: item.id, buyer };
    this.store.save();

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
        return { ok: false, why: result.reason || result.detail || `the wallet's answer is ${result.outcome}` };
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
    const asked = record.asked[key];
    if (asked === undefined || Object.hasOwn(record.paid, payment)) {
      delete record.asked[key];
      this.store.save();
      return undefined;
    }
    // the item goes to the wallet that was asked and paid: never to another, whatever connection is in hand
    if (hex(connection.opponent.signingKey) !== asked.buyer) throw new Error("the connection is not the wallet that was asked to pay");
    // The asking is closed, the serial taken, the payment marked used and the sale recorded as pending, before anything is awaited.
    delete record.asked[key];
    const serial = (record.serial += 1);
    const symbol = `${PREFIX[asked.kind]}${String(serial).padStart(6, "0")}`;
    record.paid[payment] = "";
    record.pending[payment] = { kind: asked.kind, itemId: asked.itemId, serial, symbol, buyer: asked.buyer, object: "" };
    this.store.save();
    const object = await this.finish(connection, payment);
    return { ok: true, object, serial };
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
        const issued = (await this.app.owned()).find((o) => o.object.symbol === sale.symbol)?.object;
        info = issued ?? (await this.app.issue({ symbol: sale.symbol, name: `${item.name} #${sale.serial}`, supply: "1", description: `Sennite: Factions ${sale.kind} ${item.id}` }));
        sale.object = info.id;
        if (sale.kind === "skin") record.skins[info.id] = { skinId: sale.itemId, serial: sale.serial, symbol: sale.symbol };
        else if (sale.kind === "hero") record.heroes[info.id] = { heroId: sale.itemId, serial: sale.serial, symbol: sale.symbol };
        else record.weapons[info.id] = { weaponId: sale.itemId, serial: sale.serial, symbol: sale.symbol };
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
          const connection = await this.app.connection(asked.connection);
          if (result.fact === "received" && result.received !== undefined) {
            await this.paid(connection, key, hex(result.received));
          } else if (result.outcome === "declined" || result.outcome === "failed" || !connection.connected) {
            delete record.asked[key];
            this.store.save();
          }
        } catch (e) {
          console.error(`[publisher] the shop's look at what it asked (${asked.kind} ${asked.itemId}): ${e instanceof Error ? e.message : String(e)}`);
        }
      }
      const connections = Object.keys(record.pending).length > 0 ? await this.app.connections() : [];
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
    await this.app.wait(await this.app.request(connection, ask.accept(object)), { until: "answered" });
    await this.app.give(connection, object, "1");
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
