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

**Pre-release, phase 0.** This repository currently contains the adopted setup
plan and repository hygiene only; the engine, bindings, CI, and release
pipeline land in the phases defined by
[`docs/specs/2026-07-20-repo-setup.md`](docs/specs/2026-07-20-repo-setup.md).
Nothing is published yet and no platform support is claimed.

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

## License

[BSD-2-Clause](LICENSE), copyright Nouprax.
