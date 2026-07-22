# Render-tree contract

Status: schemaVersion 1, frozen with Phase 3 on 2026-07-20.

This document is the language-neutral public render-tree contract implemented
by the C engine and, as they land, the Swift, Kotlin, and ES bindings
(Phases 5–7). Platform APIs may use idiomatic syntax, but they must not
change names, nullability, ownership, traversal order, defaults, units, or
semantics. The executable oracle for this contract is
`specs/render-tree/manifest.json` with its reviewed `.tex`/`.tree` pairs;
the canonical textual form is `render-tree-dump.md`.

The tree is the compiled artifact consumers render from. It carries both
views of plan §5.3 in one tree: the semantic view (node kinds and, as the
surface grows, roles) and the geometric view (resolved metrics and offsets).
This library never draws.

## Core rules

- A successful `Document.compile` yields exactly one immutable tree; a
  failed compile yields no tree and a structured error (decision D4 —
  never partial output). There is no third outcome.
- Trees are immutable, acyclic, and safely shareable across threads once
  compile returns (decision D6). Nodes are borrowed views owned by the
  tree; nodes never expose parent pointers.
- Public reads never compute or allocate. Every value below is resolved
  before compile returns.
- Child order is source order: children appear in the order their source
  constructs appear in the compile call's input. Geometry (`x`) is derived
  from layout and may step backward (negative kerns); source order never
  does.
- Every construct the engine accepts appears in the tree; every construct
  it does not accept is a structured error naming the offending bytes.
  Nothing is silently skipped, demoted, or normalized away (template §4.3).

## Units and coordinates

- Layout arithmetic is integer TeX scaled points internally
  (2^16 sp = 1 pt); the public tree publishes absolute points as `double`.
  A published value is always an exact integer-sp multiple of 2^-16 pt, so
  equality is exact and platform-independent.
- The walking skeleton typesets at a fixed 10 pt em; `size` on every glyph
  is that em size in points.
- Each node's `x`/`y` place its reference point relative to its parent's
  reference point. A box's reference point sits at its left edge on the
  baseline; `x` grows rightward, `y` grows upward (positive `y` is above
  the parent baseline). `ascent` extends up from the reference point,
  `descent` down.

## Node inventory

Fields are non-nullable and always present; a field that does not apply to
a kind does not exist on that kind. `src` is the source byte range defined
in the source-range section.

| Kind | Fields in canonical order | Semantics and invariants |
| --- | --- | --- |
| `hbox` | `width: measure`, `ascent: measure`, `descent: measure`, `src: range`, `children: [node]` | horizontal box; the compile root; `width` is the advance of its content, `ascent`/`descent` the maxima over children; the root box spans the whole input |
| `glyph` | `x: measure`, `y: measure`, `cp: codepoint`, `style: style`, `family: family`, `size: measure`, `width: measure`, `ascent: measure`, `descent: measure`, `italic: measure`, `src: range` | leaf; one glyph named by Unicode codepoint + style + family + size, never a private glyph ID; `italic` is the italic correction, included in the advance of a math Ord glyph |
| `kern` | `x: measure`, `width: measure`, `src: range` | leaf; fixed horizontal advance; `width` may be negative; the engine resolves interword spacing, explicit spacing commands, and math inter-atom spacing to kerns (glue arrives with stretch/shrink later, as a schema change) |

Field types:

- `measure` — points as `double`, exact integer-sp multiple (see units).
- `range` — half-open byte range `[begin, end)` into the compile input.
- `codepoint` — Unicode scalar value.
- `style` — `upright | italic`. Bold and the remaining faces arrive with
  the 1.0.0 milestones as schema changes.
- `family` — `main`. Additional families arrive as schema changes.

Math inter-atom spacing: in the math modes every atom carries one of TeX's
classes (Ord, Op, Bin, Rel, Open, Close, Punct, Inner — TeXbook chapter
17), contextual Bin atoms resolve to Ord exactly as in TeX's first mlist
pass, and the chapter-18 pair table inserts a thin (3/18 em), medium
(4/18 em), or thick (5/18 em) kern directly before the right atom of a
spaced pair — after any explicit spacing between the two, which never
suppresses the inserted space. The class itself is not a tree field: it is
layout input, visible through the inserted kerns. An inter-atom kern's
`src` is the source gap between the two atoms it separates,
`[left.src.end, right.src.begin)` — empty when the atoms are adjacent, and
covering the blanks or explicit-spacing bytes between them otherwise.
Document mode has no atom classes and never receives inter-atom kerns.

## Source ranges

Every node records the byte range of the source it came from, for caret
mapping and error UX. Ranges are half-open `[begin, end)` byte offsets into
this compile call's input; the root box's range is `[0, input length)`.

The contract guarantees ranges through traversal of a compiled tree and
through the canonical dump. It deliberately does **not** promise that
absolute ranges are storage-resident on nodes or readable in O(1)
independently of a walk: incremental sessions (the 2.0.0 rung) may store
anchor-relative positions internally and resolve absolute values during
traversal, exactly as the dump (an O(n) walk) does. Consumers must treat a
range as data obtained from a walk of a specific compiled tree, not as a
stable node property across revisions.

## Errors

A failed compile reports one structured error:

- `status` — `invalid-argument | invalid-utf8 | unsupported |
  allocation-failed`.
- an optional half-open source byte range locating the offending bytes
  (always present for `invalid-utf8` and `unsupported` errors from real
  input),
- a deterministic ASCII message naming the offending token, such as
  `unsupported character U+007B` or `unsupported command \frac`.

Error identity — status, range, and message — is public contract surface
and is pinned by the corpus error cases in the corpus error-record form
(see `specs/render-tree/README.md`).

## Versioning and change protocol

The schema carries `schemaVersion` 1, printed in the dump schema line and
frozen in the manifest. Any intentional change to grammar, layout, metrics,
schema, or dump — including widening `style`/`family`, adding node kinds or
fields, or resolving glue — is a public behavior change under plan §5.5:
one reviewed commit updates this contract, the engine, all shipped
bindings, fixtures, goldens, and consumers together. No platform-local
divergence, no normalization shims.
