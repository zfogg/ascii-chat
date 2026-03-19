/**
 * E2E test: Rainbow color filter cycles through the full spectrum
 *
 * Uses test mode (?test) to generate synthetic frames without a webcam.
 * Samples pixel colors from the xterm.js canvas at intervals over 4 seconds
 * and verifies we see the full hue spectrum (all 6 major color sectors).
 *
 * The C-side rainbow filter has a 3.5s cycle period, so 4s guarantees
 * at least one full rotation.
 *
 * Run with: npx playwright test tests/e2e/rainbow-cycle.spec.ts --project=chromium
 */

import { test, expect } from "@playwright/test";

const MIRROR_TEST_URL = "http://localhost:3000/mirror?test";
const TEST_TIMEOUT = 30000;

// Hue sectors of the color wheel (0-360 degrees)
const HUE_SECTORS = [
  { name: "red", min: 0, max: 30 },
  { name: "yellow", min: 30, max: 90 },
  { name: "green", min: 90, max: 150 },
  { name: "cyan", min: 150, max: 210 },
  { name: "blue", min: 210, max: 270 },
  { name: "magenta", min: 270, max: 330 },
  { name: "red-wrap", min: 330, max: 360 },
] as const;

function rgbToHue(r: number, g: number, b: number): number | null {
  const rn = r / 255;
  const gn = g / 255;
  const bn = b / 255;
  const max = Math.max(rn, gn, bn);
  const min = Math.min(rn, gn, bn);
  const delta = max - min;

  // Gray/black/white — no meaningful hue
  if (delta < 0.1) return null;

  let hue: number;
  if (max === rn) {
    hue = 60 * (((gn - bn) / delta) % 6);
  } else if (max === gn) {
    hue = 60 * ((bn - rn) / delta + 2);
  } else {
    hue = 60 * ((rn - gn) / delta + 4);
  }
  if (hue < 0) hue += 360;
  return hue;
}

function hueSector(hue: number): string {
  for (const sector of HUE_SECTORS) {
    if (hue >= sector.min && hue < sector.max) {
      return sector.name === "red-wrap" ? "red" : sector.name;
    }
  }
  return "unknown";
}

test.describe("Rainbow Color Filter Cycling", () => {
  test("should cycle through the entire color spectrum in ~4 seconds", async ({
    page,
  }) => {
    test.setTimeout(TEST_TIMEOUT);

    // Collect console messages for debugging
    page.on("console", (msg) => {
      const type = msg.type();
      const text = msg.text();
      if (type === "error" || text.includes("[rainbow-test]")) {
        const prefix = type === "error" ? "BROWSER:ERR" : "BROWSER";
        console.log(`${prefix}: ${text}`);
      }
    });

    await page.goto(MIRROR_TEST_URL);

    // Wait for the page to load
    await expect(page.locator("text=ASCII Mirror")).toBeVisible({
      timeout: 5000,
    });

    // Dev mode auto-starts the webcam. Wait for rendering to begin.
    // If it hasn't auto-started, click "Start Webcam".
    const stopButton = page.locator('button:text("Stop")');
    const startButton = page.locator('button:text("Start Webcam")');

    // Wait for either button to appear
    await expect(stopButton.or(startButton)).toBeVisible({ timeout: 15000 });

    if (await startButton.isVisible()) {
      await startButton.click();
      await expect(stopButton).toBeVisible({ timeout: 10000 });
    }

    // Wait for first frames to render
    await page.waitForTimeout(500);

    // Open settings and select rainbow filter
    await page.click('button:text("Settings")');

    const colorFilterSelect = page
      .locator("select")
      .filter({ has: page.locator('option[value="rainbow"]') });
    await colorFilterSelect.selectOption("rainbow");
    await expect(colorFilterSelect).toHaveValue("rainbow");

    // Close settings
    await page.click('button:text("Settings")');

    // Wait for the rainbow filter to take effect and a few frames to render
    await page.waitForTimeout(500);

    // Sample pixel colors from the xterm canvas over 4 seconds.
    // The rainbow filter replaces ALL truecolor ANSI codes with a single
    // cycling color per frame, so the entire terminal should be roughly
    // one hue at any given moment.
    const SAMPLE_DURATION_MS = 4000;
    const SAMPLE_INTERVAL_MS = 100; // Sample every 100ms = ~40 samples
    const SAMPLE_COUNT = Math.ceil(SAMPLE_DURATION_MS / SAMPLE_INTERVAL_MS);

    const sampledHues: { time: number; hue: number; sector: string }[] = [];

    for (let i = 0; i < SAMPLE_COUNT; i++) {
      // Read the last ANSI frame from window.__lastAnsiFrame and extract
      // the first truecolor RGB code (\x1b[38;2;R;G;Bm).
      // The rainbow filter replaces ALL color codes with one color per frame,
      // so the first code represents the current rainbow hue.
      const sample = await page.evaluate(() => {
        // eslint-disable-next-line
        const w = window as any;
        const frame = w.__lastAnsiFrame as string | undefined;
        const frameCount = w.__lastAnsiFrameCount as number | undefined;
        const frameTime = w.__lastAnsiFrameTime as number | undefined;
        if (!frame) {
          console.log(
            `[rainbow-test] No frame yet (count=${frameCount}, time=${frameTime})`,
          );
          return null;
        }

        // Find first truecolor ANSI code: ESC[38;2;R;G;Bm
        const match = frame.match(/\x1b\[38;2;(\d+);(\d+);(\d+)m/);
        if (!match) {
          console.log(
            `[rainbow-test] No truecolor code in frame (count=${frameCount}, len=${frame.length})`,
          );
          return null;
        }

        // Also extract first 5 distinct colors to see variety
        const allColors: string[] = [];
        const re = /\x1b\[38;2;(\d+);(\d+);(\d+)m/g;
        let m;
        const seen = new Set<string>();
        while ((m = re.exec(frame)) !== null && allColors.length < 5) {
          const key = `${m[1]},${m[2]},${m[3]}`;
          if (!seen.has(key)) {
            seen.add(key);
            allColors.push(key);
          }
        }
        // Log always so we can see the data
        console.log(
          `[rainbow-test] Frame #${frameCount} colors(${allColors.length}): ${allColors.join(" | ")} first80: ${JSON.stringify(frame.substring(0, 80))}`,
        );

        return {
          r: parseInt(match[1], 10),
          g: parseInt(match[2], 10),
          b: parseInt(match[3], 10),
          sampleCount: 1,
          frameCount: frameCount || 0,
          distinctColors: allColors.length,
        };
      });

      if (sample && sample.sampleCount > 0) {
        const hue = rgbToHue(sample.r, sample.g, sample.b);
        if (i === 0) {
          console.log(
            `[rainbow-debug] First sample: r=${sample.r} g=${sample.g} b=${sample.b} hue=${hue} distinct=${sample.distinctColors}`,
          );
        }
        if (hue !== null) {
          const sector = hueSector(hue);
          sampledHues.push({
            time: i * SAMPLE_INTERVAL_MS,
            hue: Math.round(hue),
            sector,
            frameCount: sample.frameCount ?? 0,
          });
        }
      }

      if (i < SAMPLE_COUNT - 1) {
        await page.waitForTimeout(SAMPLE_INTERVAL_MS);
      }
    }

    console.log(
      `Sampled ${sampledHues.length} hue values over ${SAMPLE_DURATION_MS}ms`,
    );
    console.log(
      "Hue samples:",
      sampledHues.map(
        (s) => `${s.time}ms: ${s.hue}° (${s.sector}) frame#${s.frameCount}`,
      ),
    );

    // We should have gotten a reasonable number of samples
    expect(sampledHues.length).toBeGreaterThan(10);

    // Collect unique sectors seen
    const seenSectors = new Set(sampledHues.map((s) => s.sector));
    console.log("Sectors seen:", [...seenSectors]);

    // A full rainbow cycle (3.5s) should hit at least 5 of the 6 major sectors.
    // We allow 1 sector to be missed due to sampling timing.
    const majorSectors = ["red", "yellow", "green", "cyan", "blue", "magenta"];
    const hitCount = majorSectors.filter((s) => seenSectors.has(s)).length;

    console.log(
      `Hit ${hitCount}/6 major hue sectors: ${majorSectors.filter((s) => seenSectors.has(s)).join(", ")}`,
    );
    console.log(
      `Missed: ${majorSectors.filter((s) => !seenSectors.has(s)).join(", ") || "none"}`,
    );

    expect(
      hitCount,
      `Expected rainbow to cycle through at least 5 of 6 hue sectors in 4s, but only hit ${hitCount}: ${[...seenSectors].join(", ")}`,
    ).toBeGreaterThanOrEqual(5);

    // Verify hues are actually changing (not stuck on one color)
    const uniqueHues = new Set(sampledHues.map((s) => s.hue));
    expect(
      uniqueHues.size,
      `Expected many distinct hue values but only got ${uniqueHues.size} — color may be stuck`,
    ).toBeGreaterThan(5);
  });
});
