#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
destination=${1:-}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
archive="$temporary/tex-core.zip"
unpacked="$temporary/unpacked"
package="$unpacked/tex-core"
consumer="$temporary/consumer"

cd "$root"
mkdir -p \
    "$package/packages/tex-core" \
    "$package/packages/swift-tex-core/Sources"
cp packages/swift-tex-core/Package.release.swift "$package/Package.swift"
cp LICENSE README.md VERSION "$package/"
cp -R packages/tex-core/core "$package/packages/tex-core/core"
cp -R packages/tex-core/include "$package/packages/tex-core/include"
cp -R packages/swift-tex-core/Sources/TexCore \
    "$package/packages/swift-tex-core/Sources/TexCore"

# Byte-reproducible zip: the release commit's timestamp on every entry, no
# platform extra fields, and sorted member order, so two stagings of one
# commit produce one digest.
SOURCE_DATE_EPOCH=$(git -C "$root" log -1 --format=%ct 2>/dev/null || echo 0)
epoch_touch=$(date -u -r "$SOURCE_DATE_EPOCH" +%Y%m%d%H%M.%S 2>/dev/null ||
    date -u -d "@$SOURCE_DATE_EPOCH" +%Y%m%d%H%M.%S)
find "$unpacked" -exec touch -t "$epoch_touch" {} +
(cd "$unpacked" && find tex-core | LC_ALL=C sort | TZ=UTC zip -qX "$archive" -@)

for required in \
    Package.swift \
    LICENSE \
    README.md \
    VERSION \
    packages/tex-core/include/tex_core.h \
    packages/tex-core/include/tex-core-export.h \
    packages/tex-core/include/tex-core-version.h \
    packages/swift-tex-core/Sources/TexCore/Document.swift; do
    [ -f "$package/$required" ] || { echo "Swift source archive is missing $required" >&2; exit 1; }
done

if find "$package" -type d \
    \( -iname test -o -iname tests -o -iname benchmarks -o -iname fixtures \
    -o -name .build -o -name build -o -name dist -o -name node_modules \) \
    -print | grep -q .; then
    echo "Swift source archive contains test, benchmark, fixture, build, or dependency content" >&2
    exit 1
fi

if unzip -Z1 "$archive" | grep -E -i \
    '(^|/)(Tests?|Benchmarks?|Fixtures?|Plugins?|Tools?)(/|$)|(^|/)specs(/|$)|\.tree$|\.tex$'; then
    echo "Swift release archive contains non-product source" >&2
    exit 1
fi

CLANG_MODULE_CACHE_PATH="$temporary/product-module-cache" \
    swift build --disable-sandbox --package-path "$package" --target TexCore

mkdir -p "$consumer/Sources/Consumer"
printf '%s\n' \
    '// swift-tools-version: 6.0' \
    'import PackageDescription' \
    '' \
    'let package = Package(' \
    '    name: "ReleaseConsumer",' \
    '    platforms: [.macOS(.v15)],' \
    '    dependencies: [.package(path: "../unpacked/tex-core")],' \
    '    targets: [' \
    '        .executableTarget(' \
    '            name: "Consumer",' \
    '            dependencies: [.product(name: "TexCore", package: "tex-core")]' \
    '        )' \
    '    ]' \
    ')' >"$consumer/Package.swift"
printf '%s\n' \
    'import TexCore' \
    '' \
    'let tree = try Document.compile("x", options: CompileOptions(mode: .mathInline))' \
    'guard case .glyph = tree.root.children[0] else { fatalError("compile failed") }' \
    'print(tree.dump())' >"$consumer/Sources/Consumer/main.swift"

CLANG_MODULE_CACHE_PATH="$temporary/consumer-module-cache" \
    swift run --disable-sandbox --package-path "$consumer" Consumer >/dev/null
# The compiler's index store mirrors SDK header names (Foundation ships
# NSScriptWhoseTests.h, for example), so exclude it: this gate checks whether
# this repository's test and benchmark content reached the product build.
if find "$consumer/.build" -type f \
    -not -path '*/index/*' \
    \( -iname '*test*' -o -iname '*benchmark*' \
    -o -name 'render-tree-fixtures.json' -o -name manifest.json -o -name '*.tree' \) -print | grep -q .; then
    echo "product-only Swift consumer built or carried test or benchmark content" >&2
    exit 1
fi

if [ -n "$destination" ]; then
    mkdir -p "$destination"
    cp "$archive" "$destination/tex-core-source-$(cat "$root/VERSION").zip"
fi

echo "Product-only Swift source archive and external consumer passed."
