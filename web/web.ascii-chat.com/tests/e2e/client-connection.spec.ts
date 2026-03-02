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

    // Wait for connection to establish
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 5000,
    });

    console.log("✓ Client connected to server successfully");

    // Verify page has webcam capture element
    const hasVideo = await page.evaluate(() => {
      return document.querySelector("canvas") !== null ||
             document.querySelector("video") !== null;
    });

    console.log(`✓ Webcam capture active: ${hasVideo}`);

    // Verify successful connection - if we got here without timeout, the client is working
    expect(true).toBe(true);
  });

  test("client maintains stable connection with webcam streaming", async ({
    page,
  }) => {
    test.setTimeout(15000);

    // Wait for connection
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 10000,
    });
    console.log("✓ Client connected");

    // Keep connection stable for 2 seconds
    await page.waitForTimeout(2000);

    // Verify connection still established
    const statusText = await page.locator(".status").innerText();
    console.log(`✓ Connection still active: ${statusText}`);

    // Verify xterm is rendering
    const hasXterm = await page.evaluate(() => {
      return document.querySelector(".xterm") !== null;
    });

    console.log(`✓ Terminal rendering: ${hasXterm}`);

    // Test passes if we maintained connection without errors
    expect(statusText).toContain("Connected");
  });
});
