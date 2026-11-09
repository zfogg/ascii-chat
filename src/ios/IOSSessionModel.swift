import Foundation
import Combine

enum IOSMode: Int32, CaseIterable, Identifiable {
    case mirror = 0
    case client = 1
    case discovery = 2

    var id: Int32 { rawValue }

    var title: String {
        switch self {
        case .mirror: return "Mirror"
        case .client: return "Client"
        case .discovery: return "Discovery"
        }
    }
}

@MainActor
final class IOSSessionModel: ObservableObject {
    enum Status: Equatable {
        case idle
        case starting
        case running
        case stopping
        case finished(Int32)
    }

    @Published var status: Status = .idle
    @Published var message = ""
    @Published var renderSequence: UInt64 = 0
    private var statusContinuation: AsyncStream<Status>.Continuation?

    func events() -> AsyncStream<Status> {
        AsyncStream { continuation in
            statusContinuation = continuation
            continuation.yield(status)
        }
    }

    private func publish(_ newStatus: Status, message: String) {
        status = newStatus
        self.message = message
        statusContinuation?.yield(newStatus)
    }

    var isActive: Bool {
        switch status {
        case .starting, .running, .stopping: return true
        case .idle, .finished: return false
        }
    }

    func start(mode: IOSMode) {
        publish(.starting, message: "Starting \(mode.title.lowercased())…")
    }

    func markRunning() {
        publish(.running, message: "Native session running")
    }

    func stopRequested() {
        publish(.stopping, message: "Stopping…")
    }

    func finish(result: Int32) {
        publish(.finished(result), message: result == 0 ? "Session ended" : "Session failed (\(result))")
    }
}

struct IOSAsyncEvent<Element: Sendable>: Sendable {
    let value: Element
}
