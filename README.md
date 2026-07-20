# TeX Core

TeX Core is a cross-platform TeX math renderer: a C engine that turns
TeX/LaTeX math-mode source into an immutable, platform-neutral **render tree**
with fully resolved layout, plus idiomatic bindings for Swift, Kotlin
Multiplatform, and ECMAScript/TypeScript. One release gives every platform the
same parsing, layout, metrics, and canonical dump behavior.

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
| C | `tex-core` (CMake/pkg-config; GitHub Release archives) | `tex_core_formula_render` |
| Swift | SwiftPM `https://github.com/nouprax/tex-core`, product `TexCore` | `Formula.render` |
| Kotlin Multiplatform | `com.nouprax:kotlin-tex-core` (Maven Central) | `Formula.render` |
| ECMAScript / WASM | `@nouprax/es-tex-core` (npm) | `Formula.render` |

All packages release the same version from the same protected `vX.Y.Z` tag.

## Documents

- [`docs/specs/2026-07-20-repo-setup.md`](docs/specs/2026-07-20-repo-setup.md)
  — the normative setup plan: positioning, naming family, architecture,
  package requirements, and the phased task breakdown.
- [`docs/repository-setup-template.md`](docs/repository-setup-template.md) —
  the normative CI/release/control-plane contract, imported from
  markdown-core.

## License

[BSD-2-Clause](LICENSE), copyright Nouprax.
