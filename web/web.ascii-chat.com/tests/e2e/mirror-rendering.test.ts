/**
 * E2E tests for Mirror page ASCII rendering
 *
 * Uses Chromium's fake webcam to provide video input without a real camera.
 * Run with: npx playwright test tests/e2e/mirror-rendering.test.ts --project=chromium
 */

import { test, expect } from '@playwright/test';

const MIRROR_URL = 'http://localhost:3000/mirror';

test.use({
  // Grant camera permission and use Chromium's built-in fake webcam
  permissions: ['camera'],
  launchOptions: {
    args: [
      '--use-fake-device-for-media-stream',
      '--use-fake-ui-for-media-stream',
    ],
  },
});

test.beforeEach(async ({ page }) => {
  page.on('console', msg => {
    const type = msg.type();
    const prefix = type === 'error' ? 'BROWSER:ERR' : 'BROWSER';
    console.log(`${prefix}: ${msg.text()}`);
  });
});

test.describe('Mirror Page Rendering', () => {
  test('should render ASCII art in truecolor after clicking Start Webcam', async ({ page }) => {
    await page.goto(MIRROR_URL);

    // Verify the page loaded with the control bar
    await expect(page.locator('text=ASCII Mirror')).toBeVisible({ timeout: 3000 });

    // Default color mode should be truecolor - verify via Settings panel
    await page.click('button:text("Settings")');
    const colorModeSelect = page.locator('select').filter({ has: page.locator('option[value="truecolor"]') });
    await expect(colorModeSelect).toHaveValue('truecolor');
    await page.click('button:text("Settings")');

    // Collect console messages to verify truecolor output
    const truecolorFrames: string[] = [];
    page.on('console', msg => {
      const text = msg.text();
      // WASM logs frame results including first 50 chars of ANSI output
      if (text.includes('[WASM] Frame result:') && text.includes('[38;2;')) {
        truecolorFrames.push(text);
      }
    });

    // Click Start Webcam
    await page.click('button:text("Start Webcam")');

    // Wait for Stop button to appear (confirms webcam started and isRunning=true)
    await expect(page.locator('button:text("Stop")')).toBeVisible({ timeout: 10000 });

    // Verify dimensions are shown in the header (terminal fitted successfully)
    await expect(page.locator('text=/\\d+x\\d+/')).toBeVisible();

    // Wait for truecolor frames to be generated
    await expect(async () => {
      expect(truecolorFrames.length).toBeGreaterThan(0);
    }).toPass({ timeout: 10000 });

    console.log(`Captured ${truecolorFrames.length} truecolor frames`);
    console.log('Sample frame:', truecolorFrames[0]);

    // Verify the ANSI output contains truecolor SGR sequences: ESC[38;2;R;G;Bm
    expect(truecolorFrames[0]).toMatch(/\[38;2;\d+;\d+;\d+m/);

    // Verify terminal has rendered content by checking the xterm screen element
    // has child elements (rows) with text content
    const terminalHasContent = await page.evaluate(() => {
      const screen = document.querySelector('.xterm-screen');
      if (!screen) return { hasContent: false, reason: 'no .xterm-screen' };

      // xterm renders rows into the screen element
      const rows = screen.querySelectorAll('.xterm-rows > div');
      if (rows.length === 0) return { hasContent: false, reason: 'no rows' };

      // Check if any row has text content (ASCII characters)
      let nonEmptyRows = 0;
      for (const row of rows) {
        if (row.textContent && row.textContent.trim().length > 0) {
          nonEmptyRows++;
        }
      }

      return { hasContent: nonEmptyRows > 0, nonEmptyRows, totalRows: rows.length };
    });

    console.log('Terminal content:', terminalHasContent);
    expect(terminalHasContent.hasContent).toBe(true);
  });

  test('should stop rendering when Stop is clicked', async ({ page }) => {
    await page.goto(MIRROR_URL);

    // Wait for terminal to initialize (dimensions appear in header)
    await expect(page.locator('text=/\\d+x\\d+/')).toBeVisible({ timeout: 5000 });

    // Click Start Webcam and wait for it to be running
    await page.click('button:text("Start Webcam")');
    await expect(page.locator('button:text("Stop")')).toBeVisible({ timeout: 10000 });

    // Let it render a few frames
    await page.waitForTimeout(1000);

    // Stop
    await page.click('button:text("Stop")');

    // Start Webcam button should reappear
    await expect(page.locator('button:text("Start Webcam")')).toBeVisible();

    // Stop button should be gone
    await expect(page.locator('button:text("Stop")')).not.toBeVisible();
  });
});
