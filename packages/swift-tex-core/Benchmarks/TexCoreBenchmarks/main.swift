import Foundation
import TexCore

#if canImport(Darwin)
    import Darwin
#endif

let warmupCount = 3
let repeatCount = 10

func benchmark(_ workload: String, source: String, mode: CompileMode) throws {
    let options = CompileOptions(mode: mode)
    for _ in 0..<warmupCount {
        _ = try Document.compile(source, options: options)
    }

    let clock = ContinuousClock()
    var durations: [Duration] = []
    durations.reserveCapacity(repeatCount)
    for _ in 0..<repeatCount {
        durations.append(try clock.measure { _ = try Document.compile(source, options: options) })
    }
    durations.sort()
    let median = durations[repeatCount / 2].components
    let medianNanoseconds = Double(median.seconds) * 1e9 + Double(median.attoseconds) / 1e9
    #if canImport(Darwin)
        var usage = rusage()
        getrusage(RUSAGE_SELF, &usage)
        let peakRSSKiB = usage.ru_maxrss / 1024
    #else
        let peakRSSKiB = -1
    #endif
    print(
        "benchmark runtime=swift boundary=native_compile_and_value_copy workload=\(workload) "
            + "workload_version=1 "
            + "bytes=\(source.utf8.count) warmup=\(warmupCount) repeats=\(repeatCount) "
            + "median_ns=\(Int64(medianNanoseconds)) peak_rss_kib=\(peakRSSKiB)"
    )
}

let paragraph = "The 42 rows total 3.14 units, and the galley keeps flowing.\n"
try benchmark("large_document", source: String(repeating: paragraph, count: 2_000), mode: .document)
try benchmark(
    "math_spacing",
    source: String(repeating: "a\\,b\\:c\\;d\\!e\\quad f\\qquad g\\ h", count: 200),
    mode: .mathDisplay
)
