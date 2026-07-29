#!/usr/bin/env bash
# Local iOS Simulator test entry point. The destination is discovered (or a
# temporary simulator is created) by prepare-swift-ios-simulator.sh, so local
# test commands do not depend on a named device model or moving OS alias.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_target=${1:-}

case "$test_target" in
    TexCoreTests | TexCoreConformanceTests) ;;
    *)
        echo "usage: $0 TexCoreTests|TexCoreConformanceTests" >&2
        exit 2
        ;;
esac

cd "$root"
status=0
destination=$(scripts/prepare-swift-ios-simulator.sh)
udid=${destination##*=}
CLANG_MODULE_CACHE_PATH="$root/build/swift-module-cache" \
    xcodebuild test \
        -scheme swift-tex-core-Package \
        -destination "$destination" \
        -derivedDataPath build/xcode-tests \
        "-only-testing:$test_target" || status=$?
xcrun simctl shutdown "$udid" >/dev/null 2>&1 || true
exit "$status"
