#!/usr/bin/env bash
# CI policy self-audit (template §13, phase-A scope).
#
# Verifies the frozen control-plane behavior of the C-only CI graph:
# triggers, concurrency lanes, stable gate names, fail-closed barriers,
# artifact-handoff hygiene, and the checked-in ruleset/environment recipes.
# Release-workflow rules join this audit in Phase 9; the remaining platform
# rows join as their bindings land.
#
# shellcheck disable=SC2016  # grep -F patterns match literal ${{ }} text
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

ci=.github/workflows/ci.yml
codeql=.github/workflows/codeql.yml
ruleset=.github/rulesets/main.json
release_ruleset=.github/rulesets/release-tags.json
release_environment=.github/environments/release.json
release_environment_policy=.github/environments/release-tag-policy.json

if command -v rg >/dev/null 2>&1; then
    search() {
        rg -q "$@"
    }
else
    search() {
        grep -Eq "$@"
    }
fi

for required in \
    "$ci" \
    "$codeql" \
    "$ruleset" \
    "$release_ruleset" \
    "$release_environment" \
    "$release_environment_policy" \
    scripts/bootstrap-repository.sh; do
    if [ ! -f "$required" ]; then
        echo "missing CI policy file: $required" >&2
        exit 1
    fi
done

test -x scripts/build-c-product-artifact.sh
test -x scripts/build-c-test-artifact.sh
test -x scripts/run-c-test-artifact.sh
test -x scripts/bootstrap-repository.sh
grep -Fq -- '-DTEX_CORE_TESTS=OFF' scripts/build-c-product-artifact.sh
grep -Fq -- '-DTEX_CORE_TESTS=ON' scripts/build-c-test-artifact.sh
grep -Fq 'Total Tests: [1-9][0-9]*' scripts/build-c-test-artifact.sh
grep -Fq 'benchmark' scripts/run-c-test-artifact.sh

test -x scripts/build-swift-product-artifact.sh
test -x scripts/build-swift-test-artifact.sh
test -x scripts/run-swift-test-artifact.sh
test -x scripts/build-swift-deployment.sh
test -x scripts/check-swift-source-archive.sh
grep -Fq -- 'swift build --target TexCore' scripts/build-swift-product-artifact.sh
grep -Fq 'Package.release.swift' scripts/check-swift-source-archive.sh
if grep -Eq 'swift package archive-source|cp .*Tests|cp .*Benchmarks|swift test|specs/render-tree' \
    scripts/check-swift-source-archive.sh; then
    echo "Swift release staging still includes test, benchmark, or conformance source" >&2
    exit 1
fi
if grep -Eq '\.testTarget|TexCoreBenchmarks|Conformance|Plugins|Tools' \
    packages/swift-tex-core/Package.release.swift; then
    echo "Swift release manifest contains non-product targets" >&2
    exit 1
fi

for job in \
    health-check-repository \
    health-check-c \
    health-checks-ready \
    c-product-build \
    c-product-build-windows \
    package-audit \
    c-test-build \
    c-test-build-windows \
    c-sanitizer-test-build \
    c-test \
    c-test-windows \
    c-sanitizer-test \
    benchmark-c \
    benchmarks-ready \
    builds-ready \
    build-tests-ready \
    tests-ready; do
    search "^    ${job}:$" "$ci"
done
search 'actions/upload-artifact@' "$ci"
search 'actions/download-artifact@' "$ci"

# Test consumers execute prebuilt trees; a compiler invocation there voids
# the artifact-handoff contract.
if search 'cmake --build|cmake --preset| cc | clang | gcc ' scripts/run-c-test-artifact.sh; then
    echo "test artifact consumer contains a compiler/build invocation" >&2
    exit 1
fi
search 'sha256sum --check SHA256SUMS' scripts/run-c-test-artifact.sh
search 'source_sha' scripts/run-c-test-artifact.sh

for consumer in \
    c-test \
    c-test-windows \
    c-sanitizer-test \
    benchmark-c; do
    consumer_job=$(sed -n "/^    ${consumer}:$/,/^    [a-z].*:$/p" "$ci")
    if ! grep -Fq '        needs: build-tests-ready' <<<"$consumer_job"; then
        echo "test consumer bypasses the global build-test barrier: $consumer" >&2
        exit 1
    fi
done

for producer in \
    c-product-build \
    c-product-build-windows; do
    producer_job=$(sed -n "/^    ${producer}:$/,/^    [a-z].*:$/p" "$ci")
    if ! grep -Fq '        needs: health-checks-ready' <<<"$producer_job"; then
        echo "build producer bypasses the global health-check barrier: $producer" >&2
        exit 1
    fi
done

for contract in \
    package-audit \
    c-test-build \
    c-test-build-windows \
    c-sanitizer-test-build; do
    contract_job=$(sed -n "/^    ${contract}:$/,/^    [a-z].*:$/p" "$ci")
    if ! grep -Fq '        needs: builds-ready' <<<"$contract_job"; then
        echo "build test bypasses the global build barrier: $contract" >&2
        exit 1
    fi
done

search '^        name: Health Check - Repository$' "$ci"
search '^        name: Health Check - C$' "$ci"
search '^        name: Build - C ' "$ci"
search '^        name: Build Test - C ' "$ci"
search '^        name: Build Test - Repository / Package Contents$' "$ci"
search '^        name: Test - C Sanitizer ' "$ci"
if search '^        name:.*matrix\.(os|suite|compiler|shared|sanitizer|variant|product-variant)' "$ci"; then
    echo "matrix implementation fields leaked into a visible CI job name" >&2
    exit 1
fi

tests_ready_job=$(sed -n '/^    tests-ready:$/,/^    required-gates:$/p' "$ci")
grep -Fq '        if: ${{ always() }}' <<<"$tests_ready_job"
if grep -Fq 'benchmarks-ready' <<<"$tests_ready_job"; then
    echo "Tests - Ready must not depend on the parallel benchmark barrier" >&2
    exit 1
fi
required_gate_job=$(sed -n '/^    required-gates:/,$p' "$ci")
grep -Fq '            - tests-ready' <<<"$required_gate_job"
grep -Fq '            - benchmarks-ready' <<<"$required_gate_job"
grep -Fq 'BENCHMARKS_READY: ${{ needs.benchmarks-ready.result }}' <<<"$required_gate_job"

benchmarks_ready_job=$(sed -n '/^    benchmarks-ready:$/,/^    tests-ready:$/p' "$ci")
grep -Fq '        if: ${{ always() }}' <<<"$benchmarks_ready_job"

for workflow in "$ci" "$codeql"; do
    if ! search '^    merge_group:$' "$workflow"; then
        echo "blocking workflow lacks merge_group support: $workflow" >&2
        exit 1
    fi
done
search '^    workflow_call:$' "$ci"
search '^    workflow_dispatch:$' "$ci"

ci_push_trigger=$(sed -n '/^    push:$/,/^    merge_group:$/p' "$ci")
if ! grep -Fqx '        branches:' <<<"$ci_push_trigger" ||
    ! grep -Fqx '            - main' <<<"$ci_push_trigger"; then
    echo "blocking CI push trigger must cover only the default branch" >&2
    exit 1
fi
if [ "$(grep -c '^            - ' <<<"$ci_push_trigger")" -ne 1 ]; then
    echo "blocking CI push trigger must not duplicate pull-request CI on feature branches" >&2
    exit 1
fi
if search '^        tags(-ignore)?:' <<<"$ci_push_trigger"; then
    echo "blocking CI push trigger must not run on release tags" >&2
    exit 1
fi

search '^    required-gates:$' "$ci"
grep -Fq "name: \${{ (github.event_name == 'pull_request' || github.event_name == 'merge_group') && 'Required gates' || 'Development branch gates' }}" "$ci"
grep -Fq 'group: ci-${{ github.event_name }}-${{ github.event.pull_request.number || github.ref }}' "$ci"
search '^    cancel-in-progress: true$' "$ci"
search '^    codeql-gate:$' "$codeql"
search '^        name: CodeQL gate$' "$codeql"
grep -Fq '        name: Security Scan - ${{ matrix.label }}' "$codeql"
grep -Fq -- '-DTEX_CORE_TESTS=OFF' "$codeql"
if search 'autobuild' "$codeql"; then
    echo "CodeQL must use a repo-owned manual product build" >&2
    exit 1
fi

node --input-type=module - "$ruleset" <<'NODE'
import fs from "node:fs";

const ruleset = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
// Rulesets run in evaluate mode until the Phase 10 activation flips this
// audit (and the recipes) to require "active".
if (!["evaluate", "active"].includes(ruleset.enforcement)) {
    throw new Error(`main ruleset enforcement changed: ${ruleset.enforcement}`);
}
const required = ruleset.rules.find((rule) => rule.type === "required_status_checks");
const contexts = required?.parameters?.required_status_checks?.map((check) => check.context).sort();
const expected = ["CodeQL gate", "Required gates"];
if (JSON.stringify(contexts) !== JSON.stringify(expected)) {
    throw new Error(`ruleset required checks changed: ${JSON.stringify(contexts)}`);
}
if (ruleset.conditions?.ref_name?.include?.join(",") !== "~DEFAULT_BRANCH") {
    throw new Error("ruleset must target only the default branch");
}
const mainPullRequest = ruleset.rules.find((rule) => rule.type === "pull_request");
if (mainPullRequest?.parameters?.required_reviewers?.length) {
    throw new Error("owner reviewers must not share the main CI ruleset");
}
if (ruleset.bypass_actors?.length) {
    throw new Error("the main quality gate must not carry bypass actors");
}
NODE

node --input-type=module - "$release_ruleset" "$release_environment" "$release_environment_policy" <<'NODE'
import fs from "node:fs";

const releaseRuleset = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const environment = JSON.parse(fs.readFileSync(process.argv[3], "utf8"));
const deploymentPolicy = JSON.parse(fs.readFileSync(process.argv[4], "utf8"));

if (releaseRuleset.target !== "tag") {
    throw new Error("release tag ruleset must target tags");
}
// Evaluate until Phase 10 activation, matching the main ruleset recipe.
if (!["evaluate", "active"].includes(releaseRuleset.enforcement)) {
    throw new Error(`release tag ruleset enforcement changed: ${releaseRuleset.enforcement}`);
}
if (releaseRuleset.conditions?.ref_name?.include?.join(",") !== "refs/tags/v*.*.*") {
    throw new Error("release tag ruleset must target only v*.*.* tags");
}
const releaseRuleTypes = releaseRuleset.rules.map((rule) => rule.type).sort();
if (JSON.stringify(releaseRuleTypes) !== JSON.stringify(["creation", "deletion", "update"])) {
    throw new Error(`release tag rules changed: ${JSON.stringify(releaseRuleTypes)}`);
}
if (
    releaseRuleset.bypass_actors?.length !== 1 ||
    releaseRuleset.bypass_actors[0]?.actor_id !== 8455725 ||
    releaseRuleset.bypass_actors[0]?.actor_type !== "User" ||
    releaseRuleset.bypass_actors[0]?.bypass_mode !== "always"
) {
    throw new Error("release tag ruleset bypass must remain scoped to DongyuZhao");
}
if (
    environment.wait_timer !== 0 ||
    environment.prevent_self_review !== false ||
    environment.reviewers?.length !== 1 ||
    environment.reviewers[0]?.type !== "User" ||
    environment.reviewers[0]?.id !== 8455725
) {
    throw new Error("release environment reviewer policy changed");
}
if (
    environment.deployment_branch_policy?.protected_branches !== false ||
    environment.deployment_branch_policy?.custom_branch_policies !== true
) {
    throw new Error("release environment must use a custom deployment policy");
}
if (deploymentPolicy.name !== "v*.*.*" || deploymentPolicy.type !== "tag") {
    throw new Error("release environment must accept only v*.*.* tags");
}
NODE

echo "CI policy audit passed"
