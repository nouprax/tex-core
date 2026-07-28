import { CompileError } from "./compile-error.js";
import type { CompileStatus } from "./compile-error.js";
import type { GlyphFamily, GlyphStyle, HBox, RenderNode, Rule, SourceRange } from "./model.js";
import { RenderTree } from "./model.js";
import { decoderOwned } from "./owned.js";

/**
 * Decodes the TXC1 payload written by the shared native bridge — the
 * in-process transport between the WASM engine and this decoder, never a
 * public serialization format. A status record becomes a thrown
 * `CompileError`; a preorder node stream becomes the frozen value tree.
 *
 * Decoding fails closed: an unknown status, kind, style, or family, a
 * non-scalar codepoint, a leaf with children, a node-count mismatch, or
 * trailing bytes all mean the engine and this decoder disagree about the
 * protocol, and the result would be a plausible but wrong tree. Each
 * throws instead of defaulting.
 */
export function decodePayload(payload: Uint8Array): RenderTree {
    const reader = new WireReader(payload);
    const status = reader.u32();
    if (status !== 0) {
        const hasRange = reader.u32() !== 0;
        const begin = reader.u53();
        const end = reader.u53();
        const message = reader.text(reader.u32());
        if (!reader.exhausted()) throw new Error("wire error record has trailing bytes");
        throw new CompileError(decodeStatus(status), hasRange ? { begin, end } : null, message);
    }
    const declared = reader.u32();
    const root = reader.node();
    if (root.kind !== "hbox") throw new Error("wire payload root must be an hbox");
    if (reader.decoded !== declared) {
        throw new Error(`wire payload declares ${declared} nodes but carries ${reader.decoded}`);
    }
    if (!reader.exhausted()) throw new Error("wire payload has trailing bytes");
    decoderOwned.add(root);
    return new RenderTree(root);
}

function decodeFamily(family: number): GlyphFamily {
    switch (family) {
        case 0:
            return "main";
        case 1:
            return "size1";
        case 2:
            return "size2";
        case 3:
            return "size3";
        case 4:
            return "size4";
        case 5:
            return "bold";
        case 6:
            return "textit";
        case 7:
            return "cal";
        case 8:
            return "bb";
        case 9:
            return "sans";
        case 10:
            return "mono";
        default:
            throw new Error(`wire payload contains an unknown glyph family: ${family}`);
    }
}

function decodeStyle(style: number): GlyphStyle {
    switch (style) {
        case 0:
            return "upright";
        case 1:
            return "italic";
        default:
            throw new Error(`wire payload contains an unknown glyph style: ${style}`);
    }
}

function decodeStatus(status: number): CompileStatus {
    switch (status) {
        case 1:
            return "invalidArgument";
        case 2:
            return "invalidUtf8";
        case 3:
            return "unsupported";
        case 4:
            return "allocationFailed";
        default:
            throw new Error(`wire payload contains an unknown status: ${status}`);
    }
}

function decodeCodepoint(codepoint: number): number {
    if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
        throw new Error(`wire payload contains a non-scalar codepoint: ${codepoint}`);
    }
    return codepoint;
}

class WireReader {
    private readonly view: DataView;
    private readonly bytes: Uint8Array;
    private position = 0;
    decoded = 0;

    constructor(payload: Uint8Array) {
        this.bytes = payload;
        this.view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
    }

    exhausted(): boolean {
        return this.position === this.bytes.length;
    }

    u32(): number {
        const value = this.view.getUint32(this.position, true);
        this.position += 4;
        return value;
    }

    u53(): number {
        const value = this.view.getBigUint64(this.position, true);
        this.position += 8;
        if (value > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error("wire offset exceeds the safe integer range");
        return Number(value);
    }

    f64(): number {
        const value = this.view.getFloat64(this.position, true);
        this.position += 8;
        return value;
    }

    text(length: number): string {
        if (length > this.bytes.length - this.position) {
            throw new Error("wire error message exceeds the payload");
        }
        const value = new TextDecoder().decode(this.bytes.subarray(this.position, this.position + length));
        this.position += length;
        return value;
    }

    node(): RenderNode {
        this.decoded += 1;
        const kind = this.u32();
        const x = this.f64();
        const y = this.f64();
        const width = this.f64();
        const ascent = this.f64();
        const descent = this.f64();
        const italic = this.f64();
        const codepoint = this.u32();
        const style = decodeStyle(this.u32());
        const family = decodeFamily(this.u32());
        const size = this.f64();
        const src: SourceRange = Object.freeze({ begin: this.u53(), end: this.u53() });
        const childCount = this.u32();
        if (kind !== 1 && childCount !== 0) {
            throw new Error(`wire payload gives a leaf node ${childCount} children`);
        }
        switch (kind) {
            case 2:
                return Object.freeze({
                    kind: "glyph",
                    x,
                    y,
                    codepoint: decodeCodepoint(codepoint),
                    style,
                    family,
                    size,
                    width,
                    ascent,
                    descent,
                    italic,
                    src
                });
            case 3:
                return Object.freeze({ kind: "kern", x, width, src });
            case 4: {
                const rule: Rule = Object.freeze({ kind: "rule", x, y, width, ascent, descent, src });
                return rule;
            }
            case 1: {
                const children: RenderNode[] = [];
                for (let index = 0; index < childCount; index += 1) children.push(this.node());
                const box: HBox = Object.freeze({
                    kind: "hbox",
                    x,
                    y,
                    width,
                    ascent,
                    descent,
                    src,
                    children: Object.freeze(children)
                });
                return box;
            }
            default:
                throw new Error(`wire payload contains an unknown node kind: ${kind}`);
        }
    }
}
