export interface NativeExports extends WebAssembly.Exports {
    readonly memory: WebAssembly.Memory;
    malloc(size: number): number;
    free(pointer: number): void;
    es_compile(source: number, length: number, mode: number): number;
    es_payload_data(payload: number): number;
    es_payload_length(payload: number): number;
    es_payload_free(payload: number): void;
}

const wasmURL = new URL("./tex-core.wasm", import.meta.url);

async function loadWasm(): Promise<WebAssembly.Instance> {
    let bytes: BufferSource;
    if (wasmURL.protocol === "file:") {
        const nodeFileSystem = "node:fs/promises";
        const fileSystem = (await import(nodeFileSystem)) as {
            readFile(url: URL): Promise<Uint8Array>;
        };
        bytes = Uint8Array.from(await fileSystem.readFile(wasmURL)).buffer;
    } else {
        const response = await fetch(wasmURL);
        if (!response.ok) throw new Error(`failed to load TeX Core WASM: ${response.status}`);
        bytes = await response.arrayBuffer();
    }
    // The engine consumes no clock, file, or process facility; the WASI
    // stubs exist only because a standalone-WASM libc imports them.
    const wasi = {
        clock_time_get: (): number => 0,
        fd_close: (): number => 0,
        fd_seek: (): number => 0,
        fd_write: (): number => 0,
        proc_exit: (code: number): never => {
            throw new Error(`TeX Core WASM exited with status ${code}`);
        }
    };
    const env = {
        // Growth support: nothing to refresh — every view over wasm memory
        // is created at its use site, never cached across calls, so a
        // replaced buffer is picked up automatically.
        emscripten_notify_memory_growth: (): void => {}
    };
    return (await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: wasi, env })).instance;
}

// Top-level initialization keeps Document.compile synchronous in Node and
// browsers.
const instance = await loadWasm();
export const native = instance.exports as NativeExports;
