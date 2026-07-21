import type { Glyph, HBox, Kern, RenderNode } from "./model.js";

/**
 * An exhaustive typed visitor over render nodes: one method per schema node
 * kind, so adding a kind is a source-breaking schema change, never a
 * silently unhandled case.
 */
export interface RenderVisitor<R> {
    visitHBox(node: HBox): R;
    visitGlyph(node: Glyph): R;
    visitKern(node: Kern): R;
}

/** Dispatches a node to the matching visitor method. */
export function accept<R>(node: RenderNode, visitor: RenderVisitor<R>): R {
    switch (node.kind) {
        case "hbox":
            return visitor.visitHBox(node);
        case "glyph":
            return visitor.visitGlyph(node);
        case "kern":
            return visitor.visitKern(node);
    }
}
