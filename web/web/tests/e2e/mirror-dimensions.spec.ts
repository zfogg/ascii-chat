import { expect, test, type Page } from "@playwright/test";

test("worker uses the same cell size when switching fonts", async ({
  page,
}) => {
  await page.goto("/tests/e2e/raster-worker.html");
  await page.waitForFunction(() => "render" in window);
  for (const matrix of [false, true, false]) {
    const result = await page.evaluate(async (matrixEnabled) => {
      const render = (
        window as unknown as {
          render(
            matrix: boolean,
          ): Promise<{ width: number; height: number; lit: number }>;
        }
      ).render;
      return render(matrixEnabled);
    }, matrix);
    expect(result.width).toBe(200);
    expect(result.height).toBe(200);
    expect(result.lit).toBeGreaterThan(100);
  }
});

test("Matrix and regular mode fit the same automatic grid", async ({
  page,
}) => {
  for (const viewport of [
    { width: 1280, height: 900 },
    { width: 1000, height: 750 },
  ]) {
    await page.setViewportSize(viewport);
    const grids = [];
    for (const matrix of [false, true]) {
      // Keep Settings closed so both fonts have identical drawable space.
      await page.goto(
        `/mirror?test2&playing=true${matrix ? "&animationEnabled=true&matrixRain=true" : ""}`,
      );
      await expect(
        page.getByRole("button", { name: "Stop", exact: true }),
      ).toBeVisible();
      await expect
        .poll(async () => (await canvasState(page)).height)
        .toBeGreaterThan(300);
      await expect
        .poll(() =>
          page.locator("canvas.ascii-canvas").evaluate((element) => {
            const canvas = element as HTMLCanvasElement;
            const pixels = canvas
              .getContext("2d")!
              .getImageData(0, 0, canvas.width, canvas.height).data;
            let lit = 0;
            for (let i = 0; i < pixels.length; i += 4)
              if (pixels[i + 1]! > 12) lit++;
            return lit;
          }),
        )
        .toBeGreaterThan(1000);
      const grid = (await page.getByText(/^\d+x\d+$/).innerText())
        .split("x")
        .map(Number);
      const canvas = await canvasState(page);
      grids.push(grid);
      // Check actual raster metrics as well as the reported grid, so canvas
      // scaling cannot disguise oversized glyphs or sparse output.
      expect(canvas.width / grid[0]!).toBe(10);
      expect(canvas.height / grid[1]!).toBe(20);
    }
    expect(grids[1]).toEqual(grids[0]);
  }
});

test("automatic grid fills the canvas when toggling Matrix animation", async ({
  page,
}, testInfo) => {
  await page.setViewportSize({ width: 1280, height: 900 });
  await page.goto("/mirror?test&playing=true&settings=true", {
    waitUntil: "domcontentloaded",
  });
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible();

  let matrix = false;
  const expectFullRaster = async () => {
    await expect
      .poll(() =>
        page
          .locator("canvas.ascii-canvas")
          .evaluate((element, matrixEnabled) => {
            const canvas = element as HTMLCanvasElement;
            const pixels = canvas
              .getContext("2d")!
              .getImageData(0, 0, canvas.width, canvas.height).data;
            let lit = 0;
            let colored = 0;
            for (let i = 0; i < pixels.length; i += 4) {
              if (pixels[i + 1]! > 12) lit++;
              if (
                pixels[i]! > pixels[i + 1]! + 3 ||
                pixels[i + 2]! > pixels[i + 1]! + 3
              )
                colored++;
            }
            if (lit < 20 || (matrixEnabled && colored > 0)) return 0;
            const rect = canvas.getBoundingClientRect();
            const scale = Math.min(
              rect.width / canvas.width,
              rect.height / canvas.height,
            );
            // Rounding to whole cells can leave less than one cell of unused space.
            // The old Matrix estimate left about a third of the viewport empty.
            return Math.min(
              (canvas.width * scale) / rect.width,
              (canvas.height * scale) / rect.height,
            );
          }, matrix),
      )
      .toBeGreaterThan(0.94);
  };

  await expectFullRaster();
  for (let attempt = 0; attempt < 2; attempt++) {
    matrix = true;
    await page.getByRole("button", { name: "Animation", exact: true }).click();
    await expect(page).toHaveURL(/matrixRain=true/);
    await expectFullRaster();
    await page.setViewportSize({
      width: 1100 + attempt * 180,
      height: 800 + attempt * 100,
    });
    await expectFullRaster();
    await page.getByRole("button", { name: "Settings", exact: true }).click();
    await expectFullRaster();
    await page.getByRole("button", { name: "Settings", exact: true }).click();
    await expectFullRaster();
    await page.reload({ waitUntil: "domcontentloaded" });
    await expect(
      page.getByRole("button", { name: "Stop", exact: true }),
    ).toBeVisible();
    await expectFullRaster();
    if (attempt === 1)
      await page.screenshot({
        path: testInfo.outputPath("matrix-full-canvas.png"),
      });
    await page
      .getByRole("button", { name: "🟢 Animation", exact: true })
      .click();
    matrix = false;
    await expectFullRaster();
  }
});

test("Matrix renders its first row in a compact window after direct load", async ({
  page,
}, testInfo) => {
  await page.setViewportSize({ width: 1270, height: 705 });
  const errors: string[] = [];
  page.on("console", (message) => {
    if (message.type() === "error") errors.push(message.text());
  });
  await page.goto(
    "/mirror?test2&playing=true&settings=true&animationEnabled=true&matrixRain=true",
  );
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible();
  await expect
    .poll(() =>
      page.locator("canvas.ascii-canvas").evaluate((element) => {
        const canvas = element as HTMLCanvasElement;
        const pixels = canvas
          .getContext("2d")!
          .getImageData(0, 0, canvas.width, canvas.height).data;
        for (let y = 0; y < canvas.height; y++) {
          let green = 0;
          for (let x = 0; x < canvas.width; x++) {
            const i = (y * canvas.width + x) * 4;
            if (pixels[i + 1]! > 12 && pixels[i]! < 12) green++;
          }
          if (green > 20) return y / canvas.height;
        }
        return 1;
      }),
    )
    .toBeLessThan(0.1);
  expect(
    errors.filter((message) => /dimensions|height|width/.test(message)),
  ).toEqual([]);
  await page.screenshot({ path: testInfo.outputPath("compact-matrix.png") });
});

async function canvasState(page: Page) {
  return page.locator("canvas.ascii-canvas").evaluate((element) => {
    const canvas = element as HTMLCanvasElement;
    const rect = canvas.getBoundingClientRect();
    const container = canvas.parentElement!.getBoundingClientRect();
    const pixels = canvas
      .getContext("2d")!
      .getImageData(0, 0, canvas.width, canvas.height).data;
    let hash = 2166136261;
    for (let i = 0; i < pixels.length; i += 16) {
      hash = Math.imul(hash ^ pixels[i]!, 16777619);
      hash = Math.imul(hash ^ pixels[i + 1]!, 16777619);
    }
    return {
      width: canvas.width,
      height: canvas.height,
      hash,
      centered:
        Math.abs(rect.x + rect.width / 2 - container.x - container.width / 2) <
          1 &&
        Math.abs(
          rect.y + rect.height / 2 - container.y - container.height / 2,
        ) < 1,
      fits:
        rect.width <= container.width + 1 &&
        rect.height <= container.height + 1,
    };
  });
}

for (const matrix of [false, true]) {
  test(`Mirror grid controls resize and stay centered (${matrix ? "Matrix" : "normal"})`, async ({
    page,
  }, testInfo) => {
    test.setTimeout(60_000);
    await page.setViewportSize({ width: 1280, height: 900 });
    await page.goto("/mirror?test", { waitUntil: "domcontentloaded" });
    await expect(
      page.getByRole("button", { name: "Stop", exact: true }),
    ).toBeVisible();
    await page.getByRole("button", { name: "Settings", exact: true }).click();
    if (matrix)
      await page
        .getByRole("button", { name: "Animation", exact: true })
        .click();
    const fit = page.getByLabel("Content fit", { exact: true });
    const canvas = page.locator("canvas.ascii-canvas");
    await expect(fit).toHaveValue("contain");
    await expect(canvas).toHaveCSS("object-fit", "contain");
    await fit.selectOption("cover");
    await expect(canvas).toHaveCSS("object-fit", "cover");
    await expect(page).toHaveURL(/fit=cover/);
    await fit.selectOption("contain");
    await expect(canvas).toHaveCSS("object-fit", "contain");
    await expect(page).not.toHaveURL(/fit=/);
    const width = page.getByLabel("Width", { exact: true });
    const height = page.getByLabel("Height", { exact: true });
    await expect(width).toHaveValue("0");
    await expect(height).toHaveValue("0");
    const initial = await canvasState(page);
    await page.setViewportSize({ width: 1000, height: 750 });
    await expect
      .poll(async () => (await canvasState(page)).width)
      .not.toBe(initial.width);
    await expect
      .poll(async () => (await canvasState(page)).height)
      .not.toBe(initial.height);

    await width.fill("40");
    await height.fill("12");
    await expect(page.getByText("40x12", { exact: true })).toBeVisible();
    const small = await canvasState(page);
    await width.fill("80");
    await expect(page.getByText("80x12", { exact: true })).toBeVisible();
    await expect
      .poll(async () => (await canvasState(page)).width)
      .toBe(small.width * 2);
    await height.fill("24");
    await expect(page.getByText("80x24", { exact: true })).toBeVisible();
    await expect
      .poll(async () => (await canvasState(page)).height)
      .toBe(small.height * 2);
    await expect
      .poll(async () => (await canvasState(page)).centered)
      .toBe(true);
    await expect.poll(async () => (await canvasState(page)).fits).toBe(true);
    await page.screenshot({ path: testInfo.outputPath("centered-grid.png") });
    const frame = await canvasState(page);
    await expect
      .poll(async () => (await canvasState(page)).hash)
      .not.toBe(frame.hash);

    // Zero returns each axis to automatic sizing independently.
    await width.fill("0");
    await expect(width).toHaveValue("0");
    await expect(height).toHaveValue("24");
    await expect
      .poll(async () => (await canvasState(page)).width)
      .not.toBe(frame.width);
    await width.fill("60");
    await expect(page.getByText("60x24", { exact: true })).toBeVisible();

    // Custom grid dimensions survive window resizing while staying centered.
    const custom = await canvasState(page);
    await page.setViewportSize({ width: 900, height: 1000 });
    await expect(width).toHaveValue("60");
    await expect(height).toHaveValue("24");
    await expect(page.getByText("60x24", { exact: true })).toBeVisible();
    await expect
      .poll(async () => (await canvasState(page)).width)
      .toBe(custom.width);
    await expect
      .poll(async () => (await canvasState(page)).height)
      .toBe(custom.height);
    await expect
      .poll(async () => (await canvasState(page)).centered)
      .toBe(true);
    await expect.poll(async () => (await canvasState(page)).fits).toBe(true);
    const resumedFrame = await canvasState(page);
    await expect
      .poll(async () => (await canvasState(page)).hash)
      .not.toBe(resumedFrame.hash);

    // An automatic axis still follows the viewport beside a custom axis.
    await width.fill("0");
    await expect
      .poll(async () => (await canvasState(page)).width)
      .not.toBe(custom.width);
    const mixed = await canvasState(page);
    await page.setViewportSize({ width: 1100, height: 850 });
    await expect(width).toHaveValue("0");
    await expect(height).toHaveValue("24");
    await expect
      .poll(async () => (await canvasState(page)).width)
      .not.toBe(mixed.width);
    await expect
      .poll(async () => (await canvasState(page)).height)
      .toBe(custom.height);
    await expect
      .poll(async () => (await canvasState(page)).centered)
      .toBe(true);

    await width.fill("40");
    await height.fill("12");
    await expect(page.getByText("40x12", { exact: true })).toBeVisible();
    await page.reload({ waitUntil: "domcontentloaded" });
    if (!(await width.isVisible()))
      await page.getByRole("button", { name: "Settings", exact: true }).click();
    await expect(width).toHaveValue("0");
    await expect(height).toHaveValue("0");
    await expect
      .poll(async () => (await canvasState(page)).centered)
      .toBe(true);
  });
}
