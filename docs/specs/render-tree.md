# Render-tree contract

Status: schemaVersion 4 (milestone M1 delimiters change-set: the size
families, `\left`/`\right`, explicit delimiter sizes, and `\binom`);
schemaVersion 3 (fractions) and 2 (scripts) landed 2026-07-27;
schemaVersion 1 was frozen with Phase 3 on 2026-07-20.

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
| `hbox` | `x: measure`, `y: measure`, `width: measure`, `ascent: measure`, `descent: measure`, `src: range`, `children: [node]` | horizontal box: the compile root, a braced group's nucleus, a script box, a fraction atom's box, a numerator/denominator box, a `\left`/`\right` box, or a delimiter piece assembly; `x`/`y` place its reference point in its parent (the root sits at the origin), a script, numerator, or denominator box's `y` is its baseline shift; `width` is the advance of its content — plus TeX's `\scriptspace` (0.5 pt) on a script box — `ascent`/`descent` the maxima over children floored at zero; the root box spans the whole input |
| `glyph` | `x: measure`, `y: measure`, `cp: codepoint`, `style: style`, `family: family`, `size: measure`, `width: measure`, `ascent: measure`, `descent: measure`, `italic: measure`, `src: range` | leaf; one glyph named by Unicode codepoint + style + family + size, never a private glyph ID; `italic` is the italic correction, included in the advance of a math Ord glyph unless its atom carries a subscript — the correction then offsets the superscript box instead, exactly as TeX |
| `kern` | `x: measure`, `width: measure`, `src: range` | leaf; fixed horizontal advance; `width` may be negative; the engine resolves interword spacing, explicit spacing commands, math inter-atom spacing, and the null-delimiter spaces flanking a fraction to kerns (glue arrives with stretch/shrink later, as a schema change) |
| `rule` | `x: measure`, `y: measure`, `width: measure`, `ascent: measure`, `descent: measure`, `src: range` | leaf; a solid rectangle: its reference point sits at its left edge on the parent's baseline shifted by `y`, and the ink extends `ascent` up, `descent` down, and `width` right from there; today produced only as the fraction bar (`ascent` is the rule thickness, `descent` is zero, and `src` is the fraction command's own token) |

Field types:

- `measure` — points as `double`, exact integer-sp multiple (see units).
- `range` — half-open byte range `[begin, end)` into the compile input.
- `codepoint` — Unicode scalar value.
- `style` — `upright | italic`. Bold and the remaining faces arrive with
  the 1.0.0 milestones as schema changes.
- `family` — `main | size1 | size2 | size3 | size4`. The size families
  are the delimiter size-variant faces (Computer Modern cmex, vendored
  as KaTeX Size1–Size4): their glyphs are always upright, and their
  `size` is always the 10 pt text em — TeX's extension fonts do not
  shrink inside scripts. Additional families arrive as schema changes.

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
argument (a character, symbol command, fraction command, or group) is
`missing superscript argument`/`missing subscript argument`, an
unterminated group is `unclosed group`, a stray `}` is
`unmatched closing brace`, and `{` nesting deeper than 255 groups is
`group nesting too deep`. Document mode keeps rejecting `{ } ^ _` as
unsupported characters until the M3 text surface.

## Fractions

`\frac`, `\dfrac`, and `\tfrac` are math-mode commands taking exactly two
arguments — numerator, then denominator. An argument is a single math
character, a symbol command, or a braced group; anything else where an
argument is required (including a bare fraction command, a script mark, or
end of input) is the structured error `missing numerator argument`/
`missing denominator argument`, locating the offending token — or the
fraction command itself at end of input. A fraction command is also a
legal script argument (`x^\frac{1}{2}`), filling the script field the way
a group does.

A fraction is one Inner atom for spacing and Bin demotion, and scripts
attach to it like to any box nucleus. Layout is TeXbook Appendix G rule 15
(tex.web's `make_fraction`): `\frac` is set in the surrounding style
(cramping included), `\dfrac` forces display style and `\tfrac` text style
exactly as `\displaystyle`/`\textstyle`; the numerator is set one step
smaller (`num_style`), the denominator one step smaller and cramped
(`denom_style`). The numerator shifts up `num1` (display) or `num2`
(other styles), the denominator down `denom1`/`denom2`, and each shift
grows until the gap to the bar reaches three rule thicknesses in display
style, one otherwise. The bar is a `rule` of the current size's
`defaultRuleThickness`, vertically centered on the math axis
(`axisHeight`) with its top `half(thickness)` above it, rounding up; all
parameters resolve at the fraction's own style size, so a fraction inside
a script uses the script columns. Both boxes are TeX's `clean_box` over
the field; the narrower of the two centers over the wider one, its inset
`half(excess)` rounding up. TeX's null delimiters flank the pair: one
kern of `\nulldelimiterspace` (1.2 pt, absolute at every size) on each
side, with an empty source range at the construct's edge. The fraction
box's `width` is the common numerator/denominator width plus the two
kerns; its `ascent`/`descent` are the shifted numerator top and
denominator bottom. Child order is source order: left kern, `rule` (its
`src` is the command token), numerator box, denominator box, right kern.

`\binom` is the generalized fraction with no bar (TeX's `\atopwithdelims`
over parentheses): the shifts start at `num1`/`num3` (display/other) and
`denom1`/`denom2`, and when the gap between the parts falls short of
seven rule thicknesses in display style (three otherwise) both parts move
apart by half the shortfall, `half()` rounding up. No `rule` appears; the
null-delimiter kerns are replaced by real parenthesis delimiters sized to
`delim1` (display) or `delim2` (other styles) of the fraction's size.
Both delimiters carry the command token's source, which precedes the
arguments — so child order is: left delimiter, right delimiter,
numerator box, denominator box, with the right delimiter placed after
the parts by `x` alone.

## Delimiters

`\left`/`\right` enclose a math sub-list between two variable-size
delimiters. A delimiter is one of the characters `( ) [ ] < > / |` or
`.`, the control symbols `\{ \} \| \\`, or the commands `\lbrace \rbrace
\lbrack \rbrack \langle \rangle \lfloor \rfloor \lceil \rceil \vert
\Vert \backslash \uparrow \downarrow \updownarrow \Uparrow \Downarrow
\Updownarrow`; `<`, `>`, and `\\` map to the angle brackets and the
backslash exactly as plain TeX's delimiter codes, and `.` is the null
delimiter — an empty slot published as one `\nulldelimiterspace` kern.
Every `\left` needs its `\right` inside the same group (`missing
\right`), a stray `\right` is `unmatched \right`, and a token that is not
a delimiter where one is required is `missing delimiter`. `\left`
subformulas nest inside braces and other `\left` pairs; the 255-group
bound counts both.

The construct is one Inner atom (TeXbook chapter 17): outside, it spaces
as Inner; inside its box the left delimiter spaces as an Open atom, the
right as a Close atom, and the enclosed atoms keep their normal
inter-atom spacing. The published box's children are, in source order:
the left delimiter node, the enclosed nodes spliced directly (never
re-boxed), and the right delimiter node.

Delimiter sizing is TeXbook Appendix G rule 19 (tex.web's
`make_left_right` and `var_delimiter`): with `h`/`d` the maxima of the
enclosed material above and below the axis, the target is
`max((delta div 500) * 901, 2*delta - 5pt)` where
`delta = max(h - axisHeight, d + axisHeight)` — TeX's
`\delimiterfactor` 901 and `\delimitershortfall` 5 pt, integer `div`.
The variant ladder tries the main-family glyph at the current style's
size and every larger script size, then the size1–size4 faces at the
text em, and finally — for delimiters TeX extends — a piece assembly;
the first candidate at least as tall as the target wins. Angle brackets,
the slashes, and the arrows-without-pieces stop at their largest glyph,
exactly as their Computer Modern successor chains do. A chosen glyph is
published as one `glyph` (family `main` or `size1`…`size4`); an
assembly is an `hbox` of piece glyphs (family `size1` or `size4`)
stacked by `y` — top piece, enough repeaters to reach the target, a
middle piece for braces, bottom piece — with no overlap. Either form is
vertically centered on the math axis: the node's center sits at
`axisHeight`, `half()` rounding up. A delimiter node's `src` is its own
delimiter token; the surrounding box spans `\left` through `\right`.

The explicit-size commands `\bigl \bigm \bigr \big` and their `\Big`,
`\bigg`, `\Bigg` families take one delimiter argument and run the same
ladder against the fixed plain TeX targets — rule 19 over empty boxes
8.5 pt, 11.5 pt, 14.5 pt, and 17.5 pt tall at the 10 pt text axis,
whatever the surrounding style. The atom class is Open for `l`, Close
for `r`, Rel for `m`, and Ord for the bare forms; the published node is
the same centered glyph or assembly, and scripts attach to it like to
any atom.

## Radicals

`\sqrt` is a math-mode command taking one mandatory argument (a
character, a symbol command, a sized delimiter, or a braced group) after
an optional LaTeX index: a `[` directly after the command opens the
index, which runs to the matching `]` — braces nest inside it, anywhere
else `[` stays an ordinary Open atom. A missing argument is the
structured error `missing radical argument` (the offending token, or the
command at end of input); an unterminated index is `unclosed radical
index` at its `[`. A radical is one Ord atom and a legal script argument.

Layout is TeXbook Appendix G rule 11 (tex.web's `make_radical`): the
radicand is a clean box in the cramped current style; the clearance is
one `defaultRuleThickness` plus a quarter of the x-height in display
style, a quarter thickness otherwise; the sign runs the delimiter ladder
against radicand height + depth + clearance + thickness — stopping at
the `size4` glyph, exactly like the ladders without published assembly
pieces — and half of any excess joins the clearance. The bar is a `rule`
of the current thickness whose bottom sits the clearance above the
radicand, flush with the sign's ink top; sign and bar carry the command
token's source. The index is laid out in uncramped scriptscript style,
raised 0.6 (the 16.16 fraction 39322) of the sign-and-bar box's ascent
minus descent, between kerns of 5 mu and -10 mu of the current size's
quad (LaTeX's `\r@@t` exactly — the negative kern deliberately tucks
the sign under the raised index, so the sign's `x` may be negative).
Child order is source order: sign, bar, then for an indexed radical the
5 mu kern, the index box, and the -10 mu kern (the kerns anchor at the
bracket edges), then the radicand box.

## Operators

The big operators (`\sum`, `\prod`, `\coprod`, `\int`, `\oint`, and
the `\big...` set-operator family) are Op atoms whose glyph comes from
the `size1` face — `size2` in display style — always at the text em and
vertically centered on the math axis (tex.web's `make_op`); its italic
correction is the script delta, so a `\nolimits` integral tucks its
subscript under the slant. The function names (`\sin` through `\Pr`)
are Op atoms whose nucleus is the upright letter run at the current
size; `\limsup` and `\liminf` carry their inner thin space.

Scripts on an Op atom place as limits — the TeXbook rule 13a assembly —
in display style for the sums and the `\lim` family, never for the
integrals and the `\sin` family; `\limits` and `\nolimits` directly
after an Op atom override its default and extend its source range, and
anywhere else they are the structured error `misplaced \limits`/
`misplaced \nolimits`. A limits assembly is one hbox: the operator
centered in the common width, the superscript box centered above
(nudged half the italic correction right) behind the `bigOpSpacing3`/
`bigOpSpacing1` clearance, the subscript box centered below (nudged
half left) behind `bigOpSpacing4`/`bigOpSpacing2`, and the assembly's
ascent and descent gain the `bigOpSpacing5` pads — the one hbox whose
extent deliberately exceeds its children's maxima. Its children keep
source order: operator, then the script boxes in written order. Without
limits the scripts attach exactly as on any box nucleus.

## Accents

The math accents (`\hat \check \tilde \acute \grave \dot \ddot
\breve \bar \vec`) take one argument exactly as a radical does, and a
missing one is `missing accent argument`. The accented atom is Ord and
a legal script argument. Layout is TeXbook Appendix G rule 12 (tex.web's
`make_math_accent`): the nucleus is a clean box in the cramped style and
keeps its width; the accent glyph — main family at the current size —
sits at its natural height over a nucleus no taller than the x-height
and rides up with anything taller, shifted right by the nucleus
character's skew (the vendored KaTeX `skew` column, TeX's skewchar
kern). `\widehat` and `\widetilde` run a width ladder instead: the
text-size glyph, then the size faces, the first at least as wide as the
nucleus, capped at `size4`. Child order is source order: the accent
glyph (its `src` is the command token), then the nucleus box.

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

The schema carries `schemaVersion` 4, printed in the dump schema line and
frozen in the manifest; version 4 widened `family` to the size faces and
added delimiters, version 3 added the `rule` node kind and fractions,
version 2 added `x`/`y` to `hbox`, nested boxes, and per-glyph script
sizes. Any intentional change to grammar, layout, metrics,
schema, or dump — including widening `style`/`family`, adding node kinds or
fields, or resolving glue — is a public behavior change under plan §5.5:
one reviewed commit updates this contract, the engine, all shipped
bindings, fixtures, goldens, and consumers together. No platform-local
divergence, no normalization shims.
