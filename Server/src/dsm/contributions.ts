// Contributions and the funding read (spec 8.2, 8.3; DSM Amendment A35).
//
// A contribution is the member's own escrow vault. Its terms are the wallet's
// own contribution template: `return`, decided by the publisher's key, and
// `withdraw`, decided by the owner's key, both paying the vault's owner.
// Faction state records no contribution: a structure's funding is read from
// the vaults committed to it, each time it is wanted.
//
// As DSM built it, each owner's contributions to one structure sit at that
// owner's own verdict cell. The server learns a cell when the publisher's
// account establishes a commitment made there, and reads every vault bound to
// it from then on (`escrow.locked`).

import { pb, type AppAccount } from "dsm-app-client";
import { hex, unhex } from "../faction/canonical.js";
import type { Catalogue } from "../faction/catalogue.js";
import type { FactionState, Member } from "../faction/state.js";
import type { Store } from "../publisher/store.js";
import { lockedVaults, type Party } from "./routes.js";

const RETURN = "return";
const WITHDRAW = "withdraw";

/** One vault found on a structure's cells, and whether it counts toward funding. */
export interface Contribution {
  /** The vault (lowercase hex). */
  vault: string;
  /** The signing key that decides its withdraw (lowercase hex); empty when its terms name none. */
  owner: string;
  /** The resource's symbol; empty when the vault holds something that is not a resource. */
  resource: string;
  amount: number;
  /** Whether the vault is Active: its units are in it. */
  active: boolean;
  /** Whether it meets all four conditions of spec 8.3. */
  counts: boolean;
  /** Whether it counts only because its owner left by their own change and the 48-hour notice has not run. */
  leaving: boolean;
  /** Why it does not count; empty when it does. */
  why: string;
}

export interface Funding {
  /** `not yet known` while a read that could change the answer has not completed (spec 8.3). */
  status: "fully funded" | "needs resources" | "not yet known";
  /** Each resource of the recipe: what it needs, what is funded (never more than needed) and the shortfall. */
  resources: { resource: string; needed: number; funded: number; shortfall: number }[];
  contributions: Contribution[];
  /** The verdict cells whose vaults could not all be established. */
  notEstablished: string[];
}

const same = (a: Uint8Array, b: Uint8Array): boolean => a.length === b.length && a.every((x, i) => x === b[i]);

export class Contributions {
  #resources: Map<string, string> | undefined;

  constructor(
    private readonly app: AppAccount,
    private readonly store: Store,
    private readonly catalogue: Catalogue,
  ) {}

  /** Record where a commitment the account established can be read again. */
  record(factionId: string, instanceId: string, owner: string, cell: Uint8Array, vaults: Uint8Array[]): void {
    const list = (this.store.faction(factionId).contributions[instanceId] ??= []);
    for (const vault of vaults) {
      if (!list.some((c) => c.vault === hex(vault))) list.push({ vault: hex(vault), cell: hex(cell), owner });
    }
    this.store.save();
  }

  /** The resources' object ids (lowercase hex) and the symbol of each. */
  private async resources(): Promise<Map<string, string>> {
    if (this.#resources !== undefined) return this.#resources;
    const map = new Map<string, string>();
    for (const symbol of this.catalogue.resources) map.set(hex((await this.app.object(symbol)).raw), symbol);
    return (this.#resources = map);
  }

  /**
   * Whether `vault` is a contribution under exactly the terms of spec 8.2 for
   * this publisher: two outcomes, `return` decided by the publisher's key
   * alone and `withdraw` by one other key alone, both paying the vault's own
   * owner. The key that decides its withdraw, or why it is not one.
   */
  static terms(vault: pb.EscrowVaultV1, publisher: Party): { owner: string } | { why: string } {
    if (vault.outcomes.length !== 2) return { why: "its terms are not the two outcomes of a contribution" };
    const named = (name: string) => vault.outcomes.find((o) => new TextDecoder().decode(o.outcome) === name);
    const [back, withdraw] = [named(RETURN), named(WITHDRAW)];
    if (back === undefined || withdraw === undefined) return { why: "its terms are not the two outcomes of a contribution" };
    for (const outcome of [back, withdraw]) {
      if (!same(outcome.recipientGenesis, vault.ownerGenesis) || !same(outcome.recipientDeviceId, vault.ownerDeviceId)) {
        return { why: "one of its outcomes pays someone other than its owner" };
      }
    }
    const [decidesReturn] = back.signers;
    if (
      back.signers.length !== 1 ||
      decidesReturn === undefined ||
      decidesReturn.signatureAlg !== publisher.signatureAlg ||
      !same(decidesReturn.publicKey, publisher.signingKey)
    ) {
      return { why: "its return is not decided by the publisher's key alone" };
    }
    const [decidesWithdraw] = withdraw.signers;
    if (withdraw.signers.length !== 1 || decidesWithdraw === undefined) return { why: "its withdraw is not decided by one key" };
    if (decidesWithdraw.signatureAlg !== publisher.signatureAlg) return { why: "its withdraw is not decided by a device key" };
    if (same(decidesWithdraw.publicKey, publisher.signingKey)) return { why: "its withdraw is decided by the publisher" };
    return { owner: hex(decidesWithdraw.publicKey) };
  }

  /** Every vault bound to the cells recorded for one structure, each judged against spec 8.3. */
  private async read(
    factionId: string,
    instanceId: string,
    state: FactionState,
    leavers: Member[],
    publisher: Party,
  ): Promise<{ contributions: Contribution[]; notEstablished: string[] }> {
    const recorded = this.store.faction(factionId).contributions[instanceId] ?? [];
    const resources = await this.resources();
    const contributions: Contribution[] = [];
    const notEstablished: string[] = [];
    for (const cell of new Set(recorded.map((c) => c.cell))) {
      let read: Awaited<ReturnType<typeof lockedVaults>>;
      try {
        read = await lockedVaults(this.app, unhex(cell, "a verdict cell"));
      } catch {
        notEstablished.push(cell);
        continue;
      }
      if (!read.complete) notEstablished.push(cell);
      for (const vault of read.vaults) {
        const terms = Contributions.terms(vault, publisher);
        const resource = resources.get(hex(vault.tokenPolicyCommit)) ?? "";
        const owner = "owner" in terms ? terms.owner : "";
        let why = "";
        let leaving = false;
        if ("why" in terms) why = terms.why;
        else if (vault.status !== pb.SofiVaultStatus.ACTIVE) why = "it is not Active: its units went back to its owner";
        else if (resource === "") why = "it holds something that is not a resource";
        else {
          const isOwner = (m: Member): boolean =>
            m.signingKey === owner && m.genesis === hex(vault.ownerGenesis) && m.deviceId === hex(vault.ownerDeviceId);
          const listed = state.members.some(isOwner);
          // A member who left by their own change still counts while the vault is Active (spec 8.3, condition 4).
          leaving = !listed && leavers.some(isOwner);
          if (!listed && !leaving) why = "its owner is not a member of the faction";
        }
        contributions.push({ vault: hex(vault.vaultId), owner, resource, amount: Number(vault.amount),
          active: vault.status === pb.SofiVaultStatus.ACTIVE,
          counts: why === "",
          leaving,
          why,
        });
      }
    }
    return { contributions, notEstablished };
  }

  /** The funding of every placed structure (spec 8.3). `leavers` are the members who left by their own change. */
  async funding(factionId: string, state: FactionState, leavers: Member[], publisher: Party): Promise<Map<string, Funding>> {
    const out = new Map<string, Funding>();
    for (const placed of state.placed) {
      const prefab = this.catalogue.prefabs.get(placed.prefabId);
      if (prefab === undefined) throw new Error(`placed prefab ${placed.prefabId} is not in the catalogue`);
      const { contributions, notEstablished } = await this.read(factionId, placed.instanceId, state, leavers, publisher);
      const resources = Object.entries(prefab.recipe).map(([resource, needed]) => {
        const committed = contributions.filter((c) => c.counts && c.resource === resource).reduce((sum, c) => sum + c.amount, 0);
        // Amounts beyond the recipe count for nothing.
        const funded = Math.min(committed, needed);
        return { resource, needed, funded, shortfall: needed - funded };
      });
      const met = resources.every((r) => r.shortfall === 0);
      // A vault not yet established is never counted as zero: it could only add.
      const status = met ? "fully funded" : notEstablished.length > 0 ? "not yet known" : "needs resources";
      out.set(placed.instanceId, { status, resources, contributions, notEstablished });
    }
    return out;
  }

  /**
   * The Active contributions of one owner across a faction's structures,
   * dismantled ones included; or on one structure, when `instanceId` names it.
   */
  async activeVaultsOf(factionId: string, owner: string, publisher: Party, instanceId?: string): Promise<Uint8Array[]> {
    const record = this.store.faction(factionId);
    const cells = new Set<string>();
    for (const [instance, list] of Object.entries(record.contributions)) {
      if (instanceId !== undefined && instance !== instanceId) continue;
      for (const c of list) if (c.owner === owner) cells.add(c.cell);
    }
    const vaults: Uint8Array[] = [];
    for (const cell of cells) {
      const read = await lockedVaults(this.app, unhex(cell, "a verdict cell"));
      if (!read.complete) throw new Error(`not every vault on cell ${cell} could be established yet`);
      for (const vault of read.vaults) {
        const terms = Contributions.terms(vault, publisher);
        if ("owner" in terms && terms.owner === owner && vault.status === pb.SofiVaultStatus.ACTIVE) vaults.push(vault.vaultId);
      }
    }
    return vaults;
  }
}
