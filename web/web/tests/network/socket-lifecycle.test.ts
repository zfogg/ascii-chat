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
});
