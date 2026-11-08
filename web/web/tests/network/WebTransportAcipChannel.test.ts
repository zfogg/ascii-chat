import { describe, expect, it, vi } from "vite-plus/test";
import type { WebTransportPacket } from "../../src/network/WebTransportBridge";

const state = vi.hoisted(() => ({
  onPacket: null as ((packet: WebTransportPacket) => void) | null,
  onError: null as ((error: Error) => void) | null,
  sent: [] as Array<{ flow: number; delivery: string; packet: Uint8Array }>,
  init: vi.fn(),
  close: vi.fn(),
  begin: vi.fn(() => 0),
  receive: vi.fn(() => 0),
  cryptoPolicy: vi.fn(() => 1),
  sendCallback: null as ((packet: Uint8Array) => void) | null,
}));
function packet(type: number, payload: Uint8Array): Uint8Array {
  const result = new Uint8Array(22 + payload.length);
  new DataView(result.buffer).setUint16(8, type);
  result.set(payload, 22);
  return result;
}
function wrap(inner: Uint8Array): Uint8Array {
  return packet(1200, inner);
}
vi.mock("../../src/wasm/client", () => ({
  initPacketSecurity: state.init,
  tickPacketSecurity: () => [],
  getClientModule: () => ({
    _client_handshake_begin: state.begin,
    receiveHandshake: state.receive,
  }),
  getConnectionState: () => 3,
  ConnectionState: { CONNECTED: 3 },
  registerSendPacketCallback: (callback: (packet: Uint8Array) => void) => {
    state.sendCallback = callback;
  },
  resolveCryptoPolicy: state.cryptoPolicy,
  parsePacket: (value: Uint8Array) => ({
    type: new DataView(value.buffer, value.byteOffset).getUint16(8),
  }),
  throwWasmError: () => {
    throw new Error("WASM failure");
  },
  serializePacket: packet,
  processSecurePacket: (
    operation: string,
    _flow: number,
    _delivery: number,
    value: Uint8Array,
  ) => ({
    packet: operation === "seal" ? wrap(value) : value.slice(22),
    sequence: 1n,
  }),
}));
vi.mock("../../src/network/WebTransportBridge", () => ({
  createBrowserWebTransportBridge: (
    _url: string,
    onPacket: typeof state.onPacket,
    onError: typeof state.onError,
  ) => {
    state.onPacket = onPacket;
    state.onError = onError;
    return {
      connect: async () => {},
      close: state.close,
      getBufferedAmount: () => 0,
      send: async (flow: number, delivery: string, value: Uint8Array) => {
        state.sent.push({ flow, delivery, packet: value });
      },
    };
  },
}));
import { WebTransportAcipChannel } from "../../src/network/WebTransportAcipChannel";

describe("WebTransport ACIP channel", () => {
  it("runs the prepared WASM handshake with encryption required before readiness", async () => {
    state.sent = [];
    state.init.mockClear();
    state.begin.mockClear();
    state.receive.mockClear();
    state.cryptoPolicy.mockClear();
    const ready = vi.fn(),
      failed = vi.fn();
    const channel = new WebTransportAcipChannel("https://localhost/", {
      handshake: vi.fn(),
      packet: vi.fn(),
      ready,
      keyframe: vi.fn(),
      failed,
    });
    await channel.connectWithWasmHandshake();
    expect(state.cryptoPolicy).toHaveBeenCalledWith("https://localhost/", true);
    expect(state.begin).toHaveBeenCalledOnce();
    state.sendCallback!(packet(1, new Uint8Array()));
    expect(state.sent.at(-1)).toMatchObject({ flow: 0, delivery: "reliable" });
    state.onPacket!({
      flow: 0,
      delivery: "reliable",
      packet: packet(1108, new Uint8Array()),
    });
    expect(state.receive).toHaveBeenCalledOnce();
    expect(state.init).toHaveBeenCalledOnce();
    expect(channel.isReady()).toBe(false);
    state.onPacket!({
      flow: 0,
      delivery: "reliable",
      packet: wrap(packet(1110, Uint8Array.from([65, 67, 81, 82, 1]))),
    });
    state.onPacket!({
      flow: 0,
      delivery: "reliable",
      packet: wrap(packet(1110, Uint8Array.from([65, 67, 81, 65, 1]))),
    });
    expect(ready).toHaveBeenCalledOnce();
    expect(failed).not.toHaveBeenCalled();
    channel.close();
  });
  it("binds handshake, encrypted readiness and application delivery", async () => {
    state.sent = [];
    state.init.mockClear();
    state.close.mockClear();
    const handshake = vi.fn(),
      received = vi.fn(),
      ready = vi.fn(),
      failed = vi.fn();
    const channel = new WebTransportAcipChannel("https://localhost/", {
      handshake,
      packet: received,
      ready,
      keyframe: vi.fn(),
      failed,
    });
    await channel.connect();
    const hello = packet(1, new Uint8Array());
    channel.sendHandshake(hello);
    state.onPacket!({ flow: 0, delivery: "reliable", packet: hello });
    expect(handshake).toHaveBeenCalledWith(hello);
    channel.authenticated();
    expect(state.init).toHaveBeenCalledOnce();
    const receive = (type: number, payload: number[], flow: 0 | 1 = 0) =>
      state.onPacket!({
        flow,
        delivery: "reliable",
        packet: wrap(packet(type, Uint8Array.from(payload))),
      });
    receive(1110, [65, 67, 81, 82, 1]);
    expect(channel.isReady()).toBe(false);
    receive(1110, [65, 67, 81, 65, 1]);
    expect(channel.isReady()).toBe(true);
    expect(ready).toHaveBeenCalledOnce();
    channel.send(4000, Uint8Array.from([7]));
    expect(state.sent.at(-1)).toMatchObject({ flow: 1, delivery: "reliable" });
    receive(4000, [8], 1);
    expect(received).toHaveBeenCalledOnce();
    expect(failed).not.toHaveBeenCalled();
    channel.close();
    expect(state.close).toHaveBeenCalledOnce();
  });
});
