import { test, expect, type Page } from "./browser-fixture";
import type { TestInfo } from "@playwright/test";
import { spawn, spawnSync, type ChildProcess } from "node:child_process";
import { writeFileSync } from "node:fs";
import { getRandomPort, waitForPort } from "./server-fixture";
import { installOutputMeter } from "./audio-proof";

// Replace only physical input acquisition. Browser tracks, audio graphs, C/WASM
// rendering, native transport, MediaRecorder, download, and decoding stay real.
async function input(page: Page, frequency = 880) {
  await page.addInitScript(
    ({ frequency }) => {
      const owned: MediaStream[] = [];
      Object.assign(window, { recordingInputs: owned });
      const tone = async (hz: number) => {
        const context = new AudioContext({ sampleRate: 48000 });
        await context.resume();
        const oscillator = context.createOscillator();
        oscillator.frequency.value = hz;
        const gain = context.createGain();
        gain.gain.value = 0.1;
        const destination = context.createMediaStreamDestination();
        oscillator.connect(gain).connect(destination);
        oscillator.start();
        destination.stream
          .getAudioTracks()[0]!
          .addEventListener("ended", () => {
            oscillator.stop();
            void context.close();
          });
        return destination.stream;
      };
      const acquire = navigator.mediaDevices.getUserMedia.bind(
        navigator.mediaDevices,
      );
      navigator.mediaDevices.getUserMedia = async (constraints) => {
        if (constraints?.audio && !constraints.video) {
          const stream = await tone(frequency / 2);
          owned.push(stream);
          return stream;
        }
        return acquire(constraints);
      };
      navigator.mediaDevices.getDisplayMedia = async () => {
        const canvas = document.createElement("canvas");
        canvas.width = 320;
        canvas.height = 180;
        const context = canvas.getContext("2d")!;
        const draw = () => {
          context.fillStyle = frequency === 880 ? "#d82b18" : "#185bd8";
          context.fillRect(0, 0, 320, 180);
          context.fillStyle = "white";
          context.font = "bold 40px monospace";
          context.fillText(`ASCII ${frequency}`, 10, 70);
          context.fillRect((performance.now() / 10) % 270, 110, 40, 40);
        };
        draw();
        const timer = setInterval(draw, 33);
        const stream = canvas.captureStream(30);
        const audio = await tone(frequency);
        audio.getAudioTracks().forEach((track) => stream.addTrack(track));
        stream
          .getVideoTracks()[0]!
          .addEventListener("ended", () => clearInterval(timer));
        owned.push(stream);
        return stream;
      };
    },
    { frequency },
  );
}

function command(binary: string, args: string[]) {
  const result = spawnSync(binary, args, {
    windowsHide: true,
    encoding: "utf8",
    timeout: 30000,
    maxBuffer: 20 * 1024 * 1024,
  });
  expect(result.error, result.stderr).toBeUndefined();
  expect(result.status, result.stderr).toBe(0);
  return result.stdout;
}

function decode(path: string, frequencies: number[], projectionStart = 1) {
  const metadata = JSON.parse(
    command("ffprobe", [
      "-v",
      "error",
      "-show_streams",
      "-show_format",
      "-of",
      "json",
      path,
    ]),
  );
  expect(
    metadata.streams
      .map((stream: { codec_type: string }) => stream.codec_type)
      .sort(),
  ).toEqual(["audio", "video"]);
  const frames = command("ffmpeg", [
    "-v",
    "error",
    "-i",
    path,
    "-map",
    "0:v:0",
    "-f",
    "framemd5",
    "-",
  ])
    .split(/\r?\n/)
    .filter((line) => line && !line.startsWith("#"));
  expect(frames.length).toBeGreaterThan(15);
  expect(
    new Set(frames.map((line) => line.split(",").at(-1)!.trim())).size,
  ).toBeGreaterThan(8);
  // Inputs stay bright through startup: a cleared resize surface must never
  // replace an already presented ASCII frame in the opening second.
  const opening = spawnSync(
    "ffmpeg",
    [
      "-v",
      "error",
      "-i",
      path,
      "-t",
      "1",
      "-vf",
      "scale=160:90",
      "-fps_mode",
      "passthrough",
      "-f",
      "rawvideo",
      "-pix_fmt",
      "gray",
      "-",
    ],
    { windowsHide: true, maxBuffer: 20 * 1024 * 1024 },
  );
  expect(opening.status, opening.stderr?.toString()).toBe(0);
  const openingMeans: number[] = [];
  for (let offset = 0; offset < opening.stdout.length; offset += 160 * 90) {
    const pixels = opening.stdout.subarray(offset, offset + 160 * 90);
    openingMeans.push(
      pixels.reduce((sum, value) => sum + value, 0) / pixels.length,
    );
  }
  const openingMedian = [...openingMeans].sort((a, b) => a - b)[
    Math.floor(openingMeans.length / 2)
  ]!;
  expect(
    Math.min(...openingMeans),
    JSON.stringify(openingMeans),
  ).toBeGreaterThan(openingMedian * 0.2);
  const raw = spawnSync(
    "ffmpeg",
    [
      "-v",
      "error",
      "-i",
      path,
      "-map",
      "0:a:0",
      "-ac",
      "1",
      "-ar",
      "48000",
      "-f",
      "f32le",
      "-",
    ],
    { windowsHide: true, maxBuffer: 20 * 1024 * 1024 },
  );
  expect(raw.status, raw.stderr?.toString()).toBe(0);
  const samples = raw.stdout!;
  const count = samples.length / 4;
  const audioDuration = count / 48000;
  const audioStart = Number(
    metadata.streams.find(
      (stream: { codec_type: string }) => stream.codec_type === "audio",
    ).start_time ?? 0,
  );
  const duration = audioStart + audioDuration;
  expect(duration).toBeGreaterThan(2);
  expect(duration).toBeLessThan(20);
  const videoTimes = command("ffprobe", [
    "-v",
    "error",
    "-select_streams",
    "v:0",
    "-show_entries",
    "packet=pts_time",
    "-of",
    "csv=p=0",
    path,
  ])
    .trim()
    .split(/\r?\n/)
    .map((line) => Number.parseFloat(line))
    .filter(Number.isFinite);
  const videoEnd = Math.max(...videoTimes);
  expect(videoTimes.length).toBeGreaterThan(15);
  expect(Math.abs(videoEnd - duration)).toBeLessThan(0.6);
  let energy = 0;
  for (let i = 0; i < count; i++) energy += samples.readFloatLE(i * 4) ** 2;
  const rms = Math.sqrt(energy / count);
  expect(rms).toBeGreaterThan(0.015);
  // Remote resampling/jitter spreads a sinusoid across nearby frequency bins.
  // Search only ±2 Hz, keep the original amplitude bounds, and compare with
  // unrelated frequencies so noise cannot pass as an intended source.
  const project = (frequency: number) => {
    let real = 0,
      imaginary = 0;
    const length = 48000;
    for (let i = 0; i < length; i++) {
      const value = samples.readFloatLE((i + 48000 * projectionStart) * 4);
      const phase = (2 * Math.PI * frequency * i) / 48000;
      real += value * Math.cos(phase);
      imaginary += value * Math.sin(phase);
    }
    return (2 * Math.hypot(real, imaginary)) / length;
  };
  const noiseFloor = Math.max(...[197, 997, 1777].map(project));
  const amplitudes = frequencies.map((frequency) => {
    const band = [-2, -1, -0.5, 0, 0.5, 1, 2].map((offsetHz) => ({
      offsetHz,
      amplitude: project(frequency + offsetHz),
    }));
    const peak = band.reduce((best, candidate) =>
      candidate.amplitude > best.amplitude ? candidate : best,
    );
    return { frequency, ...peak };
  });
  for (const tone of amplitudes) {
    expect(tone.amplitude, JSON.stringify(amplitudes)).toBeGreaterThan(0.025);
    expect(tone.amplitude, JSON.stringify(amplitudes)).toBeLessThan(0.16);
    expect(tone.amplitude / Math.max(noiseFloor, 1e-8)).toBeGreaterThan(8);
  }
  return {
    metadata,
    frames: frames.length,
    distinctFrames: new Set(frames.map((line) => line.split(",").at(-1))).size,
    duration,
    audioStart,
    audioDuration,
    videoEnd,
    rms,
    amplitudes,
    noiseFloor,
    openingMeans,
  };
}

async function record(
  page: Page,
  name: string,
  info: TestInfo,
  frequencies: number[],
  during?: () => Promise<void>,
  finish?: () => Promise<void>,
) {
  const checkLayout = !during && !finish;
  if (checkLayout) {
    await page.waitForTimeout(500);
    await page.screenshot({
      path: info.outputPath(`${name}-idle.png`),
      fullPage: true,
    });
    await page.evaluate(() => {
      const proof = window as unknown as {
        recordingLayout: number[][];
        stopRecordingLayout: () => void;
      };
      proof.recordingLayout = [];
      let frame = 0;
      const measure = () => {
        const canvas =
          document.querySelector<HTMLCanvasElement>(".ascii-canvas")!;
        const box = canvas.getBoundingClientRect();
        proof.recordingLayout.push([
          box.x,
          box.y,
          box.width,
          box.height,
          canvas.width,
          canvas.height,
        ]);
        frame = requestAnimationFrame(measure);
      };
      measure();
      proof.stopRecordingLayout = () => cancelAnimationFrame(frame);
    });
  }
  await page.getByRole("button", { name: "Record ASCII", exact: true }).click();
  await expect(
    page.getByText("Recording locally", { exact: false }),
  ).toBeVisible();
  await during?.();
  await page.waitForTimeout(4000);
  await page.screenshot({
    path: info.outputPath(`${name}-recording.png`),
    fullPage: true,
  });
  if (finish) await finish();
  else
    await page
      .getByRole("button", { name: "Stop recording", exact: true })
      .click();
  const link = page.getByRole("link", {
    name: "Download recording",
    exact: true,
  });
  await expect(link).toBeVisible();
  if (checkLayout) {
    await page.waitForTimeout(400);
    const geometry = await page.evaluate(() => {
      const proof = window as unknown as {
        recordingLayout: number[][];
        stopRecordingLayout: () => void;
      };
      proof.stopRecordingLayout();
      return proof.recordingLayout;
    });
    writeFileSync(
      info.outputPath(`${name}-layout.json`),
      JSON.stringify(geometry),
    );
    expect(
      new Set(geometry.map((box) => JSON.stringify(box))).size,
      "canvas geometry from idle through download",
    ).toBe(1);
  }
  const download = page.waitForEvent("download");
  await link.click();
  const saved = await download;
  const path = info.outputPath(`${name}.webm`);
  await saved.saveAs(path);
  const evidence = decode(path, frequencies);
  writeFileSync(
    info.outputPath(`${name}-decoded.json`),
    JSON.stringify(evidence, null, 2),
  );
  await page.screenshot({
    path: info.outputPath(`${name}-download.png`),
    fullPage: true,
  });
  await page.evaluate(async () => {
    const link = document.querySelector<HTMLAnchorElement>(
      'a[download^="ascii-chat-"]',
    )!;
    const container = document.createElement("section");
    container.id = "export-playback";
    container.style.cssText =
      "position:fixed;inset:70px 40px 40px;z-index:9999;background:#10121b;color:white;padding:20px;overflow:auto";
    const title = document.createElement("h2");
    title.textContent = "Downloaded ASCII recording — browser playback";
    const video = document.createElement("video");
    video.src = link.href;
    video.controls = true;
    video.muted = true;
    video.style.cssText = "width:100%;max-height:75vh;object-fit:contain";
    container.append(title, video);
    document.body.append(container);
    await video.play();
    await new Promise((resolve) => setTimeout(resolve, 800));
    video.pause();
  });
  await page.screenshot({
    path: info.outputPath(`${name}-playback.png`),
    fullPage: true,
  });
  await page
    .locator("#export-playback")
    .evaluate((element) => element.remove());
  return evidence;
}

test("Mirror exports changing ASCII and screen/microphone audio without local monitoring", async ({
  page,
}, info) => {
  await installOutputMeter(page);
  await input(page);
  await page.goto(
    "/mirror?width=80&height=24&targetFps=15&colorMode=truecolor",
  );
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeEnabled();
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await page
    .getByRole("button", { name: "Enable microphone", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Disable microphone", exact: true }),
  ).toBeVisible();
  await record(page, "mirror", info, [440, 880]);
  expect(await page.evaluate(() => window.outputProof!.peak)).toBeLessThan(
    0.00001,
  );
  await expect(
    page.getByRole("button", { name: "Disable microphone", exact: true }),
  ).toBeVisible();
  // A second recording finalizes when capture ends and no source is restored.
  await page.getByRole("button", { name: "Record ASCII", exact: true }).click();
  await page.waitForTimeout(1200);
  await page.evaluate(() => {
    const streams = (window as unknown as { recordingInputs: MediaStream[] })
      .recordingInputs;
    const track = streams
      .find((stream) => stream.getVideoTracks().length)!
      .getVideoTracks()[0]!;
    track.stop();
    track.dispatchEvent(new Event("ended"));
  });
  await expect(
    page.getByRole("button", { name: "Record ASCII", exact: true }),
  ).toBeVisible();
});

test("mobile recording reserves header space through start, stop and download", async ({
  page,
}, info) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await input(page);
  await page.goto(
    "/mirror?width=80&height=24&targetFps=15&colorMode=truecolor",
  );
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await page
    .getByRole("button", { name: "Enable microphone", exact: true })
    .click();
  await record(page, "mobile-header", info, [440, 880]);
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThanOrEqual(390);
});

test("Mirror records muted file audio across screen replacement and source restoration", async ({
  page,
}, info) => {
  const fixture = info.outputPath("source.mp4");
  command("ffmpeg", [
    "-v",
    "error",
    "-y",
    "-f",
    "lavfi",
    "-i",
    "testsrc2=size=320x180:rate=15",
    "-f",
    "lavfi",
    "-i",
    "sine=frequency=570:sample_rate=48000",
    "-t",
    "20",
    "-c:v",
    "libx264",
    "-threads",
    "2",
    "-pix_fmt",
    "yuv420p",
    "-c:a",
    "aac",
    "-af",
    "volume=0.8",
    fixture,
  ]);
  await input(page);
  await page.goto("/mirror?colorMode=truecolor&targetFps=15");
  await page.getByRole("button", { name: "Upload file", exact: true }).click();
  await page.getByLabel("Choose media file").setInputFiles(fixture);
  await expect(
    page.getByRole("button", { name: "Record ASCII", exact: true }),
  ).toBeEnabled();
  await page
    .locator("video")
    .first()
    .evaluate((element) => {
      element.muted = true;
    });
  await record(page, "mirror-file-screen", info, [570], async () => {
    await page.waitForTimeout(2200);
    await page
      .getByRole("button", { name: "Share screen", exact: true })
      .click();
    await page.waitForTimeout(2200);
    await page
      .getByRole("button", { name: "Stop sharing", exact: true })
      .click();
  });
  const screen = decode(info.outputPath("mirror-file-screen.webm"), [880], 3);
  writeFileSync(
    info.outputPath("mirror-file-screen-membership.json"),
    JSON.stringify(screen, null, 2),
  );
});

test("navigating away stops the real encoder and releases source/capture tracks", async ({
  page,
}) => {
  await input(page);
  await page.addInitScript(() => {
    const original = MediaRecorder.prototype.start;
    const recorders: MediaRecorder[] = [];
    Object.assign(window, { activeRecorders: recorders });
    MediaRecorder.prototype.start = function (...args) {
      recorders.push(this);
      return Reflect.apply(original, this, args);
    };
  });
  await page.goto("/mirror");
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await page.getByRole("button", { name: "Record ASCII", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop recording", exact: true }),
  ).toBeVisible();
  await page.getByRole("link", { name: "Home", exact: true }).click();
  await expect
    .poll(() =>
      page.evaluate(() => {
        const state = window as unknown as {
          activeRecorders: MediaRecorder[];
          recordingInputs: MediaStream[];
        };
        return (
          state.activeRecorders.every(
            (recorder) =>
              recorder.state === "inactive" &&
              recorder.stream
                .getTracks()
                .every((track) => track.readyState === "ended"),
          ) &&
          state.recordingInputs.every((stream) =>
            stream.getTracks().every((track) => track.readyState === "ended"),
          )
        );
      }),
    )
    .toBe(true);
});

test("unsupported browser explains recording and acquires no recording graph", async ({
  page,
}) => {
  await page.addInitScript(() =>
    Object.defineProperty(window, "MediaRecorder", {
      value: undefined,
      configurable: true,
    }),
  );
  await page.goto("/mirror");
  await expect(
    page.getByText("ASCII recording is unsupported in this browser."),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Record ASCII", exact: true }),
  ).toBeDisabled();
});

for (const mode of ["client", "discovery"] as const)
  test(`${mode}: both peers export local and received audio exactly once`, async ({
    page,
    context,
  }, info) => {
    test.setTimeout(180000);
    const binary = process.env["ASCII_CHAT_TEST_BINARY"];
    test.skip(
      !binary,
      "Set ASCII_CHAT_TEST_BINARY to the native server executable",
    );
    const processes: ChildProcess[] = [];
    let output = "";
    const start = (args: string[]) => {
      const child = spawn(binary!, args, { windowsHide: true });
      processes.push(child);
      child.stdout?.on("data", (data) => {
        output += data;
      });
      child.stderr?.on("data", (data) => {
        output += data;
      });
    };
    const port = getRandomPort();
    const query = new URLSearchParams({
      width: "80",
      height: "24",
      targetFps: "15",
      encoding: "raw",
      colorMode: "truecolor",
    });
    try {
      const server = [
        "server",
        "0.0.0.0",
        "--port",
        String(port),
        "--websocket-port",
        String(port + 1),
        "--status-screen=false",
      ];
      if (mode === "discovery") {
        const acds = port + 1000;
        start([
          "discovery-service",
          "--port",
          String(acds),
          "--websocket-port",
          String(acds + 1),
          "--database",
          info.outputPath("acds.sqlite"),
          "--status-screen=false",
          "--stun-servers",
          "",
          "--turn-servers",
          "",
        ]);
        await waitForPort(acds + 1, "127.0.0.1", { timeoutMs: 20000 });
        server.push(
          "--discovery",
          "--discovery-service",
          "127.0.0.1",
          "--discovery-service-port",
          String(acds),
          "--password",
          "record-test",
          "--webrtc",
          "--stun-servers",
          "",
          "--turn-servers",
          "",
        );
        query.set("signalingUrl", `ws://127.0.0.1:${acds + 1}`);
        query.set("stunUrls", "");
        query.set("turnUrls", "");
      }
      start(server);
      await waitForPort(port + 1, "127.0.0.1", { timeoutMs: 20000 });
      if (mode === "client")
        query.set("serverUrl", `ws://127.0.0.1:${port + 1}`);
      else {
        await expect
          .poll(
            () =>
              output.match(/Session created: ([a-z]+-[a-z]+-[a-z]+)/i)?.[1] ??
              "",
            { timeout: 30000 },
          )
          .not.toBe("");
        query.set(
          "session",
          output.match(/Session created: ([a-z]+-[a-z]+-[a-z]+)/i)![1]!,
        );
      }
      const peer = await context.newPage();
      for (const [index, participant] of [page, peer].entries()) {
        await input(participant, index ? 1320 : 880);
        await participant.goto(`/${mode}?${query}`);
        if (mode === "discovery")
          await participant
            .getByLabel("Session password", { exact: true })
            .fill("record-test");
        await participant
          .getByRole("button", {
            name: mode === "client" ? "Connect" : "Join session",
            exact: true,
          })
          .click();
        await expect(
          participant.getByRole("button", { name: "Disconnect", exact: true }),
        ).toBeVisible({ timeout: 45000 });
        await participant
          .getByRole("button", { name: "Enable microphone", exact: true })
          .click();
        await participant
          .getByRole("button", { name: "Share screen", exact: true })
          .click();
        await expect(participant.getByTestId("audio-levels")).toHaveAttribute(
          "data-sent",
          /[1-9]\d*/,
        );
      }
      // No speakers are enabled on either peer; received recording is independent.
      await record(page, `${mode}-peer-a`, info, [440, 880, 660, 1320]);
      // End the call while recording: finalization and its download/error UI
      // must remain reachable without joining a session again.
      await record(
        peer,
        `${mode}-peer-b`,
        info,
        [440, 880, 660, 1320],
        undefined,
        async () => {
          await peer
            .getByRole("button", { name: "Disconnect", exact: true })
            .click();
        },
      );
      await expect(
        peer.getByRole("button", { name: "Record ASCII", exact: true }),
      ).toBeDisabled();
      await expect(
        peer.getByRole("link", { name: "Download recording", exact: true }),
      ).toBeVisible();
      for (const participant of [page, peer])
        if (
          await participant
            .getByRole("button", { name: "Disconnect", exact: true })
            .isVisible()
        )
          await participant
            .getByRole("button", { name: "Disconnect", exact: true })
            .click();
      await peer.close();
    } finally {
      writeFileSync(info.outputPath("native.log"), output);
      processes.forEach((child) => child.kill());
    }
  });
