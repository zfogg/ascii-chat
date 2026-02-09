import { spawn, ChildProcess } from "child_process";
import * as fs from "fs";
import * as path from "path";
import * as net from "net";

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
    this.wsUrl = `ws://localhost:${port + 1}`;
    this.logFile = path.join(process.cwd(), `.server-${port}.log`);
  }

  async start(): Promise<void> {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.process?.kill();
        reject(new Error(`Server failed to start on port ${this.port} within 20s`));
      }, 20000);

      this.logStream = fs.createWriteStream(this.logFile, { flags: "a" });

      // Path to binary relative to project root (tests run from web/ subdirectory)
      const binaryPath = path.join(process.cwd(), "../../build/bin/ascii-chat");

      // Use separate ports for TCP and WebSocket: TCP on basePort, WebSocket on basePort+1
      const tcpPort = this.port;
      const wsPort = this.port + 1;
      const debugLogFile = path.join(process.cwd(), `.server-debug-${this.port}.log`);

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
        if (this.logStream && !this.logStream.destroyed && !this.logStream.writableEnded) {
          try {
            this.logStream.write(data);
          } catch (e) {
            // Stream closed, ignore
          }
        }
      });

      this.process.stderr?.on("data", (data) => {
        if (this.logStream && !this.logStream.destroyed && !this.logStream.writableEnded) {
          try {
            this.logStream.write(data);
          } catch (e) {
            // Stream closed, ignore
          }
        }
      });

      this.process.on("error", (err) => {
        clearTimeout(timeout);
        try {
          this.logStream?.end();
        } catch (e) {
          // Ignore stream errors
        }
        reject(new Error(`Failed to spawn server: ${err.message}`));
      });

      this.process.on("exit", (code) => {
        clearTimeout(timeout);
        try {
          this.logStream?.end();
        } catch (e) {
          // Ignore stream errors
        }
        if (code !== 0 && code !== null) {
          reject(new Error(`Server exited with code ${code}`));
        }
      });

      // Poll for port availability to detect startup
      const pollInterval = setInterval(async () => {
        const isListening = await isPortListening(tcpPort);
        if (isListening) {
          clearInterval(pollInterval);
          clearTimeout(timeout);
          resolve();
        }
      }, 100);
    });
  }

  async stop(): Promise<void> {
    return new Promise((resolve) => {
      try {
        this.logStream?.end();
      } catch (e) {
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

/**
 * Check if a port is listening
 */
function isPortListening(port: number, host: string = "127.0.0.1"): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = net.createConnection({ port, host });
    socket.on("connect", () => {
      socket.destroy();
      resolve(true);
    });
    socket.on("error", () => {
      resolve(false);
    });
    socket.setTimeout(500);
    socket.on("timeout", () => {
      socket.destroy();
      resolve(false);
    });
  });
}

/**
 * Get a free port number
 */
export function getRandomPort(): number {
  // Use ports in the range 27230-27300 for test servers
  return 27230 + Math.floor(Math.random() * 70);
}
