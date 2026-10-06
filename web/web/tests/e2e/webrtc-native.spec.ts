import { test, expect } from "@playwright/test";

test("joins native discovery over encrypted WS and opens WebRTC", async ({
  page,
}) => {
  test.setTimeout(60000);
  test.skip(
    !process.env["ASCII_CHAT_TEST_SESSION"],
    "Requires a running native discovery session",
  );
  const errors: string[] = [];
  page.on("console", (message) => {
    if (
      message.text().includes("Audio playback failed") ||
      message.text().includes("Audio send failed")
    )
      errors.push(message.text());
  });
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("http://127.0.0.1:3000/discovery");
  await page
    .getByLabel("Session name", { exact: true })
    .fill(process.env["ASCII_CHAT_TEST_SESSION"]!);
  await page.getByText("Connection settings", { exact: true }).click();
  await page
    .getByLabel("Discovery service URL")
    .fill(process.env["ASCII_CHAT_TEST_SIGNALING_URL"] || "ws://127.0.0.1:28227");
  if (process.env["ASCII_CHAT_TEST_PASSWORD"])
    await page
      .getByLabel("Session password", { exact: true })
      .fill(process.env["ASCII_CHAT_TEST_PASSWORD"]);
  await page.getByLabel("STUN/TURN URLs").fill("");
  await page.getByRole("button", { name: "Join session", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Disconnect", exact: true }),
  ).toBeVisible({ timeout: 40000 });
  const renderedFrames = () =>
    page.evaluate(() => window.__clientFrameMetrics?.rendered ?? 0);
  await expect.poll(renderedFrames, { timeout: 15000 }).toBeGreaterThan(3);
  await expect
    .poll(
      () =>
        page.locator("canvas.ascii-canvas").evaluate((element) => {
          const canvas = element as HTMLCanvasElement;
          const context = canvas.getContext("2d");
          // bitmaprenderer owns the presentation context in accelerated
          // browsers; rendered-frame telemetry covers that path.
          if (!context) return true;
          const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
          for (let i = 0; i < pixels.length; i += 4) {
            if (pixels[i]! > 30 || pixels[i + 1]! > 30 || pixels[i + 2]! > 30)
              return true;
          }
          return false;
        }),
      { timeout: 15000 },
    )
    .toBe(true);
  await page
    .getByRole("button", { name: "Enable microphone", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Mute microphone", exact: true }),
  ).toBeVisible();
  const initialFrames = await renderedFrames();
  // A connected WebRTC data channel is not enough: assert that rendered
  // frames advance during a short browser smoke window.
  await page.waitForTimeout(15000);
  await expect(
    page.getByRole("button", { name: "Disconnect", exact: true }),
  ).toBeVisible();
  const finalFrames = await renderedFrames();
  // The 15-second smoke window is intentionally short, but it must still
  // demonstrate interactive-rate rendering rather than a merely live channel.
  expect((finalFrames - initialFrames) / 15).toBeGreaterThan(50);
  await page.screenshot({
    path: "../../build/webrtc-browser/connected.png",
    fullPage: true,
  });
  await page
    .getByRole("button", { name: "Mute microphone", exact: true })
    .click();
  await page.getByRole("button", { name: "Disconnect", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Join session", exact: true }),
  ).toBeVisible();
  expect(errors).toEqual([]);
});
