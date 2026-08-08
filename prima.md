# Prima

A general-purpose execution model built on As/NotAs constraints.

**Namespace:** `com.prima.*`

---

## Architecture

Three layers, three primitives.

### Primitives

| Primitive | Role | Definition |
|-----------|------|------------|
| **As** | Frame | Position + configuration. A named space with named Connections (atomId + polarity +/-). Pure data. |
| **NotAs** | Atom | A pure stateless computation. One input → one output. No hidden state, no side effects. |
| **Connection** | Edge | Binds an atom to a frame slot with a polarity: `(atomId, polarity)`. Polarity determines how input is routed. |

### Layers

1. **Frame Layer** — what the system *counts as*. Positions, slots, topology. Equivalent to a constraint surface.
2. **Pipeline Layer** — resolves a frame by walking Connections in order, feeding each atom's output as the next atom's input. Linear trace, no branching.
3. **Atom Layer** — the actual computation. Atoms know nothing about frames. They transform an input and return an output.

Execution is: **Frame → Pipeline(trace over Connections) → Atoms fire → Output**

---

## Axioms

1. Every frame is a complete configuration. No frame is "wrong" — only different.
2. An atom is identical to its computation. Two atoms with the same input always produce the same output.
3. Polarity is the only modulation. Frames cannot inject data, only position and sign.
4. The execution trace is linear and observable. No hidden state between atoms.
5. Frame swaps are instantaneous. Switch the frame, keep the atoms, get different results.

---

## Proven Domains

| Domain | Implementation | Frame | Atoms |
|--------|---------------|-------|-------|
| **Web (3D)** | `panda-zoo/index.html` (constellation) | Position in 3D space + force layout | Node render, edge render, bloom, tooltip |
| **Android** | `snake-prima` | Game state (snake segments, food, grid) | Movement, collision, physics, particles |
| **Python** | `pipe/` core primitives | Data pipeline topology | Transform, filter, aggregate |
| **Browser (Cosmic Portal)** | `prima.html` | Spiral arm + scroll position | Phase transition, star rendering, text reveal |
| **Browser (Frame Demo)** | `prima-xp.html` | Windows XP desktop metaphor | Collatz step, polarity mapping, history |

---

## Key Insight

> Prima is not a visual style. It is an execution model that makes the frame visible.

As/NotAs asks: *what does this count as?* Prima answers by providing a concrete mechanism — Frame, Connection, Atom, Pipeline — that lets you change what something counts as without changing the something.

Same atoms, different frame, different result.
