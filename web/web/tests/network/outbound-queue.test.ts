import { afterEach, describe, expect, it, vi } from "vite-plus/test";
import {
  OutboundQueue,
  OUTBOUND_BUDGET,
} from "../../src/network/OutboundQueue";

const queues: OutboundQueue[] = [];
afterEach(() => {
  queues.forEach((queue) => queue.close());
  queues.length = 0;
  vi.useRealTimers();
});
function packet(type: number, size = 100, stream = 0, keyframe = false) {
  const bytes = new Uint8Array(22 + size);
  const view = new DataView(bytes.buffer);
  view.setUint16(8, type, false);
  view.setUint32(18, stream, false);
  bytes[22] = keyframe ? 1 : 0;
  return bytes;
}
function fixture() {
  let buffered = 300000,
    now = 0;
  const delivered = vi.fn(),
    failed = vi.fn();
  const queue = new OutboundQueue(
    () => buffered,
    delivered,
    failed,
    () => now,
  );
  queues.push(queue);
  return {
    queue,
    delivered,
    failed,
    buffer: (n: number) => {
      buffered = n;
    },
    clock: (n: number) => {
      now = n;
    },
  };
}
describe("plaintext outbound admission", () => {
  it("replaces independent frames per stream before encryption and prioritizes audio/control", () => {
    const f = fixture();
    const old = packet(3001),
      latest = packet(3001, 200),
      other = packet(3001, 100, 2);
    f.queue.submit(old);
    f.queue.submit(other);
    f.queue.submit(latest);
    const ping = packet(5001),
      audio = packet(4001);
    f.queue.submit(ping);
    f.queue.submit(audio);
    expect(f.delivered).not.toHaveBeenCalled();
    f.buffer(0);
    f.queue.flush();
    expect(f.delivered.mock.calls.map(([p]) => p)).toEqual([
      ping,
      audio,
      other,
      latest,
    ]);
    expect(f.queue.counters.dropped).toBe(1);
  });
  it.each([3002, 3003])(
    "invalidates codec %s dependencies and resumes only with a fresh keyframe",
    (type) => {
      const f = fixture();
      expect(f.queue.submit(packet(type))).toBe("needs-keyframe");
      f.queue.submit(packet(type, 100, 0, true));
      f.queue.submit(packet(type));
      f.clock(101);
      f.queue.flush();
      expect(f.queue.needsKeyframe(type)).toBe(true);
      expect(f.queue.submit(packet(type))).toBe("needs-keyframe");
      const recovery = packet(type, 200, 0, true);
      f.queue.submit(recovery);
      f.buffer(0);
      f.queue.flush();
      expect(f.delivered).toHaveBeenCalledExactlyOnceWith(recovery);
      expect(f.queue.counters.recoveryFrames).toBe(1);
    },
  );
  it("never moves priority traffic across a protocol barrier", () => {
    const f = fixture();
    const before = packet(3001, 100, 1),
      setup = packet(1000),
      after = packet(3001, 100, 2),
      audio = packet(4001);
    f.queue.submit(before);
    f.queue.submit(setup);
    f.queue.submit(after);
    f.queue.submit(audio);
    expect(f.delivered).not.toHaveBeenCalled();
    f.buffer(0);
    f.queue.flush();
    expect(f.delivered.mock.calls.map(([p]) => p)).toEqual([
      before,
      setup,
      audio,
      after,
    ]);
  });
  it("bounds byte admission and preserves capacity for control and audio", () => {
    const f = fixture();
    for (let stream = 0; stream < 100; stream++)
      f.queue.submit(packet(3001, 100000, stream));
    expect(
      f.queue.snapshot().applicationBytes + f.queue.snapshot().backendBytes,
    ).toBeLessThanOrEqual(OUTBOUND_BUDGET);
    expect(f.queue.counters.congestion).toBeGreaterThan(0);
    f.queue.submit(packet(5001));
    f.queue.submit(packet(4001));
    expect(f.delivered).not.toHaveBeenCalled();
    f.buffer(0);
    f.queue.flush();
    expect(
      f.delivered.mock.calls
        .slice(0, 2)
        .map(([p]) => new DataView(p.buffer).getUint16(8, false)),
    ).toEqual([5001, 4001]);
  });
  it("never silently drops admitted audio and fails a stalled reliable queue at its deadline", () => {
    let now = 0;
    const failed = vi.fn(),
      delivered = vi.fn();
    const buffered = vi
      .fn()
      .mockReturnValueOnce(0)
      .mockReturnValue(OUTBOUND_BUDGET);
    const queue = new OutboundQueue(buffered, delivered, failed, () => now);
    queues.push(queue);
    expect(queue.submit(packet(4001))).toBe("accepted");
    now = 5001;
    queue.flush();
    expect(failed).toHaveBeenCalledOnce();
    expect(delivered).not.toHaveBeenCalled();
    expect(queue.snapshot().applicationBytes).toBe(0);
  });
  it("clears queued owners and timers on disconnect and isolates healthy connections", () => {
    vi.useFakeTimers();
    const slow = fixture(),
      healthy = fixture();
    healthy.buffer(0);
    slow.queue.submit(packet(3001));
    healthy.queue.submit(packet(3001));
    expect(healthy.delivered).toHaveBeenCalledOnce();
    expect(slow.delivered).not.toHaveBeenCalled();
    slow.queue.close();
    vi.runAllTimers();
    expect(slow.queue.snapshot().applicationBytes).toBe(0);
    expect(() => slow.queue.submit(packet(3001))).toThrow("closed");
  });
});
