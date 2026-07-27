import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { CompileError, Document } from "../dist/index.js";

const packageDirectory = path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const bundle = JSON.parse(
    await readFile(path.join(packageDirectory, "build/generated/conformance/render-tree-fixtures.json"), "utf8")
);
assert.equal(bundle.schemaVersion, 2);
assert.ok(bundle.cases.length > 0);

function errorRecord(error) {
    const status = {
        invalidArgument: "invalid-argument",
        invalidUtf8: "invalid-utf8",
        unsupported: "unsupported",
        allocationFailed: "allocation-failed"
    }[error.status];
    const src = error.range ? `${error.range.begin}..${error.range.end}` : "none";
    return `render-error 2\nerror status=${status} src=${src} message=${error.errorMessage}\n`;
}

test("conformance: every manifest case matches the shared render-tree spec", () => {
    for (const testCase of bundle.cases) {
        const source = Uint8Array.from(Buffer.from(testCase.sourceBase64, "base64"));
        const options = { mode: testCase.mode };
        if (testCase.outcome === "tree") {
            const tree = Document.compile(source, options);
            assert.equal(tree.dump(), testCase.expected, testCase.name);
            assert.equal(tree.dump(), tree.dump(), `${testCase.name} determinism`);
        } else {
            let failure;
            try {
                Document.compile(source, options);
            } catch (error) {
                failure = error;
            }
            assert.ok(failure instanceof CompileError, `${testCase.name}: compile must fail`);
            assert.equal(errorRecord(failure), testCase.expected, testCase.name);
        }
    }
});

test("conformance: string compiles agree with byte compiles for UTF-8 sources", () => {
    for (const testCase of bundle.cases) {
        if (testCase.outcome !== "tree") continue;
        const bytes = Buffer.from(testCase.sourceBase64, "base64");
        const tree = Document.compile(bytes.toString("utf8"), { mode: testCase.mode });
        assert.equal(tree.dump(), testCase.expected, testCase.name);
    }
});

test("conformance: the corpus reaches every public node kind", () => {
    const kinds = new Set();
    const collect = (node) => {
        kinds.add(node.kind);
        if (node.kind === "hbox") for (const child of node.children) collect(child);
    };
    for (const testCase of bundle.cases) {
        if (testCase.outcome !== "tree") continue;
        const source = Uint8Array.from(Buffer.from(testCase.sourceBase64, "base64"));
        collect(Document.compile(source, { mode: testCase.mode }).root);
    }
    assert.deepEqual([...kinds].sort(), ["glyph", "hbox", "kern"]);
});
