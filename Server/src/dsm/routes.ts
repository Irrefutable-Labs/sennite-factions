// Routes of the publisher's account that DSM's client package does not name
// yet: authored objects and the vaults bound to a verdict cell. This file
// carries bytes through the account's ingress; every rule is the SDK's.

import { IngressError, pb, type Account } from "dsm-app-client";

type Payload = pb.Envelope["payload"];
type PayloadCase = Exclude<Payload["case"], undefined>;
type PayloadOf<C extends PayloadCase> = Extract<Payload, { case: C }>["value"];

function payload<C extends PayloadCase>(route: string, envelope: pb.Envelope, expected: C): PayloadOf<C> {
  if (envelope.payload.case !== expected) {
    throw new IngressError(route, 0, `answered with ${String(envelope.payload.case)}, not ${expected}`);
  }
  return envelope.payload.value as PayloadOf<C>;
}

/** This account as an escrow party: its genesis, device id and signing key. */
export interface Party {
  genesis: Uint8Array;
  deviceId: Uint8Array;
  signatureAlg: number;
  signingKey: Uint8Array;
}

export async function party(account: Account): Promise<Party> {
  const route = "escrow.party";
  const answered = payload(route, await account.ingress.call("invoke", route, new pb.EscrowPartyRequest(), { quiet: true }), "escrowPartyResponse");
  if (answered.signer === undefined) throw new IngressError(route, 0, "named no signing key");
  return {
    genesis: answered.genesis,
    deviceId: answered.deviceId,
    signatureAlg: answered.signer.signatureAlg,
    signingKey: answered.signer.publicKey,
  };
}

/**
 * Publish `payload` as an object of this account's under `topic`: content
 * addressed, signed by the account's key. Its address, once the set stored it.
 */
export async function publishAuthored(account: Account, topic: Uint8Array, bytes: Uint8Array): Promise<Uint8Array> {
  const route = "authored.publish";
  const request = new pb.AuthoredPublishRequestV1({ topic, payload: bytes });
  const published = payload(route, await account.ingress.call("invoke", route, request), "authoredPublishedResponse");
  if (!published.stored) throw new IngressError(route, 0, "the object was not read back from the set");
  return published.address;
}

export interface AuthoredObject {
  address: Uint8Array;
  payload: Uint8Array;
}

/**
 * Every object `author` published under `topic`, in the order it published
 * them, each checked by this account from the object's own bytes. `complete`
 * says whether every candidate under the locator was read.
 */
export async function readAuthored(
  account: Account,
  author: Uint8Array,
  topic: Uint8Array,
): Promise<{ objects: AuthoredObject[]; complete: boolean }> {
  const route = "authored.read";
  const request = new pb.AuthoredReadRequestV1({ authorDeviceId: author, topic });
  const read = payload(route, await account.ingress.call("query", route, request, { quiet: true }), "authoredObjectsResponse");
  return { objects: read.objects.map((o) => ({ address: o.address, payload: o.payload })), complete: read.complete };
}

/**
 * Whether a read of an account's failed only because something is still
 * settling: a position of its own is pending, or a vault's walk is not
 * complete, a claim it stands on not final, a cell not decided, yet. The
 * account says so, and is asked again: that is no answer about what it holds.
 */
export function settling(e: unknown): boolean {
  return e instanceof IngressError && /is pending|not (complete|final|decided) yet/.test(e.detail);
}

/** One outcome of an escrow vault's terms: its name and the identity its release pays. */
export interface Outcome {
  outcome: string;
  pays: { genesis: Uint8Array; deviceId: Uint8Array };
}

const bytes = (text: string): Uint8Array => new TextEncoder().encode(text);

/**
 * Lock `amount` of an object of this account's in an escrow vault: every
 * outcome decided by `decidedBy`'s key alone, each paying whom it names. Vaults
 * locked with the same `external` and the same outcomes and deciding key share
 * one verdict cell, whoever each pays. The vault and its verdict cell.
 */
export async function lockVault(
  account: Account,
  external: Uint8Array,
  object: Uint8Array,
  amount: string,
  decidedBy: Party,
  outcomes: Outcome[],
): Promise<{ vault: Uint8Array; cell: Uint8Array }> {
  const route = "escrow.create";
  // DSM takes the outcomes in one order: ascending by their bytes.
  const ordered = [...outcomes].sort((a, b) => Buffer.compare(bytes(a.outcome), bytes(b.outcome)));
  const request = new pb.EscrowCreateRequest({
    external,
    tokenPolicyCommit: object,
    amountEntered: amount,
    branches: ordered.map(
      (o) =>
        new pb.EscrowBranchV1({
          outcome: bytes(o.outcome),
          signers: [new pb.EscrowSignerV1({ signatureAlg: decidedBy.signatureAlg, publicKey: decidedBy.signingKey })],
          recipientGenesis: o.pays.genesis,
          recipientDeviceId: o.pays.deviceId,
        }),
    ),
  });
  const created = payload(route, await account.ingress.call("invoke", route, request), "escrowCreatedResponse");
  return { vault: created.vaultId, cell: created.verdictCell };
}

/** Every escrow vault this account locked, each at its walked head. */
export async function ownVaults(account: Account): Promise<pb.EscrowVaultV1[]> {
  const route = "escrow.vaults";
  return payload(route, await account.ingress.call("invoke", route, new pb.EscrowVaultsRequest(), { quiet: true }), "escrowVaultsResponse").vaults;
}

/** What a verdict cell holds: the outcome, and whether it is the cell's verdict for good. */
export interface Verdict {
  /** Empty while no verdict holds the cell. */
  outcome: string;
  final: boolean;
}

const verdictOf = (answered: pb.EscrowVerdictResponse): Verdict => ({
  outcome: new TextDecoder().decode(answered.outcome),
  final: answered.state === pb.EscrowVerdictState.FINAL,
});

/**
 * Decide `outcome` of a vault this account's key decides, and with it every
 * vault on the same verdict cell. What the cell holds afterwards: the first
 * verdict there stands, so it may be another outcome than the one asked.
 */
export async function decideVault(account: Account, vault: Uint8Array, outcome: string): Promise<Verdict> {
  const route = "escrow.adjudicate";
  const request = new pb.EscrowOutcomeRequest({ vaultId: vault, outcome: bytes(outcome) });
  return verdictOf(payload(route, await account.ingress.call("invoke", route, request), "escrowVerdictResponse"));
}

/** What the verdict cell of a vault holds now. */
export async function verdict(account: Account, vault: Uint8Array): Promise<Verdict> {
  const route = "escrow.verdict";
  const request = new pb.EscrowVerdictRequest({ vaultId: vault });
  return verdictOf(payload(route, await account.ingress.call("invoke", route, request, { quiet: true }), "escrowVerdictResponse"));
}

/**
 * Release a vault to this account: DSM does it only once the cell's verdict
 * is final on an outcome that pays this account. Whether the release realized.
 */
export async function releaseVault(account: Account, vault: Uint8Array): Promise<boolean> {
  const route = "escrow.release";
  const request = new pb.EscrowReleaseRequest({ vaultId: vault });
  const released = payload(route, await account.ingress.call("invoke", route, request), "sofiPositionResponse");
  return released.state === pb.SofiPositionState.REALIZED;
}

/**
 * The escrow vaults bound to a verdict cell, each at its walked head.
 * `complete` says whether every vault there was established.
 */
export async function lockedVaults(
  account: Account,
  verdictCell: Uint8Array,
): Promise<{ vaults: pb.EscrowVaultV1[]; complete: boolean }> {
  const route = "escrow.locked";
  const request = new pb.EscrowLockedRequest({ verdictCell });
  const locked = payload(route, await account.ingress.call("invoke", route, request, { quiet: true }), "escrowVaultsResponse");
  return { vaults: locked.vaults, complete: locked.search === pb.SofiSearch.COMPLETE };
}
