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
