#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

clean=false
physical=false
case "${1:-}" in
    "") ;;
    --clean) clean=true ;;
    --physical)
        clean=true
        physical=true
        ;;
    *)
        echo "Usage: $0 [--clean|--physical]" >&2
        exit 2
        ;;
esac

fail() {
    echo "Repository audit failed: $1" >&2
    exit 1
}

if [ "$clean" = true ]; then
    status=$(git status --porcelain --untracked-files=all)
    if [ -n "$status" ]; then
        printf '%s\n' "$status" >&2
        fail "the source snapshot is not a clean checkout"
    fi
fi

# The SwiftPM manifest name is fixed by the naming family (plan §3): the
# package identity comes from the repository URL, the manifest name from the
# Swift package directory.
for manifest in Package.swift packages/swift-tex-core/Package.release.swift; do
    if ! grep -Fq 'name: "swift-tex-core"' "$manifest"; then
        fail "$manifest must declare the manifest name swift-tex-core"
    fi
done

# The committed version header must match the canonical VERSION file; the
# header is hand-maintained because SwiftPM (and later bindings) compile it
# without a generation step.
version=$(cat VERSION)
header_version=$(sed -n 's/^#define TEX_CORE_VERSION_STRING "\(.*\)"$/\1/p' \
    packages/tex-core/include/tex-core-version.h)
if [ "$version" != "$header_version" ]; then
    fail "tex-core-version.h ($header_version) drifted from VERSION ($version)"
fi

tracked_secret_paths=$(git ls-files | awk '
    /(^|\/)\.env($|\.)/ && $0 !~ /\.env\.example$/ { print }
    /\.(jks|keystore|p12|pem)$/ { print }
')
if [ -n "$tracked_secret_paths" ]; then
    printf '%s\n' "$tracked_secret_paths" >&2
    fail "credential-shaped files are tracked"
fi

secret_pattern='-----BEGIN (RSA |EC |DSA |OPENSSH |PGP )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{36,}|npm_[A-Za-z0-9]{36,}|AKIA[0-9A-Z]{16}'
if secret_matches=$(git grep -Il -E -e "$secret_pattern" -- .); then
    printf '%s\n' "$secret_matches" >&2
    fail "high-confidence credential material is tracked"
fi

large_file_report=$(mktemp)
trap 'rm -f "$large_file_report"' EXIT
git ls-files | while IFS= read -r path; do
    [ -f "$path" ] || continue
    size=$(wc -c <"$path" | tr -d ' ')
    if [ "$size" -gt 5242880 ]; then
        printf '%s (%s bytes)\n' "$path" "$size"
    fi
done >"$large_file_report"
large_files=$(cat "$large_file_report")
if [ -n "$large_files" ]; then
    printf '%s\n' "$large_files" >&2
    fail "tracked files exceed the reviewed 5 MiB limit"
fi

git ls-files -s | awk '$1 == "120000" { print $4 }' | while IFS= read -r link; do
    [ -L "$link" ] || fail "tracked symlink is missing: $link"
    target=$(readlink "$link")
    case "$target" in
        /*) fail "tracked symlink is absolute: $link" ;;
    esac
    [ -e "$link" ] || fail "tracked symlink is broken: $link -> $target"
done

git ls-files scripts | while IFS= read -r path; do
    [ -f "$path" ] || continue
    if [ "$(head -c 2 "$path")" = '#!' ]; then
        mode=$(git ls-files -s -- "$path" | awk 'NR == 1 { print $1 }')
        [ "$mode" = "100755" ] || fail "script with a shebang is not executable: $path"
    fi
done

git ls-files | while IFS= read -r path; do
    [ -f "$path" ] && [ -s "$path" ] || continue
    # Render-tree fixture inputs are byte-exact contract data; boundary
    # coverage includes sources that deliberately end without a newline.
    # The .tree goldens stay under the check: the canonical dump is
    # newline-terminated.
    case "$path" in
        specs/render-tree/*.tex) continue ;;
    esac
    if LC_ALL=C grep -Iq -- . "$path"; then
        last_byte=$(tail -c 1 "$path" | od -An -t u1 | tr -d ' ')
        [ "$last_byte" = "10" ] || fail "text file lacks a final newline: $path"
    fi
done

# The lockfile must resolve every package from the public npm registry.
# Machine-level registry mirrors (corporate feeds, proxies) otherwise leak
# environment-specific tarball URLs into pnpm-lock.yaml, and a fresh
# contributor or CI host cannot install from them. pnpm omits the tarball
# field entirely for the default registry, so any tarball host that is not
# registry.npmjs.org is a leak.
if [ -f pnpm-lock.yaml ]; then
    foreign_tarballs=$(grep -Eo 'tarball: https?://[^/]+' pnpm-lock.yaml \
        | grep -v '//registry\.npmjs\.org$' || true)
    if [ -n "$foreign_tarballs" ]; then
        printf '%s\n' "$foreign_tarballs" | sort -u >&2
        fail "pnpm-lock.yaml resolves packages outside the public npm registry"
    fi
fi

# TeX Core is a from-scratch implementation (plan decision D1): LICENSE is the
# only required attribution file. If an upstream is ever imported, COPYING and
# UPSTREAM.md become required here, per markdown-core practice.
[ -f LICENSE ] || fail "required attribution file is missing: LICENSE"

# Naming-family checks (plan section 3) activate as their packages land.
if [ -f packages/es-tex-core/LICENSE ]; then
    cmp LICENSE packages/es-tex-core/LICENSE >/dev/null \
        || fail "the npm package license differs from the repository license"
fi
if [ -f packages/kotlin-tex-core/build.gradle.kts ]; then
    grep -q '^group = "com.nouprax"$' packages/kotlin-tex-core/build.gradle.kts \
        || fail "the Kotlin Maven group changed"
fi
if [ -f packages/kotlin-tex-core/android-runtime/build.gradle.kts ]; then
    grep -q 'artifactId = "kotlin-tex-core-android-runtime"' \
        packages/kotlin-tex-core/android-runtime/build.gradle.kts \
        || fail "the Android runtime coordinate changed"
fi
if [ -f packages/es-tex-core/package.json ]; then
    grep -q '"name": "@nouprax/es-tex-core"' packages/es-tex-core/package.json \
        || fail "the npm package coordinate changed"
fi
if [ -f Package.swift ]; then
    # This is the manifest `name:` field, not the SwiftPM identity: consumers
    # resolve the identity `tex-core` from the repository URL, while the
    # manifest name matches the package directory (plan section 3,
    # markdown-core convention).
    grep -q 'name: "swift-tex-core"' Package.swift \
        || fail "the Swift package manifest name changed"
fi
if [ -f packages/tex-core/include/tex_core.h ]; then
    grep -q 'tex_core_document_compile' packages/tex-core/include/tex_core.h \
        || fail "the C entry point changed"
fi

if [ "$physical" = true ]; then
    generated=$(find . -type d \
        \( -name .build -o -name .cxx -o -name .gradle \
        -o -name .kotlin -o -name .pnpm-store -o -name .swiftpm -o -name .tools \
        -o -name .vscode -o -name build -o -name DerivedData -o -name dist \
        -o -name node_modules -o -name target \) \
        -not -path './.git/*' -prune -print | sort)
    if [ -n "$generated" ]; then
        printf '%s\n' "$generated" >&2
        fail "generated, cache, dependency, or IDE directories remain"
    fi

    idea_residue=$(
        {
            git ls-files --others --exclude-standard -- .idea
            git ls-files --others --ignored --exclude-standard -- .idea
        } | sort -u
    )
    if [ -n "$idea_residue" ]; then
        printf '%s\n' "$idea_residue" >&2
        fail "untracked or ignored IDE state remains"
    fi

    empty_dirs=$(find . -type d -empty -not -path './.git/*' -print | sort)
    if [ -n "$empty_dirs" ]; then
        printf '%s\n' "$empty_dirs" >&2
        fail "empty directories remain in the physical checkout"
    fi
fi

if [ "$physical" = true ]; then
    echo "Repository audit passed (physical checkout)"
else
    echo "Repository audit passed"
fi
