import { Document } from "../dist/index.js";

const warmupCount = 3;
const repeatCount = 10;

function benchmark(workload, source, mode) {
    const options = { mode };
    for (let index = 0; index < warmupCount; index += 1) Document.compile(source, options);

    const durations = [];
    for (let index = 0; index < repeatCount; index += 1) {
        const start = process.hrtime.bigint();
        Document.compile(source, options);
        durations.push(Number(process.hrtime.bigint() - start));
    }
    durations.sort((left, right) => left - right);
    const medianNanoseconds = durations[Math.trunc(repeatCount / 2)];
    const heapUsedKib = Math.trunc(process.memoryUsage().heapUsed / 1024);
    console.log(
        `benchmark runtime=es-node boundary=wasm_compile_and_value_copy workload=${workload} ` +
            `workload_version=1 ` +
            `bytes=${Buffer.byteLength(source, "utf8")} warmup=${warmupCount} repeats=${repeatCount} ` +
            `median_ns=${medianNanoseconds} heap_used_kib=${heapUsedKib}`
    );
}

const paragraph = "The 42 rows total 3.14 units, and the galley keeps flowing.\n";
benchmark("large_document", paragraph.repeat(2000), "document");
benchmark("math_spacing", "a\\,b\\:c\\;d\\!e\\quad f\\qquad g\\ h".repeat(200), "mathDisplay");
