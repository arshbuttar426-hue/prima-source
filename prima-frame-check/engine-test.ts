/*
 * engine-test.ts — isolated verification of the prima frame engine.
 *
 * Runs with:  node engine-test.ts
 *
 * Every case mirrors an outcome we actually observed from the real C
 * interpreter (prima.c). If the engine disagrees with any of them,
 * the mirror is wrong.
 */

import {
  FrameRegistry,
  loadFrames,
  judgeAs,
  judgeNotAs,
  verdictLine,
  val_str,
  val_int,
  val_nil,
  parseFrameDefs,
} from "./frame-engine.ts";

const FRAMES = `
fav_movie as string not_as digit;
single_in as alpha not_as empty;
batch_in as string not_as single_in;
non_empty as string not_as empty;
`;

let failures = 0;
let checks = 0;

function check(desc: string, actual: boolean | string, expected: boolean | string) {
  checks++;
  const ok = actual === expected;
  if (!ok) failures++;
  console.log(`${ok ? "PASS" : "FAIL"}  ${desc}`);
  if (!ok) console.log(`      expected: ${expected}\n      actual:   ${actual}`);
}

const registry = new FrameRegistry();
loadFrames(registry, FRAMES);

/* --- the rules we discovered --- */

check(
  "as enforces BOTH sides: '' as non_empty is rejected (negative violated)",
  verdictLine(judgeAs("x", val_str(""), "non_empty", registry)),
  "constraint failed: 'x' as 'non_empty' (negative violated)",
);

check(
  "as enforces BOTH sides: 'hi' as non_empty passes",
  verdictLine(judgeAs("x", val_str("hi"), "non_empty", registry)),
  "pass",
);

check(
  "not_as consults the positive side only: '7' not_as digit counts as it",
  verdictLine(judgeNotAs("x", val_str("7"), "digit", registry)),
  "constraint failed: 'x' not_as 'digit' (counts as it)",
);

check(
  "not_as consults the positive side only: 'abc' not_as digit passes",
  verdictLine(judgeNotAs("x", val_str("abc"), "digit", registry)),
  "pass",
);

/* --- the movie model (proxy_model.prima) --- */

check("valid movie: 'Inception' as fav_movie", verdictLine(judgeAs("movie", val_str("Inception"), "fav_movie", registry)), "pass");
check("pure-digit is not a movie: '007' as fav_movie", verdictLine(judgeAs("movie", val_str("007"), "fav_movie", registry)), "constraint failed: 'movie' as 'fav_movie' (negative violated)");
check("mixed-title is a movie: 'Big Hero 6' as fav_movie", verdictLine(judgeAs("movie", val_str("Big Hero 6"), "fav_movie", registry)), "pass");
check("a number is not a movie: 7 as fav_movie", verdictLine(judgeAs("movie", val_int(7), "fav_movie", registry)), "constraint failed: 'movie' as 'fav_movie' (positive violated)");

check("single: 'Inception' as single_in", verdictLine(judgeAs("movie", val_str("Inception"), "single_in", registry)), "pass");
check(
  "single as batch is rejected (negative violated)",
  verdictLine(judgeAs("batch", val_str("Inception"), "batch_in", registry)),
  "constraint failed: 'batch' as 'batch_in' (negative violated)",
);
check("a comma string is a batch: 'A,B,C' as batch_in", verdictLine(judgeAs("batch", val_str("A,B,C"), "batch_in", registry)), "pass");
check("an empty string is not a single: '' as single_in", verdictLine(judgeAs("movie", val_str(""), "single_in", registry)), "constraint failed: 'movie' as 'single_in' (positive violated)");

/* --- value typing --- */

check("nil is empty", verdictLine(judgeAs("x", val_nil(), "empty", registry)), "constraint failed: 'x' as 'empty' (negative violated)");
check("nil is not truthy", verdictLine(judgeAs("x", val_nil(), "truthy", registry)), "constraint failed: 'x' as 'truthy' (positive violated)");
check("empty string is falsy", verdictLine(judgeAs("x", val_str(""), "falsy", registry)), "pass");
check("0 is zero", verdictLine(judgeAs("x", val_int(0), "zero", registry)), "pass");
check("2 is even and prime", verdictLine(judgeAs("x", val_int(2), "even", registry)), "pass");

/* --- frame swap: redefine once, everything that points at it changes --- */

registry.define("fav_movie", "alpha", "number");
check(
  "after swap, 'Inception' as fav_movie still passes (alpha, not number)",
  verdictLine(judgeAs("movie", val_str("Inception"), "fav_movie", registry)),
  "pass",
);
check(
  "after swap, 'Big Hero 6' as fav_movie is now rejected (space, digit — not alpha)",
  verdictLine(judgeAs("movie", val_str("Big Hero 6"), "fav_movie", registry)),
  "constraint failed: 'movie' as 'fav_movie' (positive violated)",
);
check(
  "after swap, the batch definition now inherits the new fav_movie shape",
  verdictLine(judgeAs("batch", val_str("A,B,C"), "batch_in", registry)),
  "pass",
);

/* --- parsing --- */

const parsed = parseFrameDefs("-- comment\nfav_movie as string not_as digit;\n\nscore as number not_as negative;");
check("parseFrameDefs skips comments and loads both defs", parsed.length, 2);
check("parseFrameDefs captures names", parsed.map((d) => d.name).join(","), "fav_movie,score");

console.log(`\n${checks} checks, ${failures} failures`);
process.exit(failures === 0 ? 0 : 1);
