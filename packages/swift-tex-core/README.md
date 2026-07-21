# swift-tex-core

The Swift binding of TeX Core. The public product is `TexCore`
(`Document.compile` → `RenderTree`); `TexCoreC` compiles the C engine
sources directly from `packages/tex-core` and is never public API. The
root `Package.swift` owns the development targets; `Package.release.swift`
is the product-only manifest staged into the release source archive
(`scripts/check-swift-source-archive.sh` proves an external consumer builds
from it).

- `Sources/TexCore` — the immutable `Sendable` value tree (`RenderTree`,
  `HBox`/`Glyph`/`Kern`, `RenderVisitor`), `Document.compile` over the
  three input modes, the structured `CompileError`, and the canonical
  dumper. Trees are detached values: no C pointer or lifetime survives
  `compile`.
- `Tests/TexCoreTests` — API, error, and visitor suites (`swift test`,
  lane `test:swift-macos`).
- `Tests/TexCoreConformanceTests` — replays every case of
  `specs/render-tree/manifest.json` through the public API and compares
  dumps byte for byte with the shared goldens; the corpus is injected by
  the `GenerateRenderTreeResources` build-tool plugin, never copied in.
- `Tests/Consumer` — an external SwiftPM package consuming only the
  public `TexCore` product.
- `Benchmarks/TexCoreBenchmarks` — the informational benchmark
  executable (lane `benchmark:swift-macos`).
