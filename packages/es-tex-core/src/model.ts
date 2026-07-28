import { RenderDumper } from "./dumper.js";
import { decoderOwned } from "./owned.js";

/**
 * Half-open byte range `[begin, end)` into the compile call's source.
 *
 * Ranges are data obtained from a compiled tree, valid for that compile
 * call's input; the schema contract deliberately does not promise that they
 * are storage-resident node properties across revisions.
 */
export interface SourceRange {
    readonly begin: number;
    readonly end: number;
}

/** The face style a glyph is drawn in (schema `style`). */
export type GlyphStyle = "upright" | "italic";

/** The font family a glyph belongs to (schema `family`). */
export type GlyphFamily =
    "main" | "size1" | "size2" | "size3" | "size4" | "bold" | "textit" | "cal" | "bb" | "sans" | "mono";

/**
 * A horizontal box: the box node of the schema; the compile root is always
 * an `HBox`. Measures are absolute points and exact integer multiples of
 * 2^-16 pt (see `docs/specs/render-tree.md`).
 */
export interface HBox {
    readonly kind: "hbox";
    /** Offset of the box's reference point within its parent, in points. */
    readonly x: number;
    /** Baseline shift within the parent; positive is up. */
    readonly y: number;
    /** The advance of the box's content in points. */
    readonly width: number;
    /** The maximum extent above the baseline over the children, in points. */
    readonly ascent: number;
    /** The maximum extent below the baseline over the children, in points. */
    readonly descent: number;
    /** The source bytes this box was compiled from. */
    readonly src: SourceRange;
    /** Child nodes in source order. */
    readonly children: readonly RenderNode[];
}

/**
 * A leaf glyph, named by Unicode codepoint + style + family + size — never
 * by a private glyph ID.
 */
export interface Glyph {
    readonly kind: "glyph";
    /** Horizontal offset of the reference point from the parent's, in points. */
    readonly x: number;
    /** Vertical offset from the parent baseline, positive upward, in points. */
    readonly y: number;
    /** The Unicode scalar value this glyph renders. */
    readonly codepoint: number;
    /** The face style. */
    readonly style: GlyphStyle;
    /** The font family. */
    readonly family: GlyphFamily;
    /** The em size in points. */
    readonly size: number;
    /** The glyph advance in points. */
    readonly width: number;
    /** Extent above the baseline in points. */
    readonly ascent: number;
    /** Extent below the baseline in points. */
    readonly descent: number;
    /** The italic correction in points; included in a math Ord glyph's advance. */
    readonly italic: number;
    /** The source bytes this glyph was compiled from. */
    readonly src: SourceRange;
}

/**
 * A leaf rule: a solid rectangle. Its reference point sits at its left
 * edge on the parent's baseline shifted by `y`; the ink extends `ascent`
 * up, `descent` down, and `width` right. Today produced as fraction bars
 * and radical bars.
 */
export interface Rule {
    readonly kind: "rule";
    /** Horizontal offset of the reference point from the parent's, in points. */
    readonly x: number;
    /** Vertical offset from the parent baseline, positive upward, in points. */
    readonly y: number;
    /** The ink width in points. */
    readonly width: number;
    /** Ink extent above the reference point in points. */
    readonly ascent: number;
    /** Ink extent below the reference point in points. */
    readonly descent: number;
    /** The source bytes this rule was compiled from. */
    readonly src: SourceRange;
}

/** A leaf kern: a fixed horizontal advance. `width` may be negative. */
export interface Kern {
    readonly kind: "kern";
    /** Horizontal offset of the reference point from the parent's, in points. */
    readonly x: number;
    /** The advance in points; negative widths move the cursor backward. */
    readonly width: number;
    /** The source bytes this kern was compiled from. */
    readonly src: SourceRange;
}

/**
 * One node of the render tree. The union members are exactly the schema's
 * node kinds, discriminated by `kind`, so `switch` statements over a node
 * are exhaustive by construction.
 */
export type RenderNode = HBox | Glyph | Kern | Rule;

function frozenRange(src: SourceRange): SourceRange {
    return Object.freeze({ begin: src.begin, end: src.end });
}

/* Rebuilds a caller-supplied node as a deeply frozen plain-data clone, so
 * a tree constructed through the public `RenderTree` constructor is as
 * immutable as a decoded one — no live aliases, getters, or extra
 * properties survive into the value tree. */
function frozenNode(node: RenderNode): RenderNode {
    switch (node.kind) {
        case "glyph":
            return Object.freeze({
                kind: "glyph",
                x: node.x,
                y: node.y,
                codepoint: node.codepoint,
                style: node.style,
                family: node.family,
                size: node.size,
                width: node.width,
                ascent: node.ascent,
                descent: node.descent,
                italic: node.italic,
                src: frozenRange(node.src)
            });
        case "kern":
            return Object.freeze({ kind: "kern", x: node.x, width: node.width, src: frozenRange(node.src) });
        case "rule":
            return Object.freeze({
                kind: "rule",
                x: node.x,
                y: node.y,
                width: node.width,
                ascent: node.ascent,
                descent: node.descent,
                src: frozenRange(node.src)
            });
        case "hbox":
            return frozenBox(node);
        default:
            throw new TypeError("RenderTree nodes must be hbox, glyph, kern, or rule values");
    }
}

function frozenBox(box: HBox): HBox {
    return Object.freeze({
        kind: "hbox",
        x: box.x,
        y: box.y,
        width: box.width,
        ascent: box.ascent,
        descent: box.descent,
        src: frozenRange(box.src),
        children: Object.freeze(box.children.map(frozenNode))
    });
}

/**
 * An immutable compiled document: the render tree of schema version 5.
 *
 * Trees are plain frozen values — structurally comparable and detached from
 * the WASM engine the moment `Document.compile` returns. A root passed to
 * the public constructor is snapshotted into a deeply frozen clone, so no
 * caller-held alias can change the tree after construction; decoded roots
 * are already frozen and are adopted as-is.
 */
export class RenderTree {
    /** The root horizontal box; its range spans the whole compile input. */
    readonly root: HBox;

    constructor(root: HBox) {
        this.root = decoderOwned.has(root) ? root : frozenBox(root);
        Object.freeze(this);
    }

    /**
     * Renders the canonical dump (`docs/specs/render-tree-dump.md`):
     * byte-identical across platforms for the same compiled tree.
     */
    dump(): string {
        return RenderDumper.dump(this);
    }
}
