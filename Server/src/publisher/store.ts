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
}

const empty = (): StoreData => ({ asked: {}, factions: {}, players: {} });

export class Store {
  readonly data: StoreData;

  /** A store kept in `file`, or in memory when no file is named. */
  constructor(private readonly file?: string) {
    this.data = file !== undefined && existsSync(file) ? { ...empty(), ...(JSON.parse(readFileSync(file, "utf8")) as StoreData) } : empty();
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
