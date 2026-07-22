import type { CompileOptions } from "./options.js";
import { nativeMode } from "./options.js";
import type { RenderTree } from "./model.js";
import { decodePayload } from "./wire.js";
import { native } from "./runtime.js";

/**
 * The compile entry point: UTF-8 LaTeX source in, an immutable render tree
 * out. The WASM engine validates, parses, and lays out the source; the
 * returned tree is a detached frozen value and no engine handle survives
 * the call. This library never draws.
 *
 * Strings are encoded as UTF-8; a `Uint8Array` compiles byte-exact, so the
 * engine's encoding validation (a structured `CompileError`, never a silent
 * replacement) is reachable from callers holding raw bytes.
 */
export const Document = {
    compile(source: string | Uint8Array, options?: CompileOptions): RenderTree {
        const bytes = typeof source === "string" ? new TextEncoder().encode(source) : source;
        const pointer = bytes.length > 0 ? native.malloc(bytes.length) : 0;
        if (bytes.length > 0 && pointer === 0) throw new Error("native source allocation failed");
        let payload = 0;
        try {
            if (bytes.length > 0) {
                new Uint8Array(native.memory.buffer, pointer, bytes.length).set(bytes);
            }
            payload = native.es_compile(pointer, bytes.length, nativeMode(options));
            if (payload === 0) throw new Error("native render-tree copy failed");
            const data = native.es_payload_data(payload);
            const length = native.es_payload_length(payload);
            // Copy out before free: the decoded tree must never alias wasm
            // memory.
            const copy = new Uint8Array(length);
            copy.set(new Uint8Array(native.memory.buffer, data, length));
            return decodePayload(copy);
        } finally {
            if (payload !== 0) native.es_payload_free(payload);
            if (pointer !== 0) native.free(pointer);
        }
    }
};
