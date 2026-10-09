import { spawn, ChildProcess } from "child_process";
import * as fs from "fs";
import * as path from "path";
import * as net from "net";
import { expect, type Page } from "@playwright/test";

function getAsciiChatBinaryPath(): string {
  const configuredPath = process.env["ASCII_CHAT_TEST_BINARY"];
  if (configuredPath) return path.resolve(configuredPath);
  return path.resolve(process.cwd(), "../../build/bin/ascii-chat");
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

      this.logStream = fs.createWriteStream(this.logFile, { flags: "a" });

      // Path to binary relative to project root (tests run from web/ subdirectory)
      const binaryPath = getAsciiChatBinaryPath();

      // Use separate ports for TCP and WebSocket: TCP on basePort, WebSocket on basePort+1
      const tcpPort = this.port;
      const wsPort = this.websocketPort;
      const debugLogFile = `/tmp/ascii-chat-server-${this.port}.log`;
      let logLevel = "info";
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

      // Wait for both listeners because WebSocket initialization can lag TCP.
      void Promise.all([
        waitForPort(tcpPort, "127.0.0.1"),
        waitForPort(wsPort, "127.0.0.1"),
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

  get exitCode(): number | null | undefined {
    return this.process?.exitCode;
  }

  get logs(): string {
    let fileLogs = "";
    try {
      fileLogs = fs.readFileSync(
        `/tmp/ascii-chat-server-${this.port}.log`,
        "utf8",
      );
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
}

/** A real CLI connection that completes before the browser joins. */
export class NativeClientFixture {
  private process: ChildProcess | null = null;
  private appDataDir: string | null = null;

  constructor(
    private readonly serverPort: number,
    private readonly fps = 60,
  ) {}

  async start(): Promise<void> {
    const binaryPath = getAsciiChatBinaryPath();
    this.appDataDir = fs.mkdtempSync(
      path.join(process.cwd(), ".native-client-appdata-"),
    );
    this.process = spawn(
      binaryPath,
      [
        "--no-check-update",
        "client",
        `127.0.0.1:${this.serverPort}`,
        "--test-pattern",
        "--fps",
        // Keep the native peer at the same source cadence expected by the
        // browser FPS assertions.
        String(this.fps),
      ],
      {
        env: { ...process.env, APPDATA: this.appDataDir },
      },
    );
    await new Promise<void>((resolve, reject) => {
      const process = this.process;
      if (!process) {
        reject(new Error("Native client process did not start"));
        return;
      }
      const timer = setTimeout(resolve, 500);
      process.once("exit", (code) => {
        clearTimeout(timer);
        reject(
          new Error(`Native client exited before connecting (code ${code})`),
        );
      });
    });
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
export function getRandomPort(): number {
  // Use ports in the range 27230-27300 for test servers
  return 27230 + Math.floor(Math.random() * 70);
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
) {
  const sample = await page.evaluate(
    async ({ source, durationMs }) => {
      const readFrame = () => {
        const win = window as Window & {
          __clientFrameMetrics?: { lastRenderedFrame?: string };
          __lastAnsiFrame?: string;
        };
        return source === "client"
          ? (win.__clientFrameMetrics?.lastRenderedFrame ?? "")
          : (win.__lastAnsiFrame ?? "");
      };
      function* visualCells(frame: string) {
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
              yield `${attributes}|${foreground}|${background}|${match[4]}`;
            }
          } else if (match[3] === "m") {
            applySgr(match[1]!);
          }
        }
      }
      const materiallyDifferent = (left: string, right: string) => {
        if (left === right) return false;
        const a = visualCells(left);
        const b = visualCells(right);
        // Compare glyph plus effective ANSI style; stop at the first changed
        // terminal cell, without allocating arrays for the whole screen.
        for (;;) {
          const nextA = a.next();
          const nextB = b.next();
          if (nextA.done || nextB.done) return nextA.done !== nextB.done;
          if (nextA.value !== nextB.value) return true;
        }
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
        let previousPipeline = (() => {
          const metrics = window.__clientFrameMetrics;
          return {
            received: metrics?.received ?? 0,
            changedReceived: metrics?.changedReceived ?? 0,
            rendered: metrics?.rendered ?? 0,
          };
        })();
        // Capture references on the animation clock; parse terminal cells only
        // after sampling so the assertion itself cannot stall frame production.
        const frames: Array<{
          now: number;
          frame: string;
          metrics: {
            received: number;
            changedReceived: number;
            rendered: number;
          };
        }> = [];
        const processSample = ({
          now,
          frame: current,
          metrics,
        }: (typeof frames)[number]) => {
          if (materiallyDifferent(previous, current)) {
            meaningfulChanges++;
            bucketFrames++;
            previous = current;
          }
          if (now - bucketStartedAt >= 1_000) {
            const elapsed = now - bucketStartedAt;
            buckets.push((bucketFrames * 1_000) / elapsed);
            const currentPipeline = {
              received: metrics?.received ?? 0,
              changedReceived: metrics?.changedReceived ?? 0,
              rendered: metrics?.rendered ?? 0,
            };
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
        };
        const tick = (now: number) => {
          const metrics = window.__clientFrameMetrics;
          frames.push({
            now,
            frame: readFrame(),
            metrics: {
              received: metrics?.received ?? 0,
              changedReceived: metrics?.changedReceived ?? 0,
              rendered: metrics?.rendered ?? 0,
            },
          });
          if (now - startedAt >= durationMs) {
            for (const frame of frames) processSample(frame);
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
    `ASCII content must change materially at near-60 FPS; observed ${sample.meaningfulChanges} changes, visual FPS ${sample.fps.join(", ")}, pipeline FPS ${JSON.stringify(sample.pipelineFps)}`,
  ).toBeGreaterThanOrEqual(Math.floor((durationMs / 1_000) * 55));
  expect(
    sample.fps.length,
    `Expected one FPS sample per second; got ${sample.fps.join(", ")}`,
  ).toBeGreaterThanOrEqual(Math.floor(durationMs / 1_000));
  const average =
    sample.fps.reduce((total, fps) => total + fps, 0) / sample.fps.length;
  expect(
    average,
    `Expected near-60 FPS meaningful frames, got ${sample.fps.join(", ")}`,
  ).toBeGreaterThanOrEqual(55);
  expect(
    sample.fps.filter((fps) => fps >= 55).length,
    `Every interval should stay near 60 FPS, got ${sample.fps.join(", ")}`,
  ).toBeGreaterThanOrEqual(Math.floor(sample.fps.length * 0.8));
  return sample;
}
