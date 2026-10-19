import { describe, it, expect, vi } from "vite-plus/test";
import { BrowserHostSession } from "../../src/network/BrowserHostSession";
import { ConnectionState, PacketType } from "../../src/wasm/client";

const harness = vi.hoisted(() => ({
  state: (_state: number) => {},
  packet: (_packet: { type: number }, _payload: Uint8Array) => {},
  disconnected: vi.fn(),
  module: {
    _browser_host_create: () => 0,
    _browser_host_add_client: () => 1,
    _browser_host_remove_client: vi.fn(),
    _browser_host_destroy: vi.fn(),
    _browser_host_render: () => 0,
    hostPacketCallback: undefined as
      | undefined
      | ((id: number, bytes: Uint8Array) => void),
  },
}));
vi.mock("../../src/network/ClientConnection", () => ({
  ClientConnection: class {
    onStateChange(callback: typeof harness.state) {
      harness.state = callback;
    }
    onPacketReceived(callback: typeof harness.packet) {
      harness.packet = callback;
    }
    connect() {
      harness.state(3);
      return Promise.resolve();
    }
    sendPacket() {}
    disconnect() {
      harness.disconnected();
    }
  },
}));
vi.mock("../../src/network/DiscoveryProtocol", () => ({
  DiscoveryProtocol: class {
    enter() {}
    close() {}
    receive() {
      return {
        state: 6,
        name: "test-session",
        joined: {
          participantId: new Uint8Array(16),
          hostId: new Uint8Array(16),
        },
      };
    }
  },
}));
vi.mock("../../src/wasm/client", async (original) => ({
  ...(await original<typeof import("../../src/wasm/client")>()),
  getClientModule: () => harness.module,
}));

describe("browser host peer isolation", () => {
  it.each(["WebRTC channel is not open", "WebRTC send buffer is full"])(
    "keeps other peers and signaling alive: %s",
    async (message) => {
      harness.disconnected.mockClear();
      const host = new BrowserHostSession({
        signalingUrl: "ws://localhost",
        sessionName: "",
        password: "",
        stunServers: [],
        turnServers: [],
        createSession: true,
      });
      const ready = host.connect();
      harness.packet({ type: 1 }, new Uint8Array());
      await ready;
      const makePeer = (id: number) => ({
        id,
        pc: { close: vi.fn() },
        bridge: { close: vi.fn(), getBufferedAmount: () => 0 },
        crypto: { ready: true, close: vi.fn(), sendPacket: vi.fn() },
      });
      const bad = makePeer(2),
        good = makePeer(3);
      bad.crypto.sendPacket.mockImplementation(() => {
        throw new Error(message);
      });
      const peers = Reflect.get(host, "peers") as Map<string, typeof bad>;
      peers.set("bad", bad);
      peers.set("good", good);
      const bytes = new Uint8Array(22);
      new DataView(bytes.buffer).setUint16(8, PacketType.PING, false);
      try {
        harness.module.hostPacketCallback!(2, bytes);
        await Promise.resolve();
        harness.module.hostPacketCallback!(3, bytes);
        await Promise.resolve();
        expect(peers.has("bad")).toBe(false);
        expect(bad.crypto.close).toHaveBeenCalledOnce();
        expect(good.crypto.sendPacket).toHaveBeenCalledWith(bytes);
        expect(host.getState()).toBe(ConnectionState.CONNECTED);
        expect(harness.disconnected).not.toHaveBeenCalled();
      } finally {
        host.disconnect();
      }
    },
  );
});
