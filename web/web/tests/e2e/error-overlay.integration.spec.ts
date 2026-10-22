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

function errorPacket(): Buffer {
  const message = Buffer.from(
    frames
      .map(
        ([file, fullFile, line, func, text]) =>
          `${text} (${file}:${line} in ${func}())\tfull-path=${fullFile}`,
      )
      .join("\n"),
  );
  const payload = Buffer.alloc(8 + message.length);
  payload.writeUInt32BE(62, 0);
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
      await expect(row.locator(".text-blue-400")).toHaveText(`${file}:${line}`);
      await expect(row.locator(".text-blue-400")).toHaveClass(/underline/);
      await expect(row.locator(".text-blue-400")).toHaveAttribute(
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

    await rows.nth(0).locator(".text-blue-400").click();
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
