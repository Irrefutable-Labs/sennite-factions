// Matches pay out materials (spec 10.6).
//
// Each match has a prize: a small quantity of each material, out of what the
// publisher's account holds of its capped supply, locked in escrow before the
// match and paid to the winners.
//
// A DSM escrow vault holds one object and is released whole, once, to the one
// identity its decided outcome pays. So the prize is one vault per member's
// share (owner's ruling of 2026-11-13: "Yes, per share"). Every vault of a
// match has the same outcomes, the factions in the match and `void`, each
// decided by the publisher's key alone, so all of them are bound to one
// verdict cell and settle on one verdict. A member's vault pays that member
// when their faction wins, and pays the publisher back on any other outcome.
//
// How the prize is split is a game rule: each faction's members, as the
// faction's state lists them when the match opens, share it equally in whole
// units. What does not divide is never locked and stays with the publisher.
//
// The server records which vaults it locked. What each holds, and what the
// cell's verdict is, is read from DSM each time it is wanted.

import { encodeB32, IngressError, pb, type AppAccount } from "dsm-app-client";
import { hex, unhex } from "../faction/canonical.js";
import type { Member } from "../faction/state.js";
import { decideVault, lockedVaults, lockVault, ownVaults, releaseVault, settling, verdict, type Party } from "../dsm/routes.js";
import { Refused } from "./refused.js";
import type { MatchRecord, PrizeShare, Store } from "./store.js";
import type { MatchWorld } from "./world.js";

/** The outcome that pays every share back to the publisher: the match had no winner. */
export const VOID = "void";

/** How long a decision is given to become the cell's verdict for good. */
const FINAL_MS = 240_000;
/** How long a match's read is served again to the games asking what their wallets have won. */
const WAITING_FRESH_MS = 5_000;

/** A share of a prize as the server reads it now. */
export interface ShareView {
  factionId: string;
  signingKey: string;
  resource: string;
  amount: number;
  /**
   * `locked` until the match is decided; then `waiting` (the member's to
   * collect) and `collected`, or `returning` and `returned` (the publisher's).
   * `not yet known` while the vault could not be read.
   */
  state: "not locked" | "locked" | "waiting" | "collected" | "returning" | "returned" | "not yet known";
}

export interface MatchView {
  matchId: string;
  factions: string[];
  /** A faction, `void`, or empty while the match is undecided. */
  outcome: string;
  /** The match's verdict cell, as a wallet's escrow screen takes it (Base32 Crockford). */
  cell: string;
  opened: boolean;
  shares: ShareView[];
  /** Vaults on the cell the server did not lock. They are no part of the prize. */
  others: number;
}

/** A share a member won and has not collected: found in the wallet by the cell. */
export interface Waiting {
  matchId: string;
  cell: string;
  resource: string;
  amount: number;
}

export interface MatchesOptions {
  app: AppAccount;
  store: Store;
  world: MatchWorld;
  /** The members a faction's state lists now. */
  members: (factionId: string) => Promise<Member[]>;
  /** The publisher's account as DSM names it. */
  party: () => Promise<Party>;
  now: () => number;
}

const same = (a: Uint8Array, b: Uint8Array): boolean => a.length === b.length && a.every((x, i) => x === b[i]);

export class Matches {
  #opening: Promise<unknown> = Promise.resolve();
  readonly #read = new Map<string, { at: number; view: MatchView }>();

  constructor(private readonly options: MatchesOptions) {}

  private record(matchId: string): MatchRecord {
    const matches = this.options.store.data.matches;
    const found = Object.hasOwn(matches, matchId) ? matches[matchId] : undefined;
    if (found === undefined) throw new Refused(`no match ${matchId} is open`);
    return found;
  }

  /** The factions of a match the match server opened, in the order it named them. */
  factionsOf(matchId: string): string[] {
    return [...this.record(matchId).factions];
  }

  /** A match's outcome: a faction, `void`, or empty while it is undecided. */
  outcomeOf(matchId: string): string {
    return this.record(matchId).outcome;
  }

  /** What every vault of a match commits to: the match, by the id the match server gave it. */
  static external(matchId: string): Uint8Array {
    return new TextEncoder().encode(`sennite/match-prize/v1/${matchId}`);
  }

  /**
   * Open a match between `factions` and lock its prize: one vault per member's
   * share. Asked again for a match already open, it locks whatever is not
   * locked yet and nothing twice. One opening at a time.
   */
  open(matchId: string, factions: string[]): Promise<MatchView> {
    const opened = this.#opening.then(
      () => this.opened(matchId, factions),
      () => this.opened(matchId, factions),
    );
    this.#opening = opened.then(
      () => undefined,
      () => undefined,
    );
    return opened;
  }

  private async opened(matchId: string, factions: string[]): Promise<MatchView> {
    const { store } = this.options;
    if (!/^[A-Za-z0-9_-]{1,64}$/.test(matchId)) throw new Refused("a match id is 1 to 64 letters, digits, - or _");
    // An opening the server did not finish is finished first: a vault it locked
    // and did not record is then recorded under its own match, never another's.
    for (const [id, other] of Object.entries(store.data.matches)) {
      if (id !== matchId && !other.opened) await this.lock(id);
    }
    if (Object.hasOwn(store.data.matches, matchId)) {
      const held = this.record(matchId);
      if ([...held.factions].sort().join() !== [...factions].sort().join()) throw new Refused(`match ${matchId} is open between other factions`);
    } else {
      store.data.matches[matchId] = await this.plan(factions);
      store.save();
    }
    await this.lock(matchId);
    return this.read(matchId);
  }

  /** The shares a match between `factions` would lock now. Nothing is locked here. */
  private async plan(factions: string[]): Promise<MatchRecord> {
    const { app, store, world } = this.options;
    if (factions.length < 2 || new Set(factions).size !== factions.length) throw new Refused("a match is between two or more different factions");
    const shares: PrizeShare[] = [];
    const seen = new Set<string>();
    for (const factionId of factions) {
      if (!Object.hasOwn(store.data.factions, factionId)) throw new Refused(`no faction ${factionId}`);
      const members = await this.options.members(factionId);
      if (members.length === 0) throw new Refused(`faction ${factionId} has no members`);
      for (const member of members) {
        if (seen.has(member.signingKey)) throw new Refused("one wallet is a member of two factions of the match");
        seen.add(member.signingKey);
      }
      for (const [resource, amount] of Object.entries(world.prize)) {
        const each = Math.floor(amount / members.length);
        if (each === 0) continue;
        for (const m of members) {
          shares.push({ factionId, signingKey: m.signingKey, genesis: m.genesis, deviceId: m.deviceId, resource, amount: each, vault: "", released: false });
        }
      }
    }
    if (shares.length === 0) throw new Refused("the prize gives no member a whole unit");
    // The prize comes out of what the publisher's account holds of each material's fixed supply, and no further.
    const owned = await app.owned();
    for (const resource of Object.keys(world.prize)) {
      const needed = shares.filter((s) => s.resource === resource).reduce((sum, s) => sum + BigInt(s.amount), 0n);
      const held = owned.find((o) => o.object.symbol === resource)?.amount ?? 0n;
      if (held < needed) throw new Refused(`the publisher's account holds ${held} ${resource}, less than the ${needed} the match's prize takes`);
    }
    return { factions: [...factions], cell: "", shares, opened: false, outcome: "", openedAt: this.options.now() };
  }

  /**
   * Every escrow vault the account locked, once nothing of the account's is
   * still settling: while something is, the account says so and is asked again.
   */
  private async own(): Promise<pb.EscrowVaultV1[]> {
    const deadline = Date.now() + 120_000;
    for (;;) {
      try {
        return await ownVaults(this.options.app);
      } catch (e) {
        if (!settling(e) || Date.now() > deadline) throw e;
        await new Promise((resolve) => setTimeout(resolve, 500));
      }
    }
  }

  /** Whether `vault` is exactly `share` of the match as the server would lock it. */
  private isShare(vault: pb.EscrowVaultV1, record: MatchRecord, share: PrizeShare, object: Uint8Array, publisher: Party): boolean {
    if (vault.status !== pb.SofiVaultStatus.ACTIVE || !same(vault.tokenPolicyCommit, object) || vault.amount !== BigInt(share.amount)) return false;
    if (record.cell !== "" && hex(vault.verdictCell) !== record.cell) return false;
    const names = [...record.factions, VOID];
    if (vault.outcomes.length !== names.length) return false;
    return names.every((name) => {
      const outcome = vault.outcomes.find((o) => new TextDecoder().decode(o.outcome) === name);
      const [decides] = outcome?.signers ?? [];
      if (outcome === undefined || decides === undefined || outcome.signers.length !== 1) return false;
      if (decides.signatureAlg !== publisher.signatureAlg || !same(decides.publicKey, publisher.signingKey)) return false;
      const pays = name === share.factionId ? { genesis: unhex(share.genesis, "genesis"), deviceId: unhex(share.deviceId, "device id") } : publisher;
      return same(outcome.recipientGenesis, pays.genesis) && same(outcome.recipientDeviceId, pays.deviceId);
    });
  }

  /** Lock every share of a match that is not locked yet. */
  private async lock(matchId: string): Promise<void> {
    const { app, store } = this.options;
    const record = this.record(matchId);
    if (record.opened) return;
    const publisher = await this.options.party();
    const names = [...record.factions, VOID];
    // A vault the account locked for this match that the server did not get to record
    // (it stopped between the two) is taken up here, so no share is locked twice.
    const recorded = new Set(Object.values(store.data.matches).flatMap((m) => m.shares.map((s) => s.vault)));
    const loose = (await this.own()).filter((v) => !recorded.has(hex(v.vaultId)));
    for (const share of record.shares) {
      if (share.vault !== "") continue;
      const object = (await app.object(share.resource)).raw;
      const member = { genesis: unhex(share.genesis, "genesis"), deviceId: unhex(share.deviceId, "device id") };
      const at = loose.findIndex((v) => this.isShare(v, record, share, object, publisher));
      const [found] = at >= 0 ? loose.splice(at, 1) : [];
      const locked =
        found !== undefined
          ? { vault: found.vaultId, cell: found.verdictCell }
          : await lockVault(
              app,
              Matches.external(matchId),
              object,
              String(share.amount),
              publisher,
              names.map((name) => ({ outcome: name, pays: name === share.factionId ? member : publisher })),
            );
      if (record.cell === "") record.cell = hex(locked.cell);
      else if (record.cell !== hex(locked.cell)) throw new Error(`a share of match ${matchId} was locked on another verdict cell`);
      share.vault = hex(locked.vault);
      store.save();
    }
    record.opened = true;
    store.save();
  }

  /**
   * Decide a match: `outcome` is the winning faction, or `void`. The
   * publisher's key decides once, on the match's one verdict cell, and every
   * vault settles on it. The first final verdict stands: a match already
   * decided otherwise is refused, and nothing changes. The publisher then
   * takes back the shares the verdict pays it.
   */
  async decide(matchId: string, outcome: string): Promise<MatchView> {
    const { app, store } = this.options;
    const record = this.record(matchId);
    if (!record.opened) throw new Refused(`match ${matchId}'s prize is not all locked yet`);
    if (![...record.factions, VOID].includes(outcome)) throw new Refused("that is not an outcome of the match");
    if (record.outcome === "") {
      const [first] = record.shares;
      if (first === undefined) throw new Error(`match ${matchId} has no shares`);
      const vault = unhex(first.vault, "a vault");
      let held = await decideVault(app, vault, outcome);
      const deadline = Date.now() + FINAL_MS;
      while (!held.final) {
        if (Date.now() > deadline) throw new Error(`match ${matchId}'s verdict is not final yet`);
        await new Promise((resolve) => setTimeout(resolve, 500));
        held = await verdict(app, vault);
      }
      record.outcome = held.outcome;
      store.save();
    }
    if (record.outcome !== outcome) throw new Refused(`match ${matchId} is already decided: ${record.outcome}`);
    await this.reclaim(matchId);
    return this.read(matchId);
  }

  /** Take back each share of a decided match that the verdict pays the publisher. How many are still to come back. */
  private async reclaim(matchId: string): Promise<number> {
    const { app, store } = this.options;
    const record = this.record(matchId);
    await this.read(matchId);
    let left = 0;
    for (const share of record.shares) {
      if (share.released || share.factionId === record.outcome) continue;
      try {
        if (await releaseVault(app, unhex(share.vault, "a vault"))) {
          share.released = true;
          store.save();
          continue;
        }
      } catch (e) {
        if (!(e instanceof IngressError)) throw e;
        console.error(`[publisher] match ${matchId}: a share did not come back yet: ${e.detail}`);
      }
      left += 1;
    }
    return left;
  }

  /** Take back what decided matches still owe the publisher. How many shares are still to come back. */
  async tick(): Promise<number> {
    let left = 0;
    for (const [matchId, record] of Object.entries(this.options.store.data.matches)) {
      if (record.outcome === "" || record.shares.every((s) => s.released || s.factionId === record.outcome)) continue;
      left += await this.reclaim(matchId);
    }
    return left;
  }

  /** A match and its prize, each share as DSM holds it now. */
  async read(matchId: string): Promise<MatchView> {
    const { app, store } = this.options;
    const record = this.record(matchId);
    const status = new Map<string, pb.SofiVaultStatus>();
    let others = 0;
    if (record.cell !== "" && record.shares.some((s) => s.vault !== "" && !s.released)) {
      const mine = new Set(record.shares.map((s) => s.vault));
      for (const vault of (await lockedVaults(app, unhex(record.cell, "a verdict cell"))).vaults) {
        if (mine.has(hex(vault.vaultId))) status.set(hex(vault.vaultId), vault.status);
        else others += 1;
      }
    }
    let changed = false;
    const shares = record.shares.map((share): ShareView => {
      const won = record.outcome === share.factionId;
      let state: ShareView["state"];
      if (share.vault === "") state = "not locked";
      else if (share.released) state = won ? "collected" : "returned";
      else {
        const held = status.get(share.vault);
        if (held === undefined) state = "not yet known";
        else if (held === pb.SofiVaultStatus.ACTIVE) state = record.outcome === "" ? "locked" : won ? "waiting" : "returning";
        else {
          share.released = true;
          changed = true;
          state = won ? "collected" : "returned";
        }
      }
      return { factionId: share.factionId, signingKey: share.signingKey, resource: share.resource, amount: share.amount, state };
    });
    if (changed) store.save();
    return {
      matchId,
      factions: record.factions,
      outcome: record.outcome,
      cell: record.cell === "" ? "" : encodeB32(unhex(record.cell, "a verdict cell")),
      opened: record.opened,
      shares,
      others,
    };
  }

  /** What the wallet with `signingKey` has won and not collected yet, across every decided match. */
  async waiting(signingKey: string): Promise<Waiting[]> {
    const waiting: Waiting[] = [];
    for (const [matchId, record] of Object.entries(this.options.store.data.matches)) {
      if (record.outcome === "" || record.outcome === VOID) continue;
      if (!record.shares.some((s) => s.signingKey === signingKey && s.factionId === record.outcome && !s.released)) continue;
      // every winner's game asks this every few seconds: one read of the cell serves them all for a moment
      const held = this.#read.get(matchId);
      const view = held !== undefined && Date.now() - held.at < WAITING_FRESH_MS ? held.view : await this.read(matchId);
      if (view !== held?.view) this.#read.set(matchId, { at: Date.now(), view });
      for (const share of view.shares) {
        if (share.signingKey === signingKey && share.state === "waiting") {
          waiting.push({ matchId, cell: view.cell, resource: share.resource, amount: share.amount });
        }
      }
    }
    return waiting;
  }
}
