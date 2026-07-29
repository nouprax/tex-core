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
release=.github/workflows/release.yml
release_dry_run=.github/workflows/release-dry-run.yml
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

# Extract one top-level YAML key (a job under `jobs:` or a trigger under
# `on:`) through the next 4-space key, regardless of neighbouring job order.
# Policy assertions must slice structurally instead of treating workflow
# layout as part of the contract.
job_body() {
    awk -v key="$1" '
        BEGIN { target = "    " key ":" }
        $0 == target { collecting = 1; print; next }
        collecting && /^    [A-Za-z0-9_-]+:$/ { exit }
        collecting { print }
    ' "$2"
}

# Supply-chain pinning: every workflow and repo-owned composite action must
# reference an immutable commit SHA (a movable major tag lets a tag
# replacement change the code CI executes without a reviewed diff). Local
# composite actions (uses: ./…) are exempt from the SHA rule, but their
# external dependencies are scanned alongside workflows.
action_sources=(.github/workflows/)
if [ -d .github/actions ]; then
    action_sources+=(.github/actions/)
fi
if grep -rhoE 'uses: [^ ]+' "${action_sources[@]}" | grep -vE 'uses: [^ ]+@[0-9a-f]{40}$' | grep -v 'uses: \./' | grep -q .; then
    echo "workflow action references must be pinned to a full commit SHA:" >&2
    grep -rnE 'uses: [^ ]+' "${action_sources[@]}" | grep -vE '@[0-9a-f]{40}( #.*)?$' | grep -v 'uses: \./' >&2
    exit 1
fi
grep -q 'EMSCRIPTEN_COMMIT=[0-9a-f]\{40\}' scripts/init-environment.sh || {
    echo "init-environment.sh must pin the emsdk commit" >&2
    exit 1
}
grep -q -- '--require-hashes' scripts/init-environment.sh || {
    echo "init-environment.sh must install Python tools with --require-hashes" >&2
    exit 1
}
grep -q 'brace-expansion@5\.0\.8:' pnpm-lock.yaml || {
    echo "pnpm lockfile must carry the patched brace-expansion release" >&2
    exit 1
}
for required in \
    "$ci" \
    "$codeql" \
    "$release" \
    "$release_dry_run" \
    "$ruleset" \
    "$release_ruleset" \
    "$release_environment" \
    "$release_environment_policy" \
    docs/repository-setup-template.md \
    scripts/lib/artifact.sh \
    scripts/lib/discover-toolchain.sh \
    scripts/bootstrap-repository.sh; do
    if [ ! -f "$required" ]; then
        echo "missing CI policy file: $required" >&2
        exit 1
    fi
done
for setup_recipe in scripts/bootstrap-repository.sh docs/repository-setup-template.md; do
    if [ "$(grep -Fc 'required_review_thread_resolution: true' "$setup_recipe")" -ne 1 ]; then
        echo "main ruleset recipe must require resolved review conversations: $setup_recipe" >&2
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
test -x scripts/run-swift-ios-tests.sh
test -x scripts/build-swift-deployment.sh
test -x scripts/check-swift-source-archive.sh
grep -Fq -- 'swift build --target TexCore' scripts/build-swift-product-artifact.sh
grep -Fq 'Package.release.swift' scripts/check-swift-source-archive.sh
grep -Fq 'prepare-swift-ios-simulator.sh' scripts/run-swift-test-artifact.sh
grep -Fq 'prepare-swift-ios-simulator.sh' scripts/run-swift-ios-tests.sh
if grep -Eq 'name=iPhone|OS=latest' scripts/build-swift-test-artifact.sh scripts/run-swift-test-artifact.sh \
    scripts/run-swift-ios-tests.sh package.json; then
    echo "Swift CI or pnpm entry points hard-code a simulator model or moving runtime alias" >&2
    exit 1
fi
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

test -x scripts/build-kotlin-product-artifact.sh
test -x scripts/build-kotlin-host-test-artifact.sh
test -x scripts/run-kotlin-host-test-artifact.sh
test -x scripts/build-kotlin-android-test-artifact.sh
test -x scripts/run-kotlin-android-test-artifact.sh
test -x scripts/stage-maven-publications.sh
for toolchain_consumer in scripts/gradle.sh scripts/check-kotlin-consumers.sh scripts/gradle-model-smoke.sh; do
    grep -Fq 'scripts/lib/discover-toolchain.sh' "$toolchain_consumer" \
        || grep -Fq 'lib/discover-toolchain.sh' "$toolchain_consumer"
done
grep -Fq 'gradle/wrapper/gradle-wrapper.properties' scripts/gradle-model-smoke.sh
if grep -Eq 'gradle-[0-9]+\.[0-9]+(\.[0-9]+)?/lib/gradle-tooling-api' scripts/gradle-model-smoke.sh; then
    echo "Gradle model smoke hard-codes the wrapper/tooling API version" >&2
    exit 1
fi
grep -Fq 'org.gradle.configuration-cache=true' gradle.properties
grep -Fq 'org.gradle.configuration-cache.parallel=true' gradle.properties
grep -Fq 'org.gradle.toolchains.foojay-resolver-convention' settings.gradle.kts
grep -Fq 'val runtimeAarFile = androidRuntimeAar' packages/kotlin-tex-core/build.gradle.kts
grep -Fq 'inputs.file(desktopLibraryFile)' packages/kotlin-tex-core/build.gradle.kts
grep -Fq 'stageJvmBenchmarkArtifact' scripts/build-kotlin-host-test-artifact.sh
grep -Fq -- '-PtexCore.android.abis=x86_64' scripts/build-kotlin-android-test-artifact.sh
search 'artifact_verify ' scripts/run-kotlin-android-test-artifact.sh

# The emulator consumers execute one immutable APK; any build-system entry
# point there voids the build-once/test-many contract, and the four
# page-size x suite legs must stay independent so a wedged leg cannot mask
# its siblings.
android_test_job=$(job_body kotlin-android-test "$ci")
for forbidden in \
    'gradle' \
    'sdkmanager "platforms' \
    'ndk;' \
    'cmake' \
    'publishKotlinToMavenLocal' \
    'run-kotlin-android-emulator-tests.sh'; do
    if grep -Fq "$forbidden" <<<"$android_test_job"; then
        echo "Android test consumer contains build dependency: $forbidden" >&2
        exit 1
    fi
done
if [ "$(grep -c -E '^[[:space:]]*suite:' <<<"$android_test_job")" -ne 4 ]; then
    echo "Android correctness/conformance and 4K/16K must be four independent consumers" >&2
    exit 1
fi

test -x scripts/build-es-product-artifact.sh
test -x scripts/build-es-test-artifact.sh
test -x scripts/run-es-test-artifact.sh
grep -Fq 'kind=es-product-dist' scripts/build-es-product-artifact.sh
search 'artifact_verify ' scripts/run-es-test-artifact.sh

for job in \
    health-check-repository \
    health-check-c \
    health-check-kotlin \
    health-check-es \
    health-checks-ready \
    c-product-build \
    c-product-build-windows \
    kotlin-product-build \
    es-product-build \
    package-audit \
    c-test-build \
    c-test-build-windows \
    c-sanitizer-test-build \
    kotlin-test-build \
    kotlin-consumers \
    kotlin-android-test-build \
    es-test-build \
    c-test \
    c-test-windows \
    c-sanitizer-test \
    kotlin-test \
    kotlin-android-test \
    es-test \
    benchmark-c \
    benchmark-kotlin \
    benchmark-es \
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
search 'artifact_verify ' scripts/run-c-test-artifact.sh

for consumer in \
    c-test \
    c-test-windows \
    c-sanitizer-test \
    kotlin-test \
    kotlin-android-test \
    es-test \
    benchmark-c \
    benchmark-kotlin \
    benchmark-es; do
    consumer_job=$(job_body "$consumer" "$ci")
    if ! grep -Fq '        needs: build-tests-ready' <<<"$consumer_job"; then
        echo "test consumer bypasses the global build-test barrier: $consumer" >&2
        exit 1
    fi
done

for producer in \
    c-product-build \
    c-product-build-windows \
    kotlin-product-build \
    es-product-build; do
    producer_job=$(job_body "$producer" "$ci")
    if ! grep -Fq '        needs: health-checks-ready' <<<"$producer_job"; then
        echo "build producer bypasses the global health-check barrier: $producer" >&2
        exit 1
    fi
done

for contract in \
    package-audit \
    c-test-build \
    c-test-build-windows \
    c-sanitizer-test-build \
    kotlin-test-build \
    kotlin-consumers \
    kotlin-android-test-build \
    es-test-build; do
    contract_job=$(job_body "$contract" "$ci")
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

tests_ready_job=$(job_body tests-ready "$ci")
grep -Fq '        if: ${{ always() }}' <<<"$tests_ready_job"
if grep -Fq 'benchmarks-ready' <<<"$tests_ready_job"; then
    echo "Tests - Ready must not depend on the parallel benchmark barrier" >&2
    exit 1
fi
required_gate_job=$(job_body required-gates "$ci")
grep -Fq '            - tests-ready' <<<"$required_gate_job"
grep -Fq '            - benchmarks-ready' <<<"$required_gate_job"
grep -Fq 'BENCHMARKS_READY: ${{ needs.benchmarks-ready.result }}' <<<"$required_gate_job"

benchmarks_ready_job=$(job_body benchmarks-ready "$ci")
grep -Fq '        if: ${{ always() }}' <<<"$benchmarks_ready_job"

# Formal release workflow: tag-triggered only, tag-local quality gates,
# environment-gated publishes, ordered publication, curated notes, OIDC npm.
search '^    push:$' "$release"
search '^        tags:$' "$release"
search '^    workflow_dispatch:$' "$release"
if search '^    pull_request:$' "$release"; then
    echo "formal release workflow may not accept pull requests" >&2
    exit 1
fi
search '^    contents: read$' "$release"
search '^        environment: release$' "$release"
search '^    quality:$' "$release"
search '^        name: Quality Gate - Release$' "$release"
search '^        uses: \./\.github/workflows/ci\.yml$' "$release"
# The ban is on consulting historical check results; the tag-ancestry
# guard (git merge-base --is-ancestor against origin/main) is tag-local
# validation and stays allowed.
if search 'GITHUB_SHA|check-runs|CodeQL gate|Required gates|Development branch gates' "$release"; then
    echo "formal release must run tag-local quality gates instead of querying historical checks" >&2
    exit 1
fi
search 'merge-base --is-ancestor HEAD origin/main' "$release"
if [ "$(grep -c '^        needs: quality$' "$release")" -ne 5 ]; then
    echo "every initial release artifact job must wait for tag-local quality gates" >&2
    exit 1
fi
for release_name in \
    'Health Check - Release / Tag and Versions' \
    'Build Release - C / ${{ matrix.label }}' \
    'Build Release - Swift / Product Source' \
    'Build Release - ES / npm Package' \
    'Build Release - Kotlin / Linux Publications' \
    'Build Release - Kotlin / macOS Publications' \
    'Assemble Release - Maven Central' \
    'Release Artifacts - Ready' \
    'Publish Release - Maven Central / Stage' \
    'Publish Release - ES / npm' \
    'Publish Release - Maven Central / Commit' \
    'Publish Release - GitHub'; do
    grep -Fq "        name: $release_name" "$release"
done
release_ready_job=$(job_body release-artifacts-ready "$release")
grep -Fq "if: \${{ github.event_name == 'push' && always() }}" <<<"$release_ready_job"
for dependency in c-artifacts swift-source npm-package maven-assemble; do
    grep -Fq "$dependency" <<<"$release_ready_job"
done
maven_stage_job=$(job_body maven-stage "$release")
grep -Fq '        needs: release-artifacts-ready' <<<"$maven_stage_job"
grep -Fq 'central-portal.sh upload build/tex-core-maven-central.zip' <<<"$maven_stage_job"
grep -Fq 'name: release-central-deployment' <<<"$maven_stage_job"
grep -Fq 'if: ${{ always() }}' <<<"$maven_stage_job"
if search 'central-portal\.sh upload' <(job_body maven-assemble "$release"); then
    echo "Maven assembly phase may not publish externally" >&2
    exit 1
fi
search '^            id-token: write$' "$release"
search '^            attestations: write$' "$release"
search 'actions/attest-build-provenance@' "$release"
search 'npm publish \./release-npm/\*\.tgz --access public' "$release"
search '^    resume-publish:$' "$release"
search "if: github.event_name == 'workflow_dispatch'" "$release"
if search 'central-deployment-id:' "$release"; then
    echo "release resume may not accept a Central deployment id from the operator" >&2
    exit 1
fi
search 'github\.event_name.*format.*refs/tags' "$release"
search "jq -r '\\.status'.*= \"completed\"" "$release"
search 'gh run download "\$SOURCE_RUN_ID" --name release-central-deployment' "$release"
search 'bound_tag.*RELEASE_TAG' "$release"
search 'bound_version.*cat VERSION' "$release"
search 'gh run download "\$SOURCE_RUN_ID" --name release-npm-package' "$release"
grep -Fq 'test -s "docs/releases/$(cat VERSION).md"' "$release"
grep -Fq -- '--notes-file "docs/releases/$(cat VERSION).md"' "$release"
if search -- '--generate-notes' "$release"; then
    echo "formal release workflow must use curated release notes" >&2
    exit 1
fi
search 'publishingType=USER_MANAGED' scripts/central-portal.sh
for secret in \
    MAVEN_CENTRAL_USERNAME \
    MAVEN_CENTRAL_PASSWORD \
    MAVEN_SIGNING_KEY \
    MAVEN_SIGNING_PASSWORD; do
    search "secrets\.$secret" "$release"
done
if search 'NODE_AUTH_TOKEN|NPM_TOKEN|secrets\.NPM' "$release"; then
    echo "npm release job must use OIDC rather than a registry token" >&2
    exit 1
fi

# Release dry run: PR-triggered, secret-free, disposable signing key, full
# aggregate audit.
search '^    pull_request:$' "$release_dry_run"
search '^    workflow_dispatch:$' "$release_dry_run"
search '^    contents: read$' "$release_dry_run"
search '^        name: Release Dry Run - Ready$' "$release_dry_run"
search 'sign-maven-publications\.sh build/release-maven-central --ephemeral' "$release_dry_run"
search 'audit-maven-publications\.mjs' "$release_dry_run"
search 'build/release-maven-central --full --signed' "$release_dry_run"
if search 'secrets\.|environment: release|contents: write|id-token: write' "$release_dry_run"; then
    echo "release dry run may not read secrets or request publish permissions" >&2
    exit 1
fi

for workflow in "$ci" "$codeql"; do
    if ! search '^    merge_group:$' "$workflow"; then
        echo "blocking workflow lacks merge_group support: $workflow" >&2
        exit 1
    fi
done
search '^    workflow_call:$' "$ci"
search '^    workflow_dispatch:$' "$ci"

ci_push_trigger=$(job_body push "$ci")
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
if (ruleset.enforcement !== "active") {
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
if (mainPullRequest?.parameters?.required_review_thread_resolution !== true) {
    throw new Error("all pull-request review conversations must be resolved before merge");
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
if (releaseRuleset.enforcement !== "active") {
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
