/**
 * E2E tests for Mirror page ASCII rendering
 *
 * Uses the mirror's deterministic test source so rendering is verified without
 * depending on a host camera device.
 */

import { test, expect } from "@playwright/test";
import { expectMeaningful60Fps } from "./server-fixture";

const MIRROR_URL = "http://localhost:3000/mirror?test";
const TEST_TIMEOUT = 20000; // 20 second timeout for all tests

async function ensureMirrorRunning(page: import("@playwright/test").Page) {
  const startButton = page.getByRole("button", { name: "Start Webcam" });
  const stopButton = page.getByRole("button", { name: "Stop", exact: true });

  // Development builds can start the fake camera as soon as the renderer has
  // dimensions. Both states are valid here; only click when it has not started.
  await expect(startButton.or(stopButton)).toBeVisible({ timeout: 10_000 });
  if (await startButton.isVisible()) {
    await startButton.click();
  }
  await expect(stopButton).toBeVisible({ timeout: 10_000 });
}

test.describe("Mirror Page Rendering", () => {
  test("should render ASCII art in truecolor when the mirror starts", async ({
    page,
  }) => {
    test.setTimeout(TEST_TIMEOUT);
    await page.goto(MIRROR_URL);

    // Verify the page loaded with the control bar
    await expect(page.locator("text=ASCII Mirror")).toBeVisible({
      timeout: 3000,
    });

    // Default color mode should be truecolor - verify via Settings panel
    await page.click('button:text("Settings")');
    const colorModeSelect = page
      .locator("select")
      .filter({ has: page.locator('option[value="truecolor"]') });
    await expect(colorModeSelect).toHaveValue("truecolor");
    await page.click('button:text("Settings")');

    await ensureMirrorRunning(page);

    // Verify dimensions are shown in the header (terminal fitted successfully)
    await expect(page.locator("text=/\\d+x\\d+/")).toBeVisible();

    // The renderer draws ANSI output to a canvas, not an xterm DOM grid.
    // Wait for non-transparent, non-black pixels to prove ASCII is visible.
    const asciiCanvas = page.locator("canvas.ascii-canvas");
    await expect(async () => {
      const visiblePixels = await asciiCanvas.evaluate((canvas) => {
        if (!(canvas instanceof HTMLCanvasElement)) return 0;
        const context = canvas.getContext("2d");
        if (!context || canvas.width === 0 || canvas.height === 0) return 0;
        const pixels = context.getImageData(
          0,
          0,
          canvas.width,
          canvas.height,
        ).data;
        let count = 0;
        for (let index = 0; index < pixels.length; index += 4) {
          if (
            pixels[index + 3]! > 0 &&
            (pixels[index]! > 8 ||
              pixels[index + 1]! > 8 ||
              pixels[index + 2]! > 8)
          ) {
            count++;
          }
        }
        return count;
      });
      expect(visiblePixels).toBeGreaterThan(0);
    }).toPass({ timeout: 10_000 });
    await expectMeaningful60Fps(page, "mirror", 3_000);
  });

  test("should stop rendering when Stop is clicked", async ({ page }) => {
    test.setTimeout(TEST_TIMEOUT);
    await page.goto(MIRROR_URL);

    // Wait for terminal to initialize (dimensions appear in header)
    await expect(page.locator("text=/\\d+x\\d+/")).toBeVisible({
      timeout: 5000,
    });

    await ensureMirrorRunning(page);

    await expectMeaningful60Fps(page, "mirror", 3_000);

    // Stop
    await page.click('button:text("Stop")');

    // Start Webcam button should reappear
    await expect(page.locator('button:text("Start Webcam")')).toBeVisible();

    // Stop button should be gone
    await expect(page.locator('button:text("Stop")')).not.toBeVisible();
  });
});
