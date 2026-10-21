import { test, expect } from "./browser-fixture";
import { writeFile } from "node:fs/promises";

test("real browser upload reduces load on collapse and recovers with fresh decoded video and continuous audio", async ({
  page,
}, testInfo) => {
  test.setTimeout(60000);
  await page.goto("/tests/network/crypto-policy.html");
  const result = await page.evaluate(async () => {
    const encoderUrl = "/src/network/VideoUploadEncoder.ts";
    const queueUrl = "/src/network/OutboundQueue.ts";
    const { VideoUploadEncoder } = (await import(
      /* @vite-ignore */ encoderUrl
    )) as typeof import("../../src/network/VideoUploadEncoder");
    const { OutboundQueue } = (await import(
      /* @vite-ignore */ queueUrl
    )) as typeof import("../../src/network/OutboundQueue");
    document.body.innerHTML =
      '<h1>Adaptive browser upload</h1><canvas id="received" width="640" height="360"></canvas><pre id="metrics"></pre>';
    const canvas = new OffscreenCanvas(640, 360);
    const context = canvas.getContext("2d")!;
    const display = document.querySelector("canvas")!.getContext("2d")!;
    const start = performance.now();
    const elapsed = () => performance.now() - start;
    let inFlight: Uint8Array | null = null,
      remaining = 0;
    let audioReceived = 0,
      lastAudio = 0,
      maxAudioGap = 0,
      decoded = 0,
      recentAge = 0;
    const errors: string[] = [];
    const samples: {
      time: number;
      width: number | undefined;
      fps: number | undefined;
      bitrate: number | undefined;
      reason: string;
      decoded: number;
      age: number;
      audio: number;
    }[] = [];
    const decoder = new VideoDecoder({
      output(frame) {
        decoded++;
        recentAge = performance.now() - frame.timestamp / 1000;
        display.drawImage(frame, 0, 0, 640, 360);
        frame.close();
      },
      error(error) {
        errors.push(error.message);
      },
    });
    function receive(packet: Uint8Array) {
      const view = new DataView(packet.buffer);
      if (view.getUint16(8) === 4001) {
        audioReceived++;
        const now = elapsed();
        if (lastAudio) maxAudioGap = Math.max(maxAudioGap, now - lastAudio);
        lastAudio = now;
      } else {
        if (packet[22]! & 2)
          decoder.configure({
            codec: "avc1.42E01F",
            codedWidth: view.getUint32(31),
            codedHeight: view.getUint32(35),
            optimizeForLatency: true,
          });
        decoder.decode(
          new EncodedVideoChunk({
            type: packet[22]! & 1 ? "key" : "delta",
            timestamp: view.getFloat64(23),
            data: packet.subarray(39),
          }),
        );
      }
    }
    const queue = new OutboundQueue(
      () => remaining,
      (packet) => {
        inFlight = packet;
        remaining = packet.byteLength;
      },
      (error) => errors.push(error.message),
    );
    const encoder = new VideoUploadEncoder((chunk) => {
      const packet = new Uint8Array(39 + chunk.data.length);
      const view = new DataView(packet.buffer);
      view.setUint16(8, 3003);
      packet[22] = chunk.flags;
      view.setFloat64(23, chunk.timestamp!);
      view.setUint32(31, chunk.width);
      view.setUint32(35, chunk.height);
      packet.set(chunk.data, 39);
      queue.submit(packet);
    }, "h264");
    await encoder.initialize(640, 360, 30);
    let previousTick = performance.now();
    const link = setInterval(() => {
      const now = performance.now();
      const bps = elapsed() >= 4000 && elapsed() < 14000 ? 120000 : 3000000;
      remaining = Math.max(
        0,
        remaining - ((bps / 8) * (now - previousTick)) / 1000,
      );
      previousTick = now;
      if (!remaining && inFlight) {
        const packet = inFlight;
        inFlight = null;
        receive(packet);
      }
      queue.flush();
    }, 5);
    let counter = 0;
    const capture = setInterval(() => {
      const snapshot = queue.snapshot();
      encoder.observeTransport({
        bufferedBytes: snapshot.applicationBytes + snapshot.backendBytes,
        oldestFrameAgeMs: snapshot.oldestFrameAgeMs,
        droppedFrames: snapshot.dropped,
      });
      context.fillStyle = `hsl(${(counter++ * 7) % 360} 80% 40%)`;
      context.fillRect(0, 0, 640, 360);
      for (let i = 0; i < 80; i++) {
        context.fillStyle = `hsl(${i * 17 + counter} 80% 60%)`;
        context.fillRect((i * 71 + counter * 11) % 640, (i * 37) % 360, 25, 25);
      }
      const frame = new VideoFrame(canvas, {
        timestamp: Math.round(performance.now() * 1000),
      });
      try {
        encoder.encode(frame, 30, queue.needsKeyframe(3003));
      } catch (error) {
        errors.push(String(error));
      } finally {
        frame.close();
      }
    }, 1000 / 30);
    const audio = setInterval(() => {
      const packet = new Uint8Array(150);
      new DataView(packet.buffer).setUint16(8, 4001);
      queue.submit(packet);
    }, 20);
    const monitor = setInterval(() => {
      const status = encoder.qualityStatus;
      samples.push({
        time: elapsed(),
        width: status.width,
        fps: status.framerate,
        bitrate: status.bitrate,
        reason: status.reason,
        decoded,
        age: recentAge,
        audio: audioReceived,
      });
      document.querySelector("#metrics")!.textContent = JSON.stringify(
        { ...samples.at(-1), maxAudioGap },
        null,
        2,
      );
    }, 500);
    await new Promise((resolve) => setTimeout(resolve, 34000));
    [link, capture, audio, monitor].forEach(clearInterval);
    encoder.destroy();
    queue.close();
    decoder.close();
    return { samples, errors, maxAudioGap, audioReceived, decoded };
  });
  const evidencePath = testInfo.outputPath("collapse-recovery.json");
  await writeFile(evidencePath, JSON.stringify(result, null, 2));
  await testInfo.attach("collapse-recovery.json", {
    path: evidencePath,
    contentType: "application/json",
  });
  await page.screenshot({ path: testInfo.outputPath("adaptive-upload.png") });
  expect(result.errors).toEqual([]);
  const initial = result.samples.find((sample) => sample.time > 2000)!;
  const collapse = result.samples.filter(
    (sample) => sample.time > 9000 && sample.time < 14000,
  );
  expect(collapse.length).toBeGreaterThan(0);
  const low = collapse.reduce((a, b) => (a.bitrate! < b.bitrate! ? a : b));
  const recovered = result.samples.at(-1)!;
  expect(low.bitrate!).toBeLessThan(initial.bitrate!);
  expect(low.width!).toBeLessThan(initial.width!);
  expect(low.fps!).toBeLessThan(initial.fps!);
  expect(recovered.bitrate!).toBeGreaterThan(low.bitrate!);
  expect(recovered.decoded).toBeGreaterThan(low.decoded + 50);
  expect(recovered.age).toBeLessThan(250);
  expect(result.audioReceived).toBeGreaterThan(1000);
  expect(result.maxAudioGap).toBeLessThan(1500);
});
