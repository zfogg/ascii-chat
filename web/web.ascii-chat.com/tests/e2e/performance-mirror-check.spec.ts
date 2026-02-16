/**
 * E2E performance test for Mirror mode rendering
 *
 * Verifies that mirror mode can render with fake webcam.
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
