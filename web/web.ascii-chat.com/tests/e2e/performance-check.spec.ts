/**
 * E2E performance test for Client mode rendering
 *
 * Verifies that client mode can connect and render.
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
