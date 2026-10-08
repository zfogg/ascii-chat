import { describe, it, expect, vi } from "vite-plus/test";
import { ConnectionState } from "../../src/wasm/client";
import { WebRTCBridge } from "../../src/network/WebRTCBridge";
import {
  discoveryRTCConfiguration,
  WebRTCSession,
  type DiscoveryOptions,
} from "../../src/network/WebRTCSession";
import {
  AcdsType,
  lookupRequest,
  parseJoined,
  parseSignal,
  signalPacket,
} from "../../src/network/acdsProtocol";

const signalingHarness = vi.hoisted(() => ({
  current: null as null | {
    emit(state: number): void;
    sendPacket: ReturnType<typeof vi.fn>;
    options: {
      applicationEncryption?: boolean;
      discoveryHandshake?: boolean;
    };
  },
}));

vi.mock("../../src/network/ClientConnection", () => ({
  ClientConnection: class {
    private stateCallback: ((state: number) => void) | null = null;

    constructor(
      public options: {
        applicationEncryption?: boolean;
        discoveryHandshake?: boolean;
      },
    ) {
      signalingHarness.current = this;
    }
    onStateChange(callback: (state: number) => void) {
      this.stateCallback = callback;
    }
    onPacketReceived() {}
    connect() {
      return Promise.resolve();
    }
    sendPacket = vi.fn();
    getState() {
      return ConnectionState.DISCONNECTED;
    }
    disconnect() {}
    emit(state: number) {
      this.stateCallback?.(state);
    }
  },
}));

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
    stunServers: ["stun:localhost:3478"],
    turnServers: ["turn:localhost:3478"],
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
    expect(
      discoveryRTCConfiguration({ ...options, turnServers: [] }, empty)
        .iceServers,
    ).toEqual([
      { urls: "stun:localhost:3478" },
    ]);
    expect(() =>
      discoveryRTCConfiguration(options, empty),
    ).toThrow("TURN server requires credentials");
    expect(() =>
      discoveryRTCConfiguration(
        {
          ...options,
          turnServers: [],
          iceTransportPolicy: "relay",
        },
        joined,
      ),
    ).toThrow("Relay only requires");
  });
  it("uses explicit TURN credentials ahead of discovery credentials", () => {
    const config = discoveryRTCConfiguration(
      {
        ...options,
        turnServers: ["turns:private:5349?transport=tcp"],
        turnUsername: "private",
        turnCredential: "private-password",
      },
      joined,
    );
    expect(config.iceServers?.[1]).toEqual({
      urls: "turns:private:5349?transport=tcp",
      username: "private",
      credential: "private-password",
    });
  });
  it("uses discovery credentials when explicit credentials are blank", () => {
    const config = discoveryRTCConfiguration(
      {
        ...options,
        turnServers: ["turn:localhost:3478"],
      },
      joined,
    );
    expect(config.iceServers?.[1]).toEqual({
      urls: "turn:localhost:3478",
      username: joined.turnUsername,
      credential: joined.turnPassword,
    });
  });
});

describe("WebRTC signaling reconnects", () => {
  it("enables protocol negotiation and the transport-appropriate encryption default", () => {
    new WebRTCSession({
      sessionName: "blue-mountain-tiger",
      password: "",
      signalingUrl: "ws://localhost:27225",
      stunServers: [],
      turnServers: [],
    });

    expect(signalingHarness.current!.options.applicationEncryption).toBeUndefined();
    expect(signalingHarness.current!.options.discoveryHandshake).toBe(true);
  });

  it("repeats lookup after a transient reconnect before the session is joined", async () => {
    const options: DiscoveryOptions = {
      sessionName: "blue-mountain-tiger",
      password: "",
      signalingUrl: "ws://localhost:27225",
      stunServers: [],
      turnServers: [],
    };
    const session = new WebRTCSession(options);
    const attempt = session.connect();
    void attempt.catch(() => {});
    await Promise.resolve();

    const signaling = signalingHarness.current!;
    signaling.emit(ConnectionState.CONNECTED);
    expect(signaling.sendPacket).toHaveBeenCalledTimes(1);
    expect(signaling.sendPacket).toHaveBeenLastCalledWith(
      AcdsType.LOOKUP,
      lookupRequest(options.sessionName),
    );

    signaling.emit(ConnectionState.CONNECTING);
    signaling.emit(ConnectionState.HANDSHAKE);
    expect(signaling.sendPacket).toHaveBeenCalledTimes(1);
    signaling.emit(ConnectionState.CONNECTED);

    expect(signaling.sendPacket).toHaveBeenCalledTimes(2);
    session.disconnect();
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
    Object.defineProperty(dc, "bufferedAmount", {
      value: 300000,
      writable: true,
    });
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
  it("sends one raw frame without pausing inside the 256 KiB live window", () => {
    const dc = channel();
    Object.defineProperty(dc, "bufferedAmount", { value: 0, writable: true });
    vi.mocked(dc.send).mockImplementation((data) => {
      Object.defineProperty(dc, "bufferedAmount", {
        value: dc.bufferedAmount + (data as Uint8Array).byteLength,
        writable: true,
      });
    });
    const bridge = new WebRTCBridge(dc, vi.fn(), vi.fn());
    const frame = packetOfType(3001, 230424);

    bridge.send(frame, true);

    const sentBytes = vi
      .mocked(dc.send)
      .mock.calls.reduce(
        (total, [chunk]) => total + (chunk as Uint8Array).byteLength,
        0,
      );
    expect(sentBytes).toBe(frame.byteLength);
    expect(dc.send).toHaveBeenCalledTimes(15);
  });
  it("sends queued audio before a replaceable frame that has not started", () => {
    const dc = channel();
    Object.defineProperty(dc, "bufferedAmount", {
      value: 300000,
      writable: true,
    });
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
  it("dispatches every received ASCII snapshot without waiting for animation frames", () => {
    const dc = channel(),
      received = vi.fn(),
      failure = vi.fn();
    const requestFrame = vi.fn();
    vi.stubGlobal("requestAnimationFrame", requestFrame);
    try {
      new WebRTCBridge(dc, received, failure);
      const firstFrame = packetOfType(3000, 10);
      const secondFrame = packetOfType(3000, 20);

      dc.onmessage!({ data: firstFrame.buffer } as MessageEvent<ArrayBuffer>);
      dc.onmessage!({ data: secondFrame.buffer } as MessageEvent<ArrayBuffer>);

      expect(received.mock.calls.map(([packet]) => packet)).toEqual([
        firstFrame,
        secondFrame,
      ]);
      expect(requestFrame).not.toHaveBeenCalled();
      expect(failure).not.toHaveBeenCalled();
    } finally {
      vi.unstubAllGlobals();
    }
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
