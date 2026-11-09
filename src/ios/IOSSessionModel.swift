import Foundation
import Observation

@MainActor
@Observable
final class IOSSessionModel {
    enum Status: Equatable {
        case idle
        case starting
        case running
        case stopping
        case finished(Int32)
    }

    var status: Status = .idle
    var message = ""
    var renderSequence: UInt64 = 0

    var isActive: Bool {
        switch status {
        case .starting, .running, .stopping: return true
        case .idle, .finished: return false
        }
    }

    func start(mode: IOSMode) {
        status = .starting
        message = "Starting \(mode.title.lowercased())…"
    }

    func markRunning() {
        status = .running
        message = "Native session running"
    }

    func stopRequested() {
        status = .stopping
        message = "Stopping…"
    }

    func finish(result: Int32) {
        status = .finished(result)
        message = result == 0 ? "Session ended" : "Session failed (\(result))"
    }
}

struct IOSAsyncEvent<Element: Sendable>: Sendable {
    let value: Element
}
