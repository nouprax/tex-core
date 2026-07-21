# Changelog

All notable release changes are recorded here. TeX Core follows Semantic
Versioning for source packages and public API behavior; the C binary ABI is
not promised to remain compatible between releases.

## Unreleased

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
