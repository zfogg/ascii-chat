import { afterEach, describe, expect, it } from "vite-plus/test";
import { WebSocket, WebSocketServer } from "ws";
import { SocketBridge } from "../../src/network/SocketBridge";

// WASM runs in isolated browsers; reconnection belongs to the socket adapter.
Object.assign(globalThis, { WebSocket });
let server: WebSocketServer | undefined;
let bridge: SocketBridge | undefined;
afterEach(async () => {
  bridge?.close();
  server?.clients.forEach((client) => client.terminate());
  if (server)
    await new Promise<void>((resolve) => server!.close(() => resolve()));
  server = undefined;
  bridge = undefined;
});
async function start(port: number) {
  server = new WebSocketServer({ port, host: "127.0.0.1" });
  await new Promise<void>((resolve) => server!.once("listening", resolve));
  return (server as unknown as { address(): { port: number } }).address().port;
}
describe("Reconnection with server restart", () => {
  it("reopens after a server restart and stops retrying on disconnect", async () => {
    const port = await start(0);
    const states: string[] = [];
    bridge = new SocketBridge({
      url: `ws://127.0.0.1:${port}`,
      onStateChange: (state) => states.push(state),
    });
    await bridge.connect();
    expect(bridge.isConnected()).toBe(true);
    server!.clients.forEach((client) => client.terminate());
    await new Promise<void>((resolve) => server!.close(() => resolve()));
    server = undefined;
    await expect.poll(() => states.includes("closed")).toBe(true);
    await start(port);
    await expect
      .poll(() => states.filter((state) => state === "open").length, {
        timeout: 6000,
      })
      .toBe(2);
    expect(bridge.isConnected()).toBe(true);
    bridge.close();
    await expect.poll(() => bridge!.isConnected()).toBe(false);
    const opens = states.filter((state) => state === "open").length;
    await new Promise((resolve) => setTimeout(resolve, 1200));
    expect(states.filter((state) => state === "open")).toHaveLength(opens);
  }, 10000);
});
