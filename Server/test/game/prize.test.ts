/**
 * A match's prize, in the game (spec 10.6): the Unreal game runs its own check
 * (Source/Sennite/Private/Tests/BasePrizeTest.cpp), pressing the player's real keys; this file is
 * everything outside the game.
 *
 * The player founds a faction in the game. This file is then the publisher's match server: with
 * the match server's secret it opens a match between the player's faction and another, which locks
 * the prize in escrow, and decides it for the player's faction. The game says a prize is waiting
 * and where the wallet finds it. This file is then the player in their own wallet: it releases
 * each share there, as the wallet's escrow screen does. The game cannot ask the wallet to.
 *
 * Tools/check_base_editor.sh runs it (see test/real-dsm/hosts.ts for what it needs).
 */
import { spawn } from "node:child_process";
import { randomBytes } from "node:crypto";
import { existsSync, readFileSync, rmSync } from "node:fs";
import type { Server } from "node:http";
import type { AddressInfo } from "node:net";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { hex, unhex } from "../../src/faction/canonical.js";
import { party, releaseVault } from "../../src/dsm/routes.js";
import { Service } from "../../src/publisher/service.js";
import { connect, GaveUp, until } from "../real-dsm/hosts.js";
import { accounts, GAME, type Accounts } from "../real-dsm/stage.js";
import { holds } from "../real-dsm/wallets.js";

const REPO = fileURLToPath(new URL("../../..", import.meta.url));
const FOLDER = join(REPO, "Saved", "BasePrize");
const CODE_FILE = join(FOLDER, "connect-code.txt");
const FOUNDED_FILE = join(FOLDER, "founded.txt");
const SHOWN_FILE = join(FOLDER, "shown.txt");
/** What the publisher's match server sends. No player's game has it. */
const SECRET = hex(randomBytes(32));
const MATCH = "range-1";

let a: Accounts;
let server: Server;
let url = "";

/** One call of the publisher's match server. */
async function matchServer(path: string, body: unknown): Promise<{ status: number; body: Record<string, unknown> }> {
  const response = await fetch(url + path, {
    method: "POST",
    headers: { authorization: `Bearer ${SECRET}`, "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  return { status: response.status, body: (await response.json()) as Record<string, unknown> };
}

beforeAll(async () => {
  a = await accounts("prize", ["alice", "carol"]);
  server = await new Service({ publisher: a.publisher, world: a.world, store: a.store, matchSecret: SECRET }).listen(0, "127.0.0.1");
  url = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
});

afterAll(async () => {
  server?.close();
  await a?.stop();
});

describe("a match's prize in the game", () => {
  it("the game says a prize is waiting and where the wallet finds it; the player collects it in their own wallet", async () => {
    const wallet = a.wallets.alice!.wallet;
    for (const file of [CODE_FILE, FOUNDED_FILE, SHOWN_FILE]) rmSync(file, { force: true });

    const carol = a.publisher.member(await connect(a.app, a.wallets.carol!.wallet, (await a.publisher.offer(a.caps)).code, GAME));

    const game = spawn(join(REPO, "Tools", "run_game_check.sh"), ["Sennite.BasePrize", "WonAndCollected", "BasePrize", `-PublisherUrl=${url}`], { cwd: REPO });
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

      // The player founds a faction, in the game. Then the match server opens a match between it and
      // carol's, which locks the prize, and decides it for the player's faction.
      await until("the player's faction is founded and the game shows no prize", 600_000, async () => {
        running("the faction was founded");
        return existsSync(FOUNDED_FILE) ? true : null;
      });
      const key = hex((await party(wallet)).signingKey);
      const founded = await a.publisher.factionOf(key);
      if (founded === undefined) throw new Error("the player's wallet is in no faction");
      const first = founded.factionId;
      // The other faction of the match: carol's, on the other parcel.
      const other = [...a.world.parcels.keys()].find((id) => id !== founded.parcelId);
      if (other === undefined) throw new Error("the registry has no second parcel");
      const second = (await a.publisher.assign(a.publisher.founding(other, [carol], hex(randomBytes(16))))).factionId;
      const opened = await matchServer(`/matches/${MATCH}`, { factions: [first, second] });
      expect(opened.status, JSON.stringify(opened.body)).toBe(201);
      const decided = await matchServer(`/matches/${MATCH}/outcome`, { outcome: first });
      expect(decided.status, JSON.stringify(decided.body)).toBe(200);
      expect(decided.body.outcome).toBe(first);

      // The game shows the prize. Then the player collects it: in their own wallet, share by share.
      await until("the game shows the prize waiting", 900_000, async () => {
        running("it showed the prize");
        return existsSync(SHOWN_FILE) ? true : null;
      });
      const shares = a.store.data.matches[MATCH]!.shares.filter((x) => x.signingKey === key);
      expect(shares.map((x) => `${x.amount} ${x.resource}`)).toEqual(["60 WOOD", "40 STONE", "25 STEEL"]);
      for (const share of shares) expect(await releaseVault(wallet, unhex(share.vault, "a vault"))).toBe(true);

      const code_ = await exited;
      console.log(output);
      expect(output).toContain("CHECK PASSED: Sennite.BasePrize");
      expect(code_).toBe(0);
    } finally {
      game.kill("SIGKILL");
    }

    // What came of it, from the wallet itself and from the publisher's account.
    expect((await holds(wallet, "WOOD")).available).toBe(60n);
    expect((await holds(wallet, "STONE")).available).toBe(40n);
    expect((await holds(wallet, "STEEL")).available).toBe(25n);
    const view = await until("the publisher's account reads the shares collected", 240_000, async () => {
      await a.publisher.matches.tick();
      const read = await a.publisher.matches.read(MATCH);
      return read.shares.every((x) => x.state === "collected" || x.state === "returned") ? read : null;
    });
    expect(view.shares.map((x) => x.state)).toEqual([...Array<string>(3).fill("collected"), ...Array<string>(3).fill("returned")]);
  });
});
