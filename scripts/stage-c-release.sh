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

rm -rf "$output"
mkdir -p "$output"
cmake -S "$root" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DTEX_CORE_TESTS=OFF \
    -DTEX_CORE_STATIC=ON \
    -DTEX_CORE_SHARED=ON
cmake --build "$build" --config Release --parallel 2
cmake --install "$build" --config Release

find "$prefix" \( -type f -o -type l \) | while IFS= read -r artifact; do
    relative=${artifact#"$prefix/"}
    case "$relative" in
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
        lib/pkgconfig/tex-core.pc)
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

cmake -S "$root/packages/tex-core/tests/consumers/cmake" \
    -B "$temporary/consumer-build" \
    -DCMAKE_PREFIX_PATH="$prefix" >/dev/null
cmake --build "$temporary/consumer-build" --parallel 2 >/dev/null
DYLD_LIBRARY_PATH="$prefix/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
    LD_LIBRARY_PATH="$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$temporary/consumer-build/tex-core-installed-consumer"

if command -v pkg-config >/dev/null 2>&1; then
    read -r -a pkg_config_flags <<<"$(
        PKG_CONFIG_PATH="$prefix/lib/pkgconfig" \
            pkg-config --cflags --libs tex-core
    )"
    PKG_CONFIG_PATH="$prefix/lib/pkgconfig" \
        cc "$root/packages/tex-core/tests/consumers/c/main.c" \
        -o "$temporary/pkg-config-consumer" \
        "${pkg_config_flags[@]}"
    DYLD_LIBRARY_PATH="$prefix/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
        LD_LIBRARY_PATH="$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$temporary/pkg-config-consumer"
fi

tar -czf "$output/$name.tar.gz" -C "$temporary" "$name"
echo "Staged $output/$name.tar.gz"
