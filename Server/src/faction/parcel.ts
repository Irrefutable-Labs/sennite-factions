// The parcel registry (spec 4.3): non-overlapping regions of the map. A
// boundary may be any simple polygon. Coordinates are whole map units.

export type Point = readonly [x: number, y: number];

export interface Parcel {
  /** parcel_id */
  id: string;
  /** The boundary's corners, in order, in map units. */
  boundary: Point[];
}

/** An axis-aligned rectangle on the ground: what a placed prefab covers. */
export interface Rect {
  minX: number;
  minY: number;
  maxX: number;
  maxY: number;
}

/** The largest coordinate a parcel or placement may use: cross products stay exact. */
export const MAX_COORDINATE = 1 << 24;

const cross = (o: Point, a: Point, b: Point): number => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);

function onSegment(p: Point, a: Point, b: Point): boolean {
  return (
    cross(a, b, p) === 0 &&
    Math.min(a[0], b[0]) <= p[0] &&
    p[0] <= Math.max(a[0], b[0]) &&
    Math.min(a[1], b[1]) <= p[1] &&
    p[1] <= Math.max(a[1], b[1])
  );
}

/** Whether `p` is inside `boundary` or on it. */
export function inside(boundary: Point[], p: Point): boolean {
  let within = false;
  for (let i = 0; i < boundary.length; i++) {
    const a = boundary[i] as Point;
    const b = boundary[(i + 1) % boundary.length] as Point;
    if (onSegment(p, a, b)) return true;
    // A ray to the right of p: count the edges it crosses.
    if (a[1] > p[1] !== b[1] > p[1]) {
      const side = cross(a, b, p);
      if ((b[1] > a[1] && side > 0) || (b[1] < a[1] && side < 0)) within = !within;
    }
  }
  return within;
}

/** Whether segments ab and cd cross at a point interior to both. */
function crosses(a: Point, b: Point, c: Point, d: Point): boolean {
  const d1 = cross(c, d, a);
  const d2 = cross(c, d, b);
  const d3 = cross(a, b, c);
  const d4 = cross(a, b, d);
  return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

/**
 * Whether `rect` lies inside `boundary`: every corner is inside or on it, no
 * boundary corner is strictly inside the rectangle, and no edge of one crosses
 * an edge of the other. Touching the boundary is inside.
 */
export function contains(boundary: Point[], rect: Rect): boolean {
  const corners: Point[] = [
    [rect.minX, rect.minY],
    [rect.maxX, rect.minY],
    [rect.maxX, rect.maxY],
    [rect.minX, rect.maxY],
  ];
  if (!corners.every((c) => inside(boundary, c))) return false;
  for (const v of boundary) {
    if (v[0] > rect.minX && v[0] < rect.maxX && v[1] > rect.minY && v[1] < rect.maxY) return false;
  }
  for (let i = 0; i < boundary.length; i++) {
    const a = boundary[i] as Point;
    const b = boundary[(i + 1) % boundary.length] as Point;
    for (let j = 0; j < 4; j++) {
      if (crosses(a, b, corners[j] as Point, corners[(j + 1) % 4] as Point)) return false;
    }
  }
  return true;
}

/** Whether two rectangles cover any ground in common. Sharing an edge is not overlapping. */
export function overlaps(a: Rect, b: Rect): boolean {
  return a.minX < b.maxX && b.minX < a.maxX && a.minY < b.maxY && b.minY < a.maxY;
}

/** Read the parcel registry from its bytes. Anything malformed is refused. */
export function readParcels(bytes: Uint8Array): Map<string, Parcel> {
  const raw = JSON.parse(new TextDecoder().decode(bytes)) as { parcels?: unknown };
  if (!Array.isArray(raw.parcels)) throw new Error("the parcel registry lists no parcels");
  const parcels = new Map<string, Parcel>();
  for (const entry of raw.parcels as Record<string, unknown>[]) {
    const id = entry.id;
    if (typeof id !== "string" || id.length === 0 || id.length > 64) throw new Error("a parcel has no parcel_id");
    if (parcels.has(id)) throw new Error(`the registry lists parcel ${id} twice`);
    if (!Array.isArray(entry.boundary) || entry.boundary.length < 3) throw new Error(`parcel ${id} has no boundary`);
    const boundary = (entry.boundary as unknown[]).map((p): Point => {
      if (!Array.isArray(p) || p.length !== 2) throw new Error(`parcel ${id}: a boundary corner is not [x, y]`);
      const [x, y] = p as [unknown, unknown];
      for (const c of [x, y]) {
        if (typeof c !== "number" || !Number.isSafeInteger(c) || Math.abs(c) > MAX_COORDINATE) {
          throw new Error(`parcel ${id}: a boundary coordinate is not a whole number within the map`);
        }
      }
      return [x as number, y as number];
    });
    parcels.set(id, { id, boundary });
  }
  return parcels;
}
