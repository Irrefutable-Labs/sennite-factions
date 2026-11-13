/**
 * The host killed, in the game (spec Stage 4; section 11, "the host disconnects"; Acceptance 3 and 9):
 * the Unreal game runs its own check (Source/Sennite/Private/Tests/BaseSessionTest.cpp), pressing
 * the player's real keys; this file is everything outside the game.
 *
 * The publisher's server here is the real program as a process of its own. The player founds a
 * faction and places a watchtower. This file then kills the server. With no server running, the
 * base is still what the publisher's account reads at its cells: it is kept in storage, not in the
 * session. The game goes on showing the base and says there is no live session. Once the player has
 * tried to build, a server is started again on the record the kill left, at the same address, and
 * the game shows the same base and builds on it.
 *
 * Tools/check_base_editor.sh runs it (see test/real-dsm/hosts.ts for what it needs).
 */
import { spawn } from "node:child_process";
import { existsSync, readFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import type { StoreData } from "../../src/publisher/store.js";
import { GaveUp, until } from "../real-dsm/hosts.js";
import { build, GameServer } from "../real-dsm/servers.js";
import { accounts, GAME, type Accounts } from "../real-dsm/stage.js";

const REPO = fileURLToPath(new URL("../../..", import.meta.url));
const FOLDER = join(REPO, "Saved", "BaseSession");
const CODE_FILE = join(FOLDER, "connect-code.txt");
const TRIED_FILE = join(FOLDER, "tried.txt");
const PLANNED_FILE = join(FOLDER, "planned.txt");

let a: Accounts;
let server: GameServer;

/** How many of the writes the server's record names hold their cells, final, as the publisher's account reads them now. */
async function written(): Promise<number> {
  if (!existsSync(server.record)) return 0;
  const record = JSON.parse(readFileSync(server.record, "utf8")) as StoreData;
  let count = 0;
  for (const one of Object.values(record.asked).flat()) {
    if ((await a.app.result({ connection: one.connection, seq: BigInt(one.seq) })).fact === "written") count += 1;
  }
  return count;
}

beforeAll(async () => {
  a = await accounts("session", ["alice"]);
  build();
  server = await GameServer.start("session", join(a.dir, "server-session"), a.publisherHost, a.dir);
});

afterAll(async () => {
  await server?.kill();
  await a?.stop();
});

describe("the host killed, in the game", () => {
  it("with the server killed the game shows the base and says there is no live session; a server started again shows the same base", async () => {
    const wallet = a.wallets.alice!.wallet;
    rmSync(CODE_FILE, { force: true });
    rmSync(TRIED_FILE, { force: true });
    rmSync(PLANNED_FILE, { force: true });

    const game = spawn(join(REPO, "Tools", "run_game_check.sh"), ["Sennite.BaseSession", "HostKilledAndBack", "BaseSession", `-PublisherUrl=${server.url}`], { cwd: REPO });
    let output = "";
    game.stdout.on("data", (chunk: Buffer) => (output += chunk.toString()));
    game.stderr.on("data", (chunk: Buffer) => (output += chunk.toString()));
    let exitCode: number | null | undefined;
    const exited = new Promise<number | null>((resolve) => game.once("exit", (code) => resolve((exitCode = code))));
    const running = (what: string): void => {
      if (exitCode !== undefined) throw new GaveUp(`the game ended before ${what}:\n${output}`);
    };

    try {
      // As a player does: read the connect code off the game's screen and approve it in the wallet.
      const code = await until("the game shows a connect code", 600_000, async () => {
        running("it showed a connect code");
        return existsSync(CODE_FILE) ? readFileSync(CODE_FILE, "utf8").trim() || null : null;
      });
      const preview = await wallet.preview(code);
      expect(preview.name).toBe(GAME);
      await wallet.approve(preview);

      // The player founds a faction and places a watchtower, in the game. Then the server is killed.
      await until("the player's watchtower holds its cell and the game shows it as a plan", 600_000, async () => {
        running("the watchtower was placed");
        return existsSync(PLANNED_FILE) && (await written()) === 1 ? true : null;
      });
      await server.kill();

      // No server is running. The base is where it was: the publisher's account reads the same write at its cell.
      expect(await written()).toBe(1);

      // The game says so, the player tries to build, and is told. Then a server starts on the record the kill left.
      await until("the player has tried to build with no session", 900_000, async () => {
        running("the player tried to build with no session");
        return existsSync(TRIED_FILE) ? true : null;
      });
      expect(await written()).toBe(1);
      await server.start();

      const code_ = await exited;
      console.log(output);
      expect(output).toContain("CHECK PASSED: Sennite.BaseSession");
      expect(code_).toBe(0);
    } finally {
      game.kill("SIGKILL");
    }

    // What came of it, from the publisher's account: the watchtower, and the structure placed through the server that took over.
    expect(await written()).toBe(2);
    const record = JSON.parse(readFileSync(server.record, "utf8")) as StoreData;
    expect(Object.keys(record.factions)).toHaveLength(1);
  });
});
