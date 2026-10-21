import { describe, it, expect, vi } from "vite-plus/test";
import { ConnectionState, PacketType } from "../../src/wasm/client";
import { WebRTCBridge } from "../../src/network/WebRTCBridge";
import {
  discoveryRTCConfiguration,
  WebRTCSession,
  type DiscoveryOptions,
} from "../../src/network/WebRTCSession";
import {
  AcdsType,
  parseSignal,
  signalPacket,
} from "../../src/network/acdsProtocol";

vi.mock("../../src/wasm/client", async (original) => ({
  ...(await original<typeof import("../../src/wasm/client")>()),
  serializePacket(type: number, payload: Uint8Array) {
    const bytes = new Uint8Array(22 + payload.length);
    const view = new DataView(bytes.buffer);
    view.setUint16(8, type, false);
    view.setUint32(10, payload.length, false);
    bytes.set(payload, 22);
    return bytes;
  },
}));

const signalingHarness = vi.hoisted(() => ({
  current: null as null | {
    emit(state: number): void;
    packet(type: number, payload: Uint8Array): void;
    sendPacket: ReturnType<typeof vi.fn>;
    options: { applicationEncryption?: boolean; discoveryHandshake?: boolean };
  },
}));

vi.mock("../../src/network/ClientConnection", () => ({
  ClientConnection: class {
    private stateCallback: ((state: number) => void) | null = null;
    private packetCallback:
      | ((packet: { type: number }, payload: Uint8Array) => void)
      | null = null;

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
    onPacketReceived(
      callback: (packet: { type: number }, payload: Uint8Array) => void,
    ) {
      this.packetCallback = callback;
    }
    packet(type: number, payload: Uint8Array) {
      this.packetCallback?.({ type }, payload);
    }
    connect() {
      return Promise.resolve();
    }
    sendPacket = vi.fn();
    getState() {
      return ConnectionState.DISCONNECTED;
    }
    disconnect = vi.fn();
    emit(state: number) {
      this.stateCallback?.(state);
    }
  },
}));

vi.mock("../../src/network/DiscoveryProtocol", async () => {
  const { SessionRejectedError } =
    await import("../../src/network/acdsProtocol");
  return {
    DiscoveryProtocol: class {
      constructor(private send: (type: number, payload: Uint8Array) => void) {}
      enter() {
        this.send(AcdsType.LOOKUP, new Uint8Array());
      }
      receive() {
        throw new SessionRejectedError("Session rejected by shared C protocol");
      }
      close() {}
    },
  };
});

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
    ).toEqual([{ urls: "stun:localhost:3478" }]);
    expect(() => discoveryRTCConfiguration(options, empty)).toThrow(
      "TURN server requires credentials",
    );
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
  it("keeps media credentials separate from discovery-service credentials", () => {
    new WebRTCSession({
      sessionName: "blue-mountain-tiger",
      password: "media-password",
      signalingUrl: "wss://localhost",
      applicationEncryption: true,
      cryptoOptions: {
        password: "media-password",
        expectedServerPublicKeyText: "media-key",
      },
      stunServers: [],
      turnServers: [],
    });
    expect(
      signalingHarness.current!.options.applicationEncryption,
    ).toBeUndefined();
    expect(signalingHarness.current!.options).not.toHaveProperty(
      "cryptoOptions",
    );
  });

  it.each([AcdsType.INFO, AcdsType.JOINED, AcdsType.END])(
    "stops retrying rejected/ended session response %s",
    async (type) => {
      const session = new WebRTCSession({
        sessionName: "blue-mountain-tiger",
        password: "",
        signalingUrl: "ws://localhost",
        stunServers: [],
        turnServers: [],
      });
      const result = session.connect();
      const rejected = expect(result).rejects.toThrow();
      signalingHarness.current!.emit(ConnectionState.CONNECTED);
      signalingHarness.current!.packet(
        type,
        new Uint8Array(type === AcdsType.JOINED ? 519 : 72),
      );
      await rejected;
      expect(session.canReconnect).toBe(false);
      expect(session.getState()).toBe(ConnectionState.ERROR);
    },
  );
  it("enables protocol negotiation and the transport-appropriate encryption default", () => {
    new WebRTCSession({
      sessionName: "blue-mountain-tiger",
      password: "",
      signalingUrl: "ws://localhost:27225",
      stunServers: [],
      turnServers: [],
    });

    expect(
      signalingHarness.current!.options.applicationEncryption,
    ).toBeUndefined();
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
      expect.any(Uint8Array),
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
  it("preserves every wire packet in order after encryption", () => {
    const dc = channel();
    Object.defineProperty(dc, "bufferedAmount", {
      value: 300000,
      writable: true,
    });
    const bridge = new WebRTCBridge(dc, vi.fn(), vi.fn());
    const oldFrame = packet(100),
      control = packet(12),
      newest = packet(200);
    bridge.send(oldFrame);
    bridge.send(control);
    bridge.send(newest);
    Object.defineProperty(dc, "bufferedAmount", { value: 0 });
    dc.onbufferedamountlow!(new Event("bufferedamountlow"));
    expect(vi.mocked(dc.send).mock.calls.map((call) => call[0])).toEqual([
      oldFrame,
      control,
      newest,
    ]);
  });
  it("finishes partial wire packets and preserves subsequent wire order", () => {
    const dc = channel();
    Object.defineProperty(dc, "bufferedAmount", { value: 0, writable: true });
    vi.mocked(dc.send).mockImplementation(() => {
      Object.defineProperty(dc, "bufferedAmount", { value: 300000 });
    });
    const bridge = new WebRTCBridge(dc, vi.fn(), vi.fn(), 8192);
    const first = packet(12000),
      stale = packet(100),
      newest = packet(200);
    bridge.send(first);
    bridge.send(stale);
    bridge.send(newest);
    vi.mocked(dc.send).mockImplementation(() => {});
    Object.defineProperty(dc, "bufferedAmount", { value: 0 });
    dc.onbufferedamountlow!(new Event("bufferedamountlow"));
    expect(vi.mocked(dc.send).mock.calls.map((call) => call[0])).toEqual([
      first.slice(0, 8192),
      first.slice(8192),
      stale,
      newest,
    ]);
  });
  it("drains every complete wire frame after backpressure", () => {
    const dc = channel();
    Object.defineProperty(dc, "bufferedAmount", {
      value: 300000,
      writable: true,
    });
    const bridge = new WebRTCBridge(dc, vi.fn(), vi.fn());
    const first = packet(230424),
      second = packet(230424);
    first[30] = 1;
    second[30] = 2;
    bridge.send(first);
    bridge.send(second);
    expect(dc.send).not.toHaveBeenCalled();
    Object.defineProperty(dc, "bufferedAmount", { value: 0 });
    dc.onbufferedamountlow!(new Event("bufferedamountlow"));
    const sent = vi
      .mocked(dc.send)
      .mock.calls.flatMap(([chunk]) => Array.from(chunk as Uint8Array));
    expect(sent).toEqual([...first, ...second]);
    bridge.close();
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

    bridge.send(frame);

    const sentBytes = vi
      .mocked(dc.send)
      .mock.calls.reduce(
        (total, [chunk]) => total + (chunk as Uint8Array).byteLength,
        0,
      );
    expect(sentBytes).toBe(frame.byteLength);
    expect(dc.send).toHaveBeenCalledTimes(15);
  });
  it("never reorders audio ahead of encrypted wire packets", () => {
    const dc = channel();
    Object.defineProperty(dc, "bufferedAmount", {
      value: 300000,
      writable: true,
    });
    const bridge = new WebRTCBridge(dc, vi.fn(), vi.fn());
    const frame = packet(230424);
    const audio = packetOfType(4001, 150);

    bridge.send(frame);
    bridge.send(audio);
    expect(dc.send).not.toHaveBeenCalled();

    Object.defineProperty(dc, "bufferedAmount", { value: 0 });
    dc.onbufferedamountlow!(new Event("bufferedamountlow"));
    expect(vi.mocked(dc.send).mock.calls[0]?.[0]).toEqual(
      frame.slice(0, 16384),
    );
    expect(vi.mocked(dc.send).mock.calls.at(-1)?.[0]).toEqual(audio);
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

describe("handoff with queued media", () => {
  const options: DiscoveryOptions = {
    sessionName: "blue-mountain-tiger",
    password: "",
    signalingUrl: "ws://localhost",
    stunServers: [],
    turnServers: [],
  };
  it("discards old plaintext before replacing a peer and its crypto context", () => {
    const session = new WebRTCSession(options);
    const oldCrypto = { sendPacket: vi.fn(), close: vi.fn() };
    let buffered = 300000;
    Reflect.set(session, "state", ConnectionState.CONNECTED);
    Reflect.set(session, "bridge", {
      getBufferedAmount: () => buffered,
      close: vi.fn(),
    });
    Reflect.set(session, "peerCrypto", oldCrypto);
    session.sendPacket(PacketType.IMAGE_FRAME, new Uint8Array(8));
    expect(session.getBufferedAmount()).toBeGreaterThan(buffered);
    const oldQueue = Reflect.get(session, "outbound");
    Reflect.get(session, "closePeer").call(session);
    buffered = 0;
    oldQueue.flush();
    expect(oldCrypto.sendPacket).not.toHaveBeenCalled();
    expect(oldCrypto.close).toHaveBeenCalledOnce();
    const nextCrypto = { sendPacket: vi.fn(), close: vi.fn() };
    Reflect.set(session, "bridge", {
      getBufferedAmount: () => 0,
      close: vi.fn(),
    });
    Reflect.set(session, "peerCrypto", nextCrypto);
    expect(session.needsKeyframe(PacketType.IMAGE_FRAME_H264)).toBe(true);
    session.sendPacket(
      PacketType.IMAGE_FRAME_H264,
      new Uint8Array([1, 0, 1, 0, 1, 0]),
    );
    expect(nextCrypto.sendPacket).toHaveBeenCalledOnce();
    session.disconnect();
  });
  it("uses the promoted host's queue and recovery state", () => {
    const session = new WebRTCSession(options);
    const feedback = {
      bufferedBytes: 123,
      oldestFrameAgeMs: 80,
      droppedFrames: 7,
    };
    const host = {
      getBufferedAmount: () => 123,
      getUploadFeedback: vi.fn(() => feedback),
      needsKeyframe: vi.fn(() => false),
      sendPacket: vi.fn(),
      disconnect: vi.fn(),
    };
    Reflect.set(session, "promoted", host);
    expect(session.getBufferedAmount()).toBe(123);
    expect(session.getUploadFeedback()).toBe(feedback);
    expect(host.getUploadFeedback).toHaveBeenCalledOnce();
    expect(session.needsKeyframe(PacketType.IMAGE_FRAME_H264)).toBe(false);
    const payload = new Uint8Array([1]);
    session.sendPacket(PacketType.IMAGE_FRAME_H264, payload);
    expect(host.sendPacket).toHaveBeenCalledWith(
      PacketType.IMAGE_FRAME_H264,
      payload,
    );
    session.disconnect();
  });
});

it("retains signaling after the audio congestion deadline and bounds successor recovery", async () => {
  vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout", "performance"] });
  const session = new WebRTCSession({
    sessionName: "blue-mountain-tiger",
    password: "",
    signalingUrl: "ws://localhost",
    stunServers: [],
    turnServers: [],
  });
  const signaling = signalingHarness.current as unknown as {
    disconnect: ReturnType<typeof vi.fn>;
  };
  const media = session as unknown as {
    state: ConnectionState;
    bridge: object;
    peerCrypto: object;
  };
  const bridgeClosed = vi.fn(),
    cryptoClosed = vi.fn();
  media.state = ConnectionState.CONNECTED;
  media.bridge = { getBufferedAmount: () => 1024, close: bridgeClosed };
  media.peerCrypto = { sendPacket: vi.fn(), close: cryptoClosed };
  try {
    session.sendPacket(PacketType.AUDIO_OPUS_BATCH, new Uint8Array(32));
    await vi.advanceTimersByTimeAsync(5004);
    expect(bridgeClosed).toHaveBeenCalledOnce();
    expect(cryptoClosed).toHaveBeenCalledOnce();
    expect(session.getState()).toBe(ConnectionState.CONNECTING);
    expect(signaling.disconnect).not.toHaveBeenCalled();
    // Audio's five-second congestion bound remains enforced; the failed
    // queue closes, and the independent successor wait is still finite.
    await vi.advanceTimersByTimeAsync(15000);
    expect(session.getState()).toBe(ConnectionState.ERROR);
    expect(signaling.disconnect).toHaveBeenCalledOnce();
  } finally {
    session.disconnect();
    vi.useRealTimers();
  }
});
