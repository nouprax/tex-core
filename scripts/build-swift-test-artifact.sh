#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
. "$root/scripts/lib/artifact.sh"
output=${1:-"$root/build/ci-artifacts/swift"}
consumer=packages/swift-tex-core/Tests/Consumer

cd "$root"
CLANG_MODULE_CACHE_PATH="$root/build/swift-module-cache" \
    swift build --build-tests --disable-sandbox
# The repository topology audit runs on Linux without a Swift toolchain, so
# the Swift producer itself must prove that the built products expose a
# non-empty Swift Testing graph.
swift_test_list=$(CLANG_MODULE_CACHE_PATH="$root/build/swift-module-cache" \
    swift test --disable-sandbox list)
if [ -z "$swift_test_list" ]; then
    echo "Swift test artifact discovers no Swift Testing suites" >&2
    exit 1
fi
CLANG_MODULE_CACHE_PATH="$root/build/swift-module-cache" \
    swift build --build-tests --disable-sandbox --package-path "$consumer"
CLANG_MODULE_CACHE_PATH="$root/build/swift-module-cache" \
    swift build --disable-sandbox -c release --product TexCoreBenchmarks
benchmark_bin=$(swift build --disable-sandbox -c release --show-bin-path)
rm -rf build/ci-benchmark/swift
mkdir -p build/ci-benchmark/swift
cp "$benchmark_bin/TexCoreBenchmarks" build/ci-benchmark/swift/
CLANG_MODULE_CACHE_PATH="$root/build/swift-module-cache" \
    xcodebuild build-for-testing \
        -scheme swift-tex-core-Package \
        -destination 'generic/platform=iOS Simulator' \
        -derivedDataPath build/xcode-tests

rm -rf "$output"
mkdir -p "$output"
tar -czf "$output/swift-test-products.tar.gz" \
    .build \
    "$consumer/.build" \
    build/ci-benchmark/swift \
    build/xcode-tests
cat >"$output/manifest.txt" <<EOF
schema=1
kind=swift-test-products
source_sha=$(artifact_source_sha "$root")
EOF
artifact_sha256_write "$output" swift-test-products.tar.gz manifest.txt
