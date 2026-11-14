import XCTest
@testable import AsciiChatIOSModel

@MainActor
final class IOSSessionModelTests: XCTestCase {
    func testModeTitlesMatchNativeLaunchModes() {
        XCTAssertEqual(IOSMode.allCases.map(\.title), ["Mirror", "Client", "Discovery"])
        XCTAssertEqual(IOSMode.allCases.map(\.id), [0, 1, 2])
    }

    func testHardwareKeyboardSequencesMatchTerminalInput() {
        let cases = [
            ("UIKeyInputUpArrow", "\u{1b}[A"),
            ("UIKeyInputDownArrow", "\u{1b}[B"),
            ("UIKeyInputLeftArrow", "\u{1b}[D"),
            ("UIKeyInputRightArrow", "\u{1b}[C"),
            ("UIKeyInputEscape", "\u{1b}"),
            ("UIKeyInputDelete", "\u{7f}"),
            ("h", "\u{08}"),
            ("c", "\u{03}"),
            ("d", "\u{04}"),
            ("l", "\u{0c}"),
            ("u", "\u{15}")
        ]
        for (input, expected) in cases {
            XCTAssertEqual(IOSKeyboardInput.terminalSequence(for: input), expected, input)
        }
        XCTAssertNil(IOSKeyboardInput.terminalSequence(for: "unsupported"))
    }

    func testFailedSessionPublishesFailureMessage() {
        let model = IOSSessionModel()
        model.start(mode: .client)
        model.finish(result: 2)
        XCTAssertEqual(model.status, .finished(2))
        XCTAssertEqual(model.message, "Session failed (2)")
        XCTAssertFalse(model.isActive)
    }

    func testSessionLifecyclePublishesExpectedState() async {
        let model = IOSSessionModel()
        model.start(mode: .mirror)
        XCTAssertEqual(model.status, .starting)
        XCTAssertEqual(model.message, "Starting mirror…")
        XCTAssertTrue(model.isActive)

        model.markRunning()
        XCTAssertEqual(model.status, .running)

        model.stopRequested()
        XCTAssertEqual(model.status, .stopping)

        model.finish(result: 0)
        XCTAssertEqual(model.status, .finished(0))
        XCTAssertEqual(model.message, "Session ended")
        XCTAssertFalse(model.isActive)
    }

    func testSessionEventsIncludeInitialAndTerminalStates() async {
        let model = IOSSessionModel()
        let stream = model.events()
        var iterator = stream.makeAsyncIterator()

        let initial = await iterator.next()
        XCTAssertEqual(initial, .idle)
        model.start(mode: .mirror)
        let starting = await iterator.next()
        XCTAssertEqual(starting, .starting)
        model.finish(result: 7)
        let finished = await iterator.next()
        XCTAssertEqual(finished, .finished(7))
    }
}
