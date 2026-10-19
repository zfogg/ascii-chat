import { expect, test } from "./browser-fixture";
import { WebSocketServer } from "ws";
import type { SocketBridge } from "../../src/network/SocketBridge";

declare global {
  interface Window {
    __connectionCi: {
      bridge: SocketBridge;
      states: string[];
      packets: number[][];
    };
  }
}

test("browser transport exchanges packets, reconnects after restart, and honors disconnect", async ({
  page,
}) => {
  let server: WebSocketServer | undefined;
  let connections = 0;
  const start = async (port = 0) => {
    server = new WebSocketServer({ host: "127.0.0.1", port });
    server.on("connection", (socket) => {
      connections++;
      socket.on("message", (data) =>
        socket.send(Array.isArray(data) ? Buffer.concat(data) : data),
      );
    });
    await new Promise<void>((resolve, reject) => {
      server!.once("listening", resolve);
      server!.once("error", reject);
    });
    return (server.address() as { port: number }).port;
  };
  const stop = async () => {
    if (!server) return;
    server.clients.forEach((socket) => socket.terminate());
    await new Promise<void>((resolve) => server!.close(() => resolve()));
    server = undefined;
  };
  try {
    const port = await start();
    await page.goto("/client");
    await page.evaluate(async (port) => {
      const moduleUrl = "/src/network/SocketBridge.ts";
      const { SocketBridge } = await import(/* @vite-ignore */ moduleUrl);
      const states: string[] = [];
      const packets: number[][] = [];
      const bridge = new SocketBridge({
        url: `ws://127.0.0.1:${port}`,
        onStateChange: (state: string) => states.push(state),
        onPacket: (packet: Uint8Array) => packets.push(Array.from(packet)),
      });
      window.__connectionCi = { bridge, states, packets };
      await bridge.connect();
    }, port);
    const sendPacket = (value: number) =>
      page.evaluate((value) => {
        // A framed payload for the adapter; this does not simulate a handshake.
        const packet = new Uint8Array(23);
        new DataView(packet.buffer).setUint32(10, 1);
        packet[22] = value;
        window.__connectionCi.bridge.send(packet);
      }, value);
    await sendPacket(17);
    await expect
      .poll(() =>
        page.evaluate(() => window.__connectionCi.packets.at(-1)?.[22]),
      )
      .toBe(17);
    await stop();
    await expect
      .poll(() => page.evaluate(() => window.__connectionCi.states))
      .toContain("closed");
    // Observe a retry while the server is down before bringing it back.
    await expect
      .poll(() =>
        page.evaluate(
          () =>
            window.__connectionCi.states.filter((s) => s === "connecting")
              .length,
        ),
      )
      .toBeGreaterThan(1);
    await start(port);
    await expect
      .poll(
        () => page.evaluate(() => window.__connectionCi.bridge.isConnected()),
        {
          timeout: 10_000,
        },
      )
      .toBe(true);
    await sendPacket(29);
    await expect
      .poll(() =>
        page.evaluate(() => window.__connectionCi.packets.at(-1)?.[22]),
      )
      .toBe(29);
    expect(connections).toBe(2);
    await page.evaluate(() => window.__connectionCi.bridge.close());
    await expect.poll(() => server!.clients.size).toBe(0);
    // Stay beyond two retry intervals to detect an unwanted reconnect.
    await page.waitForTimeout(2_200);
    expect(connections).toBe(2);
    expect(
      await page.evaluate(() => window.__connectionCi.bridge.isConnected()),
    ).toBe(false);
  } finally {
    if (!page.isClosed())
      await page.evaluate(() => window.__connectionCi?.bridge.close());
    await stop();
  }
});
