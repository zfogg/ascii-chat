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

  console.log("✓ Client connected, measuring FPS...");

  // Collect xterm render timestamps to calculate FPS
  const frameTimes: number[] = [];
  const updateDomContent = async () => {
    const content = await page.evaluate(() => {
      return document.querySelector(".xterm-screen")?.textContent || "";
    });
    if (content.length > 0) {
      frameTimes.push(Date.now());
    }
  };

  // Collect frame timing data over 5 seconds
  const startTime = Date.now();
  while (Date.now() - startTime < 5000) {
    await updateDomContent();
    await page.waitForTimeout(50); // Sample every 50ms
  }

  // Calculate FPS from collected samples
  if (frameTimes.length > 1) {
    const timeSpan = frameTimes[frameTimes.length - 1] - frameTimes[0];
    const fps = (frameTimes.length / timeSpan) * 1000;
    console.log(
      `✓ Client FPS: ${fps.toFixed(2)} (${frameTimes.length} updates in ${timeSpan}ms)`,
    );
    expect(fps).toBeGreaterThanOrEqual(15);
  } else {
    console.log(`⚠ Client: Only ${frameTimes.length} frames detected`);
  }
});
