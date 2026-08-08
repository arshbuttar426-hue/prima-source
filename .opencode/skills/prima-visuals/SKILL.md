---
name: prima-visuals
description: Canvas-based interactive visual system for Prima landing pages. Use when building or modifying the animated point-field lattice, physics simulation, or spatial interaction layer. Covers proven canvas setup, coordinate mapping, physics engine, connection system, and render pipeline. Do NOT use for general CSS layout or static page design.
---

# Prima Visuals — Proven Architecture

## Canvas Setup (DO THIS)

```js
const cv = document.querySelector('canvas');
const ctx = cv.getContext('2d');
const dpr = Math.min(devicePixelRatio || 1, 2);

let W = 0, H = 0; // CSS pixel logical space

function fill() {
  W = cv.clientWidth;   // CSS pixels — NOT innerWidth
  H = cv.clientHeight;  // CSS pixels — NOT innerHeight
  if (!W || !H) return;
  cv.width  = W * dpr;  // buffer in physical pixels
  cv.height = H * dpr;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  // place grid in [0, W] x [0, H] space
}
// Wrap initial fill in requestAnimationFrame so layout has resolved
requestAnimationFrame(() => { fill(); /* start loop */ });
```

**Critical rule**: never use `innerWidth`/`innerHeight`. Always use `cv.clientWidth`/`cv.clientHeight`. The canvas buffer size and drawing coordinate space must derive from the canvas element's actual CSS box, not the viewport. Mismatch causes coordinate drift.

## Pointer Mapping (DO THIS)

```js
let rect = { left: 0, top: 0 };
let px = -1e4, py = -1e4; // off-screen initial

// Store rect once after fill/resize
rect = cv.getBoundingClientRect();

// Every pointer event: canvas-local coordinates
function pointer(e) {
  px = e.clientX - rect.left;
  py = e.clientY - rect.top;
}
```

Use `getBoundingClientRect()` only for the pointer offset. The coordinate space (`W`, `H`) comes from `clientWidth`/`clientHeight`. These must match: `rect.width === W` and `rect.height === H` for a viewport-filling canvas.

## CSS: Canvas Element

```css
canvas {
  position: fixed;
  top: 0; left: 0;
  width: 100%; height: 100%;
}
```

Use explicit `top/left/width/height` over `inset:0` — more universally reliable across browsers. No `display:block` needed (canvas is inline-block by default, `position:fixed` overrides).

## Physics Engine

```
Each point: { x, y, hx, hy, vx, vy, e }
```

| Field | Purpose |
|-------|---------|
| `x, y` | current position (CSS pixels) |
| `hx, hy` | home position — the grid anchor for spring force |
| `vx, vy` | velocity |
| `e` | energy (0–1), decays each frame |

### Forces per frame per point:

1. **Spring**: `v += (home - position) * K` where `K ≈ 0.006`
2. **Pointer repulsion**: inverse-square falloff within radius `PTR_R ≈ 160px`. `force = PTR_F / d²`, applied in direction away from pointer. Energy gain: `e += EGAIN * (1 - e)` where `EGAIN ≈ 0.28`
3. **Integration**: `position += velocity`
4. **Damping**: `velocity *= DAMP` where `DAMP ≈ 0.93`
5. **Energy decay**: `e *= EDECAY` where `EDECAY ≈ 0.955`

## Connection System

Two modes:

**At rest** (`e < 0.005` for all points): use `baseConns` — precomputed once after `fill()`. Grid neighbors within `CB = 50px` are connected. Never recompute while field is settled.

**Active**: recompute every frame. Connection threshold expands per-point: `threshold = CB + (CA - CB) * point.e` where `CA ≈ 86`. This reveals more structure near disturbed areas.

**Neighbor pull**: once connections exist, disturbed points attract their connected neighbors. `force = ae * NPULL` where `ae` is average energy of the pair. This propagates disturbance through the lattice, causing local structure formation.

## Render Pipeline

Order matters. Draw back-to-front:

1. **Connections**: thin lines, opacity `0.02 + ae * 0.16`, lineWidth `0.25 + ae * 0.35`
2. **Points**: circles, radius `0.5 + e * 2.0`, opacity `0.025 + e * 0.55`

At rest: points are barely visible (opacity 2.5%, radius 0.5px). Connections are near-invisible (opacity 2%). Interaction reveals the structure.

Always `clearRect(0, 0, W, H)` at the start of each frame.

## CSS Typography Layer

```css
.hero {
  position: fixed; inset: 0; z-index: 1;
  display: flex; flex-direction: column;
  justify-content: center; align-items: center;
  pointer-events: none; /* passes through to canvas */
}
h1 {
  font-weight: 260; /* ultra-light */
  font-size: clamp(2.8rem, 11vw, 8rem);
  letter-spacing: -0.06em; line-height: 0.82;
  color: rgba(245, 245, 247, 0.94);
}
```

One type family: system font stack (`-apple-system, BlinkMacSystemFont, "SF Pro Display", "Helvetica Neue", sans-serif`). No Google Fonts CDN. No external dependencies.

## What Has Failed

| Attempt | Failure |
|---------|---------|
| `innerWidth`/`innerHeight` for W,H | Coordinate mismatch — canvas CSS box ≠ viewport |
| `getBoundingClientRect()` for W,H | Circular dependency — setting `cv.width` can change the rect |
| Calling `fill()` synchronously before layout | `clientWidth`/`clientHeight` return 0 |
| Rendering only points with `e > 0.002` | Latent structure invisible — nothing renders at rest |
| Stale function references (missing `hx`/`hy`) | Spring force broken — points never return home |
| `inset:0` instead of explicit `top/left/width/height` | Less reliable cross-browser |
| Early `hasEnergy` guard in connection computation | Base lattice never visible |

## Architecture Constraints

- One function = one conceptual task. Separate: `fill` (geometry), `step` (physics), `reconnect` (connections), `pull` (neighbor forces), `draw` (rendering), `pointer` (input)
- No external dependencies. No CDN. No framework.
- Zero allocations inside render loop — reuse arrays, mutate in place.
- Skip connection recomputation when field is settled (reuse `baseConns`).
- Minimum viable DOM: `<canvas>` + `.hero` div with `<h1>` and `<p>`.

## Design Language

The Prima visual identity is not decoration. It is the constraint model rendered spatially. Every visual decision derives from the same primitive: `as / not_as`.

### Color

| Role | Value | Meaning |
|------|-------|---------|
| Field | `#08080b` | The void — near-black, not pure. Sits between presence and absence. |
| Ink | `rgba(245,245,247, ...)` | All surface elements derive from one white, modulated only by opacity. No secondary greys. |
| Blue `#5a7aff` | **as** — the positive edge. Appears when structure is affirmed. |
| Gold `#f5c451` | **frontier** — the seam. Appears in transition, the in-between state. |
| Orange `#e06a3a` | **not_as** — the negative edge. Appears on violation, guard fires, boundary breach. |

Only one color may dominate the field at a time. The three states sum to unity. The field's dominant hue expresses what the system is doing — not what it looks like.

### Typography

- **One family**: system font stack only. `-apple-system, BlinkMacSystemFont, "SF Pro Display", "Helvetica Neue", sans-serif`
- **Weight**: 240–320 (ultra-light to light). Never bold. Authority comes from restraint, not weight.
- **Tracking**: generous. Letter-spacing at `-0.04em` to `-0.06em` for display, `0.02em` to `0.08em` for labels.
- **Sizing**: `clamp()` for fluidity. Display: `clamp(2.8rem, 11vw, 8rem)`. Body: `clamp(0.72rem, 1.3vw, 0.9rem)`.
- **Color**: `rgba(245,245,247, 0.94)` for primary, stepping down to `0.32` for secondary. Opacity is hierarchy.

### The Point-Field Lattice

The signature visual of Prima. A regular grid of small points rendered on a dark canvas. The lattice is the latent structure of the constraint model — always present, invisible until disturbed.

- **At rest**: points at 2.5% opacity, radius 0.5px. Near-invisible. The field is calm.
- **On interaction**: pointer repels points locally. Disturbed points brighten, enlarge, reveal connections. The lattice emerges.
- **Settling**: energy decays at 0.955/frame. The field returns to equilibrium. No looping. No screensaver motion.
- **Constraint guard fire**: shockwave propagates. Field turns orange. Points scatter violently, then recover. The system reacted structurally to a boundary violation.

### Motion Principles

1. **Every movement originates from state, interaction, geometry, or recovery.** No decoration.
2. **Calm and continuous.** Damping at 0.93 ensures overshoot is absorbed, not amplified.
3. **Disturbance propagates.** Neighbor pull (`NPULL ≈ 0.0014`) allows local disturbance to ripple through the lattice — not instantly, but through connected structure.
4. **Resize recomputes, not stretches.** Grid placement recalculates from viewport dimensions. The lattice adapts to the canvas.

### What To Avoid

| Don't | Why |
|-------|-----|
| Gradients | Implies surface. Prima has no surface — only structure. |
| Cards, shadows | Implies containment. Prima has frames, not boxes. |
| Oversized marketing copy | The field is the subject. Text is secondary. |
| Particle simulation aesthetic | Pudds, trails, glow — feels decorative. Prima is structural. |
| Screensaver motion | Looping animation without intent. Every frame must originate from state. |
| External fonts / CDN | Dependency is vulnerability. System fonts are universal. |
| Hover gimmicks | Interaction must have structural consequence, not cosmetic response. |
| Section stacking | One canvas, one field. The page is not a document — it's a spatial system.

## Output Guard Integration

When combining the visual system with the `not_as visible` output guard, the field responds to guard violations:

```js
let shock = 0; // guard violation intensity, decays each frame

// On guard fire:
shock = 1;

// In step():
if(shock > 0 && Math.random() < 0.15) {
  p.vx += (Math.random() - 0.5) * shock * 12;
  p.vy += (Math.random() - 0.5) * shock * 12;
  p.e = Math.min(1, p.e + 0.5);
}

// In draw():
if(shock > 0.01) {
  ctx.fillStyle = `rgba(224,106,58,${shock * 0.04})`;
  ctx.fillRect(0, 0, W, H); // orange wash across entire field
}
// Points shift to orange while shock persists
```

The shockwave is the visual language of constraint violation. It communicates: *"The boundary was tested. It held."*