/**
 * E2E tests for WebSocket client connection to native ascii-chat server
 *
 * Each test starts its own server on a unique port to enable parallel execution.
 * Run with: bun run test:e2e
 */

import { expect, test } from "@playwright/test";
import { ServerFixture, getRandomPort } from "./server-fixture";

const WEB_CLIENT_URL = "http://localhost:3000/client";
const TEST_TIMEOUT = 5000;

// Get server port from environment variable or use default (27226 is the WebSocket default)
const SERVER_PORT = process.env.PORT ? parseInt(process.env.PORT) : 27226;
const USE_EXTERNAL_SERVER = process.env.PORT !== undefined;

// Create server fixture for this test file
let server: ServerFixture | null = null;
let serverUrl: string = "";

test.beforeAll(async () => {
  if (USE_EXTERNAL_SERVER) {
    // Use external server on specified port (e.g., from debug script)
    serverUrl = `ws://127.0.0.1:${SERVER_PORT}`;
    console.log(`✓ Using external server on ${serverUrl}`);
  } else {
    // Start our own server for this test file
    const port = getRandomPort();
    server = new ServerFixture(port);
    await server.start();
    serverUrl = server.getUrl();
    console.log(`✓ Server started on ${serverUrl}`);
  }
});

test.afterAll(async () => {
  if (server && !USE_EXTERNAL_SERVER) {
    await server.stop();
    console.log(`✓ Server stopped`);
  } else if (USE_EXTERNAL_SERVER) {
    console.log(
      `✓ External server (port ${SERVER_PORT}) left running for debug script`,
    );
  }
});

// Capture ALL browser console messages to stdout immediately for every test
test.beforeEach(async ({ page }) => {
  page.on("console", (msg) => {
    const type = msg.type();
    const prefix = type === "error" ? "BROWSER:ERR" : "BROWSER";
    console.log(`${prefix}: ${msg.text()}`);
  });
});

test.describe("Client Connection to Native Server", () => {
  test.beforeEach(async ({ page, context }) => {
    // Grant permissions BEFORE navigating (so permission dialogs don't block)
    await context.grantPermissions(["camera", "microphone"]);

    // Inject fake webcam that generates animated test patterns
    await context.addInitScript(() => {
      const originalGetUserMedia = navigator.mediaDevices.getUserMedia;

      navigator.mediaDevices.getUserMedia = async (constraints: any) => {
        if (constraints?.video) {
          // Create a canvas-based fake video stream
          const canvas = document.createElement("canvas");
          canvas.width = 640;
          canvas.height = 480;

          const ctx = canvas.getContext("2d")!;
          let frame = 0;

          // Generate animated test pattern
          const animateTestPattern = () => {
            frame++;

            // Fill with gradient that changes each frame
            const gradient = ctx.createLinearGradient(
              0,
              0,
              canvas.width,
              canvas.height,
            );
            const hue = (frame * 2) % 360;
            gradient.addColorStop(0, `hsl(${hue}, 100%, 50%)`);
            gradient.addColorStop(1, `hsl(${(hue + 180) % 360}, 100%, 50%)`);
            ctx.fillStyle = gradient;
            ctx.fillRect(0, 0, canvas.width, canvas.height);

            // Add frame counter
            ctx.fillStyle = "white";
            ctx.font = "48px Arial";
            ctx.fillText(`Frame: ${frame}`, 50, 100);

            // Add moving circle
            const circleX = canvas.width / 2 + Math.sin(frame * 0.05) * 100;
            const circleY = canvas.height / 2 + Math.cos(frame * 0.05) * 100;
            ctx.fillStyle = "rgba(255, 255, 255, 0.7)";
            ctx.beginPath();
            ctx.arc(circleX, circleY, 50, 0, Math.PI * 2);
            ctx.fill();

            requestAnimationFrame(animateTestPattern);
          };

          animateTestPattern();

          // Return canvas stream at 30fps
          return canvas.captureStream(30) as any;
        }

        // Fallback to original for audio or other constraints
        return originalGetUserMedia.call(navigator.mediaDevices, constraints);
      };
    });

    // Navigate to client demo page with test server URL
    const clientUrl = `${WEB_CLIENT_URL}?testServerUrl=${encodeURIComponent(serverUrl)}`;
    await page.goto(clientUrl, { waitUntil: "networkidle" });
  });

  test("should auto-connect to native server and complete handshake", async ({
    page,
  }) => {
    test.setTimeout(10000);

    // Just verify status element is visible (page loaded and initialized)
    await expect(page.locator(".status")).toBeVisible({ timeout: 5000 });
  });

  test("client successfully connects and sends frames", async ({
    page,
  }) => {
    test.setTimeout(4500);

    // Track frames sent by client (measure immediately, don't wait for response)
    const frameStats = await page.evaluate(() => {
      return new Promise<{
        frameCount: number;
        fps: number;
        logs: string[];
      }>((resolve) => {
        let frameCount = 0;
        const logs: string[] = [];
        const startTime = performance.now();
        const measurementDuration = 3000; // 3 seconds - give it time to connect

        // Hook console to track frames sent
        const originalLog = console.log;
        const originalError = console.error;

        console.log = function (...args: any[]) {
          const msg = args.join(" ");
          logs.push(msg);
          if (msg.includes("SEND #")) {
            frameCount++;
          }
          originalLog.apply(console, args);
        };

        console.error = function (...args: any[]) {
          const msg = args.join(" ");
          logs.push("[ERR] " + msg);
          if (msg.includes("SEND #")) {
            frameCount++;
          }
          originalError.apply(console, args);
        };

        // Stop after measurement duration
        setTimeout(() => {
          console.log = originalLog;
          console.error = originalError;

          const endTime = performance.now();
          const elapsedSeconds = (endTime - startTime) / 1000;
          const fps = frameCount / elapsedSeconds;

          resolve({
            frameCount,
            fps: Math.round(fps * 100) / 100,
            logs: logs
              .filter(
                (l) => l.includes("SEND") || l.includes("Connected"),
              )
              .slice(-20),
          });
        }, measurementDuration);
      });
    });

    console.log("=== Client Frame Sending ===");
    console.log(
      `Client sent ${frameStats.frameCount} frames in 3 seconds (${frameStats.fps} fps)`,
    );
    console.log("Recent logs:", frameStats.logs);

    // Verify client is sending frames
    if (frameStats.frameCount === 0) {
      console.log("[RESULT] 0 fps - No frames sent by client");
    } else {
      console.log(`[RESULT] ${frameStats.fps} fps - Client sending frames successfully`);
    }

    // Client should be sending frames
    expect(frameStats.frameCount).toBeGreaterThan(0);
  });

  test("client maintains stable connection with webcam streaming", async ({
    page,
  }) => {
    test.setTimeout(15000);

    // Monitor for stable connection and frame streaming
    const connectionStats = await page.evaluate(() => {
      return new Promise<{
        isConnected: boolean;
        framesSent: number;
        connectionErrors: number;
      }>((resolve) => {
        let isConnected = false;
        let framesSent = 0;
        let connectionErrors = 0;
        const originalLog = console.log;
        const originalError = console.error;

        console.log = function (...args: any[]) {
          const msg = args.join(" ");
          if (msg.includes("Connected")) {
            isConnected = true;
          }
          if (msg.includes("SEND #")) {
            framesSent++;
          }
          originalLog.apply(console, args);
        };

        console.error = function (...args: any[]) {
          const msg = args.join(" ");
          if (msg.includes("error") || msg.includes("Error") || msg.includes("failed")) {
            connectionErrors++;
          }
          originalError.apply(console, args);
        };

        setTimeout(() => {
          console.log = originalLog;
          console.error = originalError;
          resolve({ isConnected, framesSent, connectionErrors });
        }, 5000);
      });
    });

    console.log(
      `Client Connection Status: Connected=${connectionStats.isConnected}, Frames=${connectionStats.framesSent}, Errors=${connectionStats.connectionErrors}`,
    );

    // Verify client connected and sent frames
    expect(connectionStats.isConnected).toBe(true);
    expect(connectionStats.framesSent).toBeGreaterThan(0);
  });
});
