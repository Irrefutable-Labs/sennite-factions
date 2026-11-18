/**
 * A hero bought in the game, end to end: the Unreal game runs its own check
 * (Source/Sennite/Private/Tests/HeroShopTest.cpp) on the menu, by taps on the screen; this file is
 * everything outside the game. The player's wallet, new and holding only ERA, connects to the game
 * by the code the game hands it. In the game the player trades ERA for D-bucks from the character
 * select, buys a hero with them, chooses it, hosts a lobby, and comes in as it once the lobby's server has checked the
 * hero pass with the publisher's server; then plays it on the test range. Outside the game, the
 * wallet proves it owns the hero.
 *
 * Tools/check_base_editor.sh runs it with the other game checks (see test/real-dsm/hosts.ts for what it needs).
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
import { spendable } from "../real-dsm/wallets.js";

const REPO = fileURLToPath(new URL("../../..", import.meta.url));
const SAVED = join(REPO, "Saved", "HeroShop");
const CODE_FILE = join(SAVED, "connect-code.txt");
const PROFILE = join(REPO, "Saved", "SaveGames", "HeroShop.sav");
const DBUCKS = "DBUCKS";

let a: Accounts;
let server: Server;
let url = "";

beforeAll(async () => {
  a = await accounts("gameheroes", ["alice"]);
  server = await new Service({ publisher: a.publisher, world: a.world, store: a.store, matchSecret: "" }).listen(0, "127.0.0.1");
  url = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
});

afterAll(async () => {
  server?.close();
  await a?.stop();
});

describe("a hero bought in the game", () => {
  it("a player buys a hero with D-bucks in the game, chooses it, and plays it once the server has checked the pass", async () => {
    const alice = a.wallets.alice!;
    const hero = a.world.shop.heroes[0]!;

    // The player's wallet comes to the game as a new wallet does: with the ERA the faucet gave it and no
    // D-bucks. Everything from there to playing the hero is done in the game, by taps.
    expect(await spendable(alice.wallet, DBUCKS)).toBe(0n);

    rmSync(CODE_FILE, { force: true });
    rmSync(PROFILE, { force: true });
    const game = spawn(join(REPO, "Tools", "run_game_check.sh"), ["Sennite.HeroShop.BuyChooseAndPlay", "BuyChooseAndPlay", "HeroShop", `-PublisherUrl=${url}`, "-ProfileSlot=HeroShop"], {
      cwd: REPO,
      env: { ...process.env, SENNITE_MAP: "/Game/Maps/Menu" },
    });
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
      expect(output).toContain("CHECK PASSED: Sennite.HeroShop.BuyChooseAndPlay");
      // what the game said it did, from its own log
      const said = readFileSync(join(SAVED, "check.log"), "latin1");
      expect(said).toContain("GAMECHECK the character select traded ERA for D-bucks");
      expect(said).toContain(`GAMECHECK bought ${hero.id} for ${hero.price} D-bucks`);
      expect(said).toContain(`GAMECHECK the lobby's server checked the pass: the player is ${hero.id}`);
      expect(said).toContain(`GAMECHECK on the range as ${hero.id}`);
      expect(ended).toBe(0);
    } finally {
      game.kill("SIGKILL");
    }

    // Outside the game, the wallet proves it owns the hero, on the connection the game made: an object of
    // which one exists, the wallet's.
    const aliceId = await alice.wallet.id();
    const connection = (await a.app.connections()).find((c) => c.connected && c.peer === aliceId);
    if (connection === undefined) throw new Error("alice's wallet is not connected to the game's account");
    const proven = await a.publisher.holdings(connection);
    expect(proven?.heroes).toMatchObject([{ heroId: hero.id, name: hero.name, serial: 1 }]);
  });
});
