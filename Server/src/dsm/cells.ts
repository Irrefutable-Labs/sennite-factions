// Application cells, through the publisher's account (DSM Amendment A35).
//
// A member's wallet writes a cell when the publisher's account asks it to,
// under the state grant. What holds a cell is then a fact the account
// establishes itself: for a request it asked, `connect.app.status` reads the
// cell and says whether that request's write holds it, final, and gives the
// cell's step.
//
// That is the only reading of a cell the account offers today. It has no call
// that returns a cell's occupant, so this reader can name the occupant only
// when it is a write the server asked for and recorded. A cell held by a
// write the server did not ask for is reported as not established, and a cell
// it asked nothing at as unread. See docs/dsm-gaps.md.

import { ask, type AppAccount, type Connection, type RequestRef, type Result } from "dsm-app-client";
import { digest, hex, unhex } from "../faction/canonical.js";
import type { CellReader, CellReading } from "../faction/mirror.js";
import type { AskedWrite, Store } from "../publisher/store.js";

const OPEN = "no recognized write holds the cell";

/**
 * Whether the account's reading of a cell says its leader seat was not read. No other seat stands
 * in for a cell's leader, so the cell is undecided until it answers. The account says so only in
 * the words of its detail (docs/dsm-gaps.md, 1).
 */
export function leaderUnread(detail: string): boolean {
  return detail.includes("LeaderUnread");
}

/**
 * The server's own key for a cell in its record: the name and the writer
 * set, whatever order the writers are given in. The same name under another
 * writer set is another cell.
 */
export function cellRecordKey(name: Uint8Array, writers: Uint8Array[]): string {
  const keys = writers.map((w) => hex(w)).sort();
  return `${hex(name)}:${hex(digest("sennite/cell-writers/v1", ...keys.map((k) => unhex(k, "a writer's key"))))}`;
}

export class AccountCells implements CellReader {
  constructor(
    private readonly app: AppAccount,
    private readonly store: Store,
  ) {}

  /**
   * Ask the wallet on `connection` to write `payload` at the cell named
   * `name` for `writers`, and record that it was asked. The account refuses
   * to ask a wallet whose key is not one of the writers.
   */
  async ask(connection: Connection, name: Uint8Array, writers: Uint8Array[], payload: Uint8Array): Promise<RequestRef> {
    const ref = await this.app.request(connection, ask.state.write(name, writers, payload));
    const asked: AskedWrite = {
      connection: ref.connection,
      seq: ref.seq.toString(),
      signerKey: hex(connection.opponent.signingKey),
      payload: hex(payload),
    };
    (this.store.data.asked[cellRecordKey(name, writers)] ??= []).push(asked);
    this.store.save();
    return ref;
  }

  /** The writes asked at a cell, in the order asked. */
  refs(name: Uint8Array, writers: Uint8Array[]): RequestRef[] {
    return (this.store.data.asked[cellRecordKey(name, writers)] ?? []).map((a) => ({ connection: a.connection, seq: BigInt(a.seq) }));
  }

  /** What the account established about one asked write. */
  result(asked: AskedWrite): Promise<Result> {
    return this.app.result({ connection: asked.connection, seq: BigInt(asked.seq) });
  }

  /**
   * What holds the cell named `name` for `writers`, as far as the account's
   * facts about the writes asked there say.
   */
  async read(name: Uint8Array, writers: Uint8Array[]): Promise<CellReading> {
    const asked = this.store.data.asked[cellRecordKey(name, writers)] ?? [];
    if (asked.length === 0) return { cell: "unread" };
    let heldByAnother = false;
    let open = false;
    let why = "";
    let leader = false;
    for (const one of asked) {
      const r = await this.result(one);
      if (r.fact === "written") {
        if (r.state?.step === undefined) return { cell: "not_established", why: "the account named the write final without the cell's step" };
        return { cell: "held", signerKey: one.signerKey, payload: unhex(one.payload, "the asked payload"), step: r.state.step };
      }
      if (r.state?.step !== undefined) heldByAnother = true;
      else if (r.detail.startsWith(OPEN)) open = true;
      else {
        why = r.detail || r.reason || `the request is ${r.outcome}`;
        if (leaderUnread(r.detail)) leader = true;
      }
    }
    if (heldByAnother) {
      return {
        cell: "not_established",
        waits: "occupant",
        why: "a write this server did not ask for holds the cell, final; the publisher's account has no call that reads a cell's occupant",
      };
    }
    if (leader) return { cell: "not_established", waits: "leader", why };
    if (why !== "") return { cell: "not_established", why };
    return open ? { cell: "open" } : { cell: "not_established", why: "the account established nothing about the cell" };
  }
}
