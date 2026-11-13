/**
 * Building paused, in the game (spec Stage 4; section 11, "the leader seat of the current cell is
 * unreachable"): the Unreal game runs its own check (Source/Sennite/Private/Tests/BasePauseTest.cpp),
 * pressing the player's real keys; this file is everything outside the game.
 *
 * The player founds a faction and places a watchtower. This file then finds the storage seat that
 * leads the faction's next change and takes it offline: a real seat of the set, stopped. The game
 * goes on showing the base, says building is paused, and a placement the player tries is not taken.
 * Once the player has tried, the seat is brought back, and the game places the same structure.
 *
 * Finding the leader needs a write asked at the cell of a wallet that is not running (seats.ts). So
 * a second player, Bob, is invited to the faction by Alice's wallet, and his wallet is stopped: the
 * acceptance asked of it waits, and is the probe.
 *
 * Tools/check_building_paused.sh runs it on a developer's own set (see test/real-dsm/hosts.ts and seats.ts).
 */
import { spawn } from "node:child_process";
import { existsSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import type { Server } from "node:http";
import type { AddressInfo } from "node:net";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { encodeChange } from "../../../src/faction/change.js";
import { PAUSED } from "../../../src/publisher/publisher.js";
import { Service } from "../../../src/publisher/service.js";
import { connect, GaveUp, until } from "../../real-dsm/hosts.js";
import { bringBack, leaderOf, seats, takeOffline, type Seat } from "../../real-dsm/seats.js";
import { accounts, GAME, type Accounts } from "../../real-dsm/stage.js";

const REPO = fileURLToPath(new URL("../../../..", import.meta.url));
const FOLDER = join(REPO, "Saved", "BasePause");
const CODE_FILE = join(FOLDER, "connect-code.txt");
const TRIED_FILE = join(FOLDER, "tried.txt");
const OFFLINE_FILE = join(FOLDER, "seat-offline.txt");

const all: Seat[] = seats();
let a: Accounts;
let server: Server;
let url = "";

beforeAll(async () => {
  a = await accounts("pause", ["alice", "bob"]);
  server = await new Service({ publisher: a.publisher, world: a.world, store: a.store, matchSecret: "" }).listen(0, "127.0.0.1");
  url = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
});

afterAll(async () => {
  // Whatever happened, the set is left with every seat serving.
  for (const seat of all) await bringBack(seat);
  server?.close();
  await a?.stop();
});

describe("building paused, in the game", () => {
  it("with the leader seat offline the game shows the base and says building is paused; it builds again when the seat returns", async () => {
    const alice = a.wallets.alice!.wallet;
    rmSync(CODE_FILE, { force: true });
    rmSync(TRIED_FILE, { force: true });
    rmSync(OFFLINE_FILE, { force: true });

    const game = spawn(join(REPO, "Tools", "run_game_check.sh"), ["Sennite.BasePause", "PauseAndResume", "BasePause", `-PublisherUrl=${url}`], { cwd: REPO });
    let output = "";
    game.stdout.on("data", (chunk: Buffer) => (output += chunk.toString()));
    game.stderr.on("data", (chunk: Buffer) => (output += chunk.toString()));
    let exitCode: number | null | undefined;
    const exited = new Promise<number | null>((resolve) => game.once("exit", (code) => resolve((exitCode = code))));
    const running = (what: string): void => {
      if (exitCode !== undefined) throw new GaveUp(`the game ended before ${what}:\n${output}`);
    };

    let factionId = "";
    let before: Awaited<ReturnType<typeof a.publisher.base>>;
    try {
      // As a player does: read the connect code off the game's screen and approve it in the wallet.
      const code = await until("the game shows a connect code", 600_000, async () => {
        running("it showed a connect code");
        return existsSync(CODE_FILE) ? readFileSync(CODE_FILE, "utf8").trim() || null : null;
      });
      const preview = await alice.preview(code);
      expect(preview.name).toBe(GAME);
      await alice.approve(preview);

      // The player founds a faction and places a watchtower, in the game.
      before = await until("the player's watchtower is part of the base", 600_000, async () => {
        running("the watchtower was placed");
        const [id] = Object.keys(a.store.data.factions);
        if (id === undefined) return null;
        factionId = id;
        const base = await a.publisher.base(id);
        return base.structures.length === 1 ? base : null;
      });

      // Bob connects with a code of his own and Alice's wallet invites him. His wallet then stops, so
      // the acceptance the game asks of it waits at the successor cell and nothing is written there.
      const offer = await a.publisher.offer(a.caps);
      const bob = await connect(a.app, a.wallets.bob!.wallet, offer.code, GAME);
      const aliceId = await alice.id();
      const alices = (await a.app.connections()).find((c) => c.connected && c.peer === aliceId);
      if (alices === undefined) throw new Error("the publisher's account does not list the player's connection");
      const invited = await a.publisher.change(factionId, alices, { kind: "invite", member: a.publisher.member(bob) });
      expect(invited, JSON.stringify(invited)).toMatchObject({ taken: true });
      await a.wallets.bob!.host.stop();
      const { mirror } = await a.publisher.follow(factionId);
      const { name, writers } = mirror.successorCell();
      const probe = await a.publisher.cells.ask(bob, name, writers, encodeChange({ factionId, parent: mirror.commitment, change: { kind: "accept" } }));

      const { leader, readings } = await leaderOf(a.app, probe, all);
      console.log(`the successor cell's leader is ${leader.name}`, readings);
      const paused = await a.publisher.base(factionId);
      await takeOffline(leader);
      // Finding the leader took each seat offline for a moment; the game's player acts only from here on.
      writeFileSync(OFFLINE_FILE, leader.name);

      // The game sees it, the player tries to build, and is told. Then the seat returns.
      await until("the player has tried to build while paused", 900_000, async () => {
        running("the player tried to build while paused");
        return existsSync(TRIED_FILE) ? true : null;
      });
      const during = await a.publisher.base(factionId);
      expect(during.commitment).toBe(paused.commitment);
      expect(during.structures.map((x) => x.instanceId)).toEqual(before.structures.map((x) => x.instanceId));
      await bringBack(leader);

      const code_ = await exited;
      console.log(output);
      expect(output).toContain("CHECK PASSED: Sennite.BasePause");
      expect(code_).toBe(0);
      expect(output).toContain(`building=${PAUSED}`);
    } finally {
      game.kill("SIGKILL");
    }

    // What came of it, from the publisher's account: the tower, then the structure placed after the seat returned.
    const base = await a.publisher.base(factionId);
    expect(base.structures).toHaveLength(2);
    expect(base.structures.map((x) => x.instanceId)).toContain(before.structures[0]!.instanceId);
    const lineage = (await a.publisher.follow(factionId)).mirror.lineage;
    expect(lineage.map((e) => [e.change?.kind, e.valid])).toEqual([
      ["place", true],
      ["invite", true],
      ["place", true],
    ]);
  });
});
