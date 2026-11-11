import {
  afterEach,
  beforeEach,
  describe,
  expect,
  it,
  vi,
} from "vite-plus/test";
import { SocketBridge } from "../../src/network/SocketBridge";
class Socket extends EventTarget {
  static OPEN = 1;
  static instances: Socket[] = [];
  readyState = 0;
  binaryType = "";
  close = vi.fn(() => {
    this.readyState = 3;
  });
  constructor() {
    super();
    Socket.instances.push(this);
  }
  open() {
    this.readyState = 1;
    this.dispatchEvent(new Event("open"));
  }
  closed() {
    this.readyState = 3;
    this.dispatchEvent(new Event("close"));
  }
}
let bridge: SocketBridge;
beforeEach(() => {
  vi.useFakeTimers();
  vi.stubGlobal("WebSocket", Socket);
  Socket.instances = [];
  bridge = new SocketBridge({ url: "ws://localhost" });
});
afterEach(() => {
  bridge.close();
  vi.useRealTimers();
  vi.unstubAllGlobals();
});
describe("socket establishment ownership", () => {
  it("settles cancellation and ignores queued open events", async () => {
    const states: string[] = [];
    bridge.onStateChange((state) => states.push(state));
    const connecting = bridge.connect();
    const rejected = expect(connecting).rejects.toThrow("cancelled");
    const socket = Socket.instances[0]!;
    bridge.close();
    socket.open();
    socket.closed();
    await rejected;
    await vi.runAllTimersAsync();
    expect(states).toEqual(["connecting"]);
    expect(Socket.instances).toHaveLength(1);
    expect(bridge.isConnected()).toBe(false);
  });
  it("rejects close before open without waiting for an error event", async () => {
    const connecting = bridge.connect();
    const rejected = expect(connecting).rejects.toThrow(
      "closed before opening",
    );
    Socket.instances[0]!.closed();
    await rejected;
    await vi.runAllTimersAsync();
    expect(Socket.instances).toHaveLength(1);
  });
  it("ignores events from the old socket after reconnect", async () => {
    const states: string[] = [];
    bridge.onStateChange((state) => states.push(state));
    const connecting = bridge.connect();
    const old = Socket.instances[0]!;
    old.open();
    await connecting;
    old.closed();
    await vi.advanceTimersByTimeAsync(1000);
    Socket.instances[1]!.open();
    const count = states.length;
    old.closed();
    old.open();
    await vi.advanceTimersByTimeAsync(2000);
    expect(states).toHaveLength(count);
    expect(Socket.instances).toHaveLength(2);
    expect(bridge.isConnected()).toBe(true);
  });
  it("lets a close-state callback cancel automatic reconnect", async () => {
    bridge.onStateChange((state) => {
      if (state === "closed") bridge.close();
    });
    const connecting = bridge.connect();
    Socket.instances[0]!.open();
    await connecting;
    Socket.instances[0]!.closed();
    await vi.runAllTimersAsync();
    expect(Socket.instances).toHaveLength(1);
    expect(bridge.isConnected()).toBe(false);
  });
  it("clears partial packet state on reconnect and closes on invalid framing", async () => {
    const packets: Uint8Array[] = [];
    const errors: Error[] = [];
    bridge.onPacket((packet) => packets.push(packet));
    bridge.onError((error) => errors.push(error));
    const connecting = bridge.connect();
    const first = Socket.instances[0]!;
    first.open();
    await connecting;
    const packet = new Uint8Array(23);
    const header = new DataView(packet.buffer);
    header.setBigUint64(0, 0xa5c11c4a1n);
    header.setUint32(10, 1);
    header.setUint32(14, 0xb048173d);
    packet[22] = 17;
    first.dispatchEvent(
      new MessageEvent("message", { data: packet.slice(0, 10).buffer }),
    );
    first.closed();
    await vi.advanceTimersByTimeAsync(1000);
    const second = Socket.instances[1]!;
    second.open();
    second.dispatchEvent(
      new MessageEvent("message", { data: packet.slice(0, 12).buffer }),
    );
    second.dispatchEvent(
      new MessageEvent("message", { data: packet.slice(12).buffer }),
    );
    expect(packets).toEqual([packet]);
    const invalid = packet.slice();
    new DataView(invalid.buffer).setUint32(10, 0xffffffff);
    second.dispatchEvent(new MessageEvent("message", { data: invalid.buffer }));
    expect(errors.at(-1)?.message).toContain("header");
    expect(second.close).toHaveBeenCalledWith(1002, "Invalid ACIP packet");
  });
});
