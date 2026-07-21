import Foundation

/// Packs the shared render-tree corpus (`specs/render-tree/`) into one JSON
/// resource for the Swift conformance suite. Inputs are carried as base64
/// bytes — they are byte-exact compile input and one case is deliberately
/// invalid UTF-8 — while expected files are UTF-8 text. The build-tool
/// plugin runs this on every build, so the resource can never drift from
/// the reviewed corpus.
@main
enum RenderTreeResourceGenerator {
    static func main() {
        do {
            try generate(arguments: Array(CommandLine.arguments.dropFirst()))
        } catch {
            FileHandle.standardError.write(Data("\(error)\n".utf8))
            exit(1)
        }
    }

    private static func generate(arguments: [String]) throws {
        guard arguments.count == 2 else { throw GeneratorFailure.invalidArguments }
        let specDirectory =
            URL(fileURLWithPath: arguments[0], isDirectory: true)
            .resolvingSymlinksInPath()
            .standardizedFileURL
        let output = URL(fileURLWithPath: arguments[1], isDirectory: false)
        let manifestData = try Data(
            contentsOf: specDirectory.appendingPathComponent("manifest.json", isDirectory: false)
        )
        let manifest: RenderTreeManifest
        do {
            manifest = try JSONDecoder().decode(RenderTreeManifest.self, from: manifestData)
        } catch let error as GeneratorFailure {
            throw error
        } catch {
            throw GeneratorFailure.invalidManifest(String(describing: error))
        }
        guard manifest.schemaVersion == 1, !manifest.cases.isEmpty else {
            throw GeneratorFailure.invalidManifest("schemaVersion 1 requires at least one case")
        }
        guard Set(manifest.cases.map(\.name)).count == manifest.cases.count else {
            throw GeneratorFailure.invalidManifest("case names must be unique")
        }

        let cases = try manifest.cases.map { testCase -> GeneratedCase in
            guard testCase.input == "\(testCase.name).tex", testCase.expected == "\(testCase.name).tree" else {
                throw GeneratorFailure.invalidManifest("case \(testCase.name) has non-canonical paths")
            }
            let source = try fixtureData(at: testCase.input, in: specDirectory)
            let expectedData = try fixtureData(at: testCase.expected, in: specDirectory)
            guard let expected = String(data: expectedData, encoding: .utf8) else {
                throw GeneratorFailure.invalidManifest("case \(testCase.name) expected file is not UTF-8")
            }
            return GeneratedCase(
                name: testCase.name,
                sourceBase64: source.base64EncodedString(),
                expected: expected,
                mode: testCase.compileOptions.mode,
                outcome: testCase.outcome
            )
        }
        let generated = GeneratedManifest(schemaVersion: manifest.schemaVersion, cases: cases)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(generated)
        try FileManager.default.createDirectory(
            at: output.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try data.write(to: output, options: .atomic)
    }

    private static func fixtureData(at relativePath: String, in root: URL) throws -> Data {
        guard !relativePath.hasPrefix("/") else {
            throw GeneratorFailure.invalidRelativePath(relativePath)
        }
        let rootPrefix = root.path.hasSuffix("/") ? root.path : root.path + "/"
        let url = root.appendingPathComponent(relativePath).resolvingSymlinksInPath()
            .standardizedFileURL
        guard url.path.hasPrefix(rootPrefix) else {
            throw GeneratorFailure.invalidRelativePath(relativePath)
        }
        return try Data(contentsOf: url)
    }
}

private enum GeneratorFailure: Error, CustomStringConvertible {
    case invalidArguments
    case invalidManifest(String)
    case invalidRelativePath(String)

    var description: String {
        switch self {
        case .invalidArguments:
            "usage: RenderTreeResourceGenerator SPEC_DIRECTORY OUTPUT_FILE"
        case .invalidManifest(let reason):
            "invalid render-tree manifest: \(reason)"
        case .invalidRelativePath(let path):
            "render-tree case path escapes the spec directory: \(path)"
        }
    }
}

private struct RenderTreeManifest: Decodable {
    let schemaVersion: Int
    let contract: String
    let dumpGrammar: String
    let cases: [RenderTreeCase]
}

private struct RenderTreeCase: Decodable {
    let name: String
    let input: String
    let expected: String
    let compileOptions: RenderTreeCompileOptions
    let outcome: RenderTreeOutcome
}

private struct RenderTreeCompileOptions: Decodable {
    let mode: RenderTreeMode
}

private enum RenderTreeMode: String, Codable {
    case document
    case mathInline
    case mathDisplay
}

private enum RenderTreeOutcome: String, Codable {
    case tree
    case error
}

private struct GeneratedManifest: Encodable {
    let schemaVersion: Int
    let cases: [GeneratedCase]
}

private struct GeneratedCase: Encodable {
    let name: String
    let sourceBase64: String
    let expected: String
    let mode: RenderTreeMode
    let outcome: RenderTreeOutcome
}
