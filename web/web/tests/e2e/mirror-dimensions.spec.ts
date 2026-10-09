import { expect, test, type Page } from "@playwright/test";

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
    const width = page.getByLabel("Width (columns; 0 = auto)", { exact: true });
    const height = page.getByLabel("Height (rows; 0 = auto)", { exact: true });
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
