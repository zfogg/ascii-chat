import { test, expect } from "@playwright/test";
import { mkdtempSync, writeFileSync, unlinkSync, rmdirSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
const fixtureDirectory = mkdtempSync(
  path.join(tmpdir(), "ascii-device-audio-"),
);
const audioFixture = path.join(fixtureDirectory, "tone.wav");
const samples = 48000 * 3,
  wav = Buffer.alloc(44 + samples * 2);
wav.write("RIFF", 0);
wav.writeUInt32LE(wav.length - 8, 4);
wav.write("WAVEfmt ", 8);
wav.writeUInt32LE(16, 16);
wav.writeUInt16LE(1, 20);
wav.writeUInt16LE(1, 22);
wav.writeUInt32LE(48000, 24);
wav.writeUInt32LE(96000, 28);
wav.writeUInt16LE(2, 32);
wav.writeUInt16LE(16, 34);
wav.write("data", 36);
wav.writeUInt32LE(samples * 2, 40);
for (let i = 0; i < samples; i++)
  wav.writeInt16LE(
    Math.round(8000 * Math.sin((2 * Math.PI * 440 * i) / 48000)),
    44 + i * 2,
  );
writeFileSync(audioFixture, wav);

export function deviceSetupTests(site: "www" | "web") {
  test.use({
    permissions: ["camera", "microphone"],
    launchOptions: {
      args: [
        "--use-fake-device-for-media-stream",
        "--use-fake-ui-for-media-stream",
        `--use-file-for-fake-audio-capture=${audioFixture}`,
      ],
    },
  });
  test.afterEach(async ({ page }, info) => {
    await info.attach("devices.json", {
      body: JSON.stringify(
        await page.evaluate(async () => ({
          probe: { ...(window as any).deviceProbe, tracks: undefined },
          devices: (await navigator.mediaDevices.enumerateDevices()).map(
            (d) => ({ id: d.deviceId, kind: d.kind }),
          ),
          saved: localStorage.getItem("ascii-chat.media-devices"),
        })),
      ),
      contentType: "application/json",
    });
  });
  test.afterAll(() => {
    unlinkSync(audioFixture);
    rmdirSync(fixtureDirectory);
  });
  test(`${site}: shared device preview, selection, persistence and live capture`, async ({
    page,
  }, info) => {
    test.setTimeout(90000);
    const errors: string[] = [];
    page.on("pageerror", (error) => errors.push(error.message));
    await page.addInitScript(() => {
      const probe = {
        requests: [] as MediaStreamConstraints[],
        tracks: [] as MediaStreamTrack[],
        failures: [] as unknown[],
      };
      Object.assign(window, { deviceProbe: probe });
      const acquire = navigator.mediaDevices.getUserMedia.bind(
        navigator.mediaDevices,
      );
      navigator.mediaDevices.getUserMedia = async (constraints) => {
        probe.requests.push(constraints!);
        let stream: MediaStream;
        try {
          stream = await acquire(constraints);
        } catch (error) {
          probe.failures.push({
            name: (error as DOMException).name,
            constraint: (error as OverconstrainedError).constraint,
            constraints,
          });
          throw error;
        }

        for (const track of stream.getTracks()) {
          probe.tracks.push(track);
          const clone = track.clone.bind(track);
          track.clone = () => {
            const result = clone();
            probe.tracks.push(result);
            return result;
          };
        }
        return stream;
      };
    });
    await page.goto(site === "www" ? "/docs/display#face-follow" : "/mirror");
    const widget =
      site === "www"
        ? page.getByTestId("face-follow-demo")
        : page.locator("body");
    const setup = () => widget.getByRole("button", { name: /^Device setup$/i });
    await setup().click();
    const dialog = page.getByRole("dialog");
    await expect(
      dialog.getByRole("heading", { name: "Device setup", exact: true }),
    ).toBeVisible();
    const preview = dialog.getByLabel("Camera preview", { exact: true });
    await expect
      .poll(() => preview.evaluate((el: HTMLVideoElement) => el.videoWidth))
      .toBeGreaterThan(0);
    const camera = dialog.getByRole("combobox", {
        name: "Camera",
        exact: true,
      }),
      microphone = dialog.getByRole("combobox", {
        name: "Microphone",
        exact: true,
      });
    const { cameraId, micId } = await page.evaluate(async () => {
      const devices = await navigator.mediaDevices.enumerateDevices();
      return {
        cameraId: devices.find((d) => d.kind === "videoinput")!.deviceId,
        micId: devices.find(
          (d) =>
            d.kind === "audioinput" &&
            !["default", "communications"].includes(d.deviceId),
        )!.deviceId,
      };
    });
    expect(cameraId).toBeTruthy();
    expect(micId).toBeTruthy();
    await camera.selectOption(cameraId);
    await microphone.selectOption(micId);
    await expect(
      dialog.getByRole("button", { name: "Stop test", exact: true }),
    ).toBeEnabled();
    await expect
      .poll(async () =>
        Number(await dialog.getByRole("meter").getAttribute("aria-valuenow")),
      )
      .toBeGreaterThan(0);
    await dialog
      .getByRole("button", { name: "Stop test", exact: true })
      .click();
    await dialog
      .getByRole("button", { name: "Test speakers", exact: true })
      .click();
    await expect(
      dialog.getByRole("button", { name: "Playing test tone…", exact: true }),
    ).toBeVisible();
    await expect(
      dialog.getByRole("button", { name: "Test speakers", exact: true }),
    ).toBeEnabled();
    await dialog
      .locator(".modal-panel")
      .screenshot({ path: info.outputPath(`${site}-device-setup.png`) });
    if (site === "www")
      await expect(dialog.getByLabel("Video encoding")).toHaveCount(0);
    else await expect(dialog.getByLabel("Video encoding")).toBeVisible();
    const query = await page.evaluate(() => location.search);
    await dialog.getByRole("button", { name: "Done", exact: true }).click();
    await expect(dialog).toHaveCount(0);
    const saved = await page.evaluate(() =>
      JSON.parse(localStorage.getItem("ascii-chat.media-devices")!),
    );
    expect(saved).toMatchObject({ cameraId, microphoneId: micId });
    if (site === "www")
      expect(await page.evaluate(() => location.search)).toBe(query);
    else
      expect(new URL(page.url()).searchParams.get("cameraId")).toBe(cameraId);
    const probe = () =>
      page.evaluate(async () => {
        const p = (window as any).deviceProbe;
        return {
          requests: p.requests,
          failures: p.failures,
          devices: (await navigator.mediaDevices.enumerateDevices()).map(
            (d) => ({ id: d.deviceId, kind: d.kind }),
          ),
          live: p.tracks.filter(
            (t: MediaStreamTrack) => t.readyState === "live",
          ).length,
        };
      });
    await expect.poll(async () => (await probe()).live).toBe(0);
    await widget
      .getByRole("button", {
        name: site === "www" ? "Webcam" : "Start Webcam",
        exact: true,
      })
      .click();
    await expect
      .poll(
        async () => {
          const state = await probe();
          if (state.failures.length) throw new Error(JSON.stringify(state));
          return state.live;
        },
        { timeout: 30000 },
      )
      .toBeGreaterThan(0);
    await expect(
      widget.getByRole("button", { name: "Stop", exact: true }),
    ).toBeVisible();
    await expect
      .poll(async () =>
        (await probe()).requests.some(
          (r: any) => r.video?.deviceId?.exact === cameraId,
        ),
      )
      .toBe(true);
    // Previewing an active camera clones it; closing the modal must leave capture alive.
    const before = (await probe()).live;
    await setup().click();
    await expect
      .poll(() => preview.evaluate((el: HTMLVideoElement) => el.videoWidth))
      .toBeGreaterThan(0);
    await dialog.getByRole("button", { name: "Cancel", exact: true }).click();
    await expect.poll(async () => (await probe()).live).toBe(before);
    await setup().click();
    await dialog
      .getByRole("combobox", { name: "Camera", exact: true })
      .selectOption("");
    const requestCount = (await probe()).requests.length;
    await dialog.getByRole("button", { name: "Done", exact: true }).click();
    await expect
      .poll(async () => (await probe()).requests.length)
      .toBeGreaterThan(requestCount);
    await expect
      .poll(async () => {
        const requests = (await probe()).requests.filter((r: any) => r.video);
        return requests.at(-1)?.video?.deviceId?.exact ?? "";
      })
      .toBe("");
    await expect(widget.locator("canvas.ascii-canvas")).toBeVisible();
    await widget.screenshot({ path: info.outputPath(`${site}-capture.png`) });
    await widget.getByRole("button", { name: "Stop", exact: true }).click();
    await expect.poll(async () => (await probe()).live).toBe(0);
    // Chromium's incognito fake-device IDs rotate across reloads. Verify saved
    // controls independently after exercising real capture with the selected IDs.
    await page.reload();
    await setup().click();
    await expect(
      dialog.getByRole("combobox", { name: "Camera", exact: true }),
    ).toHaveValue("");
    await expect(
      dialog.getByRole("combobox", { name: "Microphone", exact: true }),
    ).toHaveValue(micId);
    await dialog.getByRole("button", { name: "Cancel", exact: true }).click();
    await expect.poll(async () => (await probe()).live).toBe(0);
    expect(errors).toEqual([]);
  });
}
