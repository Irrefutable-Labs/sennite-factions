/**
 * The publisher's server as a process of its own: `node dist/publisher/main.js`, the program a
 * publisher runs, on the publisher's account and on a record folder of its own. It can be stopped,
 * killed and started again, and a second one can run beside it.
 */
import { execFileSync, spawn, type ChildProcess } from "node:child_process";
import { createWriteStream } from "node:fs";
import { createServer } from "node:net";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { required, until, type Host } from "./hosts.js";

const SERVER_DIR = fileURLToPath(new URL("../..", import.meta.url));

type Json = Record<string, any>;

/** Build the server the way a publisher does (`npm run build`). */
export function build(): void {
  execFileSync("npx", ["tsc", "-p", "tsconfig.build.json"], { cwd: SERVER_DIR, stdio: "inherit" });
}

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

export class GameServer {
  private child: ChildProcess | null = null;
  /** Where the game reaches it: the same address every time it is started, as a game is given one. */
  private port = 0;
  url = "";

  private constructor(
    readonly name: string,
    /** Where this server keeps its record. */
    readonly dataDir: string,
    private readonly account: Host,
    private readonly logFile: string,
  ) {}

  /** Start a server named `name` on the publisher's account, keeping its record in `dataDir`. */
  static async start(name: string, dataDir: string, account: Host, logDir: string): Promise<GameServer> {
    const server = new GameServer(name, dataDir, account, join(logDir, `server-${name}.log`));
    await server.start();
    return server;
  }

  get record(): string {
    return join(this.dataDir, "publisher.json");
  }

  async start(): Promise<void> {
    if (this.port === 0) this.port = await freePort();
    const port = this.port;
    this.url = `http://127.0.0.1:${port}`;
    const log = createWriteStream(this.logFile, { flags: "a" });
    const child = spawn("node", [join(SERVER_DIR, "dist/publisher/main.js")], {
      env: {
        ...process.env,
        DSM_APP_HOST: this.account.url,
        DSM_APP_HOST_SECRET_FILE: this.account.secretFile,
        DSM_ENV_CONFIG: required("DSM_ENV_CONFIG"),
        SENNITE_DATA_DIR: this.dataDir,
        SENNITE_BIND: `127.0.0.1:${port}`,
      },
    });
    child.stdout.pipe(log);
    child.stderr.pipe(log);
    this.child = child;
    let exited: string | null = null;
    child.once("exit", (code, signal) => {
      exited = `${code ?? signal}`;
    });
    await until(`the server ${this.name} serves the game (see ${this.logFile})`, 600_000, async () => {
      if (exited !== null) throw new Error(`the server exited (${exited})`);
      return (await fetch(`${this.url}/world`)).ok ? true : null;
    });
  }

  private async end(signal: NodeJS.Signals): Promise<void> {
    const child = this.child;
    this.child = null;
    if (!child || child.exitCode !== null) return;
    const gone = new Promise((r) => child.once("exit", r));
    child.kill(signal);
    await gone;
  }

  /** Stop the server as its operator would. */
  stop(): Promise<void> {
    return this.end("SIGTERM");
  }

  /** Kill it as a crash or a power cut would: nothing it was doing is finished. */
  kill(): Promise<void> {
    return this.end("SIGKILL");
  }

  /** One HTTP call of a player's game. */
  async call(method: string, path: string, token = "", body?: Json): Promise<{ status: number; body: Json }> {
    const headers: Record<string, string> = {};
    if (token !== "") headers.authorization = `Bearer ${token}`;
    if (body !== undefined) headers["content-type"] = "application/json";
    const response = await fetch(this.url + path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: (await response.json()) as Json };
  }

  /** Send something a wallet has to answer; the id the game reads its outcome under. */
  async send(token: string, path: string, body: Json): Promise<string> {
    const started = await this.call("POST", path, token, body);
    if (started.status !== 202) throw new Error(`${path}: ${started.status} ${JSON.stringify(started.body)}`);
    return started.body.request as string;
  }

  /** What became of something sent. */
  outcome(token: string, request: string): Promise<Json> {
    return until(`the request settles at the server ${this.name}`, 600_000, async () => {
      const r = await this.call("GET", `/requests/${request}`, token);
      return r.body.state === "done" ? r.body : null;
    });
  }

  /** Send it and wait for what became of it. */
  async settled(token: string, path: string, body: Json): Promise<Json> {
    return this.outcome(token, await this.send(token, path, body));
  }
}
