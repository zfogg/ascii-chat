import { expect, test } from "@playwright/test";

interface MatrixFrameProbe {
  active: boolean;
  frames: number;
  coloredFrames: number;
  greenFrames: number;
}

declare global {
  interface Window {
    matrixFrameProbe: MatrixFrameProbe;
  }
}

// Observe every canvas upload, including frames too brief for screenshots.
test("Matrix starts green after enabling, restarting, and resizing", async ({
  page,
}, testInfo) => {
  test.setTimeout(60_000);
  await page.addInitScript(() => {
    const probe = {
      active: false,
      frames: 0,
      coloredFrames: 0,
      greenFrames: 0,
    };
    Object.assign(window, { matrixFrameProbe: probe });
    const putImageData = CanvasRenderingContext2D.prototype.putImageData;
    CanvasRenderingContext2D.prototype.putImageData = function (
      image: ImageData,
      x: number,
      y: number,
      ...dirtyRect: number[]
    ) {
      if (probe.active && this.canvas.classList.contains("ascii-canvas")) {
        let colored = false;
        let green = false;
        for (let i = 0; i < image.data.length; i += 4) {
          if (image.data[i + 3]! === 0) continue;
          // Default rain is RGB(0, 255, 80), scaled by glyph coverage.
          if (
            image.data[i]! > 12 ||
            image.data[i + 2]! > image.data[i + 1]! * (80 / 255) + 3
          )
            colored = true;
          if (image.data[i + 1]! > 12) green = true;
        }
        probe.frames++;
        if (colored) probe.coloredFrames++;
        if (green) probe.greenFrames++;
      }
      Reflect.apply(putImageData, this, [image, x, y, ...dirtyRect]);
    };
    // Arm in the click task so pre-click frames are excluded.
    document.addEventListener(
      "click",
      (event) => {
        if (
          (event.target as Element).closest("button")?.textContent ===
          "Animation"
        ) {
          Object.assign(probe, {
            active: true,
            frames: 0,
            coloredFrames: 0,
            greenFrames: 0,
          });
        }
      },
      true,
    );
  });
  await page.goto("/mirror?test", { waitUntil: "domcontentloaded" });
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  const readProbe = () => page.evaluate(() => window.matrixFrameProbe);
  const samples: MatrixFrameProbe[] = [];
  const assertMatrixFrames = async () => {
    await expect
      .poll(async () => (await readProbe()).frames)
      .toBeGreaterThanOrEqual(8);
    const probe = await readProbe();
    expect(probe.greenFrames).toBeGreaterThan(0);
    expect(probe.coloredFrames, JSON.stringify(probe)).toBe(0);
    samples.push(probe);
  };
  for (let attempt = 0; attempt < 2; attempt++) {
    await page.getByRole("button", { name: "Animation", exact: true }).click();
    await assertMatrixFrames();
    for (const width of [1100, 1280]) {
      const previousWidth = await page
        .locator("canvas.ascii-canvas")
        .evaluate((canvas: HTMLCanvasElement) => canvas.width);
      await page.evaluate(() => {
        const probe = window.matrixFrameProbe;
        Object.assign(probe, { frames: 0, coloredFrames: 0, greenFrames: 0 });
      });
      await page.setViewportSize({ width, height: 800 });
      await expect
        .poll(() =>
          page
            .locator("canvas.ascii-canvas")
            .evaluate((canvas: HTMLCanvasElement) => canvas.width),
        )
        .not.toBe(previousWidth);
      await assertMatrixFrames();
    }
    if (attempt === 1) {
      await page.screenshot({ path: testInfo.outputPath("matrix.png") });
    }
    await page.evaluate(() => {
      window.matrixFrameProbe.active = false;
    });
    await page
      .getByRole("button", { name: "🟢 Animation", exact: true })
      .click();
    // Ensure the non-Matrix converter has run before enabling again.
    await expect
      .poll(() =>
        page.evaluate(() => {
          const canvas = document.querySelector<HTMLCanvasElement>(
            "canvas.ascii-canvas",
          )!;
          const pixels = canvas
            .getContext("2d")!
            .getImageData(0, 0, canvas.width, canvas.height).data;
          return pixels.some(
            (value, i) => (i % 4 === 0 || i % 4 === 2) && value > 12,
          );
        }),
      )
      .toBe(true);
  }
  await testInfo.attach("canvas-frame-samples", {
    body: JSON.stringify(samples),
    contentType: "application/json",
  });
});
