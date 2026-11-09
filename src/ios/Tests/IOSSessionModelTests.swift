import XCTest
@testable import AsciiChatIOSModel

@MainActor
final class IOSSessionModelTests: XCTestCase {
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
