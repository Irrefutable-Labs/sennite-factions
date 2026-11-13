// A mirror of one faction's base (spec 6.6): the committed state it holds and
// the lineage it walked to get there. It advances only on what a cell reader
// read at the successor cell: the occupant that holds the cell, final. A
// change delivered any other way is never evidence.

import type { Catalogue } from "./catalogue.js";
import { hex } from "./canonical.js";
import type { Parcel } from "./parcel.js";
import {
  firstCommitment,
  firstState,
  nextCommitment,
  successorCellName,
  writerKeys,
  type FactionState,
  type Founding,
} from "./state.js";
import { decodeChange, type Change } from "./change.js";
import { judge } from "./validity.js";

/** What a reader found at a cell. */
export type CellReading =
  /** No recognized write holds the cell. */
  | { cell: "open" }
  /** A recognized write holds the cell and is final: the cell's occupant. */
  | { cell: "held"; signerKey: string; payload: Uint8Array; step: Uint8Array }
  /** Not established either way yet. Never treated as open, and never as invalid (spec 7.4). */
  | { cell: "not_established"; why: string }
  /** The reader has no way to read this cell: it has nothing to go on there. */
  | { cell: "unread" };

/** Reads the cell an application names for a writer set. */
export interface CellReader {
  read(name: Uint8Array, writers: Uint8Array[]): Promise<CellReading>;
}

/** One spent successor cell of the lineage. */
export interface LineageEntry {
  /** R_n: the state whose successor cell this was. */
  parent: string;
  /** R_n+1. */
  commitment: string;
  /** The key that signed the occupant. */
  signerKey: string;
  /** The cell's step, as DSM derives it. */
  step: string;
  /** Whether the occupant was a valid change. An invalid one spent the cell and changed nothing. */
  valid: boolean;
  /** Why it was invalid; empty when valid. */
  why: string;
  /** The change the occupant made, when it was valid. */
  change?: Change;
}

export type WalkEnd =
  /** The successor cell is open: the mirror holds the latest state. */
  | { at: "open" }
  /** The successor cell's occupant is not established: the mirror waits here. */
  | { at: "not_established"; why: string }
  /** The reader could not read the successor cell at all: as far as it can see, the mirror holds the latest state. */
  | { at: "unread" }
  /** The state lists no member: it has no successor cell. */
  | { at: "no_members" };

export class Mirror {
  readonly lineage: LineageEntry[] = [];
  #state: FactionState;
  #commitment: string;

  constructor(
    first: FactionState,
    readonly catalogue: Catalogue,
    readonly parcel: Parcel,
  ) {
    if (catalogue.version !== first.catalogue) throw new Error("this is not the catalogue version the faction names");
    if (parcel.id !== first.parcelId) throw new Error("this is not the parcel the faction names");
    this.#state = first;
    this.#commitment = firstCommitment(first);
  }

  /** A mirror at R_0 of the faction `founding` founds under the assignment at `assignment`. */
  static at(founding: Founding, assignment: string, catalogue: Catalogue, parcel: Parcel): Mirror {
    return new Mirror(firstState(founding, assignment), catalogue, parcel);
  }

  /** The committed state this mirror holds. */
  get state(): FactionState {
    return this.#state;
  }

  /** R_n: the commitment of the state this mirror holds. */
  get commitment(): string {
    return this.#commitment;
  }

  /** n: how many successor cells this mirror has walked. */
  get height(): number {
    return this.lineage.length;
  }

  /** The name of the successor cell of the state this mirror holds. */
  successorCell(): { name: Uint8Array; writers: Uint8Array[] } {
    return { name: successorCellName(this.#state.factionId, this.#commitment), writers: writerKeys(this.#state) };
  }

  /**
   * Take the occupant of this state's successor cell. A valid change is
   * applied; an invalid one leaves the base as it was. Either way the lineage
   * advances past the cell (spec 7.4).
   */
  accept(occupant: { signerKey: string; payload: Uint8Array; step: Uint8Array }): LineageEntry {
    const verdict = judge(this.#state, this.#commitment, this.catalogue, this.parcel, occupant.signerKey, occupant.payload);
    const next = verdict.valid ? verdict.next : this.#state;
    const entry: LineageEntry = {
      parent: this.#commitment,
      commitment: nextCommitment(this.#commitment, occupant.step, next),
      signerKey: occupant.signerKey,
      step: hex(occupant.step),
      valid: verdict.valid,
      why: verdict.valid ? "" : verdict.why,
    };
    if (verdict.valid) {
      const read = decodeChange(occupant.payload);
      if (read.ok) entry.change = read.signedOver.change;
    }
    this.#state = next;
    this.#commitment = entry.commitment;
    this.lineage.push(entry);
    return entry;
  }

  /**
   * Walk the lineage from the state this mirror holds: read the successor
   * cell, take its occupant, apply, repeat (spec 6.6).
   */
  async walk(reader: CellReader): Promise<WalkEnd> {
    for (;;) {
      if (this.#state.members.length === 0) return { at: "no_members" };
      const { name, writers } = this.successorCell();
      const reading = await reader.read(name, writers);
      if (reading.cell === "open") return { at: "open" };
      if (reading.cell === "unread") return { at: "unread" };
      if (reading.cell === "not_established") return { at: "not_established", why: reading.why };
      this.accept(reading);
    }
  }
}
