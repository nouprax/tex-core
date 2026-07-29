import type { Glyph, HBox, Kern, RenderNode, RenderTree, Rule } from "./model.js";
import type { RenderVisitor } from "./visitor.js";
import { accept } from "./visitor.js";

/**
 * Renders the canonical dump through the public visitor path. The output is
 * byte-deterministic: measures print with Knuth's `print_scaled` algorithm
 * over the exact scaled-point values the engine computed, so an ES dump of
 * a tree equals the C dump of the same compile byte for byte.
 */
export const RenderDumper = {
    dump(tree: RenderTree): string {
        const visitor = new DumpVisitor();
        const stack: Array<{ node: RenderNode; depth: number }> = [{ node: tree.root, depth: 0 }];
        while (stack.length > 0) {
            const frame = stack.pop()!;
            visitor.depth = frame.depth;
            accept(frame.node, visitor);
            if (frame.node.kind === "hbox") {
                for (let index = frame.node.children.length - 1; index >= 0; index -= 1) {
                    stack.push({ node: frame.node.children[index]!, depth: frame.depth + 1 });
                }
            }
        }
        return visitor.output;
    }
};

class DumpVisitor implements RenderVisitor<void> {
    output = "render-tree 5\n";
    depth = 0;

    visitHBox(node: HBox): void {
        this.indent();
        this.output +=
            `hbox x=${measure(node.x)} y=${measure(node.y)}` +
            ` width=${measure(node.width)} ascent=${measure(node.ascent)}` +
            ` descent=${measure(node.descent)} src=${range(node.src)}\n`;
    }

    visitGlyph(node: Glyph): void {
        this.indent();
        this.output +=
            `glyph x=${measure(node.x)} y=${measure(node.y)} cp=${codepoint(node.codepoint)}` +
            ` style=${node.style} family=${node.family} size=${measure(node.size)}` +
            ` width=${measure(node.width)} ascent=${measure(node.ascent)} descent=${measure(node.descent)}` +
            ` italic=${measure(node.italic)} src=${range(node.src)}\n`;
    }

    visitKern(node: Kern): void {
        this.indent();
        this.output += `kern x=${measure(node.x)} width=${measure(node.width)} src=${range(node.src)}\n`;
    }

    visitRule(node: Rule): void {
        this.indent();
        this.output +=
            `rule x=${measure(node.x)} y=${measure(node.y)} width=${measure(node.width)}` +
            ` ascent=${measure(node.ascent)} descent=${measure(node.descent)} src=${range(node.src)}\n`;
    }

    private indent(): void {
        this.output += "  ".repeat(this.depth);
    }
}

function range(src: { begin: number; end: number }): string {
    return `${src.begin}..${src.end}`;
}

function codepoint(value: number): string {
    return `U+${value.toString(16).toUpperCase().padStart(4, "0")}`;
}

/**
 * Knuth's `print_scaled` (TeX §103) over the recovered integer scaled
 * value: published measures are exact multiples of 2^-16 pt, so the
 * recovery is lossless. Every intermediate stays far below 2^53, so plain
 * number arithmetic is exact.
 */
function measure(points: number): string {
    const scaled = points * 65536;
    let value = Math.trunc(scaled < 0 ? scaled - 0.5 : scaled + 0.5);
    let text = "";
    if (value < 0) {
        text += "-";
        value = -value;
    }
    text += `${Math.trunc(value / 65536)}.`;
    let fraction = 10 * (value % 65536) + 5;
    let delta = 10;
    do {
        if (delta > 65536) {
            fraction += 32768 - delta / 2;
        }
        text += `${Math.trunc(fraction / 65536)}`;
        fraction = 10 * (fraction % 65536);
        delta *= 10;
    } while (fraction > delta);
    return `${text}pt`;
}
