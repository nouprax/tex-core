import { CompileError, Document, RenderTree, accept } from "@nouprax/es-tex-core";
import type { CompileOptions, Glyph, HBox, Kern, RenderNode, RenderVisitor, Rule } from "@nouprax/es-tex-core";

const options: CompileOptions = { mode: "mathInline" };
const tree: RenderTree = Document.compile("x", options);
const root: HBox = tree.root;
const first: RenderNode = root.children[0] as RenderNode;

const visitor: RenderVisitor<string> = {
    visitHBox(node: HBox): string {
        return `hbox:${node.children.length}`;
    },
    visitGlyph(node: Glyph): string {
        return `glyph:${node.codepoint.toString(16)}:${node.style}`;
    },
    visitKern(node: Kern): string {
        return `kern:${node.width}`;
    },
    visitRule(node: Rule): string {
        return `rule:${node.width}`;
    }
};
const described: string = accept(first, visitor);
if (described.length === 0) throw new Error("unreachable");

try {
    Document.compile(new Uint8Array([0xff]));
} catch (error) {
    if (error instanceof CompileError && error.range !== null) {
        const begin: number = error.range.begin;
        if (begin < 0) throw new Error("unreachable", { cause: error });
    }
}

const dumped: string = tree.dump();
if (!dumped.startsWith("render-tree 3")) throw new Error("unexpected dump header");
