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

import { ask, grant, type AppAccount, type Connection, type Grant, type ObjectInfo } from "dsm-app-client";
import { hex } from "../faction/canonical.js";
import { Refused } from "./refused.js";
import type { Store } from "./store.js";
import type { HeroForSale, ShopWorld, SkinForSale } from "./world.js";

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

/** What a wallet proved it holds that the shop cares about. */
export interface Purse {
  /** D-bucks, in whole units. */
  dbucks: number;
  /** ERA, as the account renders it. */
  era: string;
  skins: OwnedSkin[];
  heroes: OwnedHero[];
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

  /** What the shop asks a wallet to grant: trading ERA for D-bucks through the vault, and paying D-bucks, within caps. */
  grants(): Grant[] {
    const { era, dbucks } = this.world.grant;
    return [
      grant.trade("ERA", this.symbol, [
        { object: "ERA", ...era },
        { object: this.symbol, ...dbucks },
      ]),
      grant.send(this.symbol, dbucks.perRequest, dbucks.total),
    ];
  }

  skin(skinId: string): SkinForSale {
    const found = this.world.skins.find((k) => k.id === skinId);
    if (found === undefined) throw new Refused(`the shop has no skin ${skinId}`);
    return found;
  }

  hero(heroId: string): HeroForSale {
    const found = this.world.heroes.find((h) => h.id === heroId);
    if (found === undefined) throw new Refused(`the shop has no hero ${heroId}`);
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

  /**
   * The wallet sends the item's price in D-bucks; once the publisher's account
   * has accepted that transfer, and only then, the shop issues the item, an
   * object of which one exists, records it with `keep`, and gives it to the
   * wallet. A payment buys one item, once.
   */
  private async sell(
    connection: Connection,
    item: { id: string; name: string; price: string },
    kind: string,
    prefix: string,
    keep: (object: string, serial: number, symbol: string) => void,
  ): Promise<{ ok: true; object: string; serial: number } | { ok: false; why: string }> {
    const paid = await this.app.wait(await this.app.request(connection, ask.send(this.symbol, item.price, `${kind}: ${item.name}`)));
    if (paid.fact !== "received" || paid.received === undefined) {
      return { ok: false, why: paid.reason || paid.detail || `the wallet's answer is ${paid.outcome}` };
    }
    const payment = hex(paid.received);
    const record = this.store.data.shop;
    if (Object.hasOwn(record.paid, payment)) return { ok: false, why: `that payment has already bought a ${kind}` };

    // The serial is taken, and the payment marked used, before anything else is awaited.
    const serial = (record.serial += 1);
    const symbol = `${prefix}${String(serial).padStart(6, "0")}`;
    record.paid[payment] = "";
    this.store.save();

    const object = await this.app.issue({ symbol, name: `${item.name} #${serial}`, supply: "1", description: `Sennite: Factions ${kind} ${item.id}` });
    keep(object.id, serial, symbol);
    record.paid[payment] = object.id;
    this.store.save();
    await this.deliver(connection, object);
    return { ok: true, object: object.id, serial };
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

  /** What the shop reads from a wallet's proof: its D-bucks, its ERA, its skins and its heroes. */
  async purse(proven: Map<string, bigint>): Promise<Purse> {
    const dbucks = await this.app.object(this.symbol);
    const era = await this.app.object("ERA");
    return {
      dbucks: Number(proven.get(dbucks.id) ?? 0n),
      era: await this.app.display(era, proven.get(era.id) ?? 0n),
      skins: this.skinsAmong(proven),
      heroes: this.heroesAmong(proven),
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
