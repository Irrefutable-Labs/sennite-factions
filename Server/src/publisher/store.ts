// The publisher's server's own record: what it asked of which wallet, which
// factions it assigned parcels to, the contributions it saw committed, the
// leavers' notices it is timing and what stands in play. It is game data,
// kept in one file. Nothing in it is evidence about DSM: every fact is read
// again from the publisher's account when it is needed.

import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";

/** A write the server asked a wallet to make at a cell. */
export interface AskedWrite {
  /** The connection it was asked over (Base32 Crockford). */
  connection: string;
  seq: string;
  /** The asked wallet's signing key (lowercase hex). */
  signerKey: string;
  /** The asked payload (lowercase hex). */
  payload: string;
}

/** A contribution the publisher's account established as committed. */
export interface ContributionRecord {
  /** The vault (lowercase hex). */
  vault: string;
  /** The owner's verdict cell for the structure (lowercase hex). */
  cell: string;
  /** The owner's signing key (lowercase hex). */
  owner: string;
}

/** A leaver's 48-hour notice (spec 3): the publisher's server keeps the clock. */
export interface Notice {
  /** The leaver's genesis and device id (lowercase hex), as the state listed them. */
  genesis: string;
  deviceId: string;
  /** When the leave was final, on the server's clock (milliseconds). */
  leftAt: number;
  /** When the server returned the leaver's vaults; 0 until it has. */
  returnedAt: number;
}

/** Whether a plan stands in play: decided by the publisher's server, outside faction state (spec 7.6). */
export interface Play {
  /** The prefab_id that stands in play; empty while the plan is not built. */
  standing: string;
  /** The lineage height at which a battle destroyed it; -1 when it was not, or a member has rebuilt it since. */
  destroyedAt: number;
}

export interface FactionRecord {
  parcelId: string;
  /** signing key -> notice */
  notices: Record<string, Notice>;
  /** instance_id -> contributions seen committed to it */
  contributions: Record<string, ContributionRecord[]>;
  /** instance_id -> what stands in play there (spec 7.6) */
  play: Record<string, Play>;
}

/** A player of the game, as the server knows one: by the offer their wallet connects from. */
export interface PlayerRecord {
  /** The digest of the offer this player's connect code names (lowercase hex). */
  offer: string;
  /** When the server made the record, on its clock (milliseconds). */
  since: number;
  /** Whether a wallet ever approved the offer. Set once, and never taken back when the wallet disconnects. */
  approved?: boolean;
  /** When the player's game last asked anything with its token, to the hour (milliseconds). */
  lastSeen?: number;
  /** Which terms the player's offer was made on (TERMS in service.ts); a record from before they were kept has none. */
  terms?: number;
}

/** A skin the shop sold: the one object issued for it. */
export interface SoldSkin {
  /** Which skin of the shop it is. */
  skinId: string;
  /** Its number among the skins sold, and the symbol it was issued under. */
  serial: number;
  symbol: string;
}

/** A hero the shop sold: the one object issued for it. */
export interface SoldHero {
  /** Which hero of the shop it is. */
  heroId: string;
  /** Its number among everything the shop has sold, and the symbol it was issued under. */
  serial: number;
  symbol: string;
}

/** A gun the shop sold: the one object issued for it. */
export interface SoldWeapon {
  /** Which gun of the shop it is. */
  weaponId: string;
  /** Its number among everything the shop has sold, and the symbol it was issued under. */
  serial: number;
  symbol: string;
}

/**
 * A sale paid for and not yet finished: the payment is taken, and the item is
 * to be issued (if `object` is empty) and given to the buyer. A sale stays here
 * until the item is given, so a sale cut off part way is finished later.
 */
export interface PendingSale {
  kind: "skin" | "hero" | "weapon";
  itemId: string;
  serial: number;
  symbol: string;
  /** The buyer's wallet: its signing key (lowercase hex). */
  buyer: string;
  /** The object issued for it (Base32 Crockford); empty until it is issued. */
  object: string;
}

/**
 * A trade of a material with the shop: `bought` is a pack the wallet pays D-bucks for, `sold` is a pack the
 * wallet sends the shop for D-bucks. What it was asked at is kept with it: a price changed since then does
 * not change a trade already asked.
 */
export interface MaterialTrade {
  way: "bought" | "sold";
  /** The material's symbol. */
  resource: string;
  /** Whole units of the material. */
  units: number;
  /** Whole D-bucks: what the wallet pays for a pack bought, what the shop pays for a pack sold. */
  dbucks: number;
  /** The wallet: its signing key (lowercase hex). */
  wallet: string;
}

/** A material trade the shop asked a wallet for and has not seen the end of, as an asked sale is kept. */
export interface AskedTrade extends MaterialTrade {
  connection: string;
  seq: string;
  at?: number;
}

/**
 * A material trade whose first half landed (the wallet's D-bucks, or its material) and whose second half
 * is owed. `giving` is set, and written down, before the shop gives what it owes, and the trade is taken
 * off once that is given. A trade found here with `giving` set was cut off while the shop was giving: the
 * account cannot say whether that give was sent, and a material or a D-buck given twice is twice in
 * circulation, so the shop does not give it again by itself. It stays here for the publisher to settle.
 */
export interface OwedTrade extends MaterialTrade {
  /**
   * The connection the trade was asked over (Base32 Crockford): what is owed is given over it. A wallet
   * can have more than one connection to the game that the account still counts as connected (one from
   * before the game was installed anew), and only the one its game uses now is answered.
   */
  connection?: string;
  giving?: boolean;
}

/**
 * A payment the shop asked a wallet for and has not seen the end of: it waits
 * in the wallet for the player, who may approve it at any time. Kept until the
 * payment lands (the sale then becomes pending), or it is declined or fails.
 */
export interface AskedSale {
  /** The request, as the account names it: the connection's id and the request's number on it. */
  connection: string;
  seq: string;
  kind: "skin" | "hero" | "weapon";
  itemId: string;
  /** The buyer's wallet: its signing key (lowercase hex). */
  buyer: string;
  /** When it was asked, on the server's clock (milliseconds). */
  at?: number;
}

/** The shop's own record (spec 10.5). Who owns what is never here: a wallet proves that. */
export interface ShopRecord {
  /** How many skins and heroes the shop has sold. */
  serial: number;
  /** object id (Base32 Crockford) -> the skin it is */
  skins: Record<string, SoldSkin>;
  /** object id (Base32 Crockford) -> the hero it is */
  heroes: Record<string, SoldHero>;
  /** object id (Base32 Crockford) -> the gun it is */
  weapons: Record<string, SoldWeapon>;
  /** payment (lowercase hex of the accepted transfer's id) -> the object it bought. A payment buys once. */
  paid: Record<string, string>;
  /** payment -> the sale it pays for, while the item has not been given. */
  pending: Record<string, PendingSale>;
  /** "connection:seq" of a payment the shop asked a wallet for -> what it is for, until the wallet's answer is seen. */
  asked: Record<string, AskedSale>;
  /** "connection:seq" of what a material trade asked a wallet to send -> the trade, until the wallet's answer is seen. */
  trades: Record<string, AskedTrade>;
  /** landed transfer (lowercase hex of its id) -> the material trade it began, until what the shop owes is given. */
  owed: Record<string, OwedTrade>;
  /** landed transfer -> "" : every transfer a material trade has been begun on. A transfer begins one trade, once. */
  traded: Record<string, string>;
}

/** One member's share of a match's prize (spec 10.6): an escrow vault the publisher's account locks. */
export interface PrizeShare {
  /** The faction whose win pays this share to the member. Any other outcome pays it back to the publisher. */
  factionId: string;
  /** The member: signing key, genesis and device id (lowercase hex), as the faction's state listed them when the match opened. */
  signingKey: string;
  genesis: string;
  deviceId: string;
  resource: string;
  amount: number;
  /** The vault (lowercase hex); empty until it is locked. */
  vault: string;
  /** Whether the server has read the vault released: to the member, or back to the publisher. */
  released: boolean;
}

/**
 * What one member's mining droid took of one material in a match, which the publisher gives that member's
 * wallet (owner, 2026-11-22). `giving` is set, and written down, before the publisher gives it, and `given`
 * once it has: one found with `giving` set and not `given` was cut off while it was being given, and is
 * not given again by the server (the account cannot say whether that give was sent).
 */
export interface MinedShare {
  /** The member: its signing key (lowercase hex), as the match's shares name it. */
  signingKey: string;
  resource: string;
  units: number;
  giving?: boolean;
  given: boolean;
}

/** A match and its prize, as the publisher's server opened it. What each vault holds is read from DSM. */
export interface MatchRecord {
  /** The factions in the match: each is an outcome of every vault. */
  factions: string[];
  /** The one verdict cell every vault of the match is bound to (lowercase hex); empty until the first is locked. */
  cell: string;
  shares: PrizeShare[];
  /** Whether every share is locked. */
  opened: boolean;
  /** The outcome the cell's final verdict names: a faction, or `void`; empty until the match is decided. */
  outcome: string;
  openedAt: number;
  /** What the match's players mined, as the match server reported it once the match was over; none until it has. */
  mined?: MinedShare[];
}

export interface StoreData {
  /** cell (its name and writer set, as `cellRecordKey` gives it) -> the writes asked there, in the order asked */
  asked: Record<string, AskedWrite[]>;
  /** faction_id -> the server's record of it */
  factions: Record<string, FactionRecord>;
  /**
   * The game's own token for a player -> that player. The token only resumes a
   * player's place in the game; it is never DSM authority.
   */
  players: Record<string, PlayerRecord>;
  shop: ShopRecord;
  /** match id -> the match and its prize (spec 10.6) */
  matches: Record<string, MatchRecord>;
}

const empty = (): StoreData => ({ asked: {}, factions: {}, players: {}, shop: { serial: 0, skins: {}, heroes: {}, weapons: {}, paid: {}, pending: {}, asked: {}, trades: {}, owed: {}, traded: {} }, matches: {} });

export class Store {
  readonly data: StoreData;

  /** A store kept in `file`, or in memory when no file is named. */
  constructor(private readonly file?: string) {
    this.data = file !== undefined && existsSync(file) ? { ...empty(), ...(JSON.parse(readFileSync(file, "utf8")) as StoreData) } : empty();
    // a record written before the shop sold heroes or guns, or kept sales pending, has none
    this.data.shop = { ...empty().shop, ...this.data.shop };
  }

  /** Write the record to its file: a whole new file, renamed into place. */
  save(): void {
    if (this.file === undefined) return;
    mkdirSync(dirname(this.file), { recursive: true });
    const next = `${this.file}.next`;
    writeFileSync(next, JSON.stringify(this.data, null, 1));
    renameSync(next, this.file);
  }

  faction(factionId: string): FactionRecord {
    const found = this.data.factions[factionId];
    if (found === undefined) throw new Error(`the server holds no record of faction ${factionId}`);
    return found;
  }
}
