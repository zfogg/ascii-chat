import { expect, it, vi } from "vite-plus/test";
import {
  HandoffProtocol,
  HANDOFF_PACKET,
} from "../../src/network/HandoffProtocol";
const module = vi.hoisted(() => ({
  HEAPU8: new Uint8Array(100000),
  _malloc: () => 128,
  _free: vi.fn(),
  handoffResult: {
    kind: 6,
    participant: new Uint8Array(16),
    generation: 1,
    sequence: 1,
  },
  _browser_handoff_decode: () => 0,
  _browser_handoff_encode: (
    kind: number,
    _session: number,
    _id: number,
    _generation: number,
    sequence: number,
    _caps: number,
    _nat: number,
    _port: number,
    out: number,
  ) => {
    const view = new DataView(module.HEAPU8.buffer);
    view.setUint32(out, kind);
    view.setUint32(out + 4, sequence);
  },
}));
vi.mock("../../src/wasm/client", () => ({ getClientModule: () => module }));
it("keeps the original probe clock and sequence through slow host startup", async () => {
  vi.useFakeTimers({ toFake: ["setInterval", "clearInterval", "performance"] });
  const joined = {
    sessionId: new Uint8Array(16),
    participantId: new Uint8Array(16),
  } as any;
  const probes: { sequence: number; at: number }[] = [];
  const send = (type: number, bytes: Uint8Array) => {
    expect(type).toBe(HANDOFF_PACKET);
    const view = new DataView(bytes.buffer);
    if (bytes.length === 64 && view.getUint32(0) === 1)
      probes.push({ sequence: view.getUint32(4), at: performance.now() });
  };
  const oldSend = vi.fn(send),
    newSend = vi.fn(send);
  const oldCommit = vi.fn(),
    newCommit = vi.fn();
  const protocol = new HandoffProtocol(joined, oldSend, oldCommit);
  try {
    await vi.advanceTimersByTimeAsync(4900);
    protocol.adopt(newSend, newCommit);
    expect(probes).toEqual([{ sequence: 1, at: 0 }]);
    // A host engine taking 12 seconds must not pause the independent ACDS clock.
    await vi.advanceTimersByTimeAsync(12000);
    expect(probes).toEqual([
      { sequence: 1, at: 0 },
      { sequence: 2, at: 5000 },
      { sequence: 3, at: 10000 },
      { sequence: 4, at: 15000 },
    ]);
    expect(oldSend).toHaveBeenCalledTimes(2); // Initial marker and 64 KiB body.
    expect(newSend).toHaveBeenCalledTimes(6);
    expect(vi.getTimerCount()).toBe(1);
    protocol.receive(HANDOFF_PACKET, new Uint8Array(64));
    expect(oldCommit).not.toHaveBeenCalled();
    expect(newCommit).toHaveBeenCalledWith(joined.participantId);
  } finally {
    protocol.close();
    vi.useRealTimers();
  }
  expect(() => protocol.adopt(send, newCommit)).toThrow("stopped");
});
