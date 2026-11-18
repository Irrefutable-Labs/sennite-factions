/**
 * A gun bought in the game, end to end: the Unreal game runs its own check
 * (Source/Sennite/Private/Tests/WeaponShopTest.cpp) on the menu, by taps on the screen; this file is
 * everything outside the game. The player's wallet, new and holding only ERA, connects to the game
 * by the code the game hands it. In the store's WEAPONS the player trades ERA for D-bucks, buys the
 * energy rifle with them, and the game sees the wallet own it; a hero pass made out to the game
 * speaks for the gun. Outside the game, the wallet proves it owns the gun: one object, its own.
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
const SAVED = join(REPO, "Saved", "WeaponShop");
const CODE_FILE = join(SAVED, "connect-code.txt");
const PLAYER_FILE = join(SAVED, "player.json");
const PROFILE = join(REPO, "Saved", "SaveGames", "WeaponShopCheck.sav");
const DBUCKS = "DBUCKS";

let a: Accounts;
let server: Server;
let url = "";

beforeAll(async () => {
  a = await accounts("gameweapons", ["alice"]);
  server = await new Service({ publisher: a.publisher, world: a.world, store: a.store, matchSecret: "" }).listen(0, "127.0.0.1");
  url = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
});

afterAll(async () => {
  server?.close();
  await a?.stop();
});

describe("a gun bought in the game", () => {
  it("a player buys a gun with D-bucks in the store, the game sees the wallet own it, and a hero pass speaks for it", async () => {
    const alice = a.wallets.alice!;
    const [gun, other] = a.world.shop.weapons;
    if (gun === undefined || other === undefined) throw new Error("the shop sells fewer than two guns");

    // The player's wallet comes to the game as a new wallet does: with the ERA the faucet gave it and no D-bucks.
    expect(await spendable(alice.wallet, DBUCKS)).toBe(0n);

    mkdirSync(SAVED, { recursive: true });
    for (const stale of [CODE_FILE, PLAYER_FILE, PROFILE]) rmSync(stale, { force: true });
    const game = spawn(
      join(REPO, "Tools", "run_game_check.sh"),
      ["Sennite.WeaponShop.BuyAndOwn", "BuyAndOwn", "WeaponShop", `-PublisherUrl=${url}`, `-PublisherPlayerFile=${PLAYER_FILE}`, "-ProfileSlot=WeaponShopCheck"],
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
      expect(said).toContain(`GAMECHECK bought ${gun.id} for ${gun.price} D-bucks`);
      expect(said).toMatch(new RegExp(`GAMECHECK the hero pass speaks for: .*${gun.id}`));
      expect(said).toContain(`GAMECHECK ${other.id} is still for sale`);
      expect(said).toContain("GAMECHECK finished");
      expect(said).toContain("Test Completed. Result={Success} Name={BuyAndOwn}");
      expect(ended).toBe(0);
    } finally {
      game.kill("SIGKILL");
    }

    // Outside the game, the wallet proves it owns the gun, on the connection the game made: an object of
    // which one exists, the wallet's. It owns no hero and not the other gun.
    const aliceId = await alice.wallet.id();
    const connection = (await a.app.connections()).find((c) => c.connected && c.peer === aliceId);
    if (connection === undefined) throw new Error("alice's wallet is not connected to the game's account");
    const proven = await a.publisher.holdings(connection);
    expect(proven?.weapons).toMatchObject([{ weaponId: gun.id, name: gun.name, serial: 1 }]);
    expect(proven?.heroes).toEqual([]);
    const sold = Object.entries(a.store.data.shop.weapons);
    expect(sold).toHaveLength(1);
    const owned = (await alice.wallet.owned()).find((o) => o.object.id === sold[0]?.[0]);
    expect(owned?.amount).toBe(1n);
    expect(owned?.object).toMatchObject({ kind: "unique", supply: "1" });
  });
});
