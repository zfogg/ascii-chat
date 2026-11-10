import { test, expect } from "@playwright/test";
import {
  expectMeaningful60Fps,
  ServerFixture,
  getRandomPort,
} from "./server-fixture";

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

test("Real webcam animates with server frames", async ({ page, context }) => {
  test.setTimeout(45_000);

  // Grant real camera permission - this will use the ACTUAL system webcam
  await context.grantPermissions(["camera", "microphone"]);

  console.log("Opening browser client with REAL WEBCAM...");
  const clientUrl = `http://localhost:3000/client?connect&testServerUrl=${encodeURIComponent(serverUrl)}`;
  await page.goto(clientUrl, { waitUntil: "networkidle" });

  console.log("\nWaiting 5 seconds for frames to render from real webcam...");
  await page.waitForTimeout(5000);

  const metrics = await page.evaluate(() => window.__clientFrameMetrics);
  console.log("REAL_WEBCAM_METRICS", metrics);
  expect(metrics?.received ?? 0).toBeGreaterThan(0);
  expect(metrics?.uniqueRendered ?? 0).toBeGreaterThan(1);
  await expectMeaningful60Fps(page, "client", 5_000);
});
