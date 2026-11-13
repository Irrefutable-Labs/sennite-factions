/**
 * The storage seats of a developer's own set on this machine (DSM's `scripts/dev_net.sh`): take one
 * offline and bring it back.
 *
 * A seat is the production `storage_node` on its own Postgres database. Taking it offline stops the
 * process; bringing it back starts the same binary on the same config and the same database, so it
 * returns holding everything it held. Nothing here answers for a seat or changes what one holds.
 *
 * Only a set laid out as `dev_net.sh` lays it out can be driven this way: the folder of
 * `DSM_ENV_CONFIG` holds each node's config, CA and pid. On any other set these refuse.
 *
 * Required as well: `DSM_STORAGE_NODE_BIN`, the `storage_node` binary the set runs.
 */
import { spawn } from "node:child_process";
import { closeSync, existsSync, openSync, readFileSync, writeFileSync } from "node:fs";
import { request } from "node:https";
import { dirname, join } from "node:path";
import type { AppAccount, RequestRef } from "dsm-app-client";
import { required, until } from "./hosts.js";

/** How the publisher's account's reading of a cell begins when the cell is open. */
const OPEN = "no recognized write holds the cell";

export interface Seat {
  /** The node's name in the env config: `dsm-node-1` to `dsm-node-5`. */
  name: string;
  /** Its member id: the register incarnation a faction's storage set lists. */
  member: string;
  port: number;
}

const net = (): string => dirname(required("DSM_ENV_CONFIG"));
const pidFile = (seat: Seat): string => join(net(), "run", `${seat.name}.pid`);

/** The five seats the env config names, in its order. */
export function seats(): Seat[] {
  const text = readFileSync(required("DSM_ENV_CONFIG"), "utf8");
  const found: Seat[] = [];
  for (const block of text.split("[[nodes]]").slice(1)) {
    const field = (key: string): string => {
      const m = block.match(new RegExp(`^${key}\\s*=\\s*"([^"]+)"`, "m"));
      if (m === null) throw new Error(`a node of the env config has no ${key}`);
      return m[1] as string;
    };
    found.push({ name: field("name"), member: field("register_incarnation"), port: Number(new URL(field("endpoint")).port) });
  }
  if (found.length !== 5) throw new Error(`the env config names ${found.length} seats; a storage set has five`);
  for (const seat of found) {
    if (!existsSync(pidFile(seat)) || !existsSync(join(net(), `${seat.name}.toml`))) {
      throw new Error(
        `${seat.name} is not a seat this suite can take offline: it runs only on a developer's own set on this machine (DSM's scripts/dev_net.sh up)`,
      );
    }
  }
  return found;
}

/**
 * The seat that leads the cell `probe` was asked to write at. Which seat leads a cell is DSM's to
 * derive, and the game never computes it; this finds it the way an observer can: it is the one seat
 * without which the publisher's account cannot read the cell. `probe` is a write asked of a wallet
 * that is not running, so nothing is written at the cell while the seats are taken offline in turn.
 * Every seat serves again when this returns.
 */
export async function leaderOf(app: AppAccount, probe: RequestRef, all: Seat[] = seats()): Promise<{ leader: Seat; readings: Record<string, string> }> {
  const readings: Record<string, string> = {};
  for (const seat of all) {
    await takeOffline(seat);
    try {
      readings[seat.name] = (await app.result(probe)).detail;
    } finally {
      await bringBack(seat);
    }
  }
  const unread = all.filter((seat) => !(readings[seat.name] as string).startsWith(OPEN));
  if (unread.length !== 1) throw new Error(`not exactly one seat leads the cell: ${JSON.stringify(readings, null, 2)}`);
  return { leader: unread[0] as Seat, readings };
}

/** Whether the seat answers its health route over its own TLS. */
export function serving(seat: Seat): Promise<boolean> {
  return new Promise((resolve) => {
    const req = request(
      {
        host: "127.0.0.1",
        port: seat.port,
        path: "/api/v2/health",
        method: "GET",
        ca: readFileSync(join(net(), `${seat.name}-ca.pem`)),
        timeout: 3000,
      },
      (res) => {
        res.resume();
        resolve(res.statusCode === 200);
      },
    );
    req.once("timeout", () => req.destroy());
    req.once("error", () => resolve(false));
    req.end();
  });
}

/** Take the seat offline: stop its process, as `dev_net.sh down` does for every node. */
export async function takeOffline(seat: Seat): Promise<void> {
  const pid = Number(readFileSync(pidFile(seat), "utf8").trim());
  if (!Number.isInteger(pid) || pid <= 0) throw new Error(`${seat.name} has no pid on record`);
  try {
    process.kill(pid, "SIGTERM");
  } catch {
    // already gone
  }
  // Offline once the process is gone, not merely once it stopped answering: its database
  // connections are released only then.
  await until(`${seat.name} is offline`, 60_000, async () => (running(pid) || (await serving(seat)) ? null : true));
}

function running(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
}

/**
 * Bring the seat back: the same binary on the same config and database. A node that cannot get its
 * database connections yet exits at once (the server may still be closing the ones it had), so it is
 * started again until it serves.
 */
export async function bringBack(seat: Seat): Promise<void> {
  if (await serving(seat)) return;
  let pid = 0;
  const start = (): void => {
    const log = openSync(join(net(), "logs", `${seat.name}.log`), "a");
    const child = spawn(required("DSM_STORAGE_NODE_BIN"), ["--config", join(net(), `${seat.name}.toml`)], {
      detached: true,
      stdio: ["ignore", log, log],
      env: { ...process.env, RUST_LOG: "info" },
    });
    closeSync(log);
    if (child.pid === undefined) throw new Error(`${seat.name} did not start`);
    pid = child.pid;
    writeFileSync(pidFile(seat), `${pid}\n`);
    child.unref();
  };
  start();
  await until(`${seat.name} serves again (see its log in ${join(net(), "logs")})`, 120_000, async () => {
    if (await serving(seat)) return true;
    if (!running(pid)) start();
    return null;
  });
}
