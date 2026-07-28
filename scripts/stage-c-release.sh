#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?usage: stage-c-release.sh OUTPUT_DIRECTORY}
version=$(cat "$root/VERSION")
os=$(uname -s | tr '[:upper:]' '[:lower:]')
arch=$(uname -m)
name="tex-core-c-$version-$os-$arch"
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
prefix="$temporary/$name"
build="$temporary/build"

# The Apple support floor (Package.swift declares macOS 15): without an
# explicit deployment target the toolchain stamps the build host's OS as
# the dylib's minimum, and the artifact refuses to load anywhere older.
MACOS_DEPLOYMENT_TARGET=15.0

rm -rf "$output"
mkdir -p "$output"
configure_flags=()
if [ "$os" = "darwin" ]; then
    configure_flags+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_DEPLOYMENT_TARGET")
fi
cmake -S "$root" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DTEX_CORE_TESTS=OFF \
    -DTEX_CORE_STATIC=ON \
    -DTEX_CORE_SHARED=ON \
    ${configure_flags[@]+"${configure_flags[@]}"}
# Apple's ar embeds member timestamps unless ZERO_AR_DATE is set (GNU ar is
# deterministic by default on release toolchains); without it the static
# archive alone breaks byte-reproducibility.
ZERO_AR_DATE=1 cmake --build "$build" --config Release --parallel 2
cmake --install "$build" --config Release

# Binary redistribution terms: the archive must reproduce the exact
# BSD-2-Clause text.
cp "$root/LICENSE" "$prefix/LICENSE"
cmp -s "$root/LICENSE" "$prefix/LICENSE" || { echo "staged LICENSE is not byte-identical" >&2; exit 1; }

find "$prefix" \( -type f -o -type l \) | while IFS= read -r artifact; do
    relative=${artifact#"$prefix/"}
    case "$relative" in
        LICENSE | \
        bin/tex-core | \
        include/tex_core.h | \
        include/tex-core-export.h | \
        include/tex-core-version.h | \
        lib/cmake/tex-core/tex-core-config.cmake | \
        lib/cmake/tex-core/tex-core-config-version.cmake | \
        lib/cmake/tex-core/tex-core-targets.cmake | \
        lib/cmake/tex-core/tex-core-targets-release.cmake | \
        lib/libtex-core.a | \
        lib/libtex-core*.dylib | \
        lib/libtex-core.so | \
        lib/libtex-core.so.* | \
        lib/pkgconfig/tex-core.pc | \
        lib/pkgconfig/tex-core-static.pc)
            ;;
        *) echo "unexpected C release artifact: $relative" >&2; exit 1 ;;
    esac
done
# Leakage is a file-shape question: no corpus fixtures, manifests, or spec
# directories may install (header comments may reference the spec docs).
if find "$prefix" \( -name '*.tree' -o -name '*.tex' -o -name 'manifest.json' -o -path '*specs*' \) -print | grep -q .; then
    echo "C release artifact contains conformance spec data" >&2
    exit 1
fi

# Deployment-target audit: every staged Mach-O must load on the declared
# minimum macOS, so a toolchain default can never leak into a release.
if [ "$os" = "darwin" ]; then
    find "$prefix" -type f \( -name '*.dylib' -o -path '*/bin/*' \) | while IFS= read -r binary; do
        minos=$(vtool -show-build "$binary" 2>/dev/null | awk '/minos/ { print $2; exit }')
        [ -n "$minos" ] || { echo "vtool reported no minos for $binary" >&2; exit 1; }
        case "$minos" in
            "$MACOS_DEPLOYMENT_TARGET" | "${MACOS_DEPLOYMENT_TARGET%%.*}" | "${MACOS_DEPLOYMENT_TARGET%%.*}".*) ;;
            *)
                echo "$binary requires macOS $minos, above the supported minimum $MACOS_DEPLOYMENT_TARGET" >&2
                exit 1
                ;;
        esac
    done
fi

# Static-archive determinism: even with ZERO_AR_DATE, Apple's ranlib
# stamps the symbol-table member with the current time. Normalize every ar
# member header (mtime 0, uid/gid 0, mode 644) in place; the object bytes
# are untouched.
find "$prefix" -type f -name '*.a' | while IFS= read -r archive; do
    python3 - "$archive" <<'PY'
import sys

path = sys.argv[1]
with open(path, "rb") as handle:
    data = bytearray(handle.read())
assert data[:8] == b"!<arch>\n", "not an ar archive"
offset = 8
while offset + 60 <= len(data):
    size = int(bytes(data[offset + 48:offset + 58]).decode().strip() or "0")
    data[offset + 16:offset + 28] = b"0           "  # mtime
    data[offset + 28:offset + 34] = b"0     "        # uid
    data[offset + 34:offset + 40] = b"0     "        # gid
    data[offset + 40:offset + 48] = b"100644  "      # mode
    offset += 60 + size + (size % 2)
with open(path, "wb") as handle:
    handle.write(data)
PY
done

# Byte-reproducible archive: fixed timestamps (the release commit's), root
# ownership, normalized permissions, sorted member order, and an
# undated gzip stream, so two stagings of one commit produce one digest.
SOURCE_DATE_EPOCH=$(git -C "$root" log -1 --format=%ct 2>/dev/null || echo 0)
export SOURCE_DATE_EPOCH
find "$prefix" -type d -exec chmod 755 {} +
find "$prefix" -type f -perm +111 -exec chmod 755 {} + 2>/dev/null ||
    find "$prefix" -type f -executable -exec chmod 755 {} +
find "$prefix" -type f ! -perm 755 -exec chmod 644 {} +
epoch_touch=$(date -u -r "$SOURCE_DATE_EPOCH" +%Y%m%d%H%M.%S 2>/dev/null ||
    date -u -d "@$SOURCE_DATE_EPOCH" +%Y%m%d%H%M.%S)
find "$prefix" -exec touch -h -t "$epoch_touch" {} + 2>/dev/null || find "$prefix" -exec touch -t "$epoch_touch" {} +
(
    cd "$temporary"
    find "$name" | LC_ALL=C sort >"$temporary/archive-members"
    if tar --version 2>/dev/null | grep -q GNU; then
        tar --no-recursion --numeric-owner --owner=0 --group=0 \
            -T "$temporary/archive-members" -cf "$temporary/$name.tar"
    else
        tar -n --numeric-owner --uid 0 --gid 0 \
            -T "$temporary/archive-members" -cf "$temporary/$name.tar"
    fi
)
gzip -n -9 -c "$temporary/$name.tar" >"$output/$name.tar.gz"

# Consumers must work from the *extracted archive at a new location*, not
# from the staging tree: this is the relocation the shipped bytes actually
# face. The staging prefix is deleted first so a leaked absolute path
# cannot resolve.
rm -rf "$prefix"
extracted="$temporary/extracted"
mkdir -p "$extracted"
tar -xzf "$output/$name.tar.gz" -C "$extracted"
installed="$extracted/$name"

cmp -s "$root/LICENSE" "$installed/LICENSE" || { echo "extracted LICENSE is not byte-identical" >&2; exit 1; }

cmake -S "$root/packages/tex-core/tests/consumers/cmake" \
    -B "$temporary/consumer-build" \
    -DCMAKE_PREFIX_PATH="$installed" >/dev/null
cmake --build "$temporary/consumer-build" --parallel 2 >/dev/null
DYLD_LIBRARY_PATH="$installed/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
    LD_LIBRARY_PATH="$installed/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$temporary/consumer-build/tex-core-installed-consumer"

if command -v pkg-config >/dev/null 2>&1; then
    read -r -a pkg_config_flags <<<"$(
        PKG_CONFIG_PATH="$installed/lib/pkgconfig" \
            pkg-config --cflags --libs tex-core
    )"
    PKG_CONFIG_PATH="$installed/lib/pkgconfig" \
        cc "$root/packages/tex-core/tests/consumers/c/main.c" \
        -o "$temporary/pkg-config-consumer" \
        "${pkg_config_flags[@]}"
    DYLD_LIBRARY_PATH="$installed/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
        LD_LIBRARY_PATH="$installed/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$temporary/pkg-config-consumer"

    # The static contract: tex-core-static.pc must define
    # TEX_CORE_STATIC_DEFINE and the archive must link and run standalone.
    read -r -a static_cflags <<<"$(
        PKG_CONFIG_PATH="$installed/lib/pkgconfig" \
            pkg-config --cflags tex-core-static
    )"
    case " ${static_cflags[*]} " in
        *" -DTEX_CORE_STATIC_DEFINE "*) ;;
        *) echo "tex-core-static.pc does not define TEX_CORE_STATIC_DEFINE" >&2; exit 1 ;;
    esac
    cc "$root/packages/tex-core/tests/consumers/c/main.c" \
        -o "$temporary/static-consumer" \
        "${static_cflags[@]}" "$installed/lib/libtex-core.a"
    "$temporary/static-consumer"
fi

echo "Staged $output/$name.tar.gz"
