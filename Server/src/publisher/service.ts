// What the game client asks of the publisher's server, over HTTP and JSON.
//
// The game stays an ordinary game: it shows a connect code, sends the choices
// a player makes in the editor, and draws what the server says the base is.
// It holds no DSM account and no key. A player is known to the server by a
// token the server gave their game; the token only resumes their place in the
// game and is never DSM authority. What the player's wallet signs, commits or
// takes back is asked of the wallet by the publisher's account, and the server
// answers only with facts that account established.

import { createHash, randomBytes, timingSafeEqual } from "node:crypto";
import { createServer, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import { encodeB32, type Connection } from "dsm-app-client";
import { hex, unhex } from "../faction/canonical.js";
import { decodeChange, encodeChange, type Change } from "../faction/change.js";
import type { Funding } from "../dsm/contributions.js";
import type { Member } from "../faction/state.js";
import { buildingAt, Publisher, Refused, type Structure } from "./publisher.js";
import type { Say } from "./shop.js";
import { leastOf } from "./swap.js";
import type { Store } from "./store.js";
import type { World } from "./world.js";

export interface ServiceOptions {
  publisher: Publisher;
  world: World;
  store: Store;
  /** What the publisher's match server sends to record destruction and to open and decide matches. Never given to a player's game. */
  matchSecret: string;
}

/** A request the server answers at once and settles later: the wallet answers on its own time. */
interface Job {
  player: string;
  state: "pending" | "done";
  ok: boolean;
  message: string;
  /** What else the answer carries, beside whether it was carried out. */
  data: Record<string, unknown>;
  /** While it is pending: what it is waiting on, in words for the player; empty when the work does not say. */
  stage: string;
  /** While it is pending: whether what it waits on is the player, in their wallet. */
  onWallet: boolean;
}

class HttpError extends Error {
  constructor(
    readonly status: number,
    message: string,
  ) {
    super(message);
  }
}

const MAX_BODY_BYTES = 64 * 1024;
/** The most players the server keeps a record of at all. */
const MAX_PLAYERS = 10_000;
/** How long a player's token lasts with no game using it. The wallet then connects again by a new code. */
const IDLE_MS = 30 * 24 * 60 * 60 * 1000;
/** How often a token's last use is written to the record. */
const SEEN_EVERY_MS = 60 * 60 * 1000;
/** The most players the server keeps a record of whose connect code no wallet has approved yet. */
const MAX_UNAPPROVED = 1_000;
/** How many connect codes one address may ask for in a minute. */
const CODES_A_MINUTE = 10;
/** How long a player whose wallet never connected is kept. */
const UNCONNECTED_MS = 60 * 60 * 1000;
/** How long a base read is served again before it is read anew. */
const BASE_FRESH_MS = 1000;
/** How long a ticket into a match is good for. It is good once. */
const TICKET_MS = 2 * 60 * 1000;
/** The most tickets kept at once. */
const MAX_TICKETS = 10_000;
/** How long a hero pass is good for. It is good once: the game server that asks with it uses it up. */
const PASS_MS = 2 * 60 * 1000;
/** The words the server's routes are made of: all of a path that is written to the server's output (said). */
const ROUTE_WORDS = new Set([
  "world", "players", "me", "prizes", "shop", "dbucks", "swap", "price", "trades", "skins", "heroes", "weapons", "hero-pass", "hero-passes", "free",
  "materials", "factions", "parcels", "base", "invitations", "changes", "commitments", "withdrawals", "requests", "destroyed", "matches", "outcome",
  "tickets", "entries", "bases",
]);

/**
 * The terms an offer is made on. 1: the shop's grant let the game be paid D-bucks and trade ERA within caps without
 * asking the player. 2: it grants neither, and every payment and trade waits for the player in their wallet. A player
 * whose offer was made on other terms than these is a new player the next time their game asks anything.
 */
const TERMS = 2;

/** How old a wallet's proof of its heroes may be and still answer a hero pass. */
const PROOF_FRESH_MS = 60 * 60 * 1000;
/** The most wallets whose proven heroes are kept at once. */
const MAX_PROVEN = 10_000;
/** The audience of a hero pass for the publisher's match server, which holds no player's token. */
const MATCH_AUDIENCE = "match";

/**
 * What a game is called as the audience of a hero pass: a digest of the token the server gave it, which the
 * game learns from GET /players/me and can show the players who join it. It is no token: nothing is done with it.
 */
export function audienceOf(token: string): string {
  return createHash("sha256").update(`sennite hero-pass audience\0${token}`).digest("hex").slice(0, 32);
}
/** The most hero passes kept at once. */
const MAX_PASSES = 10_000;

/** A wallet's way into one match: who it is and which of the match's factions it plays for. */
interface Ticket {
  matchId: string;
  factionId: string;
  signingKey: string;
  wallet: string;
  until: number;
}

async function body(req: IncomingMessage): Promise<Record<string, unknown>> {
  const chunks: Buffer[] = [];
  let size = 0;
  for await (const chunk of req) {
    size += (chunk as Buffer).length;
    if (size > MAX_BODY_BYTES) throw new HttpError(413, "the request is too large");
    chunks.push(chunk as Buffer);
  }
  if (size === 0) return {};
  let parsed: unknown;
  try {
    parsed = JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    throw new HttpError(400, "the request is not JSON");
  }
  if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed)) throw new HttpError(400, "the request is not a JSON object");
  return parsed as Record<string, unknown>;
}

function text(value: unknown, what: string): string {
  if (typeof value !== "string" || value.length === 0 || value.length > 256) throw new HttpError(400, `${what} is missing`);
  return value;
}

export class Service {
  readonly #jobs = new Map<string, Job>();
  /** When each address last asked for connect codes. */
  readonly #asked = new Map<string, number[]>();
  readonly #bases = new Map<string, { at: number; base: Awaited<ReturnType<Publisher["base"]>> }>();
  /** Tickets into matches that no match server has taken yet. Kept in memory: a game asks again. */
  readonly #tickets = new Map<string, Ticket>();
  /** The heroes each wallet (by signing key) proved it owns at its last proof here, and when. Kept in memory, oldest first: a game proves again on connecting. */
  readonly #provenHeroes = new Map<string, { heroes: string[]; at: number }>();
  /** The last answer each game was given to each read, so that only a change is written down (said). */
  readonly #saidLast = new Map<string, number>();
  /** Hero passes: pass -> the wallet it speaks for, the one game that may ask with it, and until when. One live pass per wallet. */
  readonly #passes = new Map<string, { signingKey: string; wallet: string; audience: string; until: number }>();

  constructor(private readonly options: ServiceOptions) {}

  /** Serve on `host:port`. Port 0 picks a free one; the server's address says which. */
  listen(port: number, host: string): Promise<Server> {
    const server = createServer((req, res) => void this.handle(req, res));
    return new Promise((resolve, reject) => {
      server.once("error", reject);
      server.listen(port, host, () => resolve(server));
    });
  }

  /**
   * What a game asked and what it was answered, one line each on the server's output: everything that is not a
   * read, and a read whose answer changed since that game last made it (a game reads the same things every
   * second). Nothing secret is written: a game is named by six characters of a hash of what it bears (its token,
   * or the match server's secret), never by a piece of it, and of a path only the words the server's routes are
   * made of are kept; a request's id, a hero pass, a parcel, a faction or a match is written as "…".
   */
  private said(req: IncomingMessage, status: number, why = ""): void {
    const bears = this.bearer(req);
    const game = bears === "" ? "-" : createHash("sha256").update(bears).digest("hex").slice(0, 6);
    const path = (req.url ?? "").split("?")[0]!.split("/").map((part) => (part === "" || ROUTE_WORDS.has(part) ? part : "…")).join("/");
    // which read it is, is told by the whole path (kept here only, never written): two parcels are two reads
    const key = `${game} ${req.method} ${createHash("sha256").update(req.url ?? "").digest("hex").slice(0, 16)}`;
    if (req.method === "GET") {
      if (this.#saidLast.get(key) === status) return;
      if (this.#saidLast.size > 4096) this.#saidLast.clear();
      this.#saidLast.set(key, status);
    }
    console.log(`[publisher] ${new Date().toISOString().slice(11, 19)}Z game ${game} ${req.method} ${path} -> ${status}${why === "" ? "" : `: ${why}`}`);
  }

  private async handle(req: IncomingMessage, res: ServerResponse): Promise<void> {
    try {
      const answer = await this.route(req);
      const bytes = Buffer.from(JSON.stringify(answer.body));
      res.writeHead(answer.status, { "content-type": "application/json", "content-length": bytes.length });
      res.end(bytes);
      this.said(req, answer.status, typeof (answer.body as { request?: unknown }).request === "string" ? `request ${String((answer.body as { request: string }).request).slice(0, 6)}` : "");
    } catch (e) {
      const status = e instanceof HttpError ? e.status : e instanceof Refused ? 409 : 500;
      const message = e instanceof Error ? e.message : String(e);
      this.said(req, status, message);
      if (status === 500) console.error(`[publisher] ${req.method} ${req.url}: ${message}`);
      const bytes = Buffer.from(JSON.stringify({ error: status === 500 ? "the publisher's server could not answer" : message }));
      res.writeHead(status, { "content-type": "application/json", "content-length": bytes.length });
      res.end(bytes);
    }
  }

  private bearer(req: IncomingMessage): string {
    const header = req.headers.authorization ?? "";
    return header.startsWith("Bearer ") ? header.slice(7) : "";
  }

  /** Refuse a request that does not carry the publisher's match server's secret. No player's game has it. */
  private matchServer(req: IncomingMessage, does: string): void {
    const want = Buffer.from(this.options.matchSecret);
    const got = Buffer.from(this.bearer(req));
    if (want.length === 0 || got.length !== want.length || !timingSafeEqual(got, want)) {
      throw new HttpError(401, `only the publisher's match server ${does}`);
    }
  }

  /** Keep the heroes a wallet just proved it owns: proofs over an hour old are dropped, and no more than MAX_PROVEN wallets are kept, the oldest going first. */
  private keepProvenHeroes(signingKey: string, heroes: string[]): void {
    const now = Date.now();
    this.#provenHeroes.delete(signingKey);
    for (const [key, proven] of this.#provenHeroes) {
      if (now - proven.at <= PROOF_FRESH_MS && this.#provenHeroes.size < MAX_PROVEN) break;
      this.#provenHeroes.delete(key);
    }
    this.#provenHeroes.set(signingKey, { heroes, at: now });
  }

  /** The player a request comes from, by the token the server gave their game. */
  private player(req: IncomingMessage): string {
    const token = this.bearer(req);
    const players = this.options.store.data.players;
    const record = token !== "" && Object.hasOwn(players, token) ? players[token] : undefined;
    if (record === undefined) throw new HttpError(401, "the game is not known to the server");
    if ((record.terms ?? 1) !== TERMS) {
      // the offer this player's wallet approved granted what the game no longer asks for: the game starts again as a
      // new player, and the wallet approves an offer on the terms there are now
      delete players[token];
      this.options.store.save();
      throw new HttpError(401, "the game is not known to the server");
    }
    const now = Date.now();
    if (now - (record.lastSeen ?? record.since) > IDLE_MS) {
      // unused for too long: the token is over, and the game starts again as a new player
      delete players[token];
      this.options.store.save();
      throw new HttpError(401, "the game is not known to the server");
    }
    if (now - (record.lastSeen ?? 0) > SEEN_EVERY_MS) {
      record.lastSeen = now;
      this.options.store.save();
    }
    return token;
  }

  /** The wallet connected for a player, or undefined while none is. */
  private async connection(player: string): Promise<Connection | undefined> {
    const record = this.options.store.data.players[player];
    if (record === undefined) return undefined;
    const all = await this.options.publisher.app.connections();
    const found = all.find((c) => c.connected && hex(c.raw.offerDigest) === record.offer);
    if (found !== undefined && record.approved !== true) {
      record.approved = true;
      this.options.store.save();
    }
    if (found === undefined && all.some((c) => !c.connected && hex(c.raw.offerDigest) === record.offer)) {
      // The wallet that approved this player's offer ended the connection. That is not a passing state: the account
      // lists an ended connection as ended for good, and an offer is approved once, so its code cannot connect a
      // wallet again (the account refuses a second approval of it). So the player is over, and the game starts
      // again as a new player with a new code for the wallet to approve. Nothing of the wallet's is lost with the
      // record: what it owns and its faction are kept by its own key, not by the game's token.
      delete this.options.store.data.players[player];
      this.options.store.save();
      throw new HttpError(401, "the wallet ended its connection to the game: connect it again");
    }
    return found;
  }

  /**
   * Drop the players whose connect code no wallet approved within the hour,
   * and those whose token no game has used for a month. A player whose wallet
   * approved once is kept through a disconnect: the account lists every
   * wallet that ever connected, and the record remembers.
   */
  private async forgetUnapproved(): Promise<void> {
    const players = this.options.store.data.players;
    const offers = new Set((await this.options.publisher.app.connections()).map((c) => hex(c.raw.offerDigest)));
    let changed = false;
    for (const [token, p] of Object.entries(players)) {
      if (p.approved === true) {
        if (!(Date.now() - (p.lastSeen ?? p.since) <= IDLE_MS)) {
          delete players[token];
          changed = true;
        }
        continue;
      }
      if (offers.has(p.offer)) {
        p.approved = true;
        changed = true;
      } else if (!(Date.now() - p.since <= UNCONNECTED_MS)) {
        // older than the hour, or a record with no time on it
        delete players[token];
        changed = true;
      }
    }
    if (changed) this.options.store.save();
  }

  /** Whether `address` may be given another connect code now: a few a minute, each address on its own count. */
  private mayAsk(address: string): boolean {
    const now = Date.now();
    const recent = (this.#asked.get(address) ?? []).filter((at) => now - at < 60_000);
    if (recent.length >= CODES_A_MINUTE) {
      this.#asked.set(address, recent);
      return false;
    }
    recent.push(now);
    this.#asked.set(address, recent);
    // addresses that asked nothing for a minute are forgotten
    for (const [other, times] of this.#asked) if (times.every((at) => now - at >= 60_000)) this.#asked.delete(other);
    return true;
  }

  private async connected(player: string): Promise<Connection> {
    const connection = await this.connection(player);
    if (connection === undefined) throw new HttpError(409, "no wallet is connected");
    return connection;
  }

  /**
   * The connected wallet of a player, which must be a member of the faction;
   * or, with `orInvited`, one the faction has invited.
   */
  private async member(player: string, factionId: string, orInvited = false): Promise<Connection> {
    if (!Object.hasOwn(this.options.store.data.factions, factionId)) throw new HttpError(404, "no such faction");
    const connection = await this.connected(player);
    const { mirror } = await this.options.publisher.follow(factionId);
    const key = hex(connection.opponent.signingKey);
    const listed = mirror.state.members.some((m) => m.signingKey === key);
    const invited = orInvited && mirror.state.invited.some((m) => m.signingKey === key);
    if (!listed && !invited) throw new HttpError(403, "the wallet is not a member of the faction");
    return connection;
  }

  /** A person as a game shows them: by their wallet's DSM identity, as people read it. */
  private person(m: Member): { signingKey: string; wallet: string } {
    return { signingKey: m.signingKey, wallet: encodeB32(unhex(m.deviceId, "device id")) };
  }

  /** Start work a wallet has to answer; the game reads what became of it under the id. */
  private start(
    player: string,
    work: (say: Say) => Promise<{ ok: boolean; message: string; data?: Record<string, unknown> }>,
    factionId = "",
  ): { request: string } {
    const id = hex(randomBytes(16));
    const job: Job = { player, state: "pending", ok: false, message: "", data: {}, stage: "", onWallet: false };
    this.#jobs.set(id, job);
    work((stage, onWallet) => {
      job.stage = stage;
      job.onWallet = onWallet;
    })
      .then(
        (done) => Object.assign(job, done),
        (e: unknown) => {
          job.message = e instanceof Refused ? e.message : "the publisher's account could not carry it out";
          if (!(e instanceof Refused)) console.error(`[publisher] request ${id}: ${e instanceof Error ? e.message : String(e)}`);
        },
      )
      .finally(() => {
        job.state = "done";
        this.#bases.delete(factionId);
        console.log(`[publisher] ${new Date().toISOString().slice(11, 19)}Z request ${id.slice(0, 6)} of game ${createHash("sha256").update(player).digest("hex").slice(0, 6)} is done: ${job.ok ? "carried out" : "not carried out"}: ${job.message}`);
      });
    return { request: id };
  }

  private async base(factionId: string): Promise<Awaited<ReturnType<Publisher["base"]>>> {
    const held = this.#bases.get(factionId);
    if (held !== undefined && Date.now() - held.at < BASE_FRESH_MS) return held.base;
    const base = await this.options.publisher.base(factionId);
    this.#bases.set(factionId, { at: Date.now(), base });
    return base;
  }

  private async route(req: IncomingMessage): Promise<{ status: number; body: unknown }> {
    const url = new URL(req.url ?? "/", "http://publisher");
    const parts = url.pathname.split("/").filter((p) => p.length > 0);
    const [a, b, c] = parts;
    const { publisher, world, store } = this.options;
    const is = (method: string, ...shape: (string | null)[]): boolean =>
      req.method === method && parts.length === shape.length && shape.every((s, i) => s === null || s === parts[i]);

    // The publisher's world: the prefab catalogue and the parcel registry.
    if (is("GET", "world")) {
      return {
        status: 200,
        body: {
          catalogue: {
            version: world.catalogue.version,
            grid: world.catalogue.grid,
            resources: world.catalogue.resources,
            prefabs: [...world.catalogue.prefabs.values()],
          },
          parcels: [...world.parcels.values()].map((p) => ({ id: p.id, boundary: p.boundary, factionId: publisher.factionOn(p.id) ?? "" })),
        },
      };
    }

    // A new player: a token for their game, and a connect code for their wallet.
    if (is("POST", "players")) {
      if (!this.mayAsk(req.socket.remoteAddress ?? "")) throw new HttpError(429, "too many connect codes asked for: wait a minute");
      await this.forgetUnapproved();
      const kept = Object.values(store.data.players);
      if (kept.length >= MAX_PLAYERS) throw new HttpError(429, "the server has no room for another player now");
      if (kept.filter((p) => p.approved !== true).length >= MAX_UNAPPROVED) {
        throw new HttpError(429, "too many connect codes are waiting for a wallet: try again later");
      }
      const grant = world.economy.grant;
      const offer = await publisher.offer(world.economy.resources.map((r) => ({ symbol: r.symbol, ...grant })));
      const player = hex(randomBytes(32));
      store.data.players[player] = { offer: hex(offer.digest), since: Date.now(), terms: TERMS };
      store.save();
      return { status: 201, body: { player, code: offer.code } };
    }

    if (is("GET", "players", "me")) {
      const connection = await this.connection(this.player(req));
      if (connection === undefined) return { status: 200, body: { connected: false, audience: audienceOf(this.bearer(req)) } };
      const signingKey = hex(connection.opponent.signingKey);
      return {
        status: 200,
        body: {
          connected: true,
          audience: audienceOf(this.bearer(req)),
          wallet: connection.peer,
          signingKey,
          faction: (await publisher.factionOf(signingKey)) ?? null,
          invitations: await publisher.invitationsOf(signingKey),
        },
      };
    }

    // What the player's wallet has won in matches and not collected yet (spec 10.6). The wallet
    // collects it itself, in its escrow screen, by the verdict cell: the game cannot ask it to.
    if (is("GET", "players", "me", "prizes")) {
      const connection = await this.connected(this.player(req));
      return { status: 200, body: { prizes: await publisher.matches.waiting(hex(connection.opponent.signingKey)) } };
    }

    // The shop: the game's currency and the skins and heroes the publisher sells for it (spec 10.5).
    if (is("GET", "shop")) {
      const shop = publisher.shop.world;
      return {
        status: 200,
        body: { currency: { symbol: shop.currency.symbol, name: shop.currency.name }, skins: shop.skins, heroes: shop.heroes, weapons: shop.weapons, materials: publisher.shop.packs() },
      };
    }

    // The wallet buys a pack of a material from the shop, or sells one back to it (owner, 2026-11-22). Buying,
    // the wallet pays the pack's price in D-bucks and is given the pack; selling, it sends the pack and is paid
    // what a pack sold back pays. Either way the wallet's half waits in the wallet for the player, and the shop
    // gives its half only once the publisher's account has the wallet's.
    if (is("POST", "shop", "materials")) {
      const player = this.player(req);
      const connection = await this.connected(player);
      const asked = await body(req);
      const pack = publisher.shop.pack(text(asked.symbol, "symbol"));
      const way = text(asked.way, "way");
      if (way !== "buy" && way !== "sell") throw new HttpError(400, "way is buy or sell");
      return {
        status: 202,
        body: this.start(player, async (say) => {
          const traded = way === "buy" ? await publisher.shop.buyMaterial(connection, pack.symbol, say) : await publisher.shop.sellMaterial(connection, pack.symbol, say);
          if (!traded.ok) return { ok: false, message: traded.why };
          const { units, resource, dbucks } = traded.trade;
          if (!traded.given) {
            // the wallet's half landed; the shop's is given once the wallet has taken that in, which it does when it is next open
            return { ok: true, message: way === "buy" ? `paid: your ${units} ${resource} are on their way to your wallet` : `the shop has your ${units} ${resource}: your ${dbucks} D-bucks are on their way to your wallet` };
          }
          return { ok: true, message: way === "buy" ? `${units} ${resource} are the wallet's, for ${dbucks} D-bucks` : `${units} ${resource} sold to the shop for ${dbucks} D-bucks` };
        }),
      };
    }

    // The wallet trades ERA for D-bucks through the publisher's vault, at the price the vault's reserves set.
    if (is("POST", "shop", "dbucks")) {
      const player = this.player(req);
      const connection = await this.connected(player);
      const era = text((await body(req)).era, "era");
      if (!/^[0-9]{1,6}(\.[0-9]{1,2})?$/.test(era) || Number(era) <= 0) throw new HttpError(400, "the ERA to trade is not an amount above zero");
      return {
        status: 202,
        body: this.start(player, async (say) => {
          const quoted = await publisher.shop.price(connection, era);
          // the price is the vault's at the time of the trade: a little less than quoted is taken, much less is not
          const traded = await publisher.shop.buyDbucks(connection, era, Math.floor(quoted * 0.97), say);
          return traded.ok ? { ok: true, message: `${era} ERA traded for D-bucks` } : { ok: false, message: traded.why };
        }),
      };
    }

    // Swaps through the publisher's vaults (swap.ts): what can be swapped, a price, and the trade.
    if (is("GET", "shop", "swap")) {
      return { status: 200, body: await publisher.swap.board() };
    }

    // What an amount of one object would get in another now: the wallet's answer to DSM's quote through the vaults.
    if (is("POST", "shop", "swap", "price")) {
      const player = this.player(req);
      const connection = await this.connected(player);
      const { from, to, amount } = await publisher.swap.asked(await body(req));
      return {
        status: 202,
        body: this.start(player, async () => {
          const price = await publisher.swap.price(connection, from, to, amount);
          return {
            ok: true,
            message: `${price.amountIn} ${from.name} gets ${price.amountOut} ${to.name} now`,
            data: { price: { from: from.id, to: to.id, amount, amountIn: price.amountIn, amountOut: price.amountOut, out: String(price.out), hops: price.hops } },
          };
        }),
      };
    }

    // The wallet swaps one object for another, for no less than a little under the price the player was shown.
    if (is("POST", "shop", "swap", "trades")) {
      const player = this.player(req);
      const connection = await this.connected(player);
      const asked = await body(req);
      const { from, to, amount } = await publisher.swap.asked(asked);
      const shown = asked.shown;
      if (typeof shown !== "string" || !/^[0-9]{1,30}$/.test(shown)) throw new HttpError(400, "a swap names the price it was shown: ask the price first");
      const atLeast = leastOf(BigInt(shown));
      return {
        status: 202,
        body: this.start(player, async (say) => {
          const traded = await publisher.swap.trade(connection, from, to, amount, atLeast, say);
          return traded.ok ? { ok: true, message: `${amount} ${from.name} swapped for ${to.name}` } : { ok: false, message: traded.why };
        }),
      };
    }

    // The wallet buys a skin: it pays in D-bucks, and the publisher's account issues the skin and gives it once it has the payment.
    if (is("POST", "shop", "skins")) {
      const player = this.player(req);
      const connection = await this.connected(player);
      const skin = publisher.shop.skin(text((await body(req)).skinId, "skinId"));
      return {
        status: 202,
        body: this.start(player, async (say) => {
          const sold = await publisher.shop.buySkin(connection, skin.id, say);
          return sold.ok ? { ok: true, message: `${sold.skin.name} #${sold.skin.serial} is the wallet's` } : { ok: false, message: sold.why };
        }),
      };
    }

    // The wallet buys a hero, as it buys a skin.
    if (is("POST", "shop", "heroes")) {
      const player = this.player(req);
      const connection = await this.connected(player);
      const hero = publisher.shop.hero(text((await body(req)).heroId, "heroId"));
      return {
        status: 202,
        body: this.start(player, async (say) => {
          const sold = await publisher.shop.buyHero(connection, hero.id, say);
          return sold.ok ? { ok: true, message: `${sold.hero.name} #${sold.hero.serial} is the wallet's` } : { ok: false, message: sold.why };
        }),
      };
    }

    // The wallet buys a gun, as it buys a hero.
    if (is("POST", "shop", "weapons")) {
      const player = this.player(req);
      const connection = await this.connected(player);
      const weapon = publisher.shop.weapon(text((await body(req)).weaponId, "weaponId"));
      return {
        status: 202,
        body: this.start(player, async (say) => {
          const sold = await publisher.shop.buyWeapon(connection, weapon.id, say);
          return sold.ok ? { ok: true, message: `${sold.weapon.name} #${sold.weapon.serial} is the wallet's` } : { ok: false, message: sold.why };
        }),
      };
    }

    // A hero pass for the player's game to show one game server it has joined, named by its audience (as that
    // game's GET /players/me gave it, or "match" for the publisher's match server). That server alone can ask
    // here with it, once: which heroes the wallet proved it owns at its last proof, and whose wallet it is. A
    // pass is good for two minutes; a new pass replaces the wallet's last one.
    if (is("POST", "players", "me", "hero-pass")) {
      const connection = await this.connected(this.player(req));
      const audience = text((await body(req)).audience, "audience");
      if (audience !== MATCH_AUDIENCE && !/^[0-9a-f]{32}$/.test(audience)) throw new HttpError(400, "audience is no game's audience");
      const signingKey = hex(connection.opponent.signingKey);
      const now = Date.now();
      for (const [held, p] of this.#passes) if (p.until < now || p.signingKey === signingKey) this.#passes.delete(held);
      if (this.#passes.size >= MAX_PASSES) throw new HttpError(429, "too many hero passes are out: try again in a moment");
      const pass = hex(randomBytes(16));
      this.#passes.set(pass, { signingKey, wallet: connection.peer, audience, until: now + PASS_MS });
      return { status: 201, body: { pass, seconds: PASS_MS / 1000 } };
    }

    // The game server a hero pass is for asks what it speaks for: whose wallet, and the heroes that wallet proved
    // it owns at a proof no older than an hour. It asks with its own token (or the match server's secret); to
    // any other asker the pass is no pass. The asking uses the pass up.
    if (is("GET", "hero-passes", null) && b !== undefined) {
      const held = this.#passes.get(b);
      if (held === undefined || held.until < Date.now()) throw new HttpError(404, "that is no hero pass");
      if (held.audience === MATCH_AUDIENCE) this.matchServer(req, "asks with a hero pass for a match");
      else if (audienceOf(this.player(req)) !== held.audience) throw new HttpError(404, "that is no hero pass");
      this.#passes.delete(b);
      const proven = this.#provenHeroes.get(held.signingKey);
      const heroes = proven !== undefined && Date.now() - proven.at <= PROOF_FRESH_MS ? proven.heroes : [];
      return { status: 200, body: { heroes, signingKey: held.signingKey, wallet: held.wallet } };
    }

    // The wallets connected to the game that are in no faction: who a member can invite.
    if (is("GET", "players", "free")) {
      const caller = await this.connected(this.player(req));
      // only a member of a faction has anyone to invite, and a wallet's DSM identity is all inviting takes
      if ((await publisher.factionOf(hex(caller.opponent.signingKey))) === undefined) {
        throw new HttpError(403, "the wallet is not a member of a faction");
      }
      const free = [];
      for (const other of (await publisher.app.connections()).filter((x) => x.connected && x.id !== caller.id)) {
        if ((await publisher.factionOf(hex(other.opponent.signingKey))) === undefined) free.push({ wallet: other.peer });
      }
      return { status: 200, body: { players: free } };
    }

    // What the player's wallet holds of each resource: the wallet proves it, the publisher's account checks the proof.
    if (is("POST", "players", "me", "materials")) {
      const player = this.player(req);
      const connection = await this.connected(player);
      return {
        status: 202,
        body: this.start(player, async () => {
          const holdings = await publisher.holdings(connection);
          // what a hero pass speaks for: the heroes and the guns the wallet proved it owns, by the shop's ids, in one list
          if (holdings !== undefined) {
            this.keepProvenHeroes(hex(connection.opponent.signingKey), [...holdings.heroes.map((h) => h.heroId), ...holdings.weapons.map((w) => w.weaponId)]);
          }
          return holdings === undefined
            ? { ok: false, message: "the wallet proved nothing about what it holds" }
            : { ok: true, message: "the wallet proved what it holds", data: { ...holdings } };
        }),
      };
    }

    // Found a faction on an unassigned parcel, of the asking player's own wallet alone. Nobody is
    // made a member of a faction by someone else's request.
    if (is("POST", "factions")) {
      const caller = await this.connected(this.player(req));
      const parcelId = text((await body(req)).parcelId, "parcelId");
      if ((await publisher.factionOf(hex(caller.opponent.signingKey))) !== undefined) {
        throw new Refused("the wallet is already a member of a faction");
      }
      const first = await publisher.assign(publisher.founding(parcelId, [publisher.member(caller)], hex(randomBytes(16))));
      return { status: 201, body: { factionId: first.factionId, parcelId } };
    }

    // A parcel's base. Members see every plan and its funding; anyone else sees what stands in play.
    if (is("GET", "parcels", null, "base") && b !== undefined) {
      if (!world.parcels.has(b)) throw new HttpError(404, "no such parcel");
      const factionId = publisher.factionOn(b);
      if (factionId === undefined) return { status: 200, body: { parcelId: b, assigned: false } };
      const token = this.bearer(req);
      const connection = token !== "" && Object.hasOwn(store.data.players, token) ? await this.connection(token) : undefined;
      const { mirror, end } = await publisher.follow(factionId);
      const key = connection === undefined ? "" : hex(connection.opponent.signingKey);
      const member = mirror.state.members.some((m) => m.signingKey === key);
      const invited = mirror.state.invited.some((m) => m.signingKey === key);
      const base = await this.base(factionId);
      // Who the members and the invited are, and which votes stand, is the members' to see.
      const people = member
        ? {
            members: mirror.state.members.map((m) => {
              const voters = mirror.state.votes.find((v) => v.target === m.signingKey)?.voters ?? [];
              return { ...this.person(m), you: m.signingKey === key, votes: voters.length, voted: voters.includes(key) };
            }),
            invited: mirror.state.invited.map((m) => this.person(m)),
          }
        : undefined;
      const shown = (s: Structure) => ({
        instanceId: s.instanceId,
        prefabId: s.prefabId,
        x: s.x,
        y: s.y,
        rotation: s.rotation,
        config: s.config,
        standing: s.standing,
        destroyed: s.destroyed,
        funding: member ? this.funding(s.funding, key) : { status: s.funding.status },
      });
      return {
        status: 200,
        body: {
          parcelId: b,
          assigned: true,
          factionId,
          commitment: base.commitment,
          height: base.height,
          member,
          invited,
          people,
          // Whether a member can build now, and the plain message when not (spec 11). Members only.
          building: member ? buildingAt(end) : undefined,
          members: mirror.state.members.length,
          structures: base.structures.filter((s) => member || s.standing).map(shown),
        },
      };
    }

    // A member invites a wallet connected to the game: the first of the two signatures a join takes.
    if (is("POST", "factions", null, "invitations") && b !== undefined) {
      const player = this.player(req);
      const connection = await this.member(player, b);
      const wallet = text((await body(req)).wallet, "wallet");
      const invitee = (await publisher.app.connections()).find((x) => x.connected && x.peer === wallet);
      if (invitee === undefined) throw new Refused("that wallet is not connected to the game");
      if ((await publisher.factionOf(hex(invitee.opponent.signingKey))) !== undefined) {
        throw new Refused("that wallet is already a member of a faction");
      }
      const change: Change = { kind: "invite", member: publisher.member(invitee) };
      return {
        status: 202,
        body: this.start(
          player,
          async () => {
            const done = await publisher.change(b, connection, change);
            return done.taken ? { ok: true, message: "the invitation is part of the faction's state" } : { ok: false, message: done.why };
          },
          b,
        ),
      };
    }

    // A member's change to the faction's state: their wallet signs it and writes it. An invited
    // wallet may send one change: its own acceptance.
    if (is("POST", "factions", null, "changes") && b !== undefined) {
      const player = this.player(req);
      const change = this.change((await body(req)).change);
      // an invitation names a wallet connected to the game, which the server looks up: it is not taken as bytes from a game
      if (change.kind === "invite") throw new HttpError(400, "an invitation is sent as an invitation");
      const connection = await this.member(player, b, change.kind === "accept");
      // a wallet is a member of one faction at a time
      if (change.kind === "accept" && (await publisher.factionOf(hex(connection.opponent.signingKey))) !== undefined) {
        throw new Refused("the wallet is already a member of a faction");
      }
      return {
        status: 202,
        body: this.start(
          player,
          async () => {
            const done = await publisher.change(b, connection, change);
            return done.taken ? { ok: true, message: "the change is part of the base" } : { ok: false, message: done.why };
          },
          b,
        ),
      };
    }

    // A member commits materials to a structure: their wallet locks them in a vault of its own.
    if (is("POST", "factions", null, "commitments") && b !== undefined) {
      const player = this.player(req);
      const connection = await this.member(player, b);
      const asked = await body(req);
      const instanceId = text(asked.instanceId, "instanceId");
      const resource = text(asked.resource, "resource");
      const amount = asked.amount;
      if (typeof amount !== "number" || !Number.isSafeInteger(amount) || amount <= 0) throw new HttpError(400, "the amount is not a whole number above zero");
      return {
        status: 202,
        body: this.start(
          player,
          async () => {
            const result = await publisher.contribute(b, connection, instanceId, resource, String(amount));
            return result.fact === "contributed"
              ? { ok: true, message: `${amount} ${resource} committed` }
              : { ok: false, message: result.reason || result.detail || `the wallet's answer is ${result.outcome}` };
          },
          b,
        ),
      };
    }

    // Anyone who committed to a structure takes back what they still have there, member or not.
    if (is("POST", "factions", null, "withdrawals") && b !== undefined) {
      const player = this.player(req);
      if (!Object.hasOwn(store.data.factions, b)) throw new HttpError(404, "no such faction");
      const connection = await this.connected(player);
      const instanceId = text((await body(req)).instanceId, "instanceId");
      return {
        status: 202,
        body: this.start(
          player,
          async () => {
            const result = await publisher.withdraw(b, connection, instanceId);
            if (result === undefined) return { ok: false, message: "the wallet has nothing committed there" };
            return result.fact === "taken_back"
              ? { ok: true, message: "the materials are back in the wallet" }
              : { ok: false, message: result.reason || result.detail || `the wallet's answer is ${result.outcome}` };
          },
          b,
        ),
      };
    }

    if (is("GET", "requests", null) && b !== undefined) {
      const job = this.#jobs.get(b);
      if (job === undefined || job.player !== this.player(req)) throw new HttpError(404, "no such request");
      return {
        status: 200,
        body: job.state === "pending" ? { state: "pending", stage: job.stage, onWallet: job.onWallet } : { ...job.data, state: "done", ok: job.ok, message: job.message },
      };
    }

    // The publisher's match server records that a battle destroyed a built structure (spec 7.6).
    if (is("POST", "factions", null, "destroyed") && b !== undefined) {
      this.matchServer(req, "records destruction");
      if (!Object.hasOwn(store.data.factions, b)) throw new HttpError(404, "no such faction");
      await publisher.destroyed(b, text((await body(req)).instanceId, "instanceId"));
      this.#bases.delete(b);
      return { status: 200, body: {} };
    }

    // The publisher's match server opens a match between factions: the prize is locked before it is played (spec 10.6).
    if (is("POST", "matches", null) && b !== undefined) {
      this.matchServer(req, "opens a match");
      const factions = (await body(req)).factions;
      if (!Array.isArray(factions) || factions.length > 16 || !factions.every((f) => typeof f === "string")) {
        throw new HttpError(400, "factions is missing");
      }
      return { status: 201, body: await publisher.matches.open(b, factions as string[]) };
    }

    // The publisher's match server says who won, or that nobody did: the publisher's key decides the prize.
    if (is("POST", "matches", null, "outcome") && b !== undefined) {
      this.matchServer(req, "decides a match");
      return { status: 200, body: await publisher.matches.decide(b, text((await body(req)).outcome, "outcome")) };
    }

    // The publisher's match server says what each player's droid mined, once the match is over (owner,
    // 2026-11-22): the publisher's account gives it to each player's wallet. Recorded once.
    if (is("POST", "matches", null, "mined") && b !== undefined) {
      this.matchServer(req, "reports what a match's players mined");
      return { status: 200, body: { mined: await publisher.matches.mined(b, (await body(req)).mined) } };
    }

    // A player's game asks to enter a match. The server says which of the match's factions the
    // connected wallet is a member of, as a ticket the game hands the match server: good once, for
    // this match alone. Nothing of the player's place at this server crosses the match's own wire.
    if (is("POST", "matches", null, "tickets") && b !== undefined) {
      const connection = await this.connected(this.player(req));
      const signingKey = hex(connection.opponent.signingKey);
      if (publisher.matches.outcomeOf(b) !== "") throw new Refused(`match ${b} is over`);
      let factionId = "";
      for (const each of publisher.matches.factionsOf(b)) {
        const { mirror } = await publisher.follow(each);
        if (mirror.state.members.some((m) => m.signingKey === signingKey)) factionId = each;
      }
      if (factionId === "") throw new Refused(`the wallet is a member of no faction of match ${b}`);
      const now = Date.now();
      for (const [held, t] of this.#tickets) if (t.until < now) this.#tickets.delete(held);
      if (this.#tickets.size >= MAX_TICKETS) throw new HttpError(429, "too many players are entering matches: try again in a moment");
      const ticket = hex(randomBytes(16));
      this.#tickets.set(ticket, { matchId: b, factionId, signingKey, wallet: connection.peer, until: now + TICKET_MS });
      return { status: 201, body: { ticket } };
    }

    // The publisher's match server takes a ticket a joining game handed it: who the player is, and their faction.
    if (is("POST", "matches", null, "entries") && b !== undefined) {
      this.matchServer(req, "lets a player into a match");
      const ticket = text((await body(req)).ticket, "ticket");
      const held = this.#tickets.get(ticket);
      if (held === undefined || held.matchId !== b || held.until < Date.now()) throw new Refused("that is no ticket into this match");
      this.#tickets.delete(ticket);
      return { status: 200, body: { factionId: held.factionId, wallet: held.wallet, signingKey: held.signingKey } };
    }

    // The publisher's match server reads what the match is assembled from: each faction's parcel and built structures (spec 10.3).
    if (is("GET", "matches", null, "bases") && b !== undefined) {
      this.matchServer(req, "reads a match's bases");
      // with the bases, what the arena holds to mine: the match server stands that much in deposits
      return { status: 200, body: { matchId: b, bases: await publisher.matchBases(b), mining: publisher.matches.mining } };
    }

    if (is("GET", "matches", null) && b !== undefined) {
      this.matchServer(req, "reads a match's prize");
      return { status: 200, body: await publisher.matches.read(b) };
    }

    void a;
    void c;
    throw new HttpError(404, "no such route");
  }

  /** A change as the game sent it, checked to be one before anything is asked of a wallet. */
  private change(value: unknown): Change {
    try {
      const zero = "00".repeat(32);
      const read = decodeChange(encodeChange({ factionId: zero, parent: zero, change: value as Change }));
      if (read.ok) return read.signedOver.change;
    } catch {
      // not a change: refused below
    }
    throw new HttpError(400, "that is not a change");
  }

  /** A structure's funding as a member's game shows it: who committed what, and which of it is this player's. */
  private funding(funding: Funding, key: string) {
    return {
      status: funding.status,
      resources: funding.resources,
      contributions: funding.contributions
        .filter((x) => x.active && (x.counts || x.owner === key))
        .map((x) => ({ owner: x.owner, resource: x.resource, amount: x.amount, counts: x.counts, leaving: x.leaving, mine: x.owner === key })),
    };
  }
}
