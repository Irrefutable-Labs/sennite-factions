/**
 * The people of a faction in the game, end to end (spec 7.2): the Unreal game runs its own check
 * (Source/Sennite/Private/Tests/BasePeopleTest.cpp), pressing the player's real keys; this file is
 * everything outside the game. Alice has a game of her own, here as the HTTP calls her game would
 * send, and a real wallet: she founds a faction and invites the game's player. The game's player
 * joins with their own wallet, votes Alice out, invites her back and withdraws the invitation, all
 * from the game. Then the faction's lineage is read from the publisher's account.
 *
 * Tools/check_base_editor.sh runs it (see test/real-dsm/hosts.ts for what it needs).
 */
import { spawn } from "node:child_process";
import { existsSync, readFileSync, rmSync } from "node:fs";
import type { Server } from "node:http";
import type { AddressInfo } from "node:net";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { hex } from "../../src/faction/canonical.js";
import { Service } from "../../src/publisher/service.js";
import { GaveUp, until } from "../real-dsm/hosts.js";
import { accounts, GAME, type Accounts } from "../real-dsm/stage.js";

const REPO = fileURLToPath(new URL("../../..", import.meta.url));
const CODE_FILE = join(REPO, "Saved", "BasePeople", "connect-code.txt");
const PARCEL = "north-bluff";

type Json = Record<string, any>;

let a: Accounts;
let server: Server;
let url = "";

async function call(method: string, path: string, token = "", body?: Json): Promise<{ status: number; body: Json }> {
  const headers: Record<string, string> = {};
  if (token !== "") headers.authorization = `Bearer ${token}`;
  if (body !== undefined) headers["content-type"] = "application/json";
  const response = await fetch(url + path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
  return { status: response.status, body: (await response.json()) as Json };
}

async function settled(token: string, path: string, body: Json): Promise<Json> {
  const started = await call("POST", path, token, body);
  expect(started.status, JSON.stringify(started.body)).toBe(202);
  return until(`${path} settles`, 300_000, async () => {
    const r = await call("GET", `/requests/${started.body.request}`, token);
    return r.body.state === "done" ? r.body : null;
  });
}

beforeAll(async () => {
  a = await accounts("people", ["alice", "bob"]);
  server = await new Service({ publisher: a.publisher, world: a.world, store: a.store, matchSecret: "" }).listen(0, "127.0.0.1");
  url = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
});

afterAll(async () => {
  server?.close();
  await a?.stop();
});

describe("the people of a faction in the game", () => {
  it("a player invited by another joins from the game, votes, invites and withdraws an invitation", async () => {
    const [alice, bob] = [a.wallets.alice!.wallet, a.wallets.bob!.wallet];

    // Alice, in her own game: she connects her wallet and founds a faction on the parcel.
    const entered = await call("POST", "/players");
    const token = entered.body.player as string;
    await alice.approve(await alice.preview(entered.body.code as string));
    await until("alice's game sees her wallet connected", 240_000, async () => ((await call("GET", "/players/me", token)).body.connected === true ? true : null));
    const founded = await call("POST", "/factions", token, { parcelId: PARCEL });
    expect(founded.status, JSON.stringify(founded.body)).toBe(201);
    const factionId = founded.body.factionId as string;

    // The game, running its own check as the second player.
    rmSync(CODE_FILE, { force: true });
    const game = spawn(join(REPO, "Tools", "run_game_check.sh"), ["Sennite.BasePeople", "JoinVoteAndInvite", "BasePeople", `-PublisherUrl=${url}`], { cwd: REPO });
    let output = "";
    game.stdout.on("data", (chunk: Buffer) => (output += chunk.toString()));
    game.stderr.on("data", (chunk: Buffer) => (output += chunk.toString()));
    let exitCode: number | null | undefined;
    const exited = new Promise<number | null>((resolve) => game.once("exit", (code) => resolve((exitCode = code))));

    try {
      // The game's player reads the connect code off the screen and approves it in their wallet.
      const code = await until("the game shows a connect code", 600_000, async () => {
        if (exitCode !== undefined) throw new GaveUp(`the game ended before it showed a connect code:\n${output}`);
        return existsSync(CODE_FILE) ? readFileSync(CODE_FILE, "utf8").trim() || null : null;
      });
      const preview = await bob.preview(code);
      expect(preview.name).toBe(GAME);
      await bob.approve(preview);

      // Alice's game lists the new player among those in no faction, and she invites them.
      const id = await bob.id();
      await until("alice's game lists the game's player as in no faction", 240_000, async () => {
        const free = (await call("GET", "/players/free", token)).body.players as Json[];
        return free.some((p) => p.wallet === id) ? true : null;
      });
      expect(await settled(token, `/factions/${factionId}/invitations`, { wallet: id })).toMatchObject({ ok: true });

      const ended = await exited;
      console.log(output);
      expect(output).toContain("CHECK PASSED: Sennite.BasePeople");
      expect(ended).toBe(0);
    } finally {
      game.kill("SIGKILL");
    }

    // What came of it, from the publisher's account: every part is the signer's own change.
    const { mirror } = await a.publisher.follow(factionId);
    const connections = await a.app.connections();
    const [aliceId, bobId] = [await alice.id(), await bob.id()];
    const aliceKey = hex(connections.find((c) => c.peer === aliceId)!.opponent.signingKey);
    const bobKey = hex(connections.find((c) => c.peer === bobId)!.opponent.signingKey);
    expect(mirror.lineage.map((e) => [e.change?.kind, e.signerKey, e.valid])).toEqual([
      ["invite", aliceKey, true],
      ["accept", bobKey, true],
      ["vote", bobKey, true],
      ["invite", bobKey, true],
      ["uninvite", bobKey, true],
    ]);
    expect(mirror.state.members.map((m) => m.signingKey)).toEqual([bobKey]);
    expect(mirror.state.invited).toEqual([]);
    expect(mirror.state.votes).toEqual([]);
  });
});
