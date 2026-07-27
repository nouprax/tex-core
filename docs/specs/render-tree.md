# Render-tree contract

Status: schemaVersion 2 (milestone M1 scripts change-set: nested boxes,
positioned `hbox`, script sizes); schemaVersion 1 was frozen with Phase 3
on 2026-07-20.

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
- The engine typesets at a 10 pt text size; script and scriptscript
  material is set at 7 pt and 5 pt. `size` on every glyph is the em it
  was set at, in points.
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
| `hbox` | `x: measure`, `y: measure`, `width: measure`, `ascent: measure`, `descent: measure`, `src: range`, `children: [node]` | horizontal box: the compile root, a braced group's nucleus, or a script box; `x`/`y` place its reference point in its parent (the root sits at the origin), a script box's `y` is its baseline shift; `width` is the advance of its content — plus TeX's `\scriptspace` (0.5 pt) on a script box — `ascent`/`descent` the maxima over children floored at zero; the root box spans the whole input |
| `glyph` | `x: measure`, `y: measure`, `cp: codepoint`, `style: style`, `family: family`, `size: measure`, `width: measure`, `ascent: measure`, `descent: measure`, `italic: measure`, `src: range` | leaf; one glyph named by Unicode codepoint + style + family + size, never a private glyph ID; `italic` is the italic correction, included in the advance of a math Ord glyph unless its atom carries a subscript — the correction then offsets the superscript box instead, exactly as TeX |
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
pass, and the chapter-18 pair table inserts a thin (3 mu), medium (4 mu),
or thick (5 mu) kern directly before the right atom of a spaced pair —
after any explicit spacing between the two, which never suppresses the
inserted space. As in TeX, the medium and thick pairs — and the thin pairs
TeX marks conditional — are inserted only in display and text styles,
never inside script material. One mu is 1/18 of the current size's quad,
so the same commands and pairs shrink inside scripts; `\quad` and
`\qquad` are em-based and always measure the 10 pt text em, exactly as
TeX's em unit in math mode. The class itself is not a tree field: it is
layout input, visible through the inserted kerns. An inter-atom kern's
`src` is the source gap between the two atoms it separates,
`[left.src.end, right.src.begin)` — empty when the atoms are adjacent, and
covering the blanks or explicit-spacing bytes between them otherwise.
Document mode has no atom classes and never receives inter-atom kerns.

## Math styles and scripts

Math material is set in TeX's styles: display math opens in display
style, inline math in text style, both at the 10 pt text size.
Superscripts and subscripts are set one style smaller — script material at
7 pt, scriptscript material at 5 pt, deeper nesting stays scriptscript —
with TeX's cramped variants inside subscripts. A braced group `{…}` is one
Ord atom whose nucleus is laid out in the surrounding style and boxed; a
group's box never changes style.

Script geometry follows TeXbook Appendix G rule 18 (tex.web's
`make_scripts`) over the vendored Computer Modern parameters
(`scripts/metrics/katex-font-metrics.json`): superscript minimum shifts
`sup1`/`sup2`/`sup3` for display/other/cramped styles, subscript shifts
`sub1`/`sub2`, box-nucleus drops `supDrop`/`subDrop`, the x-height
quarter and four-fifths clearances, and the four-rule-thickness clash
fixup when both scripts are present. Every script box takes TeX's
`clean_box` simplification — a box holding exactly one glyph drops that
glyph's italic correction from its width — and then gains `\scriptspace`
(0.5 pt) of width. A superscript sits `italic` to the right of its
subscript (rule 18f); each script box appears in `children` at its source
position, so a subscript written first precedes its superscript.

The script grammar is TeX's: `^`/`_` attach to the preceding atom, a
script with nothing to attach to gets an empty-nucleus Ord atom of its
own, a repeated script on one atom is the structured error
`double superscript`/`double subscript`, a script mark without a legal
argument (a character, symbol command, or group) is
`missing superscript argument`/`missing subscript argument`, an
unterminated group is `unclosed group`, a stray `}` is
`unmatched closing brace`, and `{` nesting deeper than 255 groups is
`group nesting too deep`. Document mode keeps rejecting `{ } ^ _` as
unsupported characters until the M3 text surface.

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

The schema carries `schemaVersion` 2, printed in the dump schema line and
frozen in the manifest; version 2 added `x`/`y` to `hbox`, nested boxes,
and per-glyph script sizes. Any intentional change to grammar, layout, metrics,
schema, or dump — including widening `style`/`family`, adding node kinds or
fields, or resolving glue — is a public behavior change under plan §5.5:
one reviewed commit updates this contract, the engine, all shipped
bindings, fixtures, goldens, and consumers together. No platform-local
divergence, no normalization shims.
