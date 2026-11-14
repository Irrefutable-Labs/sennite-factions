/**
 * Two factions' bases in one running match (spec 10.3, Stage 3: "both bases load into one Unreal
 * world"; spec 7.6: a structure destroyed in battle). The Unreal game runs its own check
 * (Source/Sennite/Private/Tests/MatchBasesTest.cpp) as two games: the publisher's match server
 * hosting the arena, and a player who joins it. This file is everything outside the game.
 *
 * Two wallets each found a faction, plan a watchtower and commit its steel, so each base has one
 * built structure; the second faction also has a plan nobody funded. The match server's secret
 * opens a match between them. The two games are started on that match. Each prints where every
 * base and structure is in its own world; this file holds the two against each other and against
 * what the publisher's server gave. The host shoots the tower on its base down; the publisher's
 * server records it, and this file reads what came of it from the publisher's account and the
 * owner's wallet.
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
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { hex } from "../../src/faction/canonical.js";
import type { MatchBase } from "../../src/publisher/publisher.js";
import { Service } from "../../src/publisher/service.js";
import { until } from "../real-dsm/hosts.js";
import { player, stage, type Stage } from "../real-dsm/stage.js";
import { holds } from "../real-dsm/wallets.js";

const REPO = fileURLToPath(new URL("../../..", import.meta.url));
const FOLDER = join(REPO, "Saved", "MatchBases");
/** What the publisher's match server sends. No player's game has it. */
const SECRET = hex(randomBytes(32));
const MATCH = "arena-1";
const STEEL = "STEEL";
/** What a watchtower's recipe needs. */
const TOWER = 300n;
/** Each faction's parcel, and where on it its tower stands (the middle of what it covers, map units). */
const SITES = {
  alice: { parcel: "north-bluff", x: -7680, y: -7680 },
  carol: { parcel: "east-shore", x: -7680, y: 3840 },
} as const;

let s: Stage;
let server: Server;
let url = "";
const factions: Record<string, string> = {};
const towers: Record<string, string> = {};
let plan = "";

/** One call with the match server's secret, or with another bearer. */
async function call(method: string, path: string, body?: unknown, bearer = SECRET): Promise<{ status: number; body: Record<string, unknown> }> {
  const response = await fetch(url + path, {
    method,
    headers: { ...(bearer === "" ? {} : { authorization: `Bearer ${bearer}` }), "content-type": "application/json" },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });
  return { status: response.status, body: (await response.json()) as Record<string, unknown> };
}

async function bases(): Promise<MatchBase[]> {
  const read = await call("GET", `/matches/${MATCH}/bases`);
  expect(read.status, JSON.stringify(read.body)).toBe(200);
  return read.body.bases as MatchBase[];
}

/** The lines one of the two games printed about where things are, without which game said so. */
function printed(log: string, side: string, what: string): string[] {
  return [...log.matchAll(new RegExp(`GAMECHECK ${side}: (${what} .*)`, "g"))]
    .map((m) => (m[1] as string).trim().replace(/(^| )-0(?= |$)/g, "$10"))
    .sort();
}

beforeAll(async () => {
  s = await stage("match-bases", ["alice", "carol"]);
  server = await new Service({ publisher: s.publisher, world: s.world, store: s.store, matchSecret: SECRET }).listen(0, "127.0.0.1");
  url = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
  for (const who of ["alice", "carol"] as const) {
    const p = player(s, who);
    const site = SITES[who];
    factions[who] = (await s.publisher.assign(s.publisher.founding(site.parcel, [p.member], hex(randomBytes(16))))).factionId;
    await until(`the publisher gives ${who} ${TOWER} steel`, 240_000, async () => {
      await s.app.give(p.connection, STEEL, TOWER.toString());
      return true;
    });
    await until(`${who}'s wallet takes in the steel`, 240_000, async () => ((await holds(p.wallet, STEEL)).available === TOWER ? true : null));
    // the wallet's own member plans a watchtower and commits what it needs: it is built
    towers[who] = hex(randomBytes(16));
    const placed = await s.publisher.change(factions[who] as string, p.connection, {
      kind: "place",
      instanceId: towers[who] as string,
      prefabId: "watchtower",
      x: site.x,
      y: site.y,
      rotation: 0,
      config: {},
    });
    expect(placed.taken, `${who}'s tower: ${placed.taken ? "" : placed.why}`).toBe(true);
    const committed = await s.publisher.contribute(factions[who] as string, p.connection, towers[who] as string, STEEL, TOWER.toString());
    expect(committed.fact, `${who} commits the steel: ${committed.reason || committed.detail}`).toBe("contributed");
  }
  // carol's faction also plans a lookout nobody funds: a plan, which no match shows
  plan = hex(randomBytes(16));
  const planned = await s.publisher.change(factions.carol as string, player(s, "carol").connection, {
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
      const base = await s.publisher.base(factions[who] as string);
      return base.structures.find((x) => x.instanceId === towers[who])?.standing ? true : null;
    });
  }
});

afterAll(async () => {
  server?.close();
  await s?.stop();
});

describe("two factions' bases in one running match", () => {
  it("only the match server reads what a match is assembled from, and only of a match it opened", async () => {
    expect((await call("GET", `/matches/${MATCH}/bases`)).status, "a match nobody opened").toBe(409);
    const opened = await call("POST", `/matches/${MATCH}`, { factions: [factions.alice, factions.carol] });
    expect(opened.status, JSON.stringify(opened.body)).toBe(201);
    for (const bearer of ["", hex(randomBytes(32))]) {
      expect((await call("GET", `/matches/${MATCH}/bases`, undefined, bearer)).status).toBe(401);
    }
  });

  it("a match is assembled from each faction's parcel and built structures, in the match's order; a plan is no part of it", async () => {
    const read = await bases();
    expect(read.map((b) => [b.factionId, b.parcelId])).toEqual([
      [factions.alice, SITES.alice.parcel],
      [factions.carol, SITES.carol.parcel],
    ]);
    for (const [i, who] of (["alice", "carol"] as const).entries()) {
      const base = read[i] as MatchBase;
      expect(base.boundary).toEqual(s.world.parcels.get(SITES[who].parcel)?.boundary);
      expect(base.structures).toEqual([
        { instanceId: towers[who], prefabId: "watchtower", x: SITES[who].x, y: SITES[who].y, rotation: 0, width: 960, depth: 960 },
      ]);
    }
    // the plan is in carol's faction's state, and in no match
    expect((await s.publisher.base(factions.carol as string)).structures.map((x) => [x.instanceId, x.standing])).toContainEqual([plan, false]);
  });

  it("both bases stand in one running match, the same on both machines; one is shot down, for both, and the publisher's server records it", async () => {
    const before = await bases();
    const secretFile = join(s.dir, "match-secret");
    writeFileSync(secretFile, SECRET, { mode: 0o600 });
    const game = spawn(join(REPO, "Tools", "run_two_player_check.sh"), ["Sennite.MatchBases", "MatchBases"], {
      cwd: REPO,
      env: {
        ...process.env,
        SENNITE_MAP: `/Game/Maps/Arena?Seed=11?Bases=2?Connector=Bridges?Match=${MATCH}`,
        // the hosting game is the publisher's match server; the joining player's game is given nothing
        SENNITE_HOST_ARGS: `-PublisherUrl=${url} -MatchSecretFile=${secretFile}`,
      },
    });
    let output = "";
    game.stdout.on("data", (chunk: Buffer) => (output += chunk.toString()));
    game.stderr.on("data", (chunk: Buffer) => (output += chunk.toString()));
    const code = await new Promise<number | null>((resolve) => game.once("exit", resolve));
    console.log(output);
    expect(output).toContain("CHECK PASSED: Sennite.MatchBases");
    expect(code).toBe(0);

    const host = readFileSync(join(FOLDER, "host.log"), "utf8");
    const joiner = readFileSync(join(FOLDER, "joiner.log"), "utf8");
    // The two machines built the same arena and hold the same structures at the same places.
    const plots = printed(host, "host", "base");
    expect(plots).toHaveLength(2);
    expect(printed(joiner, "joiner", "base")).toEqual(plots);
    const stood = printed(host, "host", "structure");
    expect(stood).toHaveLength(2);
    expect(printed(joiner, "joiner", "structure")).toEqual(stood);
    expect(host).toContain(`GAMECHECK host: match ${MATCH}`);
    expect(joiner).toContain(`GAMECHECK joiner: match ${MATCH}`);

    // Each structure stands where its parcel puts it: as far from its base's middle as it is from
    // the middle of what its parcel spans, on the base of its faction's place in the match.
    const centre = new Map(plots.map((line) => {
      const [, index, , x, y, z] = line.split(" ");
      return [Number(index), [Number(x), Number(y), Number(z)]] as const;
    }));
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

    // The host shot down the tower on the base it started on; the joining player saw that one go.
    const down = /GAMECHECK host: watchtower ([0-9a-f]{32}) came down after (\d+) shots/.exec(host);
    expect(down, "the host's log names what came down").not.toBeNull();
    const [, instanceId, shots] = down as RegExpExecArray;
    expect(Number(shots)).toBeGreaterThan(1);
    expect(joiner).toContain(`GAMECHECK joiner: watchtower ${instanceId} is gone, 1 still stand`);
    expect(host).toContain(`GAMECHECK host: the publisher's server recorded ${instanceId} destroyed`);
    const whose = instanceId === towers.alice ? "alice" : "carol";
    const other = whose === "alice" ? "carol" : "alice";
    expect(instanceId).toBe(towers[whose]);

    // What came of it (spec 7.6): the structure is a plan again, with its funding intact; nothing of
    // its owner's moved; the other base is as it was.
    const base = await s.publisher.base(factions[whose] as string);
    const tower = base.structures.find((x) => x.instanceId === instanceId);
    expect(tower).toMatchObject({ standing: false, destroyed: true });
    expect(tower?.funding.status).toBe("fully funded");
    expect(await holds(player(s, whose).wallet, STEEL)).toEqual({ available: 0n, locked: TOWER });
    expect((await s.publisher.base(factions[other] as string)).structures.find((x) => x.instanceId === towers[other])).toMatchObject({
      standing: true,
      destroyed: false,
    });
    const after = await bases();
    expect(after.map((b) => b.structures.map((x) => x.instanceId))).toEqual(
      (["alice", "carol"] as const).map((who) => (who === whose ? [] : [towers[who]])),
    );
  });

  it("it is not rebuilt until a member rebuilds it: then the next match has it again", async () => {
    const whose = (await bases())[0]?.structures.length === 0 ? "alice" : "carol";
    const rebuilt = await s.publisher.change(factions[whose] as string, player(s, whose).connection, { kind: "confirm", instanceId: towers[whose] as string });
    expect(rebuilt.taken, rebuilt.taken ? "" : rebuilt.why).toBe(true);
    await until("the tower stands in play again", 240_000, async () => {
      const base = await s.publisher.base(factions[whose] as string);
      return base.structures.find((x) => x.instanceId === towers[whose])?.standing ? true : null;
    });
    expect((await bases()).map((b) => b.structures.map((x) => x.instanceId))).toEqual([[towers.alice], [towers.carol]]);
    expect(await holds(player(s, whose).wallet, STEEL)).toEqual({ available: 0n, locked: TOWER });
  });
});
