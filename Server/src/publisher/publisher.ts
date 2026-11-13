// The publisher's server (spec 4.4, 7.6 and the working notes): it holds the
// publisher's account on dsm-app-host, signs parcel assignments, asks members'
// wallets to write their changes, keeps the clock for a leaver's 48 hours,
// records destruction and turns a fully funded plan into a built structure.
// It opens matches, locks their prizes and decides who won (spec 10.6). It
// never holds a player's keys or materials, and it never decides the order
// of a faction's changes: the leader of each successor cell does.

import { ask, grant, type AppAccount, type Connection, type Offer, type RequestRef, type Result } from "dsm-app-client";
import { canonical, digest, hex, unhex, type Canonical } from "../faction/canonical.js";
import type { Catalogue } from "../faction/catalogue.js";
import { encodeChange, type Change } from "../faction/change.js";
import { Mirror, type WalkEnd } from "../faction/mirror.js";
import type { Parcel } from "../faction/parcel.js";
import {
  checkFounding,
  factionId as factionIdOf,
  firstState,
  foundingValue,
  readFounding,
  type FactionState,
  type Founding,
  type Member,
} from "../faction/state.js";
import { judge } from "../faction/validity.js";
import { AccountCells, leaderUnread } from "../dsm/cells.js";
import { Contributions, type Funding } from "../dsm/contributions.js";
import { party, publishAuthored, readAuthored, type Party } from "../dsm/routes.js";
import { Matches } from "./matches.js";
import { Refused } from "./refused.js";
import { Shop, type Purse } from "./shop.js";
import type { Store } from "./store.js";
import type { Economy, MatchWorld, ShopWorld } from "./world.js";

/** How many times a wallet is asked for a proof before the game says it proved nothing. */
const PROOF_ATTEMPTS = 5;
/** How long an answered proof is given to be established before the wallet is asked again. */
const PROOF_SETTLE_MS = 2_000;

/** How long a leaver's materials stay committed after they go (spec 3): 48 hours. */
export const NOTICE_MS = 48 * 60 * 60 * 1000;

/** What a player is told when a change waits on the successor cell's leader seat (spec 11). */
export const PAUSED =
  "Building is paused: the storage seat that leads the next change cannot be reached. The base is unchanged; building resumes when the seat returns.";

export interface PublisherOptions {
  /** The publisher's account, through its dsm-app-host's ingress. */
  app: AppAccount;
  store: Store;
  catalogue: Catalogue;
  parcels: Map<string, Parcel>;
  /** The game's currency, the vault that sells it and the shop (spec 10.5). */
  shop: ShopWorld;
  /** What a match pays (spec 10.6). */
  matches: MatchWorld;
  /** The five member ids of the storage set the account is on. */
  storageSet: string[];
  /** The name wallets show for the game. */
  name: string;
  /** The server's clock, in milliseconds. */
  now: () => number;
}

/** A parcel assignment as the publisher signed it and storage holds it. */
export interface Assignment {
  /** The assignment's address (lowercase hex). */
  address: string;
  parcelId: string;
  factionId: string;
  founding: Founding;
}

/** What a player is told at a server that met a change it did not carry (docs/dsm-gaps.md, 1). */
export const BEHIND =
  "This server has not read the newest state of the base: a change it did not carry holds the next cell. Nothing can be built through it until it has.";

/** Whether a member can build now, as far as the server's reading of the successor cell goes, and what to tell them if not. */
export function buildingAt(end: WalkEnd): { open: boolean; message: string } {
  if (end.at !== "not_established") return { open: true, message: "" };
  if (end.waits === "leader") return { open: false, message: PAUSED };
  if (end.waits === "occupant") return { open: false, message: BEHIND };
  // what the account said stays in the server's own log: it names cells, and is no player's business
  return { open: false, message: "Building waits: the base's next state is not established yet." };
}

/** What became of a proposed change. */
export type Proposed =
  /** It holds the successor cell and was applied. */
  | { taken: true; commitment: string }
  /**
   * Another change holds the cell, or the wallet did not write. The mirror is at `commitment`.
   * `paused` when nothing holds the cell because its leader seat could not be reached: the same
   * change can be asked again once the seat returns (spec 6.7, 11).
   */
  | { taken: false; why: string; commitment: string; paused?: true };

/** A structure as the game shows it (spec 7.6, with the owner's ruling that a built structure stands through a withdrawal). */
export interface Structure {
  instanceId: string;
  prefabId: string;
  x: number;
  y: number;
  rotation: number;
  config: Record<string, string>;
  /** Whether it stands in play. A plan is visible only in the faction's editor. */
  standing: boolean;
  /** Whether a battle destroyed it and no member has rebuilt it yet. */
  destroyed: boolean;
  funding: Funding;
}

export class Publisher {
  readonly cells: AccountCells;
  readonly contributions: Contributions;
  readonly shop: Shop;
  readonly matches: Matches;
  readonly #mirrors = new Map<string, Mirror>();
  readonly #walking = new Map<string, Promise<void>>();
  #party: Party | undefined;

  constructor(private readonly options: PublisherOptions) {
    this.cells = new AccountCells(options.app, options.store);
    this.contributions = new Contributions(options.app, options.store, options.catalogue);
    this.shop = new Shop(options.app, options.store, options.shop);
    this.matches = new Matches({
      app: options.app,
      store: options.store,
      world: options.matches,
      members: async (factionId) => (await this.follow(factionId)).mirror.state.members,
      party: () => this.party(),
      now: options.now,
    });
  }

  get app(): AppAccount {
    return this.options.app;
  }

  get catalogue(): Catalogue {
    return this.options.catalogue;
  }

  /** The publisher's account as DSM names it. */
  async party(): Promise<Party> {
    return (this.#party ??= await party(this.options.app));
  }

  /**
   * Issue each resource the account has not issued yet, with its fixed
   * maximum supply (spec 8.1, 8.5), and D-bucks with theirs, and open the
   * vault that sells D-bucks for ERA (spec 10.5). The account then holds each
   * whole supply but what it put in the vault.
   */
  async issueResources(economy: Economy): Promise<void> {
    const owned = new Set((await this.options.app.owned()).map((o) => o.object.symbol));
    for (const resource of economy.resources) {
      if (owned.has(resource.symbol)) continue;
      await this.options.app.issue({ symbol: resource.symbol, name: resource.name, supply: resource.supply });
    }
    await this.shop.open();
  }

  /**
   * The connect code a player's wallet reads. The grant lets the game ask the
   * wallet, without asking the player again, to sign and store changes to the
   * faction's state, to commit materials within caps and take them back, to
   * trade ERA for D-bucks through the publisher's vault and pay D-bucks to the
   * shop within caps, to prove what it owns of the materials, D-bucks and ERA,
   * and to take in objects the publisher issued.
   */
  async offer(caps: { symbol: string; perRequest: string; total: string }[]): Promise<Offer> {
    return this.options.app.offer({
      name: this.options.name,
      grants: [
        grant.state(),
        grant.contribute(caps.map((c) => ({ object: c.symbol, perRequest: c.perRequest, total: c.total }))),
        grant.owns([...caps.map((c) => c.symbol), this.shop.symbol, "ERA"]),
        grant.accept(),
        ...this.shop.grants(),
      ],
      adopt: [...caps.map((c) => c.symbol), this.shop.symbol],
      freeRequests: 4096,
    });
  }

  /** The member a connected wallet would be: its genesis, device id and signing key. */
  member(connection: Connection): Member {
    return {
      genesis: hex(connection.opponent.genesis),
      deviceId: hex(connection.opponent.deviceId),
      signingKey: hex(connection.opponent.signingKey),
    };
  }

  parcel(parcelId: string): Parcel {
    const found = this.options.parcels.get(parcelId);
    if (found === undefined) throw new Refused(`the registry has no parcel ${parcelId}`);
    return found;
  }

  /** What a faction founded now on `parcelId` by `members` would be founded from. */
  founding(parcelId: string, members: Member[], nonce: string): Founding {
    const founding: Founding = {
      storageSet: [...this.options.storageSet],
      parcelId,
      catalogue: this.options.catalogue.version,
      members,
      nonce,
    };
    checkFounding(founding);
    return founding;
  }

  static assignmentTopic(parcelId: string): Uint8Array {
    return digest("sennite/parcel-assignment/v1", new TextEncoder().encode(parcelId));
  }

  /**
   * The assignment of `parcelId`, read from storage: the first object the
   * publisher's account published for the parcel, checked by the account from
   * the object's own bytes. Undefined while the parcel is unassigned. A later
   * object for the same parcel assigns nothing (spec 4.3).
   */
  async assignment(parcelId: string): Promise<Assignment | undefined> {
    const publisher = await this.party();
    const read = await readAuthored(this.options.app, publisher.deviceId, Publisher.assignmentTopic(parcelId));
    if (!read.complete) throw new Error(`not every object under parcel ${parcelId}'s assignment could be read yet`);
    const first = read.objects[0];
    if (first === undefined) return undefined;
    const raw = JSON.parse(new TextDecoder().decode(first.payload)) as Record<string, unknown>;
    const founding = readFounding(raw.founding);
    const named = factionIdOf(founding);
    if (raw.v !== 1 || raw.parcelId !== parcelId || founding.parcelId !== parcelId || raw.factionId !== named) {
      throw new Error(`the object under parcel ${parcelId}'s assignment does not assign it to the faction it founds`);
    }
    return { address: hex(first.address), parcelId, factionId: named, founding };
  }

  /**
   * Assign `founding`'s parcel to the faction it founds: the publisher's
   * signed act (spec 4.3). The parcel must be in the registry and unassigned.
   * Returns the faction's first state R_0.
   */
  async assign(founding: Founding): Promise<FactionState> {
    checkFounding(founding);
    this.parcel(founding.parcelId);
    if (founding.catalogue !== this.options.catalogue.version) throw new Error("the founding names another catalogue version");
    if ((await this.assignment(founding.parcelId)) !== undefined) {
      throw new Refused(`parcel ${founding.parcelId} is already assigned`);
    }
    const factionId = factionIdOf(founding);
    const payload: Canonical = { factionId, founding: foundingValue(founding), parcelId: founding.parcelId, v: 1 };
    await publishAuthored(this.options.app, Publisher.assignmentTopic(founding.parcelId), canonical(payload));
    const held = await this.assignment(founding.parcelId);
    if (held === undefined || held.factionId !== factionId) {
      throw new Refused(`parcel ${founding.parcelId} is already assigned: another faction was first`);
    }
    this.options.store.data.factions[factionId] ??= { parcelId: founding.parcelId, notices: {}, contributions: {}, play: {} };
    this.options.store.save();
    return firstState(founding, held.address);
  }

  /**
   * A new mirror of the faction on `parcelId`, at R_0. Its first state is
   * valid only under the publisher's assignment, read from storage (spec 7.1).
   */
  async mirrorAt(parcelId: string): Promise<Mirror> {
    const assignment = await this.assignment(parcelId);
    if (assignment === undefined) throw new Error(`parcel ${parcelId} is not assigned to any faction`);
    return Mirror.at(assignment.founding, assignment.address, this.options.catalogue, this.parcel(parcelId));
  }

  /**
   * The server's own mirror of a faction, walked to the latest state it can
   * read. One walk at a time: two walks of one mirror would each take the
   * same occupant.
   */
  async follow(factionId: string): Promise<{ mirror: Mirror; end: WalkEnd }> {
    const earlier = this.#walking.get(factionId) ?? Promise.resolve();
    const walk = earlier.then(
      () => this.walk(factionId),
      () => this.walk(factionId),
    );
    this.#walking.set(
      factionId,
      walk.then(
        () => undefined,
        () => undefined,
      ),
    );
    return walk;
  }

  private async walk(factionId: string): Promise<{ mirror: Mirror; end: WalkEnd }> {
    let mirror = this.#mirrors.get(factionId);
    if (mirror === undefined) {
      mirror = await this.mirrorAt(this.options.store.faction(factionId).parcelId);
      this.#mirrors.set(factionId, mirror);
    }
    const before = mirror.height;
    // Members only leave on this walk, so whoever leaves is listed here.
    const listed = [...mirror.state.members];
    const end = await mirror.walk(this.cells);
    this.noteLeavers(factionId, mirror, before, listed);
    return { mirror, end };
  }

  /** Start the 48-hour clock for each member whose own change left the faction since `from`. */
  private noteLeavers(factionId: string, mirror: Mirror, from: number, listed: Member[]): void {
    const record = this.options.store.faction(factionId);
    let noted = false;
    for (const entry of mirror.lineage.slice(from)) {
      if (!entry.valid || entry.change?.kind !== "leave" || record.notices[entry.signerKey] !== undefined) continue;
      const leaver = listed.find((m) => m.signingKey === entry.signerKey);
      if (leaver === undefined) throw new Error("a member left whom the state did not list");
      record.notices[entry.signerKey] = { genesis: leaver.genesis, deviceId: leaver.deviceId, leftAt: this.options.now(), returnedAt: 0 };
      noted = true;
    }
    if (noted) this.options.store.save();
  }

  /**
   * Ask the wallet on `connection` to sign `change` against the faction's
   * current state and write it at the successor cell (spec 6.4, steps 1 to 3).
   * A change that is invalid against that state is refused here, before
   * anything is written, and no cell is occupied.
   */
  async propose(factionId: string, connection: Connection, change: Change): Promise<RequestRef> {
    const { mirror, end } = await this.follow(factionId);
    // A change this server did not carry holds the next cell: a write asked there would count for nothing.
    if (end.at === "not_established" && end.waits === "occupant") throw new Refused(BEHIND);
    const payload = encodeChange({ factionId, parent: mirror.commitment, change });
    const signerKey = hex(connection.opponent.signingKey);
    const verdict = judge(mirror.state, mirror.commitment, mirror.catalogue, mirror.parcel, signerKey, payload);
    if (!verdict.valid) throw new Refused(verdict.why);
    const { name, writers } = mirror.successorCell();
    return this.cells.ask(connection, name, writers, payload);
  }

  /**
   * Wait for a proposed change to settle, then walk the mirror: whether the
   * change holds its cell, and the state the faction is at now.
   */
  async settle(factionId: string, ref: RequestRef, timeoutMs = 240_000): Promise<Proposed> {
    const deadline = this.options.now() + timeoutMs;
    let result: Result;
    for (;;) {
      result = await this.options.app.result(ref);
      const cellDecided = result.state?.step !== undefined;
      if (result.fact === "written" || cellDecided || result.outcome === "declined" || result.outcome === "failed") break;
      if (this.options.now() > deadline) break;
      await new Promise((resolve) => setTimeout(resolve, 500));
    }
    const { mirror } = await this.follow(factionId);
    if (result.fact === "written") return { taken: true, commitment: mirror.commitment };
    // The account's own reading of the cell, not the wallet's word, says the leader was not read.
    if (result.state?.step === undefined && leaderUnread(result.detail)) {
      return { taken: false, paused: true, why: PAUSED, commitment: mirror.commitment };
    }
    const why =
      result.state?.step !== undefined
        ? "another change holds the cell"
        : result.reason || result.detail || `the wallet's answer is ${result.outcome}`;
    return { taken: false, why, commitment: mirror.commitment };
  }

  /** Propose a change and wait for what became of it. */
  async change(factionId: string, connection: Connection, change: Change): Promise<Proposed> {
    return this.settle(factionId, await this.propose(factionId, connection, change));
  }

  /** What a contribution to a structure is committed to: the faction and the structure's instance_id. */
  static commitment(factionId: string, instanceId: string): Uint8Array {
    const tag = new TextEncoder().encode("sennite/contribution/v1");
    const out = new Uint8Array(tag.length + 32 + 16);
    out.set(tag, 0);
    out.set(unhex(factionId, "faction_id"), tag.length);
    out.set(unhex(instanceId, "instance_id"), tag.length + 32);
    return out;
  }

  /**
   * Ask the wallet on `connection` to commit `amount` of `resource` to a
   * structure (spec 8.2). The wallet builds the terms itself; the game names
   * only what the amount is committed to, the object and the amount.
   */
  async commit(factionId: string, connection: Connection, instanceId: string, resource: string, amount: string): Promise<RequestRef> {
    const { mirror } = await this.follow(factionId);
    if (!mirror.state.placed.some((p) => p.instanceId === instanceId)) throw new Refused(`instance ${instanceId} does not exist`);
    if (!this.options.catalogue.resources.includes(resource)) throw new Refused(`${resource} is not a resource`);
    const to = Publisher.commitment(factionId, instanceId);
    return this.options.app.request(connection, ask.contribution.commit(to, resource, amount));
  }

  /**
   * Wait for a commitment to settle. Once the publisher's account has
   * established it (the wallet's Active vault holds the amount under exactly
   * the contribution terms), the server records where to read it again.
   */
  async committed(factionId: string, connection: Connection, instanceId: string, ref: RequestRef): Promise<Result> {
    const result = await this.options.app.wait(ref);
    if (result.fact === "contributed" && result.contribution !== undefined) {
      const owner = hex(connection.opponent.signingKey);
      this.contributions.record(factionId, instanceId, owner, result.contribution.cell, result.contribution.contributions);
    }
    return result;
  }

  /** Commit to a structure and wait for what the publisher's account established. */
  async contribute(factionId: string, connection: Connection, instanceId: string, resource: string, amount: string): Promise<Result> {
    return this.committed(factionId, connection, instanceId, await this.commit(factionId, connection, instanceId, resource, amount));
  }

  /** Ask the wallet on `connection` to take contributions back (spec 8.4: withdraw, or release after a dismantle). */
  async takeBack(connection: Connection, vaults: Uint8Array[]): Promise<Result> {
    return this.options.app.wait(await this.options.app.request(connection, ask.contribution.takeBack(vaults)));
  }

  /**
   * Ask the wallet on `connection` to take back everything it still has
   * committed to one structure. Undefined when it has nothing there.
   */
  async withdraw(factionId: string, connection: Connection, instanceId: string): Promise<Result | undefined> {
    const owner = hex(connection.opponent.signingKey);
    const vaults = await this.contributions.activeVaultsOf(factionId, owner, await this.party(), instanceId);
    if (vaults.length === 0) return undefined;
    // A request names at most eight contributions.
    let last: Result | undefined;
    for (let i = 0; i < vaults.length; i += 8) {
      last = await this.takeBack(connection, vaults.slice(i, i + 8));
      if (last.fact !== "taken_back") return last;
    }
    return last;
  }

  /**
   * What the wallet on `connection` owns, by object id, from a proof the
   * wallet gives and the publisher's account verifies (spec 10.2: a holding
   * counts only once it is proven, never on a client's say-so). It covers the
   * resources, D-bucks, ERA and every skin the shop has sold. Undefined when
   * no proof was established.
   */
  async proven(connection: Connection): Promise<Map<string, bigint> | undefined> {
    // a proof covers the objects it is asked about: the skins are asked about one by one, each by its object id
    const asked = [...this.options.catalogue.resources, this.shop.symbol, "ERA", ...Object.keys(this.options.store.data.shop.skins)];
    // A proof stands only while the wallet has not moved on from the position it was made at. A
    // wallet that takes something in just after answering leaves a proof that establishes nothing:
    // that is no answer about what it holds, so the wallet is asked again.
    for (let attempt = 0; attempt < PROOF_ATTEMPTS; attempt++) {
      const ref = await this.options.app.request(connection, ask.owns(asked));
      const answered = await this.options.app.wait(ref, { until: "answered" });
      if (answered.outcome !== "carried_out") return undefined;
      const deadline = this.options.now() + PROOF_SETTLE_MS;
      for (;;) {
        const result = await this.options.app.result(ref);
        if (result.fact === "owns" && result.owns !== undefined) return new Map(result.owns.map((o) => [o.object, o.amount]));
        if (this.options.now() > deadline) break;
        await new Promise((resolve) => setTimeout(resolve, 250));
      }
    }
    return undefined;
  }

  /** What a proof says the wallet owns of each resource. */
  private async materialsAmong(proven: Map<string, bigint>): Promise<Record<string, number>> {
    const materials: Record<string, number> = {};
    for (const symbol of this.options.catalogue.resources) {
      materials[symbol] = Number(proven.get((await this.options.app.object(symbol)).id) ?? 0n);
    }
    return materials;
  }

  /** What the wallet on `connection` proves it owns of each resource. */
  async materials(connection: Connection): Promise<Record<string, number> | undefined> {
    const proven = await this.proven(connection);
    return proven === undefined ? undefined : this.materialsAmong(proven);
  }

  /** Everything the game shows of a wallet, from one proof: its materials, its D-bucks and ERA, and its skins. */
  async holdings(connection: Connection): Promise<({ materials: Record<string, number> } & Purse) | undefined> {
    const proven = await this.proven(connection);
    if (proven === undefined) return undefined;
    return { materials: await this.materialsAmong(proven), ...(await this.shop.purse(proven)) };
  }

  /** The faction that lists `signingKey` as a member now, among those the server has assigned parcels to. */
  async factionOf(signingKey: string): Promise<{ factionId: string; parcelId: string } | undefined> {
    for (const [factionId, record] of Object.entries(this.options.store.data.factions)) {
      const { mirror } = await this.follow(factionId);
      if (mirror.state.members.some((m) => m.signingKey === signingKey)) return { factionId, parcelId: record.parcelId };
    }
    return undefined;
  }

  /** The factions that list `signingKey` as invited now. */
  async invitationsOf(signingKey: string): Promise<{ factionId: string; parcelId: string }[]> {
    const invitations = [];
    for (const [factionId, record] of Object.entries(this.options.store.data.factions)) {
      const { mirror } = await this.follow(factionId);
      if (mirror.state.invited.some((m) => m.signingKey === signingKey)) invitations.push({ factionId, parcelId: record.parcelId });
    }
    return invitations;
  }

  /** The faction_id assigned to a parcel, from the server's record; undefined while it is unassigned. */
  factionOn(parcelId: string): string | undefined {
    return Object.entries(this.options.store.data.factions).find(([, r]) => r.parcelId === parcelId)?.[0];
  }

  /** The funding of every placed structure of a faction, read now (spec 8.3). */
  async funding(factionId: string): Promise<Map<string, Funding>> {
    const { mirror } = await this.follow(factionId);
    const record = this.options.store.faction(factionId);
    const publisher = await this.party();
    const leavers = Object.entries(record.notices).map(([signingKey, n]) => ({ signingKey, genesis: n.genesis, deviceId: n.deviceId }));
    return this.contributions.funding(factionId, mirror.state, leavers, publisher);
  }

  /**
   * The base as the game shows it: every plan, whether it stands in play, and
   * its funding. A plan that becomes fully funded is built. A built structure
   * keeps standing when its funding falls and shows its deficit (owner's
   * ruling of 2026-11-12). A structure destroyed in battle is a plan again
   * until a member confirms or replaces it (spec 7.6).
   */
  async base(factionId: string): Promise<{ commitment: string; height: number; structures: Structure[] }> {
    const { mirror } = await this.follow(factionId);
    const funding = await this.funding(factionId);
    const record = this.options.store.faction(factionId);
    let changed = false;
    const placed = new Set(mirror.state.placed.map((p) => p.instanceId));
    for (const instanceId of Object.keys(record.play)) {
      if (!placed.has(instanceId)) {
        delete record.play[instanceId];
        changed = true;
      }
    }
    const structures = mirror.state.placed.map((p): Structure => {
      const play = (record.play[p.instanceId] ??= { standing: "", destroyedAt: -1 });
      const before = JSON.stringify(play);
      // Replaced by another prefab: what stood is no longer what is planned.
      if (play.standing !== "" && play.standing !== p.prefabId) play.standing = "";
      if (play.destroyedAt >= 0) {
        const rebuilt = mirror.lineage.slice(play.destroyedAt).some((entry) => {
          const c = entry.change;
          return entry.valid && c !== undefined && (c.kind === "confirm" || c.kind === "replace") && c.instanceId === p.instanceId;
        });
        if (rebuilt) play.destroyedAt = -1;
      }
      const f = funding.get(p.instanceId);
      if (f === undefined) throw new Error(`no funding was read for ${p.instanceId}`);
      if (play.standing === "" && play.destroyedAt < 0 && f.status === "fully funded") play.standing = p.prefabId;
      if (JSON.stringify(play) !== before) changed = true;
      return {
        instanceId: p.instanceId,
        prefabId: p.prefabId,
        x: p.x,
        y: p.y,
        rotation: p.rotation,
        config: p.config,
        standing: play.standing !== "",
        destroyed: play.destroyedAt >= 0,
        funding: f,
      };
    });
    if (changed) this.options.store.save();
    return { commitment: mirror.commitment, height: mirror.height, structures };
  }

  /**
   * Record that a battle destroyed a built structure (spec 7.6): a game
   * event, not a faction-state change. The plan and its funding are unchanged.
   */
  async destroyed(factionId: string, instanceId: string): Promise<void> {
    const { mirror } = await this.follow(factionId);
    const record = this.options.store.faction(factionId);
    const play = record.play[instanceId];
    if (play === undefined || play.standing === "") throw new Refused(`${instanceId} is not standing in play`);
    play.standing = "";
    play.destroyedAt = mirror.height;
    this.options.store.save();
  }

  /**
   * The 48-hour clock (spec 8.4): for each leaver whose notice has run, the
   * publisher's key decides `return` on each of their vaults that is still
   * Active, which lets the leaver's wallet take the units back. It decides
   * when they go back, never to whom. Returns the vaults it returned.
   */
  async tick(): Promise<string[]> {
    const returned: string[] = [];
    for (const [factionId, record] of Object.entries(this.options.store.data.factions)) {
      for (const [signingKey, notice] of Object.entries(record.notices)) {
        if (notice.returnedAt !== 0 || this.options.now() - notice.leftAt < NOTICE_MS) continue;
        const publisher = await this.party();
        for (const vault of await this.contributions.activeVaultsOf(factionId, signingKey, publisher)) {
          await this.options.app.returnContribution(vault);
          returned.push(hex(vault));
        }
        notice.returnedAt = this.options.now();
        this.options.store.save();
      }
    }
    return returned;
  }
}

export { Refused };
