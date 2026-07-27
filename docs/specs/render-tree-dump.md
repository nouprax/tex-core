# Canonical render-tree dump

Status: schemaVersion 3 (milestone M1 fractions change-set); schemaVersion
2 landed 2026-07-27; schemaVersion 1 was frozen with Phase 3 on
2026-07-20.

The dump is the deterministic public diagnostic representation of the render
tree and the reviewed expected representation used by conformance tests. It
is not JSON, XML, a renderer, or a serialization/transport API. Consumers
that need structured data must traverse the typed immutable tree
(`render-tree.md`).

The complete reviewed `.tree` golden corpus and its coverage manifest live
only at `specs/render-tree/`. Every conformance target enumerates that same
non-empty manifest. Each binding implements this format independently over
its own public immutable tree; it never calls the native C dump or another
binding's output. Dump text is never used to construct production values.

The dump is byte-deterministic for a given tree across runs and platforms:
integer scaled-point arithmetic only, no floating point, no locale, no
pointers. Output is ASCII, uses LF line endings, ends with exactly one LF,
and has no trailing whitespace.

## Line grammar

The first line is the schema line:

```text
render-tree 3
```

The integer is the render-tree `schemaVersion` and moves only with a
reviewed schema change.

Every following line is one node: two spaces of indentation per depth
level, the kind, then its fields in canonical order as ` name=value`
pairs. A node's children follow its line, indented one level deeper, in
child (source) order. The root node has depth zero.

```text
hbox x=<m> y=<m> width=<m> ascent=<m> descent=<m> src=<b>..<e>
  glyph x=<m> y=<m> cp=U+XXXX style=<s> family=<f> size=<m> width=<m> ascent=<m> descent=<m> italic=<m> src=<b>..<e>
  kern x=<m> width=<m> src=<b>..<e>
  rule x=<m> y=<m> width=<m> ascent=<m> descent=<m> src=<b>..<e>
```

## Scalar encoding

- `<m>` (measure): the scaled-point value printed by Knuth's
  `print_scaled` (TeX §103) — a leading `-` for negatives, the integer
  part in decimal, a `.`, then the shortest digit run that reads back to
  the same scaled value (at least one digit, so zero is `0.0`) — followed
  by the unit suffix `pt`. Examples: `0.0pt`, `3.33328pt`, `-1.66672pt`,
  `10.0pt`.
- `cp`: `U+` followed by the codepoint in uppercase hexadecimal, at least
  four digits, no padding beyond four (`U+0061`, `U+1D453`).
- `style`, `family`: the lowercase contract spelling, unquoted (`upright`,
  `italic`, `main`).
- `src`: `<begin>..<end>` — the node's half-open byte range as base-10
  ASCII with no leading zeros except zero itself. Ranges printed by the
  dump are absolute for the dumped compile call.
- Every field of a kind is always printed; fields are never omitted for
  being zero or default. No field contains spaces, quotes, or escapes.

## Field order by kind

Fields appear in exactly this order (`render-tree.md` is the semantic
authority):

| Kind | Ordered fields |
| --- | --- |
| `hbox` | `x`, `y`, `width`, `ascent`, `descent`, `src` |
| `glyph` | `x`, `y`, `cp`, `style`, `family`, `size`, `width`, `ascent`, `descent`, `italic`, `src` |
| `kern` | `x`, `width`, `src` |
| `rule` | `x`, `y`, `width`, `ascent`, `descent`, `src` |

Child count is structural: a node's children are exactly the deeper-indented
lines that follow it, so the dump carries no count or edge-label fields.

Any behavior-bearing field added later must be added to this table, the
manifest coverage vocabulary, affected shared goldens, and every dump
implementation in the same reviewed change (plan §5.5). Failed compiles
produce no dump; the corpus encodes expected failures in the corpus-only
error-record form defined in `specs/render-tree/README.md`.
