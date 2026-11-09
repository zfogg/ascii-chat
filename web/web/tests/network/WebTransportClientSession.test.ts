import { describe, expect, it, vi } from "vite-plus/test";
import type { WebTransportAcipCallbacks } from "../../src/network/WebTransportAcipChannel";

const state = vi.hoisted(() => ({
  callbacks: null as WebTransportAcipCallbacks | null,
  sends: [] as Array<{
    type: number;
    payload: Uint8Array;
    delivery: string;
    deadline: number;
  }>,
  supported: true,
  close: vi.fn(),
  cleanup: vi.fn(),
  init: vi.fn(async () => {}),
  configure: vi.fn(async () => {}),
  keypair: vi.fn(async () => "client-key"),
  address: vi.fn(),
  module: { sendPacketCallback: undefined as ((packet: Uint8Array) => void) | undefined },
  endpoint: "",
  process: vi.fn((event: { type: string; [key: string]: any }) => {
    if (event.type === "serialize") {
      const payload = event["payload"] as Uint8Array;
      const result = new Uint8Array(22 + payload.length);
      new DataView(result.buffer).setUint16(8, Number(event["packetType"]));
      result.set(payload, 22);
      return { code: 0, bytes: result, state: 1 };
    }
    if (event.type === "parse")
      return { code: 0, packet: { type: new DataView((event["packet"] as Uint8Array).buffer).getUint16(8) }, state: 1 };
    if (event.type === "quic-state") return { code: 0, epoch: 0, state: 1 };
    return { code: 0, state: 1 };
  }),
}));
vi.mock("../../src/wasm/client", () => ({
  ConnectionState: {
    DISCONNECTED: 0,
    CONNECTING: 1,
    HANDSHAKE: 2,
    CONNECTED: 3,
    ERROR: 4,
  },
  PacketType: {
    IMAGE_FRAME: 3001,
    ASCII_FRAME: 3000,
    IMAGE_FRAME_H264: 3002,
    IMAGE_FRAME_H265: 3003,
  },
  cleanupClientWasm: state.cleanup,
  initClientWasm: state.init,
  configureClientCrypto: state.configure,
  generateKeypair: state.keypair,
  getClientModule: () => state.module,
  processClientEvent: (event: { type: string; [key: string]: unknown }) => {
    if (event.type === "set-server") {
      state.address(event["host"], event["port"]);
    }
    if (event.type === "serialize") {
      const payload = event["payload"] as Uint8Array;
      const bytes = new Uint8Array(22 + payload.length);
      new DataView(bytes.buffer).setUint16(8, Number(event["packetType"]));
      bytes.set(payload, 22);
      return { code: 0, state: 1, bytes };
    }
    if (event.type === "parse")
      return {
        code: 0,
        state: 1,
        packet: { type: new DataView((event["packet"] as Uint8Array).buffer).getUint16(8) },
      };
    return { code: 0, state: 1, mode: 1 };
  },
}));
vi.mock("../../src/network/WebTransportAcipChannel", () => ({
  WebTransportAcipChannel: class {
    constructor(url: string, callbacks: WebTransportAcipCallbacks) {
      state.endpoint = url;
      state.callbacks = callbacks;
    }
    async connectWithWasmHandshake() {}
    close() {
      state.close();
    }
    getBufferedAmount() {
      return 0;
    }
    supportsDatagrams() {
      return state.supported;
    }
    send(
      type: number,
      payload: Uint8Array,
      delivery: string,
      deadline: number,
    ) {
      state.sends.push({ type, payload: payload.slice(), delivery, deadline });
    }
  },
}));
import { WebTransportClientSession } from "../../src/network/WebTransportClientSession";

describe("WebTransport client session", () => {
  it("connects a quic URL through the HTTPS WebTransport endpoint", async () => {
    const session = new WebTransportClientSession({
      serverUrl: "quic://chat.example:4443",
    });
    await session.connect();
    expect(state.endpoint).toBe("https://chat.example:4443/acip");
    expect(state.address).toHaveBeenCalledWith("chat.example", 4443);
    session.disconnect();
  });

  it("starts ACIP security and dispatches negotiated media with keyframe recovery", async () => {
    state.sends = [];
    state.supported = true;
    state.close.mockClear();
    state.cleanup.mockClear();
    const session = new WebTransportClientSession({
      serverUrl: "https://localhost:4443/chat",
      width: 40,
      height: 12,
    });
    const states: number[] = [],
      received = vi.fn();
    session.onStateChange((value) => states.push(value));
    session.onPacketReceived(received);
    await session.connect();
    expect(state.init).toHaveBeenCalledWith({ width: 40, height: 12 });
    expect(state.address).toHaveBeenCalledWith("localhost", 4443);
    expect(session.getPublicKey()).toBe("client-key");
    expect(() => session.sendPacket(4000, Uint8Array.from([1]))).toThrow(
      /negotiation/,
    );
    state.callbacks!.ready();
    expect(states).toEqual([1, 3]);
    session.sendUnencryptedAcipPacket(5001, Uint8Array.from([7]));
    session.sendPacket(3002, Uint8Array.from([1, 2]));
    expect(state.sends.map((item) => item.delivery)).toEqual([
      "reliable",
      "datagram",
    ]);
    expect(state.sends[1]!.deadline).toBeGreaterThan(0);
    expect(session.needsKeyframe(3002)).toBe(false);
    state.callbacks!.keyframe(3002, 0);
    expect(session.needsKeyframe(3002)).toBe(true);
    state.callbacks!.datagramDropped?.(3002, new Error("expired"));
    expect(session.getUploadFeedback().droppedFrames).toBe(1);
    session.sendPacket(3001, new Uint8Array(64 * 1024));
    expect(state.sends.at(-1)!.delivery).toBe("reliable");
    expect(state.sends.at(-1)!.deadline).toBe(0);
    const incoming = new Uint8Array(23);
    new DataView(incoming.buffer).setUint16(8, 4000);
    incoming[22] = 5;
    state.callbacks!.packet({
      flow: 1,
      delivery: "datagram",
      packet: incoming,
    });
    expect(received).toHaveBeenCalledWith({ type: 4000 }, Uint8Array.from([5]));
    session.disconnect();
    expect(state.close).toHaveBeenCalledOnce();
    expect(state.cleanup).toHaveBeenCalledOnce();
    expect(session.getState()).toBe(0);
  });
  it("drains a cancelled key setup before initializing its replacement", async () => {
    state.init.mockClear();
    state.keypair.mockClear();
    state.cleanup.mockClear();
    let release!: (value: string) => void;
    state.keypair.mockImplementationOnce(
      () =>
        new Promise<string>((resolve) => {
          release = resolve;
        }),
    );
    const first = new WebTransportClientSession({
      serverUrl: "https://first.example/",
    });
    const pendingFirst = first.connect();
    await vi.waitFor(() => expect(state.keypair).toHaveBeenCalledOnce());
    first.disconnect();
    expect(state.cleanup).not.toHaveBeenCalled();
    const second = new WebTransportClientSession({
      serverUrl: "https://second.example/",
    });
    const pendingSecond = second.connect();
    await Promise.resolve();
    expect(state.init).toHaveBeenCalledOnce();
    release("old-key");
    await pendingFirst;
    await pendingSecond;
    expect(state.cleanup).toHaveBeenCalledOnce();
    expect(state.init).toHaveBeenCalledTimes(2);
    expect(second.getPublicKey()).toBe("client-key");
    second.disconnect();
  });
  it("uses reliable media when datagrams are unavailable and refuses disabled ACIP encryption", async () => {
    state.sends = [];
    state.supported = false;
    const session = new WebTransportClientSession({
      serverUrl: "https://localhost/",
    });
    await session.connect();
    state.callbacks!.ready();
    session.sendPacket(4000, Uint8Array.from([1]));
    expect(state.sends[0]!.delivery).toBe("reliable");
    session.disconnect();
    const unsafe = new WebTransportClientSession({
      serverUrl: "https://localhost/",
      applicationEncryption: false,
    });
    await expect(unsafe.connect()).rejects.toThrow(/requires ACIP/);
  });
});
