import { expect, test } from "./browser-fixture";
import path from "node:path";

for (const mode of ["mirror", "client", "discovery"]) {
  test(`Upload File on /${mode}: header placement, cancel and real playback`, async ({
    page,
  }) => {
    test.setTimeout(120_000);
    const errors: string[] = [];
    page.on("pageerror", (error) => errors.push(error.message));
    await page.goto(`/${mode}`);
    const upload = page.getByRole("button", {
      name: "Upload File",
      exact: true,
    });
    await expect(upload).toBeVisible();
    const uploadBox = await upload.boundingBox();
    const settingsBox = await page
      .getByRole("button", { name: "Settings", exact: true })
      .boundingBox();
    expect(uploadBox!.x + uploadBox!.width).toBeLessThanOrEqual(settingsBox!.x);
    await upload.click();
    await expect(page.getByRole("dialog")).toContainText("8 GB");
    await page.getByRole("button", { name: "Cancel", exact: true }).click();
    await expect(page.getByRole("dialog")).toHaveCount(0);
    await upload.click();
    const file =
      process.env["UPLOAD_TEST_FILE"] ??
      path.resolve("tests/fixtures/media/tone.mp4");
    await page.getByLabel("Choose media file").setInputFiles(file);
    await expect(page.getByRole("dialog")).toHaveCount(0);
    await expect(page.locator(".status")).toContainText(path.basename(file), {
      timeout: 90_000,
    });
    const video = page.locator("video:not([autoplay])");
    await expect
      .poll(
        () =>
          video.evaluate(
            (element) => (element as HTMLVideoElement).currentTime,
          ),
        { timeout: 90_000 },
      )
      .toBeGreaterThan(0.1);
    await expect
      .poll(() =>
        video.evaluate((element) => (element as HTMLVideoElement).videoWidth),
      )
      .toBeGreaterThan(0);
    const canvas = page.locator("canvas.ascii-canvas");
    await expect
      .poll(
        () =>
          canvas.evaluate((element) => {
            const surface = element as HTMLCanvasElement;
            const pixels = surface
              .getContext("2d")
              ?.getImageData(0, 0, surface.width, surface.height).data;
            if (!pixels) return 0;
            let visible = 0;
            for (let i = 0; i < pixels.length; i += 4)
              if (pixels[i]! > 8 || pixels[i + 1]! > 8 || pixels[i + 2]! > 8)
                visible++;
            return visible;
          }),
        { timeout: 30_000 },
      )
      .toBeGreaterThan(100);
    expect(errors).toEqual([]);
    await page.screenshot({ path: `test-results/upload-file-${mode}.png` });
    const stop = page.getByRole("button", { name: "Stop", exact: true });
    const stopBox = await stop.boundingBox();
    const activeUploadBox = await upload.boundingBox();
    expect(activeUploadBox!.x).toBeGreaterThanOrEqual(
      stopBox!.x + stopBox!.width,
    );
    await stop.click();
    await expect(video).not.toHaveAttribute("src");
  });
}
