/**
 * DSM accounts for the suites on DSM: `dsm-app-host` processes, the binary the publisher's account
 * runs in production, on the storage nodes the env config names.
 *
 * One process is the publisher's own account; the others are players' wallets. A wallet here is the
 * same SDK on the same ingress a phone's wallet screen uses: it reads a connect code, approves it,
 * and answers the game's requests under its grant through its own listener. Nothing in these suites
 * stands in for DSM, forwards a message or answers for a node.
 *
 * Required, or the suite refuses (it never skips):
 * - `DSM_APP_HOST_BIN`: the `dsm-app-host` binary, built from the DSM revision under test;
 * - `DSM_ENV_CONFIG`: the env config naming the storage set that revision runs on.
 */
import { spawn, type ChildProcess } from "node:child_process";
import { createWriteStream, existsSync, mkdtempSync } from "node:fs";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { AppAccount, Ingress, WalletAccount, type Connection } from "dsm-app-client";

export function required(name: string): string {
  const value = process.env[name];
  if (!value) {
    throw new Error(
      `${name} is not set: the suites on DSM run against a dsm-app-host and a storage set, and refuse rather than skip ` +
        "(DSM_APP_HOST_BIN = the dsm-app-host binary, DSM_ENV_CONFIG = the env config of the storage set)",
    );
  }
  if (!existsSync(value)) throw new Error(`${name} names ${value}, which does not exist`);
  return value;
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

async function freePort(): Promise<number> {
  return new Promise((resolve, reject) => {
    const probe = createServer();
    probe.once("error", reject);
    probe.listen(0, "127.0.0.1", () => {
      const address = probe.address();
      if (address === null || typeof address === "string") return reject(new Error("no port"));
      probe.close(() => resolve(address.port));
    });
  });
}

/** One run's directory: every account's data, secret and log. */
export const runDir = (name: string): string => mkdtempSync(join(tmpdir(), `sennite-${name}-`));

/** `probe` until it holds, or fail naming `what` and the last reason it did not. */
export async function until<T>(what: string, ms: number, probe: () => Promise<T | null | undefined>): Promise<T> {
  const deadline = Date.now() + ms;
  let why = "not yet";
  for (;;) {
    try {
      const got = await probe();
      if (got !== null && got !== undefined) return got;
    } catch (e) {
      why = e instanceof Error ? e.message : String(e);
    }
    if (Date.now() > deadline) throw new Error(`${what}: not within ${ms / 1000} s (${why})`);
    await sleep(500);
  }
}

/** A `dsm-app-host` process: one DSM account. */
export class Host {
  private child: ChildProcess | null = null;
  private starts = 0;
  readonly url: string;
  readonly secretFile: string;
  private readonly args: string[];

  private constructor(
    readonly name: string,
    private readonly dir: string,
    game: number,
    relay: number,
  ) {
    const secretDir = join(dir, `${name}-ingress`);
    this.secretFile = join(secretDir, "ingress-token");
    this.url = `http://127.0.0.1:${game}`;
    this.args = [
      "--data-dir", join(dir, name),
      "--ingress-secret-dir", secretDir,
      "--env-config", required("DSM_ENV_CONFIG"),
      "--game-bind", `127.0.0.1:${game}`,
      "--relay-bind", `127.0.0.1:${relay}`,
      "--relay-endpoint", `https://127.0.0.1:${relay}`,
      "--relay-name", "127.0.0.1",
    ];
  }

  static async start(name: string, dir: string): Promise<Host> {
    const host = new Host(name, dir, await freePort(), await freePort());
    await host.start();
    return host;
  }

  /** Where this account's processes logged, one file per start. */
  log(start = this.starts): string {
    return join(this.dir, `${this.name}-${start}.log`);
  }

  get running(): boolean {
    return this.child !== null && this.child.exitCode === null;
  }

  /** This launch's ingress. The host writes a new secret at every start. */
  async ingress(): Promise<Ingress> {
    return Ingress.fromSecretFile(this.url, this.secretFile);
  }

  /** Start the process (again, on the same account) and wait until its ingress answers with this launch's secret. */
  async start(): Promise<void> {
    this.starts += 1;
    const log = createWriteStream(this.log());
    const child = spawn(required("DSM_APP_HOST_BIN"), this.args, { env: { ...process.env, RUST_LOG: "info" } });
    child.stdout.pipe(log);
    child.stderr.pipe(log);
    this.child = child;
    let exited: string | null = null;
    child.once("exit", (code, signal) => {
      exited = `${code ?? signal}`;
    });
    await until(`${this.name} comes up (see ${this.log()})`, 240_000, async () => {
      if (exited !== null) throw new Error(`dsm-app-host exited (${exited})`);
      const activity = await (await this.ingress()).activity(0n);
      return activity.deviceId.length === 32 ? activity : null;
    });
  }

  /** Kill the process, as a phone kills an app or a machine loses power. */
  async stop(): Promise<void> {
    const child = this.child;
    this.child = null;
    if (!child || child.exitCode !== null) return;
    const gone = new Promise((r) => child.once("exit", r));
    child.kill("SIGKILL");
    await gone;
  }

  /** This account as the publisher's own. */
  async app(): Promise<AppAccount> {
    return new AppAccount(await this.ingress());
  }

  /** This account as a player's wallet. */
  async wallet(): Promise<WalletAccount> {
    return new WalletAccount(await this.ingress());
  }
}

/** Ask the set's faucet until this account holds ERA: a new account pays for its own writes with it. */
export async function funded(name: string, account: AppAccount | WalletAccount): Promise<void> {
  await until(`${name} is funded by the faucet`, 240_000, async () => {
    const era = await account.owns("ERA").catch(() => undefined);
    if (era !== undefined && era.amount > 0n) return era;
    const claim = await account.fund();
    if (!claim.funded) throw new Error(claim.message);
    return null;
  });
}

/**
 * As a player does in the wallet: read the game's connect code and approve the grant it asks for,
 * as offered. The connection as the publisher's account lists it.
 */
export async function connect(app: AppAccount, wallet: WalletAccount, code: string, expectApp: string): Promise<Connection> {
  const preview = await wallet.preview(code);
  if (preview.name !== expectApp) throw new Error(`the code names ${preview.name}, not ${expectApp}`);
  const approved = await wallet.approve(preview);
  return until("the publisher's account lists the connection", 240_000, async () => {
    return (await app.connections()).find((c) => c.id === approved.id && c.connected);
  });
}
