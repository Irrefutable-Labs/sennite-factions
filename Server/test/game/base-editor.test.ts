/**
 * The base editor in the game, end to end (spec section 13, build step 4): the Unreal game runs its
 * own check (Source/Sennite/Private/Tests/BaseEditorTest.cpp), pressing the player's real keys,
 * against the publisher's server on DSM; this file is everything outside the game. It starts the
 * publisher's account and one player's wallet as real `dsm-app-host` accounts on the storage set,
 * serves the game, reads the connect code the game shows as a player would, approves it in the
 * wallet, and has the publisher give the player steel. Then it reads what came of it from the
 * publisher's account and from the wallet itself.
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
import { Service } from "../../src/publisher/service.js";
import { GaveUp, until } from "../real-dsm/hosts.js";
import { accounts, GAME, type Accounts } from "../real-dsm/stage.js";
import { holds } from "../real-dsm/wallets.js";

const REPO = fileURLToPath(new URL("../../..", import.meta.url));
const CODE_FILE = join(REPO, "Saved", "BaseEditor", "connect-code.txt");

let a: Accounts;
let server: Server;
let url = "";

beforeAll(async () => {
  a = await accounts("game", ["alice"]);
  server = await new Service({ publisher: a.publisher, world: a.world, store: a.store, matchSecret: "" }).listen(0, "127.0.0.1");
  url = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
});

afterAll(async () => {
  server?.close();
  await a?.stop();
});

describe("the base editor in the game", () => {
  it("a player plans a watchtower, funds it from their own wallet, and it stands in play", async () => {
    const wallet = a.wallets.alice!.wallet;
    rmSync(CODE_FILE, { force: true });

    // The game, running its own check against this publisher's server.
    const game = spawn(join(REPO, "Tools", "run_game_check.sh"), ["Sennite.BaseEditor", "PlanFundAndBuild", "BaseEditor", `-PublisherUrl=${url}`], { cwd: REPO });
    let output = "";
    game.stdout.on("data", (chunk: Buffer) => (output += chunk.toString()));
    game.stderr.on("data", (chunk: Buffer) => (output += chunk.toString()));
    let exitCode: number | null | undefined;
    const exited = new Promise<number | null>((resolve) => game.once("exit", (code) => resolve((exitCode = code))));

    try {
      // As a player does: read the connect code off the game's screen and approve it in the wallet.
      const code = await until("the game shows a connect code", 600_000, async () => {
        if (exitCode !== undefined) throw new GaveUp(`the game ended before it showed a connect code:\n${output}`);
        return existsSync(CODE_FILE) ? readFileSync(CODE_FILE, "utf8").trim() || null : null;
      });
      const preview = await wallet.preview(code);
      expect(preview.name).toBe(GAME);
      await wallet.approve(preview);

      // The publisher gives the player steel. The game learns of it only when the wallet proves it.
      const id = await wallet.id();
      await until("the publisher gives the player 300 steel", 240_000, async () => {
        await a.app.give(id, "STEEL", "300");
        return true;
      });

      const code_ = await exited;
      console.log(output);
      expect(output).toContain("CHECK PASSED: Sennite.BaseEditor");
      expect(code_).toBe(0);
    } finally {
      game.kill("SIGKILL");
    }

    // What came of it, from the publisher's account and from the wallet itself.
    const [factionId] = Object.keys(a.store.data.factions);
    expect(factionId).toBeDefined();
    const base = await a.publisher.base(factionId!);
    expect(base.height).toBe(1);
    expect(base.structures).toMatchObject([{ prefabId: "watchtower", standing: true, funding: { status: "fully funded" } }]);
    expect(await holds(wallet, "STEEL")).toEqual({ available: 0n, locked: 300n });
  });
});
