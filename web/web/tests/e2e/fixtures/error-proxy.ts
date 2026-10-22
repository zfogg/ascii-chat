import { readFile } from "node:fs/promises";
import { WebSocketServer, type WebSocket } from "ws";

export type ErrnoFrame = {
  file: string;
  line: number;
  function: string;
  message: string;
};

type ErrnoFixture = { code: number; frames: ErrnoFrame[] };

function crc32c(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++)
      crc = (crc >>> 1) ^ (crc & 1 ? 0x82f63b78 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function errorPacket(fixture: ErrnoFixture): Buffer {
  const message = Buffer.from(
    fixture.frames
      .map((frame) => `${frame.message} (${frame.file}:${frame.line} in ${frame.function}())`)
      .join("\n"),
  );
  const payload = Buffer.alloc(8 + message.length);
  payload.writeUInt32BE(fixture.code, 0);
  payload.writeUInt32BE(message.length, 4);
  message.copy(payload, 8);
  const packet = Buffer.alloc(22 + payload.length);
  packet.writeBigUInt64BE(0xa5c11c4a1n, 0);
  packet.writeUInt16BE(2003, 8);
  packet.writeUInt32BE(payload.length, 10);
  packet.writeUInt32BE(crc32c(payload), 14);
  payload.copy(packet, 22);
  return packet;
}

/** A real WebSocket peer that sends the project's ACIP error packet from JSON. */
export async function startErrorProxy(fixtureUrl: URL): Promise<{
  url: string;
  close: () => Promise<void>;
}> {
  const fixture = JSON.parse(await readFile(fixtureUrl, "utf8")) as ErrnoFixture;
  const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  await new Promise<void>((resolve, reject) => {
    server.once("listening", resolve);
    server.once("error", reject);
  });
  server.on("connection", (socket: WebSocket) => socket.send(errorPacket(fixture)));
  const { port } = server.address() as { port: number };
  return {
    url: `ws://127.0.0.1:${port}`,
    close: async () => {
      server.clients.forEach((socket) => socket.terminate());
      await new Promise<void>((resolve) => server.close(() => resolve()));
    },
  };
}
