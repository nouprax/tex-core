/** The input form of one compile call (schema `options.mode`). */
export type CompileMode = "document" | "mathInline" | "mathDisplay";

/** Options of one compile call. Every field has a frozen default. */
export interface CompileOptions {
    /** The input form; the default is `document`. */
    readonly mode?: CompileMode;
}

export function nativeMode(options: CompileOptions | undefined): number {
    const mode = options?.mode ?? "document";
    switch (mode) {
        case "document":
            return 0;
        case "mathInline":
            return 1;
        case "mathDisplay":
            return 2;
        default:
            // Unreachable for TypeScript callers; a plain-JavaScript typo
            // must fail loudly instead of coercing to document mode.
            throw new Error(`unknown compile mode: ${String(mode)}`);
    }
}
