/**
 * E2E performance test for Client mode rendering
 *
 * Verifies that client mode can connect, render, and maintain FPS > 15.
 * Run with: bun run test:e2e -- tests/e2e/performance-check.spec.ts
 */

import { test, expect } from "@playwright/test";
import { ServerFixture, getRandomPort } from "./server-fixture";

const WEB_CLIENT_URL = "http://localhost:3000/client";
const TEST_TIMEOUT = 30000;

// Use fake media device for E2E tests (real hardware may not be available)
test.use({
  launchOptions: {
    args: [
      "--use-fake-device-for-media-stream",
      "--use-fake-ui-for-media-stream",
    ],
  },
});

test.beforeEach(async ({ page }) => {
  page.on("console", (msg) => console.log(`BROWSER: ${msg.text()}`));
});

test("client mode: can initialize and connect", async ({ page, context }) => {
  test.setTimeout(TEST_TIMEOUT);

  // Start server for this test
  const port = getRandomPort();
  const server = new ServerFixture(port);
  await server.start();
  const serverUrl = server.getUrl();
  console.log(`✓ Server started on ${serverUrl}`);

  try {
    await context.grantPermissions(["camera", "microphone"]);
    const clientUrl = `${WEB_CLIENT_URL}?testServerUrl=${encodeURIComponent(serverUrl)}`;
    await page.goto(clientUrl, { waitUntil: "networkidle" });

    // Verify connection
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 20000,
    });

    console.log("✓ Client connected to server");

    // Verify page structure is ready
    const hasXTerm = await page.evaluate(() => {
      return document.querySelector(".xterm") !== null;
    });
    expect(hasXTerm).toBeTruthy();
    console.log("✓ Client has xterm terminal ready");
  } finally {
    await server.stop();
  }
});

test("client mode: maintains FPS > 15", async ({ page, context }) => {
  test.setTimeout(TEST_TIMEOUT);

  // Start server for this test
  const port = getRandomPort();
  const server = new ServerFixture(port);
  await server.start();
  const serverUrl = server.getUrl();
  console.log(`✓ Server started on ${serverUrl}`);

  try {
    await context.grantPermissions(["camera", "microphone"]);
    const clientUrl = `${WEB_CLIENT_URL}?testServerUrl=${encodeURIComponent(serverUrl)}`;
    await page.goto(clientUrl, { waitUntil: "networkidle" });

    // Wait for connection
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 20000,
    });

    console.log("✓ Client connected, measuring FPS from frame metrics...");

    // Wait 2 seconds for rendering to start
    await page.waitForTimeout(2000);

    // Measure FPS by comparing frame count over a 3-second period
    const startMetrics = await page.evaluate(() => {
      console.log(
        "[Test] Start metrics - window.__clientFrameMetrics =",
        window.__clientFrameMetrics,
      );
      const metrics = window.__clientFrameMetrics || {
        rendered: 0,
        received: 0,
        queueDepth: 0,
      };
      return { startTime: performance.now(), ...metrics };
    });

    // Wait 3 more seconds for rendering to occur
    await page.waitForTimeout(3000);

    const endMetrics = await page.evaluate(() => {
      console.log(
        "[Test] End metrics - window.__clientFrameMetrics =",
        window.__clientFrameMetrics,
      );
      const metrics = window.__clientFrameMetrics || {
        rendered: 0,
        received: 0,
        queueDepth: 0,
      };
      return { endTime: performance.now(), ...metrics };
    });

    console.log(`Start metrics:`, startMetrics);
    console.log(`End metrics:`, endMetrics);

    const elapsedMs = endMetrics.endTime - startMetrics.startTime;
    const renderedFrames = endMetrics.rendered - (startMetrics.rendered || 0);
    const receivedFrames = endMetrics.received - (startMetrics.received || 0);
    const uniqueRendered = (endMetrics as any).uniqueRendered || 0;
    const fps = Math.round((renderedFrames / elapsedMs) * 1000);
    const receivedFps = Math.round((receivedFrames / elapsedMs) * 1000);
    const uniqueFps = Math.round((uniqueRendered / elapsedMs) * 1000);

    console.log(
      `Frame metrics: received ${receivedFrames} frames, rendered ${renderedFrames} frames in ${elapsedMs.toFixed(0)}ms`,
    );
    console.log(
      `FPS breakdown - Received: ${receivedFps}, Rendered (calls): ${fps}, Unique changes: ${uniqueFps}`,
    );
    console.log(
      `Start metrics: rendered=${startMetrics.rendered}, received=${startMetrics.received}`,
    );
    console.log(
      `End metrics: rendered=${endMetrics.rendered}, received=${endMetrics.received}, unique=${uniqueRendered}`,
    );
    if ((endMetrics as any).frameHashes) {
      console.log(`Frame hash distribution:`, (endMetrics as any).frameHashes);
    }

    // Client mode MUST:
    // 1. Receive at least 30 different frames per second from network
    // 2. Render calls at least 30 times per second
    // 3. Actually show different content (unique frames) at least 30 times per second
    console.log(
      `\n*** CRITICAL TEST CHECKS ***\nReceived unique frames per sec: ${receivedFps} (should be >= 30)\nRendered calls per sec: ${fps} (should be >= 30)\nUNIQUE frame changes per sec: ${uniqueFps} (should be >= 30) ← THIS IS THE REAL REQUIREMENT`,
    );
    expect(receivedFps).toBeGreaterThanOrEqual(30);
    expect(fps).toBeGreaterThanOrEqual(30);
    expect(uniqueFps).toBeGreaterThanOrEqual(30);
  } finally {
    await server.stop();
  }
});
