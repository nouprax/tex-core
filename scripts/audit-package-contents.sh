#!/bin/sh
# Installed and published package-content audit: the C archive (both library
# shapes into throwaway prefixes, product-only allowlists, pkg-config and
# CMake find_package link consumers, export and global-state audits), the
# SwiftPM manifest shape, the npm tarball, and the Kotlin publications. The
# Swift source archive has its own consumer proof in
# check-swift-source-archive.sh.
set -eu

temp_dir=$(mktemp -d)
trap 'rm -rf "$temp_dir"' EXIT
export npm_config_cache="${npm_config_cache:-$temp_dir/npm-cache}"

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "pkg-config is required for installed C consumer validation" >&2
    exit 1
fi

cmake -S . -B "$temp_dir/cmake-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DTEX_CORE_TESTS=OFF \
    -DTEX_CORE_STATIC=OFF \
    -DTEX_CORE_SHARED=ON \
    -DCMAKE_INSTALL_PREFIX="$temp_dir/cmake-prefix" >/dev/null
cmake --build "$temp_dir/cmake-build" --parallel 2 >/dev/null
cmake --install "$temp_dir/cmake-build" >/dev/null

cmake -S . -B "$temp_dir/cmake-build-static" \
    -DCMAKE_BUILD_TYPE=Release \
    -DTEX_CORE_TESTS=ON \
    -DTEX_CORE_STATIC=ON \
    -DTEX_CORE_SHARED=OFF \
    -DCMAKE_INSTALL_PREFIX="$temp_dir/cmake-prefix-static" >/dev/null
cmake --build "$temp_dir/cmake-build-static" --parallel 2 >/dev/null
cmake --install "$temp_dir/cmake-build-static" >/dev/null

# The install step must not drag test or spec support into the prefix even
# when the build tree was configured with tests on.
find "$temp_dir/cmake-prefix" "$temp_dir/cmake-prefix-static" \( -type f -o -type l \) |
    while IFS= read -r artifact; do
        case "$artifact" in
            *test* | *Test* | *specs* | *render-tree*)
                echo "Test or spec artifact leaked into the C install prefix: $artifact" >&2
                exit 1
                ;;
        esac
    done

find "$temp_dir/cmake-prefix" \( -type f -o -type l \) | while IFS= read -r artifact; do
    relative_path=${artifact#"$temp_dir/cmake-prefix/"}
    case "$relative_path" in
        bin/tex-core | \
            include/tex_core.h | \
            include/tex-core-export.h | \
            include/tex-core-version.h | \
            lib/cmake/tex-core/tex-core-config.cmake | \
            lib/cmake/tex-core/tex-core-config-version.cmake | \
            lib/cmake/tex-core/tex-core-targets.cmake | \
            lib/cmake/tex-core/tex-core-targets-release.cmake | \
            lib/libtex-core* | \
            lib/pkgconfig/tex-core.pc | \
            lib/pkgconfig/tex-core-static.pc) ;;
        *)
            echo "Unexpected C install artifact: $relative_path" >&2
            exit 1
            ;;
    esac
done

find "$temp_dir/cmake-prefix-static" \( -type f -o -type l \) | while IFS= read -r artifact; do
    relative_path=${artifact#"$temp_dir/cmake-prefix-static/"}
    case "$relative_path" in
        bin/tex-core | \
            include/tex_core.h | \
            include/tex-core-export.h | \
            include/tex-core-version.h | \
            lib/cmake/tex-core/tex-core-config.cmake | \
            lib/cmake/tex-core/tex-core-config-version.cmake | \
            lib/cmake/tex-core/tex-core-targets.cmake | \
            lib/cmake/tex-core/tex-core-targets-release.cmake | \
            lib/libtex-core.a | \
            lib/pkgconfig/tex-core.pc | \
            lib/pkgconfig/tex-core-static.pc) ;;
        *)
            echo "Unexpected static C install artifact: $relative_path" >&2
            exit 1
            ;;
    esac
done

# The one exported CMake target is tex-core::tex-core; internal target
# names must not surface in installed metadata.
for prefix in "$temp_dir/cmake-prefix" "$temp_dir/cmake-prefix-static"; do
    if ! grep -R -I -q 'tex-core::tex-core' "$prefix/lib/cmake/tex-core"; then
        echo "Installed CMake metadata does not export tex-core::tex-core: $prefix" >&2
        exit 1
    fi
    if grep -R -I -n 'tex-core::libtex-core' "$prefix/lib/cmake/tex-core"; then
        echo "Internal library target names leaked into installed CMake metadata" >&2
        exit 1
    fi
done

# pkg-config intentionally returns separate compiler/linker arguments.
# shellcheck disable=SC2046
PKG_CONFIG_PATH="$temp_dir/cmake-prefix/lib/pkgconfig" \
    cc packages/tex-core/tests/consumers/c/main.c \
    -o "$temp_dir/pkg-config-consumer-shared" \
    $(PKG_CONFIG_PATH="$temp_dir/cmake-prefix/lib/pkgconfig" \
        pkg-config --cflags --libs tex-core)
DYLD_LIBRARY_PATH="$temp_dir/cmake-prefix/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
    LD_LIBRARY_PATH="$temp_dir/cmake-prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$temp_dir/pkg-config-consumer-shared"

# shellcheck disable=SC2046
PKG_CONFIG_PATH="$temp_dir/cmake-prefix-static/lib/pkgconfig" \
    cc packages/tex-core/tests/consumers/c/main.c \
    -o "$temp_dir/pkg-config-consumer-static" \
    $(PKG_CONFIG_PATH="$temp_dir/cmake-prefix-static/lib/pkgconfig" \
        pkg-config --static --cflags --libs tex-core)
"$temp_dir/pkg-config-consumer-static"

cmake -S packages/tex-core/tests/consumers/cmake \
    -B "$temp_dir/cmake-consumer-shared" \
    -DCMAKE_PREFIX_PATH="$temp_dir/cmake-prefix" >/dev/null
cmake --build "$temp_dir/cmake-consumer-shared" --parallel 2 >/dev/null
DYLD_LIBRARY_PATH="$temp_dir/cmake-prefix/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" \
    LD_LIBRARY_PATH="$temp_dir/cmake-prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$temp_dir/cmake-consumer-shared/tex-core-installed-consumer"

cmake -S packages/tex-core/tests/consumers/cmake \
    -B "$temp_dir/cmake-consumer-static" \
    -DCMAKE_PREFIX_PATH="$temp_dir/cmake-prefix-static" >/dev/null
cmake --build "$temp_dir/cmake-consumer-static" --parallel 2 >/dev/null
"$temp_dir/cmake-consumer-static/tex-core-installed-consumer"

# The installed shared facade must export exactly the symbols the public
# header declares — nothing internal, nothing extra.
nm_tool=$(command -v llvm-nm || true)
if [ -z "$nm_tool" ] && command -v xcrun >/dev/null 2>&1; then
    nm_tool=$(xcrun -f llvm-nm 2>/dev/null || true)
fi
if [ -z "$nm_tool" ]; then
    nm_tool=$(command -v nm || true)
fi
if [ -z "$nm_tool" ]; then
    echo "No nm implementation is available for the C export audit" >&2
    exit 1
fi
facade_library=$(find "$temp_dir/cmake-prefix/lib" -type f \
    \( -name 'libtex-core.so*' -o -name 'libtex-core.*.dylib' \) | head -n 1)
if [ -z "$facade_library" ]; then
    echo "Installed C facade shared library was not found" >&2
    exit 1
fi
case "$facade_library" in
    *.dylib)
        "$nm_tool" -gU "$facade_library" | awk '{ print $NF }' | sed 's/^_//' ;;
    *)
        "$nm_tool" -D --defined-only "$facade_library" | awk '{ print $NF }' | sed 's/@.*//' ;;
esac | grep '^tex_core_' | sort -u >"$temp_dir/c-actual-exports.txt"
sed -n 's/.*TEX_CORE_API[^(]*[ *]\(tex_core_[a-z0-9_]*\)(.*/\1/p' \
    packages/tex-core/include/tex_core.h | sort -u >"$temp_dir/c-declared-exports.txt"
if ! cmp "$temp_dir/c-declared-exports.txt" "$temp_dir/c-actual-exports.txt"; then
    echo "C facade exports differ from the public header declarations" >&2
    diff -u "$temp_dir/c-declared-exports.txt" "$temp_dir/c-actual-exports.txt" >&2 || true
    exit 1
fi

# The engine holds no process-global mutable state by contract (D6): every
# object in the installed static archive must be free of writable data/bss/
# common/TLS definitions. Two reviewed exclusions: ELF .data.rel.ro (const
# data with relocations, write-protected right after loading) and the one
# deliberate seam txc_allocation_countdown — the default-off atomic
# allocation-injection counter that only the test harness writes; Mach-O
# ltmp<N> assembler section labels are compiler artifacts, not symbols.
find "$temp_dir/cmake-prefix-static/lib" -type f -name '*.a' >"$temp_dir/c-static-archives.txt"
if ! [ -s "$temp_dir/c-static-archives.txt" ]; then
    echo "Installed C static archives were not found for the global-state audit" >&2
    exit 1
fi
# Classification prefers the SysV section column: ELF const data that
# contains relocations (string-pointer tables like the spacing-command
# inventory) is emitted into .data.rel.ro, which the loader write-protects
# right after relocation — immutable by contract and therefore allowed.
# Mach-O nm leaves the SysV section column empty, but its class letters
# already separate const data from writable data, so the letters decide
# there; the plain-format letter scan remains as the fallback.
while IFS= read -r archive; do
    sysv_output=$("$nm_tool" --format=sysv "$archive" 2>/dev/null || true)
    case "$sysv_output" in
    *'|'*)
        writable_symbols=$(printf '%s\n' "$sysv_output" | awk -F'|' '
            NF >= 6 {
                name = $1
                gsub(/^[ \t]+|[ \t]+$/, "", name)
                class = $3
                gsub(/^[ \t]+|[ \t]+$/, "", class)
                section = (NF >= 7) ? $7 : ""
                gsub(/^[ \t]+|[ \t]+$/, "", section)
                if (name == "")
                    next
                if (section != "") {
                    if (section ~ /^\.data\.rel\.ro/)
                        next
                    if (section ~ /^(\.data|\.bss|\.tdata|\.tbss)/ || section ~ /COM/)
                        print name
                } else if (class ~ /^[dDbBC]$/) {
                    print name
                }
            }' | sed 's/^_//' \
            | grep -v -E '^(txc_allocation_countdown|txc_allocation_live|ltmp[0-9]+)$' \
            | sort -u || true)
        ;;
    *)
        writable_symbols=$("$nm_tool" "$archive" 2>/dev/null \
            | awk '$2 ~ /^[dDbBC]$/ { print $NF }' \
            | sed 's/^_//' \
            | grep -v -E '^(txc_allocation_countdown|txc_allocation_live|ltmp[0-9]+)$' \
            | sort -u || true)
        ;;
    esac
    if [ -n "$writable_symbols" ]; then
        echo "Writable global state found in $archive:" >&2
        echo "$writable_symbols" >&2
        exit 1
    fi
done <"$temp_dir/c-static-archives.txt"

# npm tarball: product files only, publishable, exports pinned.
node packages/es-tex-core/scripts/build.mjs >/dev/null
npm pack ./packages/es-tex-core --dry-run --json >"$temp_dir/npm-pack.json"
node - "$temp_dir/npm-pack.json" <<'NODE'
import fs from "node:fs";

const report = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const files = report.flatMap((entry) => entry.files.map((file) => file.path));
const unexpected = files.filter(
    (file) =>
        !["LICENSE", "README.md", "package.json", "dist/tex-core.wasm"].includes(file) &&
        !/^dist\/.+\.(?:js|d\.ts)$/.test(file)
);
for (const required of [
    "LICENSE",
    "README.md",
    "package.json",
    "dist/index.js",
    "dist/index.d.ts",
    "dist/tex-core.wasm"
]) {
    if (!files.includes(required)) {
        throw new Error(`npm package is missing ${required}`);
    }
}
if (unexpected.length > 0) {
    throw new Error(`Unexpected npm package files: ${unexpected.join(", ")}`);
}
const manifest = JSON.parse(fs.readFileSync("packages/es-tex-core/package.json", "utf8"));
if (manifest.private !== false) {
    throw new Error("ES package must be publishable");
}
NODE

# SwiftPM manifest shape: the reviewed products, target paths, and the
# plugin-derived conformance resources.
CLANG_MODULE_CACHE_PATH="$temp_dir/swift-module-cache" \
    swift package --disable-sandbox dump-package >"$temp_dir/swift-package.json"
node - "$temp_dir/swift-package.json" <<'NODE'
import fs from "node:fs";

const manifest = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
if (manifest.name !== "swift-tex-core") {
    throw new Error(`Unexpected SwiftPM package name: ${manifest.name}`);
}
const product = manifest.products.find((candidate) => candidate.name === "TexCore");
if (!product) {
    throw new Error("SwiftPM TexCore product is missing");
}
const target = manifest.targets.find((candidate) => candidate.name === "TexCore");
if (!target || target.path !== "packages/swift-tex-core/Sources/TexCore") {
    throw new Error("SwiftPM TexCore target path changed unexpectedly");
}
if (target.resources.length !== 0) {
    throw new Error("SwiftPM public product must not package render-tree spec data");
}
const cTarget = manifest.targets.find((candidate) => candidate.name === "TexCoreC");
if (!cTarget || cTarget.path !== "packages/tex-core") {
    throw new Error("SwiftPM internal C target path changed unexpectedly");
}
const unexpected = cTarget.sources.filter((source) => !source.startsWith("core/"));
if (unexpected.length > 0) {
    throw new Error(`Unexpected SwiftPM sources: ${unexpected.join(", ")}`);
}
const conformanceTarget = manifest.targets.find(
    (candidate) => candidate.name === "TexCoreConformanceTests"
);
if (
    !conformanceTarget ||
    conformanceTarget.resources.length !== 0 ||
    conformanceTarget.pluginUsages?.length !== 1 ||
    conformanceTarget.pluginUsages[0].plugin?.[0] !== "GenerateRenderTreeResources"
) {
    throw new Error("Swift conformance test bundle must derive resources through its build-tool plugin");
}
NODE

cmp LICENSE packages/es-tex-core/LICENSE >/dev/null || {
    echo "npm package license attribution differs from the repository license" >&2
    exit 1
}

# Kotlin publications: every ABI in the runtime AAR, no shared spec data in
# the JVM jar, and only the reviewed AAR entries.
scripts/gradle.sh :packages:kotlin-tex-core:verifyKotlinNativePackaging >/dev/null
kotlin_jvm_jar=$(find packages/kotlin-tex-core/build/libs -maxdepth 1 -type f \
    -name '*-jvm-*.jar' ! -name '*-sources.jar' | head -n 1)
if [ -z "$kotlin_jvm_jar" ]; then
    echo "Kotlin JVM publication JAR is missing" >&2
    exit 1
fi
if unzip -Z1 "$kotlin_jvm_jar" | grep -E '(^|/)(render-tree|manifest\.json)|\.tree$|\.tex$'; then
    echo "Kotlin JVM publication contains shared conformance spec data" >&2
    exit 1
fi
kotlin_aar="packages/kotlin-tex-core/android-runtime/build/outputs/aar/android-runtime-release.aar"
unzip -Z1 "$kotlin_aar" | while IFS= read -r artifact; do
    case "$artifact" in
        */ | \
            AndroidManifest.xml | \
            R.txt | \
            classes.jar | \
            META-INF/com/android/build/gradle/aar-metadata.properties | \
            jni/arm64-v8a/libtex_core_kotlin.so | \
            jni/armeabi-v7a/libtex_core_kotlin.so | \
            jni/x86/libtex_core_kotlin.so | \
            jni/x86_64/libtex_core_kotlin.so) ;;
        *)
            echo "Unexpected Kotlin Android runtime AAR entry: $artifact" >&2
            exit 1
            ;;
    esac
done

echo "package contents audit passed"
