#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
. "$root/scripts/lib/artifact.sh"
artifact_dir=${1:-}
suite=${2:-}

artifact_verify "$artifact_dir" es-test-dist
artifact_extract "$artifact_dir" es-dist.tar.gz "$root"

case "$suite" in
    node-correctness)
        node "$root/packages/es-tex-core/scripts/run-tests.mjs" --target node --skip-build
        ;;
    node-minimum)
        node "$root/packages/es-tex-core/scripts/run-tests.mjs" --target node-minimum --skip-build
        ;;
    browser-correctness)
        node "$root/packages/es-tex-core/scripts/run-tests.mjs" --target browser --skip-build
        ;;
    node-conformance)
        node "$root/packages/es-tex-core/scripts/run-conformance.mjs" --skip-build
        ;;
    node-benchmark)
        node "$root/packages/es-tex-core/scripts/benchmark.mjs"
        ;;
    *)
        echo "usage: $0 <artifact-dir> node-correctness|node-minimum|browser-correctness|node-conformance|node-benchmark" >&2
        exit 2
        ;;
esac
