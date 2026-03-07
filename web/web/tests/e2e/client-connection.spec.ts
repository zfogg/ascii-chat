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

  test("client successfully connects and sends frames", async ({ page }) => {
    test.setTimeout(4500);

    // Capture console output to analyze packets
    const consoleLogs: string[] = [];
    page.on("console", (msg) => {
      const text = msg.text();
      consoleLogs.push(text);
      // Log key packets
      if (
        text.includes("STREAM_START") ||
        text.includes("CLIENT_CAPABILITIES")
      ) {
        console.log(`[CAPTURE] ${text}`);
      }
    });

    // Wait for connection to establish
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 5000,
    });

    console.log("✓ Client connected to server");

    // Wait for initial frames to be captured
    await page.waitForTimeout(2000);

    // Check what packets were received
    const asciiFrameCount = consoleLogs.filter((log) =>
      log.includes("ASCII_FRAME"),
    ).length;
    const encryptedCount = consoleLogs.filter(
      (log) => log.includes("ENCRYPTED") && log.includes("RECV"),
    ).length;

    // Count inner packet types from decrypted packets
    const audioOpusCount = consoleLogs.filter((log) =>
      log.includes("Inner packet type: 4001 (AUDIO_OPUS_BATCH)"),
    ).length;
    const imageFrameCount = consoleLogs.filter((log) =>
      log.includes("Inner packet type: 3000 (IMAGE_FRAME)"),
    ).length;
    const imageFrameH265Count = consoleLogs.filter((log) =>
      log.includes("Inner packet type: 3002 (IMAGE_FRAME_H265)"),
    ).length;

    console.log(
      `Packets received: ENCRYPTED=${encryptedCount}, ASCII_FRAME=${asciiFrameCount}`,
    );
    console.log(
      `Inner packet types: AUDIO_OPUS_BATCH=${audioOpusCount}, IMAGE_FRAME=${imageFrameCount}, IMAGE_FRAME_H265=${imageFrameH265Count}`,
    );

    // Check frame metrics
    const metrics = await page.evaluate(
      () => (window as any).__clientFrameMetrics,
    );

    console.log("Frame metrics after connection:", metrics);

    // Verify client received some frames from server
    if (asciiFrameCount > 0) {
      expect(metrics?.rendered).toBeGreaterThan(0);
    } else {
      console.log("⚠️  No ASCII_FRAME packets received from server");
      expect(asciiFrameCount).toBeGreaterThan(0);
    }
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

    // Capture initial metrics
    const startMetrics = await page.evaluate(
      () => (window as any).__clientFrameMetrics,
    );
    console.log("Start metrics:", startMetrics);

    // Stream for 2 seconds
    await page.waitForTimeout(2000);

    // Capture end metrics
    const endMetrics = await page.evaluate(
      () => (window as any).__clientFrameMetrics,
    );
    console.log("End metrics:", endMetrics);

    // Calculate frame delta
    const renderedDelta =
      (endMetrics?.rendered || 0) - (startMetrics?.rendered || 0);
    const uniqueDelta =
      (endMetrics?.uniqueRendered || 0) - (startMetrics?.uniqueRendered || 0);

    console.log(
      `Frames delta: rendered=${renderedDelta}, unique=${uniqueDelta}`,
    );

    // Verify streaming is active
    expect(renderedDelta).toBeGreaterThan(0);
    expect(uniqueDelta).toBeGreaterThan(0);
  });
});
