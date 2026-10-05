import { describe, it, expect, vi } from "vite-plus/test";
import { WebRTCBridge } from "../../src/network/WebRTCBridge";
import {
  lookupRequest,
  parseJoined,
  parseSignal,
  signalPacket,
} from "../../src/network/acdsProtocol";

function channel() {
  return {
    readyState: "open",
    bufferedAmount: 0,
    send: vi.fn(),
    close: vi.fn(),
  } as unknown as RTCDataChannel;
}
function packet(length: number) {
  const bytes = new Uint8Array(length + 22);
  new DataView(bytes.buffer).setUint32(10, length, false);
  return bytes;
}

describe("WebRTC ACIP framing", () => {
  it("replaces stale unsent raw video while preserving control packets", () => {
    const dc = channel();
    Object.defineProperty(dc, "bufferedAmount", {
      value: 300000,
      writable: true,
    });
    const bridge = new WebRTCBridge(dc, vi.fn(), vi.fn());
    const oldFrame = packet(100),
      control = packet(12),
      newest = packet(200);
    bridge.send(oldFrame, true);
    bridge.send(control);
    bridge.send(newest, true);
    Object.defineProperty(dc, "bufferedAmount", { value: 0 });
    dc.onbufferedamountlow!(new Event("bufferedamountlow"));
    expect(vi.mocked(dc.send).mock.calls.map((call) => call[0])).toEqual([
      control,
      newest,
    ]);
  });
  it("finishes a partially sent frame before transmitting the newest frame", () => {
    const dc = channel();
    Object.defineProperty(dc, "bufferedAmount", { value: 0, writable: true });
    vi.mocked(dc.send).mockImplementation(() => {
      Object.defineProperty(dc, "bufferedAmount", { value: 300000 });
    });
    const bridge = new WebRTCBridge(dc, vi.fn(), vi.fn(), 8192);
    const first = packet(12000),
      stale = packet(100),
      newest = packet(200);
    bridge.send(first, true);
    bridge.send(stale, true);
    bridge.send(newest, true);
    vi.mocked(dc.send).mockImplementation(() => {});
    Object.defineProperty(dc, "bufferedAmount", { value: 0 });
    dc.onbufferedamountlow!(new Event("bufferedamountlow"));
    expect(vi.mocked(dc.send).mock.calls.map((call) => call[0])).toEqual([
      first.slice(0, 8192),
      first.slice(8192),
      newest,
    ]);
  });
  it("reassembles split headers and coalesced packets", () => {
    const dc = channel(),
      received = vi.fn(),
      failure = vi.fn();
    new WebRTCBridge(dc, received, failure);
    const first = packet(40000),
      second = packet(12);
    const bytes = new Uint8Array(first.length + second.length);
    bytes.set(first);
    bytes.set(second, first.length);
    for (const chunk of [
      bytes.slice(0, 8),
      bytes.slice(8, 20000),
      bytes.slice(20000),
    ])
      dc.onmessage!({ data: chunk.buffer } as MessageEvent<ArrayBuffer>);
    expect(received.mock.calls.map((call) => call[0])).toEqual([first, second]);
    expect(failure).not.toHaveBeenCalled();
  });
  it("honors SCTP message limits and resumes after backpressure", () => {
    const dc = channel();
    const bridge = new WebRTCBridge(dc, vi.fn(), vi.fn(), 8192);
    Object.defineProperty(dc, "bufferedAmount", {
      value: 300000,
      writable: true,
    });
    bridge.send(packet(30000));
    expect(dc.send).not.toHaveBeenCalled();
    Object.defineProperty(dc, "bufferedAmount", { value: 0 });
    dc.onbufferedamountlow!(new Event("bufferedamountlow"));
    expect(
      vi
        .mocked(dc.send)
        .mock.calls.map((call) => (call[0] as Uint8Array).byteLength),
    ).toEqual([8192, 8192, 8192, 5446]);
    bridge.close();
    expect(() => bridge.send(packet(1))).toThrow("not open");
  });
  it("rejects oversized incoming packets", () => {
    const dc = channel(),
      failure = vi.fn();
    new WebRTCBridge(dc, vi.fn(), failure);
    const header = packet(0);
    new DataView(header.buffer).setUint32(10, 0xffffffff, false);
    dc.onmessage!({ data: header.buffer } as MessageEvent<ArrayBuffer>);
    expect(failure).toHaveBeenCalled();
    expect(dc.close).toHaveBeenCalled();
  });
});

describe("ACDS wire compatibility", () => {
  it("encodes fixed lookup and parses the native fixed JOINED response", () => {
    expect(lookupRequest("blue-mountain-tiger").length).toBe(49);
    expect(() => lookupRequest("bad name")).toThrow();
    const bytes = new Uint8Array(519);
    bytes[0] = 1;
    bytes[130] = 2;
    bytes[146] = 3;
    bytes[162] = 4;
    bytes[196] = 1;
    expect(parseJoined(bytes).hostId[0]).toBe(4);
    bytes[178] = 1;
    bytes[179] = 5;
    expect(parseJoined(bytes).hostId[0]).toBe(5);
    expect(() => parseJoined(bytes.slice(0, 518))).toThrow("Truncated");
  });
  it("accepts the host answer and rejects a different sender", () => {
    const local = {
      participantId: new Uint8Array(16).fill(1),
      hostId: new Uint8Array(16).fill(2),
      sessionId: new Uint8Array(16).fill(3),
      turnUsername: "",
      turnPassword: "",
    };
    const remote = {
      ...local,
      participantId: local.hostId,
      hostId: local.participantId,
    };
    const answer = signalPacket(remote, "answer", "v=0\r\n");
    expect(parseSignal(answer, local, false)).toEqual({
      type: "answer",
      sdp: "v=0\r\n",
    });
    answer[16] = 99;
    expect(() => parseSignal(answer, local, false)).toThrow("mismatch");
  });
});
