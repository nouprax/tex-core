# Development environment

This is the single environment entry point for contributors and release
maintainers. Repository commands remain native to CMake, SwiftPM, Gradle, and
pnpm; the bootstrap script checks or installs prerequisites without replacing
those build systems.

```sh
scripts/init-environment.sh --check
scripts/init-environment.sh --install
```

`--check` is read-only and never downloads, installs, accepts licenses, or
reads credentials. `--install` is non-interactive and idempotent. It
bootstraps only repository-managed tools (`.tools/`), JavaScript
dependencies, declared Android SDK packages, and the pinned Emscripten SDK.
It may use Homebrew or `apt-get` for missing basic build tools and the JDK,
but it never installs Xcode, Android command line tools, Gradle, or Maven.

Both modes accept components, which is how CI checks only the tools prepared
by each job's official setup actions:

```sh
scripts/init-environment.sh --check core node
scripts/init-environment.sh --check android android-emulator
scripts/init-environment.sh --check swift
scripts/init-environment.sh --check emscripten
```

Components whose repository surface has not landed yet report themselves as
deferred and pass: `wrappers`, `android`, and `android-emulator` activate
when Phase 6 lands the Kotlin package, `swift` when Phase 5 lands
`Package.swift`, and `emscripten` when Phase 7 lands the ES package
(phases per [`docs/specs/2026-07-20-repo-setup.md`](specs/2026-07-20-repo-setup.md)).

## Required on every development host (Phase 1 onward)

| Dependency | Required version | Source and verification |
| --- | --- | --- |
| Git | current supported release | system package; `git --version` |
| C/C++ compiler | C11-capable Clang or GCC | Xcode Command Line Tools or system package; `cc --version` |
| CMake | 3.20 or later | system package; `cmake --version` |
| pkg-config | current supported release | system package; `pkg-config --version` |
| Node.js | 26.5.0 | `.node-version` and `package.json`; `node --version` |
| pnpm | 11.7.0 | `packageManager` in `package.json`; run via `npx -y pnpm@11.7.0` — no global pnpm is assumed |
| zip/unzip | system version | release and consumer packaging |

Later phases add their platform prerequisites exactly as markdown-core
documents them: Xcode 26.6 (Phase 5); JDK 26, the Android SDK packages fixed
in `scripts/init-environment.sh`, and the repo-owned Gradle/Maven wrappers
(Phase 6); Emscripten 4.0.23 via the pinned emsdk (Phase 7). This document
gains the corresponding sections when those phases land.

## Repository-managed tools

`--install tools` installs ignored, reproducible copies under `.tools/`:

- clang-format 22.1.8 in a Python venv;
- cmake-format 0.6.13 with PyYAML 6.0.3 in a Python venv;
- SwiftLint 0.65.0 from a checksum-verified upstream archive (from Phase 5).

Prettier 3.9.5, ESLint 10.0.1, TypeScript 6.0.3, and typescript-eslint 8.63.0
come from the frozen pnpm lockfile. `--install dependencies` runs only
`npx --yes pnpm@11.7.0 install --frozen-lockfile`.

## Script lanes and phase gating

`package.json` carries the complete script namespace from Phase 1 so command
names never churn. Lanes whose phase has not landed fail loudly through
`scripts/pending.sh`, naming the phase that delivers them. Formatter and lint
lanes for languages whose sources have not landed succeed as explicit no-ops,
so the aggregates are stable at every phase:

```sh
pnpm format:check   # c + cmake + swift + es (kotlin joins in Phase 6)
pnpm lint           # c + swift + es (kotlin joins in Phase 6)
pnpm verify         # format:check + lint + audit:repository, growing per phase
```

## Clean bootstrap and validation

For the release-quality path, begin with a clean checkout and no ignored
outputs:

```sh
git clean -fdX
scripts/audit-repository.sh --physical
scripts/init-environment.sh --install
scripts/init-environment.sh --check
pnpm verify
```

The install step never reads Maven Central, npm, GitHub release, signing, or
PGP credentials. Publishing remains isolated to the protected `release`
environment and the tag-driven release workflow that Phase 9 introduces.
