#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
. "$root/scripts/lib/artifact.sh"
artifact_dir=${1:-}
suite=${2:-}
consumer=packages/swift-tex-core/Tests/Consumer

artifact_verify "$artifact_dir" swift-test-products
artifact_extract "$artifact_dir" swift-test-products.tar.gz "$root"
cd "$root"

# A filter that matches nothing still exits 0 ("Executed 0 tests"), so a
# suite rename would turn a required lane into a green no-op: every
# filtered invocation must prove at least one test actually executed.
run_swift_suite() {
    local transcript
    transcript=$(mktemp)
    swift test "$@" 2>&1 | tee "$transcript"
    if ! grep -Eq 'Test run with [1-9][0-9]* test|Executed [1-9][0-9]* test' "$transcript"; then
        echo "swift test executed zero tests for: $*" >&2
        rm -f "$transcript"
        return 1
    fi
    rm -f "$transcript"
}

run_ios_suite() {
    local test_target=$1
    local destination
    local udid
    local status=0
    local transcript
    transcript=$(mktemp)
    destination=$(scripts/prepare-swift-ios-simulator.sh)
    udid=${destination##*=}
    xcodebuild test-without-building \
        -scheme swift-tex-core-Package \
        -destination "$destination" \
        -derivedDataPath build/xcode-tests \
        "-only-testing:$test_target" 2>&1 | tee "$transcript" || status=$?
    if [ "$status" -eq 0 ] &&
        ! grep -Eq 'Test run with [1-9][0-9]* test|Executed [1-9][0-9]* test' "$transcript"; then
        echo "xcodebuild executed zero tests for: $test_target" >&2
        status=1
    fi
    rm -f "$transcript"
    xcrun simctl shutdown "$udid" >/dev/null 2>&1 || true
    return "$status"
}

case "$suite" in
    macos-correctness)
        run_swift_suite --skip-build --disable-sandbox --filter '^TexCoreTests\.'
        run_swift_suite --skip-build --disable-sandbox --package-path "$consumer"
        ;;
    macos-conformance)
        run_swift_suite --skip-build --disable-sandbox --filter '^TexCoreConformanceTests\.'
        ;;
    ios-correctness)
        run_ios_suite TexCoreTests
        ;;
    ios-conformance)
        run_ios_suite TexCoreConformanceTests
        ;;
    macos-benchmark)
        build/ci-benchmark/swift/TexCoreBenchmarks
        ;;
    *)
        echo "usage: $0 <artifact-dir> macos-correctness|macos-conformance|ios-correctness|ios-conformance|macos-benchmark" >&2
        exit 2
        ;;
esac
