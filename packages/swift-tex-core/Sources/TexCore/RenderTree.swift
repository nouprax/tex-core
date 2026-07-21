/// Half-open byte range `[begin, end)` into the compile call's source.
///
/// Ranges are data obtained from a compiled tree, valid for that compile
/// call's input; the schema contract deliberately does not promise that
/// they are storage-resident node properties across revisions.
public struct SourceRange: Sendable, Hashable {
    /// The first byte of the range.
    public let begin: Int
    /// One past the last byte of the range.
    public let end: Int

    /// Creates a range; `begin` must not exceed `end`.
    public init(begin: Int, end: Int) {
        self.begin = begin
        self.end = end
    }
}

/// The face style a glyph is drawn in (schema `style`).
public enum GlyphStyle: String, Sendable, CaseIterable {
    /// The upright text and math face.
    case upright
    /// The math italic letter face.
    case italic
}

/// The font family a glyph belongs to (schema `family`).
public enum GlyphFamily: String, Sendable, CaseIterable {
    /// The default face pair embedded in the engine.
    case main
}

/// A horizontal box: the compile root. Measures are absolute points and
/// exact integer multiples of 2^-16 pt (see the schema contract).
public struct HBox: Sendable, Hashable {
    /// The advance of the box's content in points.
    public let width: Double
    /// The maximum extent above the baseline over the children, in points.
    public let ascent: Double
    /// The maximum extent below the baseline over the children, in points.
    public let descent: Double
    /// The source bytes this box was compiled from.
    public let src: SourceRange
    /// Child nodes in source order.
    public let children: [RenderNode]

    /// Creates a box value.
    public init(width: Double, ascent: Double, descent: Double, src: SourceRange, children: [RenderNode]) {
        self.width = width
        self.ascent = ascent
        self.descent = descent
        self.src = src
        self.children = children
    }
}

/// A leaf glyph, named by Unicode codepoint + style + family + size —
/// never by a private glyph ID.
public struct Glyph: Sendable, Hashable {
    /// Horizontal offset of the reference point from the parent's, in points.
    public let x: Double
    /// Vertical offset from the parent baseline, positive upward, in points.
    public let y: Double
    /// The Unicode scalar this glyph renders.
    public let codepoint: Unicode.Scalar
    /// The face style.
    public let style: GlyphStyle
    /// The font family.
    public let family: GlyphFamily
    /// The em size in points.
    public let size: Double
    /// The glyph advance in points.
    public let width: Double
    /// Extent above the baseline in points.
    public let ascent: Double
    /// Extent below the baseline in points.
    public let descent: Double
    /// The italic correction in points; included in a math Ord glyph's advance.
    public let italic: Double
    /// The source bytes this glyph was compiled from.
    public let src: SourceRange

    /// Creates a glyph value.
    public init(
        x: Double,
        y: Double,
        codepoint: Unicode.Scalar,
        style: GlyphStyle,
        family: GlyphFamily,
        size: Double,
        width: Double,
        ascent: Double,
        descent: Double,
        italic: Double,
        src: SourceRange
    ) {
        self.x = x
        self.y = y
        self.codepoint = codepoint
        self.style = style
        self.family = family
        self.size = size
        self.width = width
        self.ascent = ascent
        self.descent = descent
        self.italic = italic
        self.src = src
    }
}

/// A leaf kern: a fixed horizontal advance. `width` may be negative.
public struct Kern: Sendable, Hashable {
    /// Horizontal offset of the reference point from the parent's, in points.
    public let x: Double
    /// The advance in points; negative widths move the cursor backward.
    public let width: Double
    /// The source bytes this kern was compiled from.
    public let src: SourceRange

    /// Creates a kern value.
    public init(x: Double, width: Double, src: SourceRange) {
        self.x = x
        self.width = width
        self.src = src
    }
}

/// One node of the render tree. The cases are exactly the schema's node
/// kinds, so `switch` statements over a node are exhaustive by construction.
public enum RenderNode: Sendable, Hashable {
    /// A horizontal box with children.
    case hbox(HBox)
    /// A leaf glyph.
    case glyph(Glyph)
    /// A leaf kern.
    case kern(Kern)

    /// Dispatches this node to the matching `visit` overload.
    public func accept<V: RenderVisitor>(_ visitor: inout V) -> V.Result {
        switch self {
        case .hbox(let box): visitor.visit(box)
        case .glyph(let glyph): visitor.visit(glyph)
        case .kern(let kern): visitor.visit(kern)
        }
    }

    /// The source bytes this node was compiled from.
    public var src: SourceRange {
        switch self {
        case .hbox(let box): box.src
        case .glyph(let glyph): glyph.src
        case .kern(let kern): kern.src
        }
    }
}

/// An exhaustive typed visitor over render nodes: one `visit` overload per
/// schema node kind, so adding a kind is a source-breaking schema change,
/// never a silently unhandled case.
public protocol RenderVisitor {
    /// The value each visit produces.
    associatedtype Result
    /// Visits a horizontal box.
    mutating func visit(_ node: HBox) -> Result
    /// Visits a glyph.
    mutating func visit(_ node: Glyph) -> Result
    /// Visits a kern.
    mutating func visit(_ node: Kern) -> Result
}

/// An immutable compiled document: the render tree of schema version 1.
///
/// Trees are plain `Sendable` values — safely shareable across threads,
/// structurally equatable, and detached from the native engine the moment
/// `Document.compile` returns.
public struct RenderTree: Sendable, Hashable {
    /// The root horizontal box; its range spans the whole compile input.
    public let root: HBox

    /// Creates a tree value.
    public init(root: HBox) {
        self.root = root
    }

    /// Renders the canonical dump (`docs/specs/render-tree-dump.md`):
    /// byte-identical across platforms for the same compiled tree.
    public func dump() -> String {
        var dumper = RenderDumper()
        dumper.visit(root)
        return dumper.output
    }
}
