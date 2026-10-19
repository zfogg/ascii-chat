import { test, expect } from "@playwright/test";

const webcam = process.env["FACE_FOLLOW_WEBCAM"];
test.use({
  launchOptions: {
    args: [
      "--use-fake-device-for-media-stream",
      "--use-fake-ui-for-media-stream",
      ...(webcam ? [`--use-file-for-fake-video-capture=${webcam}`] : []),
    ],
  },
});

test("Face Follow uses only the webcam and switches framing before ASCII rendering", async ({
  page,
}, testInfo) => {
  test.setTimeout(90_000);
  const errors: string[] = [];
  const mediaRequests: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.on("request", (request) => {
    if (/demo-video|\.webm|\.mp4/.test(request.url()))
      mediaRequests.push(request.url());
  });
  await page.addInitScript(() => {
    const probe = {
      requests: 0,
      ratio: 1,
      draws: 0,
      tracks: [] as MediaStreamTrack[],
    };
    Object.assign(window, { faceFollowProbe: probe });
    const getUserMedia = navigator.mediaDevices.getUserMedia.bind(
      navigator.mediaDevices,
    );
    navigator.mediaDevices.getUserMedia = async (constraints) => {
      probe.requests++;
      const stream = await getUserMedia(constraints);
      probe.tracks.push(...stream.getTracks());
      return stream;
    };
    const drawImage = Object.getOwnPropertyDescriptor(
      CanvasRenderingContext2D.prototype,
      "drawImage",
    )!.value as CanvasRenderingContext2D["drawImage"];
    CanvasRenderingContext2D.prototype.drawImage = function (
      this: CanvasRenderingContext2D,
      ...args: [CanvasImageSource, ...number[]]
    ) {
      if (args[0] instanceof HTMLVideoElement && args.length === 9) {
        probe.ratio = Number(args[3]) / args[0].videoWidth;
        probe.draws++;
      }
      return Reflect.apply(drawImage, this, args);
    };
  });
  await page.goto("/docs/display#face-follow");
  const section = page.getByTestId("face-follow-demo");
  await expect(
    section.getByRole("heading", { name: "Face Follow" }),
  ).toBeVisible({ timeout: 30_000 });
  await expect(section.getByRole("button", { name: "Demo Video" })).toHaveCount(
    0,
  );
  await expect(
    section.getByRole("link", { name: /White Knuckles/ }),
  ).toHaveCount(0);
  const probe = () =>
    page.evaluate(() => {
      const value = (
        window as unknown as {
          faceFollowProbe: {
            requests: number;
            ratio: number;
            draws: number;
            tracks: MediaStreamTrack[];
          };
        }
      ).faceFollowProbe;
      return {
        ...value,
        tracks: value.tracks.map((track) => track.readyState),
      };
    });
  expect((await probe()).requests).toBe(0);
  await section.getByRole("button", { name: "Webcam", exact: true }).click();
  await expect(
    section.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible({ timeout: 30_000 });
  await expect.poll(async () => (await probe()).draws).toBeGreaterThan(3);
  if (webcam)
    await expect
      .poll(async () => (await probe()).ratio, { timeout: 20000 })
      .toBeLessThan(0.75);
  await section.screenshot({ path: testInfo.outputPath("face-follow.png") });
  await section
    .getByRole("button", { name: "Full Frame", exact: true })
    .click();
  await expect
    .poll(async () => (await probe()).ratio, { timeout: 15000 })
    .toBeGreaterThan(0.99);
  await section.screenshot({ path: testInfo.outputPath("full-frame.png") });
  await section
    .getByRole("button", { name: "Face Follow", exact: true })
    .click();
  if (webcam)
    await expect
      .poll(async () => (await probe()).ratio, { timeout: 20000 })
      .toBeLessThan(0.75);
  await section.getByRole("button", { name: "Stop", exact: true }).click();
  await expect(
    section.getByRole("button", { name: "Webcam", exact: true }),
  ).toBeVisible();
  expect((await probe()).tracks.every((state) => state === "ended")).toBe(true);
  expect(mediaRequests).toEqual([]);
  expect(errors).toEqual([]);
  await page.setViewportSize({ width: 390, height: 844 });
  await section.scrollIntoViewIfNeeded();
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBe(true);
  await section.screenshot({ path: testInfo.outputPath("mobile.png") });
});

test("camera denial leaves the webcam demo retryable", async ({ page }) => {
  await page.addInitScript(() => {
    navigator.mediaDevices.getUserMedia = () =>
      Promise.reject(new DOMException("Camera denied", "NotAllowedError"));
  });
  await page.goto("/docs/display#face-follow");
  const section = page.getByTestId("face-follow-demo");
  await section.getByRole("button", { name: "Webcam", exact: true }).click();
  await expect(
    section.getByText("Camera access failed: Camera denied"),
  ).toBeVisible();
  await expect(
    section.getByRole("button", { name: "Webcam", exact: true }),
  ).toBeEnabled();
  await expect(section.getByRole("button", { name: "Demo Video" })).toHaveCount(
    0,
  );
});
