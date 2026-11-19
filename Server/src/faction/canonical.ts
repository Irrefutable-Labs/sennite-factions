// One byte encoding for everything the faction rules hash or compare: JSON with
// every object's keys in ascending order, no whitespace, integers only. Two
// mirrors that hold the same value produce the same bytes.
//
// These bytes and the digests over them are the game's own records. They are
// not DSM commitments: DSM's are the cell and its step, which the publisher's
// account reads (spec 6.2).

import { createHash } from "node:crypto";

export type Canonical = string | number | Canonical[] | { [key: string]: Canonical };

function write(value: Canonical, out: string[]): void {
  if (typeof value === "string") {
    out.push(JSON.stringify(value));
    return;
  }
  if (typeof value === "number") {
    if (!Number.isSafeInteger(value)) throw new Error(`not a whole number: ${value}`);
    out.push(String(value));
    return;
  }
  if (Array.isArray(value)) {
    out.push("[");
    value.forEach((item, i) => {
      if (i > 0) out.push(",");
      write(item, out);
    });
    out.push("]");
    return;
  }
  if (typeof value !== "object" || value === null) throw new Error("not a canonical value");
  out.push("{");
  Object.keys(value)
    .sort()
    .forEach((key, i) => {
      if (i > 0) out.push(",");
      out.push(JSON.stringify(key), ":");
      write(value[key] as Canonical, out);
    });
  out.push("}");
}

/** The canonical bytes of `value`. */
export function canonical(value: Canonical): Uint8Array {
  const out: string[] = [];
  write(value, out);
  return new TextEncoder().encode(out.join(""));
}

/** SHA-256 over `tag` and each part, each part preceded by its length. */
export function digest(tag: string, ...parts: Uint8Array[]): Uint8Array {
  const hash = createHash("sha256");
  hash.update(tag, "utf8");
  for (const part of parts) {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(part.length);
    hash.update(length);
    hash.update(part);
  }
  return new Uint8Array(hash.digest());
}

export function hex(bytes: Uint8Array): string {
  return Buffer.from(bytes).toString("hex");
}

export function unhex(text: string, what: string): Uint8Array {
  if (!/^([0-9a-f]{2})+$/.test(text)) throw new Error(`${what} is not lowercase hex`);
  return new Uint8Array(Buffer.from(text, "hex"));
}

export function isHex(text: unknown, bytes?: number): text is string {
  if (typeof text !== "string" || !/^([0-9a-f]{2})+$/.test(text)) return false;
  return bytes === undefined || text.length === bytes * 2;
}
