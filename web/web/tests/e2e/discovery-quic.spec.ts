import { expect, test } from "@playwright/test";
import { execFileSync, spawn, type ChildProcess } from "node:child_process";
import { createSocket } from "node:dgram";
import { createServer } from "node:net";
import { createServer as createVite, type ViteDevServer } from "vite";
import { X509Certificate, createHash } from "node:crypto";
import { mkdirSync, writeFileSync, readFileSync } from "node:fs";
import { resolve } from "node:path";

async function port(): Promise<number> {
  const server = createServer();
  await new Promise<void>((done) => server.listen(0, "127.0.0.1", done));
  const result = (server.address() as { port: number }).port;
  await new Promise<void>((done) => server.close(() => done()));
  return result;
}
async function sharedPort(): Promise<number> {
  for (let attempt = 0; attempt < 100; ++attempt) {
    const value = await port();
    const socket = createSocket("udp4");
    const ok = await new Promise<boolean>((done) => {
      socket.once("error", () => done(false));
      socket.bind(value, "127.0.0.1", () => done(true));
    });
    if (ok) await new Promise<void>((done) => socket.close(() => done()));
    else socket.close();
    if (ok) return value;
  }
  throw new Error("No common TCP and UDP port available");
}

class Native {
  readonly process: ChildProcess;
  readonly closed: Promise<void>;
  output = "";
  constructor(binary: string, home: string, args: string[]) {
    this.process = spawn(
      binary,
      ["--no-check-update", "--log-level", "info", ...args],
      {
        windowsHide: true,
        env: {
          ...process.env,
          HOME: home,
          USERPROFILE: home,
          APPDATA: home,
          XDG_CONFIG_HOME: home,
          ASCII_CHAT_OTEL_ENABLED_TELEMETRY: "none",
          ASCII_CHAT_DISABLE_FIRST_USE: "1",
          ASCII_CHAT_QUESTION_PROMPT_RESPONSE: "y;y;y",
          ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK: "1",
        },
      },
    );
    this.closed = new Promise((done) =>
      this.process.once("close", () => done()),
    );
    for (const stream of [this.process.stdout, this.process.stderr])
      stream?.on("data", (data: Buffer) => {
        this.output += data.toString();
      });
  }
  async ready(pattern: RegExp, timeout = 30000) {
    await expect
      .poll(
        () => {
          if (this.process.exitCode !== null)
            throw new Error(
              `Native process exited: ${this.output.slice(-10000)}`,
            );
          return this.output;
        },
        { timeout },
      )
      .toMatch(pattern);
    expect(this.process.exitCode).toBeNull();
  }
  async stop() {
    if (this.process.exitCode === null && this.process.signalCode === null)
      this.process.kill();
    await this.closed;
    expect(this.output).not.toMatch(/AddressSanitizer|EXCEPTION CAUGHT/);
  }
}

for (const browserQuic of [false, true])
  test(`native QUIC discovery clients and a browser share a ${browserQuic ? "QUIC WebTransport" : "WebRTC fallback"} discovery session`, async ({
    page,
  }, info) => {
    test.setTimeout(120000);
    const browserLog: string[] = [];
    page.on("console", (message) => browserLog.push(message.text()));
    page.on("pageerror", (error) => browserLog.push(error.message));
    const root = info.outputPath("mixed-discovery");
    mkdirSync(root, { recursive: true });
    const binary =
      process.env["ASCII_CHAT_TEST_BINARY"] ||
      resolve("../../build/bin/ascii-chat.exe");
    const certificate = resolve(root, "cert.pem"),
      key = resolve(root, "key.pem"),
      identity = resolve(root, "host-id");
    const acdsIdentity = resolve(root, "acds-id");
    writeFileSync(
      acdsIdentity,
      Buffer.from(
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
        "hex",
      ),
    );
    execFileSync(
      process.env["OPENSSL"] || "openssl",
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
        certificate,
      ],
      { stdio: "ignore" },
    );
    execFileSync("ssh-keygen", [
      "-q",
      "-t",
      "ed25519",
      "-N",
      "",
      "-f",
      identity,
    ]);
    const tcp = await sharedPort(),
      ws = await port(),
      acdsTcp = await port(),
      acdsWs = await port();
    const common = [
      "--discovery-service",
      "127.0.0.1",
      "--discovery-service-port",
      String(acdsTcp),
    ];
    let secureVite: ViteDevServer | undefined;
    let pageOrigin = "http://localhost:3000";
    const wt = await port();
    if (browserQuic) {
      secureVite = await createVite({
        server: {
          host: "127.0.0.1",
          port: 0,
          strictPort: false,
          hmr: false,
          https: { key: readFileSync(key), cert: readFileSync(certificate) },
        },
      });
      await secureVite.listen();
      pageOrigin = `https://127.0.0.1:${(secureVite.httpServer!.address() as { port: number }).port}`;
      const hash = [
        ...createHash("sha256")
          .update(new X509Certificate(readFileSync(certificate)).raw)
          .digest(),
      ];
      await page.addInitScript(
        ({ hash, publicKey }) => {
          localStorage.setItem(
            "ascii-chat.crypto-settings.v1",
            JSON.stringify({
              verificationKeys: [
                {
                  id: "host",
                  name: "test host",
                  contents: publicKey,
                  target: "client-server",
                },
              ],
              activeVerificationKeyIds: { "client-server": "host" },
            }),
          );
          const Original = window.WebTransport;
          window.WebTransport = class extends Original {
            constructor(url: string | URL, options?: WebTransportOptions) {
              super(url, {
                ...options,
                serverCertificateHashes: [
                  { algorithm: "sha-256", value: new Uint8Array(hash) },
                ],
              });
            }
          };
          const OriginalRTC = window.RTCPeerConnection;
          window.RTCPeerConnection = class extends OriginalRTC {
            constructor(
              ...args: ConstructorParameters<typeof RTCPeerConnection>
            ) {
              super(...args);
              throw new Error("Browser must use discovered QUIC, not WebRTC");
            }
          };
        },
        { hash, publicKey: readFileSync(`${identity}.pub`, "utf8") },
      );
    }
    const processes: Native[] = [];
    const start = (args: string[]) => {
      const native = new Native(binary, root, args);
      processes.push(native);
      return native;
    };
    try {
      const acds = start([
        "discovery-service",
        "127.0.0.1",
        "--key",
        acdsIdentity,
        "--port",
        String(acdsTcp),
        "--websocket-port",
        String(acdsWs),
        ...(browserQuic
          ? ["--websocket-tls-cert", certificate, "--websocket-tls-key", key]
          : []),
        "--database",
        resolve(root, "acds.db"),
        "--status-screen=false",
      ]);
      await acds.ready(/Server accepting connections/);
      const host = start([
        "server",
        "0.0.0.0",
        "--port",
        String(tcp),
        "--websocket-port",
        String(ws),
        "--status-screen=false",
        ...common,
        "--discovery",
        "--webrtc",
        "--discovery-expose-ip",
        "--password",
        "network-test-password",
        "--key",
        identity,
        "--quic-bind-address",
        "127.0.0.1",
        "--quic-tls-cert",
        certificate,
        "--quic-tls-key",
        key,
        ...(browserQuic
          ? [
              "--webtransport-bind-address",
              "127.0.0.1",
              "--webtransport-port",
              String(wt),
              "--webtransport-origin",
              pageOrigin,
              "--webtransport-public-url",
              `https://127.0.0.1:${wt}/acip`,
            ]
          : []),
      ]);
      await host.ready(/Session created: [a-z]+-[a-z]+-[a-z]+/);
      const session = host.output.match(
        /Session created: ([a-z]+-[a-z]+-[a-z]+)/,
      )![1]!;
      const clients: Native[] = [];
      for (let i = 0; i < 2; ++i) {
        const client = start([
          session,
          ...common,
          "--discovery-quic",
          "--quic-ca-file",
          certificate,
          "--server-key",
          `${identity}.pub`,
          "--password",
          "network-test-password",
          "--test-pattern",
          "--volume",
          "0",
          "--audio=false",
          "--width",
          "40",
          "--height",
          "12",
          "--color-mode",
          "none",
          "--splash-screen=false",
        ]);
        clients.push(client);
        await client.ready(/using native QUIC/);
        await client.ready(/Server identity key verified against --server-key/);
        await client.ready(/Discovery decoded video: frames=1/);
      }
      await expect
        .poll(
          () =>
            new Set(
              [
                ...host.output.matchAll(
                  /Added new transport client (\S+).*type=5,/g,
                ),
              ].map((m) => m[1]),
            ).size,
          { timeout: 20000 },
        )
        .toBeGreaterThanOrEqual(2);
      await page.route("**/api/session-strings?**", (route) =>
        route.fulfill({
          headers: { "access-control-allow-origin": "*" },
          json: { strings: ["blue-mountain-tiger"] },
        }),
      );
      await page.goto(
        `${browserQuic ? pageOrigin : ""}/discovery?encoding=raw`,
      );
      await page.getByLabel("Session name", { exact: true }).fill(session);
      await page.getByText("Connection settings", { exact: true }).click();
      await page
        .getByLabel("Discovery service URL")
        .fill(`${browserQuic ? "wss" : "ws"}://127.0.0.1:${acdsWs}`);
      await page
        .getByLabel("Session password", { exact: true })
        .fill("network-test-password");
      await page.getByLabel("STUN URLs").fill("");
      await page.getByLabel("TURN URLs").fill("");
      await page
        .getByRole("button", { name: "Join session", exact: true })
        .click();
      await expect(
        page.getByRole("button", { name: "Disconnect", exact: true }),
      ).toBeVisible({ timeout: 40000 });
      if (browserQuic)
        await host.ready(
          /WebTransport.*authenticated|authenticated.*WebTransport|type=5,/i,
          40000,
        );
      else await host.ready(/Authenticated WebRTC connection adopted/, 40000);
      await expect
        .poll(
          () => {
            const browserId = browserQuic
              ? [
                  ...host.output.matchAll(
                    /Added new transport client (\S+).*type=5,/g,
                  ),
                ][2]?.[1]
              : host.output.match(
                  /Added new transport client (\S+).*type=3,/,
                )?.[1];
            return browserId
              ? host.output.includes(
                  `Server image ingress client=${browserId} `,
                )
              : false;
          },
          { timeout: 20000 },
        )
        .toBe(true);
      await expect
        .poll(
          () => page.evaluate(() => window.__clientFrameMetrics?.rendered ?? 0),
          { timeout: 25000 },
        )
        .toBeGreaterThan(10);
      expect(clients.every((client) => client.process.exitCode === null)).toBe(
        true,
      );
      if (browserQuic)
        expect(browserLog.join("\n")).toContain(
          "Server identity key verified against active browser key",
        );
      const during = host.output.length;
      await expect
        .poll(
          () =>
            (
              host.output
                .slice(during)
                .match(/Server image ingress client=/g) || []
            ).length,
          { timeout: 30000 },
        )
        .toBeGreaterThanOrEqual(36);
      expect(clients.every((client) => client.process.exitCode === null)).toBe(
        true,
      );
      await page
        .getByRole("button", { name: "Disconnect", exact: true })
        .click();
      await expect(
        page.getByRole("button", { name: "Join session", exact: true }),
      ).toBeVisible();
      const after = host.output.length;
      await expect
        .poll(
          () =>
            new Set(
              [
                ...host.output
                  .slice(after)
                  .matchAll(/Server image ingress client=(\S+)/g),
              ].map((m) => m[1]),
            ).size,
          { timeout: 10000 },
        )
        .toBeGreaterThanOrEqual(2);
      expect(clients.every((client) => client.process.exitCode === null)).toBe(
        true,
      );
      expect(acds.output).not.toMatch(/Handoff action=6/);
      expect(
        clients.every(
          (client) => !client.output.includes("Discovery state: MIGRATING"),
        ),
      ).toBe(true);
    } finally {
      await info.attach("browser.log", {
        body: Buffer.from(browserLog.join("\n")),
        contentType: "text/plain",
      });
      for (const [index, process] of processes.entries()) {
        writeFileSync(info.outputPath(`native-${index}.log`), process.output);
        await info.attach(`native-${index}.log`, {
          body: Buffer.from(process.output),
          contentType: "text/plain",
        });
      }
      for (const process of processes.reverse()) await process.stop();
      await secureVite?.close();
    }
  });
