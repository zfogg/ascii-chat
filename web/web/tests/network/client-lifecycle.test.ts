import { beforeEach, describe, expect, it, vi } from "vite-plus/test";
import { ClientConnection } from "../../src/network/ClientConnection";
import { ConnectionState } from "../../src/wasm/client";

const harness = vi.hoisted(() => ({
  init: vi.fn(async () => {}),
  crypto: vi.fn(async () => {}),
  key: vi.fn(async () => "public-key"),
  cleanup: vi.fn(),
  address: vi.fn(),
  callback: vi.fn(),
  begin: vi.fn(() => 0),
  sockets: [] as {
    close: ReturnType<typeof vi.fn>;
    state: (state: string) => void;
  }[],
}));
vi.mock("../../src/wasm/client", async (original) => ({
  ...(await original<typeof import("../../src/wasm/client")>()),
  initClientWasm: harness.init,
  configureClientCrypto: harness.crypto,
  generateKeypair: harness.key,
  cleanupClientWasm: harness.cleanup,
  processClientEvent: vi.fn((event: Record<string, unknown>) =>
    event["type"] === "begin-handshake"
      ? { code: harness.begin(), state: ConnectionState.HANDSHAKE }
      : event["type"] === "resolve-crypto-policy"
        ? { code: 0, mode: 1, state: ConnectionState.CONNECTED }
        : event["type"] === "set-server"
          ? (harness.address(event["host"], event["port"]),
            { code: 0, state: ConnectionState.CONNECTED })
          : { code: 0, state: ConnectionState.CONNECTED },
  ),
}));
vi.mock("../../src/network/SocketBridge", () => ({
  SocketBridge: class {
    close = vi.fn();
    constructor(options: { onStateChange: (state: string) => void }) {
      harness.sockets.push({ close: this.close, state: options.onStateChange });
    }
    async connect() {
      harness.sockets.at(-1)!.state("open");
    }
    isConnected() {
      return true;
    }
    send() {}
  },
}));

beforeEach(() => {
  vi.clearAllMocks();
  harness.sockets = [];
  harness.init.mockImplementation(async () => {});
  harness.crypto.mockImplementation(async () => {});
  harness.key.mockImplementation(async () => "public-key");
});
const create = () =>
  new ClientConnection({
    serverUrl: "wss://localhost:8443",
    width: 80,
    height: 24,
  });

describe("client startup ownership", () => {
  it("drains cancelled initialization before a replacement session starts", async () => {
    let resume!: () => void;
    harness.init.mockImplementationOnce(
      () =>
        new Promise<void>((resolve) => {
          resume = resolve;
        }),
    );
    const old = create();
    const pending = old.connect();
    await vi.waitFor(() => expect(harness.init).toHaveBeenCalledTimes(1));
    old.disconnect();
    const replacement = create();
    const next = replacement.connect();
    await Promise.resolve();
    expect(harness.init).toHaveBeenCalledTimes(1);
    resume();
    await Promise.all([pending, next]);
    expect(harness.sockets).toHaveLength(1);
    const cleaned = harness.cleanup.mock.calls.length;
    old.disconnect();
    expect(harness.cleanup).toHaveBeenCalledTimes(cleaned);
    expect(replacement.getPublicKey()).toBe("public-key");
    replacement.disconnect();
  });
  it("shares one startup between concurrent connect callers", async () => {
    const client = create();
    const first = client.connect();
    expect(client.connect()).toBe(first);
    await first;
    expect(harness.init).toHaveBeenCalledTimes(1);
    expect(harness.sockets).toHaveLength(1);
    client.disconnect();
  });
  it("uses identical initialization on startup and transport reconnect", async () => {
    const client = create();
    await client.connect();
    harness.sockets[0]!.state("closed");
    harness.sockets[0]!.state("open");
    await vi.waitFor(() => expect(harness.begin).toHaveBeenCalledTimes(2));
    expect(harness.init.mock.calls).toEqual([
      [{ width: 80, height: 24 }],
      [{ width: 80, height: 24 }],
    ]);
    expect(harness.address.mock.calls).toEqual([
      ["localhost", 8443],
      ["localhost", 8443],
    ]);
    client.disconnect();
    expect(harness.sockets[0]!.close).toHaveBeenCalledTimes(1);
  });

  it.each(["init", "crypto", "key"] as const)(
    "does not publish after disconnect during %s",
    async (stage) => {
      let resume!: () => void;
      const pending = new Promise<void>((resolve) => {
        resume = resolve;
      });
      if (stage === "key")
        harness.key.mockImplementation(async () => {
          await pending;
          return "late-key";
        });
      else harness[stage].mockImplementation(() => pending);
      const client = create();
      const connecting = client.connect();
      await vi.waitFor(() => expect(harness[stage]).toHaveBeenCalled());
      client.disconnect();
      resume();
      await connecting;
      expect(harness.sockets).toHaveLength(0);
      expect(harness.callback).not.toHaveBeenCalled();
      expect(client.getPublicKey()).toBeNull();
    },
  );

  it("releases partial initialization and preserves the failure cause", async () => {
    const cause = new Error("identity rejected");
    harness.crypto.mockRejectedValueOnce(cause);
    const client = create();
    const states: ConnectionState[] = [];
    client.onStateChange((state) => states.push(state));
    await expect(client.connect()).rejects.toBe(cause);
    expect(harness.cleanup).toHaveBeenCalled();
    expect(harness.sockets).toHaveLength(0);
    expect(states.at(-1)).toBe(ConnectionState.ERROR);
  });

  it("closes the transport when reconnect initialization fails", async () => {
    const client = create();
    const states: ConnectionState[] = [];
    client.onStateChange((state) => states.push(state));
    await client.connect();
    harness.crypto.mockRejectedValueOnce(new Error("reinit failed"));
    harness.sockets[0]!.state("open");
    await vi.waitFor(() => expect(states.at(-1)).toBe(ConnectionState.ERROR));
    expect(harness.sockets[0]!.close).toHaveBeenCalledTimes(1);
    expect(harness.begin).toHaveBeenCalledTimes(1);
  });
});
