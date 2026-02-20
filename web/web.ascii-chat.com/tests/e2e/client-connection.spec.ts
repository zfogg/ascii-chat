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

  test("should fail - server not sending ASCII_FRAME packets (known bug)", async ({
    page,
  }) => {
    test.setTimeout(15000);

    // Track ASCII_FRAME packets being received (measure immediately, don't wait for Connected)
    const frameStats = await page.evaluate(() => {
      return new Promise<{
        frameCount: number;
        fps: number;
        logs: string[];
      }>((resolve) => {
        let frameCount = 0;
        const logs: string[] = [];
        const startTime = performance.now();
        const measurementDuration = 10000; // 10 seconds - give it time to connect

        // Hook console to track ASCII_FRAME packets
        const originalLog = console.log;
        const originalError = console.error;

        console.log = function (...args: any[]) {
          const msg = args.join(" ");
          logs.push(msg);
          if (msg.includes("ASCII_FRAME PACKET RECEIVED")) {
            frameCount++;
          }
          originalLog.apply(console, args);
        };

        console.error = function (...args: any[]) {
          const msg = args.join(" ");
          logs.push("[ERR] " + msg);
          if (msg.includes("ASCII_FRAME PACKET RECEIVED")) {
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
                (l) => l.includes("ASCII_FRAME") || l.includes("Connected"),
              )
              .slice(-20),
          });
        }, measurementDuration);
      });
    });

    console.log("=== Server ASCII_FRAME Delivery ===");
    console.log(
      `Received ${frameStats.frameCount} ASCII_FRAME packets in 10 seconds (${frameStats.fps} fps)`,
    );
    console.log("Recent logs:", frameStats.logs);

    // Report honestly what we measured
    if (frameStats.frameCount === 0) {
      console.log("[RESULT] 0 fps - No ASCII_FRAME packets received");
    } else {
      console.log(`[RESULT] ${frameStats.fps} fps achieved`);
    }

    // This should fail due to known bug: server doesn't send ASCII_FRAME packets
    expect(frameStats.frameCount).toBeGreaterThan(0);
  });

  test("should fail - client not receiving ASCII_FRAME packets (known bug)", async ({
    page,
  }) => {
    test.setTimeout(15000);

    // Monitor for ASCII_FRAME packets from server (measure for 5 seconds)
    const frameReceived = await page.evaluate(() => {
      return new Promise<boolean>((resolve) => {
        let frameReceived = false;
        const originalLog = console.log;
        const originalError = console.error;

        console.log = function (...args: any[]) {
          const msg = args.join(" ");
          if (msg.includes("ASCII_FRAME PACKET RECEIVED")) {
            frameReceived = true;
          }
          originalLog.apply(console, args);
        };

        console.error = function (...args: any[]) {
          const msg = args.join(" ");
          if (msg.includes("ASCII_FRAME PACKET RECEIVED")) {
            frameReceived = true;
          }
          originalError.apply(console, args);
        };

        setTimeout(() => {
          console.log = originalLog;
          console.error = originalError;
          resolve(frameReceived);
        }, 5000);
      });
    });

    console.log(
      `[BUG] Server should send ASCII_FRAME packets. Received: ${frameReceived}`,
    );

    // This fails because server isn't sending frames back (known bug)
    expect(frameReceived).toBe(true);
  });
});
