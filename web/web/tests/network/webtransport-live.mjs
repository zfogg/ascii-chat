import { chromium } from "@playwright/test";
import { createServer as createVite } from "vite";
import { execFileSync, spawn } from "node:child_process";
import {
  mkdtempSync,
  readFileSync,
  mkdirSync,
  copyFileSync,
  rmSync,
  existsSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { createSocket } from "node:dgram";
import { createServer as createTcp } from "node:net";
import { X509Certificate, createHash } from "node:crypto";
// Run from web/web: node tests/network/webtransport-live.mjs <native-binary> <wasm-directory>.
// Requires openssl, ssh-keygen, and Playwright Chromium (or CHROME_PATH).
const [binary, wasmDir] = process.argv.slice(2);
if (!binary || !wasmDir)
  throw new Error(
    "Usage: node tests/network/webtransport-live.mjs <native-binary> <wasm-directory>",
  );
const dir = mkdtempSync(join(tmpdir(), "acip-wt-app-")),
  key = join(dir, "key.pem"),
  cert = join(dir, "cert.pem");
execFileSync(
  process.env.OPENSSL || "openssl",
  [
    "req",
    "-x509",
    "-newkey",
    "ec",
    "-pkeyopt",
    "ec_paramgen_curve:P-256",
    "-nodes",
    "-days",
    "1",
    "-subj",
    "/CN=localhost",
    "-addext",
    "subjectAltName=DNS:localhost,IP:127.0.0.1",
    "-keyout",
    key,
    "-out",
    cert,
  ],
  { stdio: "ignore" },
);
const identity = join(dir, "identity");
execFileSync("ssh-keygen", ["-q", "-t", "ed25519", "-N", "", "-f", identity]);
const serverPublicKey = readFileSync(`${identity}.pub`, "utf8");
const otherIdentity = join(dir, "other-identity");
execFileSync("ssh-keygen", [
  "-q",
  "-t",
  "ed25519",
  "-N",
  "",
  "-f",
  otherIdentity,
]);
const wrongPublicKey = readFileSync(`${otherIdentity}.pub`, "utf8");
const publicDir = join(dir, "public");
mkdirSync(join(publicDir, "wasm"), { recursive: true });
for (const file of ["client.js", "client.wasm"])
  copyFileSync(join(resolve(wasmDir), file), join(publicDir, "wasm", file));
const vite = await createVite({
  configFile: false,
  root: process.cwd(),
  publicDir,
  resolve: {
    alias: { "@ascii-chat/shared": resolve("../packages/shared/src") },
  },
  server: {
    host: "127.0.0.1",
    port: 0,
    strictPort: false,
    hmr: false,
    https: { key: readFileSync(key), cert: readFileSync(cert) },
    headers: {
      "Cross-Origin-Embedder-Policy": "require-corp",
      "Cross-Origin-Opener-Policy": "same-origin",
    },
  },
});
await vite.listen();
const pagePort = vite.httpServer.address().port;
async function freeUdp() {
  const socket = createSocket("udp4");
  await new Promise((resolve) => socket.bind(0, "127.0.0.1", resolve));
  const port = socket.address().port;
  await new Promise((resolve) => socket.close(resolve));
  return port;
}
const udpPort = await freeUdp();
const nativePort = await freeUdp();
const blackhole = createSocket("udp4");
let droppedUdpPackets = 0;
blackhole.on("message", () => ++droppedUdpPackets);
await new Promise((resolve) => blackhole.bind(0, "127.0.0.1", resolve));
const blockedPort = blackhole.address().port;
async function freeTcp() {
  const s = createTcp();
  await new Promise((r) => s.listen(0, "127.0.0.1", r));
  const p = s.address().port;
  await new Promise((r) => s.close(r));
  return p;
}
const tcpPort = await freeTcp(),
  wsPort = await freeTcp();
const hash = [
  ...createHash("sha256")
    .update(new X509Certificate(readFileSync(cert)).raw)
    .digest(),
];
const env = { ...process.env, ASCII_CHAT_DISABLE_FIRST_USE: "1" };
const server = spawn(
  resolve(binary),
  [
    "--log-level",
    "debug",
    "server",
    "127.0.0.1",
    "--port",
    String(tcpPort),
    "--websocket-port",
    String(wsPort),
    "--websocket-tls-cert",
    cert,
    "--websocket-tls-key",
    key,
    "--quic-bind-address",
    "127.0.0.1",
    "--quic-port",
    String(nativePort),
    "--webtransport-bind-address",
    "127.0.0.1",
    "--webtransport-port",
    String(udpPort),
    "--webtransport-origin",
    `https://127.0.0.1:${pagePort}`,
    "--quic-tls-cert",
    cert,
    "--quic-tls-key",
    key,
    "--password",
    "test-password",
    "--key",
    identity,
    "--status-screen=false",
  ],
  { env, windowsHide: true },
);
let logs = "";
server.stdout.on("data", (c) => (logs += c));
server.stderr.on("data", (c) => (logs += c));
let browser;
const participants = [];
try {
  const deadline = Date.now() + 20000;
  while (
    !logs.includes("Server accepting connections") &&
    server.exitCode === null &&
    Date.now() < deadline
  )
    await new Promise((r) => setTimeout(r, 50));
  if (!logs.includes("Server accepting connections"))
    throw new Error(`server not ready: ${logs}`);
  for (let i = 0; i < 2; ++i) {
    const logFile = join(dir, `native-${i}.log`);
    const process = spawn(
      resolve(binary),
      [
        "--log-file",
        logFile,
        "client",
        `quic://localhost:${nativePort}`,
        "--quic-ca-file",
        cert,
        "--server-key",
        `${identity}.pub`,
        "--password",
        "test-password",
        "--test-pattern",
        "--volume",
        "0",
        "--audio=false",
        "--video-codec",
        "raw",
        "--snapshot",
        "--snapshot-delay",
        "25",
        "--splash-screen=false",
        "--color-mode",
        "none",
        "--reconnect-attempts",
        "0",
      ],
      {
        env: { ...env, ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK: "1" },
        windowsHide: true,
        stdio: ["ignore", "pipe", "pipe"],
      },
    );
    const participant = { process, logFile, output: "", outputBytes: 0 };
    process.stdout.on(
      "data",
      (data) => (participant.outputBytes += data.length),
    );

    process.stderr.on("data", (data) => (participant.output += data));
    participants.push(participant);
  }
  const nativeDeadline = Date.now() + 20000;
  while (Date.now() < nativeDeadline) {
    const ingress = new Set(
      [...logs.matchAll(/Server image ingress client=(\S+)/g)].map(
        (match) => match[1],
      ),
    );
    if (ingress.size >= 2) break;
    if (participants.some(({ process }) => process.exitCode !== null))
      throw new Error("native participant exited before media ingress");
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  const nativeIngress = new Set(
    [...logs.matchAll(/Server image ingress client=(\S+)/g)].map(
      (match) => match[1],
    ),
  );
  if (nativeIngress.size < 2)
    throw new Error("both native participants must send media");
  for (const { logFile } of participants) {
    const clientLog = readFileSync(logFile, "utf8");
    if (
      !clientLog.includes("using native QUIC") ||
      !clientLog.includes("Server identity key verified against --server-key")
    )
      throw new Error("native participant did not use authenticated QUIC");
  }
  browser = await chromium.launch({
    headless: true,
    executablePath: process.env.CHROME_PATH || undefined,
    args: ["--enable-quic"],
  });
  const context = await browser.newContext({ ignoreHTTPSErrors: true });
  const page = await context.newPage();
  page.on("console", (message) => console.log("browser:", message.text()));
  page.on("pageerror", (error) => console.log("pageerror:", String(error)));
  await page.goto(
    `https://localhost:${pagePort}/tests/network/crypto-policy.html`,
  );
  const rejectedOrigin = await page.evaluate(
    async ({ udpPort, hash }) => {
      const wt = new WebTransport(`https://127.0.0.1:${udpPort}/acip`, {
        serverCertificateHashes: [
          { algorithm: "sha-256", value: new Uint8Array(hash) },
        ],
      });
      try {
        await Promise.race([
          wt.ready,
          new Promise((_, reject) =>
            setTimeout(() => reject(new Error("timeout")), 8000),
          ),
        ]);
        wt.close();
        return false;
      } catch (error) {
        if (String(error).includes("timeout")) throw error;
        return true;
      }
    },
    { udpPort, hash },
  );
  if (!rejectedOrigin) throw new Error("incorrect origin was accepted");
  await page.goto(
    `https://127.0.0.1:${pagePort}/tests/network/crypto-policy.html`,
  );
  const rejectedPath = await page.evaluate(
    async ({ udpPort, hash }) => {
      const wt = new WebTransport(`https://127.0.0.1:${udpPort}/wrong`, {
        serverCertificateHashes: [
          { algorithm: "sha-256", value: new Uint8Array(hash) },
        ],
      });
      try {
        await Promise.race([
          wt.ready,
          new Promise((_, reject) =>
            setTimeout(() => reject(new Error("timeout")), 8000),
          ),
        ]);
        wt.close();
        return false;
      } catch (error) {
        if (String(error).includes("timeout")) throw error;
        return true;
      }
    },
    { udpPort, hash },
  );
  if (!rejectedPath) throw new Error("incorrect path was accepted");
  const rejectedCertificate = await page.evaluate(
    async ({ udpPort, hash }) => {
      const badHash = new Uint8Array(hash);
      badHash[0] ^= 1;
      const wt = new WebTransport(`https://127.0.0.1:${udpPort}/acip`, {
        serverCertificateHashes: [{ algorithm: "sha-256", value: badHash }],
      });
      try {
        await Promise.race([
          wt.ready,
          new Promise((_, reject) =>
            setTimeout(() => reject(new Error("timeout")), 8000),
          ),
        ]);
        wt.close();
        return false;
      } catch (error) {
        if (String(error).includes("timeout")) throw error;
        return true;
      }
    },
    { udpPort, hash },
  );
  if (!rejectedCertificate)
    throw new Error("incorrect certificate was accepted");
  const result = await page.evaluate(
    async ({ udpPort, hash, serverPublicKey }) => {
      const Original = globalThis.WebTransport;
      globalThis.WebTransport = class extends Original {
        constructor(url, options) {
          super(url, {
            ...options,
            serverCertificateHashes: [
              { algorithm: "sha-256", value: new Uint8Array(hash) },
            ],
          });
        }
      };
      const { WebTransportClientSession } =
        await import("/src/network/WebTransportClientSession.ts");
      const {
        buildCapabilitiesPacket,
        buildStreamStartPacket,
        buildImageFramePayload,
      } = await import("/src/network/packetBuilders.ts");
      const session = new WebTransportClientSession({
        serverUrl: `https://127.0.0.1:${udpPort}/acip`,
        cryptoOptions: {
          password: "test-password",
          expectedServerPublicKeyText: serverPublicKey,
        },
        width: 40,
        height: 12,
      });
      const states = [];
      const packets = [];
      let resolveReady, rejectReady;
      const ready = new Promise((resolve, reject) => {
        resolveReady = resolve;
        rejectReady = reject;
      });
      session.onStateChange((state) => {
        states.push(state);
        if (state === 3) resolveReady();
        if (state === 4) rejectReady(new Error("session error"));
      });
      session.onPacketReceived((packet) => packets.push(packet.type));
      try {
        await session.connect();
        await Promise.race([
          ready,
          new Promise((_, reject) =>
            setTimeout(() => reject(new Error("handshake timeout")), 15000),
          ),
        ]);
        session.sendPacket(5000, buildCapabilitiesPacket(40, 12));
        session.sendPacket(5005, buildStreamStartPacket(false));
        session.sendPacket(5001, new Uint8Array());
        const until = Date.now() + 8000;
        while (
          !packets.includes(5002) &&
          !states.includes(4) &&
          Date.now() < until
        )
          await new Promise((r) => setTimeout(r, 25));
        if (!packets.includes(5002) || states.includes(4))
          throw new Error(
            `pong missing or disconnected: ${states} packets ${packets}`,
          );
        if (!session.channel.supportsDatagrams())
          throw new Error("WebTransport datagrams not negotiated");
        const rgba = new Uint8Array(64 * 48 * 4);
        for (let i = 0; i < rgba.length; i += 4) {
          rgba[i] = 0x35;
          rgba[i + 1] = 0x72;
          rgba[i + 2] = 0xa9;
          rgba[i + 3] = 255;
        }
        const frame = buildImageFramePayload(rgba, 64, 48);
        for (let i = 0; i < 30; ++i) {
          session.sendPacket(3001, frame);
          await new Promise((r) => setTimeout(r, 50));
        }
        return {
          ok: true,
          states,
          packets,
          feedback: session.getUploadFeedback(),
        };
      } catch (error) {
        return { ok: false, states, packets, error: String(error) };
      } finally {
        session.disconnect();
      }
    },
    { udpPort, hash, serverPublicKey },
  );
  console.log("APPLICATION RESULT", JSON.stringify(result));
  if (!result.ok) throw new Error(result.error);
  if (!result.packets.includes(3000))
    throw new Error("browser received no server-rendered media");
  if (participants.some(({ process }) => process.exitCode !== null))
    throw new Error("native participant left while browser was connected");
  const afterBrowserLeaves = logs.length;
  const outputBefore = participants.map(({ outputBytes }) => outputBytes);
  const continueDeadline = Date.now() + 6000;
  while (Date.now() < continueDeadline) {
    const laterIngress = new Set(
      [
        ...logs
          .slice(afterBrowserLeaves)
          .matchAll(/Server image ingress client=(\S+)/g),
      ].map((match) => match[1]),
    );
    if (
      [...nativeIngress].every((id) => laterIngress.has(id)) &&
      participants.every((p, i) => p.outputBytes > outputBefore[i])
    )
      break;
    if (participants.some(({ process }) => process.exitCode !== null))
      throw new Error("native participant exited after browser left");
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  const laterIngress = new Set(
    [
      ...logs
        .slice(afterBrowserLeaves)
        .matchAll(/Server image ingress client=(\S+)/g),
    ].map((match) => match[1]),
  );
  if (
    ![...nativeIngress].every((id) => laterIngress.has(id)) ||
    !participants.every((p, i) => p.outputBytes > outputBefore[i])
  )
    throw new Error(
      "native call did not continue exchanging media after browser left",
    );
  const allIngress = new Set(
    [...logs.matchAll(/Server image ingress client=(\S+)/g)].map(
      (match) => match[1],
    ),
  );
  const browserClient = [...allIngress].find(
    (client) => !nativeIngress.has(client),
  );
  if (!browserClient)
    throw new Error("server did not receive the browser's visual datagram");
  const closeDeadline = Date.now() + 8000;
  while (
    !logs.includes(`Removing client ${browserClient} - marking inactive`) &&
    Date.now() < closeDeadline
  )
    await new Promise((resolve) => setTimeout(resolve, 50));
  if (!logs.includes(`Removing client ${browserClient} - marking inactive`))
    throw new Error(
      "browser session was not removed promptly after disconnect",
    );
  for (const mode of ["unsupported", "blocked", "wrong-key"]) {
    const fallbackResult = await page.evaluate(
      async ({
        udpPort,
        blockedPort,
        wsPort,
        hash,
        serverPublicKey,
        wrongPublicKey,
        mode,
      }) => {
        const Original = globalThis.WebTransport;
        globalThis.WebTransport =
          mode !== "blocked"
            ? undefined
            : class extends Original {
                constructor(url, options) {
                  super(url, {
                    ...options,
                    serverCertificateHashes: [
                      { algorithm: "sha-256", value: new Uint8Array(hash) },
                    ],
                  });
                }
              };
        const { WebTransportFallbackSession } =
          await import("/src/network/WebTransportFallbackSession.ts");
        const session = new WebTransportFallbackSession(
          {
            serverUrl: `https://127.0.0.1:${mode === "blocked" ? blockedPort : udpPort}/acip`,
            cryptoOptions: {
              password: "test-password",
              expectedServerPublicKeyText:
                mode === "wrong-key" ? wrongPublicKey : serverPublicKey,
            },
          },
          `wss://127.0.0.1:${wsPort}`,
          1500,
        );
        const states = [];
        const packets = [];
        let resolveReady, rejectReady;
        const ready = new Promise((resolve, reject) => {
          resolveReady = resolve;
          rejectReady = reject;
        });
        session.onStateChange((state) => {
          states.push(state);
          if (state === 3) resolveReady();
          if (state === 4) rejectReady(new Error("fallback connection failed"));
        });
        session.onPacketReceived((packet) => packets.push(packet.type));
        try {
          await session.connect();
          await Promise.race([
            ready,
            new Promise((_, reject) =>
              setTimeout(
                () => reject(new Error("fallback handshake timeout")),
                15000,
              ),
            ),
          ]);
          const { buildCapabilitiesPacket, buildStreamStartPacket } =
            await import("/src/network/packetBuilders.ts");
          session.sendPacket(5000, buildCapabilitiesPacket(40, 12));
          session.sendPacket(5005, buildStreamStartPacket(false));
          session.sendPacket(5001, new Uint8Array());
          const deadline = Date.now() + 8000;
          while (!packets.includes(5002) && Date.now() < deadline)
            await new Promise((resolve) => setTimeout(resolve, 25));
          if (!packets.includes(5002)) throw new Error("fallback pong missing");
          return {
            ok: true,
            states,
            packets,
            transport: session.transportType,
          };
        } catch (error) {
          return { ok: false, states, packets, error: String(error) };
        } finally {
          session.disconnect();
          globalThis.WebTransport = Original;
        }
      },
      {
        udpPort,
        blockedPort,
        wsPort,
        hash,
        serverPublicKey,
        wrongPublicKey,
        mode,
      },
    );
    console.log("FALLBACK RESULT", mode, JSON.stringify(fallbackResult));
    if (mode === "blocked" && droppedUdpPackets === 0)
      throw new Error("browser did not send UDP packets to the blackhole");
    if (mode === "wrong-key") {
      if (fallbackResult.ok || fallbackResult.states.includes(3))
        throw new Error("fallback accepted the wrong server identity");
    } else if (!fallbackResult.ok || fallbackResult.transport !== "websocket") {
      throw new Error(`WSS fallback failed: ${fallbackResult.error}`);
    }
  }
} catch (error) {
  console.error(
    "SERVER ERRORS",
    logs
      .split("\n")
      .filter((line) =>
        /ERROR|WARN|disconnect|handshake|Invalid|invalid/i.test(line),
      )
      .slice(-80)
      .join("\n"),
  );
  console.error("SERVER LOG", logs.slice(-12000));
  for (const participant of participants)
    console.error(
      "PARTICIPANT",
      participant.output,
      existsSync(participant.logFile)
        ? readFileSync(participant.logFile, "utf8").slice(-12000)
        : "no log",
    );
  throw error;
} finally {
  if (browser) await browser.close();
  await new Promise((resolve) => blackhole.close(resolve));
  for (const { process } of participants) process.kill();
  server.kill();
  await vite.close();
  rmSync(dir, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
}
