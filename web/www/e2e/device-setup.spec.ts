import { deviceSetupTests } from "../../web/tests/e2e/helpers/deviceSetup";
deviceSetupTests("www");

import { test, expect } from "@playwright/test";
test("saved microphone feeds the docs WASM audio visualization", async ({
  page,
}, info) => {
  await page.addInitScript(() => {
    const probe = {
      requests: [] as MediaStreamConstraints[],
      tracks: [] as MediaStreamTrack[],
    };
    Object.assign(window, { audioDeviceProbe: probe });
    const acquire = navigator.mediaDevices.getUserMedia.bind(
      navigator.mediaDevices,
    );
    navigator.mediaDevices.getUserMedia = async (constraints) => {
      probe.requests.push(constraints!);
      const stream = await acquire(constraints);
      probe.tracks.push(...stream.getTracks());
      return stream;
    };
  });
  await page.goto("/docs/display#audio-visualizations");
  const micId = await page.evaluate(async () => {
    const devices = await navigator.mediaDevices.enumerateDevices();
    const id = devices.find(
      (d) =>
        d.kind === "audioinput" &&
        !["default", "communications"].includes(d.deviceId),
    )!.deviceId;
    localStorage.setItem(
      "ascii-chat.media-devices",
      JSON.stringify({ cameraId: "", microphoneId: id, speakerId: "" }),
    );
    return id;
  });
  const widget = page
    .locator("[data-demo-widget]")
    .filter({
      has: page.getByRole("button", { name: "Audio Waveform", exact: true }),
    });
  await widget.getByRole("button", { name: "Microphone", exact: true }).click();
  await expect(
    widget.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible({ timeout: 30000 });
  expect(
    await page.evaluate(() => (window as any).audioDeviceProbe.requests.at(-1)),
  ).toMatchObject({ audio: { deviceId: { exact: micId } }, video: false });
  await expect
    .poll(() =>
      widget
        .locator("canvas.ascii-canvas")
        .evaluate((canvas: HTMLCanvasElement) => {
          const pixels = canvas
            .getContext("2d")!
            .getImageData(0, 0, canvas.width, canvas.height).data;
          let lit = 0;
          for (let i = 0; i < pixels.length; i += 4)
            if (pixels[i]! > 40 || pixels[i + 1]! > 40 || pixels[i + 2]! > 40)
              lit++;
          return lit;
        }),
    )
    .toBeGreaterThan(100);
  await widget.screenshot({
    path: info.outputPath("selected-microphone-waveform.png"),
  });
  await widget.getByRole("button", { name: "Stop", exact: true }).click();
  expect(
    await page.evaluate(() =>
      (window as any).audioDeviceProbe.tracks.every(
        (t: MediaStreamTrack) => t.readyState === "ended",
      ),
    ),
  ).toBe(true);
});
