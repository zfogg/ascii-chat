import { beforeEach, describe, expect, it, vi } from "vite-plus/test";
import { ClientConnection } from "../../src/network/ClientConnection";
import {
  ConnectionState,
  PacketType,
  configureClientCrypto,
  initClientWasm,
  processClientEvent,
} from "../../src/wasm/client";

const beginHandshake = vi.hoisted(() => vi.fn((_mode?: unknown) => 0));
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
    close() {
      this.emit("closed");
    }
  },
}));
vi.mock("../../src/wasm/client", async (importOriginal) => ({
  ...(await importOriginal<typeof import("../../src/wasm/client")>()),
  initClientWasm: vi.fn().mockResolvedValue(undefined),
  cleanupClientWasm: vi.fn(),
  configureClientCrypto: vi.fn().mockResolvedValue(undefined),
  generateKeypair: vi.fn().mockResolvedValue("public-key"),
  processClientEvent: vi.fn((event: Record<string, unknown>) => {
    if (event["type"] === "begin-handshake") {
      const code = beginHandshake(event["mode"]);
      return { code, state: ConnectionState.HANDSHAKE };
    }
    if (event["type"] === "serialize") {
      const payload = event["payload"] as Uint8Array;
      const bytes = new Uint8Array(22 + payload.length);
      new DataView(bytes.buffer).setUint16(
        8,
        event["packetType"] as number,
        false,
      );
      bytes.set(payload, 22);
      return { code: 0, state: ConnectionState.CONNECTED, bytes };
    }
    if (event["type"] === "crypt")
      return {
        code: 0,
        state: ConnectionState.CONNECTED,
        bytes: event["packet"],
      };
    if (event["type"] === "state")
      return { code: 0, state: ConnectionState.CONNECTED };
    if (event["type"] === "resolve-crypto-policy")
      return {
        code: 0,
        state: ConnectionState.CONNECTED,
        mode:
          event["encryption"] === -1
            ? event["wss"]
              ? 0
              : 1
            : event["encryption"],
      };
    if (event["type"] === "set-server")
      return { code: 0, state: ConnectionState.CONNECTED };
    throw new Error(`Unexpected client event: ${String(event["type"])}`);
  }),
}));

describe("WebSocket encryption defaults", () => {
  beforeEach(() => {
    sockets.sent = [];
    sockets.instances = [];
    vi.clearAllMocks();
    beginHandshake.mockReturnValue(0);
  });
  it.each([
    ["ws://localhost:27226", true],
    ["wss://example.com", false],
  ] as const)("%s uses the correct default", async (serverUrl, encrypted) => {
    const connection = new ClientConnection({ serverUrl });
    const states: ConnectionState[] = [];
    connection.onStateChange((state) => states.push(state));
    await connection.connect();
    Object.assign(connection, { transportState: ConnectionState.CONNECTED });
    connection.sendPacket(PacketType.IMAGE_FRAME, new Uint8Array([1, 2, 3]));
    expect(
      vi
        .mocked(processClientEvent)
        .mock.calls.filter(
          ([event]) => event["type"] === "crypt" && event["encrypt"] === true,
        ),
    ).toHaveLength(encrypted ? 1 : 0);
    expect(new DataView(sockets.sent[0]!.buffer).getUint16(8, false)).toBe(
      encrypted ? PacketType.ENCRYPTED : PacketType.IMAGE_FRAME,
    );
    expect(states).toContain(ConnectionState.CONNECTING);
    connection.disconnect();
  });
  it("permits explicit application encryption on WSS", async () => {
    const connection = new ClientConnection({
      serverUrl: "wss://example.com",
      applicationEncryption: true,
    });
    await connection.connect();
    Object.assign(connection, { transportState: ConnectionState.CONNECTED });
    connection.sendPacket(PacketType.IMAGE_FRAME, new Uint8Array([1]));
    expect(
      vi
        .mocked(processClientEvent)
        .mock.calls.filter(
          ([event]) => event["type"] === "crypt" && event["encrypt"] === true,
        ),
    ).toHaveLength(1);
    connection.disconnect();
  });

  it.each([
    ["ws://localhost:27226", true, ConnectionState.CONNECTING],
    ["wss://example.com", false, ConnectionState.CONNECTED],
  ] as const)(
    "%s starts the C discovery handshake with the encryption policy",
    async (serverUrl, supportsEncryption, expectedState) => {
      const connection = new ClientConnection({
        serverUrl,
        discoveryHandshake: true,
      });
      const states: ConnectionState[] = [];
      connection.onStateChange((state) => states.push(state));
      await connection.connect();

      expect(beginHandshake).toHaveBeenCalledExactlyOnceWith(
        supportsEncryption ? 1 : 0,
      );
      expect(states).toContain(expectedState);
      connection.disconnect();
    },
  );

  it("waits for WASM crypto reinitialization before starting the handshake on retry", async () => {
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
    expect(beginHandshake).toHaveBeenCalledTimes(1);
    Object.assign(connection, { wasEverConnected: true });

    const socket = sockets.instances[0]!;
    socket.emit("closed");
    socket.emit("connecting");
    socket.emit("open");
    await Promise.resolve();

    expect(beginHandshake).toHaveBeenCalledTimes(1);
    expect(configureClientCrypto).toHaveBeenCalledTimes(1);
    finishReinit();
    await vi.waitFor(() => expect(beginHandshake).toHaveBeenCalledTimes(2));
    expect(configureClientCrypto).toHaveBeenCalledTimes(2);
    expect(beginHandshake).toHaveBeenLastCalledWith(1);
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
    expect(beginHandshake).toHaveBeenCalledTimes(1);
    connection.disconnect();
  });

  it.each([false, true])(
    "reports a C handshake failure (throws: %s)",
    async (throws) => {
      if (throws) {
        beginHandshake.mockImplementationOnce(() => {
          throw new Error("WASM failure");
        });
      } else {
        beginHandshake.mockReturnValueOnce(1);
      }
      const connection = new ClientConnection({
        serverUrl: "ws://localhost:27226",
        discoveryHandshake: true,
      });
      const states: ConnectionState[] = [];
      connection.onStateChange((state) => states.push(state));
      await connection.connect();
      expect(states.at(-1)).toBe(ConnectionState.ERROR);
      expect(connection.getState()).toBe(ConnectionState.ERROR);
      expect(sockets.instances[0]!.isConnected()).toBe(false);
      // A reconnect failure can receive its close notification after ERROR.
      Object.assign(connection, { wasEverConnected: true });
      await Promise.resolve();
      sockets.instances[0]!.emit("closed");
      expect(connection.getState()).toBe(ConnectionState.ERROR);
      expect(states.at(-1)).toBe(ConnectionState.ERROR);
      connection.disconnect();
    },
  );

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

describe("authentication without payload encryption", () => {
  it("uses the C AUTH mode, blocks early media, then sends plaintext", async () => {
    beginHandshake.mockReturnValue(0);
    vi.mocked(processClientEvent).mockClear();
    const connection = new ClientConnection({
      serverUrl: "wss://example.com",
      applicationEncryption: false,
      cryptoOptions: { identityPrivateKeyText: "identity" },
    });
    await connection.connect();
    expect(processClientEvent).toHaveBeenCalledWith(
      expect.objectContaining({
        type: "resolve-crypto-policy",
        wss: true,
        encryption: 0,
      }),
    );
    expect(() =>
      connection.sendPacket(PacketType.IMAGE_FRAME, new Uint8Array([1])),
    ).toThrow("not complete");
    Object.assign(connection, { transportState: ConnectionState.CONNECTED });
    connection.sendPacket(PacketType.IMAGE_FRAME, new Uint8Array([1]));
    expect(
      vi
        .mocked(processClientEvent)
        .mock.calls.filter(
          ([event]) => event["type"] === "crypt" && event["encrypt"] === true,
        ),
    ).toHaveLength(0);
    connection.disconnect();
  });
});
