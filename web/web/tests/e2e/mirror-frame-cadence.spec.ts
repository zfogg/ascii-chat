import { expect, test } from "@playwright/test";
import { expectMeaningful60Fps } from "./server-fixture";

// Exercise a large desktop canvas at 1920x1080.
test.use({ viewport: { width: 1920, height: 1080 } });

test("Mirror test pattern sustains near-refresh-rate ASCII rendering", async ({
  page,
}) => {
  test.setTimeout(30_000);

  await page.goto("/mirror?test2");
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible({
    timeout: 15_000,
  });

  const canvas = page.locator("canvas.ascii-canvas");
  await expect(canvas).toBeVisible();
  const canvasBounds = await canvas.boundingBox();
  const viewport = page.viewportSize();
  expect(canvasBounds).not.toBeNull();
  expect(canvasBounds!.width).toBeGreaterThan(viewport!.width * 0.8);
  expect(canvasBounds!.height).toBeGreaterThan(viewport!.height * 0.6);

  await expect
    .poll(
      () =>
        canvas.evaluate((element) => {
          if (!(element instanceof HTMLCanvasElement)) return false;
          const context = element.getContext("2d");
          if (!context || element.width === 0 || element.height === 0)
            return false;
          const pixels = context.getImageData(
            0,
            0,
            element.width,
            element.height,
          ).data;
          for (let index = 0; index < pixels.length; index += 4) {
            if (
              pixels[index + 3]! > 0 &&
              (pixels[index]! > 8 ||
                pixels[index + 1]! > 8 ||
                pixels[index + 2]! > 8)
            ) {
              return true;
            }
          }
          return false;
        }),
      {
        timeout: 10_000,
        message: "ASCII canvas should contain rendered pixels",
      },
    )
    .toBe(true);

  const fps = page.getByText(/FPS:\s*\d+\s*\/\s*60/);
  await expect
    .poll(
      async () => {
        const match = (await fps.textContent())?.match(/FPS:\s*(\d+)/);
        return match ? Number(match[1]) : 0;
      },
      { timeout: 20_000, message: "Mirror should reach 60 FPS at 1920x1080" },
    )
    .toBe(60);
  await expectMeaningful60Fps(page, "mirror", 5_000);
});
