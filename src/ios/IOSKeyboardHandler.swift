import UIKit

@_silgen_name("ios_stdin_bridge_push")
private func ios_stdin_bridge_push(_ data: UnsafeRawPointer, _ length: Int) -> Int

/// First-responder sink for hardware keyboard input.
///
/// UIKit's key-command API supplies normalized control keys, so the native
/// terminal input path receives the same bytes it would receive from a tty.
final class IOSKeyboardHandler: UIView {
    override var canBecomeFirstResponder: Bool { true }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window != nil {
            becomeFirstResponder()
        }
    }

    override var keyCommands: [UIKeyCommand]? {
        [
            command(input: UIKeyCommand.inputUpArrow),
            command(input: UIKeyCommand.inputDownArrow),
            command(input: UIKeyCommand.inputLeftArrow),
            command(input: UIKeyCommand.inputRightArrow),
            command(input: UIKeyCommand.inputEscape),
            command(input: UIKeyCommand.inputDelete),
            command(input: "h", modifiers: .control),
            command(input: "c", modifiers: .control),
            command(input: "d", modifiers: .control),
            command(input: "l", modifiers: .control),
            command(input: "u", modifiers: .control),
        ]
    }

    private func command(input: String, modifiers: UIKeyModifierFlags = []) -> UIKeyCommand {
        let result = UIKeyCommand(input: input, modifierFlags: modifiers, action: #selector(handleCommand(_:)))
        result.discoverabilityTitle = "Send terminal key"
        return result
    }

    @objc private func handleCommand(_ command: UIKeyCommand) {
        let key: String
        switch command.input {
        case UIKeyCommand.inputUpArrow: key = "\u{1b}[A"
        case UIKeyCommand.inputDownArrow: key = "\u{1b}[B"
        case UIKeyCommand.inputLeftArrow: key = "\u{1b}[D"
        case UIKeyCommand.inputRightArrow: key = "\u{1b}[C"
        case UIKeyCommand.inputEscape: key = "\u{1b}"
        case UIKeyCommand.inputDelete: key = "\u{7f}"
        case "h": key = "\u{08}"
        case "c": key = "\u{03}"
        case "d": key = "\u{04}"
        case "l": key = "\u{0c}"
        case "u": key = "\u{15}"
        default: return
        }
        push(key)
    }

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var handled = false
        for press in presses {
            guard let characters = press.key?.charactersIgnoringModifiers, !characters.isEmpty else { continue }
            push(characters)
            handled = true
        }
        if !handled {
            super.pressesBegan(presses, with: event)
        }
    }

    private func push(_ text: String) {
        let bytes = Array(text.utf8)
        bytes.withUnsafeBytes { rawBuffer in
            guard let baseAddress = rawBuffer.baseAddress, !bytes.isEmpty else { return }
            _ = ios_stdin_bridge_push(baseAddress, bytes.count)
        }
    }
}
