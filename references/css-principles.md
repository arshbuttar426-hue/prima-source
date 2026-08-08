# CSS Principles — the medium Prima writes into

> A distillation of what CSS *is*, so the Prima constraint compiler understands the
> substrate it targets. Written for a system that thinks in frames, constraints,
> and linear traces — because that is exactly what CSS is: **a declarative constraint
> solver** over boxes.

Prima asks "what does a thing count as?" CSS answers the same question at the
rendering layer. This document is the map Prima needs to translate its frames into
rendered truth without fighting the medium.

---

## 0 · The Mental Model

CSS is **not** a programming language in the imperative sense. It is a **constraint
resolution system**:

```
DOM + Rules  →  Cascade (resolve conflicts)  →  Computed style  →  Layout  →  Paint
```

Compare with Prima's own pipeline: `Frame → Pipeline → Atoms → Output`.
The cascade is Prima's *frame layer* — it decides what each element **counts as**.
Layout engines are the *atoms* — deterministic algorithms that transform input into output.

Three governing facts:

1. **Declarative.** You state the *law* (the rule), not the steps. The browser resolves it.
2. **Cascading.** Multiple laws can apply to one element. The cascade is the conflict-resolver,
   and it always produces exactly one winner per property.
3. **Inheriting.** A law can propagate down the tree unless explicitly overridden.

This maps 1:1 onto Prima's axiom: *every frame is a complete configuration; no frame is
wrong — only different.* CSS never produces "wrong" — only a specific computed result.

---

## 1 · The Layered Model

CSS operates in layers that each solve a different problem. Prima should treat these
as **distinct frame types** — a rule never crosses layer boundaries casually:

| Layer | Asks | Prima equivalent |
|-------|------|------------------|
| **Document** (DOM) | What are the elements & hierarchy? | the frame's topology |
| **Cascade** | Which declaration wins? | polarity / slot resolution |
| **Layout** | Where does each box sit? | the trace over positions |
| **Rendering** (paint/composite) | What pixels appear? | the output |

Key discipline: **separate structure from presentation from motion.** A Prima design
system (like `apple_hig.prima`) should compile to tokens + layout + behavior layers,
not one fused blob.

---

## 2 · The Box Model (the atom of CSS)

Every element renders as a **box**. Layout is the placement of boxes. This is the
lowest primitive — everything else composes from it.

```
        ┌───────────────────────────────┐
        │  margin  (transparent, outer) │
        │   ┌─────────────────────────┐ │
        │   │  border                 │ │
        │   │   ┌───────────────────┐ │ │
        │   │   │  padding          │ │ │
        │   │   │   ┌─────────────┐ │ │ │
        │   │   │   │  content    │ │ │ │
        │   │   │   └─────────────┘ │ │ │
        │   │   └───────────────────┘ │ │
        │   └─────────────────────────┘ │
        └───────────────────────────────┘
```

- **content** — the actual content box (text, image, children).
- **padding** — inner spacing; keeps content off the edges.
- **border** — the visible seam; frame *is* the boundary.
- **margin** — outer spacing between siblings; collapses between vertical neighbors.

**`box-sizing`** decides what `width`/`height` mean:
- `content-box` (old default): width = content only; total = content + pad + border.
- `border-box` (modern, always): width includes padding + border. **Always use border-box.**
  It makes "the box fits the space" true.

**Box types:**
- **block** — full-line, stacks vertically, honors width/height/margins.
- **inline** — flows within a line, ignores width/height, respects line-height.
- **replaced** — `<img>`, `<video>`: intrinsic size replaced by content.

> Prima frame: `element as block not_as inline` — position and axis are the constraint.

---

## 3 · Layout Systems (the algorithms)

Layout is where boxes are *placed*. Modern CSS has four cooperating systems.
Prima should choose a system explicitly per region — never mix unconsciously.

### 3.1 Normal Flow
The default: block stacks vertically, inline flows in a line. Source order = visual order.
**Margin collapse** — vertical margins of adjacent siblings merge (take the larger).
Controls: `display`, `float` (legacy), `vertical-align`.

### 3.2 Flexbox (one-dimensional)
Packs items along **one axis** (row or column), with cross-axis alignment as a
secondary concern.

- **main axis** = the `flex-direction` axis; **cross axis** = the other.
- `justify-content` — distribution on the **main** axis (start/center/space-between).
- `align-items` / `align-self` — alignment on the **cross** axis.
- **Flex sizing** is a three-way negotiation per item:
  - `flex-basis` — the starting size.
  - `flex-grow` — how much leftover space the item claims (proportional).
  - `flex-shrink` — how much it surrenders when space runs out.
- Shorthand `flex: grow shrink basis`.

Key mental model: **flexbox distributes space, then aligns.** It's the tool for bars,
navs, toolbars, and centering (`justify-content:center; align-items:center`).

### 3.3 Grid (two-dimensional)
Positions items on **two axes simultaneously** using tracks and lines.

- `grid-template-columns` / `grid-template-rows` — define tracks.
- `fr` — fractional unit: `1fr 2fr` gives a 1:2 split of free space. **The golden tool**
  for proportional columns (use with ratio, not just equal).
- `gap` — spacing between tracks (never margin on children; that's the flex/grid-idiom).
- `grid-area` / named areas: `grid-template-areas` for semantic layout.
- **Auto-placement** — items flow into free cells automatically.
- `minmax(min, max)` + `auto-fit` / `auto-fill` for responsive columns:
  `repeat(auto-fit, minmax(280px, 1fr))` — the modern responsive grid in one line.
- **subgrid** — a nested grid can inherit the parent's tracks (`grid-template-columns: subgrid`).

### 3.4 Positioning & Stacking (explicit offset)
- `static` (default, in flow) · `relative` (offset from itself) · `absolute` (offset from
  nearest *positioned* ancestor) · `fixed` (viewport) · `sticky` (scroll-aware hybrid).
- `z-index` only works on **positioned** elements and flex/grid items; joined with
  `transform`/opacity, it forms a **stacking context**.

### 3.5 Which to choose
| Need | System |
|------|--------|
| One axis, content-sized items | Flexbox |
| Two axes, equal/proportional regions | Grid |
| Full-page skeleton | Grid (areas) |
| Bar / nav / centering a thing | Flexbox |
| Text flow / paragraphs | Normal flow |

> Prima frame: `layout as grid for regions, not_as flexbox` — pick by dimensionality.

---

## 4 · The Cascade (the constraint solver)

The cascade is the *law* that resolves conflicting declarations. Four inputs, in
**priority order** (later wins):

1. **Origin & importance** — `!important` > normal; author > user > UA.
2. **Cascade layers** (`@layer`) — earlier-declared layers lose to later ones.
   Lets you structure "base < theme < override" cleanly — **this is Prima's frame
   priority, made explicit.**
3. **Specificity** — how specific the selector is. Rough score:
   `inline style > #id > .class/:attr > element`.
4. **Source order** — if still tied, the last rule wins.

**Inheritance** — separate mechanic: some props (color, font) inherit down the tree
unless overridden; others (margin, width, background) do not.

> Design-system implication: **prefer low specificity + cascade layers + custom
> properties** over nested specificity wars. That is how a theme overrides a base
> without `!important`.

`@scope` (modern): nearest-scope scoping without flattening to specific classes —
declaration locality without specificity inflation.

---

## 5 · Design Tokens & Custom Properties (the token layer)

**Custom properties** (`--name`) are Prima's *design tokens made native*: named values
that live per-element and **inherit**.

- Declared in `:root` → inherited everywhere → a **theme** is just a different `:root`
  (or a scoped override). This is exactly `apple_hig.prima` compiled to CSS.
- **Theming is cascading** — override a token on a sub-tree and the whole subtree
  re-themes. No re-render of logic, just value resolution.
- Used via `var(--token, fallback)`.
- **`@property`** registers typed custom properties — enables them to *animate*
  (number/color/angle), which plain `--x` cannot.
- **`color-mix(in oklch, a, b)`** — compute colors at use-time; a palette becomes a
  few primaries + recipes instead of a fixed list.

> Primitive-principle: **tokens at the top, thin overrides downward.** Font, spacing,
> color, radius, easing — each a token. Content reads tokens; tokens never read content.

---

## 6 · Sizing & Units

Understanding units = understanding what a size *means* (proportion to what?).

| Unit | Relative to | Use |
|------|-------------|-----|
| `px` | device pixel | crisply defined layout, borders |
| `rem` | **root** font-size | typography scale, spacing rhythm (root-anchored) |
| `em` | parent font-size | margin tied to a component's own text |
| `%` | parent (varies by prop) | fluid widths / paddings |
| `vw/vh` | viewport | full-screen; `svh/dvh` for mobile-safe units |
| `ch` | width of "0" | text **measure** (max 45–75ch) |
| `cqw` | container | container-query-relative sizing |

**Intrinsic sizing** — size from content, not a set number:
- `min-content` — as narrow as content allows · `max-content` — as wide as it wants
- `fit-content` — up to max-content, but capped by available space.

**Fluid type / fluid spacing** — `clamp(min, preferred, max)`:
`font-size: clamp(1rem, 0.5rem + 2vw, 1.5rem)`. The single modern tool for
viewport-scaled rhythm. Combine with a golden-modular scale for the "weirdly-perfect"
typography discussed in the design work.

---

## 7 · Responsive Systems

Responsiveness = the layout *degrades gracefully* across measure. Two cooperating engines:

1. **Media queries** — respond to the *viewport* (or `prefers-*`).
   Mobile-first: base styles for small, `@media(min-width: X)` to enhance.
2. **Container queries** — respond to the *nearest container*, not the viewport.
   Enables components that reflow inside any parent. (Modern, first-class.)
3. **Fluid techniques** — `minmax`, `auto-fit`, `fr`, `clamp()` — reflow *without*
   a media query at all for grids and type.

**Logical properties** (`margin-inline`, `padding-block`, `inline-size`) — direction-
agnostic; laying out left-to-right vs right-to-left is a frame swap, not a rewrite.

---

## 8 · Typography

Typography is 90% of perceived elegance. Principles:

- **A restrained type scale.** Modular scale (e.g. ratio 1.25–1.618). Choose a base
  (e.g. 15–17px) and derive headings from it. Fewer steps, more confidence.
- **Line-height tuned to measure.** ~1.5 for body; tighter (1.0–1.1) for display.
- **Measure** = 45–75 characters/line. Use `clamp()` + `ch` to keep it.
- **Letter-spacing** — negative on large display (tightens), positive on small caps.
- **`font-variant-numeric: tabular-nums`** — monospaced digits for tables/live-readouts
  (the metamorph % readouts should use this to avoid jitter).
- **System font stacks first** — `-apple-system`, `BlinkMacSystemFont`, `SF Pro`.
  This is the *offline* discipline: no network font fetch, native rendering, classy.

---

## 9 · Color & Contrast

- **Tokenized palettes** over literal hex scattered about.
- **Contrast is a constraint** — WCAG AA needs 4.5:1 for body, 3:1 for large text/
  UI. A classy monochrome (the chosen identity) relies on luminance steps: near-black
  bg + white/gray ink with careful mid-tone `#9a9aa2`.
- **`color-scheme`** + `light-dark()` — one source that flips with the OS, avoiding a
  bolted-on `prefers-color-scheme` duplication.
- Use **alpha/transparency** (rgba) for hierarchy over hard new colors.

---

## 10 · Motion & Transitions

Motion is a *language*; it should have one grammar, not a vocabulary.

- **Timing/easing is the "accent".** Hard `cubic-bezier(0.9,0,0.1,1)` reads as decisive
  (the shutter); `cubic-bezier(0.34,1.56,0.64,1)` (back) reads as springy. Choose per
  intent; the shutter's hard ease is the signature move.
- **Animate `transform` and `opacity` only.** They run on the compositor (cheap/composited).
  Animating width/height/top triggers **layout** + **paint** (expensive).
- **Transition** for state → state; **Web Animations API / requestAnimationFrame** for
  driven loops (the grid).
- **`prefers-reduced-motion`** — respect it. Always ship a static fallback.
- **View Transitions API** — modern primitive for whole-viewport transitions
  (could power the abrupt shutter natively on supporting browsers).

---

## 11 · Stacking & Rendering Performance

- **`will-change` / `contain`** — isolate subtrees so the browser doesn't re-layout
  globally. `content-visibility` lazy-renders off-screen sections (`auto`).
- **Avoid layout thrash** — don't read layout then write in a loop; batch.
- **Compositing** — layers from `transform`, `opacity`, `will-change`. Keep animated
  layers few.
- **Stacking context** — `transform:translateZ/scale`, `opacity<1`, `filter`,
  `position+z-index`, `backdrop-filter` all create contexts; nested contexts are
  self-contained (a child can't escape its parent's z-order).

---

## 12 · Accessibility (the hard constraint)

- **Semantic structure first** — order in the DOM *is* reading order for assistive tech.
  Don't reshape visually in a way that lies about structure.
- **`focus-visible`** — visible, predictable focus for keyboard users (a style, not an
  afterthought).
- **Contrast** (see §9) and **touch target** ≥ 44px for interactive elements.
- **prefers-*** queries** — reduced-motion, color-scheme, contrast. These are
  *user frames*; the design must respond to them, not override.

---

## 13 · Modern CSS Checklist (the frontier today)

The features that make a site feel "current" and keep code tiny:

- ✅ `border-box` everywhere
- ✅ Cascade layers `@layer` (theme-over-base)
- ✅ `@scope` (localised rules)
- ✅ Container queries `@container` + `cqw`
- ✅ `:has()` (parent styling from a child — powerful, expressive)
- ✅ Subgrid
- ✅ `color-mix()` + `@property` + typed tokens
- ✅ Logical properties (inline/block)
- ✅ `light-dark()` / `color-scheme`
- ✅ Fluid `clamp()` type & spacing
- ✅ `content-visibility: auto`
- ✅ View Transitions API (for the shutter/nav transitions)
- ✅ `inset` shorthand (top/right/bottom/left as one value)

---

## 14 · How Prima should model CSS

The translation layer — a `.prima` design system compiling to real, idiomatic CSS.

| Prima frame | CSS target |
|-------------|------------|
| `frame Colors { ... }` | custom properties in `:root` (+`@property` typed) |
| `frame Typography { ... }` | `clamp()` modular scale on tokens |
| `frame Spacing { unit: 8px }` | spacing scale `calc(var(--unit)*n)` / 8pt grid |
| `frame Layout { grid / flex }` | the right algorithm per region (choose by axis-count) |
| `frame Motion { transition }` | compositor-only (transform/opacity) + a single easing grammar |
| `frame Components { card /* … */ }` | a cascade-layer for base + theme-override |
| `frame Responsive { breakpoints }` | mobile-first media + container queries + fluid |

**Non-negotiable output rules for Prima-compiled CSS:**
1. `box-sizing:border-box` on `*`.
2. Tokens in `:root`; theme = scoped `:root`/layer override; never inline literal colors.
3. One easing vocabulary (hard/soft/spring) — no random timing functions.
4. System font stack only (offline: zero network requests).
5. Animate transform/opacity only.
6. Respect `prefers-reduced-motion` and contrast.
7. Fluid type via `clamp()` from a golden-modular scale.

---

### The core insight, restated

**CSS is a constraint solver over boxes.** Prima is a constraint language over frames.
They are the same discipline at different layers. A Prima design system that
*understands* CSS — its cascade as a resolver, its engines as algorithms, its tokens
as the material, its motion as a grammar — will compile to output that feels native,
not bolted-on. That is the difference between a design system and a theme that fights
the browser.

*"The frame is the architecture" — and the browser is the ground the frame is built on.*
