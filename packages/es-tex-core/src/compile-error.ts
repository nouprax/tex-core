import type { SourceRange } from "./model.js";

/** The failure category of a compile call, mirroring `tex_core_status`. */
export type CompileStatus = "invalidArgument" | "invalidUtf8" | "unsupported" | "allocationFailed";

/**
 * The structured fail-fast compile error: a failed
 * compile produces no tree, only this error naming the offending bytes.
 */
export class CompileError extends Error {
    /** The failure category. */
    readonly status: CompileStatus;
    /** The source bytes the error names, when the failure has a location. */
    readonly range: SourceRange | null;
    /** The deterministic ASCII description, e.g. `unsupported command \frac`. */
    readonly errorMessage: string;

    constructor(status: CompileStatus, range: SourceRange | null, errorMessage: string) {
        super(range ? `${errorMessage} (bytes ${range.begin}..${range.end})` : errorMessage);
        this.name = "CompileError";
        this.status = status;
        // Snapshot and freeze: the whole error is a frozen value, so its
        // range must not stay a live alias of a caller-mutable object.
        this.range = range === null ? null : Object.freeze({ begin: range.begin, end: range.end });
        this.errorMessage = errorMessage;
        Object.freeze(this);
    }
}
