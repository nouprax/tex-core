/// The failure category of a compile call, mirroring `tex_core_status`.
public enum CompileStatus: Sendable, Hashable {
    /// A caller contract violation (for example a null source with a
    /// nonzero length); unreachable through this module's safe entry points.
    case invalidArgument
    /// The source is not valid UTF-8.
    case invalidUTF8
    /// The source uses a construct outside the shipped surface.
    case unsupported
    /// The engine could not allocate memory.
    case allocationFailed
}

/// The structured fail-fast compile error (plan decision D4): a failed
/// compile produces no tree, only this value naming the offending bytes.
public struct CompileError: Error, Sendable, Hashable, CustomStringConvertible {
    /// The failure category.
    public let status: CompileStatus
    /// The source bytes the error names, when the failure has a location.
    public let range: SourceRange?
    /// The deterministic ASCII description, for example
    /// `unsupported command \frac`.
    public let message: String

    /// Creates an error value.
    public init(status: CompileStatus, range: SourceRange?, message: String) {
        self.status = status
        self.range = range
        self.message = message
    }

    /// The message, with the byte range appended when the error has one.
    public var description: String {
        if let range {
            return "\(message) (bytes \(range.begin)..\(range.end))"
        }
        return message
    }
}
