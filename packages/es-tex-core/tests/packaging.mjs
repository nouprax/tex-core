import assert from "node:assert/strict";
import { copyFile, mkdtemp, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

const packageDirectory = path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const readmeSnippetTimeoutMs = 10_000;
const mode = process.argv[2] ?? "all";
if (!["all", "consumer", "packaging", "types"].includes(mode)) throw new Error(`unknown mode: ${mode}`);
const temporary = await mkdtemp(path.join(tmpdir(), "es-tex-core-consumer-"));
try {
    const packed = spawnSync("npm", ["pack", packageDirectory, "--json", "--pack-destination", temporary], {
        encoding: "utf8",
        env: { ...process.env, npm_config_cache: path.join(temporary, "npm-cache") }
    });
    if (packed.status !== 0) throw new Error(packed.stderr);
    const report = JSON.parse(packed.stdout)[0];
    if (mode === "all" || mode === "packaging") {
        const files = report.files.map((file) => file.path).sort();
        const sourceFiles = await sourceArtifactFiles(path.join(packageDirectory, "src"));
        const expected = [
            "LICENSE",
            "README.md",
            ...sourceFiles.map((file) => `dist/${file}`),
            "dist/tex-core.wasm",
            "package.json"
        ].sort();
        assert.deepEqual(files, expected);
        const manifest = JSON.parse(await readFile(path.join(packageDirectory, "package.json"), "utf8"));
        assert.deepEqual(Object.keys(manifest.exports), [".", "./tex-core.wasm"]);
        console.log("packaging: npm artifact contents and exports passed");
    }

    if (mode !== "packaging") {
        await writeFile(path.join(temporary, "package.json"), JSON.stringify({ type: "module", private: true }));
        const installed = spawnSync("npm", ["install", "--ignore-scripts", `./${report.filename}`], {
            cwd: temporary,
            encoding: "utf8",
            env: { ...process.env, npm_config_cache: path.join(temporary, "npm-cache") }
        });
        if (installed.status !== 0) throw new Error(installed.stderr);
    }

    if (mode === "all" || mode === "types") {
        await copyFile(path.join(packageDirectory, "tests/types/consumer.ts"), path.join(temporary, "consumer.ts"));
        await copyFile(path.join(packageDirectory, "tests/types/tsconfig.json"), path.join(temporary, "tsconfig.json"));
        const typecheck = spawnSync(
            path.resolve(packageDirectory, "../../node_modules/.bin/tsc"),
            ["-p", "tsconfig.json"],
            { cwd: temporary, encoding: "utf8" }
        );
        if (typecheck.status !== 0) throw new Error(typecheck.stdout + typecheck.stderr);
        console.log("types: packed npm artifact resolved through exports.types successfully");
    }

    if (mode === "all" || mode === "consumer") {
        const consumer = spawnSync(
            "node",
            [
                "--input-type=module",
                "--eval",
                [
                    "import * as api from '@nouprax/es-tex-core';",
                    "const tree = api.Document.compile('x', { mode: 'mathInline' });",
                    "if (tree.root.children[0].kind !== 'glyph') process.exit(2);",
                    "if (!tree.dump().startsWith('render-tree 5\\n')) process.exit(3);",
                    "if ('memory' in api || 'native' in api) process.exit(4);",
                    "const deep = await import('@nouprax/es-tex-core/dist/wire.js').then(() => true, () => false);",
                    "if (deep) process.exit(5);",
                    "const wasm = await import.meta.resolve('@nouprax/es-tex-core/tex-core.wasm');",
                    "if (!wasm.endsWith('tex-core.wasm')) process.exit(6);"
                ].join("\n")
            ],
            { cwd: temporary, encoding: "utf8" }
        );
        if (consumer.status !== 0) throw new Error(consumer.stderr || `consumer exited ${consumer.status}`);
        console.log("consumer: packed npm artifact imported, compiled, and blocked deep imports");

        // Execute every package-importing README snippet against the packed
        // artifact so documented export names and API shapes cannot drift.
        const readmes = [path.resolve(packageDirectory, "../../README.md"), path.join(packageDirectory, "README.md")];
        for (const readme of readmes) {
            const text = await readFile(readme, "utf8");
            const snippets = [...text.matchAll(/```js\n([\s\S]*?)```/g)]
                .map((match) => match[1])
                .filter((snippet) => snippet.includes("@nouprax/es-tex-core"));
            if (snippets.length === 0) throw new Error(`no runnable package snippet found in ${readme}`);
            for (const snippet of snippets) {
                const ran = spawnSync("node", ["--input-type=module", "--eval", snippet], {
                    cwd: temporary,
                    encoding: "utf8",
                    timeout: readmeSnippetTimeoutMs
                });
                if (ran.status !== 0) {
                    const details = [
                        ran.error?.code === "ETIMEDOUT"
                            ? `timed out after ${readmeSnippetTimeoutMs} ms`
                            : `exit status: ${ran.status}`,
                        ran.signal === null ? null : `signal: ${ran.signal}`,
                        ran.error === undefined ? null : `spawn error: ${ran.error.message}`,
                        ran.stderr ? `stderr:\n${ran.stderr.trimEnd()}` : null,
                        ran.stdout ? `stdout:\n${ran.stdout.trimEnd()}` : null
                    ].filter((detail) => detail !== null);
                    throw new Error(`README snippet failed in ${readme}:\n${details.join("\n")}`);
                }
            }
        }
        console.log("consumer: README snippets ran against the packed artifact");
    }
} finally {
    await rm(temporary, { recursive: true, force: true });
}

async function sourceArtifactFiles(directory, prefix = "") {
    const result = [];
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const relative = path.join(prefix, entry.name);
        if (entry.isDirectory()) {
            result.push(...(await sourceArtifactFiles(path.join(directory, entry.name), relative)));
        } else if (entry.name.endsWith(".ts")) {
            const modulePath = relative.slice(0, -3).split(path.sep).join("/");
            result.push(`${modulePath}.js`, `${modulePath}.d.ts`);
        }
    }
    return result;
}
