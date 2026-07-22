# @nouprax/es-tex-core

ECMAScript and TypeScript bindings for TeX Core: `Document.compile` turns
self-contained LaTeX source (or a bare math fragment) into an immutable
render tree with fully resolved layout, powered by the C engine compiled to
standalone WASM. The tree carries the semantic and geometric views of the
canonical schema (`docs/specs/render-tree.md`); `tree.dump()` reproduces
the canonical dump byte for byte with every other TeX Core binding. This
library never draws.

```js
import { Document } from "@nouprax/es-tex-core";

const tree = Document.compile("E = mc^2", { mode: "mathInline" });
for (const node of tree.root.children) {
    // node.kind is "hbox" | "glyph" | "kern" — an exhaustive union.
}
```

- ESM-only, `sideEffects: false`, Node ≥ 20 or any modern browser; the
  WASM asset resolves through the `./tex-core.wasm` export and deep imports
  are blocked by the `exports` map.
- Compile errors are structured `CompileError` values (status, byte range,
  deterministic message) — fail-fast, never partial output.
- `Uint8Array` sources compile byte-exact; strings encode as UTF-8.
