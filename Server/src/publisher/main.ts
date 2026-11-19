// The publisher's server, started. It reaches the publisher's own DSM account
// through a dsm-app-host on this machine and serves the game.
//
//   DSM_APP_HOST               the host's ingress, e.g. http://127.0.0.1:8787
//   DSM_APP_HOST_SECRET_FILE   <ingress-secret-dir>/ingress-token, which the host writes at each launch
//   DSM_ENV_CONFIG             the env config of the storage set the host is on
//   SENNITE_DATA_DIR           where the server keeps its own record (default ./data)
//   SENNITE_BIND               where the game reaches it (default 127.0.0.1:8790)
//
// There is no server without the account: it refuses to start without the three DSM settings.

import { randomBytes } from "node:crypto";
import { mkdirSync, writeFileSync } from "node:fs";
import type { AddressInfo } from "node:net";
import { join, resolve } from "node:path";
import { AppAccount, Ingress } from "dsm-app-client";
import { Publisher } from "./publisher.js";
import { Service } from "./service.js";
import { Store } from "./store.js";
import { loadWorld, storageSetOf } from "./world.js";

function required(name: string): string {
  const value = process.env[name];
  if (value === undefined || value.length === 0) {
    console.error(`${name} is not set: the publisher's server runs only with its DSM account (see src/publisher/main.ts)`);
    process.exit(2);
  }
  return value;
}

const world = loadWorld();
const dataDir = resolve(process.env.SENNITE_DATA_DIR ?? "data");
mkdirSync(dataDir, { recursive: true });
const store = new Store(join(dataDir, "publisher.json"));
const app = new AppAccount(await Ingress.fromSecretFile(required("DSM_APP_HOST"), required("DSM_APP_HOST_SECRET_FILE")));
const publisher = new Publisher({
  app,
  store,
  catalogue: world.catalogue,
  parcels: world.parcels,
  shop: world.shop,
  matches: world.matches,
  storageSet: storageSetOf(required("DSM_ENV_CONFIG")),
  name: "Sennite: Factions",
  now: Date.now,
});

console.log(`[publisher] the account is ${await app.id()}`);
await publisher.issueResources(world.economy);

// What the publisher's match server sends to record destruction and to open and decide matches: new at each launch, readable by this user only.
const matchSecret = randomBytes(32).toString("hex");
writeFileSync(join(dataDir, "match-secret"), matchSecret, { mode: 0o600 });

const bind = process.env.SENNITE_BIND ?? "127.0.0.1:8790";
const colon = bind.lastIndexOf(":");
const server = await new Service({ publisher, world, store, matchSecret }).listen(Number(bind.slice(colon + 1)), bind.slice(0, colon));
const address = server.address() as AddressInfo;
console.log(`[publisher] serving the game on http://${address.address}:${address.port}`);

// What the shop asked wallets to pay for: a player may approve one at any time, and the sale is carried out then.
setInterval(() => {
  publisher.shop.sweep().catch((e: unknown) => console.error(`[publisher] the shop's look at what it asked: ${e instanceof Error ? e.message : String(e)}`));
}, 10_000);

// The 48-hour clock for leavers (spec 3): the publisher's server keeps it.
setInterval(() => {
  publisher.tick().then(
    (returned) => {
      if (returned.length > 0) console.log(`[publisher] returned ${returned.length} of leavers' contributions`);
    },
    (e: unknown) => console.error(`[publisher] the 48-hour clock: ${e instanceof Error ? e.message : String(e)}`),
  );
  // The shares a decided match pays back to the publisher that have not come back yet.
  publisher.matches.tick().catch((e: unknown) => console.error(`[publisher] match prizes: ${e instanceof Error ? e.message : String(e)}`));
}, 60_000);
