/**
 * What the game client asks of the publisher's server, end to end on DSM (spec section 13, build
 * step 4, the server's half): a player's game gets a connect code, their wallet approves it, and
 * from then on everything the game sends over HTTP is carried out by the player's own wallet and
 * answered from facts the publisher's account established.
 *
 * The publisher's account and the two wallets are real `dsm-app-host` accounts on the storage set.
 * The game here is this file's HTTP calls: exactly what the Unreal client sends.
 *
 * Run with `npm run test:dsm` (see hosts.ts for what it needs).
 */
import { randomBytes } from "node:crypto";
import type { Server } from "node:http";
import type { AddressInfo } from "node:net";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { hex } from "../../src/faction/canonical.js";
import { Service } from "../../src/publisher/service.js";
import { until } from "./hosts.js";
import { accounts, GAME, type Accounts } from "./stage.js";
import { holds } from "./wallets.js";

const PARCEL = "north-bluff";
const SECRET = hex(randomBytes(32));
const tower = hex(randomBytes(16));

let a: Accounts;
let server: Server;
let base = "";
const game: Record<string, string> = {};

type Json = Record<string, any>;

/** One HTTP call of a player's game. */
async function call(method: string, path: string, token = "", body?: Json): Promise<{ status: number; body: Json }> {
  const headers: Record<string, string> = {};
  if (token !== "") headers.authorization = `Bearer ${token}`;
  if (body !== undefined) headers["content-type"] = "application/json";
  const response = await fetch(base + path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
  return { status: response.status, body: (await response.json()) as Json };
}

/** Send something a wallet has to answer, and wait for what became of it. */
async function settled(token: string, path: string, body: Json): Promise<Json> {
  const started = await call("POST", path, token, body);
  expect(started.status, JSON.stringify(started.body)).toBe(202);
  return until(`${path} settles`, 300_000, async () => {
    const r = await call("GET", `/requests/${started.body.request}`, token);
    return r.body.state === "done" ? r.body : null;
  });
}

const theBase = async (token = ""): Promise<Json> => (await call("GET", `/parcels/${PARCEL}/base`, token)).body;

/** A player opens the game: it shows a connect code, and their wallet approves it. */
async function enter(name: string): Promise<void> {
  const entered = await call("POST", "/players");
  expect(entered.status).toBe(201);
  const token = entered.body.player as string;
  expect((await call("GET", "/players/me", token)).body).toEqual({ connected: false });
  const wallet = a.wallets[name]!.wallet;
  const preview = await wallet.preview(entered.body.code as string);
  expect(preview.name).toBe(GAME);
  await wallet.approve(preview);
  const me = await until(`${name}'s game sees the wallet connected`, 240_000, async () => {
    const r = await call("GET", "/players/me", token);
    return r.body.connected === true ? r.body : null;
  });
  expect(me.wallet).toBe(await wallet.id());
  expect(me.faction).toBeNull();
  game[name] = token;
}

beforeAll(async () => {
  a = await accounts("service", ["alice", "bob"]);
  server = await new Service({ publisher: a.publisher, world: a.world, store: a.store, matchSecret: SECRET }).listen(0, "127.0.0.1");
  base = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
});

afterAll(async () => {
  server?.close();
  await a?.stop();
});

describe("the game's side of the publisher's server", () => {
  it("serves the publisher's world: the catalogue and the parcels", async () => {
    const world = (await call("GET", "/world")).body;
    expect(world.catalogue.version).toBe(a.world.catalogue.version);
    expect(world.catalogue.prefabs.map((p: Json) => p.id)).toContain("watchtower");
    expect(world.parcels.find((p: Json) => p.id === PARCEL)).toMatchObject({ factionId: "" });
  });

  it("gives each player's game a connect code of its own, and knows the wallet once it approves", async () => {
    await enter("alice");
    await enter("bob");
    expect((await call("GET", "/players/me", "not-a-token")).status).toBe(401);

    // One address is given only a few connect codes a minute.
    const asked = [];
    for (let i = 0; i < 12; i++) asked.push((await call("POST", "/players")).status);
    expect(asked.filter((status) => status === 201)).toHaveLength(8);
    expect(asked.filter((status) => status === 429)).toHaveLength(4);
  });

  it("founds a faction for a connected wallet on an unassigned parcel, once", async () => {
    expect(await theBase()).toEqual({ parcelId: PARCEL, assigned: false });
    const founded = await call("POST", "/factions", game.alice, { parcelId: PARCEL });
    expect(founded.status, JSON.stringify(founded.body)).toBe(201);
    game.faction = founded.body.factionId as string;

    expect((await call("GET", "/players/me", game.alice)).body.faction).toEqual({ factionId: game.faction, parcelId: PARCEL });
    expect((await call("GET", "/world")).body.parcels.find((p: Json) => p.id === PARCEL).factionId).toBe(game.faction);
    const again = await call("POST", "/factions", game.bob, { parcelId: PARCEL });
    expect(again.status).toBe(409);
    expect(again.body.error).toMatch(/already assigned/);
  });

  it("carries a member's placement to their wallet; the plan shows to members only", async () => {
    const [ox, oy] = [-8160, -8160];
    const place = { kind: "place", instanceId: tower, prefabId: "watchtower", x: ox + 480, y: oy + 480, rotation: 0, config: {} };
    expect(await settled(game.alice!, `/factions/${game.faction}/changes`, { change: place })).toMatchObject({ ok: true });

    const mine = await theBase(game.alice);
    expect(mine).toMatchObject({ member: true, members: 1, height: 1 });
    expect(mine.structures).toHaveLength(1);
    expect(mine.structures[0]).toMatchObject({ instanceId: tower, prefabId: "watchtower", standing: false, destroyed: false });
    expect(mine.structures[0].funding).toMatchObject({
      status: "needs resources",
      resources: [{ resource: "STEEL", needed: 300, funded: 0, shortfall: 300 }],
    });

    // Bob is no member: his game is refused, and he and a visitor with no wallet see nothing, because nothing stands.
    expect((await call("POST", `/factions/${game.faction}/changes`, game.bob, { change: place })).status).toBe(403);
    expect((await theBase(game.bob)).structures).toEqual([]);
    expect((await theBase()).structures).toEqual([]);

    // A placement that breaks a rule is refused before anything is written.
    const outside = { ...place, instanceId: hex(randomBytes(16)), x: ox - 480 };
    expect(await settled(game.alice!, `/factions/${game.faction}/changes`, { change: outside })).toEqual({
      state: "done",
      ok: false,
      message: "the placement is not inside the parcel boundary",
    });
    expect((await call("POST", `/factions/${game.faction}/changes`, game.alice, { change: { kind: "fly" } })).status).toBe(400);
    expect((await theBase(game.alice)).height).toBe(1);
  });

  it("the commit button drives the wallet; the fully funded plan stands in play for everyone", async () => {
    const alice = a.wallets.alice!.wallet;
    // The publisher can send once the wallet's side of their relationship is in place, which follows the connect.
    const id = await alice.id();
    await until("the publisher gives alice 300 steel", 240_000, async () => {
      await a.app.give(id, "STEEL", "300");
      return true;
    });
    await until("alice's wallet takes in 300 steel", 240_000, async () => ((await holds(alice, "STEEL")).available === 300n ? true : null));

    // The game shows what the wallet holds only from a proof the publisher's account checked.
    expect(await settled(game.alice!, "/players/me/materials", {})).toMatchObject({ ok: true, materials: { WOOD: 0, STONE: 0, STEEL: 300 } });

    const commit = { instanceId: tower, resource: "STEEL", amount: 300 };
    expect(await settled(game.alice!, `/factions/${game.faction}/commitments`, commit)).toMatchObject({ ok: true });
    expect(await holds(alice, "STEEL")).toEqual({ available: 0n, locked: 300n });

    const mine = await theBase(game.alice);
    expect(mine.structures[0]).toMatchObject({ standing: true });
    expect(mine.structures[0].funding.status).toBe("fully funded");
    expect(mine.structures[0].funding.contributions).toMatchObject([{ resource: "STEEL", amount: 300, counts: true, mine: true }]);

    // A visitor sees the built structure and its status, and nothing of anyone's wallet.
    const seen = await theBase();
    expect(seen.member).toBe(false);
    expect(seen.structures).toMatchObject([{ instanceId: tower, prefabId: "watchtower", standing: true, funding: { status: "fully funded" } }]);
    expect(seen.structures[0].funding.contributions).toBeUndefined();
  });

  it("a battle's destruction is recorded by the match server only; a member's confirmation rebuilds it", async () => {
    const report = (token: string) => call("POST", `/factions/${game.faction}/destroyed`, token, { instanceId: tower });
    expect((await report(game.alice!)).status).toBe(401);
    expect((await report(SECRET)).status).toBe(200);

    expect((await theBase()).structures).toEqual([]);
    const plan = (await theBase(game.alice)).structures[0];
    expect(plan).toMatchObject({ standing: false, destroyed: true });
    expect(plan.funding.status).toBe("fully funded");

    expect(await settled(game.alice!, `/factions/${game.faction}/changes`, { change: { kind: "confirm", instanceId: tower } })).toMatchObject({ ok: true });
    expect((await theBase()).structures).toMatchObject([{ instanceId: tower, standing: true }]);
  });

  it("a member invites a connected wallet, it joins by its own acceptance, and a vote removes it", async () => {
    const bob = await a.wallets.bob!.wallet.id();
    const alice = await a.wallets.alice!.wallet.id();

    // Bob is connected and in no faction: Alice's game lists him; he cannot change anything yet.
    expect((await call("GET", "/players/free", game.alice)).body.players.map((p: Json) => p.wallet)).toContain(bob);
    expect((await call("POST", `/factions/${game.faction}/changes`, game.bob, { change: { kind: "accept" } })).status).toBe(403);
    expect((await call("POST", `/factions/${game.faction}/invitations`, game.bob, { wallet: bob })).status).toBe(403);
    // Who is connected is told to members only, and an invitation is not taken as bytes from a game.
    expect((await call("GET", "/players/free", game.bob)).status).toBe(403);
    const forged = { kind: "invite", member: { genesis: "00".repeat(32), deviceId: "00".repeat(32), signingKey: "07" } };
    expect((await call("POST", `/factions/${game.faction}/changes`, game.alice, { change: forged })).status).toBe(400);

    expect(await settled(game.alice!, `/factions/${game.faction}/invitations`, { wallet: bob })).toMatchObject({ ok: true });
    expect((await call("GET", "/players/me", game.bob)).body.invitations).toEqual([{ factionId: game.faction, parcelId: PARCEL }]);
    const invited = await theBase(game.bob);
    expect(invited).toMatchObject({ member: false, invited: true });
    expect(invited.people).toBeUndefined();
    // Invited is not a member: nothing but his own acceptance is taken from him.
    expect((await call("POST", `/factions/${game.faction}/changes`, game.bob, { change: { kind: "dismantle", instanceId: tower } })).status).toBe(403);

    expect(await settled(game.bob!, `/factions/${game.faction}/changes`, { change: { kind: "accept" } })).toMatchObject({ ok: true });
    const joined = await theBase(game.bob);
    expect(joined).toMatchObject({ member: true, members: 2 });
    expect(joined.people.members.map((p: Json) => [p.wallet, p.you])).toEqual([
      [alice, false],
      [bob, true],
    ]);
    expect((await call("GET", "/players/me", game.bob)).body).toMatchObject({ faction: { factionId: game.faction }, invitations: [] });

    // In a faction of two, the other member alone is the majority: Alice's vote removes Bob at once.
    const bobsKey = joined.people.members[1].signingKey as string;
    expect(await settled(game.alice!, `/factions/${game.faction}/changes`, { change: { kind: "vote", signingKey: bobsKey } })).toMatchObject({ ok: true });
    expect(await theBase(game.alice)).toMatchObject({ members: 1 });
    expect(await theBase(game.bob)).toMatchObject({ member: false, invited: false });
  });

  it("a withdrawal goes back to the wallet; the structure stands and shows its shortfall", async () => {
    expect(await settled(game.alice!, `/factions/${game.faction}/withdrawals`, { instanceId: tower })).toMatchObject({ ok: true });
    expect(await holds(a.wallets.alice!.wallet, "STEEL")).toEqual({ available: 300n, locked: 0n });
    const mine = (await theBase(game.alice)).structures[0];
    expect(mine).toMatchObject({ standing: true });
    expect(mine.funding).toMatchObject({ status: "needs resources", resources: [{ resource: "STEEL", needed: 300, funded: 0, shortfall: 300 }] });
    expect(await settled(game.alice!, `/factions/${game.faction}/withdrawals`, { instanceId: tower })).toMatchObject({
      ok: false,
      message: "the wallet has nothing committed there",
    });
  });

  it("a hero pass tells one game server, once, the heroes the wallet proved, and nothing else", async () => {
    // Alice's wallet has just proved its holdings: it owns no hero bought in the shop.
    expect(await settled(game.alice!, "/players/me/materials", {})).toMatchObject({ ok: true });
    const first = await call("POST", "/players/me/hero-pass", game.alice);
    expect(first.status).toBe(201);
    const pass = first.body.pass as string;
    expect(pass).toMatch(/^[0-9a-f]{32}$/);

    // Any game server may ask, with no token of its own; it learns the proven heroes and nothing more.
    const asked = await call("GET", `/hero-passes/${pass}`);
    expect(asked).toEqual({ status: 200, body: { heroes: [] } });
    // Asking uses it up: a server that kept the pass cannot use it again, or show it anywhere else.
    expect((await call("GET", `/hero-passes/${pass}`)).status).toBe(404);

    // A made-up pass, and a pass the wallet's next one replaced, are no passes.
    expect((await call("GET", `/hero-passes/${hex(randomBytes(16))}`)).status).toBe(404);
    const second = await call("POST", "/players/me/hero-pass", game.alice);
    const third = await call("POST", "/players/me/hero-pass", game.alice);
    expect([second.status, third.status]).toEqual([201, 201]);
    expect((await call("GET", `/hero-passes/${second.body.pass}`)).status).toBe(404);
    expect((await call("GET", `/hero-passes/${third.body.pass}`)).status).toBe(200);

    // A game with no wallet connected gets no pass. (The tests before asked this address for its ten connect
    // codes of the minute: the minute is waited out first.)
    await new Promise((resolve) => setTimeout(resolve, 61_000));
    const made = await call("POST", "/players");
    expect(made.status, JSON.stringify(made.body)).toBe(201);
    const refused = await call("POST", "/players/me/hero-pass", made.body.player as string);
    expect(refused.status, JSON.stringify(refused.body)).toBe(409);
  });
});
