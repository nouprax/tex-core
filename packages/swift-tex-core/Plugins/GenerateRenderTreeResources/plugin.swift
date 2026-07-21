import Foundation
import PackagePlugin

/// Regenerates the conformance resource from `specs/render-tree/` on every
/// build. The shared corpus stays the single source of truth; no copy of
/// it is checked in under the Swift package.
@main
struct GenerateRenderTreeResources: BuildToolPlugin {
    func createBuildCommands(context: PluginContext, target: Target) async throws -> [Command] {
        let specDirectory =
            context.package.directoryURL
            .appendingPathComponent("specs/render-tree", isDirectory: true)
        let output =
            context.pluginWorkDirectoryURL
            .appendingPathComponent("render-tree-fixtures.json", isDirectory: false)
        let tool = try context.tool(named: "RenderTreeResourceGenerator")

        guard
            let enumerator = FileManager.default.enumerator(
                at: specDirectory,
                includingPropertiesForKeys: [.isRegularFileKey],
                options: [.skipsHiddenFiles]
            )
        else {
            throw PluginFailure("cannot enumerate the render-tree corpus at \(specDirectory.path)")
        }

        let inputs = try enumerator.compactMap { element -> URL? in
            guard let url = element as? URL else { return nil }
            let values = try url.resourceValues(forKeys: [.isRegularFileKey])
            return values.isRegularFile == true ? url : nil
        }.sorted { $0.path < $1.path }

        guard inputs.contains(where: { $0.lastPathComponent == "manifest.json" }) else {
            throw PluginFailure("render-tree manifest is missing from \(specDirectory.path)")
        }

        return [
            .buildCommand(
                displayName: "Generate render-tree conformance resource",
                executable: tool.url,
                arguments: [specDirectory.path, output.path],
                inputFiles: inputs,
                outputFiles: [output]
            )
        ]
    }
}

private struct PluginFailure: Error, CustomStringConvertible {
    let description: String

    init(_ description: String) {
        self.description = description
    }
}
