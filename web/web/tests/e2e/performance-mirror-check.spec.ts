/**
 * E2E performance test for Mirror mode rendering
 *
 * Verifies that mirror mode sustains near-60-FPS meaningful ASCII frames.
 * Run with: bun run test:e2e -- tests/e2e/performance-mirror-check.spec.ts
 */

import { test, expect } from "@playwright/test";
import { expectMeaningful60Fps } from "./server-fixture";

const WEB_MIRROR_URL = "http://localhost:3000/mirror?test2";
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
  await expect(page.getByText(/Mirror mode/)).toBeVisible({
    timeout: 3000,
  });

  console.log("✓ Mirror page loaded");

  // Wait for terminal to be ready (AsciiRenderer needs time to initialize)
  await page.waitForTimeout(1500);

  // Click Start Webcam button
  console.log("Clicking Start Webcam button...");
  const startButton = page.getByRole("button", { name: /Start Webcam/i });
  if (await startButton.isVisible({ timeout: 1_000 }).catch(() => false)) {
    await startButton.click();
    console.log("✓ Clicked Start Webcam button");
  } else {
    await expect(page.getByRole("button", { name: /Stop/i })).toBeVisible();
    console.log("✓ Synthetic test input already started webcam");
  }

  // Wait for webcam to start and rendering to begin
  await page.waitForTimeout(2000);

  // Verify the renderer is producing a non-empty ANSI frame. The current UI
  // may present the terminal through a canvas rather than xterm DOM rows.
  await expect(async () => {
    const hasContent = await page.evaluate(
      () => (window.__lastAnsiFrame?.length ?? 0) > 0,
    );
    expect(hasContent).toBeTruthy();
  }).toPass({ timeout: 3000 });
  await expectMeaningful60Fps(page, "mirror", 3_000);

  console.log("✓ Mirror mode rendered ASCII art successfully");

  // Try to stop
  try {
    const stopButton = page.getByRole("button", { name: /Stop/i });
    await stopButton.click();
  } catch {
    // OK
  }
});

test("mirror mode: sustains near-60 FPS", async ({ page }) => {
  test.setTimeout(TEST_TIMEOUT);

  await page.goto(WEB_MIRROR_URL);

  // Verify page loaded
  await expect(page.getByText(/Mirror mode/)).toBeVisible({
    timeout: 3000,
  });

  // Wait for terminal to be ready (AsciiRenderer needs time to initialize)
  await page.waitForTimeout(1500);

  // Click Start Webcam button
  console.log("Clicking Start Webcam button...");
  const startButton = page.getByRole("button", { name: /Start Webcam/i });
  if (await startButton.isVisible({ timeout: 1_000 }).catch(() => false)) {
    await startButton.click();
    console.log("✓ Clicked Start Webcam button");
  } else {
    await expect(page.getByRole("button", { name: /Stop/i })).toBeVisible();
    console.log("✓ Synthetic test input already started webcam");
  }

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
        if (match && match[1]) {
          return parseInt(match[1]);
        }
      }
    }
    console.log("No FPS text found on page");
    return null;
  });

  console.log(`Mirror FPS from page: ${fps}`);

  expect(fps).toBeGreaterThanOrEqual(55);
  expect(fps).toBeLessThanOrEqual(62);
  await expectMeaningful60Fps(page, "mirror", 5_000);

  // Try to stop
  try {
    const stopButton = page.getByRole("button", { name: /Stop/i });
    await stopButton.click();
  } catch {
    // OK
  }
});
