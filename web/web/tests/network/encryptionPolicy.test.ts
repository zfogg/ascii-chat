import { beforeEach, describe, expect, it, vi } from "vite-plus/test";
import { ClientConnection } from "../../src/network/ClientConnection";
import {
  ConnectionState,
  PacketType,
  encryptPacket,
} from "../../src/wasm/client";

const sockets = vi.hoisted(() => ({ sent: [] as Uint8Array[] }));
vi.mock("../../src/network/SocketBridge", () => ({
  SocketBridge: class {
    constructor(private options: { onStateChange: (state: string) => void }) {}
    async connect() {
      this.options.onStateChange("open");
    }
    isConnected() {
      return true;
    }
    send(bytes: Uint8Array) {
      sockets.sent.push(bytes);
    }
    close() {}
  },
}));
vi.mock("../../src/wasm/client", async (importOriginal) => ({
  ...(await importOriginal<typeof import("../../src/wasm/client")>()),
  initClientWasm: vi.fn().mockResolvedValue(undefined),
  cleanupClientWasm: vi.fn(),
  generateKeypair: vi.fn().mockResolvedValue("public-key"),
  setServerAddress: vi.fn(),
  registerSendPacketCallback: vi.fn(),
  getConnectionState: () => 3,
  serializePacket: (type: number, payload: Uint8Array) => {
    const bytes = new Uint8Array(22 + payload.length);
    new DataView(bytes.buffer).setUint16(8, type, false);
    bytes.set(payload, 22);
    return bytes;
  },
  encryptPacket: vi.fn((bytes: Uint8Array) => bytes),
}));

describe("WebSocket encryption defaults", () => {
  beforeEach(() => {
    sockets.sent = [];
    vi.clearAllMocks();
  });
  it.each([
    ["ws://localhost:27226", true],
    ["wss://example.com", false],
  ] as const)("%s uses the correct default", async (serverUrl, encrypted) => {
    const connection = new ClientConnection({ serverUrl });
    const states: ConnectionState[] = [];
    connection.onStateChange((state) => states.push(state));
    await connection.connect();
    connection.sendPacket(PacketType.IMAGE_FRAME, new Uint8Array([1, 2, 3]));
    expect(encryptPacket).toHaveBeenCalledTimes(encrypted ? 1 : 0);
    expect(new DataView(sockets.sent[0]!.buffer).getUint16(8, false)).toBe(
      encrypted ? PacketType.ENCRYPTED : PacketType.IMAGE_FRAME,
    );
    expect(states).toContain(
      encrypted ? ConnectionState.HANDSHAKE : ConnectionState.CONNECTED,
    );
    connection.disconnect();
  });
  it("permits explicit application encryption on WSS", async () => {
    const connection = new ClientConnection({
      serverUrl: "wss://example.com",
      applicationEncryption: true,
    });
    await connection.connect();
    connection.sendPacket(PacketType.IMAGE_FRAME, new Uint8Array([1]));
    expect(encryptPacket).toHaveBeenCalledOnce();
    connection.disconnect();
  });
});
