# Shared render-tree conformance corpus

This directory is the repository's only canonical LaTeX/render-tree corpus.
Each `.tex` input has a reviewed `.tree` companion containing its expected
canonical outcome. `manifest.json` is the sole case list and freezes case
order, paths, compile options, outcomes, UTF-8/LF rules, and coverage labels.
The C conformance target enumerates it directly; the Swift, Kotlin, and ES
conformance targets (Phases 5–7) must enumerate the same manifest or use
build-generated resources derived from it. A runner- or platform-owned copy
is not allowed in this directory.

These files are test-only product contract data. They are not a production
serialization format, a C-to-binding transport, or a public API. Every
binding compiles the source through its public `Document.compile`, walks its
own immutable public render tree, and compares the canonical dump byte for
byte. No production path may consume dump text, and no release artifact may
contain this directory.

The schema contract is `docs/specs/render-tree.md`; the dump grammar is
`docs/specs/render-tree-dump.md`. The manifest `schemaVersion` equals the
version in the dump's `render-tree` schema line.

## File rules

- `.tex` inputs are byte-exact compile input: every byte, including any
  final line feed, is compiled. Inputs are LF-only text with two documented
  exceptions — a case covering `input.empty` or `input.noFinalNewline` omits
  the final LF, and the input of a case covering `error.invalidUtf8`
  deliberately contains invalid UTF-8. CR and CRLF sources cannot live here
  (`.gitattributes` normalizes this directory), so CR scanning behavior is
  pinned by the engine unit suite instead.
- `.tree` expected files are UTF-8, LF-only, and end with exactly one final
  LF. A case with outcome `tree` holds the canonical render-tree dump. A
  case with outcome `error` holds a canonical error record:

  ```text
  render-error 1
  error status=<status> src=<begin>..<end> message=<message>
  ```

  `<status>` is the public `tex_core_status` value spelled `invalid-utf8`
  or `unsupported`; `src` is the error's half-open source byte range
  (`src=none` when the error carries no range); `message` is the error's
  deterministic ASCII message, read to end of line. The error record exists
  only in this corpus; the production API reports errors through the
  structured `tex_core_error` value, never as text.

## Maintenance

Run `node scripts/check-render-tree-fixtures.mjs` to audit the schema,
discovery, grammar, declared coverage, and completeness. Intentional engine
or schema changes may regenerate tree candidates with the CLI, for example
`build/cmake/packages/tex-core/core/tex-core --mode math-inline
specs/render-tree/math-letters.tex`; candidates are human-reviewed diffs
against the accepted goldens. Tests and CI never rewrite or accept them. A
grammar or schema change must update the contract, manifest, goldens, every
shipped binding, and conformance evidence in the same reviewed change
(plan §5.5).

Source byte ranges in the goldens are absolute values for this compile call,
printed by the dump walk. The goldens do not promise that absolute ranges
are storage-resident on nodes; see the source-range section of the schema
contract.
