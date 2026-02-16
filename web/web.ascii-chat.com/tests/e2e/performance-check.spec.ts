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

// Run these tests serially to avoid port/server conflicts
test.describe.serial("Client Performance", () => {
  let server: ServerFixture | null = null;
  let serverUrl: string = "";

  test.beforeAll(async () => {
    const port = getRandomPort();
    server = new ServerFixture(port);
    await server.start();
    serverUrl = server.getUrl();
    console.log(`✓ Server started on ${serverUrl}`);
  });

  test.afterAll(async () => {
    if (server) {
      await server.stop();
    }
  });

  test.beforeEach(async ({ page }) => {
    page.on("console", (msg) => console.log(`BROWSER: ${msg.text()}`));
  });

  test("client mode: can initialize and connect", async ({ page, context }) => {
    test.setTimeout(TEST_TIMEOUT);

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
  });

  test("client mode: maintains FPS > 15", async ({ page, context }) => {
    test.setTimeout(TEST_TIMEOUT);

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
        (window as any).__clientFrameMetrics,
      );
      const metrics = (window as any).__clientFrameMetrics || {
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
        (window as any).__clientFrameMetrics,
      );
      const metrics = (window as any).__clientFrameMetrics || {
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
    const fps = Math.round((renderedFrames / elapsedMs) * 1000);

    console.log(
      `Frame metrics: rendered ${renderedFrames} frames in ${elapsedMs.toFixed(0)}ms = ${fps} FPS`,
    );
    console.log(`Client FPS measured: ${fps}`);
    // Client mode should maintain at least 30 FPS
    // Low FPS indicates a bug in rendering or frame processing
    expect(fps).toBeGreaterThanOrEqual(30);
  });
});
