# prima-frame-check

An unusual opencode plugin: it brings **prima's as/not_as judgment model** into
the session so the agent can decide, with the language's exact rules, whether a
value *counts as* something.

Built fully isolated — it touches nothing in the live opencode config.

## What it demonstrates

The whole thing runs on the two rules discovered in prima:

```
x as FRAME       both sides enforced — positive AND negative
x not_as FRAME   positive side only
```

A user frame `name as POS not_as NEG` composes:

```
positive = POS's positive predicate
negative = NEG's positive predicate   (what to avoid)
```

Define once, point forever, swap freely.

## Files

| File               | Purpose                                                          |
| ------------------ | ---------------------------------------------------------------- |
| `frame-engine.ts`  | Pure mirror of prima.c's semantics. Zero deps, no opencode.      |
| `plugin.ts`        | The opencode plugin (tools + hook).                              |
| `frames.prima`     | Frame definitions in prima's own syntax.                         |
| `opencode.json`    | Isolated test config.                                            |
| `engine-test.ts`   | Standalone verification — every case cross-checked against prima.c. |

## Test in isolation

Verify the engine without opencode (all 22 checks cross-checked against the
real C interpreter):

```sh
node engine-test.ts
```

Load the plugin into a throwaway opencode run (your real config is untouched):

```sh
OPENCODE_CONFIG=/abs/path/prima-frame-check/opencode.json opencode
```

## Tools

- **`prima_define`** — define/redefine a frame in prima syntax:
  `score as number not_as negative;`
- **`prima_judge`** — judge a value against a frame name, or an inline
  judgment (`string not_as empty`). `not_as: true` applies the negative
  primitive.
- **`prima_frames`** — the registry, visible.
- **`prima_watch`** — from now on, every tool result is judged against a frame
  and the verdict is appended to the output. Define once, point forever.

## Example

```
prima_define("fav_movie as string not_as digit;")
prima_judge({ target: "movie", value: "Inception", frame: "fav_movie" })
  → pass
prima_judge({ target: "movie", value: 42, frame: "fav_movie" })
  → constraint failed: 'movie' as 'fav_movie' (positive violated)
prima_judge({ target: "x", value: "007", frame: "fav_movie" })
  → constraint failed: 'movie' as 'fav_movie' (negative violated)
```
