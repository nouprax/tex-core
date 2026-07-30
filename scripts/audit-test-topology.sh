#!/bin/sh
# Test coverage and topology audit.
#
# Verifies externally meaningful coverage boundaries: every binding consumes
# the shared render-tree conformance contract, test plumbing never fetches
# mutable inputs at runtime, the CTest selections discover the workloads
# they claim to run, and each platform's suite discovery is non-empty.
set -eu

failures=0
fail() {
    echo "FAIL: $1" >&2
    failures=$((failures + 1))
}
note() {
    echo "ok: $1"
}

# 1. Every platform consumes the shared conformance contract; a runner- or
# platform-owned fixture copy is a drift channel.
if [ ! -f specs/render-tree/manifest.json ]; then
    fail "root shared render-tree manifest is missing"
fi
if ! grep -q 'specs/render-tree' packages/tex-core/tests/CMakeLists.txt \
    || ! grep -q 'plugins: \[.plugin(name: "GenerateRenderTreeResources")\]' Package.swift \
    || ! grep -q 'specs/render-tree' packages/swift-tex-core/Plugins/GenerateRenderTreeResources/plugin.swift \
    || ! grep -q 'GenerateRenderTreeFixtures' packages/kotlin-tex-core/build.gradle.kts \
    || ! grep -q 'bundle:conformance-fixtures' packages/es-tex-core/package.json \
    || ! grep -q 'specs/render-tree' packages/es-tex-core/scripts/bundle-conformance-fixtures.mjs; then
    fail "one or more conformance targets do not consume the shared render-tree spec"
else
    note "C, SwiftPM plugin, Gradle task, and ES bundler consume the shared spec"
fi

# 2. No runtime network dependency in build/test/bench plumbing.
if grep -n 'git clone' Makefile package.json CMakePresets.json \
    packages/tex-core/tests/CMakeLists.txt 2>/dev/null; then
    fail "runtime git clone found in build/test plumbing"
else
    note "no runtime clone in build/test plumbing"
fi
if grep -n -E 'curl|wget|fetch\(' Makefile CMakePresets.json \
    packages/tex-core/tests/CMakeLists.txt \
    packages/tex-core/benchmarks/CMakeLists.txt \
    packages/es-tex-core/scripts/build.mjs \
    packages/es-tex-core/scripts/bundle-conformance-fixtures.mjs \
    packages/swift-tex-core/Plugins/GenerateRenderTreeResources/plugin.swift 2>/dev/null; then
    fail "network fetch in build/test plumbing"
else
    note "no network fetch in build/test plumbing"
fi

# 3. CTest topology: configure-only — `ctest -N` reads the generated test
# graph without any product compilation, so the health check stays on the
# critical path's cheap side (the dedicated build jobs compile the same
# source exactly once, later and in parallel).
BUILD_DIR=build/cmake
cmake --preset default >/dev/null

for label in api engine errors determinism allocfail threads cli conformance complexity benchmark; do
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

complexity_list=$(ctest --test-dir "$BUILD_DIR" -N -L '^complexity$' | sed -n 's/^  Test *#[0-9]*: //p')
if echo "$complexity_list" | grep -v '^complexity-' | grep -q .; then
    fail "complexity selection includes non-complexity tests"
elif ! echo "$correctness_list" | grep -q '^complexity-'; then
    fail "Release correctness selection omits the complexity gates"
else
    note "complexity gates are isolated and included in Release correctness"
fi

node --input-type=module <<'NODE' || fail "sanitizer presets do not exclude timing-based complexity gates"
import fs from "node:fs";
const presets = JSON.parse(fs.readFileSync("CMakePresets.json", "utf8")).testPresets;
for (const name of ["correctness-asan", "correctness-ubsan", "correctness-tsan"]) {
    const preset = presets.find((candidate) => candidate.name === name);
    if (!preset?.filter?.exclude?.label?.includes("complexity")) {
        throw new Error(`${name} does not exclude complexity`);
    }
}
NODE
note "sanitizer presets exclude timing-based complexity gates"

# 4. Platform suite discovery stays non-empty. The Swift source assertion
# runs on every host, including the required Linux health-check runner; the
# Swift artifact producer additionally lists the built test graph.
if grep -R -q '@Test' packages/swift-tex-core/Tests/TexCoreTests \
    && grep -R -q '@Test' packages/swift-tex-core/Tests/TexCoreConformanceTests; then
    note "Swift test targets declare Swift Testing tests"
else
    fail "Swift test targets declare no Swift Testing tests"
fi
if [ "$(node packages/es-tex-core/scripts/run-tests.mjs --list | wc -l)" -lt 1 ]; then
    fail "the ES runner discovers no correctness suites"
else
    note "the ES runner discovers correctness suites"
fi
if ! grep -q 'includeTestsMatching("\*ConformanceTest\*")' packages/kotlin-tex-core/build.gradle.kts; then
    fail "the Kotlin conformance test runs lost their filter"
else
    note "Kotlin conformance test runs keep their dedicated filter"
fi

if [ "$failures" -gt 0 ]; then
    echo "$failures test topology violation(s)" >&2
    exit 1
fi
echo "test topology audit passed"
