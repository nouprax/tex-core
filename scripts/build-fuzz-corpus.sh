#!/usr/bin/env bash
# Seeds the libFuzzer corpus from the shared render-tree conformance
# fixtures: every .tex source appears once per mode, prefixed with the
# harness's mode byte (0 document, 1 math-inline, 2 math-display), so the
# campaign starts from the whole supported surface instead of nothing.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?usage: build-fuzz-corpus.sh OUTPUT_DIRECTORY}

rm -rf "$output"
mkdir -p "$output"
for fixture in "$root"/specs/render-tree/*.tex; do
    name=$(basename "$fixture" .tex)
    for mode in 0 1 2; do
        { printf "\\$(printf '%03o' "$mode")"; cat "$fixture"; } >"$output/$name-mode$mode"
    done
done
count=$(find "$output" -type f | wc -l | tr -d ' ')
echo "Seeded $count fuzz corpus inputs into $output"
