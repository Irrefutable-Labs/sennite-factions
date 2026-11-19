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

export class Shop {
  constructor(
    private readonly app: AppAccount,
    private readonly store: Store,
    readonly world: ShopWorld,
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
  async buyDbucks(connection: Connection, era: string, atLeast: number): Promise<{ ok: boolean; why: string }> {
    const ref = await this.app.request(connection, ask.trade("ERA", this.symbol, era, String(atLeast)));
    const answered = await until(async () => {
      const r = await this.app.result(ref);
      return r.answered && r.outcome !== "awaiting_approval" ? r : undefined;
    });
    if (answered === undefined) return { ok: false, why: "the wallet did not answer the trade" };
    return answered.outcome === "carried_out" ? { ok: true, why: "" } : { ok: false, why: answered.reason || answered.detail || `the wallet's answer is ${answered.outcome}` };
  }

  /**
   * Sell a skin: the wallet sends the price in D-bucks; once the publisher's
   * account has accepted that transfer, and only then, the shop issues the
   * skin and gives it to the wallet. A payment buys one skin, once.
   */
  async buySkin(connection: Connection, skinId: string): Promise<{ ok: true; skin: OwnedSkin } | { ok: false; why: string }> {
    const skin = this.skin(skinId);
    const sold = await this.sell(connection, skin, "skin", "SK", (object, serial, symbol) => {
      this.store.data.shop.skins[object] = { skinId: skin.id, serial, symbol };
    });
    return sold.ok ? { ok: true, skin: { skinId: skin.id, name: skin.name, object: sold.object, serial: sold.serial } } : sold;
  }

  /** Sell a hero, as a skin is sold: paid for first, then issued, one of it, and given. */
  async buyHero(connection: Connection, heroId: string): Promise<{ ok: true; hero: OwnedHero } | { ok: false; why: string }> {
    const hero = this.hero(heroId);
    const sold = await this.sell(connection, hero, "hero", "HR", (object, serial, symbol) => {
      this.store.data.shop.heroes[object] = { heroId: hero.id, serial, symbol };
    });
    return sold.ok ? { ok: true, hero: { heroId: hero.id, name: hero.name, object: sold.object, serial: sold.serial } } : sold;
  }

  /** Sell a gun, as a hero is sold: paid for first, then issued, one of it, and given. */
  async buyWeapon(connection: Connection, weaponId: string): Promise<{ ok: true; weapon: OwnedWeapon } | { ok: false; why: string }> {
    const weapon = this.weapon(weaponId);
    const sold = await this.sell(connection, weapon, "weapon", "WP", (object, serial, symbol) => {
      this.store.data.shop.weapons[object] = { weaponId: weapon.id, serial, symbol };
    });
    return sold.ok ? { ok: true, weapon: { weaponId: weapon.id, name: weapon.name, object: sold.object, serial: sold.serial } } : sold;
  }

  /**
   * The wallet sends the item's price in D-bucks; once the publisher's account
   * has accepted that transfer, and only then, the shop issues the item, an
   * object of which one exists, records it with `keep`, and gives it to the
   * wallet. A payment buys one item, once.
   *
   * The account reports the fact "received" only for a transfer that landed
   * paying what was asked: this object, this amount (dsm-app-host's
   * landed_payment). An answer from the wallet saying it paid grants nothing.
   *
   * The sale is recorded as pending, with its buyer, before anything is issued,
   * and stays pending until the item is given: a sale cut off part way (the
   * server stopped, the wallet did not take the item up) is finished the next
   * time that wallet buys anything (`settle`).
   */
  private async sell(
    connection: Connection,
    item: { id: string; name: string; price: string },
    kind: "skin" | "hero" | "weapon",
    prefix: string,
    keep: (object: string, serial: number, symbol: string) => void,
  ): Promise<{ ok: true; object: string; serial: number } | { ok: false; why: string }> {
    await this.settle(connection);
    const paid = await this.app.wait(await this.app.request(connection, ask.send(this.symbol, item.price, `${kind}: ${item.name}`)));
    if (paid.fact !== "received" || paid.received === undefined) {
      return { ok: false, why: paid.reason || paid.detail || `the wallet's answer is ${paid.outcome}` };
    }
    const payment = hex(paid.received);
    const record = this.store.data.shop;
    if (Object.hasOwn(record.paid, payment)) return { ok: false, why: `that payment has already bought a ${kind}` };

    // The serial is taken, the payment marked used and the sale recorded as pending, before anything else is awaited.
    const serial = (record.serial += 1);
    const symbol = `${prefix}${String(serial).padStart(6, "0")}`;
    record.paid[payment] = "";
    record.pending[payment] = { kind, itemId: item.id, serial, symbol, buyer: hex(connection.opponent.signingKey), object: "" };
    this.store.save();
    const object = await this.finish(connection, payment, keep);
    return { ok: true, object, serial };
  }

  /** Issue a pending sale's item if it is not issued yet, record it, give it to the buyer, and close the sale. */
  private async finish(connection: Connection, payment: string, keep?: (object: string, serial: number, symbol: string) => void): Promise<string> {
    const record = this.store.data.shop;
    const sale = record.pending[payment];
    if (sale === undefined) throw new Error(`no pending sale for payment ${payment}`);
    const item = sale.kind === "skin" ? this.skin(sale.itemId) : sale.kind === "hero" ? this.hero(sale.itemId) : this.weapon(sale.itemId);
    let info: ObjectInfo;
    if (sale.object === "") {
      info = await this.app.issue({ symbol: sale.symbol, name: `${item.name} #${sale.serial}`, supply: "1", description: `Sennite: Factions ${sale.kind} ${item.id}` });
      sale.object = info.id;
      if (keep !== undefined) keep(info.id, sale.serial, sale.symbol);
      else if (sale.kind === "skin") record.skins[info.id] = { skinId: sale.itemId, serial: sale.serial, symbol: sale.symbol };
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
  }

  /** Finish every sale to this wallet that was paid for and cut off before its item was given. */
  async settle(connection: Connection): Promise<void> {
    const buyer = hex(connection.opponent.signingKey);
    for (const [payment, sale] of Object.entries(this.store.data.shop.pending)) {
      if (sale.buyer === buyer) await this.finish(connection, payment);
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
