import { spawn, type ChildProcess } from "node:child_process";
import * as dgram from "node:dgram";
import * as fs from "node:fs";
import * as net from "node:net";
import * as os from "node:os";
import * as path from "node:path";
import { expect, test } from "@playwright/test";
import {
  expectMeaningful60Fps,
  ServerFixture,
  waitForPort,
} from "./server-fixture";

test.use({ viewport: { width: 1920, height: 1080 } });

const turnUser = "ascii-test";
const turnPassword = "ascii-test-password";
const sessionPassword = "webrtc-fps-test-password";

function binaryPath(): string {
  return path.resolve(
    process.env["ASCII_CHAT_TEST_BINARY"] ||
      path.join(process.cwd(), "../../build/bin/ascii-chat.exe"),
  );
}

async function unusedTcpPort(): Promise<number> {
  const server = net.createServer();
  await new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  });
  const address = server.address();
  if (!address || typeof address === "string") throw new Error("No TCP port");
  await new Promise<void>((resolve) => server.close(() => resolve()));
  return address.port;
}

async function unusedUdpPort(): Promise<number> {
  const socket = dgram.createSocket("udp4");
  await new Promise<void>((resolve, reject) => {
    socket.once("error", reject);
    socket.bind(0, "127.0.0.1", resolve);
  });
  const address = socket.address();
  await new Promise<void>((resolve) => socket.close(resolve));
  return address.port;
}

function run(command: string, args: string[]): Promise<string> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { windowsHide: true });
    let output = "";
    child.stdout?.on("data", (chunk: Buffer) => (output += chunk.toString()));
    child.stderr?.on("data", (chunk: Buffer) => (output += chunk.toString()));
    child.once("error", reject);
    child.once("exit", (code) =>
      code === 0
        ? resolve(output.trim())
        : reject(new Error(`${command} exited ${code}: ${output}`)),
    );
  });
}

class LoggedProcess {
  readonly child: ChildProcess;
  private output = "";
  private readonly logPath: string;

  constructor(args: string[], logPath: string, env: NodeJS.ProcessEnv) {
    this.logPath = logPath;
    this.child = spawn(
      binaryPath(),
      ["--log-level", "debug", "--log-file", logPath, ...args],
      {
        cwd: process.cwd(),
        env,
        windowsHide: true,
        stdio: ["ignore", "pipe", "pipe"],
      },
    );
    for (const source of [this.child.stdout, this.child.stderr]) {
      source?.on("data", (chunk: Buffer) => {
        this.output += chunk.toString();
      });
    }
  }

  get logs(): string {
    let fileLogs = "";
    try {
      fileLogs = fs.readFileSync(this.logPath, "utf8");
    } catch {
      // The child may not have reached logging initialization yet.
    }
    return `${this.output}\n${fileLogs}`;
  }

  async waitFor(pattern: RegExp, timeoutMs: number): Promise<RegExpMatchArray> {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      const match = this.logs.match(pattern);
      if (match) return match;
      if (this.child.exitCode !== null)
        throw new Error(
          `ascii-chat exited ${this.child.exitCode}:\n${this.logs}`,
        );
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    throw new Error(`Timed out waiting for ${pattern}:\n${this.logs}`);
  }

  async stop(): Promise<void> {
    if (this.child.exitCode !== null) return;
    await new Promise<void>((resolve) => {
      const timer = setTimeout(() => {
        this.child.kill("SIGKILL");
        resolve();
      }, 5000);
      this.child.once("exit", () => {
        clearTimeout(timer);
        resolve();
      });
      this.child.kill("SIGTERM");
    });
  }
}

async function dockerNetworkIp(network: string): Promise<string> {
  const inspect = await run("docker", ["network", "inspect", network]);
  const parsed = JSON.parse(inspect) as Array<{
    IPAM: { Config: Array<{ Subnet: string }> };
  }>;
  const subnet = parsed[0]?.IPAM.Config[0]?.Subnet;
  if (!subnet) throw new Error(`Docker network ${network} has no subnet`);
  return `${subnet.split(".").slice(0, 3).join(".")}.10`;
}

test("Discovery WebRTC test2 sustains 55+ FPS on a 1920x1080 canvas through TURN", async ({
  page,
}, testInfo) => {
  test.setTimeout(120_000);
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "ascii-discovery-fps-"));
  const tcpPort = await unusedTcpPort();
  const serverWsPort = await unusedTcpPort();
  const acdsTcpPort = await unusedTcpPort();
  const acdsWsPort = await unusedTcpPort();
  const turnPort = await unusedUdpPort();
  const relayStart = 52000 + Math.floor(Math.random() * 12_000);
  const relayEnd = relayStart + 15;
  const network = `ascii-fps-${process.pid}-${Date.now()}`;
  const container = network;
  const subnetOctet = 20 + Math.floor(Math.random() * 200);
  const subnet = `10.253.${subnetOctet}.0/24`;
  const appData = path.join(root, "appdata");
  fs.mkdirSync(appData, { recursive: true });
  let turnStarted = false;
  let acds: LoggedProcess | undefined;
  let server: ServerFixture | undefined;

  try {
    await run("docker", ["network", "create", "--subnet", subnet, network]);
    const turnIp = await dockerNetworkIp(network);
    turnStarted = true;
    await run("docker", [
      "run",
      "--detach",
      "--name",
      container,
      "--network",
      network,
      "--ip",
      turnIp,
      "--publish",
      `127.0.0.1:${turnPort}:3478/udp`,
      "--publish",
      `127.0.0.1:${turnPort}:3478/tcp`,
      "--publish",
      `127.0.0.1:${relayStart}-${relayEnd}:${relayStart}-${relayEnd}/udp`,
      "coturn/coturn:latest",
      "--listening-ip=0.0.0.0",
      `--listening-port=3478`,
      `--relay-ip=${turnIp}`,
      `--external-ip=127.0.0.1/${turnIp}`,
      `--min-port=${relayStart}`,
      `--max-port=${relayEnd}`,
      `--user=${turnUser}:${turnPassword}`,
      "--realm=ascii-chat.test",
      "--lt-cred-mech",
      "--allowed-peer-ip=169.254.0.0-169.254.255.255",
      "--fingerprint",
      "--allow-loopback-peers",
      "--no-cli",
      "--no-tls",
      "--log-file=stdout",
      "--verbose",
    ]);
    const turnRunning = await run("docker", [
      "inspect",
      "--format",
      "{{.State.Running}}",
      container,
    ]);
    if (turnRunning !== "true")
      throw new Error(
        `Coturn container did not start:\n${await run("docker", ["logs", container])}`,
      );

    const env = { ...process.env, APPDATA: appData };
    acds = new LoggedProcess(
      [
        "discovery-service",
        "--port",
        String(acdsTcpPort),
        "--websocket-port",
        String(acdsWsPort),
        "--database",
        path.join(root, "acds.sqlite"),
        "--stun-servers",
        `stun:127.0.0.1:${turnPort}`,
        "--turn-servers",
        `turn:127.0.0.1:${turnPort}`,
      ],
      path.join(root, "acds.log"),
      env,
    );
    await waitForPort(acdsTcpPort, "127.0.0.1", { timeoutMs: 15_000 });
    await waitForPort(acdsWsPort, "127.0.0.1", { timeoutMs: 15_000 });

    server = new ServerFixture(
      tcpPort,
      [
        "server",
        "--port",
        String(tcpPort),
        "--websocket-port",
        String(serverWsPort),
        "--discovery",
        "--discovery-service",
        "127.0.0.1",
        "--discovery-service-port",
        String(acdsTcpPort),
        "--webrtc",
        "--stun-servers",
        "",
        "--turn-servers",
        `turn:127.0.0.1:${turnPort}`,
        "--turn-username",
        turnUser,
        "--turn-credential",
        turnPassword,
        "--password",
        sessionPassword,
      ],
      env,
    );
    await server.start();
    const sessionMatch = await server.waitForLog(
      /Session created: ([a-z]+-[a-z]+-[a-z]+)/i,
      30_000,
    );
    const session = sessionMatch[1]!;
    await page.bringToFront();
    const query = new URLSearchParams({
      session,
      signalingUrl: `ws://127.0.0.1:${acdsWsPort}`,
      stunUrls: `stun:127.0.0.1:${turnPort}`,
      turnUrls: `turn:127.0.0.1:${turnPort}`,
      test2: "",
    });
    await page.addInitScript(() => {
      const probe = {
        reads: 0,
        readMs: 0,
        lastReadWidth: 0,
        lastReadHeight: 0,
        dataChannelSends: 0,
        dataChannelSendMs: 0,
        dataChannelBytes: 0,
        maxBufferedAmount: 0,
      };
      (
        window as Window & { __webrtcPerfProbe?: typeof probe }
      ).__webrtcPerfProbe = probe;
      const getImageData = CanvasRenderingContext2D.prototype.getImageData;
      CanvasRenderingContext2D.prototype.getImageData = function (...args) {
        const start = performance.now();
        try {
          return getImageData.apply(this, args);
        } finally {
          probe.reads++;
          probe.readMs += performance.now() - start;
          probe.lastReadWidth = args[2];
          probe.lastReadHeight = args[3];
        }
      };
      const send = RTCDataChannel.prototype.send;
      RTCDataChannel.prototype.send = function (data) {
        const start = performance.now();
        try {
          return (
            send as (
              this: RTCDataChannel,
              value: string | Blob | ArrayBuffer | ArrayBufferView,
            ) => void
          ).call(this, data);
        } finally {
          probe.dataChannelSends++;
          probe.dataChannelSendMs += performance.now() - start;
          probe.dataChannelBytes +=
            typeof data === "string"
              ? new TextEncoder().encode(data).byteLength
              : data instanceof Blob
                ? data.size
                : data.byteLength;
          probe.maxBufferedAmount = Math.max(
            probe.maxBufferedAmount,
            this.bufferedAmount,
          );
        }
      };
    });
    await page.goto(`/discovery?${query.toString()}`);
    await page
      .getByLabel("Session password", { exact: true })
      .fill(sessionPassword);
    await page.getByText("Connection settings", { exact: true }).click();
    await page.getByLabel("Connection route").selectOption("all");
    await page.getByLabel("TURN username").fill(turnUser);
    await page.getByLabel("TURN password").fill(turnPassword);
    await page
      .getByRole("button", { name: "Join session", exact: true })
      .click();
    await expect(
      page.getByRole("button", { name: "Disconnect", exact: true }),
      {
        message: `Discovery WebRTC did not connect. Page: ${await page.locator("body").innerText()}`,
      },
    ).toBeVisible({ timeout: 45_000 });
    await page.getByText("Connection settings", { exact: true }).click();

    const canvas = page.locator("canvas.ascii-canvas");
    await expect(canvas).toBeVisible();
    const bounds = await canvas.boundingBox();
    const viewport = page.viewportSize();
    expect(bounds?.width).toBeGreaterThan(1500);
    expect(bounds?.height).toBeGreaterThan(viewport!.height * 0.55);
    await expect
      .poll(
        () => page.evaluate(() => window.__clientFrameMetrics?.rendered ?? 0),
        {
          timeout: 15_000,
        },
      )
      .toBeGreaterThan(60);

    const samples: Array<{
      frameFps: number;
      uniqueArtFps: number;
      repaintFps: number;
    }> = [];
    let previous = await page.evaluate(() => ({
      uniqueFrames: window.__clientFrameMetrics?.uniqueRendered ?? 0,
      repaints: window.__clientFrameMetrics?.rendered ?? 0,
      time: performance.now(),
    }));
    for (let index = 0; index < 10; index++) {
      await page.waitForTimeout(1_000);
      const current = await page.evaluate(() => ({
        uniqueFrames: window.__clientFrameMetrics?.uniqueRendered ?? 0,
        repaints: window.__clientFrameMetrics?.rendered ?? 0,
        time: performance.now(),
        ui: document.body.innerText.match(/FPS:\s*(\d+)\s*\/\s*60/),
      }));
      samples.push({
        frameFps: Number(current.ui?.[1] ?? 0),
        uniqueArtFps:
          ((current.uniqueFrames - previous.uniqueFrames) * 1000) /
          (current.time - previous.time),
        repaintFps:
          ((current.repaints - previous.repaints) * 1000) /
          (current.time - previous.time),
      });
      previous = current;
    }
    const goodWindows = samples.filter(({ frameFps }) => frameFps >= 55).length;
    const nearSixtyWindows = samples.filter(
      ({ frameFps }) => frameFps >= 59,
    ).length;
    console.log(
      `[discovery-webrtc-test2-fps] canvas=${Math.round(bounds!.width)}x${Math.round(bounds!.height)} ` +
        `60fps windows=${nearSixtyWindows}/10; 55+fps windows=${goodWindows}/10; ` +
        `received/unique-art/repaint=${samples.map(({ frameFps, uniqueArtFps, repaintFps }) => `${frameFps}/${uniqueArtFps.toFixed(0)}/${repaintFps.toFixed(0)}`).join(",")}`,
    );
    await expectMeaningful60Fps(page, "client", 5_000);
    console.log(
      `[discovery-webrtc-test2-fps] page=${(await page.locator("body").innerText()).replace(/\s+/g, " ").slice(0, 240)} ` +
        `serverCadence=${(server.logs.match(/Server (?:image ingress|video render|ASCII delivery) .* fps=[\d.]+/g) || []).slice(-15).join(" | ")} ` +
        `relay=${server.logs.match(/selected local candidate:.*typ relay/i)?.[0] ?? "none"}`,
    );
    const browserProbe = await page.evaluate(() => {
      const probe = (
        window as Window & {
          __webrtcPerfProbe?: Record<string, number>;
        }
      ).__webrtcPerfProbe;
      if (!probe) return null;
      const pixelCount = probe["lastReadWidth"]! * probe["lastReadHeight"]!;
      const rgba = new Uint8Array(pixelCount * 4);
      const output = new Uint8Array(pixelCount * 3);
      const startedAt = performance.now();
      for (let repeat = 0; repeat < 20; repeat++) {
        let dstIdx = 0;
        for (let index = 0; index < pixelCount; index++) {
          const srcIdx = index * 4;
          output[dstIdx++] = rgba[srcIdx]!;
          output[dstIdx++] = rgba[srcIdx + 1]!;
          output[dstIdx++] = rgba[srcIdx + 2]!;
        }
      }
      return {
        ...probe,
        rgbConversionMsPerFrame: (performance.now() - startedAt) / 20,
      };
    });
    console.log(
      "[discovery-webrtc-test2-fps] browserProbe=" +
        JSON.stringify(browserProbe),
    );
    expect(
      nearSixtyWindows,
      "received-frame FPS should reach 59+ for at least 8 of 10 samples",
    ).toBeGreaterThanOrEqual(8);
    expect(goodWindows).toBeGreaterThanOrEqual(9);
    expect(
      samples.filter(({ uniqueArtFps }) => uniqueArtFps >= 55).length,
      "changing ASCII frames should sustain 55+ FPS for at least 8 of 10 samples",
    ).toBeGreaterThanOrEqual(8);
    await expect(
      page.getByRole("button", { name: "Disconnect", exact: true }),
    ).toBeVisible();

    const ingress =
      server.logs.match(/Server image ingress .* fps=([\d.]+)/g) || [];
    expect(
      ingress.length,
      "server should receive moving test2 frames from the browser",
    ).toBeGreaterThan(0);
    expect(server.logs).toMatch(/selected local candidate:.*typ relay/i);
    const turnLogs = await run("docker", ["logs", container]);
    expect(turnLogs).toMatch(/allocation new/i);
  } catch (error) {
    await testInfo.attach("discovery-test-diagnostics.txt", {
      body: [
        `server process: ${server?.exitCode ?? "running"}`,
        "--- server log ---",
        server?.logs ?? "not started",
        "--- ACDS log ---",
        acds?.logs ?? "not started",
        "--- coturn log ---",
        turnStarted
          ? await run("docker", ["logs", container]).catch(String)
          : "not started",
        "--- browser page ---",
        await page.locator("body").innerText().catch(String),
      ].join("\n"),
      contentType: "text/plain",
    });
    throw error;
  } finally {
    await server?.stop();
    await acds?.stop();
    if (turnStarted) {
      await run("docker", ["stop", container]).catch(() => "");
      await run("docker", ["rm", "--force", container]).catch(() => "");
    }
    await run("docker", ["network", "rm", network]).catch(() => "");
    fs.rmSync(root, { recursive: true, force: true });
  }
});
