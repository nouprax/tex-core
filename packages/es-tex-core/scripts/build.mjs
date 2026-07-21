import { mkdir, rm } from "node:fs/promises";
import { existsSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

const packageDirectory = path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const root = path.resolve(packageDirectory, "../..");
const dist = path.join(packageDirectory, "dist");
const core = [
    "dump.c",
    "error.c",
    "layout.c",
    "memory.c",
    "metrics.c",
    "parse.c",
    "scaled.c",
    "tex_core.c",
    "token.c",
    "utf8.c"
].map((file) => path.join(root, "packages/tex-core/core", file));
const bridges = [
    path.join(root, "packages/kotlin-tex-core/src/native/tex_core_kotlin_bridge.c"),
    path.join(packageDirectory, "src/bridge.c")
];

// The repo-managed emsdk (docs/toolchains.md pin) wins over any PATH emcc so
// builds are reproducible across machines and CI.
const pinnedEmcc = path.join(root, ".tools/emsdk/upstream/emscripten/emcc");
const emcc = process.env.EMCC ?? (existsSync(pinnedEmcc) ? pinnedEmcc : "emcc");

await rm(dist, { recursive: true, force: true });
await mkdir(dist, { recursive: true });
const output = path.join(dist, "tex-core.wasm");
const exported = ["malloc", "free", "es_compile", "es_payload_data", "es_payload_length", "es_payload_free"].map(
    (name) => `_${name}`
);
const result = spawnSync(
    emcc,
    [
        ...core,
        ...bridges,
        "-O3",
        "-std=c11",
        "-sSTANDALONE_WASM=1",
        "-sALLOW_MEMORY_GROWTH=1",
        "--no-entry",
        `-sEXPORTED_FUNCTIONS=${JSON.stringify(exported)}`,
        "-DTEX_CORE_STATIC_DEFINE",
        `-I${path.join(root, "packages/tex-core/include")}`,
        `-I${path.join(root, "packages/kotlin-tex-core/src/native")}`,
        "-o",
        output
    ],
    {
        cwd: root,
        encoding: "utf8",
        env: { ...process.env, EM_CACHE: path.join(root, "build/emscripten-cache") }
    }
);
if (result.status !== 0) {
    process.stderr.write(result.stdout);
    process.stderr.write(result.stderr);
    process.exit(result.status ?? 1);
}
const typescript = spawnSync(
    path.join(root, "node_modules/.bin/tsc"),
    ["-p", path.join(packageDirectory, "tsconfig.json")],
    { cwd: root, encoding: "utf8" }
);
if (typescript.status !== 0) {
    process.stderr.write(typescript.stdout);
    process.stderr.write(typescript.stderr);
    process.exit(typescript.status ?? 1);
}
