# kotlin-tex-core

The Kotlin Multiplatform binding of TeX Core: `com.nouprax:kotlin-tex-core`
(Android min API 21, JVM 17, `macosArm64`, `linuxX64`), namespace
`com.nouprax.tex.core`, version from the root `VERSION`. The public entry
point is `Document.compile` → an immutable `RenderTree` value with the
exhaustive `RenderVisitor`; `RenderTree.dump()` reproduces the C canonical
dump byte for byte.

- `src/native` — the TXC1 wire bridge over the public C facade: one
  `tex_core_kotlin_compile` call serializes a compile outcome (status
  record or preorder tree) into an in-process payload. JNI serves JVM and
  Android; the same bridge compiles as a static archive for Kotlin/Native
  cinterop. The payload is transport, never a public format, and no
  production path consumes dump text.
- `src/commonMain` — the value tree (`RenderTree`, `HBox`/`Glyph`/`Kern`/`Rule`),
  `CompileOptions`/`CompileException`, the wire decoder, and the canonical
  dumper (Knuth `print_scaled` over recovered scaled points).
- `src/commonTest` — API/concurrency suites plus the conformance replay of
  `specs/render-tree/manifest.json`; the case data is generated at build
  time from the manifest (inputs as hex bytes — one case is deliberately
  invalid UTF-8), never a checked-in copy. Conformance is filtered by
  `*ConformanceTest*` into its own test runs on every target.
- Published Javadocs are generated from `commonMain` with Dokka; the
  checked-in `jvm-abi.txt` freezes Java-visible classes, hierarchy, and
  members. Native packaging verifies each ELF/Mach-O payload's actual
  architecture against the closed macOS-arm64/Linux-x64 host model.
- `android-runtime` — publishes
  `com.nouprax:kotlin-tex-core-android-runtime`, the JNI payload AAR for
  all four Android ABIs; the KMP Android publication depends on it.
- `consumers/` — KMP, JVM Gradle, Android, and Maven-wrapper projects
  resolving from a staged local repository only
  (`scripts/check-kotlin-consumers.sh`).

Gradle Managed Devices cover API 36 Pixel profiles at both 4 KB and 16 KB
page sizes (`texCoreApi36Page4k`/`texCoreApi36Page16k`). A clean Android
Studio / IntelliJ import needs no credentials and no prebuilt natives; the
headless model smoke is `scripts/gradle-model-smoke.sh`.
