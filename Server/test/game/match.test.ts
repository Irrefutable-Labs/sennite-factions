/**
 * A match between two factions, played to its end three times (spec 10.3, 10.6, 7.6). The Unreal
 * game runs its own check (Source/Sennite/Private/Tests/MatchTest.cpp) as three games: the
 * publisher's match server, which has no player of its own, and two players' games that join it.
 * This file is everything outside the game.
 *
 * Three wallets connect to the game by connect codes the publisher's server gave. Two each found a
 * faction, plan a watchtower and commit its steel, so each base has one built structure; the second
 * faction also has a plan nobody funded. The third wallet is in no faction.
 *
 * A wallet enters a match by a ticket the publisher's server gives its game: good once, for that
 * match, and only for a member of one of the match's factions. The match server trades it for the
 * faction the wallet plays for.
 *
 * Then three matches, each opened with the match server's secret, which locks its prize:
 *   the time runs out with nobody brought down: nobody wins, and the prize is the publisher's again;
 *   the first faction's player brings the other's hero down twice: first to the kills to win, and
 *   its member has the prize to collect;
 *   the first faction's player shoots the other's tower down: that faction is out, the first wins,
 *   and the publisher's server has the tower as destroyed with its funding intact.
 * In each, the match server itself tells the publisher's server how it ended, and closes. In the
 * first, the third wallet's game joins the match server too: it is given no ticket, gets no hero,
 * and is put out.
 *
 * Each match is on a different arena. A match's seed chooses what lies between its bases; the three
 * seeds here give floating pieces, a canyon and a traffic deck, and each run is held to that.
 *
 * Tools/check_base_editor.sh runs it (see test/real-dsm/hosts.ts for what it needs).
 */
import { spawn } from "node:child_process";
import { randomBytes } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import type { Server } from "node:http";
import type { AddressInfo } from "node:net";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import type { Connection } from "dsm-app-client";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { hex } from "../../src/faction/canonical.js";
import type { MatchView } from "../../src/publisher/matches.js";
import type { MatchBase } from "../../src/publisher/publisher.js";
import { Service } from "../../src/publisher/service.js";
import { connect, until } from "../real-dsm/hosts.js";
import { accounts, GAME, type Accounts } from "../real-dsm/stage.js";
import { holds } from "../real-dsm/wallets.js";

const REPO = fileURLToPath(new URL("../../..", import.meta.url));
/** What the publisher's match server sends. No player's game has it. */
const SECRET = hex(randomBytes(32));
const STEEL = "STEEL";
/** What a watchtower's recipe needs. */
const TOWER = 300n;
/** Each faction's parcel, and where on it its tower stands (the middle of what it covers, map units). */
const SITES = {
  alice: { parcel: "north-bluff", x: -7680, y: -7680 },
  carol: { parcel: "east-shore", x: -7680, y: 3840 },
} as const;
const NAMES = ["alice", "carol", "dave"] as const;
type Name = (typeof NAMES)[number];

let a: Accounts;
let server: Server;
let url = "";
let secretFile = "";
/** Each player's game's token at the publisher's server, the file their game keeps it in, and their wallet's connection. */
const tokens: Record<string, string> = {};
const files: Record<string, string> = {};
const connections: Record<string, Connection> = {};
const factions: Record<string, string> = {};
const towers: Record<string, string> = {};
let plan = "";

/** One call to the publisher's server: with the match server's secret, another bearer, or none. */
async function call(method: string, path: string, body?: unknown, bearer = SECRET): Promise<{ status: number; body: Record<string, unknown> }> {
  const response = await fetch(url + path, {
    method,
    headers: { ...(bearer === "" ? {} : { authorization: `Bearer ${bearer}` }), "content-type": "application/json" },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });
  return { status: response.status, body: (await response.json()) as Record<string, unknown> };
}

async function bases(matchId: string): Promise<MatchBase[]> {
  const read = await call("GET", `/matches/${matchId}/bases`);
  expect(read.status, JSON.stringify(read.body)).toBe(200);
  return read.body.bases as MatchBase[];
}

async function open(matchId: string): Promise<void> {
  const opened = await call("POST", `/matches/${matchId}`, { factions: [factions.alice, factions.carol] });
  expect(opened.status, JSON.stringify(opened.body)).toBe(201);
}

async function match(matchId: string): Promise<MatchView> {
  const read = await call("GET", `/matches/${matchId}`);
  expect(read.status, JSON.stringify(read.body)).toBe(200);
  return read.body as unknown as MatchView;
}

/** The lines one of the games printed about where things are, without which game said so. */
function printed(log: string, side: string, what: string): string[] {
  return [...log.matchAll(new RegExp(`GAMECHECK ${side}: (${what} .*)`, "g"))]
    .map((m) => (m[1] as string).trim().replace(/(^| )-0(?= |$)/g, "$10"))
    .sort();
}

interface Played {
  server: string;
  attacker: string;
  defender: string;
  /** The third game's log, when one was started: a wallet in no faction of the match. */
  outsider: string;
}

/** The arena each match is played on: its seed, and what that seed puts between the two bases. */
const ARENAS = {
  Time: { seed: 23, between: "Floating" },
  Kills: { seed: 12, between: "Canyon" },
  Wipe: { seed: 4, between: "Traffic" },
} as const;

/**
 * Play one match: the match server and the two players' games, started on `matchId` with the
 * match's rules; with `outsider`, a third game too, whose wallet is in no faction of the match.
 */
async function play(what: "Kills" | "Wipe" | "Time", matchId: string, rules: string[], outsider = ""): Promise<Played> {
  const arena = ARENAS[what];
  const game = spawn(join(REPO, "Tools", "run_match_check.sh"), [what, matchId], {
    cwd: REPO,
    env: {
      ...process.env,
      SENNITE_PUBLISHER_URL: url,
      SENNITE_MATCH_SECRET_FILE: secretFile,
      // alice's faction is the one the match names first: the attacker starts on the first base
      SENNITE_ATTACKER_FILE: files.alice as string,
      SENNITE_DEFENDER_FILE: files.carol as string,
      SENNITE_SERVER_ARGS: `-ini:Game:${rules.map((rule) => `[/Script/Sennite.SenniteMatch]:${rule}`).join(",")}`,
      SENNITE_SEED: String(arena.seed),
      ...(outsider === "" ? {} : { SENNITE_OUTSIDER_FILE: files[outsider] as string }),
    },
  });
  let output = "";
  game.stdout.on("data", (chunk: Buffer) => (output += chunk.toString()));
  game.stderr.on("data", (chunk: Buffer) => (output += chunk.toString()));
  const code = await new Promise<number | null>((resolve) => game.once("exit", resolve));
  console.log(output);
  expect(output).toContain(`CHECK PASSED: match ${what}`);
  expect(code).toBe(0);
  const folder = join(REPO, "Saved", `Match_${what}`);
  const played = {
    server: readFileSync(join(folder, "server.log"), "utf8"),
    attacker: readFileSync(join(folder, "attacker.log"), "utf8"),
    defender: readFileSync(join(folder, "defender.log"), "utf8"),
    outsider: outsider === "" ? "" : readFileSync(join(folder, "outsider.log"), "utf8"),
  };
  // the arena is the one its seed makes
  expect(played.server).toContain(`Arena built: seed ${arena.seed}, 2 bases, connectors: ${arena.between};`);
  // each player's hero is their wallet's faction's, on that faction's base
  expect(played.attacker).toContain(`GAMECHECK attacker: match ${matchId}; plays for faction ${factions.alice} on base 0; wallet ${(connections.alice as Connection).peer}`);
  expect(played.defender).toContain(`GAMECHECK defender: match ${matchId}; plays for faction ${factions.carol} on base 1; wallet ${(connections.carol as Connection).peer}`);
  expect(played.attacker).toContain("GAMECHECK attacker: opened a crate; holds a rifle with 30 rounds and 90 more");
  return played;
}

beforeAll(async () => {
  a = await accounts("match", [...NAMES]);
  server = await new Service({ publisher: a.publisher, world: a.world, store: a.store, matchSecret: SECRET }).listen(0, "127.0.0.1");
  url = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
  secretFile = join(a.dir, "match-secret");
  writeFileSync(secretFile, SECRET, { mode: 0o600 });
  // Each player's game asks the publisher's server for its place and a connect code, and the
  // player's wallet approves the code. The game keeps the place in a file between runs.
  for (const who of NAMES) {
    const asked = await call("POST", "/players", undefined, "");
    expect(asked.status, JSON.stringify(asked.body)).toBe(201);
    tokens[who] = asked.body.player as string;
    connections[who] = await connect(a.app, (a.wallets[who] as Accounts["wallets"][string]).wallet, asked.body.code as string, GAME);
    files[who] = join(a.dir, `${who}-player.json`);
    writeFileSync(files[who] as string, JSON.stringify({ url, player: tokens[who], code: asked.body.code }), { mode: 0o600 });
  }
  for (const who of ["alice", "carol"] as const) {
    const connection = connections[who] as Connection;
    const wallet = (a.wallets[who] as Accounts["wallets"][string]).wallet;
    const site = SITES[who];
    factions[who] = (await a.publisher.assign(a.publisher.founding(site.parcel, [a.publisher.member(connection)], hex(randomBytes(16))))).factionId;
    await until(`the publisher gives ${who} ${TOWER} steel`, 240_000, async () => {
      await a.app.give(connection, STEEL, TOWER.toString());
      return true;
    });
    await until(`${who}'s wallet takes in the steel`, 240_000, async () => ((await holds(wallet, STEEL)).available === TOWER ? true : null));
    // the wallet's own member plans a watchtower and commits what it needs: it is built
    towers[who] = hex(randomBytes(16));
    const placed = await a.publisher.change(factions[who] as string, connection, {
      kind: "place",
      instanceId: towers[who] as string,
      prefabId: "watchtower",
      x: site.x,
      y: site.y,
      rotation: 0,
      config: {},
    });
    expect(placed.taken, `${who}'s tower: ${placed.taken ? "" : placed.why}`).toBe(true);
    const committed = await a.publisher.contribute(factions[who] as string, connection, towers[who] as string, STEEL, TOWER.toString());
    expect(committed.fact, `${who} commits the steel: ${committed.reason || committed.detail}`).toBe("contributed");
  }
  // carol's faction also plans a lookout nobody funds: a plan, which no match shows
  plan = hex(randomBytes(16));
  const planned = await a.publisher.change(factions.carol as string, connections.carol as Connection, {
    kind: "place",
    instanceId: plan,
    prefabId: "lookout",
    x: SITES.carol.x + 1920,
    y: SITES.carol.y,
    rotation: 0,
    config: {},
  });
  expect(planned.taken, `carol's plan: ${planned.taken ? "" : planned.why}`).toBe(true);
  for (const who of ["alice", "carol"]) {
    await until(`${who}'s tower stands in play`, 240_000, async () => {
      const base = await a.publisher.base(factions[who] as string);
      return base.structures.find((x) => x.instanceId === towers[who])?.standing ? true : null;
    });
  }
});

afterAll(async () => {
  server?.close();
  await a?.stop();
});

describe("a match between two factions", () => {
  it("only the match server reads what a match is assembled from, and only of a match it opened", async () => {
    expect((await call("GET", "/matches/arena-time/bases")).status, "a match nobody opened").toBe(409);
    await open("arena-time");
    for (const bearer of ["", hex(randomBytes(32)), tokens.alice as string]) {
      expect((await call("GET", "/matches/arena-time/bases", undefined, bearer)).status).toBe(401);
    }
  });

  it("a match is assembled from each faction's parcel and built structures, in the match's order; a plan is no part of it", async () => {
    const read = await bases("arena-time");
    expect(read.map((b) => [b.factionId, b.parcelId])).toEqual([
      [factions.alice, SITES.alice.parcel],
      [factions.carol, SITES.carol.parcel],
    ]);
    for (const [i, who] of (["alice", "carol"] as const).entries()) {
      const base = read[i] as MatchBase;
      expect(base.boundary).toEqual(a.world.parcels.get(SITES[who].parcel)?.boundary);
      expect(base.structures).toEqual([
        { instanceId: towers[who], prefabId: "watchtower", x: SITES[who].x, y: SITES[who].y, rotation: 0, width: 960, depth: 960 },
      ]);
    }
    // the plan is in carol's faction's state, and in no match
    expect((await a.publisher.base(factions.carol as string)).structures.map((x) => [x.instanceId, x.standing])).toContainEqual([plan, false]);
  });

  it("a wallet enters a match by a ticket: good once, for that match, and only for a member of one of its factions", async () => {
    const tickets = "/matches/arena-time/tickets";
    const entries = "/matches/arena-time/entries";
    // no game the server knows, and a wallet in no faction of the match, get none; nor does anyone for a match nobody opened
    expect((await call("POST", tickets, {}, "")).status).toBe(401);
    expect((await call("POST", tickets, {}, hex(randomBytes(32)))).status).toBe(401);
    const outsider = await call("POST", tickets, {}, tokens.dave as string);
    expect(outsider.status, JSON.stringify(outsider.body)).toBe(409);
    expect(outsider.body.error).toBe("the wallet is a member of no faction of match arena-time");
    expect((await call("POST", "/matches/arena-nobody-opened/tickets", {}, tokens.alice as string)).status).toBe(409);

    const given = await call("POST", tickets, {}, tokens.alice as string);
    expect(given.status, JSON.stringify(given.body)).toBe(201);
    const ticket = given.body.ticket as string;
    expect(ticket).toMatch(/^[0-9a-f]{32}$/);
    // only the match server trades a ticket in: not a player's game, not even the one it was given to
    for (const bearer of ["", tokens.alice as string, hex(randomBytes(32))]) {
      expect((await call("POST", entries, { ticket }, bearer)).status).toBe(401);
    }
    // not for another match, and not a ticket nobody gave
    expect((await call("POST", "/matches/arena-other/entries", { ticket })).status).toBe(409);
    expect((await call("POST", entries, { ticket: hex(randomBytes(16)) })).status).toBe(409);
    // the match server learns who it is and which faction they play for
    const entered = await call("POST", entries, { ticket });
    expect(entered.status, JSON.stringify(entered.body)).toBe(200);
    expect(entered.body).toEqual({
      factionId: factions.alice,
      wallet: (connections.alice as Connection).peer,
      signingKey: hex((connections.alice as Connection).opponent.signingKey),
    });
    // once
    expect((await call("POST", entries, { ticket })).status).toBe(409);
    const carols = await call("POST", tickets, {}, tokens.carol as string);
    expect(((await call("POST", entries, { ticket: carols.body.ticket })).body as { factionId: string }).factionId).toBe(factions.carol);
  });

  it("the time runs out with nobody brought down: both bases stood the same on both machines, nobody won, and the prize is the publisher's again; a wallet in neither faction is put out", async () => {
    const before = await bases("arena-time");
    const played = await play("Time", "arena-time", ["MatchSeconds=90", "EnterSeconds=30"], "dave");

    // dave's wallet is in neither faction: his game joined, was given no ticket and no hero, and was put out
    expect(played.outsider).toContain("GAMECHECK outsider: joined the match server of match arena-time");
    expect(played.outsider).toContain("GAMECHECK outsider: refused: the wallet is a member of no faction of match arena-time");
    expect(played.outsider).toMatch(/GAMECHECK outsider: put out of the match \d+ s after it was refused/);
    expect(played.server).toContain("Match arena-time: a game that showed no ticket is put out");
    expect(played.server).not.toContain((connections.dave as Connection).peer);

    // The two machines built the same arena and hold the same structures at the same places.
    const plots = printed(played.attacker, "attacker", "base");
    expect(plots).toHaveLength(2);
    expect(printed(played.defender, "defender", "base")).toEqual(plots);
    const stood = printed(played.attacker, "attacker", "structure");
    expect(stood).toHaveLength(2);
    expect(printed(played.defender, "defender", "structure")).toEqual(stood);
    // Each structure stands where its parcel puts it: as far from its base's middle as it is from
    // the middle of what its parcel spans, on the base of its faction's place in the match.
    const centre = new Map(
      plots.map((line) => {
        const [, index, , x, y, z] = line.split(" ");
        return [Number(index), [Number(x), Number(y), Number(z)]] as const;
      }),
    );
    for (const [i, base] of before.entries()) {
      const xs = base.boundary.map((p) => p[0]);
      const ys = base.boundary.map((p) => p[1]);
      const middle = [(Math.min(...xs) + Math.max(...xs)) / 2, (Math.min(...ys) + Math.max(...ys)) / 2] as const;
      const at = centre.get(i) as readonly number[];
      for (const built of base.structures) {
        const want = [(at[0] as number) + built.x - middle[0], (at[1] as number) + built.y - middle[1], at[2] as number].map(Math.round);
        expect(stood).toContain(`structure ${built.instanceId} ${built.prefabId} base ${i} at ${want.join(" ")} yaw ${built.rotation}`);
      }
    }

    for (const side of ["attacker", "defender"] as const) {
      expect(played[side]).toContain(`GAMECHECK ${side}: over: void because the time ran out with the most kills shared; kills ${factions.alice} 0 ${factions.carol} 0;`);
      expect(played[side]).toContain(`GAMECHECK ${side}: the publisher's server recorded the outcome`);
    }
    expect(played.server).toContain("Match arena-time: the publisher's server recorded the outcome void");

    // Nobody won: every share of the prize goes back to the publisher, and nobody has one to collect.
    expect((await match("arena-time")).outcome).toBe("void");
    const settled = await until("every share is the publisher's again", 600_000, async () => {
      await a.publisher.matches.tick();
      const read = await match("arena-time");
      return read.shares.every((x) => x.state === "returned") ? read : null;
    });
    expect(settled.shares.length).toBeGreaterThan(0);
    for (const who of ["alice", "carol"]) {
      expect((await call("GET", "/players/me/prizes", undefined, tokens[who] as string)).body.prizes).toEqual([]);
    }
    // both towers stand as they did
    expect((await bases("arena-time")).map((b) => b.structures.map((x) => x.instanceId))).toEqual([[towers.alice], [towers.carol]]);
  });

  it("first to the kills to win: a hero that falls returns on its own base, the match ends at the last kill, and the winner's member has the prize to collect", async () => {
    await open("arena-kills");
    const played = await play("Kills", "arena-kills", ["KillsToWin=2"]);
    for (const side of ["attacker", "defender"] as const) {
      expect(played[side]).toContain(`GAMECHECK ${side}: over: ${factions.alice} because first to 2 kills; kills ${factions.alice} 2 ${factions.carol} 0;`);
    }
    expect(played.attacker).toMatch(/GAMECHECK attacker: crossed to the other base in \d+ s/);
    expect(played.defender).toMatch(/GAMECHECK defender: its hero returned after \d+\.\d s, \d+ from the middle of its base, health 100, 1 weapons/);
    expect(played.defender).toMatch(/GAMECHECK defender: over: .*; \d+ shots, 2 falls, 1 returns/);
    expect(played.server).toContain(`Match arena-kills: the publisher's server recorded the outcome ${factions.alice}`);

    // The publisher's key decided the prize for alice's faction: her shares wait for her wallet, carol's go back.
    const read = await match("arena-kills");
    expect(read.outcome).toBe(factions.alice);
    expect(read.shares.filter((x) => x.factionId === factions.alice).map((x) => x.state)).toEqual(["waiting", "waiting", "waiting"]);
    const waiting = (await call("GET", "/players/me/prizes", undefined, tokens.alice as string)).body.prizes as { matchId: string; resource: string; amount: number }[];
    expect(waiting.map((w) => [w.matchId, w.resource, w.amount]).sort()).toEqual(
      Object.entries(a.world.matches.prize)
        .map(([resource, amount]) => ["arena-kills", resource, amount])
        .sort(),
    );
    expect((await call("GET", "/players/me/prizes", undefined, tokens.carol as string)).body.prizes).toEqual([]);
    // nothing was destroyed
    expect((await bases("arena-kills")).map((b) => b.structures.map((x) => x.instanceId))).toEqual([[towers.alice], [towers.carol]]);
  });

  it("a base wiped: the faction is out, the other wins, and the publisher's server has the structure as destroyed with its funding intact", async () => {
    await open("arena-wipe");
    const played = await play("Wipe", "arena-wipe", ["KillsToWin=10"]);
    for (const side of ["attacker", "defender"] as const) {
      expect(played[side]).toContain(
        `GAMECHECK ${side}: over: ${factions.alice} because the last faction with a base standing; kills ${factions.alice} 0 ${factions.carol} 0 out;`,
      );
      expect(played[side]).toContain(`GAMECHECK ${side}: watchtower ${towers.carol} is gone`);
    }
    expect(played.server).toContain(`the publisher's server recorded ${towers.carol} destroyed`);
    expect(played.server).toContain(`Match arena-wipe: the publisher's server recorded the outcome ${factions.alice}`);
    expect((await match("arena-wipe")).outcome).toBe(factions.alice);

    // What came of it (spec 7.6): the structure is a plan again, with its funding intact; nothing of
    // its owner's moved; the other base is as it was.
    const base = await a.publisher.base(factions.carol as string);
    const tower = base.structures.find((x) => x.instanceId === towers.carol);
    expect(tower).toMatchObject({ standing: false, destroyed: true });
    expect(tower?.funding.status).toBe("fully funded");
    expect(await holds((a.wallets.carol as Accounts["wallets"][string]).wallet, STEEL)).toEqual({ available: 0n, locked: TOWER });
    expect((await a.publisher.base(factions.alice as string)).structures.find((x) => x.instanceId === towers.alice)).toMatchObject({
      standing: true,
      destroyed: false,
    });
    expect((await bases("arena-wipe")).map((b) => b.structures.map((x) => x.instanceId))).toEqual([[towers.alice], []]);
  });

  it("it is not rebuilt until a member rebuilds it: then the next match has it again", async () => {
    const rebuilt = await a.publisher.change(factions.carol as string, connections.carol as Connection, { kind: "confirm", instanceId: towers.carol as string });
    expect(rebuilt.taken, rebuilt.taken ? "" : rebuilt.why).toBe(true);
    await until("the tower stands in play again", 240_000, async () => {
      const base = await a.publisher.base(factions.carol as string);
      return base.structures.find((x) => x.instanceId === towers.carol)?.standing ? true : null;
    });
    expect((await bases("arena-wipe")).map((b) => b.structures.map((x) => x.instanceId))).toEqual([[towers.alice], [towers.carol]]);
    expect(await holds((a.wallets.carol as Accounts["wallets"][string]).wallet, STEEL)).toEqual({ available: 0n, locked: TOWER });
  });
});
