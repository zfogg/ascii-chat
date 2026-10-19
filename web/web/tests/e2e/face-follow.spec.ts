import { expect, test, type Page } from "@playwright/test";
import { existsSync } from "node:fs";
import path from "node:path";

const fixture = path.resolve(
  process.env["FACE_FOLLOW_VIDEO"] ??
    "../../artifacts/face-follow/scenarios.mp4",
);

test.use({ viewport: { width: 1440, height: 1000 } });
const webcam = path.resolve("../../artifacts/face-follow/webcam.y4m");
test.use({
  launchOptions: {
    args: [
      "--use-fake-device-for-media-stream",
      "--use-fake-ui-for-media-stream",
      `--use-file-for-fake-video-capture=${webcam}`,
    ],
  },
});
test.describe("live webcam", () => {
  for (const path of ["track", "video"] as const)
    test(`crops the ${path} camera path before ASCII conversion`, async ({
      page,
    }, testInfo) => {
      test.skip(!existsSync(webcam), "Generate the webcam fixture first");
      if (path === "video")
        await page.addInitScript(() => {
          Object.defineProperty(window, "MediaStreamTrackProcessor", {
            value: undefined,
          });
        });
      await page.addInitScript(() => {
        const draw = CanvasRenderingContext2D.prototype.drawImage;
        CanvasRenderingContext2D.prototype.drawImage = function (
          this: CanvasRenderingContext2D,
          ...args: [CanvasImageSource, ...number[]]
        ) {
          if (this.canvas.dataset["faceFollow"] && args.length === 9)
            (window as unknown as Record<string, unknown>)["__faceDraw"] =
              Array.from(args).slice(1);
          return Reflect.apply(draw, this, args);
        };
      });
      await page.goto("/mirror?faceFollow&faceFollowDebug&playing=true");
      await expect(page.locator("canvas[data-face-follow]")).toHaveCount(1, {
        timeout: 30_000,
      });
      await expect
        .poll(() => state(page).then((s) => s[8]), { timeout: 20_000 })
        .toBe(1);
      await expect
        .poll(() => state(page).then((s) => s[2]), { timeout: 15_000 })
        .toBeLessThan(0.75);
      const crop = await state(page);
      expect((crop[7] ?? 0) / (crop[3] ?? 1)).toBeGreaterThan(0.55);
      const draw = await page.evaluate(
        () => (window as unknown as Record<string, number[]>)["__faceDraw"],
      );
      expect(draw).toHaveLength(8);
      // The sampled source rectangle is smaller than the camera frame, before C receives its pixels.
      expect(draw![2]! / 640).toBeLessThan(0.75);
      expect(draw![6]! / draw![7]!).toBeCloseTo(640 / 480, 2);
      await testInfo.attach("camera-framing", {
        body: JSON.stringify({ crop, draw }),
        contentType: "application/json",
      });
      await page.screenshot({
        path: testInfo.outputPath(`browser-webcam-${path}.png`),
      });
    });
});

async function state(page: Page): Promise<number[]> {
  return page
    .locator("canvas[data-face-follow]")
    .first()
    .evaluate(
      (canvas) =>
        JSON.parse(
          (canvas as HTMLCanvasElement).dataset["faceFollow"] ?? "[]",
        ) as number[],
    );
}

async function mediaTime(page: Page): Promise<number> {
  return page
    .locator("video")
    .evaluateAll((videos) =>
      Math.max(
        ...videos.map((video) => (video as HTMLVideoElement).currentTime),
      ),
    );
}

test("real detector follows a moving face, retains its subject, widens on loss and reacquires", async ({
  page,
}, testInfo) => {
  test.skip(
    !existsSync(fixture),
    "Run tests/scripts/create_face_follow_fixture.py first",
  );
  test.setTimeout(90_000);
  await page.goto("/mirror?faceFollow&faceFollowDebug");
  await page.getByRole("button", { name: "Upload Video", exact: true }).click();
  await page.getByLabel("Choose media file").setInputFiles(fixture);
  await expect(page.locator("canvas[data-face-follow]")).toHaveCount(1, {
    timeout: 30_000,
  });
  await expect
    .poll(() => state(page).then((s) => s[2]), { timeout: 15_000 })
    .toBeLessThan(0.75);
  await page.screenshot({ path: testInfo.outputPath("browser-follow.png") });
  await page.evaluate(() => {
    const canvas = document.querySelector<HTMLCanvasElement>(
      "canvas[data-face-follow]",
    )!;
    const ticks: { time: number; values: number[] }[] = [];
    (window as unknown as Record<string, unknown>)["__faceTicks"] = ticks;
    new MutationObserver(() =>
      ticks.push({
        time: Number(canvas.dataset["faceFollowTick"]),
        values: JSON.parse(canvas.dataset["faceFollow"] ?? "[]") as number[],
      }),
    ).observe(canvas, {
      attributes: true,
      attributeFilter: ["data-face-follow-tick"],
    });
  });
  const samples: { time: number; values: number[] }[] = [];
  let multipleScreenshot = false;
  let lostScreenshot = false;
  while ((await mediaTime(page)) < 17.5) {
    const time = await mediaTime(page);
    const values = await state(page);
    samples.push({ time, values });
    if (!multipleScreenshot && time > 5 && (values[9] ?? 0) >= 2) {
      await page.screenshot({
        path: testInfo.outputPath("browser-multiple.png"),
      });
      multipleScreenshot = true;
    }
    if (!lostScreenshot && time > 12) {
      await page.screenshot({
        path: testInfo.outputPath("browser-full-frame-fallback.png"),
      });
      lostScreenshot = true;
    }
    await page.waitForTimeout(100);
  }
  await testInfo.attach("framing-samples", {
    body: JSON.stringify(samples, null, 2),
    contentType: "application/json",
  });
  const multiple = samples.filter(
    (sample) =>
      sample.time > 4.5 && sample.time < 8.5 && (sample.values[9] ?? 0) >= 2,
  );
  expect(multiple.length).toBeGreaterThan(3);
  expect(multiple.every((sample) => (sample.values[4] ?? 1) < 0.6)).toBe(true);
  const positions = samples
    .filter((sample) => sample.time < 8.5)
    .map((sample) => sample.values[0]!);
  expect(Math.max(...positions) - Math.min(...positions)).toBeGreaterThan(0.05);
  const absent = samples.filter(
    (sample) => sample.time > 11.5 && sample.time < 12.8,
  );
  expect(absent.length).toBeGreaterThan(2);
  expect(
    absent.every(
      (sample) => (sample.values[2] ?? 0) > 0.98 && sample.values[8] === 0,
    ),
  ).toBe(true);
  const returned = samples.filter((sample) => sample.time > 16);
  expect(
    returned.some(
      (sample) => (sample.values[2] ?? 1) < 0.75 && sample.values[8] === 1,
    ),
  ).toBe(true);
  const ticks = await page.evaluate(
    () =>
      (
        window as unknown as Record<
          string,
          { time: number; values: number[] }[]
        >
      )["__faceTicks"]!,
  );
  await testInfo.attach("controller-ticks", {
    body: JSON.stringify(ticks),
    contentType: "application/json",
  });
  const speeds = ticks.slice(1).flatMap((tick, i) => {
    const previous = ticks[i]!;
    const dt = tick.time - previous.time;
    return dt > 0
      ? [Math.abs(Math.log(tick.values[2]! / previous.values[2]!)) / dt]
      : [];
  });
  expect(speeds.length).toBeGreaterThan(100);
  expect(Math.max(...speeds)).toBeLessThan(3.1);
  const panSpeeds = ticks.slice(1).flatMap((tick, i) => {
    const previous = ticks[i]!;
    const dt = tick.time - previous.time;
    const center = (values: number[]) => values[0]! + values[2]! / 2;
    return dt > 0
      ? [Math.abs(center(tick.values) - center(previous.values)) / dt]
      : [];
  });
  expect(Math.max(...panSpeeds)).toBeLessThan(3.2);
  await page.screenshot({
    path: testInfo.outputPath("browser-reacquired.png"),
  });
  expect(await page.locator("canvas[data-face-follow-error]").count()).toBe(0);
});

test("seated shifts hold the view, a brief lean is ignored, and a sustained move reframes", async ({
  page,
}, testInfo) => {
  const seated = path.resolve("../../artifacts/face-follow/seated.mp4");
  test.skip(!existsSync(seated), "Generate the seated fixture first");
  await page.goto("/mirror?faceFollow&faceFollowDebug");
  await page.getByRole("button", { name: "Upload Video", exact: true }).click();
  await page.getByLabel("Choose media file").setInputFiles(seated);
  await expect(page.locator("canvas[data-face-follow]")).toHaveCount(1, {
    timeout: 30_000,
  });
  await expect
    .poll(() => state(page).then((s) => s[2]), { timeout: 15_000 })
    .toBeLessThan(0.75);
  const samples: { time: number; values: number[] }[] = [];
  let screenshot = false;
  while ((await mediaTime(page)) < 17) {
    const time = await mediaTime(page);
    samples.push({ time, values: await state(page) });
    if (!screenshot && time > 11) {
      await page.screenshot({
        path: testInfo.outputPath("browser-seated-hold.png"),
      });
      screenshot = true;
    }
    await page.waitForTimeout(100);
  }
  await testInfo.attach("seated-framing", {
    body: JSON.stringify(samples),
    contentType: "application/json",
  });
  const quiet = samples.filter(
    (sample) => sample.time > 4 && sample.time < 12.8,
  );
  expect(quiet.length).toBeGreaterThan(15);
  for (const axis of [0, 1, 2]) {
    const values = quiet.map((sample) => sample.values[axis]!);
    expect(Math.max(...values) - Math.min(...values)).toBeLessThan(0.001);
  }
  const before = quiet[quiet.length - 1]!.values[0]!;
  const grace = samples.filter(
    (sample) => sample.time > 13 && sample.time < 13.6,
  );
  expect(grace.length).toBeGreaterThan(1);
  expect(
    grace.every((sample) => Math.abs(sample.values[0]! - before) < 0.003),
  ).toBe(true);
  const moved = samples.filter((sample) => sample.time > 15);
  expect(moved.some((sample) => sample.values[0]! - before > 0.06)).toBe(true);
  await page.screenshot({
    path: testInfo.outputPath("browser-deliberate-reframe.png"),
  });
});

test("settings panel toggles real framing and the setting survives reload", async ({
  page,
}, testInfo) => {
  test.skip(
    !existsSync(fixture),
    "Run tests/scripts/create_face_follow_fixture.py first",
  );
  test.setTimeout(90_000);
  await page.goto("/mirror?faceFollowDebug");
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  const toggle = page.getByRole("button", { name: "Face Follow", exact: true });
  await expect(toggle).toHaveAttribute("aria-pressed", "false");
  await expect(
    page.getByRole("button", { name: /Face Follow Debug/i }),
  ).toHaveCount(0);
  await page.getByRole("button", { name: "Upload Video", exact: true }).click();
  await page.getByLabel("Choose media file").setInputFiles(fixture);
  await expect(page.locator("canvas[data-face-follow]")).toHaveCount(1, {
    timeout: 30_000,
  });
  expect((await state(page))[2]).toBe(1);
  await toggle.click();
  await expect(toggle).toHaveAttribute("aria-pressed", "true");
  await expect
    .poll(() => state(page).then((s) => s[2]), { timeout: 15_000 })
    .toBeLessThan(0.75);
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.screenshot({
    path: testInfo.outputPath("browser-settings.png"),
    fullPage: true,
  });
  await toggle.click();
  await expect
    .poll(() => state(page).then((s) => s[2]), { timeout: 10_000 })
    .toBeGreaterThan(0.99);
  await toggle.click();
  await expect(page).toHaveURL(/faceFollow=true/);
  await page.reload();
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await expect(toggle).toHaveAttribute("aria-pressed", "true");
});
