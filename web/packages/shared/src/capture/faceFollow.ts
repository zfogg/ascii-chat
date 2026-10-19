export interface FaceFollowOptions {
  faceFollow?: boolean;
  faceFollowDebug?: boolean;
}

interface Capture {
  options: FaceFollowOptions;
  worker?: Worker | undefined;
  values: number[];
  pending: boolean;
  failed: boolean;
  lastSample: number;
  lastDraw: number;
  source?: CanvasImageSource;
  sourceIdentity?: object | string | null;
  lastMediaTime?: number;
  lastMediaTick?: number;
  idle?: ReturnType<typeof setTimeout>;
}

const captures = new WeakMap<HTMLCanvasElement, Capture>();

export function configureFaceFollow(
  canvas: HTMLCanvasElement,
  options: FaceFollowOptions,
): void {
  const capture = captures.get(canvas);
  if (capture) {
    if (options.faceFollow && !capture.options.faceFollow) {
      capture.failed = false;
      delete canvas.dataset["faceFollowError"];
    }
    capture.options = options;
    if (!options.faceFollowDebug) {
      delete canvas.dataset["faceFollow"];
      delete canvas.dataset["faceFollowTick"];
    }
  } else
    captures.set(canvas, {
      options,
      values: [0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0],
      pending: false,
      failed: false,
      lastSample: -Infinity,
      lastDraw: 0,
    });
}

export function disposeFaceFollow(canvas: HTMLCanvasElement): void {
  const capture = captures.get(canvas);
  capture?.worker?.terminate();
  if (capture) {
    capture.worker = undefined;
    capture.pending = false;
  }
  clearTimeout(capture?.idle);
  captures.delete(canvas);
}

/** Paint the original source through the shared C controller's crop, before encoding or ASCII reduction. */
export function drawFaceFollow(
  context: CanvasRenderingContext2D,
  source: CanvasImageSource,
  canvas: HTMLCanvasElement = context.canvas,
): void {
  const capture = captures.get(canvas);
  const enabled = !!capture?.options.faceFollow;
  const debug = !!capture?.options.faceFollowDebug;
  if (
    capture?.worker &&
    !enabled &&
    !debug &&
    (capture.values[2] ?? 1) > 0.9999
  ) {
    capture.worker.terminate();
    capture.worker = undefined;
    capture.pending = false;
    clearTimeout(capture.idle);
  }
  if (!capture || (!enabled && !debug && !capture.worker) || capture.failed) {
    context.drawImage(source, 0, 0, canvas.width, canvas.height);
    return;
  }
  const now = performance.now();
  capture.lastDraw = now;
  if (!capture.worker) {
    const worker = new Worker(
      new URL("./faceFollow.worker.ts", import.meta.url),
      { type: "module" },
    );
    capture.worker = worker;
    worker.onmessage = (
      event: MessageEvent<{
        type: string;
        values?: number[];
        tick?: number;
        message?: string;
      }>,
    ) => {
      if (capture.worker !== worker) return;
      if (event.data.type === "state" && event.data.values) {
        capture.values = event.data.values;
        if (capture.options.faceFollowDebug) {
          canvas.dataset["faceFollow"] = JSON.stringify(capture.values);
          canvas.dataset["faceFollowTick"] = String(event.data.tick);
        }
      } else if (event.data.type === "done") capture.pending = false;
      else if (event.data.type === "error") fail(event.data.message);
    };
    const fail = (error: unknown) => {
      if (capture.worker !== worker) return;
      console.warn("Face Follow unavailable; showing the full frame:", error);
      worker.terminate();
      capture.worker = undefined;
      capture.failed = true;
      capture.pending = false;
      clearTimeout(capture.idle);
      canvas.dataset["faceFollowError"] = String(error);
    };
    worker.onerror = (event) => fail(event.message);
    const idle = () => {
      if (capture.worker !== worker) return;
      if (performance.now() - capture.lastDraw > 2000) {
        worker.terminate();
        capture.worker = undefined;
        capture.pending = false;
        capture.values = [0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0];
      } else capture.idle = setTimeout(idle, 2000);
    };
    capture.idle = setTimeout(idle, 2000);
  }
  // Use one outstanding original-frame sample. Never accumulate stale inference work.
  if (!capture.pending && now - capture.lastSample >= 100) {
    capture.pending = true;
    capture.lastSample = now;
    // VideoFrames are short-lived objects; only persistent media elements indicate source changes.
    const stableSource = !("displayWidth" in source);
    const identity =
      source instanceof HTMLVideoElement
        ? source.currentSrc || source.srcObject
        : source;
    let reset =
      stableSource &&
      (capture.source !== source || capture.sourceIdentity !== identity);
    if (source instanceof HTMLVideoElement) {
      const previous = capture.lastMediaTime;
      const elapsed = (now - (capture.lastMediaTick ?? now)) / 1000;
      if (
        previous !== undefined &&
        (source.currentTime < previous - 0.1 ||
          source.currentTime > previous + elapsed + 0.75)
      )
        reset = true;
      capture.lastMediaTime = source.currentTime;
      capture.lastMediaTick = now;
    }
    if (stableSource) {
      capture.source = source;
      capture.sourceIdentity = identity;
    }
    const sampleWorker = capture.worker;
    void createImageBitmap(source)
      .then((bitmap) => {
        if (!capture.worker || capture.worker !== sampleWorker) {
          bitmap.close();
          capture.pending = false;
          return;
        }
        capture.worker.postMessage({ bitmap, enabled, reset }, [bitmap]);
      })
      .catch((error: unknown) => {
        capture.pending = false;
        console.warn("Face Follow could not sample this frame:", error);
      });
  }
  const [
    x = 0,
    y = 0,
    w = 1,
    h = 1,
    fx = 0,
    fy = 0,
    fw = 0,
    fh = 0,
    detected = 0,
  ] = capture.values;
  const dimensions =
    source instanceof HTMLVideoElement
      ? [source.videoWidth, source.videoHeight]
      : "displayWidth" in source
        ? [source.displayWidth, source.displayHeight]
        : source instanceof HTMLImageElement
          ? [source.naturalWidth, source.naturalHeight]
          : source instanceof SVGImageElement
            ? [source.width.baseVal.value, source.height.baseVal.value]
            : [Number(source.width), Number(source.height)];
  const [width = canvas.width, height = canvas.height] = dimensions;
  context.drawImage(
    source,
    x * width,
    y * height,
    w * width,
    h * height,
    0,
    0,
    canvas.width,
    canvas.height,
  );
  if (debug && detected) {
    const side = Math.max(fw * width, fh * height);
    context.save();
    context.strokeStyle = "#ff0000";
    context.lineWidth = Math.max(3, canvas.width / 90);
    context.strokeRect(
      (((fx + fw / 2) * width - side / 2 - x * width) / (w * width)) *
        canvas.width,
      (((fy + fh / 2) * height - side / 2 - y * height) / (h * height)) *
        canvas.height,
      (side / (w * width)) * canvas.width,
      (side / (h * height)) * canvas.height,
    );
    context.restore();
  }
}
