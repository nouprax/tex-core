/**
 * Internal ownership registry (not exported from the package): roots the
 * wire decoder built and already deep-froze. `RenderTree` accepts these
 * as-is; any other root is defensively cloned and frozen, so the public
 * constructor cannot smuggle mutable state into a "frozen" tree.
 */
export const decoderOwned: WeakSet<object> = new WeakSet();
