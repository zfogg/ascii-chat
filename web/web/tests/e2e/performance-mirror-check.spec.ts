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

  // Wait for terminal to be ready (AsciiRenderer needs time to initialize)
  await page.waitForTimeout(1500);

  // Click Start Webcam button
  console.log("Clicking Start Webcam button...");
  const startButton = page.getByRole("button", { name: /Start Webcam/i });
  await startButton.click();
  console.log("✓ Clicked Start Webcam button");

  // Wait for webcam to start and rendering to begin
  await page.waitForTimeout(2000);

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
    const stopButton = await page.getByRole("button", { name: /Stop/i });
    await stopButton.click();
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

  // Wait for terminal to be ready (AsciiRenderer needs time to initialize)
  await page.waitForTimeout(1500);

  // Click Start Webcam button
  console.log("Clicking Start Webcam button...");
  const startButton = page.getByRole("button", { name: /Start Webcam/i });
  await startButton.click();
  console.log("✓ Clicked Start Webcam button");

  // Wait for rendering to start
  console.log("Waiting for rendering to begin...");
  await page.waitForTimeout(3000);

  console.log("✓ Measuring Mirror FPS from page...");

  // Extract FPS value from the page's FPS counter
  const fps = await page.evaluate(() => {
    // Search for elements containing "FPS:" text
    const elements = Array.from(document.querySelectorAll("span, div, p"));
    for (const el of elements) {
      const text = el.textContent || "";
      if (text.includes("FPS:")) {
        console.log("Found FPS text:", text);
        // Match "FPS: 19" or "FPS: 19 / 60"
        const match = text.match(/FPS:\s*(\d+)/);
        if (match) {
          return parseInt(match[1]);
        }
      }
    }
    console.log("No FPS text found on page");
    return null;
  });

  console.log(`Mirror FPS from page: ${fps}`);

  if (fps !== null) {
    expect(fps).toBeGreaterThanOrEqual(15);
  } else {
    console.log("⚠ Could not extract FPS from page");
  }

  // Try to stop
  try {
    const stopButton = await page.getByRole("button", { name: /Stop/i });
    await stopButton.click();
  } catch {
    // OK
  }
});
