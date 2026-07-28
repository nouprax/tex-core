import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { CompileError, Document, accept } from "../dist/index.js";

const packageDirectory = path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const bundle = JSON.parse(
    await readFile(path.join(packageDirectory, "build/generated/conformance/render-tree-fixtures.json"), "utf8")
);
assert.equal(bundle.schemaVersion, 5);
assert.ok(bundle.cases.length > 0);

function errorRecord(error) {
    const status = {
        invalidArgument: "invalid-argument",
        invalidUtf8: "invalid-utf8",
        unsupported: "unsupported",
        allocationFailed: "allocation-failed"
    }[error.status];
    const src = error.range ? `${error.range.begin}..${error.range.end}` : "none";
    return `render-error 5\nerror status=${status} src=${src} message=${error.errorMessage}\n`;
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

test("conformance: the corpus reaches every public node kind through accept", () => {
    // Traversal goes through the public visitor dispatch, so the corpus
    // exercises all four visit methods, not just the discriminant field.
    const kinds = new Set();
    const visitor = {
        visitHBox(node) {
            kinds.add("hbox");
            for (const child of node.children) accept(child, visitor);
        },
        visitGlyph() {
            kinds.add("glyph");
        },
        visitKern() {
            kinds.add("kern");
        },
        visitRule() {
            kinds.add("rule");
        }
    };
    for (const testCase of bundle.cases) {
        if (testCase.outcome !== "tree") continue;
        const source = Uint8Array.from(Buffer.from(testCase.sourceBase64, "base64"));
        accept(Document.compile(source, { mode: testCase.mode }).root, visitor);
    }
    assert.deepEqual([...kinds].sort(), ["glyph", "hbox", "kern", "rule"]);
});
