import { spawn, ChildProcess } from "child_process";
import * as fs from "fs";
import * as path from "path";
import * as net from "net";

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
  private logFile: string;
  private wsUrl: string;
  private logStream: fs.WriteStream | null = null;

  constructor(port: number) {
    this.port = port;
    // WebSocket port is TCP port + 1
    this.wsUrl = `ws://127.0.0.1:${port + 1}`;
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
      const wsPort = this.port + 1;
      const debugLogFile = `/tmp/ascii-chat-server-${this.port}.log`;

      this.process = spawn(binaryPath, [
        "--log-level",
        "debug",
        "--log-file",
        debugLogFile,
        "server",
        "--port",
        tcpPort.toString(),
        "--websocket-port",
        wsPort.toString(),
      ]);

      this.process.stdout?.on("data", (data) => {
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
}

/** A real CLI connection that completes before the browser joins. */
export class NativeClientFixture {
  private process: ChildProcess | null = null;
  private appDataDir: string | null = null;

  constructor(private readonly serverPort: number) {}

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
        // The browser is the 60 FPS subject under test. Keep the native peer
        // active but inexpensive so it verifies multi-client composition
        // without consuming the server's entire render budget.
        "20",
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
