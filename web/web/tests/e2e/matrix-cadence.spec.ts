import { expect, test } from "@playwright/test";

test("Matrix changes raster content above the former ten-FPS ceiling", async ({
  page,
}, info) => {
  await page.goto(
    "/mirror?test2&playing=true&animationEnabled=true&matrixRain=true",
  );
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible();
  await expect
    .poll(() =>
      page
        .locator("canvas.ascii-canvas")
        .evaluate((c: HTMLCanvasElement) => c.height),
    )
    .toBeGreaterThan(300);
  const cadence = await page
    .locator("canvas.ascii-canvas")
    .evaluate(async (canvas: HTMLCanvasElement) => {
      const ctx = canvas.getContext("2d")!;
      let previous = 0,
        changes = 0,
        samples = 0;
      const start = performance.now();
      return await new Promise<{
        changes: number;
        samples: number;
        hz: number;
      }>((resolve) => {
        function sample(now: number) {
          const data = ctx.getImageData(
            Math.max(0, (canvas.width - 320) >> 1),
            Math.max(0, (canvas.height - 160) >> 1),
            320,
            160,
          ).data;
          let hash = 2166136261;
          for (let i = 1; i < data.length; i += 4)
            hash = Math.imul(hash ^ data[i]!, 16777619);
          if (samples && hash !== previous) changes++;
          samples++;
          previous = hash;
          if (now - start < 3000) requestAnimationFrame(sample);
          else
            resolve({ changes, samples, hz: (changes * 1000) / (now - start) });
        }
        requestAnimationFrame(sample);
      });
    });
  await info.attach("actual-raster-cadence", {
    body: JSON.stringify(cadence),
    contentType: "application/json",
  });
  expect(cadence.hz).toBeGreaterThan(25);
  await page.screenshot({ path: info.outputPath("matrix-upgraded.png") });
});
