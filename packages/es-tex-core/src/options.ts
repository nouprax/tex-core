/** The input form of one compile call (schema `options.mode`). */
export type CompileMode = "document" | "mathInline" | "mathDisplay";

/** Options of one compile call. Every field has a frozen default. */
export interface CompileOptions {
    /** The input form; the default is `document`. */
    readonly mode?: CompileMode;
}

export function nativeMode(options: CompileOptions | undefined): number {
    switch (options?.mode ?? "document") {
        case "document":
            return 0;
        case "mathInline":
            return 1;
        case "mathDisplay":
            return 2;
    }
}
