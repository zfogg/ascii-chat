import {
  afterEach,
  beforeEach,
  describe,
  expect,
  it,
  vi,
} from "vite-plus/test";

const mock = vi.hoisted(() => ({
  primaryOptions: null as any,
  primaryConnect: vi.fn(),
  primaryDisconnect: vi.fn(),
  fallbackOptions: null as any,
  fallbackConnect: vi.fn(),
  fallbackDisconnect: vi.fn(),
}));
vi.mock("../../src/network/WebTransportClientSession", () => ({
  WebTransportClientSession: class {
    transportType = "webtransport";
    constructor(options: unknown) {
      mock.primaryOptions = options;
    }
    onStateChange() {}
    onPacketReceived() {}
    getState() {
      return 1;
    }
    connect() {
      return mock.primaryConnect();
    }
    disconnect() {
      mock.primaryDisconnect();
    }
  },
}));
vi.mock("../../src/network/ClientConnection", () => ({
  ClientConnection: class {
    transportType = "websocket";
    constructor(options: unknown) {
      mock.fallbackOptions = options;
    }
    onStateChange() {}
    onPacketReceived() {}
    getState() {
      return 1;
    }
    connect() {
      return mock.fallbackConnect();
    }
    disconnect() {
      mock.fallbackDisconnect();
    }
  },
}));
import { WebTransportFallbackSession } from "../../src/network/WebTransportFallbackSession";

const options = {
  serverUrl: "https://server.example/acip",
  cryptoOptions: { expectedServerPublicKeyText: "ssh-ed25519 pinned" },
};
const pending = () => new Promise<void>(() => {});
beforeEach(() => {
  vi.useFakeTimers();
  vi.stubGlobal("WebTransport", class {});
  mock.primaryOptions = null;
  mock.fallbackOptions = null;
  mock.primaryConnect.mockReset().mockImplementation(pending);
  mock.primaryDisconnect.mockReset();
  mock.fallbackDisconnect.mockReset();
  mock.fallbackConnect.mockReset().mockResolvedValue(undefined);
});
afterEach(() => {
  vi.unstubAllGlobals();
  vi.useRealTimers();
});

describe("WebTransport fallback", () => {
  it("requires secure endpoints, an identity pin and application encryption", () => {
    expect(
      () => new WebTransportFallbackSession(options, "ws://server.example"),
    ).toThrow();
    expect(
      () =>
        new WebTransportFallbackSession(
          { serverUrl: options.serverUrl },
          "wss://server.example",
        ),
    ).toThrow();
    expect(
      () =>
        new WebTransportFallbackSession(
          { ...options, applicationEncryption: false },
          "wss://server.example",
        ),
    ).toThrow();
  });
  it("uses pinned encrypted WSS when WebTransport is unavailable", async () => {
    vi.stubGlobal("WebTransport", undefined);
    const session = new WebTransportFallbackSession(
      options,
      "wss://server.example/ws",
    );
    await session.connect();
    expect(mock.fallbackOptions).toMatchObject({
      serverUrl: "wss://server.example/ws",
      applicationEncryption: true,
      cryptoOptions: options.cryptoOptions,
    });
    expect(session.transportType).toBe("websocket");
  });
  it("falls back after a pending pre-HTTP/3 timeout", async () => {
    const session = new WebTransportFallbackSession(
      options,
      "wss://server.example/ws",
      100,
    );
    const connecting = session.connect();
    await vi.advanceTimersByTimeAsync(100);
    await connecting;
    expect(mock.primaryDisconnect).toHaveBeenCalledOnce();
    expect(mock.fallbackConnect).toHaveBeenCalledOnce();
  });
  it("does not fall back after HTTP/3 is ready or on a transport failure", async () => {
    const session = new WebTransportFallbackSession(
      options,
      "wss://server.example/ws",
      100,
    );
    const connecting = session.connect();
    mock.primaryOptions.onTransportReady();
    await vi.advanceTimersByTimeAsync(200);
    expect(mock.fallbackConnect).not.toHaveBeenCalled();
    session.disconnect();
    await connecting;
    mock.primaryConnect.mockRejectedValueOnce(new Error("certificate failure"));
    const failed = new WebTransportFallbackSession(
      options,
      "wss://server.example/ws",
      100,
    );
    await expect(failed.connect()).rejects.toThrow("certificate failure");
    expect(mock.fallbackConnect).not.toHaveBeenCalled();
  });
  it("does not fall back when cancelled", async () => {
    const session = new WebTransportFallbackSession(
      options,
      "wss://server.example/ws",
      100,
    );
    const connecting = session.connect();
    session.disconnect();
    await connecting;
    await vi.advanceTimersByTimeAsync(200);
    expect(mock.fallbackConnect).not.toHaveBeenCalled();
  });
});
