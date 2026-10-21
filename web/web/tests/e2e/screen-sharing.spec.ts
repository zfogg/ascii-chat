import { expect, test, type Page } from "./browser-fixture";
import { spawn, type ChildProcess } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { getRandomPort, waitForPort } from "./server-fixture";
import {
  observeAudioVisualization,
  installOutputMeter,
  selectAudioVisualization,
  moduleUrl,
} from "./audio-proof";

const manifestText = readFileSync(
  new URL("../../src/wasm/dist/manifest.json", import.meta.url),
  "utf8",
);
const manifest = JSON.parse(manifestText) as {
  sourceHash: string;
  files: Record<string, string>;
};
const artifactVersion = createHash("sha256")
  .update(manifestText)
  .digest("hex")
  .slice(0, 32);

test("rebuilt client executes full C initialization with matching versioned assets", async ({
  page,
}, info) => {
  const initLogs: string[] = [];
  const assets: Promise<{
    url: string;
    file: string;
    version: string;
    hash: string;
  }>[] = [];
  page.on("console", (message) => {
    if (message.text().includes("_client_init_with_args"))
      initLogs.push(message.text());
  });
  page.on("response", (response) => {
    const match = new URL(response.url()).pathname.match(
      /\/wasm\/([^/]+)\/(client\.(?:js|wasm))$/,
    );
    if (match)
      assets.push(
        response.body().then((body) => ({
          url: response.url(),
          version: match[1]!,
          file: match[2]!,
          hash: createHash("sha256").update(body).digest("hex"),
        })),
      );
  });
  await page.goto("/client");
  try {
    await page.evaluate(async () => {
      const path = "/src/wasm/client.ts";
      const { initClientWasm } = await import(path);
      await initClientWasm({ width: 80, height: 24 });
    });
    expect(initLogs).toContain(
      "[Client WASM] _client_init_with_args returned: 0",
    );
    const served = await Promise.all(assets);
    for (const file of ["client.js", "client.wasm"]) {
      expect(served).toContainEqual(
        expect.objectContaining({
          file,
          version: artifactVersion,
          hash: manifest.files[file],
        }),
      );
    }
  } finally {
    const evidence = JSON.stringify(
      {
        sourceHash: manifest.sourceHash,
        artifactVersion,
        served: await Promise.all(assets),
        initLogs,
      },
      null,
      2,
    );
    const proofPath = info.outputPath("client-init-artifacts.json");
    writeFileSync(proofPath, evidence);
    await info.attach("client-init-artifacts.json", {
      path: proofPath,
      contentType: "application/json",
    });
  }
});

interface DisplayTest {
  streams: MediaStream[];
  cancel: boolean;
  calls: unknown[];
  microphones: MediaStreamTrack[];
  split: boolean;
}
declare global {
  interface Window {
    displayTest: DisplayTest;
    retainedMicrophone?: MediaStreamTrack | undefined;
  }
}

// Replace only the picker. Real MediaStreams, C/WASM, uploads and visible output run normally.
async function installPicker(page: Page) {
  await page.addInitScript(() => {
    const state: DisplayTest = {
      streams: [],
      cancel: false,
      calls: [],
      microphones: [],
      split: false,
    };
    const getUserMedia = navigator.mediaDevices.getUserMedia.bind(
      navigator.mediaDevices,
    );
    navigator.mediaDevices.getUserMedia = async (constraints) => {
      const stream = await getUserMedia(constraints);
      state.microphones.push(...stream.getAudioTracks());
      return stream;
    };
    window.displayTest = state;
    navigator.mediaDevices.getDisplayMedia = async (options) => {
      state.calls.push(options);
      if (state.cancel) {
        state.cancel = false;
        throw new DOMException("cancelled", "NotAllowedError");
      }
      const canvas = document.createElement("canvas");
      canvas.width = 640;
      canvas.height = 360;
      const context = canvas.getContext("2d")!;
      const draw = () => {
        context.fillStyle = "red";
        context.fillRect(0, 0, 640, 360);
        if (state.split) {
          context.fillStyle = "cyan";
          context.fillRect(320, 0, 320, 360);
        }
        context.fillStyle = "white";
        context.font = "bold 90px monospace";
        context.fillText("SCREEN", 40, 170);
        context.fillRect((Date.now() / 15) % 550, 230, 60, 60);
      };
      draw();
      const stream = canvas.captureStream(30);
      const audio = new AudioContext({ sampleRate: 48000 });
      await audio.resume();
      const oscillator = audio.createOscillator();
      oscillator.frequency.value = 880;
      const volume = audio.createGain();
      volume.gain.value = 0.18;
      const envelope = audio.createOscillator();
      envelope.frequency.value = 2;
      const envelopeDepth = audio.createGain();
      envelopeDepth.gain.value = 0.1;
      envelope.connect(envelopeDepth).connect(volume.gain);
      const sweep = audio.createOscillator();
      sweep.frequency.value = 1.5;
      const sweepDepth = audio.createGain();
      sweepDepth.gain.value = 500;
      sweep.connect(sweepDepth).connect(oscillator.frequency);
      const destination = audio.createMediaStreamDestination();
      oscillator.connect(volume).connect(destination);
      oscillator.start();
      envelope.start();
      sweep.start();
      destination.stream
        .getAudioTracks()
        .forEach((track) => stream.addTrack(track));
      const videoTrack = stream.getVideoTracks()[0]!;
      const stop = videoTrack.stop.bind(videoTrack);
      let stopped = false;
      videoTrack.stop = () => {
        if (!stopped) {
          stopped = true;
          oscillator.stop();
          envelope.stop();
          sweep.stop();
          void audio.close();
        }
        stop();
      };
      const timer = setInterval(draw, 33);
      stream
        .getVideoTracks()[0]!
        .addEventListener("ended", () => clearInterval(timer));
      state.streams.push(stream);
      return stream;
    };
  });
}
async function redPixels(page: Page) {
  return page.locator("canvas.ascii-canvas").evaluate((element) => {
    const canvas = element as HTMLCanvasElement;
    const pixels = canvas
      .getContext("2d")!
      .getImageData(0, 0, canvas.width, canvas.height).data;
    let red = 0;
    for (let i = 0; i < pixels.length; i += 4)
      if (
        pixels[i]! > 50 &&
        pixels[i]! > pixels[i + 1]! * 1.5 &&
        pixels[i]! > pixels[i + 2]! * 1.5
      )
        red++;
    return red;
  });
}

async function leftColorBalance(page: Page) {
  return page.locator("canvas.ascii-canvas").evaluate((element) => {
    const canvas = element as HTMLCanvasElement;
    const pixels = canvas
      .getContext("2d")!
      .getImageData(0, 0, canvas.width, canvas.height).data;
    let balance = 0;
    for (let y = 0; y < canvas.height; y++)
      for (let x = 0; x < canvas.width / 2; x++) {
        const i = (y * canvas.width + x) * 4;
        if (pixels[i]! > 80 && pixels[i]! > pixels[i + 2]! * 1.5) balance++;
        if (pixels[i + 2]! > 80 && pixels[i + 2]! > pixels[i]! * 1.5) balance--;
      }
    return balance;
  });
}

async function litPixels(page: Page) {
  return page.locator("canvas.ascii-canvas").evaluate((element) => {
    const canvas = element as HTMLCanvasElement;
    const pixels = canvas
      .getContext("2d")!
      .getImageData(0, 0, canvas.width, canvas.height).data;
    let lit = 0;
    for (let i = 0; i < pixels.length; i += 4)
      if (Math.max(pixels[i]!, pixels[i + 1]!, pixels[i + 2]!) > 60) lit++;
    return lit;
  });
}

async function fullPageProof(page: Page, file: string, screenVideo = true) {
  await page.evaluate(() => window.scrollTo(0, 0));
  // Full-page capture can expand the viewport. Hold the live renderer geometry
  // for the screenshot so its resize callback cannot clear the canvas mid-shot.
  const surface = page.locator(".ascii-canvas-container");
  const previous = await surface.getAttribute("style");
  await surface.evaluate((element) => {
    const height = element.getBoundingClientRect().height;
    const style = (element as HTMLElement).style;
    style.flex = `0 0 ${height}px`;
    style.height = `${height}px`;
    style.maxHeight = `${height}px`;
  });
  if (screenVideo)
    await expect.poll(() => redPixels(page)).toBeGreaterThan(1000);
  else await expect.poll(() => litPixels(page)).toBeGreaterThan(1000);
  const upload = page.getByRole("button", { name: "Upload file", exact: true });
  const share = page.getByRole("button", { name: "Share screen", exact: true });
  await expect(share).toHaveClass(/bg-terminal-5/);
  expect(
    await share.evaluate((button) =>
      button.parentElement?.previousElementSibling?.textContent?.trim(),
    ),
  ).toBe("Upload file");
  const uploadBox = await upload.boundingBox(),
    shareBox = await share.boundingBox();
  expect(shareBox!.x).toBeGreaterThanOrEqual(uploadBox!.x + uploadBox!.width);
  // Live settings reflow the bar; wait for its layout transition to settle.
  await expect
    .poll(async () => {
      const shareRect = await share.boundingBox(),
        uploadRect = await upload.boundingBox();
      return Math.abs(
        shareRect!.y +
          shareRect!.height / 2 -
          uploadRect!.y -
          uploadRect!.height / 2,
      );
    })
    .toBeLessThan(2);
  // Verify the actual PNG, not just the canvas before capture: full-page
  // viewport changes can otherwise clear a correctly rendered frame mid-shot.
  const canvasRect = await page.locator("canvas.ascii-canvas").boundingBox();
  await expect
    .poll(
      async () => {
        const screenshot = await page.screenshot({
          path: file,
          fullPage: true,
        });
        return page.evaluate(
          async ({ png, rect }) => {
            const image = new Image();
            image.src = `data:image/png;base64,${png}`;
            await image.decode();
            const copy = document.createElement("canvas");
            copy.width = image.width;
            copy.height = image.height;
            const context = copy.getContext("2d")!;
            context.drawImage(image, 0, 0);
            const pixels = context.getImageData(
              Math.floor(rect!.x),
              Math.floor(rect!.y),
              Math.floor(rect!.width),
              Math.floor(rect!.height),
            ).data;
            let lit = 0;
            for (let i = 0; i < pixels.length; i += 4)
              if (Math.max(pixels[i]!, pixels[i + 1]!, pixels[i + 2]!) > 60)
                lit++;
            return lit;
          },
          { png: screenshot.toString("base64"), rect: canvasRect },
        );
      },
      { timeout: 10000 },
    )
    .toBeGreaterThan(1000);
  await surface.evaluate((element, style) => {
    if (style === null) element.removeAttribute("style");
    else element.setAttribute("style", style);
  }, previous);
}

test("Mirror renders display ASCII, cancels, replaces, ends and restores the microphone", async ({
  page,
}, info) => {
  await installPicker(page);
  await page.goto("/mirror?colorMode=truecolor");
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeEnabled({ timeout: 20000 });
  await page
    .getByRole("button", { name: "Enable microphone", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Disable microphone", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Start Webcam", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop sharing", exact: true }),
  ).toBeVisible();
  expect(
    await page
      .locator("video[autoplay]")
      .evaluate(
        (video) =>
          (video as HTMLVideoElement).srcObject instanceof MediaStream &&
          (
            (video as HTMLVideoElement).srcObject as MediaStream
          ).getAudioTracks().length,
      ),
  ).toBe(1);
  await expect.poll(() => redPixels(page)).toBeGreaterThan(1000);
  await fullPageProof(page, info.outputPath("mirror-screen-ascii.png"));
  const microphone = await page.evaluate(() => {
    window.retainedMicrophone = window.displayTest.microphones.at(-1);
    return window.retainedMicrophone?.id;
  });
  await page.evaluate(() => {
    window.displayTest.cancel = true;
  });
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeEnabled();
  await expect(
    page.getByRole("button", { name: "Stop sharing", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect
    .poll(() => page.evaluate(() => window.displayTest.streams.length))
    .toBe(2);
  await expect
    .poll(() =>
      page.evaluate(
        () => window.displayTest.streams[0]!.getVideoTracks()[0]!.readyState,
      ),
    )
    .toBe("ended");
  await page.getByRole("button", { name: "Stop sharing", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop sharing", exact: true }),
  ).not.toBeVisible();
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeVisible();
  await expect
    .poll(() =>
      page.evaluate(() => {
        const video = document.querySelector(
          "video[autoplay]",
        ) as HTMLVideoElement;
        return (video.srcObject as MediaStream)?.getVideoTracks()[0]
          ?.readyState;
      }),
    )
    .toBe("live");
  expect(await page.evaluate(() => window.retainedMicrophone?.readyState)).toBe(
    "live",
  );
  expect(microphone).toBeTruthy();
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop sharing", exact: true }),
  ).toBeVisible();
  await page
    .getByLabel("ASCII terminal. Press question mark for keyboard settings.")
    .focus();
  await page.keyboard.press("[");
  await expect(
    page.getByRole("button", { name: "Stop sharing", exact: true }),
  ).not.toBeVisible();
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop sharing", exact: true }),
  ).toBeVisible();
  await page.evaluate(() => {
    const track = window.displayTest.streams.at(-1)!.getVideoTracks()[0]!;
    track.stop();
    track.dispatchEvent(new Event("ended"));
  });
  await expect(
    page.getByRole("button", { name: "Stop sharing", exact: true }),
  ).not.toBeVisible();
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Stop", exact: true }).click();
});

test("Mirror shares display video when microphone permission is denied", async ({
  page,
}) => {
  await installPicker(page);
  await page.addInitScript(() => {
    navigator.mediaDevices.getUserMedia = async () => {
      throw new DOMException("No microphone", "NotAllowedError");
    };
  });
  await page.goto("/mirror");
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop sharing", exact: true }),
  ).toBeVisible();
  await expect.poll(() => redPixels(page)).toBeGreaterThan(1000);
  expect(
    await page
      .locator("video[autoplay]")
      .evaluate(
        (element) =>
          (
            (element as HTMLVideoElement).srcObject as MediaStream
          ).getAudioTracks().length,
      ),
  ).toBe(1);
  await page.getByRole("button", { name: "Stop sharing", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Start Webcam", exact: true }),
  ).toBeVisible();
});

test("Mirror screen sharing from idle returns to idle, and file sharing restores the file", async ({
  page,
}) => {
  await installPicker(page);
  await page.goto("/mirror");
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeEnabled({ timeout: 20000 });
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop sharing", exact: true }),
  ).toBeVisible();
  await expect.poll(() => redPixels(page)).toBeGreaterThan(1000);
  await page.getByRole("button", { name: "Stop sharing", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Start Webcam", exact: true }),
  ).toBeVisible();
  const image = await page.evaluate(() => {
    const canvas = document.createElement("canvas");
    canvas.width = 320;
    canvas.height = 180;
    const ctx = canvas.getContext("2d")!;
    ctx.fillStyle = "blue";
    ctx.fillRect(0, 0, 320, 180);
    return canvas.toDataURL().split(",")[1]!;
  });
  await page.getByRole("button", { name: "Upload file", exact: true }).click();
  await page.getByLabel("Choose media file").setInputFiles({
    name: "blue.png",
    mimeType: "image/png",
    buffer: Buffer.from(image, "base64"),
  });
  await expect(
    page.getByRole("button", { name: "Toggle Media", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop sharing", exact: true }),
  ).toBeVisible();
  await expect.poll(() => redPixels(page)).toBeGreaterThan(1000);
  await page.getByRole("button", { name: "Stop sharing", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Toggle Media", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  await expect.poll(() => redPixels(page)).toBeLessThan(100);
});

test("C resolves source orientation and transforms upload pixels once while preserving local input", async ({
  page,
}) => {
  await page.goto("/mirror");
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeEnabled({ timeout: 20000 });
  const proof = await page.evaluate(async (url) => {
    const { getMirrorModule } = await import(url);
    const { runtimeOptions } = await import(
      url.replace("mirror.ts", "common/runtimeOptions.ts")
    );
    const uploadModuleUrl = "/src/utils/orientUpload.ts";
    const { orientUpload } = await import(uploadModuleUrl);
    const module = getMirrorModule();
    const store = runtimeOptions(module);
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = 2;
    const context = canvas.getContext("2d")!;
    const original = new Uint8Array([
      10, 0, 0, 255, 20, 0, 0, 255, 30, 0, 0, 255, 40, 0, 0, 255,
    ]);
    const results = [];
    for (const patch of [
      { media_input: true },
      { media_input: false },
      { flip_x: false },
      { media_input: true },
      { flip_x: true },
      { flip_y: true },
    ]) {
      store.applyPatch(patch);
      context.putImageData(
        new ImageData(new Uint8ClampedArray(original), 2, 2),
        0,
        0,
      );
      const frame = { data: original, width: 2, height: 2 };
      const upload = orientUpload(canvas, frame);
      results.push({
        required: module._wasm_capture_transform_required(),
        pixels: Array.from(upload.data as Uint8Array),
        local: Array.from(frame.data),
        flip: store.getSnapshot().values["flip_x"],
      });
    }
    return {
      original: Array.from(original),
      results,
      invalid: module._wasm_capture_transform_rgba(0, 2, 2),
    };
  }, moduleUrl);
  const reversedRows = [
    20, 0, 0, 255, 10, 0, 0, 255, 40, 0, 0, 255, 30, 0, 0, 255,
  ];
  expect(proof.results.map((result: { flip: boolean }) => result.flip)).toEqual(
    [false, true, false, false, true, true],
  );
  expect(
    proof.results.map((result: { pixels: number[] }) => result.pixels),
  ).toEqual([
    proof.original,
    reversedRows,
    proof.original,
    proof.original,
    reversedRows,
    [40, 0, 0, 255, 30, 0, 0, 255, 20, 0, 0, 255, 10, 0, 0, 255],
  ]);
  expect(
    proof.results.every(
      (result: { local: number[] }) =>
        JSON.stringify(result.local) === JSON.stringify(proof.original),
    ),
  ).toBe(true);
  expect(proof.invalid).toBe(-1);
});

test("Mirror visualizes captured audio in waveform and FFT while its enabled speakers remain silent", async ({
  page,
}, info) => {
  test.setTimeout(90000);
  await installPicker(page);
  await installOutputMeter(page);
  await page.goto("/mirror?colorMode=truecolor");
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeEnabled({ timeout: 20000 });
  await observeAudioVisualization(page);
  await page
    .getByRole("button", { name: "Enable speakers", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Disable speakers", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop sharing", exact: true }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Enable microphone", exact: true }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Switch to webcam", exact: true }),
  ).not.toBeVisible();
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).not.toBeVisible();
  const evidence = [];
  for (const animation of ["waveform", "fft"] as const) {
    await selectAudioVisualization(page, animation);
    await expect.poll(() => litPixels(page)).toBeGreaterThan(1000);
    await page
      .locator("canvas.ascii-canvas")
      .screenshot({ path: info.outputPath(`mirror-${animation}-canvas.png`) });
    await page.screenshot({
      path: info.outputPath(`mirror-${animation}-viewport.png`),
    });
    const loudFrame = await page.evaluate(() => window.audioProof!.lastFrame);
    await fullPageProof(
      page,
      info.outputPath(`mirror-${animation}.png`),
      false,
    );
    await page.evaluate(() =>
      window.displayTest.streams
        .at(-1)!
        .getAudioTracks()
        .forEach((track) => {
          track.enabled = false;
        }),
    );
    await expect
      .poll(() => page.evaluate(() => window.audioProof!.inputRms))
      .toBeLessThan(0.001);
    await expect
      .poll(() => page.evaluate(() => window.audioProof!.lastFrame))
      .not.toBe(loudFrame);
    const quietFrame = await page.evaluate(() => window.audioProof!.lastFrame);
    await page.evaluate(() =>
      window.displayTest.streams
        .at(-1)!
        .getAudioTracks()
        .forEach((track) => {
          track.enabled = true;
        }),
    );
    await expect
      .poll(() => page.evaluate(() => window.audioProof!.inputRms))
      .toBeGreaterThan(0.03);
    await expect
      .poll(() => page.evaluate(() => window.audioProof!.lastFrame))
      .not.toBe(quietFrame);
    evidence.push({
      animation,
      capture: await page.evaluate(() => window.audioProof),
      output: await page.evaluate(() => window.outputProof),
    });
  }
  await expect
    .poll(() => page.evaluate(() => window.outputProof!.graphs))
    .toBeGreaterThan(0);
  expect(await page.evaluate(() => window.outputProof!.peak)).toBeLessThan(
    0.00001,
  );
  await expect(page.getByTestId("audio-levels")).toHaveAttribute(
    "data-played",
    "0",
  );
  await page.getByRole("button", { name: "Stop sharing", exact: true }).click();
  expect(
    await page.evaluate(() =>
      window.displayTest.streams.every((stream) =>
        stream.getTracks().every((track) => track.readyState === "ended"),
      ),
    ),
  ).toBe(true);
  writeFileSync(
    info.outputPath("mirror-audio-evidence.json"),
    JSON.stringify(evidence, null, 2),
  );
});

for (const mode of ["client", "discovery"]) {
  test(`${mode}: display uploads through live server and preserves microphone`, async ({
    page,
  }, info) => {
    test.setTimeout(180000);
    const binary = process.env["ASCII_CHAT_TEST_BINARY"];
    test.skip(
      !binary,
      "Set ASCII_CHAT_TEST_BINARY to a compatible native executable",
    );
    const processes: ChildProcess[] = [];
    const clientInitLogs: string[] = [];
    const uploadEncodingLogs: string[] = [];
    const clientAssets: string[] = [];
    page.on("console", (message) => {
      if (message.text().includes("[Client] Upload encoding:"))
        uploadEncodingLogs.push(message.text());
      if (
        message.type() === "error" ||
        message.text().includes("track reader stopped")
      )
        console.log(message.text());
      if (message.text().includes("_client_init_with_args"))
        clientInitLogs.push(message.text());
    });
    page.on("request", (request) => {
      if (
        /\/wasm\/[^/]+\/client\.(js|wasm)$/.test(
          new URL(request.url()).pathname,
        )
      )
        clientAssets.push(request.url());
    });
    let output = "";
    const start = (args: string[]) => {
      const child = spawn(binary!, args, { windowsHide: true });
      processes.push(child);
      child.on("exit", (code, signal) => {
        output += `\nEXIT ${args[0]} code=${code} signal=${signal}\n`;
      });
      child.stdout?.on("data", (data) => {
        output += data;
      });
      child.stderr?.on("data", (data) => {
        output += data;
      });
    };
    const port = getRandomPort();
    const query = new URLSearchParams({
      flipX: "false",
      colorMode: "truecolor",
      encoding: process.env["ASCII_CHAT_SCREEN_ENCODING"] ?? "raw",
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
          "screen-test",
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
      await installPicker(page);
      await page.goto(`/${mode}?${query}`);
      if (mode === "discovery")
        await page
          .getByLabel("Session password", { exact: true })
          .fill("screen-test");
      await page
        .getByRole("button", {
          name: mode === "client" ? "Connect" : "Join session",
          exact: true,
        })
        .click();
      await expect(
        page.getByRole("button", { name: "Disconnect", exact: true }),
      ).toBeVisible({ timeout: 45000 });
      expect(clientInitLogs).toContain(
        "[Client WASM] _client_init_with_args returned: 0",
      );
      for (const file of ["client.js", "client.wasm"])
        expect(
          clientAssets.some(
            (url) =>
              new URL(url).pathname === `/wasm/${artifactVersion}/${file}`,
          ),
        ).toBe(true);
      const initProofPath = info.outputPath(`${mode}-client-init.json`);
      writeFileSync(
        initProofPath,
        JSON.stringify(
          {
            sourceHash: manifest.sourceHash,
            artifactVersion,
            clientInitLogs,
            uploadEncodingLogs,
            clientAssets,
          },
          null,
          2,
        ),
      );
      await info.attach(`${mode}-client-init.json`, {
        path: initProofPath,
        contentType: "application/json",
      });
      await page
        .getByRole("button", { name: "Enable speakers", exact: true })
        .click();
      await page
        .getByRole("button", { name: "Enable microphone", exact: true })
        .click();
      await expect(
        page.getByRole("button", { name: "Disable microphone", exact: true }),
      ).toBeVisible();
      const before = await page.evaluate(
        () => window.__clientFrameMetrics?.received ?? 0,
      );
      await page
        .getByRole("button", { name: "Share screen", exact: true })
        .click();
      await expect(
        page.getByRole("button", { name: "Stop sharing", exact: true }),
      ).toBeVisible();
      await expect
        .poll(() =>
          uploadEncodingLogs.some((line) =>
            line.includes(`Upload encoding: ${query.get("encoding")}`),
          ),
        )
        .toBe(true);
      await expect(
        page.getByRole("button", { name: "Switch to webcam", exact: true }),
      ).not.toBeVisible();
      await expect
        .poll(
          () => page.evaluate(() => window.__clientFrameMetrics?.received ?? 0),
          { timeout: 20000 },
        )
        .toBeGreaterThan(before + 3);
      await expect
        .poll(() => redPixels(page), { timeout: 20000 })
        .toBeGreaterThan(1000);
      await expect(
        page.getByRole("button", { name: "Disable microphone", exact: true }),
      ).toBeVisible();
      await expect(page.getByTestId("audio-levels")).toHaveAttribute(
        "data-sent",
        /[1-9]\d*/,
      );
      const observer = await page.context().newPage();
      await observer.goto(`/${mode}?${query}`);
      if (mode === "discovery")
        await observer
          .getByLabel("Session password", { exact: true })
          .fill("screen-test");
      await observer
        .getByRole("button", {
          name: mode === "client" ? "Connect" : "Join session",
          exact: true,
        })
        .click();
      await expect(
        observer.getByRole("button", { name: "Disconnect", exact: true }),
      ).toBeVisible({ timeout: 45000 });
      await expect(
        observer.getByRole("button", { name: "Disable webcam", exact: true }),
      ).toBeVisible();
      await observer
        .getByRole("button", { name: "Disable webcam", exact: true })
        .click();
      await expect
        .poll(() => redPixels(observer), { timeout: 20000 })
        .toBeGreaterThan(1000);
      // Asymmetric captured pixels prove C applies the sender preference once
      // for both its returned view and the independent receiving participant.
      await page.evaluate(() => {
        window.displayTest.split = true;
      });
      await expect
        .poll(() => leftColorBalance(observer), { timeout: 20000 })
        .toBeGreaterThan(500);
      await page.getByRole("button", { name: "Settings", exact: true }).click();
      const horizontal = page.getByRole("button", { name: /Flip Horizontal/ });
      await horizontal.click();
      await expect(horizontal).toHaveAttribute("aria-pressed", "true");
      await expect
        .poll(() => leftColorBalance(observer), { timeout: 20000 })
        .toBeLessThan(-500);
      await expect
        .poll(() => leftColorBalance(page), { timeout: 20000 })
        .toBeLessThan(-500);
      await horizontal.click();
      await expect(horizontal).toHaveAttribute("aria-pressed", "false");
      await page.getByRole("button", { name: "Settings", exact: true }).click();
      await expect
        .poll(() => leftColorBalance(observer), { timeout: 20000 })
        .toBeGreaterThan(500);
      await page.evaluate(() => {
        window.displayTest.split = false;
      });
      await fullPageProof(
        observer,
        info.outputPath(`${mode}-peer-screen-ascii.png`),
      );
      await fullPageProof(page, info.outputPath(`${mode}-screen-ascii.png`));
      // With the sender microphone off, the only transmitted signal is the
      // captured 880 Hz display track. The receiver decodes it through C/Opus.
      await page
        .getByRole("button", { name: "Disable microphone", exact: true })
        .click();
      await observer
        .getByRole("button", { name: "Enable speakers", exact: true })
        .click();
      await expect
        .poll(
          async () =>
            Number(
              await observer
                .getByTestId("audio-levels")
                .getAttribute("data-playback-level"),
            ),
          { timeout: 20000 },
        )
        .toBeGreaterThan(0.03);
      await observeAudioVisualization(page);
      await observeAudioVisualization(observer);
      const audioEvidence = [];
      for (const animation of ["waveform", "fft"] as const) {
        await selectAudioVisualization(page, animation);
        await selectAudioVisualization(observer, animation);
        await fullPageProof(
          page,
          info.outputPath(`${mode}-sender-${animation}.png`),
          false,
        );
        await fullPageProof(
          observer,
          info.outputPath(`${mode}-receiver-${animation}.png`),
          false,
        );
        audioEvidence.push({
          animation,
          sender: await page.evaluate(() => window.audioProof),
          receiver: await observer.evaluate(() => window.audioProof),
        });
      }
      const quiet = await observer.evaluate(() => window.audioProof!.lastFrame);
      await page.evaluate(() =>
        window.displayTest.streams
          .at(-1)!
          .getAudioTracks()
          .forEach((track) => {
            track.enabled = false;
          }),
      );
      await expect
        .poll(
          async () =>
            Number(
              await observer
                .getByTestId("audio-levels")
                .getAttribute("data-playback-level"),
            ),
          { timeout: 20000 },
        )
        .toBeLessThan(0.001);
      await expect
        .poll(() => observer.evaluate(() => window.audioProof!.lastFrame))
        .not.toBe(quiet);
      const silentFrame = await observer.evaluate(
        () => window.audioProof!.lastFrame,
      );
      await page.evaluate(() =>
        window.displayTest.streams
          .at(-1)!
          .getAudioTracks()
          .forEach((track) => {
            track.enabled = true;
          }),
      );
      await expect
        .poll(
          async () =>
            Number(
              await observer
                .getByTestId("audio-levels")
                .getAttribute("data-playback-level"),
            ),
          { timeout: 20000 },
        )
        .toBeGreaterThan(0.03);
      await expect
        .poll(() => observer.evaluate(() => window.audioProof!.lastFrame))
        .not.toBe(silentFrame);
      writeFileSync(
        info.outputPath(`${mode}-audio-evidence.json`),
        JSON.stringify(audioEvidence, null, 2),
      );
      await page
        .getByRole("button", { name: "Enable microphone", exact: true })
        .click();
      await expect(
        page.getByRole("button", { name: "Disable microphone", exact: true }),
      ).toBeVisible();
      await observer.close();
      await page
        .getByRole("button", { name: "Stop sharing", exact: true })
        .click();
      await expect(
        page.getByRole("button", { name: "Share screen", exact: true }),
      ).toBeVisible();
      await expect(
        page.getByRole("button", { name: "Disable microphone", exact: true }),
      ).toBeVisible();
      await page
        .getByRole("button", { name: "Disconnect", exact: true })
        .click();
      expect(
        await page.evaluate(() =>
          window.displayTest.streams.every((stream) =>
            stream
              .getVideoTracks()
              .every((track) => track.readyState === "ended"),
          ),
        ),
      ).toBe(true);
    } finally {
      writeFileSync(info.outputPath(`${mode}-native.log`), output);
      processes.forEach((child) => child.kill());
    }
  });
}
