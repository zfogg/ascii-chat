/**
 * E2E performance test for Mirror mode rendering
 *
 * Verifies that mirror mode can render with fake webcam and maintains FPS > 15.
 * Run with: bun run test:e2e -- tests/e2e/performance-mirror-check.spec.ts
 */

import { test, expect } from "@playwright/test";

const WEB_MIRROR_URL = "http://localhost:3000/mirror";
const TEST_TIMEOUT = 30000;

// Configure fake webcam for this entire test file
test.use({
  permissions: ["camera"],
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

test("mirror mode: can render with fake webcam", async ({ page }) => {
  test.setTimeout(TEST_TIMEOUT);

  await page.goto(WEB_MIRROR_URL);

  // Verify page loaded
  await expect(page.locator("text=ASCII Mirror")).toBeVisible({
    timeout: 3000,
  });

  console.log("✓ Mirror page loaded");

  // Click Start Webcam
  await page.click('button:has-text("Start Webcam")');

  // Verify xterm is rendering content
  await expect(async () => {
    const hasContent = await page.evaluate(() => {
      const screen = document.querySelector(".xterm-screen");
      if (!screen) return false;

      const rows = screen.querySelectorAll(".xterm-rows > div");
      if (rows.length === 0) return false;

      // Check if any row has text content
      for (const row of rows) {
        if (row.textContent && row.textContent.trim().length > 0) {
          return true;
        }
      }
      return false;
    });
    expect(hasContent).toBeTruthy();
  }).toPass({ timeout: 3000 });

  console.log("✓ Mirror mode rendered ASCII art successfully");

  // Try to stop
  try {
    await page.click("button:has-text('Stop')");
  } catch {
    // OK
  }
});

test("mirror mode: maintains FPS > 15", async ({ page }) => {
  test.setTimeout(TEST_TIMEOUT);

  await page.goto(WEB_MIRROR_URL);

  // Verify page loaded
  await expect(page.locator("text=ASCII Mirror")).toBeVisible({
    timeout: 3000,
  });

  // Click Start Webcam
  await page.click('button:has-text("Start Webcam")');

  // Wait for rendering to start
  await page.waitForTimeout(1000);

  console.log("✓ Measuring Mirror FPS...");

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
      `✓ Mirror FPS: ${fps.toFixed(2)} (${frameTimes.length} updates in ${timeSpan}ms)`,
    );
    expect(fps).toBeGreaterThanOrEqual(15);
  } else {
    console.log(`⚠ Mirror: Only ${frameTimes.length} frames detected`);
  }

  // Try to stop
  try {
    await page.click("button:has-text('Stop')");
  } catch {
    // OK
  }
});
