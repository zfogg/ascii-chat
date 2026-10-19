import { test, expect } from "./browser-fixture";
test("device preview releases streams and can reopen", async ({ page }) => {
  await page.goto("/mirror");
  await page.evaluate(() => {
    const original = navigator.mediaDevices.getUserMedia.bind(
      navigator.mediaDevices,
    );
    const streams: MediaStream[] = [];
    Object.assign(window, { __previewStreams: streams });
    navigator.mediaDevices.getUserMedia = async (constraints) => {
      const stream = await original(constraints);
      streams.push(stream);
      return stream;
    };
  });
  for (let i = 0; i < 2; i++) {
    await page
      .getByRole("button", { name: "Device setup", exact: true })
      .click();
    const preview = page.getByLabel("Camera preview");
    await expect
      .poll(() =>
        preview.evaluate((video: HTMLVideoElement) => video.currentTime),
      )
      .toBeGreaterThan(0);
    await page.getByRole("button", { name: "Close device setup" }).click();
    await expect
      .poll(() =>
        page.evaluate(() =>
          (
            window as unknown as { __previewStreams: MediaStream[] }
          ).__previewStreams.every((s) =>
            s.getTracks().every((t) => t.readyState === "ended"),
          ),
        ),
      )
      .toBe(true);
  }
});
