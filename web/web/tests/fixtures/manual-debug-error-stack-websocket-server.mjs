import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { readFile } from "node:fs/promises";
import { WebSocket, WebSocketServer } from "ws";

const LISTEN_PORT = 38453;
const UPSTREAM_URL = "ws://127.0.0.1:27226";
const WORKTREE_ROOT = resolve(
  dirname(fileURLToPath(import.meta.url)),
  "../../../..",
).replaceAll("\\", "/");

const testFrames = [
  [
    "lib/network/connection.c",
    376,
    "connection_receive",
    "client connection failed",
  ],
  [
    "lib/network/negotiation.c",
    228,
    "media_receive",
    "session negotiation failed",
  ],
  [
    "lib/network/acip/handlers.c",
    702,
    "acip_handle_packet",
    "ACIP message rejected",
  ],
  [
    "lib/network/packet/packet.c",
    188,
    "packet_validate",
    "packet validation failed",
  ],
  ["lib/network/tcp/transport.c", 521, "tcp_recv", "socket read failed"],
];

function usage() {
  return [
    "Usage: node manual-debug-error-stack-websocket-server.mjs [options] [frames.json ...]",
    "",
    "  --test-data[=true|false]  Include the built-in five-frame error stack (default: false)",
    "  --source-root PATH        Full source root sent for debug path copying",
    "                            (default: the current worktree root)",
    "",
    "Each JSON file may contain an array of [file, line, function, message] tuples,",
    "an array of { file, line, function, message, fullFile? } objects, or { frames: [...] }.",
  ].join("\n");
}

function parseBoolean(value, option) {
  if (value === undefined || value === "true") return true;
  if (value === "false") return false;
  throw new Error(`${option} expects true or false`);
}

function parseArguments(args) {
  let sourceRoot = WORKTREE_ROOT;
  let includeTestData = false;
  const jsonFiles = [];
  for (let index = 0; index < args.length; index++) {
    const argument = args[index];
    if (argument === "--help" || argument === "-h") {
      console.log(usage());
      process.exit(0);
    } else if (argument === "--test-data") {
      includeTestData = true;
    } else if (argument.startsWith("--test-data=")) {
      includeTestData = parseBoolean(argument.slice(12), "--test-data");
    } else if (argument === "--source-root") {
      sourceRoot = args[++index];
      if (!sourceRoot) throw new Error("--source-root requires a path");
    } else if (argument.startsWith("--source-root=")) {
      sourceRoot = argument.slice(14);
      if (!sourceRoot) throw new Error("--source-root requires a path");
    } else if (argument.endsWith(".json")) {
      jsonFiles.push(argument);
    } else {
      throw new Error(`Unknown argument: ${argument}\n\n${usage()}`);
    }
  }
  return {
    includeTestData,
    jsonFiles,
    sourceRoot: resolve(sourceRoot).replaceAll("\\", "/"),
  };
}

function normalizeFrame(frame, sourceRoot) {
  const value = Array.isArray(frame)
    ? { file: frame[0], line: frame[1], function: frame[2], message: frame[3] }
    : frame;
  if (
    !value ||
    typeof value.file !== "string" ||
    !Number.isInteger(value.line) ||
    typeof value.function !== "string" ||
    typeof value.message !== "string"
  )
    throw new Error(`Invalid error frame: ${JSON.stringify(frame)}`);
  return {
    ...value,
    fullFile: value.fullFile ?? `${sourceRoot}/${value.file}`,
  };
}

async function loadFrames({ includeTestData, jsonFiles, sourceRoot }) {
  const loaded = includeTestData ? [...testFrames] : [];
  for (const jsonFile of jsonFiles) {
    const document = JSON.parse(await readFile(jsonFile, "utf8"));
    const frames = Array.isArray(document) ? document : document.frames;
    if (!Array.isArray(frames))
      throw new Error(
        `${jsonFile} must contain an array or a { "frames": [...] } object`,
      );
    loaded.push(...frames);
  }
  return loaded.map((frame) => normalizeFrame(frame, sourceRoot));
}

const options = parseArguments(process.argv.slice(2));
const frames = await loadFrames(options);

function crc32c(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++)
      crc = (crc >>> 1) ^ (crc & 1 ? 0x82f63b78 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function errnoStackPacket() {
  const message = frames
    .map(
      ({ file, line, function: func, message: text, fullFile }) =>
        `${text} (${file}:${line} in ${func}())\tfull-path=${fullFile}`,
    )
    .join("\n");
  const text = Buffer.from(message);
  const payload = Buffer.alloc(8 + text.length);
  payload.writeUInt32BE(62, 0);
  payload.writeUInt32BE(text.length, 4);
  text.copy(payload, 8);

  const packet = Buffer.alloc(22 + payload.length);
  packet.writeBigUInt64BE(0xa5c11c4a1n, 0);
  packet.writeUInt16BE(2003, 8);
  packet.writeUInt32BE(payload.length, 10);
  packet.writeUInt32BE(crc32c(payload), 14);
  packet.writeUInt32BE(0, 18);
  payload.copy(packet, 22);
  return packet;
}

const relay = new WebSocketServer({ host: "127.0.0.1", port: LISTEN_PORT });
let corruptedServerFrame = false;

relay.on("connection", (browser) => {
  const upstream = new WebSocket(UPSTREAM_URL, "acip");
  const pending = [];

  upstream.on("open", () => {
    for (const packet of pending.splice(0)) upstream.send(packet);
    if (frames.length > 0) browser.send(errnoStackPacket());
  });
  upstream.on("message", (packet) => {
    const bytes = Buffer.from(packet);
    const type = bytes.length >= 10 ? bytes.readUInt16BE(8) : 0;
    if (!corruptedServerFrame && type === 3000 && bytes.length > 22) {
      corruptedServerFrame = true;
      bytes[bytes.length - 1] ^= 0xff;
    }
    browser.send(bytes);
  });
  upstream.on("close", () => browser.close());
  browser.on("close", () => upstream.close());
  browser.on("message", (packet) => {
    const bytes = Buffer.from(packet);
    const type = bytes.length >= 10 ? bytes.readUInt16BE(8) : 0;
    if (upstream.readyState === WebSocket.OPEN) upstream.send(bytes);
    else pending.push(bytes);
    if (type === 2003) browser.send(bytes);
  });
});

console.log(
  `Manual debug error-stack test server listening on ws://127.0.0.1:${LISTEN_PORT} with ${frames.length} injected frame(s)`,
);
