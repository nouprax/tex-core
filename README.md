# TeX Core

TeX Core is a cross-platform LaTeX renderer: a C engine that turns
self-contained LaTeX source — a single file or a single block — into an
immutable, platform-neutral **render tree** with fully resolved layout, plus
idiomatic bindings for Swift, Kotlin Multiplatform, and
ECMAScript/TypeScript. One release gives every platform the same parsing,
layout, metrics, and canonical dump behavior.

TeX Core deliberately stops at the render tree. It does not rasterize, load
font files, or draw. Consumers map the tree onto their platform's native
rendering technology:

- **Web** — MathML from the tree's semantic view, or SVG/Canvas from its
  geometric view.
- **Swift / Apple** — `NSAttributedString` text attachments, Core Text, or
  custom views.
- **Kotlin / Android / JVM** — spans, custom views, Compose layouts, or
  Java2D.

TeX Core is the rendering companion to
[Markdown Core](https://github.com/nouprax/markdown-core): the formula spans
its formula extension captures are exactly the inputs TeX Core renders. The
two projects remain independently consumable.

## Status

**Pre-release; the infrastructure phases (1–10) of the setup plan are
complete and the engine is climbing milestone M1 (math core).** The C
engine, its render-tree contract (schemaVersion 4), the Swift, Kotlin
Multiplatform, and ES/WASM bindings, the fail-closed CI quality gates, and
the release workflows are all in place. `Document.compile` covers ordinary
text atoms with explicit spacing in document mode and, in the math modes,
classed math atoms — the ASCII math characters plus the M1 symbol-command
set (Greek letters, binary operators, relations, arrows, letter-like and
delimiter symbols) — with braced groups, superscripts and subscripts set
at TeX's script sizes per Appendix G, fractions and binomials (`\frac`,
`\dfrac`, `\tfrac`, `\binom` family) built by Appendix G rule 15, and
variable-size delimiters (`\left`/`\right`, the `\big` family) grown
through the Computer Modern size faces into piece assemblies per rule 19,
laid out with TeX's inter-atom spacing over embedded Computer Modern
metrics, with byte-identical canonical dumps on every platform. The rest
of the M1–M4 surface (radicals, big operators, accents, style switches,
text galley, domain notations) lands before `1.0.0`. Nothing usable is
published yet.

Build and test the C core:

```sh
make build && make test    # or: cmake --preset default && ctest --preset correctness
```

## Planned packages

| Platform | Coordinate | Entry point |
| --- | --- | --- |
| C | `tex-core` (CMake/pkg-config; GitHub Release archives) | `tex_core_document_compile` |
| Swift | SwiftPM `https://github.com/nouprax/tex-core`, product `TexCore` | `Document.compile` |
| Kotlin Multiplatform | `com.nouprax:kotlin-tex-core` (Maven Central) | `Document.compile` |
| ECMAScript / WASM | `@nouprax/es-tex-core` (npm) | `Document.compile` |

All packages release the same version from the same protected `vX.Y.Z` tag.

## Roadmap

Capability grows along a fixed major-version ladder:

- **1.0.0 — Full LaTeX rendering of self-contained input.** A single file or
  single block renders completely; mathematics, chemistry, physics, biology,
  and other domain notations are subsets of that one LaTeX surface. The
  programmable TeX layer (user macros), reference resolution, package/module
  loading, multi-file input, and page breaking are deliberately out — output
  is one continuous galley.
- **2.0.0 — Incremental rendering.** Markdown-core-v2-style sessions: mutate
  the input and receive the updated render tree as fast as possible, with
  immutable structurally-shared snapshots, deltas, and damage-proportional
  commit cost.
- **3.0.0 — Full TeXbook support.** The programmable TeX layer, modules,
  multi-file projects, references, and page building: a product-ready core
  for a TeX project editor.

## Documents

- [`docs/specs/2026-07-20-repo-setup.md`](docs/specs/2026-07-20-repo-setup.md)
  — the normative setup plan: positioning, naming family, architecture,
  package requirements, and the phased task breakdown.
- [`docs/repository-setup-template.md`](docs/repository-setup-template.md) —
  the normative CI/release/control-plane contract, imported from
  markdown-core.
- [`docs/specs/render-tree.md`](docs/specs/render-tree.md) — the canonical
  render-tree schema contract (schemaVersion 1), with
  [`docs/specs/render-tree-dump.md`](docs/specs/render-tree-dump.md) as its
  deterministic textual form and [`specs/render-tree/`](specs/render-tree/)
  as the executable cross-platform conformance corpus.
- [`docs/specs/c-naming.md`](docs/specs/c-naming.md) — C naming
  conventions for the engine and its test tree.

## License

[BSD-2-Clause](LICENSE), copyright Nouprax.
