import assert from "node:assert/strict";
import test from "node:test";
import { CompileError, Document, accept } from "../dist/index.js";

test("api: compile returns a detached frozen value tree", () => {
    const tree = Document.compile("x", { mode: "mathInline" });
    assert.deepEqual(tree.root.src, { begin: 0, end: 1 });
    assert.equal(tree.root.children.length, 1);
    const glyph = tree.root.children[0];
    assert.equal(glyph.kind, "glyph");
    assert.equal(glyph.codepoint, "x".codePointAt(0));
    assert.equal(glyph.style, "italic");
    assert.equal(glyph.family, "main");
    assert.equal(glyph.size, 10);
    assert.equal(tree.root.width, glyph.width + glyph.italic);
    assert.ok(Object.isFrozen(tree));
    assert.ok(Object.isFrozen(tree.root));
    assert.ok(Object.isFrozen(tree.root.children));
    assert.ok(Object.isFrozen(glyph));
});

test("api: options default to document mode and dumps are canonical", () => {
    const tree = Document.compile("");
    assert.equal(tree.dump(), "render-tree 5\nhbox x=0.0pt y=0.0pt width=0.0pt ascent=0.0pt descent=0.0pt src=0..0\n");
    assert.equal(tree.dump(), tree.dump());
});

test("api: repeated compiles are structurally identical", () => {
    const first = Document.compile("a b");
    const second = Document.compile("a b");
    assert.deepEqual(first.root, second.root);
    assert.notDeepEqual(first.root, Document.compile("a c").root);
});

test("errors: unsupported input throws the structured fail-fast error", () => {
    let failure;
    try {
        Document.compile("\\foo x", { mode: "mathInline" });
    } catch (error) {
        failure = error;
    }
    assert.ok(failure instanceof CompileError);
    assert.equal(failure.status, "unsupported");
    assert.deepEqual(failure.range, { begin: 0, end: 4 });
    assert.equal(failure.errorMessage, "unsupported command \\foo");
    assert.equal(failure.message, "unsupported command \\foo (bytes 0..4)");
});

test("errors: invalid bytes throw the encoding error, never replace", () => {
    let failure;
    try {
        Document.compile(new Uint8Array([0x61, 0xff]));
    } catch (error) {
        failure = error;
    }
    assert.ok(failure instanceof CompileError);
    assert.equal(failure.status, "invalidUtf8");
    assert.deepEqual(failure.range, { begin: 1, end: 2 });
});

test("errors: mode changes the accepted surface", () => {
    Document.compile("a+b", { mode: "mathInline" });
    assert.throws(() => Document.compile("a+b"), CompileError);
});

test("errors: an unknown mode fails loudly instead of coercing to document", () => {
    assert.throws(() => Document.compile("a,b", { mode: "mathinline" }), /unknown compile mode: mathinline/);
});

test("visitor: dispatch is exhaustive over the schema kinds", () => {
    const tree = Document.compile("a b");
    const counts = { hbox: 0, glyph: 0, kern: 0 };
    const visitor = {
        visitHBox(node) {
            counts.hbox += 1;
            for (const child of node.children) accept(child, visitor);
        },
        visitGlyph() {
            counts.glyph += 1;
        },
        visitKern() {
            counts.kern += 1;
        }
    };
    accept(tree.root, visitor);
    assert.deepEqual(counts, { hbox: 1, glyph: 2, kern: 1 });
});

test("robustness: children stay in source order and reject mutation", () => {
    const tree = Document.compile("a \\quad b\\ c\\,d\\quad\n");
    const begins = tree.root.children.map((child) => child.src.begin);
    assert.deepEqual(
        begins,
        [...begins].sort((left, right) => left - right)
    );
    assert.throws(() => {
        tree.root.children.push(tree.root.children[0]);
    }, TypeError);
});

test("unicode: string sources encode as UTF-8 before compiling", () => {
    let failure;
    try {
        Document.compile("π", { mode: "mathInline" });
    } catch (error) {
        failure = error;
    }
    assert.ok(failure instanceof CompileError);
    assert.equal(failure.status, "unsupported");
    assert.deepEqual(failure.range, { begin: 0, end: 2 });
});
