// What the game client asks of the publisher's server, over HTTP and JSON.
//
// The game stays an ordinary game: it shows a connect code, sends the choices
// a player makes in the editor, and draws what the server says the base is.
// It holds no DSM account and no key. A player is known to the server by a
// token the server gave their game; the token only resumes their place in the
// game and is never DSM authority. What the player's wallet signs, commits or
// takes back is asked of the wallet by the publisher's account, and the server
// answers only with facts that account established.

import { randomBytes, timingSafeEqual } from "node:crypto";
import { createServer, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import type { Connection } from "dsm-app-client";
import { hex } from "../faction/canonical.js";
import { decodeChange, encodeChange, type Change } from "../faction/change.js";
import type { Funding } from "../dsm/contributions.js";
import { Publisher, Refused, type Structure } from "./publisher.js";
import type { Store } from "./store.js";
import type { World } from "./world.js";

export interface ServiceOptions {
  publisher: Publisher;
  world: World;
  store: Store;
  /** What the publisher's match server sends to record destruction. Never given to a player's game. */
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
/** The most players the server keeps a record of. */
const MAX_PLAYERS = 10_000;
/** How long a player whose wallet never connected is kept. */
const UNCONNECTED_MS = 60 * 60 * 1000;
/** How long a base read is served again before it is read anew. */
const BASE_FRESH_MS = 1000;

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
  readonly #bases = new Map<string, { at: number; base: Awaited<ReturnType<Publisher["base"]>> }>();

  constructor(private readonly options: ServiceOptions) {}

  /** Serve on `host:port`. Port 0 picks a free one; the server's address says which. */
  listen(port: number, host: string): Promise<Server> {
    const server = createServer((req, res) => void this.handle(req, res));
    return new Promise((resolve, reject) => {
      server.once("error", reject);
      server.listen(port, host, () => resolve(server));
    });
  }

  private async handle(req: IncomingMessage, res: ServerResponse): Promise<void> {
    try {
      const answer = await this.route(req);
      const bytes = Buffer.from(JSON.stringify(answer.body));
      res.writeHead(answer.status, { "content-type": "application/json", "content-length": bytes.length });
      res.end(bytes);
    } catch (e) {
      const status = e instanceof HttpError ? e.status : e instanceof Refused ? 409 : 500;
      const message = e instanceof Error ? e.message : String(e);
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

  /** The player a request comes from, by the token the server gave their game. */
  private player(req: IncomingMessage): string {
    const token = this.bearer(req);
    if (token === "" || !Object.hasOwn(this.options.store.data.players, token)) throw new HttpError(401, "the game is not known to the server");
    return token;
  }

  /** The wallet connected for a player, or undefined while none is. */
  private async connection(player: string): Promise<Connection | undefined> {
    const offer = this.options.store.data.players[player]?.offer;
    if (offer === undefined) return undefined;
    return (await this.options.publisher.app.connections()).find((c) => c.connected && hex(c.raw.offerDigest) === offer);
  }

  /** Drop the players whose connect code no wallet approved within the hour. */
  private async forgetUnconnected(): Promise<void> {
    const players = this.options.store.data.players;
    const stale = Object.entries(players).filter(([, p]) => Date.now() - p.since > UNCONNECTED_MS);
    if (stale.length === 0) return;
    const offers = new Set((await this.options.publisher.app.connections()).map((c) => hex(c.raw.offerDigest)));
    for (const [token, p] of stale) if (!offers.has(p.offer)) delete players[token];
    this.options.store.save();
  }

  private async connected(player: string): Promise<Connection> {
    const connection = await this.connection(player);
    if (connection === undefined) throw new HttpError(409, "no wallet is connected");
    return connection;
  }

  /** The connected wallet of a player, which must be a member of the faction. */
  private async member(player: string, factionId: string): Promise<Connection> {
    if (!Object.hasOwn(this.options.store.data.factions, factionId)) throw new HttpError(404, "no such faction");
    const connection = await this.connected(player);
    const { mirror } = await this.options.publisher.follow(factionId);
    const key = hex(connection.opponent.signingKey);
    if (!mirror.state.members.some((m) => m.signingKey === key)) throw new HttpError(403, "the wallet is not a member of the faction");
    return connection;
  }

  /** Start work a wallet has to answer; the game reads what became of it under the id. */
  private start(
    player: string,
    work: () => Promise<{ ok: boolean; message: string; data?: Record<string, unknown> }>,
    factionId = "",
  ): { request: string } {
    const id = hex(randomBytes(16));
    const job: Job = { player, state: "pending", ok: false, message: "", data: {} };
    this.#jobs.set(id, job);
    work()
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
      await this.forgetUnconnected();
      if (Object.keys(store.data.players).length >= MAX_PLAYERS) throw new HttpError(429, "the server has no room for another player now");
      const grant = world.economy.grant;
      const offer = await publisher.offer(world.economy.resources.map((r) => ({ symbol: r.symbol, ...grant })));
      const player = hex(randomBytes(32));
      store.data.players[player] = { offer: hex(offer.digest), since: Date.now() };
      store.save();
      return { status: 201, body: { player, code: offer.code } };
    }

    if (is("GET", "players", "me")) {
      const connection = await this.connection(this.player(req));
      if (connection === undefined) return { status: 200, body: { connected: false } };
      const signingKey = hex(connection.opponent.signingKey);
      return {
        status: 200,
        body: { connected: true, wallet: connection.peer, signingKey, faction: (await publisher.factionOf(signingKey)) ?? null },
      };
    }

    // What the player's wallet holds of each resource: the wallet proves it, the publisher's account checks the proof.
    if (is("POST", "players", "me", "materials")) {
      const player = this.player(req);
      const connection = await this.connected(player);
      return {
        status: 202,
        body: this.start(player, async () => {
          const materials = await publisher.materials(connection);
          return materials === undefined
            ? { ok: false, message: "the wallet proved nothing about what it holds" }
            : { ok: true, message: "the wallet proved what it holds", data: { materials } };
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
      const { mirror } = await publisher.follow(factionId);
      const key = connection === undefined ? "" : hex(connection.opponent.signingKey);
      const member = mirror.state.members.some((m) => m.signingKey === key);
      const base = await this.base(factionId);
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
          members: mirror.state.members.length,
          structures: base.structures.filter((s) => member || s.standing).map(shown),
        },
      };
    }

    // A member's change to the faction's state: their wallet signs it and writes it.
    if (is("POST", "factions", null, "changes") && b !== undefined) {
      const player = this.player(req);
      const connection = await this.member(player, b);
      const change = this.change((await body(req)).change);
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
      return { status: 200, body: job.state === "pending" ? { state: "pending" } : { ...job.data, state: "done", ok: job.ok, message: job.message } };
    }

    // The publisher's match server records that a battle destroyed a built structure (spec 7.6).
    if (is("POST", "factions", null, "destroyed") && b !== undefined) {
      const want = Buffer.from(this.options.matchSecret);
      const got = Buffer.from(this.bearer(req));
      if (want.length === 0 || got.length !== want.length || !timingSafeEqual(got, want)) {
        throw new HttpError(401, "only the publisher's match server records destruction");
      }
      if (!Object.hasOwn(store.data.factions, b)) throw new HttpError(404, "no such faction");
      await publisher.destroyed(b, text((await body(req)).instanceId, "instanceId"));
      this.#bases.delete(b);
      return { status: 200, body: {} };
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
