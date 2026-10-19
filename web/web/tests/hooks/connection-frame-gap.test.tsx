import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vite-plus/test";
import { useClientConnection } from "../../src/hooks/useClientConnection";
import { ConnectionState, PacketType } from "../../src/wasm/client";

const transport = vi.hoisted(() => ({
  state: (_state: number) => {},
  packet: (_packet: { type: number }, _payload: Uint8Array) => {},
  disconnect: vi.fn(),
}));
vi.mock("../../src/hooks/useVideoEncodings", () => ({
  useVideoEncodings: () => ({ validate: () => {} }),
}));
vi.mock("../../src/network", () => ({
  buildCapabilitiesPacket: () => new Uint8Array(),
  buildStreamStartPacket: () => new Uint8Array(),
  parseAsciiFrame: () => ({
    header: { width: 80, height: 40 },
    ansiString: "frame",
  }),
}));
vi.mock("../../src/network/BrowserHostSession", () => ({
  BrowserHostSession: class {
    onStateChange(callback: typeof transport.state) {
      transport.state = callback;
    }
    onPacketReceived(callback: typeof transport.packet) {
      transport.packet = callback;
    }
    async connect() {
      transport.state(3);
    }
    disconnect = transport.disconnect;
    sendUnencryptedAcipPacket() {}
    getPublicKey() {
      return null;
    }
  },
}));
afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

it("keeps a live host connected through a frame gap, but reports transport failure", async () => {
  vi.useFakeTimers();
  const options = {
    discovery: {
      createSession: true,
      sessionName: "",
      password: "",
      signalingUrl: "ws://localhost:29327",
      stunServers: [],
      turnServers: [],
    },
    serverUrl: "",
    terminalDimensions: { cols: 80, rows: 40 },
    settings: {
      targetFps: 60,
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
  act(() => {
    vi.advanceTimersByTime(100);
    transport.packet({ type: PacketType.ASCII_FRAME }, new Uint8Array());
    vi.advanceTimersByTime(60_000);
  });
  expect(result.current.connectionState).toBe(ConnectionState.CONNECTED);
  act(() =>
    transport.packet({ type: PacketType.ASCII_FRAME }, new Uint8Array()),
  );
  expect(options.receivedFrameCountRef.current).toBe(2);
  act(() => transport.state(ConnectionState.ERROR));
  expect(result.current.connectionState).toBe(ConnectionState.ERROR);
});
