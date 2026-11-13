/**
 * What every suite on DSM starts from: the publisher's account with its resources issued, and
 * players' wallets connected to it by the game's connect code. Every account is a real
 * `dsm-app-host` on the storage set (see hosts.ts).
 */
import { join } from "node:path";
import type { AppAccount, Connection, WalletAccount } from "dsm-app-client";
import type { Member } from "../../src/faction/state.js";
import { Publisher } from "../../src/publisher/publisher.js";
import { Store } from "../../src/publisher/store.js";
import { loadWorld, storageSetOf, type World } from "../../src/publisher/world.js";
import { connect, funded, Host, required, runDir } from "./hosts.js";

export const GAME = "Sennite: Factions";

export interface Player {
  name: string;
  host: Host;
  wallet: WalletAccount;
  /** The player's connection, as the publisher's account lists it. */
  connection: Connection;
  member: Member;
}

export interface Stage {
  dir: string;
  world: World;
  storeFile: string;
  publisherHost: Host;
  app: AppAccount;
  publisher: Publisher;
  players: Record<string, Player>;
  /** The server's clock: `advance` moves it forward without waiting. */
  clock: { now: () => number; advance: (ms: number) => void };
  /** A publisher's server started again on the same record and the same account. */
  restarted: () => Publisher;
  stop: () => Promise<void>;
}

export async function stage(run: string, names: string[]): Promise<Stage> {
  const dir = runDir(run);
  const world = loadWorld();
  const storeFile = join(dir, "publisher.json");
  let offset = 0;
  const clock = { now: () => Date.now() + offset, advance: (ms: number) => void (offset += ms) };

  const [publisherHost, ...hosts] = await Promise.all(["publisher", ...names].map((n) => Host.start(n, dir)));
  if (publisherHost === undefined) throw new Error("no publisher host");
  const app = await publisherHost.app();
  const make = (): Publisher =>
    new Publisher({
      app,
      store: new Store(storeFile),
      catalogue: world.catalogue,
      parcels: world.parcels,
      storageSet: storageSetOf(required("DSM_ENV_CONFIG")),
      name: GAME,
      now: clock.now,
    });
  const publisher = make();

  const wallets = await Promise.all(hosts.map((h) => h.wallet()));
  await Promise.all([funded("publisher", app), ...wallets.map((w, i) => funded(names[i] as string, w))]);
  await publisher.issueResources(world.economy);
  const caps = world.economy.resources.map((r) => ({
    symbol: r.symbol,
    perRequest: world.economy.grant.perRequest,
    total: world.economy.grant.total,
  }));

  const players: Record<string, Player> = {};
  for (const [i, name] of names.entries()) {
    const host = hosts[i] as Host;
    const wallet = wallets[i] as WalletAccount;
    // An offer connects one wallet: each player reads a connect code of their own.
    const offer = await publisher.offer(caps);
    const connection = await connect(app, wallet, offer.code, GAME);
    players[name] = { name, host, wallet, connection, member: publisher.member(connection) };
  }

  return {
    dir,
    world,
    storeFile,
    publisherHost,
    app,
    publisher,
    players,
    clock,
    restarted: make,
    stop: async () => {
      await Promise.all([publisherHost, ...hosts].map((h) => h.stop()));
    },
  };
}

/** The player `name` of a stage. */
export function player(s: Stage, name: string): Player {
  const found = s.players[name];
  if (found === undefined) throw new Error(`no player ${name}`);
  return found;
}
