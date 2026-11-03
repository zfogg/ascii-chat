import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vite-plus/test";

const chosen = vi.hoisted(() => ({ transports: [] as string[] }));
vi.mock("../../src/hooks/useVideoEncodings", () => ({
  useVideoEncodings: () => ({ validate: () => {} }),
}));
vi.mock("../../src/network", () => ({
  ClientConnection: class {
    constructor() {
      chosen.transports.push("websocket");
    }
    onStateChange() {}
    onPacketReceived() {}
    async connect() {}
    disconnect() {}
    getPublicKey() {
      return null;
    }
  },
  buildCapabilitiesPacket: () => new Uint8Array(),
  buildStreamStartPacket: () => new Uint8Array(),
  parseAsciiFrame: () => ({}),
}));
vi.mock("../../src/network/WebTransportClientSession", () => ({
  WebTransportClientSession: class {
    constructor() {
      chosen.transports.push("webtransport");
    }
    onStateChange() {}
    onPacketReceived() {}
    async connect() {}
    disconnect() {}
    getPublicKey() {
      return null;
    }
  },
}));
vi.mock("../../src/network/WebTransportFallbackSession", () => ({
  WebTransportFallbackSession: class {
    constructor() {
      chosen.transports.push("webtransport-fallback");
    }
    onStateChange() {}
    onPacketReceived() {}
    async connect() {}
    disconnect() {}
    getPublicKey() {
      return null;
    }
  },
}));
import { useClientConnection } from "../../src/hooks/useClientConnection";

afterEach(() => {
  cleanup();
  chosen.transports = [];
});
describe("direct browser transport selection", () => {
  it.each([
    ["https://example.test/chat", "webtransport", ""],
    [
      "https://example.test/chat",
      "webtransport-fallback",
      "wss://example.test/ws",
    ],
    ["wss://example.test/", "websocket", ""],
    ["ws://localhost:27226/", "websocket", ""],
  ])("uses %s for %s", async (serverUrl, expected, fallbackWebSocketUrl) => {
    const options = {
      serverUrl,
      fallbackWebSocketUrl,
      terminalDimensions: { cols: 80, rows: 40 },
      settings: {
        targetFps: 30,
        width: 80,
        height: 40,
        colorMode: "auto",
        colorFilter: "none",
        palette: "standard",
      },
      rendererRef: { current: null },
      frameQueueRef: { current: [] },
      uniqueReceivedFramesRef: { current: {} },
      uniqueReceivedFrameCountRef: { current: 0 },
      changedReceivedFrameCountRef: { current: 0 },
      uniqueReceivedFrameOrderRef: { current: [] },
      frameCountRef: { current: 0 },
      receivedFrameCountRef: { current: 0 },
      frameReceiptTimesRef: { current: [] },
      onWasmInitialized: () => {},
    } as Parameters<typeof useClientConnection>[0];
    const { result } = renderHook(() => useClientConnection(options));
    await act(async () => {
      await result.current.connectToServer();
    });
    expect(chosen.transports).toEqual([expected]);
  });
});
