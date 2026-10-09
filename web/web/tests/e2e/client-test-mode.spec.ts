import { expect, test } from "@playwright/test";
import {
  getRandomPort,
  NativeClientFixture,
  ServerFixture,
  expectMeaningful60Fps,
} from "./server-fixture";

const WEB_CLIENT_URL = "http://localhost:3000/client";

let server: ServerFixture;
let nativeClientA: NativeClientFixture;
let serverUrl = "";

test.beforeAll(async () => {
  const port = getRandomPort();
  server = new ServerFixture(port);
  await server.start();
  serverUrl = server.getUrl();

  nativeClientA = new NativeClientFixture(port, 60);
  await nativeClientA.start();
});

test.afterAll(async () => {
  await nativeClientA.stop();
  await server.stop();
});

test("?test auto-connects, renders, sends synthetic video and audio", async ({
  page,
}) => {
  test.setTimeout(45000);

  await page.goto(
    `${WEB_CLIENT_URL}?test&testServerUrl=${encodeURIComponent(serverUrl)}`,
    { waitUntil: "networkidle" },
  );

  await expect(page.locator(".status")).toContainText("Connected", {
    timeout: 20000,
  });
  await page.getByRole("button", { name: "Enable microphone" }).click();
  const audioLevels = page.getByTestId("audio-levels");
  await expect(audioLevels).toHaveAttribute("data-sent", /[1-9]\d*/i, {
    timeout: 10000,
  });

  await expect
    .poll(() =>
      page.evaluate(
        () =>
          (window as Window & { __clientFrameMetrics?: { rendered: number } })
            .__clientFrameMetrics?.rendered ?? 0,
      ),
    )
    .toBeGreaterThan(10);

  await expect(async () => {
    const { paintedBottomRow, height } = await page
      .locator("canvas.ascii-canvas")
      .evaluate((element) => {
        const canvas = element as HTMLCanvasElement;
        const context = canvas.getContext("2d");
        if (!context || canvas.width === 0 || canvas.height === 0) {
          return { paintedBottomRow: -1, height: canvas.height };
        }

        const pixels = context.getImageData(
          0,
          0,
          canvas.width,
          canvas.height,
        ).data;
        let lastPaintedRow = -1;
        for (let row = 0; row < canvas.height; row++) {
          for (let column = 0; column < canvas.width; column++) {
            const offset = (row * canvas.width + column) * 4;
            if (
              pixels[offset]! > 8 ||
              pixels[offset + 1]! > 8 ||
              pixels[offset + 2]! > 8
            ) {
              lastPaintedRow = row;
              break;
            }
          }
        }
        return { paintedBottomRow: lastPaintedRow, height: canvas.height };
      });

    // A server frame must fill the terminal grid it advertises. This caught a
    // half-block resize that painted only the top half of /client's canvas.
    expect(paintedBottomRow).toBeGreaterThan(Math.floor(height * 0.9));
  }).toPass({ timeout: 10_000 });

  const framesBeforeWindow = await page.evaluate(
    () => window.__clientFrameMetrics?.rendered ?? 0,
  );
  await page.waitForTimeout(5000);
  const framesAfterWindow = await page.evaluate(
    () => window.__clientFrameMetrics?.rendered ?? 0,
  );
  const renderedFps = (framesAfterWindow - framesBeforeWindow) / 5;

  // The native source and browser output should both sustain near-60 FPS.
  expect(renderedFps).toBeGreaterThanOrEqual(55);
  expect(renderedFps).toBeLessThanOrEqual(61);
  await expectMeaningful60Fps(page, "client", 5_000);
});
