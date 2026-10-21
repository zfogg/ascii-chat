import { expect, it, vi } from "vite-plus/test";
import {
  HandoffProtocol,
  HANDOFF_PACKET,
} from "../../src/network/HandoffProtocol";
const module = vi.hoisted(() => ({
  HEAPU8: new Uint8Array(100000),
  _malloc: () => 128,
  _free: vi.fn(),
  _browser_handoff_encode: (
    _kind: number,
    _session: number,
    _id: number,
    _generation: number,
    sequence: number,
    _caps: number,
    _nat: number,
    _port: number,
    out: number,
  ) => {
    new DataView(module.HEAPU8.buffer).setUint32(out, sequence);
  },
}));
vi.mock("../../src/wasm/client", () => ({ getClientModule: () => module }));
it("continues probe sequence on the same authenticated connection after promotion", async () => {
  vi.useFakeTimers();
  const joined = {
    sessionId: new Uint8Array(16),
    participantId: new Uint8Array(16),
  } as any;
  const sequences: number[] = [];
  const send = (type: number, bytes: Uint8Array) => {
    expect(type).toBe(HANDOFF_PACKET);
    if (bytes.length === 64)
      sequences.push(new DataView(bytes.buffer).getUint32(0));
  };
  const participant = new HandoffProtocol(joined, send, vi.fn());
  try {
    await vi.advanceTimersByTimeAsync(5000);
    const sequence = participant.close();
    expect(sequence).toBe(2);
    const host = new HandoffProtocol(
      joined,
      send,
      vi.fn(),
      undefined,
      sequence,
    );
    try {
      expect(sequences).toEqual([1, 2]);
      await vi.advanceTimersByTimeAsync(5000);
      expect(sequences).toEqual([1, 2, 3]);
      expect(vi.getTimerCount()).toBe(1);
    } finally {
      host.close();
    }
    expect(vi.getTimerCount()).toBe(0);
  } finally {
    participant.close();
    vi.useRealTimers();
  }
});
