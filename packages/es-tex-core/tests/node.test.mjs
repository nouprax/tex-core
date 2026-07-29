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
    // A fraction with a scripted radical produces every node kind, so all
    // four visitor methods must fire — including visitRule.
    const tree = Document.compile("\\frac{a}{2} \\sqrt{x} b", { mode: "mathInline" });
    const counts = { hbox: 0, glyph: 0, kern: 0, rule: 0 };
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
        },
        visitRule() {
            counts.rule += 1;
        }
    };
    accept(tree.root, visitor);
    assert.ok(counts.hbox > 0, "hbox nodes visited");
    assert.ok(counts.glyph > 0, "glyph nodes visited");
    assert.ok(counts.kern > 0, "kern nodes visited");
    assert.ok(counts.rule > 0, "rule nodes visited");
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

test("immutability: compile-error ranges are frozen values", () => {
    let failure;
    try {
        Document.compile("\\frobnicate", { mode: "mathInline" });
    } catch (error) {
        failure = error;
    }
    assert.ok(failure instanceof CompileError);
    assert.ok(Object.isFrozen(failure), "the error object is frozen");
    assert.ok(Object.isFrozen(failure.range), "the error range is frozen");
    assert.throws(() => {
        failure.range.begin = 99;
    }, TypeError);
});

test("immutability: a publicly constructed tree snapshots its root", async () => {
    const { RenderTree } = await import("../dist/index.js");
    const aliasChildren = [];
    const aliasRoot = {
        kind: "hbox",
        x: 0,
        y: 0,
        width: 0,
        ascent: 0,
        descent: 0,
        src: { begin: 0, end: 0 },
        children: aliasChildren
    };
    const tree = new RenderTree(aliasRoot);
    aliasChildren.push({ kind: "kern", x: 0, width: 1, src: { begin: 0, end: 0 } });
    aliasRoot.width = 42;
    assert.equal(tree.root.children.length, 0, "later alias pushes do not reach the tree");
    assert.equal(tree.root.width, 0, "later alias writes do not reach the tree");
    assert.ok(Object.isFrozen(tree.root), "the adopted root is frozen");
    assert.ok(Object.isFrozen(tree.root.children), "the adopted children are frozen");
    assert.ok(Object.isFrozen(tree.root.src), "the adopted range is frozen");
});

test("immutability: deeply nested caller trees snapshot without growing the JavaScript stack", async () => {
    const { RenderTree } = await import("../dist/index.js");
    const depth = 12_000;
    let root = hbox([]);
    for (let index = 1; index < depth; index += 1) root = hbox([root]);
    const tree = new RenderTree(root);
    let node = tree.root;
    let frozen = 1;
    while (node.children.length === 1) {
        node = node.children[0];
        assert.equal(node.kind, "hbox");
        frozen += 1;
    }
    assert.equal(frozen, depth);
});

test("immutability: decoded trees are deeply frozen", () => {
    const tree = Document.compile("\\frac{a}{2}", { mode: "mathInline" });
    const stack = [tree.root];
    while (stack.length > 0) {
        const node = stack.pop();
        assert.ok(Object.isFrozen(node), `${node.kind} nodes are frozen`);
        assert.ok(Object.isFrozen(node.src), `${node.kind} ranges are frozen`);
        if (node.kind === "hbox") {
            assert.ok(Object.isFrozen(node.children), "children lists are frozen");
            stack.push(...node.children);
        }
    }
});

test("wire: malformed payloads fail closed instead of defaulting", async () => {
    const { decodePayload } = await import("../dist/wire.js");
    const record = (build) => {
        const buffer = new ArrayBuffer(4096);
        const view = new DataView(buffer);
        const length = build(view);
        return new Uint8Array(buffer, 0, length);
    };
    const node = (view, at, fields) => {
        view.setUint32(at, fields.kind, true);
        view.setUint32(at + 52, fields.codepoint ?? 0, true);
        view.setUint32(at + 56, fields.style ?? 0, true);
        view.setUint32(at + 60, fields.family ?? 0, true);
        view.setUint32(at + 88, fields.children ?? 0, true);
        return at + 92;
    };
    const success = (view, fields, count = 1) => {
        view.setUint32(0, 0, true);
        view.setUint32(4, count, true);
        return node(view, 8, fields);
    };

    // Unknown enum values must throw, never map to a plausible default.
    assert.throws(() => decodePayload(record((view) => success(view, { kind: 9 }))), /unknown node kind/);
    assert.throws(
        () => decodePayload(record((view) => success(view, { kind: 2, family: 99 }))),
        /unknown glyph family/
    );
    assert.throws(() => decodePayload(record((view) => success(view, { kind: 2, style: 7 }))), /unknown glyph style/);
    assert.throws(
        () => decodePayload(record((view) => success(view, { kind: 2, codepoint: 0xd800 }))),
        /non-scalar codepoint/
    );
    // A leaf may not carry children; the declared node count must match.
    assert.throws(() => decodePayload(record((view) => success(view, { kind: 3, children: 1 }))), /leaf node/);
    assert.throws(() => decodePayload(record((view) => success(view, { kind: 1 }, 2))), /declares 2 nodes/);
    // Trailing bytes are a protocol failure on both record shapes.
    assert.throws(() => decodePayload(record((view) => success(view, { kind: 1 }) + 1)), /trailing bytes/);
    assert.throws(
        () =>
            decodePayload(
                record((view) => {
                    view.setUint32(0, 3, true);
                    view.setUint32(4, 0, true);
                    view.setUint32(24, 0, true);
                    return 29;
                })
            ),
        /trailing bytes/
    );
    // An unknown status is a protocol failure, not invalidArgument.
    assert.throws(
        () =>
            decodePayload(
                record((view) => {
                    view.setUint32(0, 250, true);
                    view.setUint32(4, 0, true);
                    view.setUint32(24, 0, true);
                    return 28;
                })
            ),
        /unknown status/
    );
});

test("wire: deeply nested payloads decode without growing the JavaScript stack", async () => {
    const { decodePayload } = await import("../dist/wire.js");
    const depth = 12_000;
    const tree = decodePayload(nestedHBoxPayload(depth));
    let node = tree.root;
    let decoded = 1;
    while (node.children.length === 1) {
        node = node.children[0];
        assert.equal(node.kind, "hbox");
        decoded += 1;
    }
    assert.equal(decoded, depth);
});

test("dump: deeply nested trees do not grow the JavaScript stack", async () => {
    const { decodePayload } = await import("../dist/wire.js");
    const depth = 4_096;
    const dump = decodePayload(nestedHBoxPayload(depth)).dump();
    assert.ok(dump.startsWith("render-tree 5\n"));
    assert.ok(dump.length > depth);
});

function nestedHBoxPayload(depth) {
    const nodeSize = 92;
    const payload = new Uint8Array(8 + depth * nodeSize);
    const view = new DataView(payload.buffer);
    view.setUint32(0, 0, true);
    view.setUint32(4, depth, true);
    for (let index = 0; index < depth; index += 1) {
        const at = 8 + index * nodeSize;
        view.setUint32(at, 1, true);
        view.setUint32(at + 88, index + 1 < depth ? 1 : 0, true);
    }
    return payload;
}

function hbox(children) {
    return { kind: "hbox", x: 0, y: 0, width: 0, ascent: 0, descent: 0, src: { begin: 0, end: 0 }, children };
}
