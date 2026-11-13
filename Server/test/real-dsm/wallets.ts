/**
 * What a player does in their own wallet, outside the game: add a contact, send an object to one,
 * lock an escrow vault of their own making and release it. Each is a route of the wallet's own
 * account, called through its ingress as the wallet's screens call it. The game is not a party to
 * any of them.
 */
import { readFile } from "node:fs/promises";
import { IngressError, pb, type WalletAccount } from "dsm-app-client";
import { unhex } from "../../src/faction/canonical.js";
import type { Member } from "../../src/faction/state.js";

type Payload = pb.Envelope["payload"];
type PayloadCase = Exclude<Payload["case"], undefined>;
type PayloadOf<C extends PayloadCase> = Extract<Payload, { case: C }>["value"];

function answered<C extends PayloadCase>(route: string, envelope: pb.Envelope, expected: C): PayloadOf<C> {
  if (envelope.payload.case !== expected) {
    throw new IngressError(route, 0, `answered with ${String(envelope.payload.case)}, not ${expected}`);
  }
  return envelope.payload.value as PayloadOf<C>;
}

/** Add `other` as a contact of this wallet's: DSM sends only over a relationship both sides have added. */
export async function addContact(wallet: WalletAccount, alias: string, other: Member): Promise<void> {
  const route = "contacts.addManual";
  const request = new pb.ContactManualAddRequest({
    alias,
    deviceId: unhex(other.deviceId, "device id"),
    genesisHash: unhex(other.genesis, "genesis"),
    signingPublicKey: unhex(other.signingKey, "signing key"),
  });
  answered(route, await wallet.ingress.call("invoke", route, request), "contactAddResponse");
}

/** This wallet sends `amount` of `symbol` to its contact `to`. Whether the wallet sent it, and what it said. */
export async function send(wallet: WalletAccount, to: Member, symbol: string, amount: string): Promise<{ sent: boolean; message: string }> {
  const route = "wallet.sendSmart";
  const request = new pb.OnlineTransferSmartRequest({
    recipientDeviceId: unhex(to.deviceId, "device id"),
    amount,
    tokenId: symbol,
    memo: "",
  });
  try {
    const response = answered(route, await wallet.ingress.call("invoke", route, request), "onlineTransferResponse");
    return { sent: response.success, message: response.message };
  } catch (e) {
    if (e instanceof IngressError) return { sent: false, message: e.detail };
    throw e;
  }
}

/**
 * What this wallet can spend of `symbol` (the number its own screen shows) and what it holds in
 * its own Active escrow vaults (`escrow.vaults`, each walked to its head).
 */
export async function holds(wallet: WalletAccount, symbol: string): Promise<{ available: bigint; locked: bigint }> {
  const owned = (await wallet.owned()).find((o) => o.object.symbol === symbol);
  const route = "escrow.vaults";
  const mine = answered(route, await wallet.ingress.call("invoke", route, new pb.EscrowVaultsRequest(), { quiet: true }), "escrowVaultsResponse");
  const locked = mine.vaults
    .filter((v) => v.tokenSymbol === symbol && v.status === pb.SofiVaultStatus.ACTIVE)
    .reduce((sum, v) => sum + v.amount, 0n);
  return { available: owned?.amount ?? 0n, locked };
}

/** What this wallet can spend of `symbol`: the number its own screen shows. */
export async function spendable(wallet: WalletAccount, symbol: string): Promise<bigint> {
  return (await wallet.owned()).find((o) => o.object.symbol === symbol)?.amount ?? 0n;
}

/** One outcome of an escrow vault's terms, as the wallet's own escrow screen names it. */
export interface Branch {
  outcome: string;
  /** The one key that decides it. */
  decidedBy: { signatureAlg: number; key: Uint8Array };
  pays: Member;
}

/** This wallet locks `amount` of an object in an escrow vault under terms of its own making. The vault and its verdict cell. */
export async function lockEscrow(
  wallet: WalletAccount,
  external: Uint8Array,
  object: Uint8Array,
  amount: string,
  branches: Branch[],
): Promise<{ vault: Uint8Array; cell: Uint8Array }> {
  const route = "escrow.create";
  const request = new pb.EscrowCreateRequest({
    external,
    tokenPolicyCommit: object,
    amountEntered: amount,
    branches: branches.map(
      (b) =>
        new pb.EscrowBranchV1({
          outcome: new TextEncoder().encode(b.outcome),
          signers: [new pb.EscrowSignerV1({ signatureAlg: b.decidedBy.signatureAlg, publicKey: b.decidedBy.key })],
          recipientGenesis: unhex(b.pays.genesis, "genesis"),
          recipientDeviceId: unhex(b.pays.deviceId, "device id"),
        }),
    ),
  });
  const created = answered(route, await wallet.ingress.call("invoke", route, request), "escrowCreatedResponse");
  return { vault: created.vaultId, cell: created.verdictCell };
}

/** This wallet decides `outcome` of a vault it is a signer of, then releases the vault to itself. */
export async function decideAndRelease(wallet: WalletAccount, vault: Uint8Array, outcome: string): Promise<void> {
  const decide = "escrow.adjudicate";
  answered(
    decide,
    await wallet.ingress.call("invoke", decide, new pb.EscrowOutcomeRequest({ vaultId: vault, outcome: new TextEncoder().encode(outcome) })),
    "escrowVerdictResponse",
  );
  const release = "escrow.release";
  answered(release, await wallet.ingress.call("invoke", release, new pb.EscrowReleaseRequest({ vaultId: vault })), "sofiPositionResponse");
}

/**
 * This wallet takes up an object by its id (Base32 Crockford), as a player does in the wallet's
 * Add Token screen: a wallet receives only an object it has added. The route takes the id as text,
 * so this goes to the wallet host's ingress directly, with this launch's secret.
 */
export async function takeUp(host: { url: string; secretFile: string }, object: string): Promise<void> {
  const route = "tokens.addByAnchor";
  const request = new pb.IngressRequest({
    operation: { case: "routerInvoke", value: new pb.RouterInvokeOp({ method: route, args: new TextEncoder().encode(object) }) },
  });
  const response = await fetch(`${host.url}/ingress`, {
    method: "POST",
    headers: { authorization: `Bearer ${await readFile(host.secretFile, "utf8")}`, "content-type": "application/x-protobuf" },
    body: request.toBinary(),
  });
  if (!response.ok) throw new IngressError(route, response.status, await response.text());
  const answer = pb.IngressResponse.fromBinary(new Uint8Array(await response.arrayBuffer()));
  if (answer.result.case === "error") throw new IngressError(route, answer.result.value.code, answer.result.value.message);
  if (answer.result.case !== "okBytes") throw new IngressError(route, 0, "the wallet answered nothing");
  const envelope = pb.Envelope.fromBinary(answer.result.value.subarray(1));
  if (envelope.payload.case === "error") throw new IngressError(route, envelope.payload.value.code, envelope.payload.value.message);
}
