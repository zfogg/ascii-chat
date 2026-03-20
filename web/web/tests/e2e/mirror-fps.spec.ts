/**
 * E2E test: Mirror mode FPS across different settings
 *
 * Uses test mode (?test) to generate synthetic frames without a webcam.
 * Measures actual rendering FPS for different color filter and color mode
 * configurations. All settings should maintain acceptable FPS (>20).
 *
 * Run with: npx playwright test tests/e2e/mirror-fps.spec.ts --project=chromium
 */

import { test, expect } from "@playwright/test";

const MIRROR_TEST_URL = "http://localhost:3000/mirror?test";
const TEST_TIMEOUT = 60000;

// Minimum acceptable FPS for any setting
const MIN_FPS = 20;

// How long to measure FPS for each configuration (ms)
const MEASURE_DURATION_MS = 3000;

/**
 * Wait for mirror to start rendering, then measure FPS over a duration
 * by counting __lastAnsiFrameCount changes.
 */
async function measureFps(
  page: import("@playwright/test").Page,
  durationMs: number,
): Promise<{ fps: number; frameCount: number }> {
  // Get starting frame count
  const startCount = await page.evaluate(() => {
    const w = window as unknown as Record<string, unknown>;
    return (w["__lastAnsiFrameCount"] as number) || 0;
  });
  const startTime = Date.now();

  await page.waitForTimeout(durationMs);

  // Get ending frame count
  const endCount = await page.evaluate(() => {
    const w = window as unknown as Record<string, unknown>;
    return (w["__lastAnsiFrameCount"] as number) || 0;
  });
  const elapsed = Date.now() - startTime;

  const frameCount = endCount - startCount;
  const fps = (frameCount / elapsed) * 1000;

  return { fps: Math.round(fps * 10) / 10, frameCount };
}

/**
 * Wait for the mirror to be rendering frames
 */
async function waitForRendering(page: import("@playwright/test").Page) {
  await page.waitForFunction(
    () => {
      const w = window as unknown as Record<string, unknown>;
      return ((w["__lastAnsiFrameCount"] as number) || 0) > 3;
    },
    { timeout: 15000 },
  );
}

/**
 * Start mirror: wait for page, click start if needed, wait for frames
 */
async function startMirror(page: import("@playwright/test").Page) {
  await page.goto(MIRROR_TEST_URL);
  await expect(page.locator("text=ASCII Mirror")).toBeVisible({
    timeout: 5000,
  });

  const stopButton = page.locator('button:text("Stop")');
  const startButton = page.locator('button:text("Start Webcam")');
  await expect(stopButton.or(startButton)).toBeVisible({ timeout: 15000 });

  if (await startButton.isVisible()) {
    await startButton.click();
    await expect(stopButton).toBeVisible({ timeout: 10000 });
  }

  await waitForRendering(page);
  // Let it stabilize
  await page.waitForTimeout(500);
}

/**
 * Change the color filter via the Settings panel
 */
async function setColorFilter(
  page: import("@playwright/test").Page,
  filter: string,
) {
  await page.click('button:text("Settings")');
  const colorFilterSelect = page
    .locator("select")
    .filter({ has: page.locator('option[value="rainbow"]') });
  await colorFilterSelect.selectOption(filter);
  await expect(colorFilterSelect).toHaveValue(filter);
  await page.click('button:text("Settings")');
  // Let the new setting take effect
  await page.waitForTimeout(300);
}

/**
 * Get and reset timing data from the page
 */
async function getTimingAndReset(page: import("@playwright/test").Page) {
  return page.evaluate(() => {
    const w = window as unknown as Record<string, unknown>;
    const wasmTotal = (w["__wasmTimeTotal"] as number) || 0;
    const xtermTotal = (w["__xtermTimeTotal"] as number) || 0;
    const count = (w["__timingFrameCount"] as number) || 1;
    w["__wasmTimeTotal"] = 0;
    w["__xtermTimeTotal"] = 0;
    w["__timingFrameCount"] = 0;
    return {
      avgWasmMs: Math.round((wasmTotal / count) * 100) / 100,
      avgXtermMs: Math.round((xtermTotal / count) * 100) / 100,
      frames: count,
    };
  });
}

/**
 * Get last frame string length
 */
async function getFrameLength(page: import("@playwright/test").Page) {
  return page.evaluate(() => {
    const w = window as unknown as Record<string, unknown>;
    const frame = w["__lastAnsiFrame"] as string;
    return frame ? frame.length : 0;
  });
}

test.describe("Mirror FPS Performance", () => {
  // Use a large viewport to match real-world fullscreen usage
  test.use({ viewport: { width: 1920, height: 1080 } });

  test("should maintain acceptable FPS across different color filters", async ({
    page,
  }) => {
    test.setTimeout(TEST_TIMEOUT);

    // Collect console errors
    const consoleErrors: string[] = [];
    page.on("console", (msg) => {
      if (msg.type() === "error") {
        consoleErrors.push(msg.text());
      }
    });

    await startMirror(page);
    await getTimingAndReset(page); // Reset counters

    // Measure baseline: default settings (no filter)
    const defaultResult = await measureFps(page, MEASURE_DURATION_MS);
    const defaultLen = await getFrameLength(page);
    const defaultTiming = await getTimingAndReset(page);
    console.log(
      `[FPS] Default: ${defaultResult.fps} FPS (${defaultLen} chars) WASM=${defaultTiming.avgWasmMs}ms xterm=${defaultTiming.avgXtermMs}ms`,
    );

    // Measure rainbow filter
    await setColorFilter(page, "rainbow");
    const rainbowResult = await measureFps(page, MEASURE_DURATION_MS);
    const rainbowLen = await getFrameLength(page);
    const rainbowTiming = await getTimingAndReset(page);
    console.log(
      `[FPS] Rainbow: ${rainbowResult.fps} FPS (${rainbowLen} chars) WASM=${rainbowTiming.avgWasmMs}ms xterm=${rainbowTiming.avgXtermMs}ms`,
    );

    // Measure green filter
    await setColorFilter(page, "green");
    const greenResult = await measureFps(page, MEASURE_DURATION_MS);
    const greenLen = await getFrameLength(page);
    const greenTiming = await getTimingAndReset(page);
    console.log(
      `[FPS] Green: ${greenResult.fps} FPS (${greenLen} chars) WASM=${greenTiming.avgWasmMs}ms xterm=${greenTiming.avgXtermMs}ms`,
    );

    // Measure no filter again (to check if performance degrades over time)
    await setColorFilter(page, "none");
    const noneResult = await measureFps(page, MEASURE_DURATION_MS);
    const noneLen = await getFrameLength(page);
    const noneTiming = await getTimingAndReset(page);
    console.log(
      `[FPS] None(2nd): ${noneResult.fps} FPS (${noneLen} chars) WASM=${noneTiming.avgWasmMs}ms xterm=${noneTiming.avgXtermMs}ms`,
    );

    // Log summary
    console.log(`\n[FPS] === Summary ===`);
    console.log(`[FPS] Default:    ${defaultResult.fps} FPS`);
    console.log(`[FPS] Rainbow:    ${rainbowResult.fps} FPS`);
    console.log(`[FPS] Green:      ${greenResult.fps} FPS`);
    console.log(`[FPS] None (2nd): ${noneResult.fps} FPS`);

    if (consoleErrors.length > 0) {
      console.log(
        `[FPS] Console errors (${consoleErrors.length}):`,
        consoleErrors.slice(0, 5),
      );
    }

    // All configurations should maintain acceptable FPS
    expect(
      defaultResult.fps,
      `Default settings FPS (${defaultResult.fps}) below minimum (${MIN_FPS})`,
    ).toBeGreaterThanOrEqual(MIN_FPS);

    expect(
      rainbowResult.fps,
      `Rainbow filter FPS (${rainbowResult.fps}) below minimum (${MIN_FPS})`,
    ).toBeGreaterThanOrEqual(MIN_FPS);

    expect(
      greenResult.fps,
      `Green filter FPS (${greenResult.fps}) below minimum (${MIN_FPS})`,
    ).toBeGreaterThanOrEqual(MIN_FPS);

    expect(
      noneResult.fps,
      `None filter (2nd) FPS (${noneResult.fps}) below minimum (${MIN_FPS})`,
    ).toBeGreaterThanOrEqual(MIN_FPS);

    // No filter should be dramatically slower than rainbow
    // (within 3x — if rainbow is 60fps, others should be at least 20fps)
    const maxFps = Math.max(
      defaultResult.fps,
      rainbowResult.fps,
      greenResult.fps,
      noneResult.fps,
    );
    const minFps = Math.min(
      defaultResult.fps,
      rainbowResult.fps,
      greenResult.fps,
      noneResult.fps,
    );
    const ratio = maxFps / Math.max(minFps, 0.1);

    console.log(
      `[FPS] Max/Min ratio: ${ratio.toFixed(1)}x (max=${maxFps}, min=${minFps})`,
    );

    expect(
      ratio,
      `FPS ratio between best (${maxFps}) and worst (${minFps}) settings is ${ratio.toFixed(1)}x — should be within 3x`,
    ).toBeLessThanOrEqual(3);
  });
});
