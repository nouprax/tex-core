import Foundation
import Testing
import TexCore

@Suite("conformance") struct ConformanceSuite {
    @Test("every manifest case matches the shared render-tree spec")
    func sharedCorpus() throws {
        let manifest = try loadManifest()
        for testCase in manifest.cases {
            let source = try #require(Data(base64Encoded: testCase.sourceBase64))
            let options = CompileOptions(mode: testCase.mode.value)
            switch testCase.outcome {
            case .tree:
                let tree = try Document.compile(bytes: source, options: options)
                #expect(tree.dump() == testCase.expected, Comment(rawValue: testCase.name))
                // Dump determinism: a second dump must be byte-identical.
                #expect(tree.dump() == tree.dump(), Comment(rawValue: testCase.name))
            case .error:
                do {
                    _ = try Document.compile(bytes: source, options: options)
                    Issue.record(Comment(rawValue: "\(testCase.name): compile unexpectedly succeeded"))
                } catch let error as CompileError {
                    #expect(errorRecord(error) == testCase.expected, Comment(rawValue: testCase.name))
                }
            }
        }
    }

    @Test("string compiles agree with byte compiles for UTF-8 sources")
    func stringEntryPoint() throws {
        let manifest = try loadManifest()
        for testCase in manifest.cases where testCase.outcome == .tree {
            let source = try #require(Data(base64Encoded: testCase.sourceBase64))
            guard let text = String(data: source, encoding: .utf8) else { continue }
            let tree = try Document.compile(text, options: CompileOptions(mode: testCase.mode.value))
            #expect(tree.dump() == testCase.expected, Comment(rawValue: testCase.name))
        }
    }

    @Test("the corpus reaches every public node kind through Swift values")
    func schemaReachability() throws {
        let manifest = try loadManifest()
        var collector = KindCollector()
        for testCase in manifest.cases where testCase.outcome == .tree {
            let source = try #require(Data(base64Encoded: testCase.sourceBase64))
            let tree = try Document.compile(bytes: source, options: CompileOptions(mode: testCase.mode.value))
            collector.visit(tree.root)
        }
        #expect(collector.kinds == ["hbox", "glyph", "kern"])
    }
}

/// Rebuilds the corpus error record (`specs/render-tree/README.md`) from
/// the public error value, mirroring the C conformance runner.
private func errorRecord(_ error: CompileError) -> String {
    let status =
        switch error.status {
        case .invalidArgument: "invalid-argument"
        case .invalidUTF8: "invalid-utf8"
        case .unsupported: "unsupported"
        case .allocationFailed: "allocation-failed"
        }
    let src = error.range.map { "\($0.begin)..\($0.end)" } ?? "none"
    return "render-error 2\nerror status=\(status) src=\(src) message=\(error.message)\n"
}

private struct KindCollector: RenderVisitor {
    var kinds: Set<String> = []

    mutating func visit(_ node: HBox) {
        kinds.insert("hbox")
        for child in node.children {
            child.accept(&self)
        }
    }

    mutating func visit(_ node: Glyph) {
        kinds.insert("glyph")
    }

    mutating func visit(_ node: Kern) {
        kinds.insert("kern")
    }
}

private func loadManifest() throws -> GeneratedManifest {
    let resource = try #require(
        Bundle.module.url(forResource: "render-tree-fixtures", withExtension: "json")
    )
    let manifest = try JSONDecoder().decode(GeneratedManifest.self, from: Data(contentsOf: resource))
    #expect(manifest.schemaVersion == 2)
    #expect(!manifest.cases.isEmpty)
    return manifest
}

private struct GeneratedManifest: Decodable {
    let schemaVersion: Int
    let cases: [GeneratedCase]
}

private struct GeneratedCase: Decodable {
    let name: String
    let sourceBase64: String
    let expected: String
    let mode: GeneratedMode
    let outcome: GeneratedOutcome
}

private enum GeneratedMode: String, Decodable {
    case document
    case mathInline
    case mathDisplay

    var value: CompileMode {
        switch self {
        case .document: .document
        case .mathInline: .mathInline
        case .mathDisplay: .mathDisplay
        }
    }
}

private enum GeneratedOutcome: String, Decodable {
    case tree
    case error
}
