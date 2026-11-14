import Foundation

/// Converts UIKit hardware-keyboard inputs into the byte sequences expected by
/// the shared terminal input path.
enum IOSKeyboardInput {
    static let up = "\u{1b}[A"
    static let down = "\u{1b}[B"
    static let left = "\u{1b}[D"
    static let right = "\u{1b}[C"
    static let escape = "\u{1b}"
    static let delete = "\u{7f}"

    static func terminalSequence(for input: String) -> String? {
        switch input {
        case "UIKeyInputUpArrow": return up
        case "UIKeyInputDownArrow": return down
        case "UIKeyInputLeftArrow": return left
        case "UIKeyInputRightArrow": return right
        case "UIKeyInputEscape": return escape
        case "UIKeyInputDelete": return delete
        case "h": return "\u{08}"
        case "c": return "\u{03}"
        case "d": return "\u{04}"
        case "l": return "\u{0c}"
        case "u": return "\u{15}"
        default: return nil
        }
    }
}
