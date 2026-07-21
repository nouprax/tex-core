# Toolchains and IDE Support

This document records the tool versions selected for the TeX Core monorepo.
It is the single source of truth required by plan §8
([`docs/specs/2026-07-20-repo-setup.md`](specs/2026-07-20-repo-setup.md));
upgrades must be reviewed together with lockfiles, verification metadata,
formatter output, and the full validation matrix once those exist. The matrix
was adopted verbatim from markdown-core at bootstrap.

GitHub Action revisions are repository-maintenance choices rather than product
contracts. Review runtime deprecation warnings when updating workflows, but do
not encode current or obsolete Action majors as CI allowlists/denylists.

## Version matrix

The **Since** column names the phase that first exercises the row; earlier
phases pin the version without using it.

| Area | Version | Since | Policy |
| --- | --- | --- | --- |
| Node.js | 26.5.0 | Phase 1 | Recorded in `.node-version` |
| pnpm | 11.7.0 | Phase 1 | `packageManager` in `package.json`; lockfile committed; invoke as `npx -y pnpm@11.7.0` when no global pnpm exists |
| Prettier | 3.9.5 | Phase 1 | Exact dev dependency |
| ESLint | 10.0.1 | Phase 1 | Exact dev dependency |
| TypeScript | 6.0.3 | Phase 1 | Latest stable supported by typescript-eslint 8.63.0 |
| clang-format | 22.1.8 | Phase 1 | Repository-local venv under `.tools/`; `InsertBraces: true` |
| cmake-format | 0.6.13 | Phase 1 | Repository-local venv under `.tools/` |
| CMake (host) | ≥ 3.20 | Phase 2 | System package |
| Xcode | 26.6 | Phase 5 | Selected explicitly in Swift CI; supplies Swift 6.3.3 and swift-format 6.3.0 |
| SwiftLint | 0.65.0 | Phase 5 | Checksum-verified upstream release into `.tools/` |
| Gradle | 9.6.1 | Phase 6 | Wrapper only; distribution SHA-256 pinned |
| Maven | 3.9.16 | Phase 6 | Repo-owned Maven Wrapper; no global `mvn` |
| Gradle daemon JDK | 26 | Phase 6 | Foojay-provisioned criteria |
| JVM bytecode | 17 | Phase 6 | Explicit Android compile target |
| Kotlin/KMP plugin | 2.4.0 | Phase 6 | Stable release |
| Android Gradle Plugin | 9.3.0 | Phase 6 | Stable release |
| Android SDK | compile 36, target 36, min 21 | Phase 6 | Version catalog + consumer manifests |
| Android NDK / CMake | 28.2.13676358 / 3.22.1 | Phase 6 | Fixed packages in `init-environment.sh` |
| ktlint plugin / CLI | 14.2.0 / 1.8.0 | Phase 6 | Official code style; no experimental rules |
| Emscripten | 4.0.23 | Phase 7 | Pinned emsdk clone under `.tools/emsdk/` |

## Supported IDEs

IDE validation activates with the packages that need it: any editor for
Phases 1–4; Xcode 26.6 from Phase 5; Android Studio 2026.1.2 or IntelliJ IDEA
2026.1 from Phase 6, importing at the repository root with the
repository-provisioned JDK 26 daemon criteria. A clean import must never
require release credentials, signing keys, generated `.idea`/`.iml` files, or
prebuilt native libraries (markdown-core IDE contract; details return with
the Phase 6 Gradle model smoke).

## Common commands

```sh
npx --yes pnpm@11.7.0 install --frozen-lockfile
npx --yes pnpm@11.7.0 run format:check
npx --yes pnpm@11.7.0 run lint
npx --yes pnpm@11.7.0 run audit:repository
scripts/init-environment.sh --check core node dependencies tools
```
