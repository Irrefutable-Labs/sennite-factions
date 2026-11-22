/**
 * What a match's players mined, given to their wallets: with a stand-in for the publisher's account, what
 * the server takes from the match server's report and what it gives, once. The gives themselves are DSM's.
 * The owner, 2026-11-22: "everyone has their own mining robot that goes into their wallet".
 */
import { describe, expect, it } from "vitest";
import type { AppAccount, Connection, ObjectInfo } from "dsm-app-client";
import { Matches } from "../../src/publisher/matches.js";
import { Store, type MatchRecord } from "../../src/publisher/store.js";

const KEY = { alice: "0a0a", bob: "0b0b", carol: "0c0c" };
const unhexed = (key: string): Uint8Array => new Uint8Array(key.match(/../g)!.map((b) => parseInt(b, 16)));
const wallet = (key: string, connected = true): Connection => ({ id: `CONN-${key}`, connected, opponent: { signingKey: unhexed(key) } }) as unknown as Connection;

/** The account as the matches use it here: who is connected, and what it gave to whom. */
class Account {
  connected: Connection[] = [wallet(KEY.alice), wallet(KEY.bob)];
  readonly given: string[] = [];
  /** How many of the next gives the account refuses, as one that could not send. */
  refuseGives = 0;
  async connections(): Promise<Connection[]> {
    return this.connected;
  }
  async object(symbol: string): Promise<ObjectInfo> {
    return { id: `${symbol}-OBJECT`, symbol, name: symbol, decimals: 0 } as unknown as ObjectInfo;
  }
  /** The connection each request was asked over, in the order asked. */
  readonly askedOver: string[] = [];
  async request(c: Connection): Promise<unknown> {
    this.askedOver.push(c.id);
    return {};
  }
  async wait(): Promise<unknown> {
    return { answered: true, outcome: "carried_out" };
  }
  async give(to: Connection, object: ObjectInfo, amount: string): Promise<void> {
    if (this.refuseGives > 0) {
      this.refuseGives -= 1;
      throw new Error("the transfer was not sent");
    }
    this.given.push(`${amount} ${object.symbol} to ${to.id}`);
  }
}

/** A match between two factions, alice and bob in one and carol in the other, decided or not. */
const match = (outcome: string): MatchRecord => ({
  factions: ["east", "north"],
  cell: "",
  shares: [
    { factionId: "east", signingKey: KEY.alice, genesis: "", deviceId: "", resource: "WOOD", amount: 30, vault: "", released: true },
    { factionId: "east", signingKey: KEY.bob, genesis: "", deviceId: "", resource: "WOOD", amount: 30, vault: "", released: true },
    { factionId: "north", signingKey: KEY.carol, genesis: "", deviceId: "", resource: "WOOD", amount: 60, vault: "", released: true },
  ],
  opened: true,
  outcome,
  openedAt: 0,
});

const stand = (outcome = "east") => {
  const account = new Account();
  const store = new Store();
  store.data.matches.m1 = match(outcome);
  const matches = new Matches({
    app: account as unknown as AppAccount,
    store,
    world: { prize: { WOOD: 60 }, mining: { WOOD: 120, STEEL: 50 } },
    members: async () => [],
    party: async () => {
      throw new Error("no vault is locked here");
    },
    now: Date.now,
  });
  return { account, store, matches };
};

describe("what a match's players mined", () => {
  it("is given to each player's wallet, once, when the match is over", async () => {
    const { account, store, matches } = stand();
    const report = [
      { signingKey: KEY.alice, resource: "WOOD", units: 70 },
      { signingKey: KEY.alice, resource: "STEEL", units: 20 },
      { signingKey: KEY.bob, resource: "WOOD", units: 50 },
    ];
    const recorded = await matches.mined("m1", report);
    expect(recorded.every((share) => share.given)).toBe(true);
    expect(account.given).toEqual(["70 WOOD to CONN-0a0a", "20 STEEL to CONN-0a0a", "50 WOOD to CONN-0b0b"]);

    // the first report stands: sent again, or sent differently, it changes nothing and gives nothing
    await matches.mined("m1", report);
    await matches.mined("m1", [{ signingKey: KEY.bob, resource: "STEEL", units: 50 }]);
    await matches.tick();
    expect(account.given).toHaveLength(3);
    expect(store.data.matches.m1!.mined).toHaveLength(3);
  });

  it("is not taken while the match is being played", async () => {
    const { account, matches } = stand("");
    await expect(matches.mined("m1", [{ signingKey: KEY.alice, resource: "WOOD", units: 1 }])).rejects.toThrow("is not over");
    expect(account.given).toEqual([]);
  });

  it("is refused whole when it says more was mined than the arena held", async () => {
    const { account, store, matches } = stand();
    const tooMuch = [
      { signingKey: KEY.alice, resource: "WOOD", units: 70 },
      { signingKey: KEY.bob, resource: "WOOD", units: 51 },
    ];
    await expect(matches.mined("m1", tooMuch)).rejects.toThrow("121 WOOD were not mined: the match's arena held 120");
    expect(store.data.matches.m1!.mined).toBeUndefined();
    expect(account.given).toEqual([]);
  });

  it("is refused for a wallet that is no member of the match, a material the arena did not hold, or no whole amount", async () => {
    const { account, matches } = stand();
    await expect(matches.mined("m1", [{ signingKey: "0d0d", resource: "WOOD", units: 1 }])).rejects.toThrow("no member");
    await expect(matches.mined("m1", [{ signingKey: KEY.alice, resource: "STONE", units: 1 }])).rejects.toThrow("did not hold");
    await expect(matches.mined("m1", [{ signingKey: KEY.alice, resource: "WOOD", units: 1.5 }])).rejects.toThrow("whole number");
    await expect(matches.mined("m1", [{ signingKey: KEY.alice, resource: "WOOD", units: 0 }])).rejects.toThrow("above zero");
    await expect(
      matches.mined("m1", [
        { signingKey: KEY.alice, resource: "WOOD", units: 1 },
        { signingKey: KEY.alice, resource: "WOOD", units: 1 },
      ]),
    ).rejects.toThrow("twice");
    await expect(matches.mined("m1", "everything")).rejects.toThrow("a list");
    expect(account.given).toEqual([]);
  });

  it("is given later to a wallet that was not connected when the match ended", async () => {
    const { account, matches } = stand();
    account.connected = [wallet(KEY.alice)];
    const recorded = await matches.mined("m1", [
      { signingKey: KEY.alice, resource: "WOOD", units: 10 },
      { signingKey: KEY.carol, resource: "STEEL", units: 5 },
    ]);
    expect(recorded.map((share) => share.given)).toEqual([true, false]);
    expect(account.given).toEqual(["10 WOOD to CONN-0a0a"]);

    account.connected = [wallet(KEY.alice), wallet(KEY.carol)];
    await matches.tick();
    expect(account.given).toEqual(["10 WOOD to CONN-0a0a", "5 STEEL to CONN-0c0c"]);
    await matches.tick();
    expect(account.given).toHaveLength(2);
  });

  it("is given at a later look when the account could not send it", async () => {
    const { account, store, matches } = stand();
    account.refuseGives = 1;
    await matches.mined("m1", [{ signingKey: KEY.alice, resource: "WOOD", units: 10 }]);
    // a refused give is known not to be sent: the share stays owed, and is not marked as cut off
    expect(account.given).toEqual([]);
    expect(store.data.matches.m1!.mined).toEqual([{ signingKey: KEY.alice, resource: "WOOD", units: 10, given: false }]);
    expect(matches.minedCutOff()).toEqual([]);
    await matches.tick();
    expect(account.given).toEqual(["10 WOOD to CONN-0a0a"]);
  });

  it("is given over the connection the wallet answers on, when it has an older one still counted as connected", async () => {
    const { account, matches } = stand();
    const older = { ...wallet(KEY.alice), id: "CONN-old" } as unknown as Connection;
    account.connected = [older, wallet(KEY.alice)];
    // nothing asked over the older connection is answered
    account.wait = async function (this: Account) {
      if (this.askedOver.at(-1) === "CONN-old") throw new Error("the request did not settle: unanswered");
      return { answered: true, outcome: "carried_out" };
    };
    await matches.mined("m1", [{ signingKey: KEY.alice, resource: "WOOD", units: 10 }]);
    expect(account.given).toEqual(["10 WOOD to CONN-0a0a"]);
  });

  it("cut off while the publisher was giving it is never given again by the server", async () => {
    const { account, store, matches } = stand();
    // as a server that stopped between sending the give and writing down that it was given
    store.data.matches.m1!.mined = [{ signingKey: KEY.alice, resource: "WOOD", units: 10, giving: true, given: false }];
    await matches.tick();
    await matches.mined("m1", []);
    expect(account.given).toEqual([]);
    expect(matches.minedCutOff()).toEqual([{ matchId: "m1", share: { signingKey: KEY.alice, resource: "WOOD", units: 10, giving: true, given: false } }]);
  });

  it("the arena holds what the publisher's file says a match holds to mine", () => {
    expect(stand().matches.mining).toEqual({ WOOD: 120, STEEL: 50 });
  });
});
