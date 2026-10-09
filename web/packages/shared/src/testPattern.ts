import { getMirrorModule } from "./wasm/mirror";

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

// Each canvas owns its C renderer and releases it when its source stops.
const renderers = new WeakMap<
  CanvasRenderingContext2D,
  {
    module: NonNullable<ReturnType<typeof getMirrorModule>>;
    pointer: number;
    image: ImageData;
  }
>();

export function releaseTestPatternFrame(
  context: CanvasRenderingContext2D,
): void {
  const state = renderers.get(context);
  if (state) state.module._wasm_test_pattern_destroy(state.pointer);
  renderers.delete(context);
}

export function drawSelectedTestPatternFrame(
  mode: Exclude<TestPatternMode, "none">,
  context: CanvasRenderingContext2D,
  width: number,
  height: number,
  time = performance.now(),
  cadence = new URLSearchParams(window.location.search).has("testCadence"),
): ImageData | undefined {
  const module = getMirrorModule();
  if (!module) return; // Initialization completes before the next animation frame.
  let state = renderers.get(context);
  if (state && state.module !== module) {
    releaseTestPatternFrame(context);
    state = undefined;
  }
  if (!state) {
    const pointer = module._wasm_test_pattern_create(width, height);
    if (!pointer) throw new Error("Could not create C test pattern");
    state = { module, pointer, image: context.createImageData(width, height) };
    renderers.set(context, state);
  }
  if (state.image.width !== width || state.image.height !== height)
    state.image = context.createImageData(width, height);
  const pixels = module._wasm_test_pattern_render_rgba(
    state.pointer,
    width,
    height,
    mode === "test2" ? 1 : 0,
    time,
    cadence ? 1 : 0,
  );
  if (!pixels) throw new Error("Could not render C test pattern");
  const source = module.HEAPU8;
  const target = state.image.data;
  target.set(source.subarray(pixels, pixels + target.length));
  context.putImageData(state.image, 0, 0);
  return state.image;
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
    if (canvas.width === nextCanvasWidth && canvas.height === nextCanvasHeight)
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
  const resizeObserver = targetCanvas
    ? new MutationObserver(() => {
        resize(targetCanvas.width || width, targetCanvas.height || height);
      })
    : null;
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
      releaseTestPatternFrame(context);
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
