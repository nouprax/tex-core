import Foundation
import Testing
import TexCore

@Suite("api") struct APISuite {
    @Test("options default to document mode")
    func defaults() {
        #expect(CompileOptions().mode == .document)
    }

    @Test("compile returns a detached value tree with resolved geometry")
    func compileStructure() throws {
        let tree = try Document.compile("x", options: CompileOptions(mode: .mathInline))
        #expect(tree.root.src == SourceRange(begin: 0, end: 1))
        #expect(tree.root.children.count == 1)
        guard case .glyph(let glyph) = tree.root.children[0] else {
            Issue.record("expected a glyph child")
            return
        }
        #expect(glyph.codepoint == "x")
        #expect(glyph.style == .italic)
        #expect(glyph.family == .main)
        #expect(glyph.size == 10.0)
        #expect(glyph.x == 0.0)
        #expect(tree.root.width == glyph.width + glyph.italic)
    }

    @Test("equal sources compile to equal values")
    func valueEquality() throws {
        let options = CompileOptions(mode: .document)
        let first = try Document.compile("a b", options: options)
        let second = try Document.compile("a b", options: options)
        #expect(first == second)
        #expect(first.hashValue == second.hashValue)
        #expect(first != (try Document.compile("a c", options: options)))
    }

    @Test("the dump is canonical and deterministic")
    func dump() throws {
        let tree = try Document.compile("", options: CompileOptions(mode: .document))
        #expect(tree.dump() == "render-tree 5\nhbox x=0.0pt y=0.0pt width=0.0pt ascent=0.0pt descent=0.0pt src=0..0\n")
    }

    @Test("the canonical dump is stack-safe for deeply nested trees")
    func deepDump() {
        let depth = 2_048
        var root = HBox(
            x: 0,
            y: 0,
            width: 0,
            ascent: 0,
            descent: 0,
            src: SourceRange(begin: 0, end: 0),
            children: []
        )
        for _ in 1..<depth {
            root = HBox(
                x: 0,
                y: 0,
                width: 0,
                ascent: 0,
                descent: 0,
                src: SourceRange(begin: 0, end: 0),
                children: [.hbox(root)]
            )
        }
        let retained = RetainedTree(RenderTree(root: root))
        let dump = retained.tree.dump()
        #expect(dump.hasPrefix("render-tree 5\n"))
        #expect(dump.count > depth)
        // Keep the pathological value alive until process exit: recursively
        // releasing a synthetic value-tree chain would test ARC teardown,
        // not the canonical dumper's explicit traversal stack.
        _ = Unmanaged.passRetained(retained)
    }

    @Test("trees compile and share safely across concurrent tasks")
    func concurrency() async throws {
        let trees = try await withThrowingTaskGroup(of: RenderTree.self) { group in
            for _ in 0..<8 {
                group.addTask { try Document.compile("a b", options: CompileOptions(mode: .document)) }
            }
            var results: [RenderTree] = []
            for try await tree in group {
                results.append(tree)
            }
            return results
        }
        #expect(Set(trees.map { $0.dump() }).count == 1)
    }
}

private final class RetainedTree {
    let tree: RenderTree

    init(_ tree: RenderTree) {
        self.tree = tree
    }
}

@Suite("errors") struct ErrorSuite {
    @Test("unsupported input throws the structured fail-fast error")
    func unsupported() {
        do {
            _ = try Document.compile("\\foo x", options: CompileOptions(mode: .mathInline))
            Issue.record("compile unexpectedly succeeded")
        } catch let error as CompileError {
            #expect(error.status == .unsupported)
            #expect(error.range == SourceRange(begin: 0, end: 4))
            #expect(error.message == "unsupported command \\foo")
            #expect(error.description == "unsupported command \\foo (bytes 0..4)")
            #expect(error.localizedDescription == error.description)
        } catch {
            Issue.record("unexpected error type: \(error)")
        }
    }

    @Test("invalid UTF-8 bytes throw the encoding error, never replace")
    func invalidUTF8() {
        do {
            _ = try Document.compile(bytes: [0x61, 0xFF], options: CompileOptions(mode: .document))
            Issue.record("compile unexpectedly succeeded")
        } catch let error as CompileError {
            #expect(error.status == .invalidUTF8)
            #expect(error.range == SourceRange(begin: 1, end: 2))
            #expect(error.message == "invalid UTF-8 sequence")
        } catch {
            Issue.record("unexpected error type: \(error)")
        }
    }

    @Test("mode changes the accepted surface")
    func modeSensitivity() throws {
        _ = try Document.compile("a+b", options: CompileOptions(mode: .mathInline))
        do {
            _ = try Document.compile("a+b", options: CompileOptions(mode: .document))
            Issue.record("document-mode math character unexpectedly compiled")
        } catch let error as CompileError {
            #expect(error.status == .unsupported)
        } catch {
            Issue.record("unexpected error type: \(error)")
        }
    }
}

@Suite("visitor") struct VisitorSuite {
    @Test("visitors dispatch exhaustively over the schema kinds")
    func dispatch() throws {
        let tree = try Document.compile("a b", options: CompileOptions(mode: .document))
        var counter = KindCounter()
        counter.visit(tree.root)
        #expect(counter.boxes == 1)
        #expect(counter.glyphs == 2)
        #expect(counter.kerns == 1)
        #expect(counter.rules == 0)

        let fraction = try Document.compile("\\frac{1}{2}", options: CompileOptions(mode: .mathInline))
        var fractionCounter = KindCounter()
        fractionCounter.visit(fraction.root)
        #expect(fractionCounter.rules == 1)
        #expect(fractionCounter.kerns == 2)
    }

    @Test("nodes expose their source ranges uniformly")
    func sourceRanges() throws {
        let tree = try Document.compile("a b", options: CompileOptions(mode: .document))
        let begins = tree.root.children.map { $0.src.begin }
        #expect(begins == begins.sorted())
    }
}

private struct KindCounter: RenderVisitor {
    var boxes = 0
    var glyphs = 0
    var kerns = 0
    var rules = 0

    mutating func visit(_ node: HBox) {
        boxes += 1
        for child in node.children {
            child.accept(&self)
        }
    }

    mutating func visit(_ node: Glyph) {
        glyphs += 1
    }

    mutating func visit(_ node: Kern) {
        kerns += 1
    }

    mutating func visit(_ node: Rule) {
        rules += 1
    }
}
