import Testing
@testable import AsciiChatIOSModel

@Test
@MainActor
func sessionLifecyclePublishesExpectedState() async {
    let model = IOSSessionModel()
    model.start(mode: .mirror)
    #expect(model.status == .starting)
    #expect(model.message == "Starting mirror…")
    #expect(model.isActive)

    model.markRunning()
    #expect(model.status == .running)

    model.stopRequested()
    #expect(model.status == .stopping)

    model.finish(result: 0)
    #expect(model.status == .finished(0))
    #expect(model.message == "Session ended")
    #expect(!model.isActive)
}

@Test
@MainActor
func sessionEventsIncludeInitialAndTerminalStates() async {
    let model = IOSSessionModel()
    let stream = model.events()
    var iterator = stream.makeAsyncIterator()

    #expect(await iterator.next() == .idle)
    model.start(mode: .mirror)
    #expect(await iterator.next() == .starting)
    model.finish(result: 7)
    #expect(await iterator.next() == .finished(7))
}
