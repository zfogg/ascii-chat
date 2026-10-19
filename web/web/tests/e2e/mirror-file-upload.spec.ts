import { expect, test, type Page } from "./browser-fixture";
import path from "node:path";

interface PlaybackVideo extends HTMLVideoElement {
  testAudioContext?: AudioContext;
}

async function upload(page: Page, name: string) {
  await page.getByRole("button", { name: "Upload Video", exact: true }).click();
  await page
    .getByLabel("Choose media file")
    .setInputFiles(path.resolve("tests/fixtures/media", name));
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible({ timeout: 90_000 });
  await expect
    .poll(() =>
      page.evaluate(
        () =>
          (window as unknown as Record<string, unknown>)[
            "__lastAnsiFrameCount"
          ] ?? 0,
      ),
    )
    .toBeGreaterThan(2);
}

for (const file of [
  "tone.mp4",
  "tone.webm",
  "tone.avi",
  "pcm.mov",
  "mjpeg.avi",
  "tone.mpg",
  "tone.wmv",
  "hevc.mkv",
]) {
  test(`${file}: ASCII video and decoded audio, pause, restart, mute and stop`, async ({
    page,
  }) => {
    test.setTimeout(120_000);
    await page.goto("/mirror");
    await upload(page, file);
    // Measure from a fresh playback position even if decoding/setup consumed
    // most of the short fixture before the first rendered-frame assertion.
    await page.getByRole("button", { name: "Restart", exact: true }).click();
    const video = page.locator("video:not([autoplay])");
    const rms = await video.evaluate(async (element) => {
      const media = element as PlaybackVideo;
      const context = new AudioContext();
      media.testAudioContext = context;
      await context.resume();
      // captureStream() can retain ended tracks when restart switches from
      // MSE to a completed Blob. Observe the element's current decoded output.
      const source = context.createMediaElementSource(media);
      const analyser = context.createAnalyser();
      source.connect(analyser);
      analyser.connect(context.destination);
      const samples = new Float32Array(analyser.fftSize);
      let peakRms = 0;
      for (let attempt = 0; attempt < 100 && peakRms <= 0.001; attempt++) {
        await new Promise((resolve) => setTimeout(resolve, 50));
        analyser.getFloatTimeDomainData(samples);
        peakRms = Math.max(
          peakRms,
          Math.sqrt(
            samples.reduce((sum, sample) => sum + sample * sample, 0) /
              samples.length,
          ),
        );
      }
      return peakRms;
    });
    expect(rms).toBeGreaterThan(0.001);
    expect(
      await video.evaluate((element) => (element as HTMLVideoElement).muted),
    ).toBe(false);
    await page.getByRole("button", { name: "Pause", exact: true }).click();
    const time = await video.evaluate(
      (element) => (element as HTMLVideoElement).currentTime,
    );
    await page.waitForTimeout(250);
    expect(
      await video.evaluate(
        (element) => (element as HTMLVideoElement).currentTime,
      ),
    ).toBe(time);
    await page.getByRole("button", { name: "Mute", exact: true }).click();
    expect(
      await video.evaluate((element) => (element as HTMLVideoElement).muted),
    ).toBe(true);
    await page.getByRole("button", { name: "Restart", exact: true }).click();
    await expect(
      page.getByRole("button", { name: "Pause", exact: true }),
    ).toBeVisible();
    await page.getByRole("button", { name: "Stop", exact: true }).click();
    expect(await video.getAttribute("src")).toBeNull();
    await video.evaluate((element) =>
      (element as PlaybackVideo).testAudioContext?.close(),
    );
  });
}

test("GIF animates repeatedly and pauses; PNG/JPEG replace it with still ASCII", async ({
  page,
}) => {
  await page.goto("/mirror");
  await upload(page, "loop.gif");
  const pixels = () =>
    page
      .locator("video:not([autoplay]) + canvas")
      .evaluate((element) => (element as HTMLCanvasElement).toDataURL());
  const first = await pixels();
  await expect.poll(pixels).not.toBe(first);
  await page.waitForTimeout(1200);
  const later = await pixels();
  await expect.poll(pixels).not.toBe(later);
  await page.getByRole("button", { name: "Pause", exact: true }).click();
  await page.waitForTimeout(100);
  const paused = await pixels();
  await page.waitForTimeout(250);
  expect(await pixels()).toBe(paused);
  for (const file of ["still.png", "still.jpg"]) {
    await upload(page, file);
    await expect(
      page.getByRole("button", { name: "Pause", exact: true }),
    ).not.toBeVisible();
    await page.waitForTimeout(100);
    const still = await pixels();
    await page.waitForTimeout(250);
    expect(await pixels()).toBe(still);
  }
});
