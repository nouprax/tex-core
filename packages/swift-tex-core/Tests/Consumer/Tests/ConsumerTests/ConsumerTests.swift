import Testing
import TexCore

@Suite("consumer") struct ConsumerTests {
    @Test("a clean package consumes the public TexCore product")
    func publicProduct() throws {
        let tree = try Document.compile("x", options: CompileOptions(mode: .mathInline))

        guard case .glyph(let glyph) = tree.root.children[0] else {
            Issue.record("expected a glyph child")
            return
        }
        #expect(glyph.style == .italic)
        #expect(tree.dump().hasPrefix("render-tree 4\n"))
    }
}
