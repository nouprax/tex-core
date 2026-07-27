# Changelog

All notable release changes are recorded here. TeX Core follows Semantic
Versioning for source packages and public API behavior; the C binary ABI is
not promised to remain compatible between releases.

## Unreleased

- Pin the linear-history merge policy in `bootstrap-repository.sh` and
  the template's embedded copy: squash is the only allowed merge method,
  squash commits take the pull request's title (`… (#N)`) and
  description, and the main ruleset recipe now carries the merge queue
  (squash method, ALLGREEN grouping) and the standing admin bypass, each
  behind a variable — so a bootstrap re-run reproduces the live control
  plane instead of stripping it.
- Continue milestone M1 (math core) with superscripts, subscripts, and
  braced groups, moving the render-tree contract to schemaVersion 2:
  `hbox` gains `x`/`y` and nests (a braced group is one Ord atom boxed in
  the surrounding style; script boxes carry their baseline shift in `y`),
  and glyphs carry the em they were set at (10 pt text, 7 pt script, 5 pt
  scriptscript). Script geometry follows TeXbook Appendix G rule 18 over a
  vendored KaTeX sigmasAndXis subset (sup1-3, sub1-2, supDrop/subDrop,
  x-height clearances, the four-rule-thickness clash fixup), with TeX's
  clean_box simplification, `\scriptspace` padding, and the italic
  correction withheld from a subscripted nucleus and offsetting the
  superscript instead. Inter-atom and mu-based explicit spacing now
  resolve against the current size's quad and honor TeX's
  conditionality — medium, thick, and conditional thin pairs vanish
  inside script styles. The script grammar errors are structured and
  pinned (`double superscript`/`double subscript`, `missing … argument`,
  `unclosed group`, `unmatched closing brace`, `group nesting too deep`
  past 255 groups); document mode keeps rejecting `{ } ^ _`. The shared
  corpus grows to 36 cases (nine script/group trees, six new error
  records) with tree-witnessed coverage validators, and all four bindings
  replay it byte-exactly — binding changes are limited to the `HBox`
  value types, dumpers, and schema-line pins; the TXC1 wire format
  already carried the new fields. math-mode atoms now carry TeX's classes
  (Ord, Bin, Rel, Open, Close, Punct), layout resolves contextual Bin
  atoms to Ord exactly as TeX's first mlist pass and inserts the
  TeXbook chapter-18 inter-atom spacing as kern nodes whose source range
  is the gap between the spaced atoms. The math surface grows the ASCII
  math characters (`+ - * / = < > ( ) [ ] , ; : ! ? |`, with `-`, `*`,
  and `|` remapped to their plain TeX glyphs) and about 170 symbol
  commands: the Greek alphabet, binary operators, relations, arrows,
  letter-like and miscellaneous ordinaries, delimiter atoms, and their
  plain TeX aliases. The embedded metrics tables grow to cover the new
  glyphs (181 additional KaTeX Computer Modern rows), the shared corpus
  gains nine reviewed cases (atom classes, Bin contexts, Greek, symbol
  commands, mixed explicit/inter-atom spacing, and document-mode
  rejection records) while the now-supported math-comma error case
  retires, and all four bindings replay the grown corpus byte-exactly
  with no binding code changes. Document mode is unchanged: math
  characters and symbol commands there stay structured errors until the
  M3 text surface.
- Complete the Phase 10 registry setup: the four Maven secrets live in
  the `release` environment, `@nouprax/es-tex-core@0.1.0` is
  bootstrap-published with the npm trusted publisher configured for
  OIDC-only releases, and the §12.3 enforcement-rehearsal evidence
  (push, merge, and stale-branch probes, the bypass audit, and the tag
  policy checks) is recorded in the setup plan.
- Activate the repository control plane: `main quality gates` and
  `release tag protection` rulesets now enforce (previously evaluate),
  the checked-in ruleset recipes and `audit:ci` require `active`, and
  the live policy was re-verified with the template §12.4 queries.
- Land release engineering: the secret-free release dry run (PR-triggered,
  disposable signing key, full artifact graph re-verified by staged
  consumers and the signed Maven audit) and the tag-driven formal release
  workflow (tag-local quality gates through the reusable CI, ordered
  Maven-stage → npm OIDC → Maven-commit → GitHub Release publication with
  build-provenance attestations, and a Resume Release recovery job), plus
  the release staging scripts, `docs/releasing.md`, and the template §13
  release rules in `audit:ci`.
- Complete CI phase B: the package-content audit now covers the npm
  tarball, the SwiftPM manifest shape, the Kotlin publications, the
  installed C export set, and the no-process-global-state contract over
  the static archives; new public-surface and Maven-publication audits
  pin every binding's reviewed API and publication metadata; the test
  topology audit spans all four platforms; benchmark rows upload
  informational PR metrics with a fork-safe comment workflow; and
  release discipline lands (`docs/releases/`, `release:check-version`
  inside `verify` and Health Check).

## 0.1.0 - 2026-07-21

- Land the ES/WASM binding: `@nouprax/es-tex-core`, an ESM-only package
  (Node ≥ 20 and modern browsers) compiling the C engine to standalone
  WASM through the pinned Emscripten toolchain, with `Document.compile`
  over the three input modes, a frozen discriminated-union render-tree
  value API with the exhaustive visitor, structured `CompileError`
  values, and a canonical dumper byte-identical to the C dump; the WASM
  transport reuses the shared TXC1 bridge over the public C facade.
  Conformance replays the shared corpus in Node; correctness runs on Node
  and headless Chrome; the packaging suite proves the packed tarball
  installs into a clean project, blocks deep imports through the
  `exports` map, and resolves the WASM asset through its export. CI grows
  ES health-check, WASM-package producer, test-bundle producer,
  Node/browser/conformance consumers, a Node benchmark row, and a
  JavaScript/TypeScript CodeQL lane.
- Extend CI to the Kotlin binding: health check, Linux/macOS publication
  producers, host-test producers, JVM/Android-host/Native test consumers,
  the build-once/test-many Android instrumentation APK producer with four
  independent emulator consumers ({4 KB, 16 KB} pages × {correctness,
  conformance}) under the template §14.15 stability contract (pinned
  emulator/system-image cache with restore/save split, KVM and ABI
  verification, bounded adb/boot/instrumentation phases, one fresh-AVD
  retry, escalating teardown, evidence upload on failure or
  cancellation), a JVM benchmark row, a Java/Kotlin CodeQL lane, and the
  release publication staging/merge scripts; `audit:ci` now enforces the
  Kotlin producer/consumer topology.
- Land the Kotlin Multiplatform binding: `com.nouprax:kotlin-tex-core`
  (Android min API 21, JVM 17, `macosArm64`, `linuxX64`) with
  `Document.compile` over the three input modes, an immutable render-tree
  value API with the exhaustive `RenderVisitor`, the structured
  `CompileException`, and a canonical dumper byte-identical to the C dump.
  One TXC1 wire bridge over the public C facade serves JNI (JVM desktop
  payload and the `kotlin-tex-core-android-runtime` AAR with all four
  ABIs) and Kotlin/Native cinterop alike; conformance replays the shared
  corpus from build-generated case data on every target, including both
  Gradle Managed Devices (API 36, 4 KB and 16 KB page sizes). Repo-owned
  Gradle and Maven wrappers, dependency locking with verification
  metadata, ktlint, headless Gradle model smoke, and four staged
  consumers (KMP, JVM Gradle, Android, Maven) resolving from a local
  staged repository only.
- Land the Swift binding: the `TexCore` SwiftPM product (iOS 18+/macOS 15+)
  with `Document.compile` over the three input modes, an immutable
  `Sendable` render-tree value API with exhaustive typed visitors, the
  structured `CompileError`, and a canonical dumper that reproduces the C
  dump byte for byte; conformance tests replay the shared corpus through a
  build-tool plugin, an external consumer package builds from the
  product-only release manifest, and CI grows Swift health-check, product,
  deployment-target, test-product, macOS/iOS test, and benchmark rows plus
  a Swift CodeQL lane. The C export and version headers become committed
  files so every build system compiles the same headers.
- Stand up CI phase A (C-only): the fail-closed PR quality-gate DAG
  (`Health Check → Build → Build Test → Test → Required gates`) over a
  four-way C build matrix with sanitizer suites, immutable artifact handoff
  between producer and consumer jobs, a CodeQL C/C++ manual-build scan with
  `CodeQL gate`, the C package-content audit with pkg-config and CMake
  `find_package` link consumers, CI-policy and test-topology self-audits,
  and the checked-in GitHub control-plane recipes plus
  `scripts/bootstrap-repository.sh` (evaluate enforcement first). The one
  exported CMake target is now `tex-core::tex-core`.
- Freeze render-tree schemaVersion 1: the canonical schema and dump
  contracts (`docs/specs/render-tree.md`, `docs/specs/render-tree-dump.md`),
  the shared conformance corpus `specs/render-tree/` (manifest + reviewed
  `.tex`/`.tree` pairs covering every shipped construct and error path), a
  manifest-driven byte-exact C conformance suite, the corpus audit
  `check:contracts`, and the C naming conventions
  (`docs/specs/c-naming.md`). The dump schema line moves from `render-tree
  0` to `render-tree 1`.
- Land the C core walking skeleton: `tex_core_document_compile` over the
  document/math-inline/math-display input forms, an immutable render tree
  (hbox/glyph/kern) with embedded KaTeX-derived Computer Modern metrics,
  byte-deterministic canonical dumps in integer scaled points, structured
  fail-fast errors with source ranges, the `tex-core` CLI, and the full
  CMake/CTest graph (correctness, conformance, benchmark, sanitizer presets,
  libFuzzer harness).
- Bootstrap the repository: adopt the setup plan
  (`docs/specs/2026-07-20-repo-setup.md`), import the repository setup
  template contract, and establish repository identity and hygiene.
