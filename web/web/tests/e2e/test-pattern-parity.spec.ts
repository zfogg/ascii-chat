import { expect, test } from "@playwright/test";
import type { MirrorModule } from "../../../packages/shared/src/wasm/mirror";
type PixelMetric = {
  meanError: number;
  changedFraction: number;
  geometryError: number;
};
type Timing = { median: number; p95: number };
declare global {
  interface Window {
    ready: boolean;
    patternModule: MirrorModule;
    comparePattern(
      index: number,
      width: number,
      height: number,
      time: number,
      cadence?: boolean,
      originalFont?: boolean,
    ): PixelMetric;
    releasePattern(): void;
    benchmarkPatterns(): Promise<
      Array<{ index: number; cadence: boolean; c: Timing; canvas: Timing }>
    >;
  }
}

test("fixed-time C/WASM frames match the Canvas reference", async ({
  page,
}, testInfo) => {
  await page.goto("/tests/e2e/fixtures/test-pattern.html");
  await page.waitForFunction(() => window.ready);
  const results = [];
  for (const [width, height] of [
    [320, 240],
    [640, 480],
    [801, 603],
    [1920, 1080],
  ] as const) {
    for (const index of [0, 1])
      for (const time of [0, 1000, 2000, 3999, 4000, 9999]) {
        const metric = await page.evaluate(
          ({ index, width, height, time }) =>
            window.comparePattern(index, width, height, time),
          { index, width, height, time },
        );
        results.push({ index, width, height, time, ...metric });
        expect(
          metric.geometryError,
          JSON.stringify(results.at(-1)),
        ).toBeLessThan(1.0);
        expect(metric.meanError, JSON.stringify(results.at(-1))).toBeLessThan(
          2.0,
        );
        expect(
          metric.changedFraction,
          JSON.stringify(results.at(-1)),
        ).toBeLessThan(0.025);
      }
  }
  for (const index of [0, 1]) {
    await page.evaluate(
      (index) => window.comparePattern(index, 640, 480, 2000),
      index,
    );
    await testInfo.attach(`pattern-${index}-paired`, {
      body: await page.screenshot(),
      contentType: "image/png",
    });
    await page.screenshot({ path: `test-results/pattern-${index}-paired.png` });
  }
  for (const time of [0, 2000, 4500, 9000]) {
    const cadence = await page.evaluate(
      (time) => window.comparePattern(1, 640, 480, time, true),
      time,
    );
    expect(cadence.meanError).toBe(0);
  }
  await testInfo.attach("pixel-metrics", {
    body: JSON.stringify(results, null, 2),
    contentType: "application/json",
  });
  await page.evaluate(() => window.releasePattern());
});

test("renderer validates input, resizes, and releases independent sources", async ({
  page,
}) => {
  await page.goto("/tests/e2e/fixtures/test-pattern.html");
  await page.waitForFunction(() => window.ready);
  const result = await page.evaluate(() => {
    const m = window.patternModule;
    const a = m._wasm_test_pattern_create(1, 1),
      b = m._wasm_test_pattern_create(320, 240);
    const tiny = m._wasm_test_pattern_render(a, 1, 1, 0, 0, 0);
    const resized = m._wasm_test_pattern_render(a, 320, 240, 1, 2000, 0);
    const other = m._wasm_test_pattern_render(b, 320, 240, 1, 2000, 0);
    const equal = m.HEAPU8.slice(resized, resized + 320 * 240 * 3).every(
      (value, i) => value === m.HEAPU8[other + i],
    );
    const invalid = [
      m._wasm_test_pattern_render(a, 0, 1, 0, 0, 0),
      m._wasm_test_pattern_render(a, 1, 1, 2, 0, 0),
      m._wasm_test_pattern_render(a, 1, 1, 0, NaN, 0),
    ];
    m._wasm_test_pattern_destroy(a);
    m._wasm_test_pattern_destroy(b);
    return { tiny: !!tiny, equal, independent: resized !== other, invalid };
  });
  expect(result).toEqual({
    tiny: true,
    equal: true,
    independent: true,
    invalid: [0, 0, 0],
  });
});

test("1080p source generation timing", async ({ page }, testInfo) => {
  await page.goto("/tests/e2e/fixtures/test-pattern.html");
  await page.waitForFunction(() => window.ready);
  const samples = await page.evaluate(() => window.benchmarkPatterns());
  console.log("PATTERN_GENERATION_MS", JSON.stringify(samples));
  await testInfo.attach("generation-timing", {
    body: JSON.stringify(samples, null, 2),
    contentType: "application/json",
  });
  for (const sample of samples) expect(sample.c.median).toBeLessThan(1000 / 60);
});

test("both patterns render in the mirror and stop cleanly", async ({
  page,
}, testInfo) => {
  for (const query of ["test", "test2"]) {
    await page.goto("/mirror?" + query);
    const stop = page.getByRole("button", { name: "Stop", exact: true });
    await expect(stop).toBeVisible({ timeout: 15000 });
    const canvas = page.locator("canvas.ascii-canvas");
    await expect(canvas).toBeVisible();
    await expect
      .poll(() =>
        canvas.evaluate((c) => {
          if (!(c instanceof HTMLCanvasElement))
            throw new Error("Expected canvas");
          const pixels = c
            .getContext("2d")!
            .getImageData(0, 0, c.width, c.height).data;
          let count = 0;
          for (let i = 0; i < pixels.length; i += 4)
            if (pixels[i]! > 32 || pixels[i + 1]! > 32 || pixels[i + 2]! > 32)
              count++;
          return count;
        }),
      )
      .toBeGreaterThan(1000);
    await expect(page.getByText(/FPS:\s*[1-9]\d*\s*\/\s*60/)).toBeVisible({
      timeout: 5000,
    });
    const checksum = () =>
      canvas.evaluate((c) => {
        if (!(c instanceof HTMLCanvasElement))
          throw new Error("Expected canvas");
        const pixels = c
          .getContext("2d")!
          .getImageData(0, 0, c.width, c.height).data;
        let hash = 0;
        for (let i = 0; i < pixels.length; i += 64)
          hash =
            (Math.imul(hash, 31) +
              pixels[i]! +
              pixels[i + 1]! +
              pixels[i + 2]!) |
            0;
        return hash;
      });
    const first = await checksum();
    await expect.poll(checksum).not.toBe(first);
    await testInfo.attach(query + "-mirror", {
      body: await page.screenshot(),
      contentType: "image/png",
    });
    await stop.click();
    await expect(
      page.getByRole("button", { name: "Start Webcam" }),
    ).toBeVisible();
  }
});
