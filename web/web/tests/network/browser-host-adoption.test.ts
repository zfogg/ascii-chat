import { afterEach, expect, it, vi } from "vite-plus/test";
import { BrowserHostSession } from "../../src/network/BrowserHostSession";
import { ConnectionState } from "../../src/wasm/client";
const runtime = vi.hoisted(() => ({
  ready: undefined as undefined | (() => void),
  close: vi.fn(),
}));
vi.mock("../../src/network/BrowserHostRuntime", () => ({
  BrowserHostRuntime: class {
    start() {
      return new Promise<void>((resolve) => {
        runtime.ready = resolve;
      });
    }
    addClient() {
      return 1;
    }
    close() {
      runtime.close();
    }
  },
}));
afterEach(() => {
  vi.useRealTimers();
  vi.clearAllMocks();
  runtime.ready = undefined;
});
function adopt() {
  const signaling = {
    onStateChange: vi.fn(),
    onPacketReceived: vi.fn(),
    sendPacket: vi.fn(),
    disconnect: vi.fn(),
  };
  const protocol = { adopt: vi.fn(), close: vi.fn() };
  const joined = {
    participantId: new Uint8Array(16),
    hostId: new Uint8Array(16),
    sessionId: new Uint8Array(16),
    turnUsername: "",
    turnPassword: "",
  };
  const host = BrowserHostSession.adopt(
    {
      sessionName: "blue-mountain-tiger",
      password: "",
      signalingUrl: "ws://localhost",
      stunServers: [],
      turnServers: [],
    },
    signaling as any,
    joined as any,
    protocol as any,
  );
  return { host, signaling, protocol };
}
it("keeps the transferred protocol during slow startup and cancels the deadline on readiness", async () => {
  vi.useFakeTimers();
  const { host, signaling, protocol } = adopt();
  try {
    await vi.advanceTimersByTimeAsync(12000);
    expect(protocol.adopt).toHaveBeenCalledOnce();
    expect(protocol.close).not.toHaveBeenCalled();
    expect(signaling.disconnect).not.toHaveBeenCalled();
    runtime.ready!();
    for (let i = 0; i < 8; i++) await Promise.resolve();
    expect(host.getState()).toBe(ConnectionState.CONNECTED);
    await vi.advanceTimersByTimeAsync(30000);
    expect(signaling.disconnect).not.toHaveBeenCalled();
  } finally {
    host.disconnect();
  }
});
it("stops probing and releases the signaling connection if host startup never finishes", async () => {
  vi.useFakeTimers();
  const { host, signaling, protocol } = adopt();
  try {
    await vi.advanceTimersByTimeAsync(30000);
    expect(host.getState()).toBe(ConnectionState.ERROR);
    expect(protocol.close).toHaveBeenCalledOnce();
    expect(runtime.close).toHaveBeenCalledOnce();
    expect(signaling.disconnect).toHaveBeenCalledOnce();
  } finally {
    host.disconnect();
  }
});
