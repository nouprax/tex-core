import TexCoreC

/// The input form of one compile call (schema `options.mode`).
public enum CompileMode: String, Sendable, Hashable, CaseIterable {
    /// A self-contained LaTeX file or block.
    case document
    /// A bare inline math fragment with the delimiters already stripped.
    case mathInline
    /// A bare display math fragment with the delimiters already stripped.
    case mathDisplay
}

/// Options of one compile call. Every field has a frozen default.
public struct CompileOptions: Sendable, Hashable {
    /// The input form; the default is `document`.
    public var mode: CompileMode

    /// Creates options with the frozen defaults.
    public init(mode: CompileMode = .document) {
        self.mode = mode
    }
}

/// The compile entry point: UTF-8 LaTeX source in, an immutable render
/// tree out. The native engine validates, parses, and lays out the source;
/// the returned tree is a detached Swift value and no native handle
/// survives the call. This library never draws.
public enum Document {
    /// Compiles a Swift string (always valid UTF-8).
    public static func compile(_ source: String, options: CompileOptions = CompileOptions()) throws -> RenderTree {
        try compile(bytes: Array(source.utf8), options: options)
    }

    /// Compiles exact source bytes. The engine owns encoding validation:
    /// invalid UTF-8 is a structured `CompileError`, never a silent
    /// replacement, so byte inputs round-trip the full error contract.
    public static func compile(
        bytes: some Collection<UInt8>,
        options: CompileOptions = CompileOptions()
    ) throws -> RenderTree {
        var nativeOptions = tex_core_options()
        tex_core_options_init(&nativeOptions)
        nativeOptions.mode = options.mode.native

        let source = ContiguousArray(bytes)
        var tree: OpaquePointer?
        var error = tex_core_error()
        let status = source.withUnsafeBufferPointer { buffer in
            tex_core_document_compile(buffer.baseAddress, buffer.count, &nativeOptions, &tree, &error)
        }
        guard status == TEX_CORE_STATUS_OK, let tree else {
            throw CompileError(from: error)
        }
        defer { tex_core_render_tree_free(tree) }

        guard let nativeRoot = tex_core_render_tree_root(tree),
            case .hbox(let root) = RenderNode(copying: nativeRoot)
        else {
            throw CompileError(
                status: .invalidArgument,
                range: nil,
                message: "native engine returned a rootless tree"
            )
        }
        return RenderTree(root: root)
    }
}

extension CompileMode {
    var native: tex_core_mode {
        switch self {
        case .document: TEX_CORE_MODE_DOCUMENT
        case .mathInline: TEX_CORE_MODE_MATH_INLINE
        case .mathDisplay: TEX_CORE_MODE_MATH_DISPLAY
        }
    }
}

extension RenderNode {
    /// Copies one native node (and, for boxes, its subtree) into detached
    /// Swift values through the public borrowed-view accessors.
    init(copying node: OpaquePointer) {
        let frame = tex_core_node_frame(node)
        let range = tex_core_node_range(node)
        let src = SourceRange(begin: range.begin, end: range.end)
        switch tex_core_node_get_kind(node) {
        case TEX_CORE_NODE_GLYPH:
            self = .glyph(RenderNode.copyGlyph(node, frame, src))
        case TEX_CORE_NODE_KERN:
            self = .kern(Kern(x: frame.x, width: frame.width, src: src))
        case TEX_CORE_NODE_RULE:
            self = .rule(
                Rule(
                    x: frame.x,
                    y: frame.y,
                    width: frame.width,
                    ascent: frame.ascent,
                    descent: frame.descent,
                    src: src
                )
            )
        default:
            // TEX_CORE_NODE_HBOX; NONE is unreachable from a compiled tree.
            self = .hbox(RenderNode.copyBox(node, frame, src))
        }
    }

    private static func copyGlyph(_ node: OpaquePointer, _ frame: tex_core_frame, _ src: SourceRange) -> Glyph {
        let native = tex_core_node_glyph(node)
        return Glyph(
            x: frame.x,
            y: frame.y,
            codepoint: Unicode.Scalar(native.codepoint) ?? Unicode.Scalar(UInt8(0)),
            style: native.style == TEX_CORE_STYLE_ITALIC ? .italic : .upright,
            family: RenderNode.family(of: native.family),
            size: native.size,
            width: frame.width,
            ascent: frame.ascent,
            descent: frame.descent,
            italic: frame.italic,
            src: src
        )
    }

    /// The native family values in enum order (the contract's family
    /// inventory); anything out of range reads as main.
    private static let families: [GlyphFamily] = [
        .main, .size1, .size2, .size3, .size4, .bold, .textit, .cal, .bb, .sans, .mono,
    ]

    private static func family(of native: tex_core_family) -> GlyphFamily {
        let index = Int(native.rawValue)
        return index < families.count ? families[index] : .main
    }

    private static func copyBox(_ node: OpaquePointer, _ frame: tex_core_frame, _ src: SourceRange) -> HBox {
        var children: [RenderNode] = []
        let count = tex_core_node_child_count(node)
        children.reserveCapacity(count)
        for index in 0..<count {
            if let child = tex_core_node_child(node, index) {
                children.append(RenderNode(copying: child))
            }
        }
        return HBox(
            x: frame.x,
            y: frame.y,
            width: frame.width,
            ascent: frame.ascent,
            descent: frame.descent,
            src: src,
            children: children
        )
    }
}

extension CompileError {
    init(from error: tex_core_error) {
        let status: CompileStatus =
            switch error.status {
            case TEX_CORE_STATUS_INVALID_UTF8: .invalidUTF8
            case TEX_CORE_STATUS_UNSUPPORTED: .unsupported
            case TEX_CORE_STATUS_ALLOCATION_FAILED: .allocationFailed
            default: .invalidArgument
            }
        let range = error.has_range ? SourceRange(begin: error.range.begin, end: error.range.end) : nil
        let message = withUnsafeBytes(of: error.message) { buffer in
            let bytes = buffer.prefix { $0 != 0 }
            // The engine guarantees deterministic NUL-terminated ASCII; the
            // decoding initializer keeps this total if that ever regresses.
            // swiftlint:disable:next optional_data_string_conversion
            return String(decoding: bytes, as: UTF8.self)
        }
        self.init(status: status, range: range, message: message)
    }
}
