# TeX Core — Repository Setup Plan

## 1. Document status

- Status: **Adopted at bootstrap** (2026-07-20). This is the normative plan for
  building the `nouprax/tex-core` repository. Changes require a reviewed PR.
- Companion contract: [`docs/repository-setup-template.md`](../repository-setup-template.md)
  (imported from `nouprax/markdown-core`) is the **normative CI, release, and
  control-plane contract** for this repository. This plan does not restate its
  invariants; where this plan says "per template §N", that section is binding.
- Reference implementation: [`nouprax/markdown-core`](https://github.com/nouprax/markdown-core)
  at commit `bb3b7fab62a6f3b1aa43ab78d18f2f31b5e7b680`. Per template §14.1 we
  copy its *responsibilities and semantics contract*, never its product names,
  and each binding follows its own ecosystem's best practices.

## 2. Project positioning

TeX Core is a cross-platform LaTeX renderer that turns self-contained LaTeX
source — a single file or a single block — into an immutable, platform-neutral
**render tree** with fully resolved layout. The C engine and every binding
live in this repository, so one release gives every platform identical
parsing, layout, metrics, and canonical dump behavior.

Capability grows along a fixed major-version ladder (§13): **1.0.0** renders
the full LaTeX surface of self-contained input — mathematics, chemistry,
physics, biology, and other domain notations are subsets of that surface —
without the programmable TeX layer; **2.0.0** adds markdown-core-v2-style
incremental rendering, so a consumer that mutates the input gets the updated
render tree as fast as possible; **3.0.0** completes full TeXbook semantics
as a product-ready core for a TeX project editor.

TeX Core deliberately stops at the render tree. It does not rasterize, load
font files, touch the file system or network, or draw anything. Consumers map
the render tree onto their platform's native rendering technology:

- Web: MathML (from the semantic view) or SVG/Canvas (from the geometric view).
- Swift/Apple: `NSAttributedString` text attachments, Core Text, or custom
  views.
- Kotlin/Android/JVM: spans, custom `View`s, Compose layouts, or Java2D.

TeX Core is the rendering companion to Markdown Core: the formula spans that
Markdown Core's formula extension captures (`$...$`, `$$...$$`, `\(...\)`,
`\[...\]`) are exactly the inputs TeX Core renders. The two projects remain
independently consumable; neither depends on the other at build or run time.

### 2.1 Goals

1. One C11 engine, one version, one commit: parsing, layout, and render-tree
   construction identical on every platform (template §2.1 invariants).
2. A versioned, canonical render-tree schema with byte-exact deterministic
   dump, shared conformance fixtures, and cross-platform golden tests.
3. Idiomatic bindings: Swift (SwiftPM), Kotlin Multiplatform (Maven Central),
   ECMAScript/TypeScript over WASM (npm), each with real staged-artifact
   consumers.
4. The full markdown-core quality bar: fail-closed PR gates, CodeQL, sanitizer
   suites, package audits, secret-free release dry runs, and tag-driven
   coordinated releases.

### 2.2 Non-goals

- Reaching past the current major's rung ahead of the ladder: 1.0.0 excludes
  the programmable TeX layer (user macros), reference resolution,
  package/module loading, multi-file projects, and page breaking (§5.1).
  Those are the 3.0.0 rung, not gaps to backfill in 1.x.
- Rasterization, font file parsing/embedding, or shipping font binaries.
- Text/HTML output formats. MathML/SVG generation belongs to consumers (or to
  future separate packages), not to the core.
- Mutable trees. Render trees are immutable snapshots.

### 2.3 Bootstrap decisions (review before Phase 1 starts)

These decisions were taken at bootstrap so that later phases are unambiguous.
Changing any of them requires editing this document first.

| # | Decision | Choice |
| --- | --- | --- |
| D1 | Implementation origin | Written from scratch; no upstream C baseline. If an upstream is later imported, add `UPSTREAM.md` + `COPYING` per markdown-core practice |
| D2 | License | BSD-2-Clause, copyright Nouprax (keeps the family license-compatible with markdown-core) |
| D3 | Release ladder | 1.0.0 = full LaTeX rendering of self-contained input (single file or block; no user macros, reference resolution, modules, multi-file, or pages); 2.0.0 = incremental rendering (sessions/deltas, markdown-core v2 style); 3.0.0 = full TeXbook support, product-ready for a TeX project editor (§5.1, §13) |
| D4 | Error model v1 | Fail-fast: structured error with source range; no partial output. Error-tolerant "error box" mode is milestone M5, not 1.0.0 |
| D5 | Font model | Font metrics compiled into the C core as static tables (math and text faces); glyphs exposed as Unicode codepoints + face style; no file I/O (§5.4) |
| D6 | Concurrency | No process-global mutable state; render trees are immutable and safely shareable across threads (markdown-core 2.0 contract) |
| D7 | Version line | `VERSION` starts at `0.1.0`; first coordinated public release is `1.0.0` (Phase 11) |
| D8 | npm bootstrap | First npm publish is an interactive trusted-publisher bootstrap (`0.1.0`), mirroring markdown-core's npm `1.0.0` bootstrap; first coordinated OIDC publish is `1.0.0` |

## 3. Naming family

All names are fixed now; audits added in later phases enforce them.

| Surface | Name |
| --- | --- |
| GitHub repository | `nouprax/tex-core` |
| C package directory | `packages/tex-core` |
| C library / pkg-config | `tex-core` |
| C identifier prefix | `tex_core_` (functions/types), `TEX_CORE_` (macros/constants) |
| Swift package directory | `packages/swift-tex-core` |
| SwiftPM package identity | `tex-core` — SwiftPM derives it from the dependency URL `https://github.com/nouprax/tex-core`; consumers see this identity |
| SwiftPM manifest name | `swift-tex-core` — the `name:` field in `Package.swift`, matching the package directory (markdown-core convention: manifest name and URL-derived identity are distinct fields, e.g. its `name: "swift-markdown-core"` with identity `markdown-core`); `audit:repository` enforces this value |
| SwiftPM product / module | `TexCore` (C shim target: `TexCoreC`, never public API) |
| Kotlin package directory | `packages/kotlin-tex-core` |
| Maven root coordinate | `com.nouprax:kotlin-tex-core` |
| Maven Android runtime | `com.nouprax:kotlin-tex-core-android-runtime` |
| Kotlin namespace | `com.nouprax.tex.core` |
| ES package directory | `packages/es-tex-core` |
| npm package | `@nouprax/es-tex-core` (public, `publishConfig.access=public`) |
| WASM asset export | `./tex-core.wasm` |
| Canonical spec directory | `specs/render-tree/` |
| Release tags | `v<VERSION>`, e.g. `v1.0.0` |

Public entry point, uniform across platforms:

- C: `tex_core_document_compile(source, options)` → owned render tree with
  borrowed node views; `tex_core_render_tree_dump(...)` for the canonical dump.
- Swift / Kotlin / ES: `Document.compile(source, options) -> RenderTree`, and
  `RenderTree.dump()`. Bindings copy the tree into platform values and retain
  no native handle after `compile` returns (markdown-core binding-ownership
  model). `Document.compile` mirrors markdown-core's `Document.parse` family
  shape and is the verb TeX users already use ("compile the document"). It
  supersedes the bootstrap's `Formula.render`: "Formula" no longer fits a
  full-LaTeX 1.0.0, and "render" reads as drawing on screen — exactly what
  this library never does. The *RenderTree* noun stays: the tree is the
  compiled artifact consumers render from.
- `options.mode` selects the input form: `document` (self-contained LaTeX
  file or block) or `mathInline`/`mathDisplay` (a bare math fragment with
  delimiters already stripped — the Markdown Core companion path).

## 4. Versioning and coordinated release

- One `VERSION` file at the repository root is the single canonical SemVer.
  Every package manifest, consumer fixture, release-notes file, and artifact
  metadata must match it; `release check-version` fails on any drift
  (template §2.2).
- All ecosystems release the same version from the same protected `vX.Y.Z`
  tag. Failed tags are immutable release attempts; any byte change gets a new
  SemVer (template §8).
- Source packages follow SemVer for public API behavior. The C binary ABI is
  **not** promised stable between releases; consumers rebuild engine and
  binding together (markdown-core policy).
- The render-tree schema carries its own `schemaVersion` inside the canonical
  spec; a schema change is a public behavior change and follows §5.5.

## 5. Architecture and public contract

### 5.1 Input language and the 1.0.0 scope boundary

- Encoding: UTF-8. Invalid bytes are a structured error (D4), never silently
  replaced.
- One self-contained input per call: a single LaTeX file or a single block,
  selected by `options.mode` (§3). In the math-fragment modes, delimiters
  (`$`, `$$`, `\(`, `\[`) are **not** consumed by the core — callers pass
  bare math content (Markdown Core's formula extension already strips them);
  in `document` mode the ordinary LaTeX math delimiters and environments work
  as in LaTeX.
- 1.0.0 renders the **full LaTeX surface of self-contained input**: text-mode
  galley typesetting and math, with domain notations (mathematics, chemistry,
  physics, biology, …) delivered as built-in command coverage — subsets of
  the one LaTeX surface, not separate products. The surface is staged through
  milestones M1–M4 (§13).
- 1.0.0 exclusions (the 3.0.0 rung, decision D3):
  - the programmable TeX layer — `\def`/`\newcommand`, expansion,
    conditionals, registers, catcode changes;
  - reference resolution — `\label`/`\ref`/`\cite` (explicit `\tag`
    numbering works);
  - package/module loading — `\usepackage` is recognized only for the
    built-in covered set, never loads code or definitions;
  - multi-file input — `\input`/`\include`;
  - page breaking — 1.0.0 lays out one continuous galley, not pages.
- Unsupported input is a structured error naming the offending token and its
  source range — never a silent skip (template §4.3: no drift-swallowing
  normalization).

### 5.2 Pipeline

```text
UTF-8 LaTeX source
      │ tokenizer (fixed catcodes; user catcode changes arrive with 3.0.0)
      ▼
token stream
      │ parser (built-in command set, grouping, environments,
      │         text/math mode tracking)
      ▼
semantic lists (horizontal/vertical/math lists: atoms with classes,
      │         paragraphs, environment structures)
      │ layout (TeX box-and-glue model over embedded font metrics;
      │         math style resolution D/T/S/SS, cramped variants;
      │         Knuth–Plass paragraph line breaking; continuous galley)
      ▼
render tree (semantic structure + resolved geometry, immutable)
```

The semantic lists are internal. The public contract is the render tree only.

### 5.3 Render tree

The render tree exposes **both views in one tree** so each consumer picks what
it needs:

- **Semantic view**: every layout node carries a `role` (e.g. `numerator`,
  `denominator`, `radicand`, `superscript`, `operatorLimit`, …) and structure
  nodes preserve the source construct (`fraction`, `radical`, `scripts`,
  `delimited`, `row`, joined by text-mode structures — `paragraph`, `line`,
  `heading`, `list`, `tabular`, `environment` — as milestone M3 lands).
  MathML generators walk this.
- **Geometric view**: every node has resolved metrics — `width`, `ascent`,
  `descent`, `italicCorrection` where applicable — and children carry offsets
  relative to their parent's reference point. Leaf kinds are `glyph`
  (codepoint + math font style + size), `kern`, `glue` (resolved to a fixed
  width in the tree), and `rule`. SVG/attachment generators walk this.
- Units: TeX scaled points are internal; the public tree publishes
  device-independent `double` em-relative and absolute (pt) values; the exact
  field set is fixed by the canonical spec in Phase 3.
- Source mapping: every structure node records the byte range of the source it
  came from (for caret mapping and error UX).
- Trees are immutable, acyclic, and safely shareable across threads (D6).

### 5.4 Fonts

- The core embeds metrics tables (advance, height, depth, italic correction,
  kerning, larger-variant and extensible-recipe data) for the default math
  and text faces, generated offline into checked-in C tables by a maintenance
  script (like markdown-core's tracked re2c outputs: generated files are
  committed; generation never runs during build/test).
- v1 metrics source: the KaTeX/Computer Modern metrics data (MIT-licensed
  fonts data); the generator records provenance and license in the generated
  header. Consumers may render with any visually compatible font.
- The public tree names glyphs by Unicode codepoint + style
  (`upright/italic/bold/…`) + font family identifier + size; it never exposes
  private glyph IDs.

### 5.5 Public-behavior change protocol

Per template §4.3: an intentional change to grammar, layout, metrics, schema,
or dump requires *one reviewed commit* that updates the schema/spec, the C
engine, all bindings, fixtures, goldens, and consumers together. No
platform-local divergence, no normalization shims.

### 5.6 C API shape

Mirrors markdown-core's C facade:

- Opaque owned handles (`tex_core_render_tree *`) with explicit
  `_free`; borrowed read-only node views; no hidden allocation on reads.
- All entry points thread-safe with no global state; allocation-failure paths
  return structured errors and leak nothing (validated under sanitizers and
  allocation-failure injection, milestone M1 acceptance).
- Version macros/functions `TEX_CORE_VERSION`, `tex_core_version_string()`
  matching `VERSION`.
- Naming/style spec written in Phase 3 as `docs/specs/c-naming.md` (modeled on
  markdown-core's).

## 6. Platform package requirements

### 6.1 `packages/tex-core` (C)

- C11, CMake ≥ 3.20 with committed `CMakePresets.json`: configure/build
  presets `default`, `asan`, `ubsan`, `tsan`; CTest presets `correctness`,
  `conformance`, `benchmark` (+ sanitizer variants). Shared and static builds;
  install/export with pkg-config and CMake package config.
- Root `Makefile` is a thin wrapper over the single CMake/CTest graph — never
  a second runner (markdown-core rule).
- Test layout `packages/tex-core/tests/`: correctness suites, conformance
  runner over `specs/render-tree/` fixtures, benchmark executables, fuzz
  harness (libFuzzer entry; AFL optional) as explicit non-default targets.
- A `tex-core` CLI binary (render + dump) for debugging and CI evidence.

### 6.2 `packages/swift-tex-core` (Swift)

- Root `Package.swift` (SwiftPM requires root manifests for tag packages):
  targets `TexCoreC` (C sources compiled directly from `packages/tex-core`),
  `TexCore` (public module), test targets `TexCoreTests` and
  `TexCoreConformanceTests` (fixtures injected by a build-tool plugin +
  resource generator, as markdown-core does), `TexCoreBenchmarks` executable.
- Platforms: iOS 18+, macOS 15+. Public API is an immutable `Sendable` value
  tree with exhaustive typed visitors; no C pointers or lifetimes surface.
- Consumer: an external SwiftPM project under
  `packages/swift-tex-core/Tests/Consumer` building only the `TexCore`
  product from a staged source archive.
- swift-format + SwiftLint pinned per `docs/toolchains.md`.

### 6.3 `packages/kotlin-tex-core` (Kotlin Multiplatform)

- Targets: Android (min API 21), JVM 17, `macosArm64`, `linuxX64`. Group
  `com.nouprax`, version from root `VERSION`.
- `android-runtime` subproject publishing
  `kotlin-tex-core-android-runtime` (JNI payload), same split as
  markdown-core.
- Desktop JVM JAR merges Linux + macOS JNI payloads at release assembly; klib
  cross-compilation disabled (`kotlin.native.enableKlibsCrossCompilation=false`);
  each native payload is built and consumer-tested on its real host
  (template §4.4, §14.11).
- Repo-owned Gradle wrapper with pinned distribution SHA-256, dependency
  locking, `gradle/verification-metadata.xml`, Foojay-provisioned daemon JDK,
  version catalog, ktlint official style. Maven wrapper (`mvnw`) for the Maven
  consumer.
- Gradle Managed Devices: API 36 Pixel profiles for 4 KB and 16 KB page sizes;
  CI uses the build-once/test-many APK artifact model (one x86_64
  instrumentation APK producer, four emulator consumers).
- Consumers: KMP, JVM Gradle, Android, and Maven-wrapper projects resolving
  from a staged local repository only.
- IDE contract: clean Android Studio / IntelliJ import with no credentials, no
  prebuilt natives; headless Gradle model smoke in CI.

### 6.4 `packages/es-tex-core` (ECMAScript / WASM)

- Emscripten (pinned in `docs/toolchains.md`) compiles the C core to WASM;
  ESM-only package with strict `exports` (`.` with `types` + `import`, plus
  `./tex-core.wasm`), `sideEffects: false`, `files: ["dist", "README.md"]`,
  Node ≥ 20 engine floor.
- TypeScript type definitions for the full render-tree model; API mirrors
  §3 (`Document.compile`, `dump`).
- Tests run on Node and a real browser runtime; conformance runner bundles the
  shared fixtures; benchmark script.
- Consumer: `npm pack` → install the tarball into a clean temp project →
  import and run (no workspace links).

## 7. Monorepo layout (target end state)

```text
.
├── .github/                      # workflows, rulesets, environments (Phases 4/8/9/10)
├── CHANGELOG.md
├── CMakeLists.txt                # root delegating to packages/tex-core
├── CMakePresets.json
├── LICENSE
├── Makefile
├── Package.swift                 # root SwiftPM manifest (tag package)
├── README.md
├── VERSION
├── build.gradle.kts / settings.gradle.kts / gradle.properties / gradle/
├── gradlew / gradlew.bat / mvnw / mvnw.cmd / .mvn/
├── package.json / pnpm-workspace.yaml / pnpm-lock.yaml / .node-version
├── docs/
│   ├── development-environment.md
│   ├── releasing.md
│   ├── repository-setup-template.md   # imported normative contract
│   ├── releases/<VERSION>.md
│   └── specs/                    # this plan, c-naming, render-tree specs
├── packages/
│   ├── tex-core/                 # C engine
│   ├── swift-tex-core/
│   ├── kotlin-tex-core/
│   └── es-tex-core/
├── specs/
│   └── render-tree/              # canonical schema, fixtures, goldens, manifest
└── scripts/                      # adapter + audit + release scripts
```

Root-level formatter/lint configs (`.clang-format`, `.cmake-format.yaml`,
`.swift-format`, `.swiftlint.yml`, `prettier.config.mjs`, `eslint.config.js`,
`.editorconfig`, `.prettierignore`) mirror markdown-core.

## 8. Toolchains

Adopt markdown-core's pinned matrix verbatim at Phase 1 and record it in
`docs/toolchains.md` (single source of truth; versions below are the adoption
baseline and may only move by a reviewed toolchain PR):

Gradle 9.6.1 (wrapper, pinned SHA) · Maven 3.9.16 (wrapper) · daemon JDK 26 ·
JVM bytecode 17 · Kotlin 2.4.0 · AGP 9.3.0 · Android SDK compile/target 36,
min 21 · NDK 28.2.13676358 · Android CMake 3.22.1 · ktlint 14.2.0/1.8.0 ·
Xcode 26.6 (Swift 6.3.3, swift-format 6.3.0) · SwiftLint 0.65.0 ·
clang-format 22.1.8 (`InsertBraces: true`) · cmake-format 0.6.13 ·
Node 26.5.0 · pnpm 11.7.0 · Prettier 3.9.5 · ESLint 10.0.1 · TypeScript 6.0.3 ·
Emscripten 4.0.23 · CMake ≥ 3.20 host floor.

## 9. Local development environment contract

- `scripts/init-environment.sh --check|--install [components…]` with the exact
  markdown-core semantics: `--check` read-only; `--install` non-interactive,
  idempotent, bootstraps only repo-managed tools (`.tools/`), JS dependencies,
  declared Android SDK packages, and the pinned emsdk; never installs Xcode,
  Android command-line tools, Gradle, or Maven; never reads publish
  credentials. Components: `core`, `node`, `java`, `wrappers`, `android`,
  `android-emulator`, `swift`, `emscripten`, `tools`, `dependencies`.
- The root adapter is `package.json` pnpm scripts + `scripts/*` (markdown-core
  form of template §2.2). Script namespace parity, with tex-core targets:
  `format[:check]`, `lint`, `build:c`, `build:swift`, `test:c-host`,
  `test:swift-macos`, `test:swift-ios-simulator`, `test:kotlin-jvm`,
  `test:kotlin-android-host`, `test:kotlin-android-emulator`,
  `test:kotlin-macos-arm64`, `test:kotlin-linux-x64`, `test:es-node`,
  `test:es-browser`, matching `conformance:*` lanes, `benchmark:*` lanes,
  `check:contracts`, `check:gradle-model`, `check:kotlin-consumers`,
  `audit:{repository,ci,tests,surface,packages}`, `release:check-version`,
  `release:dry-run`, and the aggregate `verify`.
- Release-quality local validation is the markdown-core sequence:
  `git clean -fdX` → `audit:repository --physical` → `init-environment
  --install` → `--check` → `pnpm verify` → `pnpm check:kotlin-consumers` →
  `pnpm release:dry-run`.

## 10. CI contract

Binding inventory (template §4.1) — all rows blocking once their phase lands:

| Binding | Host/build targets | Conformance | Consumer | Release artifact |
| --- | --- | --- | --- | --- |
| C core | Linux Clang shared, Linux GCC static, macOS Clang shared, Windows static; ASan/UBSan/TSan on Linux | canonical fixtures via CTest | CMake `find_package`/pkg-config link consumer | source + binary archives on GitHub Release |
| Swift | macOS, iOS Simulator; deployment-target builds iOS 18/26, macOS 15/26 | shared fixtures via plugin resources | external SwiftPM project | SwiftPM tag (source) |
| Kotlin/KMP | JVM, Android host, Android emulator {4 KB,16 KB}×{correctness,conformance}, macosArm64, linuxX64 | shared fixtures per target | KMP/JVM/Android/Maven staged consumers | Maven Central `com.nouprax` |
| ES/WASM | Node, browser | shared fixtures bundled | packed-tarball consumer | npm `@nouprax/es-tex-core` |

Everything else is exactly template §5–§6: DAG
`Health Check → Build → Build Test → Test → Required gates`, stable contexts
`Required gates` / `Development branch gates` / `CodeQL gate`, fail-closed
`if: always()` barriers (`Health Checks - Ready`, `Builds - Ready`,
`Build Tests - Ready`, `Tests - Ready`, `Benchmarks - Ready`), visible-name
vocabulary, artifact-manifest handoff with non-empty test inventories,
per-event concurrency lanes (no feature-branch push CI, no SHA lanes), CodeQL
manual builds for C/C++, Java/Kotlin, Swift and `none` for JS/TS, benchmarks
as required leaves with informational numbers, fork-safe metrics
(`pr-metrics-comment.yml`, optional), and controlled dependency submission.

Device/emulator consumers additionally follow the template's §14.15 addendum
(post-template stability fixes from markdown-core PRs #19/#27): mutable
runtime dependencies restored from explicit `-r<N>` cache keys with the save
landing right after download, every runtime phase bounded by a timeout, one
bounded retry with a fresh device lifecycle, job timeouts budgeted for two
full attempts, escalating never-blocking teardown, and evidence upload on
`failure() || cancelled()`. Where §14.15 and the older template body
disagree, §14.15 wins.

`scripts/audit-ci-policy.sh` enforces template §13 from Phase 4 onward.

## 11. Release contract

Per template §7–§9 and markdown-core's proven order:

- `release-dry-run.yml`: PR/manual, `contents: read` only, no environment, no
  secrets; proves secret env vars are empty; stages C archives (Linux+macOS),
  validates the SwiftPM source archive and plugin, packs and consumer-tests
  npm, aggregates all KMP publications, merges the two desktop JNI payloads
  into the one JVM JAR, signs with a disposable PGP key, produces and audits
  SHA-256/SHA-512, and runs every staged consumer; `Release Dry Run - Ready`
  fail-closed.
- `release.yml`: protected `vX.Y.Z` tag only. Validate (strict SemVer, tag ==
  `v$(VERSION)`, coordinated manifests/fixtures, non-empty
  `docs/releases/<VERSION>.md`, main ancestry) → reusable full CI on the tag
  snapshot → parallel `Build Release` producers (read-only) →
  `Assemble Release - Maven Central` (sign, recursive product-only audit,
  staged consumers) → `Release Artifacts - Ready` → publish in order:
  1. Upload user-managed Central deployment, wait `VALIDATED`.
  2. npm publish via OIDC trusted publishing (`id-token: write` on that job
     only), provenance on.
  3. Publish the validated Central deployment, wait `PUBLISHED`.
  4. GitHub Release from the same tag: source/C archives, checksums,
     artifact attestations, curated notes.
  Plus the `Resume Release - Published Artifacts` recovery path with the full
  template §8.4 guardrails.
- Release artifacts are product-only; containers (JAR/AAR/klib) are audited
  recursively. Publish jobs never rebuild.

### 11.1 External registry one-time setup (Phase 10)

| Item | Plan |
| --- | --- |
| Maven namespace | Reuse verified `com.nouprax` (no new verification) |
| Central token | New expiring Portal user token recorded in the org password manager; secrets `MAVEN_CENTRAL_USERNAME`/`MAVEN_CENTRAL_PASSWORD` in this repo's `release` environment |
| PGP key | Reuse org key `Nouprax Release <nouprax@outlook.com>`, fingerprint `0E46 FE94 9804 A119 20FE 904C 2D6E 1C75 20B9 5B01`, expires 2028-07-14; secrets `MAVEN_SIGNING_KEY`/`MAVEN_SIGNING_PASSWORD`; re-verify key-server retrievability before first release |
| npm | Interactive 2FA bootstrap publish of `@nouprax/es-tex-core@0.1.0` (D8); then configure trusted publisher org `nouprax`, repo `tex-core`, workflow `release.yml`, environment `release`; revoke the bootstrap session; no npm token in GitHub |
| SwiftPM | Identity derives from the repo URL + tag; nothing to register |
| GitHub | `release` environment (reviewer `DongyuZhao`, `prevent_self_review=false` while there is one operator), single `v*.*.*` tag deployment policy, tag ruleset, `main quality gates` ruleset — all via `scripts/bootstrap-repository.sh` (template §10), evaluate first, active after §12.2 verification |

## 12. Execution phases

Phases are strictly ordered; each lands as one or more PRs (after Phase 4,
through the live PR gates). A phase is done only when every acceptance box is
checked. Task lists are exhaustive — a phase with an unlisted deliverable is a
plan bug to fix here first.

### Phase 0 — Bootstrap commit (this change)

Tasks:

- [x] Import `docs/repository-setup-template.md` with provenance note.
- [x] Adopt this plan as `docs/specs/2026-07-20-repo-setup.md`.
- [x] `README.md` (positioning, planned coordinates, status), `LICENSE`
      (BSD-2-Clause, Nouprax), `CHANGELOG.md` (Keep-a-Changelog, Unreleased),
      `VERSION` = `0.1.0`.
- [x] Hygiene configs: `.gitignore`, `.gitattributes`, `.editorconfig`,
      `.clang-format` (with `InsertBraces: true`).

Acceptance: single commit on `main`; no build systems yet; nothing claims CI
or release support.

### Phase 1 — Root toolchain and development environment

Tasks:

- [x] Root `package.json` (private, `packageManager: pnpm@11.7.0`, engines,
      script skeleton that fails loudly for not-yet-landed lanes),
      `.node-version`, `pnpm-workspace.yaml`, committed `pnpm-lock.yaml`.
- [x] `prettier.config.mjs`, `.prettierignore`, `eslint.config.js`,
      `.cmake-format.yaml`, `.swift-format`, `.swiftlint.yml`.
- [x] `scripts/init-environment.sh` with §9 semantics and component model.
- [x] `scripts/format-c.sh`, `scripts/format-cmake.sh`, `scripts/lint-c.sh`,
      `scripts/format-swift.sh`, `scripts/lint-swift.sh` (tool bootstrap into
      `.tools/`, checksum-verified downloads).
- [x] `scripts/audit-repository.sh` (tracked-tree cleanliness, `--physical`,
      naming-family greps from §3).
- [x] `docs/toolchains.md` (§8 matrix) and
      `docs/development-environment.md` (§9 contract).

Acceptance:

- [x] Clean macOS checkout: `init-environment --check core node` passes;
      `--install dependencies tools` is idempotent (run twice, second is
      no-op).
- [x] `pnpm format:check` and `pnpm lint` pass (over docs/config surface).
- [x] No script reads credentials; `git clean -fdX` leaves a tree where
      `audit:repository --physical` passes.

### Phase 2 — C core walking skeleton

Tasks:

- [x] `packages/tex-core/` layout: `include/tex_core.h` (+ export headers),
      `core/` sources, `tests/`, `benchmarks/`, `cmake/` package-config
      templates; root `CMakeLists.txt`, `CMakePresets.json` (§6.1 presets),
      root `Makefile` wrapper.
- [x] Walking-skeleton engine: tokenizer + parser + layout for *ordinary
      atoms and explicit spacing only*, producing a real render tree
      (glyph/kern/hbox nodes with metrics from an initial embedded metrics
      table for a minimal glyph set).
- [x] `tex_core_document_compile` (with the `options.mode` input forms),
      `tex_core_render_tree_dump`, `tex_core_render_tree_free`, version API,
      structured error type with source ranges.
- [x] `tex-core` CLI (stdin/args → dump), correctness CTest suite including
      error paths and allocation-failure injection hooks, benchmark
      executable (trivial workload), libFuzzer harness target.
- [x] Metrics generator script (offline, provenance-recorded, output
      committed) for the initial table (D5, §5.4).

Acceptance:

- [x] `make build && make test`, `ctest --preset
      correctness|conformance|benchmark` all green (conformance may run a
      placeholder suite until Phase 3, but the preset and runner exist).
- [x] `asan`/`ubsan`/`tsan` presets green.
- [x] Dump output is byte-deterministic across repeated runs and across
      Linux/macOS.
- [x] `pnpm format:c:check`, `lint:c` cover the new sources.

### Phase 3 — Canonical render-tree spec, fixtures, C conformance

Tasks:

- [x] `specs/render-tree/`: schema document (every node kind, field, type,
      unit, nullability, default, child ordering), canonical dump format
      spec, `manifest.json` (schemaVersion + fixture inventory), fixture
      pairs (`<name>.tex` + `<name>.tree`) covering every construct shipped
      so far — growing with each §13 milestone — plus empty/error/boundary
      cases (template §4.3 checklist).
- [x] `docs/specs/c-naming.md` and `docs/specs/render-tree-dump.md`
      (repo-facing spec docs mirroring markdown-core's spec set).
- [x] C conformance runner consumes the fixtures; goldens are byte-exact.
- [x] `scripts/check-render-tree-fixtures.mjs` (manifest ↔ files ↔ schema
      consistency), wired into `check:contracts`.
- [x] `.gitattributes` entries pinning fixture text encoding (done at
      bootstrap; verified paths; the deliberately invalid-UTF-8 error input
      is exempted from text handling).

Acceptance:

- [x] `ctest --preset conformance` runs every manifest fixture and passes.
- [x] Deliberately corrupting one golden fails conformance; deliberately
      removing a fixture from the manifest fails `check:contracts`.
- [ ] Spec review sign-off recorded in the PR (schema is now versioned; §5.5
      protocol active).

### Phase 4 — CI phase A (C-only) and control plane in evaluate

Tasks:

- [ ] `.github/workflows/ci.yml` per template §5: triggers
      `workflow_call`/`pull_request`/push-main/`merge_group`/`workflow_dispatch`;
      event+PR concurrency lanes; Health Check (Repository, C), Build (4-way
      C matrix), Build Test (test trees + sanitizer trees + link consumer +
      package-content audit of the C archive), Test (correctness, conformance,
      sanitizers, benchmark), barriers, `Required gates` /
      `Development branch gates` naming rule.
- [ ] `.github/workflows/codeql.yml`: `Security Scan - C and C++` manual
      product-only build + `CodeQL gate`.
- [ ] `scripts/audit-ci-policy.sh` (template §13 checks that apply now),
      `scripts/audit-test-topology.sh` skeleton, artifact-manifest
      producer/validator scripts (template §5.2.3), C consumer scripts
      (`build-c-test-artifact.sh`, `run-c-test-artifact.sh`).
- [ ] `.github/rulesets/{main,release-tags}.json`,
      `.github/environments/{release,release-tag-policy}.json` recipes;
      `scripts/bootstrap-repository.sh` copied from template §10.2.
- [ ] Run bootstrap with `RULESET_ENFORCEMENT=evaluate`
      (`GH_REPO=nouprax/tex-core`, `RELEASE_REVIEWER=DongyuZhao`).

Acceptance (template §12.2 subset):

- [ ] Test PR produces `Required gates` only from the PR run; push to its
      branch produces no push CI; merge produces `Development branch gates`.
- [ ] Killing/cancelling any leaf turns the stable gate red (fail-closed
      probe).
- [ ] `CodeQL gate` green only after the C analysis succeeds; tracer sees a
      real compile.
- [ ] `pnpm audit:ci` passes locally and in Health Check.

### Phase 5 — Swift binding

Tasks: everything in §6.2 (root `Package.swift`, module, value tree +
visitors, plugin-generated conformance resources, consumer project,
benchmarks, format/lint wiring), plus root pnpm lanes
`build:swift`, `test:swift-macos`, `test:swift-ios-simulator`,
`conformance:swift-*`, `benchmark:swift-macos`, and CI extension: Swift health
check, `Build - Swift` + deployment-target producers, Build Test producer
(macOS/iOS test products via generic simulator destination), Test consumers
(simctl-discovered runtimes; template §5.2 simulator rules), CodeQL Swift
lane.

Acceptance:

- [ ] All Swift lanes green locally and in CI; consumer builds from a staged
      source archive with no workspace fallback.
- [ ] Swift dump of every shared fixture is byte-identical to the C golden.
- [ ] `swift build` from a clean checkout needs no credentials or prebuilt
      natives.

### Phase 6 — Kotlin/KMP binding

Tasks: everything in §6.3 (wrappers, catalog, KMP targets, android-runtime,
JNI strategy, locking + verification metadata, ktlint, managed devices,
consumers, model smoke, `mvnw`), root lanes (`test:kotlin-*`,
`conformance:kotlin-*`, `benchmark:kotlin-jvm`, `check:gradle-model`,
`check:kotlin-consumers`, `clean:kotlin-android-emulator`), scripts
(`gradle.sh`, emulator artifact producer/consumer scripts), and CI extension:
Kotlin health check, Linux/macOS publication producers, host-test producers,
JVM/Android-host/Native test consumers, x86_64 instrumentation APK producer +
`{4 KB,16 KB} × {correctness,conformance}` emulator consumers (KVM + image +
ABI verification, stacktrace on; per template §14.15: emulator and system
image restored from an explicit `-r<N>` cache key with restore/save split and
the save right after download, bounded adb/boot/instrumentation phases, one
bounded retry with a fresh AVD per attempt, job timeout budgeted for two full
attempts, SIGTERM → bounded probes → SIGKILL teardown that never blocks on
reaping, evidence upload on `failure() || cancelled()`), CodeQL Java/Kotlin
manual build with `--no-build-cache --rerun-tasks`.

Acceptance:

- [ ] All Kotlin lanes green on both hosts; emulator suites green in CI.
- [ ] Template §14.15 stability probes pass: a warm sibling leg hits the
      image cache without downloading; a wedged instrumentation or teardown
      fails within its phase bound (not the job timeout) and still uploads
      evidence on the cancelled path; a transient first-attempt failure is
      absorbed by the fresh-AVD retry while a real regression fails both
      attempts.
- [ ] Kotlin dump of every shared fixture matches the C goldens on every
      target.
- [ ] `publishKotlinToMavenLocal` + all four staged consumers pass;
      IDE clean-import contract (§6.3) verified once and recorded.
- [ ] `--warning-mode=fail` cache-cold matrix pass recorded.

### Phase 7 — ES/WASM binding

Tasks: everything in §6.4 (emsdk pin + install component, build script, dist
layout, types, node/browser test runners, conformance bundler, benchmark,
packed-tarball consumer), root lanes (`test:es-node`, `test:es-browser`,
`conformance:es-node`, `benchmark:es-node`), CI extension: ES health check,
package producer, test-bundle producer, Node correctness/conformance, browser
correctness, Node benchmark, CodeQL JS/TS (`none` build mode).

Acceptance:

- [ ] All ES lanes green; browser suite runs a real browser in CI.
- [ ] ES dump of every shared fixture matches the C goldens.
- [ ] Consumer installs the packed tarball in a temp project and renders;
      `exports` map blocks deep imports; WASM asset resolves via the export.

### Phase 8 — CI phase B completion and audits

Tasks:

- [ ] Benchmarks for all four platforms as required Test leaves feeding
      `Benchmarks - Ready`; metrics JSON upload `continue-on-error`.
- [ ] `scripts/audit-package-contents.sh` (recursive allowlist/denylist over
      C archive, Swift source archive, npm tarball, every Maven
      publication), `audit-public-surface.sh`, `audit-maven-publications.mjs`,
      completed `audit-test-topology.sh`; all wired into `verify` and Health
      Check.
- [ ] Optional-but-planned: `pr-metrics-comment.yml` (fork-safe §6.1
      template rules) and `dependency-submission.yml` (product-configuration
      regexes derived from this repo's Gradle inventory; automatic submission
      disabled first).
- [ ] `docs/releases/` directory convention documented; `CHANGELOG.md`
      discipline enforced by `release:check-version`.

Acceptance:

- [ ] Full template §12.2 checklist executed and recorded in the PR.
- [ ] `pnpm verify` green from a clean checkout on macOS and Linux.

### Phase 9 — Release engineering

Tasks:

- [ ] `scripts/check-release-version.mjs` (§4 drift matrix),
      `scripts/stage-maven-publications.sh`,
      `scripts/merge-maven-publications.mjs`, `scripts/central-portal.sh`,
      `scripts/release-dry-run.sh`, checksum/signing helpers.
- [ ] `.github/workflows/release-dry-run.yml` per §11 (secret-free, disposable
      key, full artifact graph, `Release Dry Run - Ready`).
- [ ] `.github/workflows/release.yml` per §11 (validate → reusable CI →
      Build Release → Assemble → barrier → ordered publish → attestations →
      GitHub Release; `Resume Release` recovery job).
- [ ] `docs/releasing.md` for this repo (§11.1 table, rotation/leak runbooks,
      publication order, verification commands).
- [ ] Extend `audit-ci-policy.sh` with the release rules of template §13.

Acceptance:

- [ ] Dry run green on a PR; downloaded artifacts independently re-audited
      (signatures verify against the disposable key; checksums match;
      product-only contents).
- [ ] Dry run provably reads no secrets (empty-env assertion job).
- [ ] Release workflow rejects a non-`v$(VERSION)` tag and a tag off `main`
      in rehearsal (evaluate-mode push of a scratch tag on a fork/test repo,
      or dispatch-free static assertion via audit script).

### Phase 10 — Control plane active and registry setup

Tasks:

- [ ] Execute §11.1 registry table: Central token, PGP secrets, key-server
      re-verification, npm bootstrap publish + trusted publisher + session
      revocation (`npm whoami` → `ENEEDAUTH`).
- [ ] `scripts/bootstrap-repository.sh` with `RULESET_ENFORCEMENT=active`;
      confirm live policy with the template §12.4 queries; update checked-in
      recipe JSON to match.
- [ ] Template §12.3 rehearsal: force-push/bypass/stale-branch probes, tag
      immutability, environment single-policy check, non-SemVer tag rejection.

Acceptance:

- [ ] Every §12.3 checkbox recorded; secrets exist only in the `release`
      environment; `gh secret list` shows the four Maven names and nothing
      else.

### Phase 11 — First coordinated release `v1.0.0`

Tasks:

- [ ] Bump `VERSION` to `1.0.0`; sweep manifests/fixtures/examples;
      `CHANGELOG.md` section; curated `docs/releases/1.0.0.md`.
- [ ] Full release-preparation checklist from `docs/releasing.md`
      (cache-cold matrix, IDE import, dry run, registry review).
- [ ] Signed protected tag `v1.0.0`; reviewer approval at the `release`
      environment; monitor the publication order.
- [ ] Post-release verification: npm provenance, Central coordinates +
      `.asc` against the published key, GitHub attestations +
      `SHA256SUMS`/`SHA512SUMS`, SwiftPM resolution of `from: "1.0.0"`,
      all four consumers against the public registries.

Acceptance: all four channels serve `1.0.0` from the same tag commit; the
verification evidence is recorded in `docs/releases/1.0.0.md`'s PR.

## 13. Release ladder and product milestones (parallel track)

Majors are capability rungs. Engine depth grows on a milestone track parallel
to the infrastructure phases; every milestone ships cross-platform in one
reviewed change-set per §5.5, with fixtures first.

| Release | Capability | Gated by |
| --- | --- | --- |
| `1.0.0` | Full LaTeX rendering of self-contained input — a single file or single block; math, chemistry, physics, biology and other domain notations as subsets of one LaTeX surface; no programmable TeX layer, reference resolution, modules, multi-file input, or pages (§5.1) | M1–M4 + Phases 1–10 |
| `2.0.0` | Incremental rendering, markdown-core v2 style: the consumer mutates the input and receives the updated render tree as fast as possible — sessions, immutable structurally-shared snapshots, deltas, damage-proportional commit cost | M6 |
| `3.0.0` | Full TeXbook support, product-ready as the core of a TeX project editor: the programmable TeX layer, modules, multi-file projects, references, page building | M7–M8 |

| Milestone | Scope | Gate |
| --- | --- | --- |
| M1 | Math core: ordinary atoms, TeX math classes (ord/op/bin/rel/open/close/punct/inner) with correct inter-atom spacing, superscripts/subscripts, fractions (`\frac`/`\dfrac`/`\tfrac`/`\binom`), radicals, `\left`/`\right` + explicit sizes, function names with limits placement, big operators, accents, Greek/arrow/relation symbol sets, style switches (`\mathbf` … `\mathbb`, `\text`), explicit spacing; complete default-face metrics tables; allocation-failure + fuzz clean | blocks `1.0.0` |
| M2 | Math environments and extensibility: `matrix` family, `cases`, `aligned`; stretchy delimiters and extensible recipes; `\operatorname`, `\overset`/`\underset`, over/under braces; `\tag` numbering | blocks `1.0.0` |
| M3 | Text-mode galley: paragraphs with Knuth–Plass line breaking, font/style commands, sectioning headings, lists, `tabular`, `verbatim`, quotes, document skeleton (`\documentclass` subset, `\begin{document}`, title block) rendered as one continuous galley | blocks `1.0.0` |
| M4 | Domain notation coverage as built-in commands: chemistry (`\ce`, mhchem subset), physics (units/braket subsets), extended math alphabets, symbol-coverage sweep with a pinned coverage inventory tied to fixtures | blocks `1.0.0` |
| M5 | Error-tolerant mode (structured error boxes in-tree, KaTeX-style opt-in) | 1.x minor, post-`1.0.0` |
| M6 | Incremental sessions: spec first (sessions-and-deltas style), session API on all platforms, dump-equality with a from-scratch compile under replay/random-edit/coverage-guided fuzzing, damage-proportional cost gates | blocks `2.0.0` |
| M7 | The programmable TeX layer: `\def`/`\newcommand`, expansion engine, conditionals, registers, catcode changes, `\halign` core | blocks `3.0.0` |
| M8 | Project features: multi-file input (`\input`/`\include`), package/module model, reference and citation resolution (`\label`/`\ref`/`\cite` with aux-equivalent fixpoint), counters, page building and output routines; editor-product contract (stable node identity, complete source mapping) | blocks `3.0.0` |

## 14. Definition of done

Template §15 applies verbatim, instantiated for this repository: four
platforms with correctness/conformance/consumer evidence from the same
commit; `Required gates` + `CodeQL gate` as the only required contexts on an
active ruleset; secret-free dry run; tag-snapshot self-proving release;
npm/Central/SwiftPM/GitHub all traceable to one `v1.0.0` tag; bootstrap
idempotent; audits guarding drift.
