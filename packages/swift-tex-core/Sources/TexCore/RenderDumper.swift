/// Renders the canonical dump through the public visitor path. The output
/// is byte-deterministic: measures print with Knuth's `print_scaled`
/// algorithm over the exact scaled-point values the engine computed, so a
/// Swift dump of a tree equals the C dump of the same compile byte for
/// byte.
struct RenderDumper: RenderVisitor {
    private(set) var output = "render-tree 2\n"
    private var depth = 0

    mutating func visit(_ node: HBox) {
        indent()
        output += "hbox x=\(measure(node.x)) y=\(measure(node.y))"
        output += " width=\(measure(node.width)) ascent=\(measure(node.ascent))"
        output += " descent=\(measure(node.descent)) src=\(range(node.src))\n"
        depth += 1
        for child in node.children {
            child.accept(&self)
        }
        depth -= 1
    }

    mutating func visit(_ node: Glyph) {
        indent()
        output += "glyph x=\(measure(node.x)) y=\(measure(node.y)) cp=\(codepoint(node.codepoint))"
        output += " style=\(node.style.rawValue) family=\(node.family.rawValue) size=\(measure(node.size))"
        output += " width=\(measure(node.width)) ascent=\(measure(node.ascent)) descent=\(measure(node.descent))"
        output += " italic=\(measure(node.italic)) src=\(range(node.src))\n"
    }

    mutating func visit(_ node: Kern) {
        indent()
        output += "kern x=\(measure(node.x)) width=\(measure(node.width)) src=\(range(node.src))\n"
    }

    private mutating func indent() {
        output += String(repeating: "  ", count: depth)
    }

    private func range(_ src: SourceRange) -> String {
        "\(src.begin)..\(src.end)"
    }

    private func codepoint(_ scalar: Unicode.Scalar) -> String {
        let hex = String(scalar.value, radix: 16, uppercase: true)
        let padded = hex.count < 4 ? String(repeating: "0", count: 4 - hex.count) + hex : hex
        return "U+" + padded
    }

    /// Knuth's `print_scaled` (TeX §103) over the recovered integer scaled
    /// value: published measures are exact multiples of 2^-16 pt, so the
    /// recovery is lossless and the digits match the engine's exactly.
    /// The half-away-from-zero adjustment guards a stray ulp without
    /// `FloatingPoint.rounded()`, whose libm symbols a consumer executable
    /// would otherwise have to link (`Int64.init` truncates toward zero
    /// and lowers to a plain conversion instruction).
    private func measure(_ points: Double) -> String {
        let scaled = points * 65536
        var value = Int64(scaled < 0 ? scaled - 0.5 : scaled + 0.5)
        var text = ""
        if value < 0 {
            text += "-"
            value = -value
        }
        text += String(value / 65536)
        text += "."
        var fraction = 10 * (value % 65536) + 5
        var delta: Int64 = 10
        repeat {
            if delta > 65536 {
                fraction += 32768 - delta / 2
            }
            text += String(fraction / 65536)
            fraction = 10 * (fraction % 65536)
            delta *= 10
        } while fraction > delta
        return text + "pt"
    }
}
