// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "swift-tex-core",
    platforms: [
        .iOS(.v18),
        .macOS(.v15),
    ],
    products: [
        .library(name: "TexCore", targets: ["TexCore"])
    ],
    targets: [
        .target(
            name: "TexCoreC",
            path: "packages/tex-core",
            sources: [
                "core/dump.c", "core/error.c", "core/layout.c", "core/memory.c",
                "core/metrics.c", "core/parse.c", "core/scaled.c", "core/tex_core.c",
                "core/token.c", "core/utf8.c",
            ],
            publicHeadersPath: "include",
            cSettings: [
                .define("TEX_CORE_STATIC_DEFINE")
            ]
        ),
        .target(
            name: "TexCore",
            dependencies: ["TexCoreC"],
            path: "packages/swift-tex-core/Sources/TexCore"
        ),
        .testTarget(
            name: "TexCoreTests",
            dependencies: ["TexCore"],
            path: "packages/swift-tex-core/Tests/TexCoreTests"
        ),
        .testTarget(
            name: "TexCoreConformanceTests",
            dependencies: ["TexCore"],
            path: "packages/swift-tex-core/Tests/TexCoreConformanceTests",
            plugins: [.plugin(name: "GenerateRenderTreeResources")]
        ),
        .executableTarget(
            name: "RenderTreeResourceGenerator",
            path: "packages/swift-tex-core/Tools/RenderTreeResourceGenerator"
        ),
        .plugin(
            name: "GenerateRenderTreeResources",
            capability: .buildTool(),
            dependencies: ["RenderTreeResourceGenerator"],
            path: "packages/swift-tex-core/Plugins/GenerateRenderTreeResources"
        ),
        .executableTarget(
            name: "TexCoreBenchmarks",
            dependencies: ["TexCore"],
            path: "packages/swift-tex-core/Benchmarks/TexCoreBenchmarks"
        ),
    ],
    cLanguageStandard: .c11
)
