/**
 * A swap in the game, both ways, end to end: the Unreal game runs its own check
 * (Source/Sennite/Private/Tests/SwapTest.cpp) on the menu, by taps on the screen; this file is
 * everything outside the game. The player's wallet, new and holding only ERA, connects to the game
 * by the code the game hands it. In the store's SWAP the player types 2 ERA, asks the price, and
 * swaps it for D-bucks through the publisher's vault; then changes the sides over and swaps 500
 * D-bucks back for ERA. Outside the game, the wallet's own account and the vault say the same.
 *
 * Run with `npm run test:game` (see test/real-dsm/hosts.ts for what it needs).
 */
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync } from "node:fs";
import type { Server } from "node:http";
import type { AddressInfo } from "node:net";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { Service } from "../../src/publisher/service.js";
import { GaveUp, until } from "../real-dsm/hosts.js";
import { accounts, GAME, type Accounts } from "../real-dsm/stage.js";
import { spendable } from "../real-dsm/wallets.js";

const REPO = fileURLToPath(new URL("../../..", import.meta.url));
const SAVED = join(REPO, "Saved", "Swap");
const CODE_FILE = join(SAVED, "connect-code.txt");
const PLAYER_FILE = join(SAVED, "player.json");
const PROFILE = join(REPO, "Saved", "SaveGames", "SwapCheck.sav");
const DBUCKS = "DBUCKS";

let a: Accounts;
let server: Server;
let url = "";

const reserve = async (symbol: string): Promise<bigint> => {
  const v = (await a.app.vaults()).find((each) => each.active && [each.a.symbol, each.b.symbol].includes(DBUCKS));
  if (v === undefined) throw new Error("the publisher's vault is not open");
  return v.a.symbol === symbol ? v.a.reserve : v.b.reserve;
};

beforeAll(async () => {
  a = await accounts("gameswap", ["alice"]);
  server = await new Service({ publisher: a.publisher, world: a.world, store: a.store, matchSecret: "" }).listen(0, "127.0.0.1");
  url = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
});

afterAll(async () => {
  server?.close();
  await a?.stop();
});

describe("a swap in the game", () => {
  it("a player swaps ERA for D-bucks in the store, and D-bucks back for ERA, at the prices the wallet gave", async () => {
    const alice = a.wallets.alice!;

    // The player's wallet comes to the game as a new wallet does: with the ERA the faucet gave it and no D-bucks.
    expect(await spendable(alice.wallet, DBUCKS)).toBe(0n);
    const eraAtStart = await spendable(alice.wallet, "ERA");

    mkdirSync(SAVED, { recursive: true });
    for (const stale of [CODE_FILE, PLAYER_FILE, PROFILE]) rmSync(stale, { force: true });
    const game = spawn(
      join(REPO, "Tools", "run_game_check.sh"),
      ["Sennite.Swap.BothWays", "BothWays", "Swap", `-PublisherUrl=${url}`, `-PublisherPlayerFile=${PLAYER_FILE}`, "-ProfileSlot=SwapCheck"],
      { cwd: REPO, env: { ...process.env, SENNITE_MAP: "/Game/Maps/Menu" } },
    );
    let output = "";
    game.stdout.on("data", (chunk: Buffer) => (output += chunk.toString()));
    game.stderr.on("data", (chunk: Buffer) => (output += chunk.toString()));
    let exitCode: number | null | undefined;
    const exited = new Promise<number | null>((resolve) => game.once("exit", (code) => resolve((exitCode = code))));
    const running = <T>(what: string, probe: () => Promise<T | null | undefined>): Promise<T> =>
      until(what, 600_000, async () => {
        if (exitCode !== undefined) throw new GaveUp(`the game ended before ${what}:\n${output}`);
        return probe();
      });

    try {
      // The game's player reads the connect code off the screen and approves it in their wallet.
      const code = await running("the game shows a connect code", async () => (existsSync(CODE_FILE) ? readFileSync(CODE_FILE, "utf8").trim() || null : null));
      const preview = await alice.wallet.preview(code);
      expect(preview.name).toBe(GAME);
      await alice.wallet.approve(preview);

      const ended = await exited;
      console.log(output);
      // what the game said it did, from its own log
      const said = readFileSync(join(SAVED, "check.log"), "latin1");
      expect(said).toMatch(/GAMECHECK price: 2 ERA gets [0-9]+ D-bucks now/);
      expect(said).toMatch(/GAMECHECK swapped 2 ERA for [0-9]+ D-bucks/);
      expect(said).toMatch(/GAMECHECK price: 500 D-bucks get [0-9.]+ ERA now/);
      expect(said).toContain("GAMECHECK swapped 500 D-bucks for ERA");
      expect(said).toContain("GAMECHECK finished");
      expect(said).toContain("Test Completed. Result={Success} Name={BothWays}");
      expect(ended).toBe(0);
    } finally {
      game.kill("SIGKILL");
    }

    // Outside the game, the wallet's own account: D-bucks from the first swap less the 500 swapped back,
    // and less ERA than it started with (the vault's fee, both ways, and DSM's own costs).
    const [dbucks, era] = [await spendable(alice.wallet, DBUCKS), await spendable(alice.wallet, "ERA")];
    expect(dbucks).toBeGreaterThan(500n);
    expect(era).toBeLessThan(eraAtStart);
    // and the publisher's vault: what the wallet holds in D-bucks left the vault, to the unit.
    expect(await reserve(DBUCKS)).toBe(BigInt(a.world.shop.vault.dbucks) - dbucks);
  });
});
