import { WebSocketServer } from "ws";
import { expect, test } from "./browser-fixture";

const frames = [
  [
    "lib/network/connection.c",
    "C:/build/ascii-chat/lib/network/connection.c",
    376,
    "connection_receive",
    "client connection failed",
  ],
  [
    "lib/network/negotiation.c",
    "C:/build/ascii-chat/lib/network/negotiation.c",
    228,
    "media_receive",
    "session negotiation failed",
  ],
  [
    "lib/network/acip/handlers.c",
    "C:/build/ascii-chat/lib/network/acip/handlers.c",
    702,
    "acip_handle_packet",
    "ACIP message rejected",
  ],
  [
    "lib/network/packet/packet.c",
    "C:/build/ascii-chat/lib/network/packet/packet.c",
    188,
    "packet_validate",
    "packet validation failed",
  ],
  [
    "lib/network/tcp/transport.c",
    "C:/build/ascii-chat/lib/network/tcp/transport.c",
    521,
    "tcp_recv",
    "socket read failed",
  ],
] as const;

function crc32c(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++)
      crc = (crc >>> 1) ^ (crc & 1 ? 0x82f63b78 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function errorPacket(code = 62, text?: string): Buffer {
  const message = Buffer.from(
    text ??
      frames
        .map(
          ([file, fullFile, line, func, text]) =>
            `${text} (${file}:${line} in ${func}())\tfull-path=${fullFile}`,
        )
        .join("\n"),
  );
  const payload = Buffer.alloc(8 + message.length);
  payload.writeUInt32BE(code, 0);
  payload.writeUInt32BE(message.length, 4);
  message.copy(payload, 8);

  const packet = Buffer.alloc(22 + payload.length);
  packet.writeBigUInt64BE(0xa5c11c4a1n, 0);
  packet.writeUInt16BE(2003, 8);
  packet.writeUInt32BE(payload.length, 10);
  packet.writeUInt32BE(crc32c(payload), 14);
  packet.writeUInt32BE(0, 18);
  payload.copy(packet, 22);
  return packet;
}

test("renders a five-frame errno stack as paired source and message rows", async ({
  page,
}) => {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, "clipboard", {
      configurable: true,
      value: {
        writeText: async (value: string) => {
          (window as Window & { __copiedPath?: string }).__copiedPath = value;
        },
      },
    });
  });
  const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  await new Promise<void>((resolve, reject) => {
    server.once("listening", resolve);
    server.once("error", reject);
  });
  const port = (server.address() as { port: number }).port;
  server.on("connection", (socket) => socket.send(errorPacket()));

  try {
    await page.goto(
      `/client?serverUrl=${encodeURIComponent(`ws://127.0.0.1:${port}`)}&settings=true&connect=true`,
    );

    const dialog = page.getByRole("alertdialog", { name: "ascii-chat error" });
    await expect(dialog).toBeVisible();
    await expect(dialog.getByRole("heading")).toHaveText(
      "Remote protocol error 62",
    );

    const list = dialog.locator("ol");
    await expect(dialog.getByText("Stack order: 0 is newest")).toBeVisible();
    await expect(list).toHaveClass(/space-y-4/);
    const rows = list.locator("li");
    await expect(rows).toHaveCount(5);

    for (let index = 0; index < frames.length; index++) {
      const [file, fullFile, line, func, message] = frames[index]!;
      const row = rows.nth(index);
      await expect(row).toHaveClass(/border-red-900/);
      await expect(
        row.locator(
          `[title="${index === 0 ? "Newest error" : `Stack frame ${index}`}"]`,
        ),
      ).toHaveText(String(index));
      const sourceLocation = row.locator(".source-location");
      await expect(sourceLocation).toHaveText(`${file}:${line}`);
      await expect(sourceLocation).toHaveClass(/underline/);
      await expect(sourceLocation.locator(".text-blue-400")).toHaveText(file);
      await expect(sourceLocation.locator(".text-terminal-fg")).toHaveText(":");
      await expect(sourceLocation.locator(".text-purple-400")).toHaveText(
        String(line),
      );
      await expect(sourceLocation).toHaveAttribute(
        "title",
        `Copy full path: ${fullFile}`,
      );
      await expect(row.getByText("in", { exact: true })).toHaveClass(
        /text-terminal-fg/,
      );
      await expect(row.locator(".text-orange-400")).toHaveText(`${func}()`);
      await expect(row.locator(".text-red-100")).toHaveText(message);
      await expect(row).not.toContainText("C:/Users/");
    }

    await rows.nth(0).locator(".source-location").click();
    await expect
      .poll(() =>
        page.evaluate(
          () => (window as Window & { __copiedPath?: string }).__copiedPath,
        ),
      )
      .toBe(frames[0][1]);

    await dialog.getByRole("button", { name: "Dismiss" }).click();
    await expect(dialog).toBeHidden();
  } finally {
    server.clients.forEach((socket) => socket.terminate());
    await new Promise<void>((resolve) => server.close(() => resolve()));
  }
});

// The server endpoint uses the same compiled security engine as browser hosts.
// Packets still cross a real WebSocket; no crypto or WASM functions are mocked.
for (const encryption of [false, true]) {
  test(`remote errors continue, retry and stop over ${encryption ? "encrypted" : "plain"} signaling WebSocket`, async ({
    page,
    context,
  }) => {
    const endpoint = await context.newPage();
    const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
    await new Promise<void>((resolve) => server.once("listening", resolve));
    const port = (server.address() as { port: number }).port;
    let socket: import("ws").WebSocket | undefined;
    let connections = 0;
    let incoming = Promise.resolve();
    const received: number[][] = [];
    const failures: string[] = [];
    await endpoint.exposeFunction("sendErrorTestPacket", (bytes: number[]) =>
      socket?.send(Buffer.from(bytes)),
    );
    await endpoint.goto("/client");
    await endpoint.evaluate(async () => {
      const url = "/src/wasm/client.ts";
      const engine = await import(/* @vite-ignore */ url);
      await engine.initClientWasm();
    });
    server.on("connection", (next) => {
      socket = next;
      connections++;
      if (connections === 1) {
        next.send(errorPacket(48, "Session full during negotiation"));
        return;
      }
      incoming = incoming.then(() =>
        endpoint.evaluate(async (encryption) => {
          const url = "/src/wasm/client.ts";
          const { BrowserSession } = await import(/* @vite-ignore */ url);
          window.__errorPeer?.close();
          window.__errorPeer = await BrowserSession.create(
            {},
            encryption,
            true,
            (packet: Uint8Array) => {
              // ACDS starts crypto without the media peer's version ACK.
              if (
                new DataView(packet.buffer, packet.byteOffset).getUint16(8) ===
                1
              )
                return;
              void window.sendErrorTestPacket(Array.from(packet));
            },
          );
        }, encryption),
      );
      next.on("message", (data) => {
        const bytes = Array.from(Buffer.from(data as Buffer));
        incoming = incoming
          .then(async () => {
            const decoded = await endpoint.evaluate((bytes) => {
              const packet = window.__errorPeer!.receive(new Uint8Array(bytes));
              return packet ? Array.from(packet) : null;
            }, bytes);
            if (decoded) received.push(decoded);
          })
          .catch((error: unknown) => {
            failures.push(String(error));
          });
      });
    });
    try {
      await page.goto("/client");
      await page.evaluate(
        async ({ port, encryption }) => {
          const url = "/src/network/ClientConnection.ts";
          const { ClientConnection } = await import(/* @vite-ignore */ url);
          const connection = new ClientConnection({
            serverUrl: `ws://127.0.0.1:${port}`,
            applicationEncryption: encryption,
            discoveryHandshake: true,
          });
          window.__errorConnection = connection;
          window.__errorStates = [];
          window.__errorReports = [];
          window.addEventListener("ascii-chat:error", (event) => {
            window.__errorReports.push((event as CustomEvent).detail.title);
          });
          connection.onStateChange((state: number) =>
            window.__errorStates.push(state),
          );
          await connection.connect();
        },
        { port, encryption },
      );
      const state = () =>
        page.evaluate(() => window.__errorConnection!.getState());
      // ConnectionState.CONNECTED is 3.
      await expect.poll(state, { timeout: 15000 }).toBe(3);
      const dialog = page.getByRole("alertdialog", {
        name: "ascii-chat error",
      });
      await expect(dialog).toContainText("Session full during negotiation");
      expect(await page.evaluate(() => window.__errorStates)).not.toContain(4);
      await dialog.getByRole("button", { name: "Dismiss" }).click();
      const send = async (packet: Buffer) => {
        await incoming;
        expect(failures).toEqual([]);
        await endpoint.evaluate(
          (bytes) => window.__errorPeer!.sendPacket(new Uint8Array(bytes)),
          Array.from(packet),
        );
      };
      await send(errorPacket(23, "Audio device failed"));
      await expect(dialog).toContainText("Audio device failed");
      await expect.poll(state).toBe(3);
      // Local reports travel back to the server while the remote modal is open.
      await page.evaluate(async () => {
        const url = "/src/errors/errorBus.ts";
        const { forwardJavaScriptError } = await import(/* @vite-ignore */ url);
        forwardJavaScriptError(new Error("browser capture failed"));
      });
      await expect
        .poll(
          () =>
            received.filter(
              (bytes) =>
                new DataView(new Uint8Array(bytes).buffer).getUint16(8) ===
                2003,
            ).length,
        )
        .toBe(1);
      await dialog.getByRole("button", { name: "Dismiss" }).click();
      await send(errorPacket(48, "Session is full"));
      await expect(dialog).toContainText("Session is full");
      await expect.poll(() => connections).toBe(3);
      await expect.poll(state, { timeout: 15000 }).toBe(3);
      await expect
        .poll(() => page.evaluate(() => window.__errorReports))
        .toEqual([
          "Remote protocol error 48",
          "Remote protocol error 23",
          "Remote protocol error 48",
        ]);
      await dialog.getByRole("button", { name: "Dismiss" }).click();
      await send(errorPacket(53, "Internal server error"));
      await expect(dialog).toContainText("Internal server error");
      await expect.poll(state).toBe(4);
      await page.waitForTimeout(2200);
      expect(connections).toBe(3);
      expect(failures).toEqual([]);
      await expect
        .poll(() => page.evaluate(() => window.__errorReports.length))
        .toBe(4);
    } finally {
      await page.evaluate(() => window.__errorConnection?.disconnect());
      server.clients.forEach((client) => client.terminate());
      await incoming;
      await endpoint.evaluate(() => window.__errorPeer?.close());
      await endpoint.close();
      await new Promise<void>((resolve) => server.close(() => resolve()));
    }
  });
}

declare global {
  interface Window {
    __errorPeer?: import("../../src/wasm/client").BrowserSession;
    __errorConnection?: import("../../src/network/ClientConnection").ClientConnection;
    __errorStates: number[];
    __errorReports: string[];
    sendErrorTestPacket(bytes: number[]): Promise<void>;
  }
}

for (const terminalCode of [48, 53]) {
  test(`encrypted WebRTC errors stay visible and ${terminalCode === 48 ? "allow recovery" : "stop recovery"}`, async ({
    page,
  }) => {
    await page.goto("/client");
    await page.evaluate(async () => {
      const engineUrl = "/src/wasm/client.ts";
      const sessionUrl = "/src/network/WebRTCSession.ts";
      const { initClientWasm, BrowserSession, processClientEvent, PacketType } =
        await import(/* @vite-ignore */ engineUrl);
      const { WebRTCSession } = await import(/* @vite-ignore */ sessionUrl);
      await initClientWasm();
      const serializePacket = (packetType: number, payload: Uint8Array) => {
        const result = processClientEvent({
          type: "serialize",
          packetType,
          payload,
          clientId: 0,
        });
        if (result.code || !result.bytes)
          throw new Error("Serialization failed");
        return result.bytes;
      };
      const remote = new RTCPeerConnection({ iceServers: [] });
      const channel = remote.createDataChannel("acip", {
        negotiated: true,
        id: 0,
        ordered: true,
      });
      const crypto = await BrowserSession.create(
        {},
        true,
        true,
        (bytes: Uint8Array) => channel.send(new Uint8Array(bytes)),
      );
      const received: number[] = [];
      let acknowledged = false;
      channel.onmessage = (event) => {
        const decoded = crypto.receive(new Uint8Array(event.data));
        if (decoded)
          received.push(
            new DataView(decoded.buffer, decoded.byteOffset).getUint16(8),
          );
        if (crypto.ready && !acknowledged) {
          acknowledged = true;
          crypto.sendPacket(
            serializePacket(PacketType.SERVER_STATE, new Uint8Array()),
          );
        }
      };
      channel.onopen = () => crypto.flush();
      const session = new WebRTCSession({
        signalingUrl: "ws://127.0.0.1",
        sessionName: "local-test",
        password: "",
        stunServers: [],
        turnServers: [],
        applicationEncryption: true,
      });
      // Supply a completed discovery result and exchange SDP locally. The
      // production session owns the real DataChannel and C security handshake.
      Reflect.set(session, "joined", {
        sessionId: new Uint8Array(16),
        participantId: new Uint8Array(16),
        hostId: new Uint8Array(16),
        turnUsername: "",
        turnPassword: "",
      });
      Reflect.get(session, "signaling").sendPacket = () => {};
      window.__rtcError = { session, remote, crypto, received };
      await Reflect.get(session, "connectPeer").call(session);
      const peer = Reflect.get(session, "peer") as RTCPeerConnection;
      const gather = (pc: RTCPeerConnection) =>
        pc.iceGatheringState === "complete"
          ? Promise.resolve()
          : new Promise<void>((resolve) => {
              const listener = () => {
                if (pc.iceGatheringState === "complete") {
                  pc.removeEventListener("icegatheringstatechange", listener);
                  resolve();
                }
              };
              pc.addEventListener("icegatheringstatechange", listener);
            });
      await gather(peer);
      await remote.setRemoteDescription(peer.localDescription!);
      await remote.setLocalDescription(await remote.createAnswer());
      await gather(remote);
      await peer.setRemoteDescription(remote.localDescription!);
    });
    try {
      await expect
        .poll(() => page.evaluate(() => window.__rtcError!.session.getState()))
        .toBe(3);
      await page.evaluate(
        (bytes) => window.__rtcError!.crypto.sendPacket(new Uint8Array(bytes)),
        Array.from(errorPacket(23, "Peer audio failed")),
      );
      const dialog = page.getByRole("alertdialog", {
        name: "ascii-chat error",
      });
      await expect(dialog).toContainText("Peer audio failed");
      await page.evaluate(async () => {
        const url = "/src/errors/errorBus.ts";
        const { forwardJavaScriptError } = await import(/* @vite-ignore */ url);
        forwardJavaScriptError(new Error("WebRTC capture failed"));
      });
      await expect
        .poll(() => page.evaluate(() => window.__rtcError!.received))
        .toContain(2003);
      await page.evaluate(async () => {
        const url = "/src/wasm/client.ts";
        const { processClientEvent, PacketType } = await import(
          /* @vite-ignore */ url
        );
        const result = processClientEvent({
          type: "serialize",
          packetType: PacketType.PING,
          payload: new Uint8Array(8),
          clientId: 0,
        });
        if (result.code || !result.bytes)
          throw new Error("Serialization failed");
        window.__rtcError!.crypto.sendPacket(result.bytes);
      });
      await expect
        .poll(() => page.evaluate(() => window.__rtcError!.received))
        .toContain(5002);
      await dialog.getByRole("button", { name: "Dismiss" }).click();
      await page.evaluate(
        (bytes) => window.__rtcError!.crypto.sendPacket(new Uint8Array(bytes)),
        Array.from(errorPacket(terminalCode, "Peer session rejected")),
      );
      await expect(dialog).toContainText("Peer session rejected");
      await expect
        .poll(() => page.evaluate(() => window.__rtcError!.session.getState()))
        .toBe(4);
      expect(
        await page.evaluate(() => window.__rtcError!.session.canReconnect),
      ).toBe(terminalCode === 48);
    } finally {
      await page.evaluate(() => {
        window.__rtcError?.crypto.close();
        window.__rtcError?.session.disconnect();
        window.__rtcError?.remote.close();
      });
    }
  });
}

declare global {
  interface Window {
    __rtcError?: {
      session: import("../../src/network/WebRTCSession").WebRTCSession;
      remote: RTCPeerConnection;
      crypto: import("../../src/wasm/client").BrowserSession;
      received: number[];
    };
  }
}
