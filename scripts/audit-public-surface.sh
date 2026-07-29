#!/bin/sh
# Public-surface audit: pins the reviewed API of every binding so drift is a
# red diff here before it is a compatibility break for a consumer. The
# render tree is a read-only compiled artifact — no mutating, rendering, or
# native-handle vocabulary may go public on any platform.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

fail() {
    echo "Public surface audit failed: $1" >&2
    exit 1
}

temp_dir=$(mktemp -d)
trap 'rm -rf "$temp_dir"' EXIT

# C: exactly the reviewed facade headers, and exactly the reviewed symbols.
public_headers=$(find packages/tex-core/include -maxdepth 1 -type f -print | sort)
expected_headers='packages/tex-core/include/tex-core-export.h
packages/tex-core/include/tex-core-version.h
packages/tex-core/include/tex_core.h'
if [ "$public_headers" != "$expected_headers" ]; then
    printf '%s\n' "$public_headers" >&2
    fail "the C package must install exactly the reviewed facade headers"
fi
declared=$(sed -n 's/.*TEX_CORE_API[^(]*[ *]\(tex_core_[a-z0-9_]*\)(.*/\1/p' \
    packages/tex-core/include/tex_core.h | sort -u)
expected_declared='tex_core_document_compile
tex_core_dump_free
tex_core_node_child
tex_core_node_child_count
tex_core_node_frame
tex_core_node_get_kind
tex_core_node_glyph
tex_core_node_kind_name
tex_core_node_range
tex_core_options_init
tex_core_render_tree_dump
tex_core_render_tree_free
tex_core_render_tree_root
tex_core_version_string'
if [ "$declared" != "$expected_declared" ]; then
    printf '%s\n' "$declared" >&2
    fail "the C facade symbol set drifted from the reviewed pin"
fi
# render_tree is the product noun (the compiled artifact), not a rendering
# action; strip it before scanning for forbidden verbs.
if printf '%s\n' "$declared" | sed 's/render_tree/rendertree/g' \
    | grep -E '_(set|insert|append|prepend|replace|unlink|new|render)_'; then
    fail "mutating or rendering C symbol is public"
fi

# Swift: one product, no mutation/native vocabulary, exhaustive visitor.
CLANG_MODULE_CACHE_PATH="$temp_dir/swift-module-cache" \
    swift package --disable-sandbox dump-package >"$temp_dir/swift-package.json"
node - "$temp_dir/swift-package.json" <<'NODE'
import fs from "node:fs";

const manifest = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const products = manifest.products.map((product) => `${product.name}:${product.targets.join(",")}`);
if (products.join("\n") !== "TexCore:TexCore") {
    throw new Error(`Unexpected SwiftPM products: ${products.join(", ")}`);
}
NODE
if grep -R -n -E \
    'public (func|var|let|static func).*\b(render[A-Z(]|set[A-Z]|insert|append|prepend|replace|unlink|nativeHandle|pointer|memory|wasm)' \
    packages/swift-tex-core/Sources/TexCore; then
    fail "Swift exports mutation, renderer, or native implementation details"
fi
grep -q 'public func dump() -> String' packages/swift-tex-core/Sources/TexCore/RenderTree.swift \
    || fail "Swift does not expose the reviewed RenderTree.dump API"
grep -q 'extension CompileError: LocalizedError' packages/swift-tex-core/Sources/TexCore/CompileError.swift \
    && grep -q 'lint --strict' scripts/format-swift.sh \
    || fail "Swift errors or public documentation linting are not Foundation/CI complete"
if grep -R -n 'defaultVisit' packages/swift-tex-core/Sources/TexCore; then
    fail "Swift RenderVisitor exposes a catch-all fallback"
fi
test "$(grep -c 'mutating func visit' packages/swift-tex-core/Sources/TexCore/RenderTree.swift)" -eq 4 \
    || fail "Swift RenderVisitor is not exhaustive over all 4 node kinds"

# Kotlin: explicit API, no mutation/native vocabulary, exhaustive visitor.
grep -q 'explicitApi()' packages/kotlin-tex-core/build.gradle.kts \
    || fail "Kotlin explicit API mode is disabled"
grep -q 'alias(libs.plugins.dokka)' packages/kotlin-tex-core/build.gradle.kts \
    && grep -q 'dokkaGeneratePublicationHtml' packages/kotlin-tex-core/build.gradle.kts \
    || fail "Kotlin javadoc artifact does not contain generated API reference"
grep -q 'verifyJvmAbi' packages/kotlin-tex-core/build.gradle.kts \
    && test -s packages/kotlin-tex-core/jvm-abi.txt \
    || fail "Kotlin JVM Java-visible ABI snapshot is missing"
grep -q 'class HostTriple' packages/kotlin-tex-core/build.gradle.kts \
    && grep -q 'binaryArchitecture' packages/kotlin-tex-core/build.gradle.kts \
    || fail "Kotlin native packaging lacks the closed host/architecture checks"
grep -q '@kotlin.jvm.JvmStatic' packages/kotlin-tex-core/src/commonMain/kotlin/com/nouprax/tex/core/Document.kt \
    && grep -q '@kotlin.jvm.JvmOverloads' \
        packages/kotlin-tex-core/src/commonMain/kotlin/com/nouprax/tex/core/model/CompileOptions.kt \
    && grep -q 'Document.compile' packages/kotlin-tex-core/consumers/jvm-maven/src/main/java/consumer/Main.java \
    || fail "Kotlin Java facade is not directly callable with default arguments"
grep -q 'getResourceAsStream' \
    packages/kotlin-tex-core/src/androidMain/kotlin/com/nouprax/tex/core/CBridge.android.kt \
    || fail "Kotlin Android host loader lacks the classpath fallback"
if grep -R -n -E \
    'public (fun|val|var).*\b(render[A-Z(]|set[A-Z]|insert|append|prepend|replace|unlink|nativeHandle|pointer|memory|wasm)' \
    packages/kotlin-tex-core/src/commonMain; then
    fail "Kotlin exports mutation, renderer, or native implementation details"
fi
grep -q 'public fun dump(): String' \
    packages/kotlin-tex-core/src/commonMain/kotlin/com/nouprax/tex/core/model/RenderTree.kt \
    || fail "Kotlin does not expose the reviewed RenderTree.dump API"
if grep -R -n 'defaultVisit' packages/kotlin-tex-core/src/commonMain; then
    fail "Kotlin RenderVisitor exposes a catch-all fallback"
fi
test "$(grep -c 'public fun visit' \
    packages/kotlin-tex-core/src/commonMain/kotlin/com/nouprax/tex/core/walker/RenderVisitor.kt)" -eq 4 \
    || fail "Kotlin RenderVisitor is not exhaustive over all 4 node kinds"

# ES: pinned exports map and runtime surface, exhaustive visitor, and a
# types consumer that resolves only through the published exports.
node - packages/es-tex-core/package.json packages/es-tex-core/src/index.ts <<'NODE'
import fs from "node:fs";

const [, , manifestPath, runtimePath] = process.argv;
const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
const rootExport = manifest.exports?.["."];
const exportKeys = Object.keys(manifest.exports ?? {}).sort();
if (exportKeys.join("\n") !== ".\n./tex-core.wasm") {
    throw new Error(`Unexpected npm export paths: ${exportKeys.join(", ")}`);
}
if (rootExport?.types !== "./dist/index.d.ts" || rootExport?.import !== "./dist/index.js") {
    throw new Error("npm root export does not point at the reviewed ESM and declaration files");
}
const runtime = fs.readFileSync(runtimePath, "utf8");
const runtimeExports = [
    ...[...runtime.matchAll(/^export \{ ([^}]+) \} from /gm)].flatMap((match) =>
        match[1].split(",").map((name) => name.trim())
    )
].sort();
const expectedRuntime = ["CompileError", "Document", "RenderTree", "accept"].sort();
if (runtimeExports.join("\n") !== expectedRuntime.join("\n")) {
    throw new Error(`Unexpected ES runtime exports: ${runtimeExports.join(", ")}`);
}
NODE
test "$(grep -c '    visit[A-Z][A-Za-z]*(node:' packages/es-tex-core/src/visitor.ts)" -eq 4 \
    || fail "ES RenderVisitor is not exhaustive over all 4 node kinds"
if grep -R -E -n 'defaultVisit|visit[A-Z][A-Za-z]+\?' packages/es-tex-core/src; then
    fail "ES RenderVisitor exposes a catch-all or optional typed handlers"
fi
if grep -q '"paths"' packages/es-tex-core/tests/types/tsconfig.json \
    || grep -R -n -E '(\.\./)+dist/index\.d\.ts' packages/es-tex-core/tests/types; then
    fail "ES type consumer bypasses installed-package exports.types resolution"
fi
grep -q '"node": ">=24"' packages/es-tex-core/package.json \
    && grep -q 'README snippets ran against the packed artifact' packages/es-tex-core/tests/packaging.mjs \
    || fail "ES runtime floor or executable README contract drifted"

# Coordinates the audits already pinned per package.
grep -q '^group = "com.nouprax"$' packages/kotlin-tex-core/build.gradle.kts \
    || fail "Kotlin group coordinate drifted"
grep -q 'artifactId = "kotlin-tex-core-android-runtime"' \
    packages/kotlin-tex-core/android-runtime/build.gradle.kts \
    || fail "internal Android runtime coordinate drifted"

echo "Public surface audit passed."
