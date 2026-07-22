import { mkdir, readFile, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const packageDirectory = path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const repositoryRoot = path.resolve(packageDirectory, "../..");
const specDirectory = path.join(repositoryRoot, "specs/render-tree");
const output = path.join(packageDirectory, "build/generated/conformance/render-tree-fixtures.json");

const manifest = JSON.parse(await readFile(path.join(specDirectory, "manifest.json"), "utf8"));
if (manifest.schemaVersion !== 1 || !Array.isArray(manifest.cases) || manifest.cases.length === 0) {
    throw new Error("shared render-tree manifest v1 must contain at least one case");
}

async function readFixture(relativePath) {
    if (typeof relativePath !== "string" || path.isAbsolute(relativePath)) {
        throw new Error(`fixture paths must be relative: ${relativePath}`);
    }
    const resolved = path.resolve(specDirectory, relativePath);
    if (!resolved.startsWith(specDirectory + path.sep)) {
        throw new Error(`fixture path escapes the spec directory: ${relativePath}`);
    }
    return readFile(resolved);
}

const cases = [];
for (const testCase of manifest.cases) {
    if (testCase.input !== `${testCase.name}.tex` || testCase.expected !== `${testCase.name}.tree`) {
        throw new Error(`${testCase.name} has non-canonical fixture paths`);
    }
    // Inputs are byte-exact compile input (one case is deliberately invalid
    // UTF-8), so they travel as base64; expected dumps are UTF-8 text.
    cases.push({
        name: testCase.name,
        sourceBase64: (await readFixture(testCase.input)).toString("base64"),
        expected: (await readFixture(testCase.expected)).toString("utf8"),
        mode: testCase.compileOptions.mode,
        outcome: testCase.outcome
    });
}

await mkdir(path.dirname(output), { recursive: true });
await writeFile(output, `${JSON.stringify({ schemaVersion: manifest.schemaVersion, cases }, null, 2)}\n`);
process.stdout.write(`Bundled ${cases.length} render-tree conformance cases.\n`);
