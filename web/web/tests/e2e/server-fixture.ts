import { spawn, ChildProcess } from "child_process";
import * as fs from "fs";
import * as path from "path";
import * as net from "net";
import { tmpdir } from "os";
import { expect, type Page } from "@playwright/test";

function getAsciiChatBinaryPath(): string {
  const configuredPath = process.env["ASCII_CHAT_TEST_BINARY"];
  if (configuredPath) return path.resolve(configuredPath);
  const binaryName = `ascii-chat${process.platform === "win32" ? ".exe" : ""}`;
  const candidates = [
    path.resolve(process.cwd(), `../../build/bin/${binaryName}`),
    path.resolve(
      process.cwd(),
      `../../build/native-quic-check/bin/${binaryName}`,
    ),
  ];
  return (
    candidates.find((candidate) => fs.existsSync(candidate)) ?? candidates[0]!
  );
}

/**
 * Server fixture for E2E tests - starts a dedicated server on a dynamic port
 */
export class ServerFixture {
  private process: ChildProcess | null = null;
  private port: number;
  private websocketPort: number;
  private logFile: string;
  private wsUrl: string;
  private logStream: fs.WriteStream | null = null;
  private output = "";

  constructor(
    port: number,
    private readonly serverArgs: string[] = [],
    private readonly env: NodeJS.ProcessEnv = process.env,
  ) {
    this.port = port;
    const websocketPortIndex = serverArgs.indexOf("--websocket-port");
    this.websocketPort =
      websocketPortIndex >= 0 && serverArgs[websocketPortIndex + 1]
        ? Number(serverArgs[websocketPortIndex + 1])
        : port + 1;
    this.wsUrl = `ws://127.0.0.1:${this.websocketPort}`;
    this.logFile = path.join(process.cwd(), `.server-${port}.log`);
  }

  async start(): Promise<void> {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.process?.kill();
        reject(
          new Error(`Server failed to start on port ${this.port} within 20s`),
        );
      }, 20000);

      this.logStream = fs.createWriteStream(this.logFile, { flags: "w" });

      // Path to binary relative to project root (tests run from web/ subdirectory)
      const binaryPath = getAsciiChatBinaryPath();

      // Use separate ports for TCP and WebSocket: TCP on basePort, WebSocket on basePort+1
      const tcpPort = this.port;
      const wsPort = this.websocketPort;
      const debugLogFile = path.join(
        tmpdir(),
        `ascii-chat-server-${this.port}.log`,
      );
      fs.writeFileSync(debugLogFile, "");
      let logLevel = this.env["ASCII_CHAT_TEST_LOG_LEVEL"] ?? "info";
      const modeArgs: string[] = [];
      for (let index = 0; index < this.serverArgs.length; index++) {
        const arg = this.serverArgs[index]!;
        if (arg === "server") continue;
        if (arg === "--log-level") {
          const value = this.serverArgs[++index];
          if (value) logLevel = value;
          continue;
        }
        modeArgs.push(arg);
      }

      this.process = spawn(
        binaryPath,
        [
          "--no-check-update",
          "--log-level",
          logLevel,
          "--log-file",
          debugLogFile,
          "server",
          "--port",
          tcpPort.toString(),
          "--websocket-port",
          wsPort.toString(),
          ...modeArgs,
        ],
        { env: this.env, windowsHide: true },
      );

      this.process.stdout?.on("data", (data) => {
        this.output += data.toString();
        if (
          this.logStream &&
          !this.logStream.destroyed &&
          !this.logStream.writableEnded
        ) {
          try {
            this.logStream.write(data);
          } catch (error) {
            console.error("Error writing to log stream:", error);
          }
        }
      });

      this.process.stderr?.on("data", (data) => {
        this.output += data.toString();
        if (
          this.logStream &&
          !this.logStream.destroyed &&
          !this.logStream.writableEnded
        ) {
          try {
            this.logStream.write(data);
          } catch (error) {
            console.error("Error writing to log stream:", error);
          }
        }
      });

      this.process.on("error", (err) => {
        clearTimeout(timeout);
        try {
          this.logStream?.end();
        } catch (error) {
          console.error("Error closing log stream:", error);
        }
        reject(new Error(`Failed to spawn server: ${err.message}`));
      });

      this.process.on("exit", (code) => {
        clearTimeout(timeout);
        try {
          this.logStream?.end();
        } catch (error) {
          console.error("Error closing log stream:", error);
        }
        if (code !== 0 && code !== null) {
          reject(new Error(`Server exited with code ${code}`));
        }
      });

      // Wait for the server's own readiness logs instead of opening probe
      // sockets. A probe is a real accepted connection for the async TCP
      // backend and can race the first authenticated peer.
      void Promise.all([
        this.waitForLog(
          new RegExp(`Listening on 127\\.0\\.0\\.1:${tcpPort} \\(IPv4\\)`),
          20_000,
        ),
        this.waitForLog(
          new RegExp(`WebSocket server starting event loop on port ${wsPort}`),
          20_000,
        ),
        this.waitForWebSocketHealth(wsPort, 20_000),
      ]).then(
        () => {
          clearTimeout(timeout);
          resolve();
        },
        (error: unknown) => {
          clearTimeout(timeout);
          reject(error);
        },
      );
    });
  }

  async stop(): Promise<void> {
    return new Promise((resolve) => {
      try {
        this.logStream?.end();
      } catch {
        // Ignore stream errors
      }

      if (!this.process) {
        resolve();
        return;
      }

      const timeout = setTimeout(() => {
        this.process?.kill("SIGKILL");
        resolve();
      }, 5000);

      this.process.once("exit", () => {
        clearTimeout(timeout);
        // Clean up log file
        try {
          fs.unlinkSync(this.logFile);
        } catch {
          // Ignore cleanup errors
        }
        resolve();
      });

      this.process.kill("SIGTERM");
    });
  }

  getUrl(): string {
    return this.wsUrl;
  }

  getPort(): number {
    return this.port;
  }

  get debugLogPath(): string {
    return path.join(tmpdir(), `ascii-chat-server-${this.port}.log`);
  }

  get exitCode(): number | null | undefined {
    return this.process?.exitCode;
  }

  get logs(): string {
    let fileLogs = "";
    try {
      fileLogs = fs.readFileSync(this.debugLogPath, "utf8");
    } catch {
      // The server may not have reached logging initialization.
    }
    return `${this.output}\n${fileLogs}`;
  }

  async waitForLog(
    pattern: RegExp,
    timeoutMs: number,
  ): Promise<RegExpMatchArray> {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      const match = this.logs.match(pattern);
      if (match) return match;
      if (
        this.process?.exitCode !== null &&
        this.process?.exitCode !== undefined
      )
        throw new Error(
          `ascii-chat exited ${this.process.exitCode}:\n${this.logs}`,
        );
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    throw new Error(`Timed out waiting for ${pattern}:\n${this.logs}`);
  }

  private async waitForWebSocketHealth(
    port: number,
    timeoutMs: number,
  ): Promise<void> {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      try {
        const response = await fetch(`http://127.0.0.1:${port}/health`);
        if (response.ok) return;
      } catch {
        // The listener may have logged readiness before accepting HTTP work.
      }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    throw new Error(`WebSocket health endpoint did not respond on ${port}`);
  }
}

/** A real CLI connection that completes before the browser joins. */
export class NativeClientFixture {
  private process: ChildProcess | null = null;
  private appDataDir: string | null = null;
  private logFile: string | null = null;

  constructor(
    private readonly serverPort: number,
    private readonly fps = 60,
    private readonly clientArgs: string[] = [],
  ) {}

  async start(): Promise<void> {
    const binaryPath = getAsciiChatBinaryPath();
    this.appDataDir = fs.mkdtempSync(
      path.join(process.cwd(), ".native-client-appdata-"),
    );
    this.logFile = path.join(this.appDataDir, "client.log");
    this.process = spawn(
      binaryPath,
      [
        "--no-check-update",
        "--log-file",
        this.logFile,
        "client",
        `127.0.0.1:${this.serverPort}`,
        ...this.clientArgs,
        "--test-pattern",
        "--fps",
        // Keep the native peer at the same source cadence expected by the
        // browser FPS assertions.
        String(this.fps),
      ],
      {
        env: {
          ...process.env,
          APPDATA: this.appDataDir,
          USERPROFILE: this.appDataDir,
          XDG_CONFIG_HOME: this.appDataDir,
          ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK: "1",
        },
      },
    );
    const childProcess = this.process;
    const logFile = this.logFile;
    if (!childProcess || !logFile)
      throw new Error("Native client process did not start");
    const deadline = Date.now() + 15_000;
    while (Date.now() < deadline) {
      if (childProcess.exitCode !== null) {
        const details = fs.readFileSync(logFile, "utf8").slice(-8000);
        throw new Error(
          `Native client exited before connecting (code ${childProcess.exitCode}):\n${details}`,
        );
      }
      let details = "";
      try {
        details = fs.readFileSync(logFile, "utf8");
      } catch {
        // Logging may not have initialized yet.
      }
      if (details.includes("CLIENT_CONNECTION_RUNNING")) return;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    const details = fs.readFileSync(logFile, "utf8").slice(-8000);
    throw new Error(
      `Native client did not complete its connection:\n${details}`,
    );
  }

  async stop(): Promise<void> {
    const process = this.process;
    this.process = null;
    if (!process || process.exitCode !== null) return;

    await new Promise<void>((resolve) => {
      const timeout = setTimeout(() => {
        process.kill("SIGKILL");
        resolve();
      }, 5000);
      process.once("exit", () => {
        clearTimeout(timeout);
        resolve();
      });
      process.kill("SIGTERM");
    });
    if (this.appDataDir) {
      fs.rmSync(this.appDataDir, { force: true, recursive: true });
      this.appDataDir = null;
      this.logFile = null;
    }
  }
}

/** Wait until a TCP port accepts connections, or fail after the deadline. */
export async function waitForPort(
  port: number,
  host: string,
  options: { timeoutMs?: number; pollIntervalMs?: number } = {},
): Promise<void> {
  const deadline = Date.now() + (options.timeoutMs ?? 20_000);
  const pollIntervalMs = options.pollIntervalMs ?? 100;

  while (Date.now() < deadline) {
    const listening = await new Promise<boolean>((resolve) => {
      const socket = net.createConnection({ host, port });
      socket.once("connect", () => {
        socket.destroy();
        resolve(true);
      });
      socket.once("error", () => resolve(false));
      socket.setTimeout(500, () => {
        socket.destroy();
        resolve(false);
      });
    });
    if (listening) return;
    await new Promise((resolve) => setTimeout(resolve, pollIntervalMs));
  }

  throw new Error(`No TCP listener started on ${host}:${port}`);
}

/**
 * Get a free port number
 */
export function getRandomPort(excludedPorts: readonly number[] = []): number {
  // Use ports in the range 27230-27300 for test servers
  const available = Array.from(
    { length: 70 },
    (_, index) => 27230 + index,
  ).filter((port) => !excludedPorts.includes(port));
  if (!available.length) throw new Error("No test ports available");
  return available[Math.floor(Math.random() * available.length)]!;
}

/**
 * Assert that the page presents near-refresh-rate ASCII frames whose visible
 * glyph content materially changes. Hash/repaint counters alone can count
 * repeated frames or one-character noise, so sample the actual rendered frame.
 */
export async function expectMeaningful60Fps(
  page: Page,
  source: "client" | "mirror" = "client",
  durationMs = 5_000,
): Promise<void> {
  const sample = await page.evaluate(
    async ({ source, durationMs }) => {
      const readFrame = () => {
        const win = window as Window & {
          __clientFrameMetrics?: { lastRenderedFrame?: string };
          __lastAnsiFrame?: string;
          __lastAnsiFrameCount?: number;
        };
        return source === "client"
          ? (win.__clientFrameMetrics?.lastRenderedFrame ?? "")
          : (win.__lastAnsiFrame ?? "");
      };
      const visualCells = (frame: string) => {
        const cells: string[] = [];
        let foreground = "";
        let background = "";
        let attributes = "";
        const reset = () => {
          foreground = "";
          background = "";
          attributes = "";
        };
        const applySgr = (parameters: string) => {
          const values =
            parameters === ""
              ? [0]
              : parameters.split(";").map((value) => Number(value || 0));
          for (let index = 0; index < values.length; index++) {
            const value = values[index]!;
            if (value === 0) reset();
            else if ([1, 2, 3, 4, 5, 7, 8, 9].includes(value)) {
              attributes = `${attributes},${value}`;
            } else if (value === 22) {
              attributes = attributes.replace(/,(1|2)/g, "");
            } else if (value >= 23 && value <= 29) {
              attributes = attributes.replace(new RegExp(`,${value - 22}`), "");
            } else if (value === 39) foreground = "";
            else if (value === 49) background = "";
            else if (
              (value === 38 || value === 48) &&
              values[index + 1] === 2
            ) {
              const color = values.slice(index, index + 5).join(";");
              if (value === 38) foreground = color;
              else background = color;
              index += 4;
            } else if (
              (value === 38 || value === 48) &&
              values[index + 1] === 5
            ) {
              const color = values.slice(index, index + 3).join(";");
              if (value === 38) foreground = color;
              else background = color;
              index += 2;
            } else if (
              (value >= 30 && value <= 37) ||
              (value >= 90 && value <= 97)
            ) {
              foreground = String(value);
            } else if (
              (value >= 40 && value <= 47) ||
              (value >= 100 && value <= 107)
            ) {
              background = String(value);
            }
          }
        };
        const tokenPattern = /\u001b\[([0-?]*)([ -/]*)([@-~])|([\s\S])/g;
        for (const match of frame.matchAll(tokenPattern)) {
          if (match[4] !== undefined) {
            if (match[4] !== "\r") {
              cells.push(
                `${attributes}|${foreground}|${background}|${match[4]}`,
              );
            }
          } else if (match[3] === "m") {
            applySgr(match[1]!);
          }
        }
        return cells;
      };
      const materiallyDifferent = (left: string, right: string) => {
        const a = visualCells(left);
        const b = visualCells(right);
        const length = Math.max(a.length, b.length);
        if (length === 0) return false;
        let changed = 0;
        for (let index = 0; index < length; index++) {
          if (a[index] !== b[index]) changed++;
        }
        // Compare full terminal cells (glyph plus effective ANSI style), not
        // raster pixels. A changed cell covers a glyph-sized area and cannot
        // pass from a one-pixel difference.
        return changed >= 1;
      };

      const startedAt = performance.now();
      let previous = readFrame();
      const buckets: number[] = [];
      let bucketStartedAt = startedAt;
      let bucketFrames = 0;
      return await new Promise<{
        fps: number[];
        meaningfulChanges: number;
        pipelineFps: Array<{
          received: number;
          changedReceived: number;
          rendered: number;
        }>;
      }>((resolve) => {
        let meaningfulChanges = 0;
        const pipelineFps: Array<{
          received: number;
          changedReceived: number;
          rendered: number;
        }> = [];
        const pipelineSnapshot = () => {
          const win = window as Window & { __lastAnsiFrameCount?: number };
          const metrics = window.__clientFrameMetrics;
          if (source === "mirror") {
            const rendered = win.__lastAnsiFrameCount ?? 0;
            return { received: rendered, changedReceived: rendered, rendered };
          }
          return {
            received: metrics?.received ?? 0,
            changedReceived: metrics?.changedReceived ?? 0,
            rendered: metrics?.rendered ?? 0,
          };
        };
        let previousPipeline = pipelineSnapshot();
        const tick = (now: number) => {
          const current = readFrame();
          if (materiallyDifferent(previous, current)) {
            meaningfulChanges++;
            bucketFrames++;
            previous = current;
          }
          if (now - bucketStartedAt >= 1_000) {
            const elapsed = now - bucketStartedAt;
            buckets.push((bucketFrames * 1_000) / elapsed);
            const currentPipeline = pipelineSnapshot();
            pipelineFps.push({
              received:
                ((currentPipeline.received - previousPipeline.received) *
                  1_000) /
                elapsed,
              changedReceived:
                ((currentPipeline.changedReceived -
                  previousPipeline.changedReceived) *
                  1_000) /
                elapsed,
              rendered:
                ((currentPipeline.rendered - previousPipeline.rendered) *
                  1_000) /
                elapsed,
            });
            previousPipeline = currentPipeline;
            bucketFrames = 0;
            bucketStartedAt = now;
          }
          if (now - startedAt >= durationMs) {
            if (bucketFrames > 0)
              buckets.push((bucketFrames * 1_000) / (now - bucketStartedAt));
            resolve({ fps: buckets, meaningfulChanges, pipelineFps });
            return;
          }
          requestAnimationFrame(tick);
        };
        requestAnimationFrame(tick);
      });
    },
    { source, durationMs },
  );

  expect(
    sample.meaningfulChanges,
    `ASCII content must change visibly during the sample; observed ${sample.meaningfulChanges} changes, visual FPS ${sample.fps.join(", ")}, pipeline FPS ${JSON.stringify(sample.pipelineFps)}`,
  ).toBeGreaterThanOrEqual(Math.floor((durationMs / 1_000) * 20));
  expect(
    sample.fps.length,
    `Expected one FPS sample per second; got ${sample.fps.join(", ")}`,
  ).toBeGreaterThanOrEqual(Math.floor(durationMs / 1_000));
  const average =
    sample.fps.reduce((total, fps) => total + fps, 0) / sample.fps.length;
  expect(
    average,
    `Expected recurring visible ASCII changes, got ${sample.fps.join(", ")}`,
  ).toBeGreaterThanOrEqual(20);
  expect(
    sample.fps.filter((fps) => fps >= 20).length,
    `Every interval should contain visible ASCII changes, got ${sample.fps.join(", ")}`,
  ).toBeGreaterThanOrEqual(Math.floor(sample.fps.length * 0.8));
  const pipelineSamples = sample.pipelineFps.filter(
    (sample) => sample.received > 0 || sample.rendered > 0,
  );
  expect(
    pipelineSamples.length,
    `Expected pipeline FPS samples, got ${JSON.stringify(sample.pipelineFps)}`,
  ).toBeGreaterThanOrEqual(Math.max(1, Math.floor(durationMs / 1_000) - 1));
  expect(
    pipelineSamples.every(
      (sample) => sample.received >= 45 && sample.rendered >= 45,
    ),
    `Expected near-60Hz receive/render cadence, got ${JSON.stringify(sample.pipelineFps)}`,
  ).toBe(true);
}
