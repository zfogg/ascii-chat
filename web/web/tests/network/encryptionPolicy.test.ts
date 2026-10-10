import { beforeEach, describe, expect, it, vi } from "vite-plus/test";
import { ClientConnection } from "../../src/network/ClientConnection";
import {
  ConnectionState,
  PacketType,
  configureClientCrypto,
  encryptPacket,
  initClientWasm,
} from "../../src/wasm/client";

const sockets = vi.hoisted(() => ({
  sent: [] as Uint8Array[],
  instances: [] as Array<{
    emit(state: string): void;
    isConnected(): boolean;
  }>,
}));
vi.mock("../../src/network/SocketBridge", () => ({
  SocketBridge: class {
    private connected = false;
    constructor(private options: { onStateChange: (state: string) => void }) {
      sockets.instances.push(this);
    }
    async connect() {
      this.connected = true;
      this.options.onStateChange("open");
    }
    isConnected() {
      return this.connected;
    }
    emit(state: string) {
      this.connected = state === "open";
      this.options.onStateChange(state);
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
  configureClientCrypto: vi.fn().mockResolvedValue(undefined),
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
    sockets.instances = [];
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
      encrypted ? ConnectionState.CONNECTING : ConnectionState.CONNECTED,
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

  it.each([
    ["ws://localhost:27226", true, ConnectionState.CONNECTING],
    ["wss://example.com", false, ConnectionState.CONNECTED],
  ] as const)(
    "%s sends the discovery protocol version before signaling",
    async (serverUrl, supportsEncryption, expectedState) => {
      const connection = new ClientConnection({
        serverUrl,
        discoveryHandshake: true,
      });
      const states: ConnectionState[] = [];
      connection.onStateChange((state) => states.push(state));
      await connection.connect();

      const versionPacket = sockets.sent[0]!;
      expect(new DataView(versionPacket.buffer).getUint16(8, false)).toBe(
        PacketType.PROTOCOL_VERSION,
      );
      expect(versionPacket[26]).toBe(supportsEncryption ? 1 : 0);
      expect(states).toContain(expectedState);
      connection.disconnect();
    },
  );

  it("waits for WASM crypto reinitialization before sending version on retry", async () => {
    let finishReinit!: () => void;
    const reinit = new Promise<void>((resolve) => {
      finishReinit = resolve;
    });
    vi.mocked(initClientWasm)
      .mockResolvedValueOnce(undefined)
      .mockImplementationOnce(() => reinit);

    const connection = new ClientConnection({
      serverUrl: "ws://localhost:27226",
      discoveryHandshake: true,
    });
    await connection.connect();
    expect(configureClientCrypto).toHaveBeenCalledTimes(1);
    expect(sockets.sent).toHaveLength(1);
    Object.assign(connection, { wasEverConnected: true });

    const socket = sockets.instances[0]!;
    socket.emit("closed");
    socket.emit("connecting");
    socket.emit("open");
    await Promise.resolve();

    expect(sockets.sent).toHaveLength(1);
    expect(configureClientCrypto).toHaveBeenCalledTimes(1);
    finishReinit();
    await vi.waitFor(() => expect(sockets.sent).toHaveLength(2));
    expect(configureClientCrypto).toHaveBeenCalledTimes(2);
    expect(new DataView(sockets.sent[1]!.buffer).getUint16(8, false)).toBe(
      PacketType.PROTOCOL_VERSION,
    );
    connection.disconnect();
  });
  it("waits for crypto configuration before opening the socket", async () => {
    let finishConfiguration!: () => void;
    vi.mocked(configureClientCrypto).mockImplementationOnce(
      () =>
        new Promise<void>((resolve) => {
          finishConfiguration = resolve;
        }),
    );
    const connection = new ClientConnection({
      serverUrl: "ws://localhost:27226",
      discoveryHandshake: true,
    });
    const connecting = connection.connect();
    await vi.waitFor(() =>
      expect(configureClientCrypto).toHaveBeenCalledOnce(),
    );
    expect(sockets.instances).toHaveLength(0);
    expect(sockets.sent).toHaveLength(0);
    finishConfiguration();
    await connecting;
    expect(sockets.instances).toHaveLength(1);
    expect(sockets.sent).toHaveLength(1);
    connection.disconnect();
  });

  it("does not open a socket when crypto configuration fails", async () => {
    vi.mocked(configureClientCrypto).mockRejectedValueOnce(
      new Error("Invalid crypto configuration"),
    );
    const connection = new ClientConnection({
      serverUrl: "ws://localhost:27226",
    });
    await expect(connection.connect()).rejects.toThrow(
      "Invalid crypto configuration",
    );
    expect(sockets.instances).toHaveLength(0);
    expect(sockets.sent).toHaveLength(0);
    connection.disconnect();
  });
});
