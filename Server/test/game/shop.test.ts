/**
 * The shop in the game, end to end (spec 10.5): the Unreal game runs its own check
 * (Source/Sennite/Private/Tests/BaseShopTest.cpp), pressing the player's real keys; this file is
 * everything outside the game. The game's player trades ERA for D-bucks through the publisher's
 * vault, buys a skin and wears it. Then, outside the game and in their own wallets, the player
 * sells the skin to a second player for D-bucks. The game learns of it from the wallet's next
 * proof and the skin comes off the hero; the second player's wallet proves it owns the skin.
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
import { becomeContacts, send, spendable, takeUp } from "../real-dsm/wallets.js";

const REPO = fileURLToPath(new URL("../../..", import.meta.url));
const SAVED = join(REPO, "Saved", "BaseShop");
const CODE_FILE = join(SAVED, "connect-code.txt");
const WORN_FILE = join(SAVED, "worn.txt");
const DBUCKS = "DBUCKS";

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
  a = await accounts("gameshop", ["alice", "bob"]);
  server = await new Service({ publisher: a.publisher, world: a.world, store: a.store, matchSecret: "" }).listen(0, "127.0.0.1");
  url = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
});

afterAll(async () => {
  server?.close();
  await a?.stop();
});

describe("the shop in the game", () => {
  it("a player buys D-bucks and a skin in the game, wears it, and sells it on from their own wallet", async () => {
    const [alice, bob] = [a.wallets.alice!, a.wallets.bob!];

    // The second player, in a game of their own: connected, with D-bucks from the publisher's vault.
    const entered = await call("POST", "/players");
    const token = entered.body.player as string;
    await bob.wallet.approve(await bob.wallet.preview(entered.body.code as string));
    await until("bob's game sees his wallet connected", 240_000, async () => ((await call("GET", "/players/me", token)).body.connected === true ? true : null));

    rmSync(CODE_FILE, { force: true });
    rmSync(WORN_FILE, { force: true });
    const game = spawn(join(REPO, "Tools", "run_game_check.sh"), ["Sennite.BaseShop", "BuyWearAndSell", "BaseShop", `-PublisherUrl=${url}`], { cwd: REPO });
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

      // In the game: D-bucks bought, the skin bought and worn. Someone watching sees the hero in it.
      const worn = await running("the hero wears the skin in the game", async () => (existsSync(WORN_FILE) ? readFileSync(WORN_FILE, "utf8").trim() || null : null));
      expect(worn).toContain("DekkerMagma");
      const skin = (await alice.wallet.owned()).find((o) => o.object.kind === "unique" && o.amount === 1n);
      if (skin === undefined) throw new Error("the player's wallet holds no skin");

      // The sale, wallet to wallet, outside the game: the buyer gets D-bucks from the vault, the two
      // are contacts, the buyer's wallet takes up the skin's object, pays, and the seller sends the skin.
      expect(await settled(token, "/shop/dbucks", { era: "2" })).toMatchObject({ ok: true });
      await until("bob's wallet holds D-bucks", 240_000, async () => ((await spendable(bob.wallet, DBUCKS)) >= 900n ? true : null));
      const connections = await a.app.connections();
      const [aliceId, bobId] = [await alice.wallet.id(), await bob.wallet.id()];
      const aliceIs = a.publisher.member(connections.find((c) => c.peer === aliceId)!);
      const bobIs = a.publisher.member(connections.find((c) => c.peer === bobId)!);
      await becomeContacts({ wallet: alice.wallet, is: aliceIs, name: "alice" }, { wallet: bob.wallet, is: bobIs, name: "bob" });
      await takeUp(bob.host, skin.object.id);
      await until("bob sends alice 900 D-bucks", 240_000, async () => {
        const sent = await send(bob.wallet, aliceIs, DBUCKS, "900");
        if (!sent.sent) throw new Error(sent.message);
        return true;
      });
      await until("alice sends bob the skin", 240_000, async () => {
        const sent = await send(alice.wallet, bobIs, skin.object.symbol, "1");
        if (!sent.sent) throw new Error(sent.message);
        return true;
      });

      const ended = await exited;
      console.log(output);
      expect(output).toContain("CHECK PASSED: Sennite.BaseShop");
      expect(output).toContain("GAMECHECK the sale brought 900 D-bucks");
      expect(ended).toBe(0);
    } finally {
      game.kill("SIGKILL");
    }

    // The buyer's game asks their wallet what it owns: the skin is theirs.
    const proven = await settled(token, "/players/me/materials", {});
    expect(proven.skins).toMatchObject([{ skinId: "magma", name: "Dekker Magma", serial: 1 }]);
  });
});
