import { describe, it, expect, vi } from "vite-plus/test";
import { WebRTCBridge } from "../../src/network/WebRTCBridge";
import {
  discoveryRTCConfiguration,
  type DiscoveryOptions,
} from "../../src/network/WebRTCSession";
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
function packetOfType(type: number, length: number) {
  const bytes = packet(length);
  new DataView(bytes.buffer).setUint16(8, type, false);
  return bytes;
}

describe("Discovery connection settings", () => {
  const options: DiscoveryOptions = {
    sessionName: "blue-mountain-tiger",
    password: "",
    signalingUrl: "ws://localhost:27225",
    iceServers: [{ urls: ["stun:localhost:3478", "turn:localhost:3478"] }],
  };
  const joined = {
    turnUsername: "discovery-user",
    turnPassword: "discovery-password",
  };

  it("defaults to automatic routing and discovery credentials", () => {
    expect(discoveryRTCConfiguration(options, joined)).toEqual({
      iceTransportPolicy: "all",
      iceServers: [
        { urls: "stun:localhost:3478" },
        {
          urls: "turn:localhost:3478",
          username: joined.turnUsername,
          credential: joined.turnPassword,
        },
      ],
    });
  });
  it("uses custom credentials and enforces relay-only routing", () => {
    const config = discoveryRTCConfiguration(
      {
        ...options,
        iceTransportPolicy: "relay",
        turnUsername: "custom",
        turnCredential: "p:@%/",
      },
      joined,
    );
    expect(config.iceTransportPolicy).toBe("relay");
    expect(config.iceServers?.[1]).toEqual({
      urls: "turn:localhost:3478",
      username: "custom",
      credential: "p:@%/",
    });
  });
  it("rejects incomplete overrides instead of mixing credential sources", () => {
    expect(() =>
      discoveryRTCConfiguration({ ...options, turnUsername: "custom" }, joined),
    ).toThrow("both TURN");
    expect(() =>
      discoveryRTCConfiguration(
        { ...options, turnCredential: "custom" },
        joined,
      ),
    ).toThrow("both TURN");
  });
  it("keeps STUN when TURN lacks credentials and fails clearly for relay only", () => {
    const empty = { turnUsername: "", turnPassword: "" };
    expect(discoveryRTCConfiguration(options, empty).iceServers).toEqual([
      { urls: "stun:localhost:3478" },
    ]);
    expect(() =>
      discoveryRTCConfiguration(
        { ...options, iceTransportPolicy: "relay" },
        empty,
      ),
    ).toThrow("Relay only requires");
    expect(() =>
      discoveryRTCConfiguration(
        {
          ...options,
          iceServers: [{ urls: "stun:localhost:3478" }],
          iceTransportPolicy: "relay",
        },
        joined,
      ),
    ).toThrow("Relay only requires");
  });
  it("preserves per-server credentials ahead of discovery credentials", () => {
    const config = discoveryRTCConfiguration(
      {
        ...options,
        iceServers: [
          {
            urls: "turns:private:5349?transport=tcp",
            username: "private",
            credential: "private-password",
          },
        ],
      },
      joined,
    );
    expect(config.iceServers?.[0]).toEqual({
      urls: "turns:private:5349?transport=tcp",
      username: "private",
      credential: "private-password",
    });
  });
  it("does not combine an incomplete per-server override with discovery credentials", () => {
    const config = discoveryRTCConfiguration(
      {
        ...options,
        iceServers: [{ urls: "turn:localhost:3478", username: "incomplete" }],
      },
      joined,
    );
    expect(config.iceServers?.[0]).toEqual({
      urls: "turn:localhost:3478",
      username: joined.turnUsername,
      credential: joined.turnPassword,
    });
  });
});

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
  it("keeps a full raw frame together and replaces it while backpressured", () => {
    const dc = channel();
    Object.defineProperty(dc, "bufferedAmount", { value: 100000, writable: true });
    const bridge = new WebRTCBridge(dc, vi.fn(), vi.fn());
    const stale = packet(230424);
    const newest = packet(230424);

    bridge.send(stale, true);
    expect(dc.send).not.toHaveBeenCalled();
    bridge.send(newest, true);
    expect(dc.send).not.toHaveBeenCalled();

    Object.defineProperty(dc, "bufferedAmount", { value: 0 });
    dc.onbufferedamountlow!(new Event("bufferedamountlow"));
    const sent = vi.mocked(dc.send).mock.calls.map((call) => call[0]);
    expect(sent[0]).toEqual(newest.slice(0, 16384));
    expect(sent[sent.length - 1]).toEqual(
      newest.slice(Math.floor(newest.length / 16384) * 16384),
    );
    expect(sent).not.toContain(stale);
  });
  it("sends queued audio before a replaceable frame that has not started", () => {
    const dc = channel();
    Object.defineProperty(dc, "bufferedAmount", { value: 100000, writable: true });
    const bridge = new WebRTCBridge(dc, vi.fn(), vi.fn());
    const frame = packet(230424);
    const audio = packetOfType(4001, 150);

    bridge.send(frame, true);
    bridge.send(audio);
    expect(vi.mocked(dc.send).mock.calls[0]?.[0]).toEqual(audio);

    Object.defineProperty(dc, "bufferedAmount", { value: 0 });
    dc.onbufferedamountlow!(new Event("bufferedamountlow"));
    expect(vi.mocked(dc.send).mock.calls[1]?.[0]).toEqual(
      frame.slice(0, 16384),
    );
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
