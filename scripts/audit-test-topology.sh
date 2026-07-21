#!/bin/sh
# Test coverage and topology audit (phase-A skeleton).
#
# Verifies externally meaningful coverage boundaries for the C engine: the
# shared render-tree conformance contract is consumed, test plumbing never
# fetches mutable inputs at runtime, and the CTest selections discover the
# workloads they claim to run. Platform bindings extend this audit as they
# land; Phase 8 completes it.
set -eu

failures=0
fail() {
    echo "FAIL: $1" >&2
    failures=$((failures + 1))
}
note() {
    echo "ok: $1"
}

# 1. The C test graph consumes the shared conformance contract.
if [ ! -f specs/render-tree/manifest.json ]; then
    fail "root shared render-tree manifest is missing"
fi
if ! grep -q 'specs/render-tree' packages/tex-core/tests/CMakeLists.txt; then
    fail "C conformance targets do not consume the shared render-tree spec"
else
    note "C conformance suite consumes the shared render-tree spec"
fi

# 2. No runtime network dependency in build/test/bench plumbing.
if grep -n 'git clone' Makefile package.json CMakePresets.json \
    packages/tex-core/tests/CMakeLists.txt 2>/dev/null; then
    fail "runtime git clone found in build/test plumbing"
else
    note "no runtime clone in build/test plumbing"
fi
if grep -n -E 'curl|wget' Makefile CMakePresets.json \
    packages/tex-core/tests/CMakeLists.txt \
    packages/tex-core/benchmarks/CMakeLists.txt 2>/dev/null; then
    fail "network fetch in build/test plumbing"
else
    note "no network fetch in build/test plumbing"
fi

# 3. CTest topology: configure/build if needed, then cross-check labels and
# selections against runner discovery.
BUILD_DIR=build/cmake
if [ ! -f "$BUILD_DIR/CTestTestfile.cmake" ]; then
    cmake --preset default >/dev/null
    cmake --build --preset default --parallel >/dev/null
fi

for label in api engine errors determinism allocfail threads cli conformance benchmark; do
    count=$(ctest --test-dir "$BUILD_DIR" -N -L "^${label}$" | sed -n 's/^Total Tests: //p')
    if [ "${count:-0}" -lt 1 ]; then
        fail "no CTest tests carry label '$label'"
    fi
done
note "every required label resolves to at least one test"

if ctest --test-dir "$BUILD_DIR" -N | grep -q 'Disabled'; then
    fail "disabled tests present in the CTest graph"
else
    note "no disabled tests in the CTest graph"
fi

correctness_list=$(ctest --test-dir "$BUILD_DIR" -N -LE '^(benchmark|conformance)$' | sed -n 's/^  Test *#[0-9]*: //p')
if echo "$correctness_list" | grep -Eq '^(conformance-|benchmark-)'; then
    fail "correctness selection includes conformance or benchmark workloads"
else
    note "correctness selection excludes conformance and benchmark"
fi

conformance_list=$(ctest --test-dir "$BUILD_DIR" -N -L '^conformance$' | sed -n 's/^  Test *#[0-9]*: //p')
if echo "$conformance_list" | grep -vq '^conformance-'; then
    fail "C conformance selection includes non-conformance workloads"
else
    note "C conformance selection is isolated from correctness"
fi

# Every manifest case must surface as exactly one CTest conformance test.
manifest_count=$(node --input-type=module -e '
import fs from "node:fs";
const manifest = JSON.parse(fs.readFileSync("specs/render-tree/manifest.json", "utf8"));
process.stdout.write(String(manifest.cases.length));
')
ctest_count=$(echo "$conformance_list" | grep -c '^conformance-' || true)
if [ "$manifest_count" -ne "$ctest_count" ]; then
    fail "manifest lists $manifest_count conformance cases but CTest discovers $ctest_count"
else
    note "manifest and CTest agree on $manifest_count conformance cases"
fi

benchmark_list=$(ctest --test-dir "$BUILD_DIR" -N -L '^benchmark$' | sed -n 's/^  Test *#[0-9]*: //p')
if echo "$benchmark_list" | grep -v '^benchmark-' | grep -q .; then
    fail "benchmark selection includes non-benchmark tests"
else
    note "benchmark selection contains only benchmark workloads"
fi

if [ "$failures" -gt 0 ]; then
    echo "$failures test topology violation(s)" >&2
    exit 1
fi
echo "test topology audit passed"
