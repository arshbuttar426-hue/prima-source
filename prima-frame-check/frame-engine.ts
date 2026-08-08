/*
 * frame-engine.ts
 *
 * Pure TypeScript mirror of prima.c's judgment semantics. No dependencies,
 * no opencode imports — runs standalone in node. The engine exists so prima's
 * as/not_as model can be exercised in isolation, exactly as the C interpreter
 * decides it.
 *
 * The two rules this faithfully reproduces:
 *
 *   x as FRAME    — x must satisfy FRAME's positive AND must not satisfy
 *                   FRAME's negative. Both sides enforced.
 *   x not_as FRAME— x must not satisfy FRAME's positive. Only the positive
 *                   side is consulted.
 *
 * A user frame "name as POS not_as NEG" composes:
 *   positive = POS's positive predicate
 *   negative = NEG's positive predicate  (what to avoid)
 *
 * Redefining a frame name replaces it — the frame swap. Every judge call
 * resolves the name at call time, so the swap is instantaneous.
 */

export type PrimaType = "nil" | "int" | "float" | "string";

export interface PrimaValue {
  type: PrimaType;
  s?: string;
  n?: number;
}

export function val_nil(): PrimaValue {
  return { type: "nil" };
}

export function val_int(n: number | boolean): PrimaValue {
  return { type: "int", n: typeof n === "boolean" ? (n ? 1 : 0) : n };
}

export function val_float(f: number): PrimaValue {
  return { type: "float", n: f };
}

export function val_str(s: string): PrimaValue {
  return { type: "string", s };
}

export function fromJson(v: unknown): PrimaValue {
  if (v === null || v === undefined) return val_nil();
  if (typeof v === "number") return Number.isInteger(v) ? val_int(v) : val_float(v);
  if (typeof v === "boolean") return val_int(v ? 1 : 0);
  return val_str(String(v));
}

export function valToString(v: PrimaValue): string {
  switch (v.type) {
    case "nil":
      return "nil";
    case "int":
    case "float":
      return String(v.n);
    case "string":
      return v.s ?? "";
  }
}

/* ===== predicates (mirror prima.c exactly) ===== */

type Pred = (v: PrimaValue) => PrimaValue;

const is_alpha = (c: string) => /[A-Za-z]/.test(c);
const is_digit = (c: string) => /[0-9]/.test(c);
const is_alnum = (c: string) => is_alpha(c) || is_digit(c);
const is_lower = (c: string) => /[a-z]/.test(c);
const is_upper = (c: string) => /[A-Z]/.test(c);

const allOf = (v: PrimaValue, test: (c: string) => boolean): boolean => {
  if (v.type !== "string") return false;
  const s = v.s ?? "";
  if (s.length === 0) return false;
  for (const c of s) if (!test(c)) return false;
  return true;
};

export const pred_number: Pred = (v) => val_int(v.type === "int" || v.type === "float");
export const pred_int: Pred = (v) => val_int(v.type === "int");
export const pred_string: Pred = (v) => val_int(v.type === "string");
export const pred_alpha: Pred = (v) => val_int(allOf(v, is_alpha));
export const pred_alnum: Pred = (v) => val_int(allOf(v, is_alnum));
export const pred_zero: Pred = (v) => val_int((v.type === "int" || v.type === "float") && v.n === 0);
export const pred_positive: Pred = (v) => val_int((v.type === "int" || v.type === "float") && (v.n ?? 0) > 0);
export const pred_negative: Pred = (v) => val_int((v.type === "int" || v.type === "float") && (v.n ?? 0) < 0);
export const pred_nonzero: Pred = (v) => val_int((v.type === "int" || v.type === "float") && v.n !== 0);
export const pred_even: Pred = (v) => val_int(v.type === "int" && (v.n ?? 0) % 2 === 0);
export const pred_odd: Pred = (v) => val_int(v.type === "int" && (v.n ?? 0) % 2 !== 0);
export const pred_empty: Pred = (v) => {
  if (v.type === "nil") return val_int(1);
  if (v.type === "string") return val_int(!v.s || v.s.length === 0);
  if (v.type === "int") return val_int(v.n === 0);
  return val_int(0);
};
export const pred_nil: Pred = (v) => val_int(v.type === "nil");
export const pred_any: Pred = () => val_int(1);
export const pred_non_numeric: Pred = (v) => val_int(v.type !== "int" && v.type !== "float");
export const pred_float: Pred = (v) => val_int(v.type === "float");
export const pred_digit: Pred = (v) => val_int(allOf(v, is_digit));
export const pred_lowercase: Pred = (v) => val_int(allOf(v, is_lower));
export const pred_uppercase: Pred = (v) => val_int(allOf(v, is_upper));
export const pred_truthy: Pred = (v) => {
  if (v.type === "nil") return val_int(0);
  if (v.type === "int" || v.type === "float") return val_int(v.n !== 0);
  if (v.type === "string") return val_int(!!v.s && v.s.length > 0);
  return val_int(0);
};
export const pred_falsy: Pred = (v) => {
  if (v.type === "nil") return val_int(1);
  if (v.type === "int" || v.type === "float") return val_int(v.n === 0);
  if (v.type === "string") return val_int(!v.s || v.s.length === 0);
  return val_int(1);
};
export const pred_finite: Pred = (v) => {
  if (v.type === "int") return val_int(1);
  if (v.type === "float") return val_int(Number.isFinite(v.n));
  return val_int(0);
};
export const pred_prime: Pred = (v) => {
  if (v.type !== "int" || (v.n ?? 0) < 2) return val_int(0);
  const n = v.n as number;
  for (let i = 2; i * i <= n; i++) if (n % i === 0) return val_int(0);
  return val_int(1);
};

/* ===== built-in frames (prima.c's init table) ===== */

export interface Frame {
  name: string;
  posName: string;
  negName: string;
  positive: Pred;
  negative: Pred;
  userDefined: boolean;
}

const BUILTIN_TABLE: Array<[string, Pred, Pred]> = [
  ["nil", pred_nil, pred_empty],
  ["number", pred_number, pred_nil],
  ["int", pred_int, pred_nil],
  ["string", pred_string, pred_nil],
  ["alpha", pred_alpha, pred_nil],
  ["alnum", pred_alnum, pred_nil],
  ["zero", pred_zero, pred_nonzero],
  ["positive", pred_positive, pred_negative],
  ["negative", pred_negative, pred_positive],
  ["nonzero", pred_nonzero, pred_zero],
  ["even", pred_even, pred_nil],
  ["odd", pred_odd, pred_nil],
  ["empty", pred_empty, pred_nil],
  ["any", pred_any, pred_nil],
  ["non_numeric", pred_non_numeric, pred_nil],
  ["float", pred_float, pred_nil],
  ["digit", pred_digit, pred_nil],
  ["lowercase", pred_lowercase, pred_nil],
  ["uppercase", pred_uppercase, pred_nil],
  ["truthy", pred_truthy, pred_falsy],
  ["falsy", pred_falsy, pred_truthy],
  ["finite", pred_finite, pred_nil],
  ["prime", pred_prime, pred_nil],
];

/* ===== registry ===== */

export class FrameRegistry {
  private frames = new Map<string, Frame>();

  constructor() {
    for (const [name, pos, neg] of BUILTIN_TABLE) {
      this.frames.set(name, {
        name,
        posName: name,
        negName: name,
        positive: pos,
        negative: neg,
        userDefined: false,
      });
    }
  }

  get(name: string): Frame | undefined {
    return this.frames.get(name);
  }

  list(): Frame[] {
    return [...this.frames.values()];
  }

  /* define once, point forever, swap freely */
  define(name: string, posName: string, negName: string): void {
    if (!/^[A-Za-z_][A-Za-z0-9_<>]*$/.test(name)) {
      throw new Error(`parse error: bad frame name '${name}'`);
    }
    const pos = this.frames.get(posName);
    const neg = this.frames.get(negName);
    if (!pos) throw new Error(`unknown positive frame '${posName}'`);
    if (!neg) throw new Error(`unknown negative frame '${negName}'`);
    this.frames.set(name, {
      name,
      posName,
      negName,
      positive: pos.positive,
      negative: neg.positive,
      userDefined: true,
    });
  }
}

/* ===== parsing (prima frame-def syntax) ===== */

export interface FrameDef {
  name: string;
  pos: string;
  neg: string;
}

export function parseFrameDefs(src: string): FrameDef[] {
  const cleaned = src.replace(/--[^\n]*/g, "").replace(/;\s*$/gm, ";");
  const out: FrameDef[] = [];
  const re = /^([A-Za-z_][A-Za-z0-9_<>]*)\s+as\s+([A-Za-z_][A-Za-z0-9_<>]*)\s+not_as\s+([A-Za-z_][A-Za-z0-9_<>]*)\s*;/gm;
  let m: RegExpExecArray | null;
  while ((m = re.exec(cleaned)) !== null) {
    out.push({ name: m[1], pos: m[2], neg: m[3] });
  }
  return out;
}

export function loadFrames(registry: FrameRegistry, src: string): string[] {
  const defs = parseFrameDefs(src);
  const loaded: string[] = [];
  for (const d of defs) {
    registry.define(d.name, d.pos, d.neg);
    loaded.push(`${d.name} as ${d.pos} not_as ${d.neg};`);
  }
  return loaded;
}

/* ===== judging (the two rules) ===== */

export interface Verdict {
  pass: boolean;
  message?: string;
  positive: boolean;
  negative: boolean;
}

export function judgeAs(target: string, value: PrimaValue, frameName: string, registry: FrameRegistry): Verdict {
  const f = registry.get(frameName);
  if (!f) return { pass: false, message: `unknown frame '${frameName}'`, positive: false, negative: false };
  const pos = f.positive(value);
  const neg = f.negative(value);
  const posOk = pos.type === "int" && pos.n !== 0;
  if (!posOk) {
    return { pass: false, message: `constraint failed: '${target}' as '${frameName}' (positive violated)`, positive: false, negative: false };
  }
  const negOk = !(neg.type === "int" && neg.n !== 0);
  if (!negOk) {
    return { pass: false, message: `constraint failed: '${target}' as '${frameName}' (negative violated)`, positive: true, negative: false };
  }
  return { pass: true, positive: true, negative: true };
}

export function judgeNotAs(target: string, value: PrimaValue, frameName: string, registry: FrameRegistry): Verdict {
  const f = registry.get(frameName);
  if (!f) return { pass: false, message: `unknown frame '${frameName}'`, positive: false, negative: false };
  const pos = f.positive(value);
  const posCounts = pos.type === "int" && pos.n !== 0;
  if (posCounts) {
    return { pass: false, message: `constraint failed: '${target}' not_as '${frameName}' (counts as it)`, positive: true, negative: false };
  }
  return { pass: true, positive: false, negative: false };
}

export function verdictLine(v: Verdict): string {
  return v.pass ? "pass" : v.message ?? "fail";
}
