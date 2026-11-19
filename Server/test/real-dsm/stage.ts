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
import { connect, funded, Host, required, runDir, until } from "./hosts.js";

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
  /** The record `publisher` keeps. */
  store: Store;
  players: Record<string, Player>;
  /** The server's clock: `advance` moves it forward without waiting. */
  clock: { now: () => number; advance: (ms: number) => void };
  /** A publisher's server started again on the same record and the same account. */
  restarted: () => Publisher;
  stop: () => Promise<void>;
}

/** The accounts a suite runs on, before any wallet is connected to the game. */
export interface Accounts {
  dir: string;
  world: World;
  storeFile: string;
  publisherHost: Host;
  app: AppAccount;
  publisher: Publisher;
  /** The record `publisher` keeps. */
  store: Store;
  /** Each player's host and wallet, by name. */
  wallets: Record<string, { host: Host; wallet: WalletAccount }>;
  clock: Stage["clock"];
  restarted: () => Publisher;
  /** What the grant lets the game commit of each resource. */
  caps: { symbol: string; perRequest: string; total: string }[];
  stop: () => Promise<void>;
}

/** Start the publisher's account and each player's wallet, fund them, and issue the publisher's resources. */
export async function accounts(run: string, names: string[]): Promise<Accounts> {
  const dir = runDir(run);
  const world = loadWorld();
  const storeFile = join(dir, "publisher.json");
  let offset = 0;
  const clock = { now: () => Date.now() + offset, advance: (ms: number) => void (offset += ms) };

  const [publisherHost, ...hosts] = await Promise.all(["publisher", ...names].map((n) => Host.start(n, dir)));
  if (publisherHost === undefined) throw new Error("no publisher host");
  const app = await publisherHost.app();
  const make = (store: Store = new Store(storeFile)): Publisher =>
    new Publisher({
      app,
      store,
      catalogue: world.catalogue,
      parcels: world.parcels,
      shop: world.shop,
      matches: world.matches,
      storageSet: storageSetOf(required("DSM_ENV_CONFIG")),
      name: GAME,
      now: clock.now,
    });
  const store = new Store(storeFile);
  const publisher = make(store);

  const stop = async (): Promise<void> => {
    await Promise.all([publisherHost, ...hosts].map((h) => h.stop()));
  };
  const wallets: Accounts["wallets"] = {};
  let caps: Accounts["caps"];
  try {
    const made = await Promise.all(hosts.map((h) => h.wallet()));
    await Promise.all([funded("publisher", app), ...made.map((w, i) => funded(names[i] as string, w))]);
    // An issue the set has not made final yet is held by the account and resumed by asking again.
    await until("the publisher's account issues its resources", 600_000, async () => {
      await publisher.issueResources(world.economy);
      return true;
    });
    caps = world.economy.resources.map((r) => ({
      symbol: r.symbol,
      perRequest: world.economy.grant.perRequest,
      total: world.economy.grant.total,
    }));
    for (const [i, name] of names.entries()) wallets[name] = { host: hosts[i] as Host, wallet: made[i] as WalletAccount };
  } catch (e) {
    // a suite that cannot start leaves no account running behind it
    await stop();
    throw e;
  }

  return {
    dir,
    world,
    storeFile,
    publisherHost,
    app,
    publisher,
    store,
    wallets,
    clock,
    restarted: () => make(),
    caps,
    stop,
  };
}

/** The accounts, with every player's wallet connected to the game by a connect code of its own. */
export async function stage(run: string, names: string[]): Promise<Stage> {
  const a = await accounts(run, names);
  const players: Record<string, Player> = {};
  try {
    for (const name of names) {
      const { host, wallet } = a.wallets[name] as { host: Host; wallet: WalletAccount };
      // An offer connects one wallet: each player reads a connect code of their own.
      const offer = await a.publisher.offer(a.caps);
      const connection = await connect(a.app, wallet, offer.code, GAME);
      players[name] = { name, host, wallet, connection, member: a.publisher.member(connection) };
    }
  } catch (e) {
    await a.stop();
    throw e;
  }
  return {
    dir: a.dir,
    world: a.world,
    storeFile: a.storeFile,
    publisherHost: a.publisherHost,
    app: a.app,
    publisher: a.publisher,
    store: a.store,
    players,
    clock: a.clock,
    restarted: a.restarted,
    stop: a.stop,
  };
}

/** The player `name` of a stage. */
export function player(s: Stage, name: string): Player {
  const found = s.players[name];
  if (found === undefined) throw new Error(`no player ${name}`);
  return found;
}
