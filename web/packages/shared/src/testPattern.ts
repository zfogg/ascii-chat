export interface TestPatternVideoSource {
  stream: MediaStream;
  resize: (width: number, height: number) => void;
  stop: () => void;
}

export interface TestPatternAudioSource {
  stream: MediaStream;
  stop: () => void;
}

export type TestPatternMode = "none" | "test" | "test2";

export function getTestPatternMode(
  search = window.location.search,
): TestPatternMode {
  const params = new URLSearchParams(search);
  if (params.has("test2")) return "test2";
  return params.has("test") ? "test" : "none";
}

export function isTestMode(search = window.location.search): boolean {
  return getTestPatternMode(search) !== "none";
}

export function drawTestPatternFrame(
  context: CanvasRenderingContext2D,
  width: number,
  height: number,
  time = performance.now(),
): void {
  const phase = (time % 10000) / 10000;
  const gradient = context.createLinearGradient(0, 0, width, height);
  gradient.addColorStop(0, `hsl(${phase * 360}, 100%, 50%)`);
  gradient.addColorStop(1, `hsl(${((phase + 0.5) % 1) * 360}, 100%, 50%)`);
  context.fillStyle = gradient;
  context.fillRect(0, 0, width, height);

  const boxSize = Math.max(32, Math.min(width, height) / 4);
  const x = ((time / 11) % (width + boxSize)) - boxSize;
  const y = ((time / 17) % (height + boxSize)) - boxSize;
  context.fillStyle = "rgba(255, 255, 255, 0.8)";
  context.fillRect(x, y, boxSize, boxSize);
  context.fillStyle = "#101820";
  context.font = `${Math.max(18, Math.min(width, height) / 10)}px sans-serif`;
  context.fillText("ascii-chat test", 24, Math.max(36, height / 8));
}

/** The second deterministic animation used to compare frame delivery paths. */
export function drawTestPatternFrame2(
  context: CanvasRenderingContext2D,
  width: number,
  height: number,
  time = performance.now(),
): void {
  const phase = (time % 4000) / 4000;
  context.fillStyle = "#101820";
  context.fillRect(0, 0, width, height);

  const stripeWidth = Math.max(8, width / 12);
  const stripeOffset = phase * width;
  const firstStripe = Math.ceil(-stripeOffset / stripeWidth);
  for (let offset = 0; offset <= 12; offset++) {
    const index = firstStripe + offset;
    const hue = ((index % 12) + 12) % 12 * 30;
    const x = index * stripeWidth + stripeOffset;
    context.fillStyle = `hsl(${hue}, 100%, 55%)`;
    context.fillRect(x - stripeWidth, 0, stripeWidth, height);
  }

  const radius = Math.max(12, Math.min(width, height) / 8);
  const x = width + radius - phase * (width + radius * 2);
  const y = height / 2 + Math.sin(phase * Math.PI * 4) * height / 4;
  context.beginPath();
  context.arc(x, y, radius, 0, Math.PI * 2);
  context.fillStyle = "#ffffff";
  context.fill();
  context.fillStyle = "#101820";
  context.font = `${Math.max(18, Math.min(width, height) / 10)}px sans-serif`;
  context.fillText("ascii-chat test2", 24, Math.max(36, height / 8));
}

export function drawSelectedTestPatternFrame(
  mode: Exclude<TestPatternMode, "none">,
  context: CanvasRenderingContext2D,
  width: number,
  height: number,
  time = performance.now(),
): void {
  if (mode === "test2")
    drawTestPatternFrame2(context, width, height, time);
  else drawTestPatternFrame(context, width, height, time);
}

export function createTestPatternVideoSource(
  targetFps: number,
  width = 640,
  height = 480,
  mode: Exclude<TestPatternMode, "none"> = "test",
  targetCanvas?: HTMLCanvasElement,
  onFrame?: (canvas: HTMLCanvasElement) => void,
): TestPatternVideoSource {
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  // Frames are read back into CPU memory before being sent over WebRTC.
  const context = canvas.getContext("2d", { willReadFrequently: true });
  if (!context) throw new Error("Could not create test-pattern canvas");

  let animationFrame = 0;
  const resize = (nextWidth: number, nextHeight: number) => {
    const nextCanvasWidth = Math.max(1, Math.floor(nextWidth));
    const nextCanvasHeight = Math.max(1, Math.floor(nextHeight));
    if (
      canvas.width === nextCanvasWidth &&
      canvas.height === nextCanvasHeight
    )
      return;
    canvas.width = nextCanvasWidth;
    canvas.height = nextCanvasHeight;
  };
  const draw = () => {
    drawSelectedTestPatternFrame(mode, context, canvas.width, canvas.height);
    onFrame?.(canvas);
    animationFrame = requestAnimationFrame(draw);
  };
  draw();
  const resizeObserver = targetCanvas ? new MutationObserver(() => {
    resize(targetCanvas.width || width, targetCanvas.height || height);
  }) : null;
  if (targetCanvas) {
    resize(targetCanvas.width || width, targetCanvas.height || height);
    resizeObserver?.observe(targetCanvas, {
      attributes: true,
      attributeFilter: ["width", "height"],
    });
  }

  return {
    stream: canvas.captureStream(targetFps),
    resize,
    stop: () => {
      resizeObserver?.disconnect();
      cancelAnimationFrame(animationFrame);
    },
  };
}

export function createTestPatternAudioSource(
  context: AudioContext,
): TestPatternAudioSource {
  const destination = context.createMediaStreamDestination();
  const gain = context.createGain();
  gain.gain.value = 0.2;
  const oscillator = context.createOscillator();
  oscillator.type = "sine";
  oscillator.frequency.value = 440;
  oscillator.connect(gain).connect(destination);
  oscillator.start();

  return {
    stream: destination.stream,
    stop: () => {
      oscillator.stop();
      oscillator.disconnect();
      gain.disconnect();
      destination.disconnect();
    },
  };
}

export function fillTestPatternAudioSamples(
  samples: Float32Array,
  time = performance.now(),
): void {
  for (let index = 0; index < samples.length; index++) {
    const phase = index / samples.length;
    samples[index] =
      Math.sin(phase * Math.PI * 16 + time / 70) * 0.68 +
      Math.sin(phase * Math.PI * 53 + time / 31) * 0.22;
  }
}
