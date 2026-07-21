// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "TexCoreConsumer",
    platforms: [.iOS(.v18), .macOS(.v15)],
    dependencies: [.package(path: "../../../..")],
    targets: [
        .testTarget(
            name: "ConsumerTests",
            dependencies: [.product(name: "TexCore", package: "tex-core")]
        )
    ]
)
