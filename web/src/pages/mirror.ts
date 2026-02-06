// Mirror Mode - Local webcam ASCII preview
import { initMirrorWasm, convertFrameToAscii, isWasmReady } from '../wasm/mirror';

// Configuration
const ASCII_WIDTH = 80;
const ASCII_HEIGHT = 24;
const TARGET_FPS = 30;
const FRAME_INTERVAL = 1000 / TARGET_FPS;

// State
let videoElement: HTMLVideoElement | null = null;
let canvasElement: HTMLCanvasElement | null = null;
let ctx: CanvasRenderingContext2D | null = null;
let outputElement: HTMLPreElement | null = null;
let fpsElement: HTMLSpanElement | null = null;
let stream: MediaStream | null = null;
let animationFrameId: number | null = null;
let lastFrameTime = 0;
let frameCount = 0;
let fpsUpdateTime = 0;

/**
 * Initialize Mirror Mode UI
 */
export async function initMirror(): Promise<void> {
  const app = document.querySelector<HTMLDivElement>('#app')!;

  app.innerHTML = `
    <div class="min-h-screen bg-terminal-bg text-terminal-fg p-4">
      <div class="max-w-6xl mx-auto">
        <header class="mb-4">
          <h1 class="text-2xl font-bold mb-2">ascii-chat Mirror Mode</h1>
          <p class="text-terminal-8">Local webcam ASCII preview (no networking)</p>
        </header>

        <div class="grid grid-cols-1 lg:grid-cols-2 gap-4">
          <!-- Video preview -->
          <div class="bg-terminal-0 p-4 rounded">
            <h2 class="text-lg font-semibold mb-2">Video Input</h2>
            <video id="mirror-video" class="w-full rounded" autoplay muted playsinline></video>
            <canvas id="mirror-canvas" class="hidden"></canvas>
          </div>

          <!-- ASCII output -->
          <div class="bg-terminal-0 p-4 rounded">
            <h2 class="text-lg font-semibold mb-2">ASCII Output</h2>
            <div class="mb-2 text-sm text-terminal-8">
              FPS: <span id="fps-counter" class="text-terminal-2">--</span>
            </div>
            <pre id="ascii-output" class="font-mono text-xs leading-none overflow-hidden whitespace-pre"></pre>
          </div>
        </div>

        <div class="mt-4 flex gap-2">
          <button id="start-btn" class="px-4 py-2 bg-terminal-2 text-terminal-bg rounded hover:bg-terminal-10">
            Start Webcam
          </button>
          <button id="stop-btn" class="px-4 py-2 bg-terminal-1 text-terminal-bg rounded hover:bg-terminal-9 hidden">
            Stop
          </button>
          <a href="/" class="px-4 py-2 bg-terminal-8 text-terminal-bg rounded hover:bg-terminal-7 inline-block">
            Back to Home
          </a>
        </div>

        <div id="error-message" class="mt-4 p-4 bg-terminal-1 text-terminal-fg rounded hidden"></div>
      </div>
    </div>
  `;

  // Get DOM elements
  videoElement = document.querySelector<HTMLVideoElement>('#mirror-video')!;
  canvasElement = document.querySelector<HTMLCanvasElement>('#mirror-canvas')!;
  ctx = canvasElement.getContext('2d', { willReadFrequently: true })!;
  outputElement = document.querySelector<HTMLPreElement>('#ascii-output')!;
  fpsElement = document.querySelector<HTMLSpanElement>('#fps-counter')!;

  const startBtn = document.querySelector<HTMLButtonElement>('#start-btn')!;
  const stopBtn = document.querySelector<HTMLButtonElement>('#stop-btn')!;

  // Load WASM module
  try {
    await initMirrorWasm();
    console.log('[Mirror] WASM module ready');
  } catch (error) {
    showError(`Failed to load WASM module: ${error}`);
    return;
  }

  // Button handlers
  startBtn.addEventListener('click', async () => {
    try {
      await startWebcam();
      startBtn.classList.add('hidden');
      stopBtn.classList.remove('hidden');
    } catch (error) {
      showError(`Failed to start webcam: ${error}`);
    }
  });

  stopBtn.addEventListener('click', () => {
    stopWebcam();
    startBtn.classList.remove('hidden');
    stopBtn.classList.add('hidden');
  });
}

/**
 * Start webcam capture
 */
async function startWebcam(): Promise<void> {
  if (!videoElement || !canvasElement) return;

  // Request webcam access
  stream = await navigator.mediaDevices.getUserMedia({
    video: {
      width: { ideal: 640 },
      height: { ideal: 480 },
      facingMode: 'user',
    },
    audio: false,
  });

  videoElement.srcObject = stream;

  // Wait for video to be ready
  await new Promise<void>((resolve) => {
    videoElement!.addEventListener('loadedmetadata', () => {
      // Set canvas size to match video
      canvasElement!.width = videoElement!.videoWidth;
      canvasElement!.height = videoElement!.videoHeight;
      resolve();
    }, { once: true });
  });

  // Start rendering loop
  lastFrameTime = performance.now();
  fpsUpdateTime = lastFrameTime;
  frameCount = 0;
  renderLoop();
}

/**
 * Stop webcam and rendering
 */
function stopWebcam(): void {
  // Stop rendering loop
  if (animationFrameId !== null) {
    cancelAnimationFrame(animationFrameId);
    animationFrameId = null;
  }

  // Stop media tracks
  if (stream) {
    stream.getTracks().forEach(track => track.stop());
    stream = null;
  }

  // Clear video
  if (videoElement) {
    videoElement.srcObject = null;
  }

  // Clear output
  if (outputElement) {
    outputElement.textContent = '';
  }

  if (fpsElement) {
    fpsElement.textContent = '--';
  }
}

/**
 * Main rendering loop
 */
function renderLoop(): void {
  const now = performance.now();
  const elapsed = now - lastFrameTime;

  // Frame rate limiting
  if (elapsed >= FRAME_INTERVAL) {
    lastFrameTime = now;
    renderFrame();

    // Update FPS counter every second
    frameCount++;
    if (now - fpsUpdateTime >= 1000) {
      const fps = Math.round(frameCount / ((now - fpsUpdateTime) / 1000));
      if (fpsElement) {
        fpsElement.textContent = fps.toString();
      }
      frameCount = 0;
      fpsUpdateTime = now;
    }
  }

  animationFrameId = requestAnimationFrame(renderLoop);
}

/**
 * Render a single frame
 */
function renderFrame(): void {
  if (!videoElement || !canvasElement || !ctx || !outputElement) return;
  if (!isWasmReady()) return;

  // Draw video frame to canvas
  ctx.drawImage(videoElement, 0, 0, canvasElement.width, canvasElement.height);

  // Get RGBA pixel data
  const imageData = ctx.getImageData(0, 0, canvasElement.width, canvasElement.height);
  const rgbaData = new Uint8Array(imageData.data);

  // Convert to ASCII using WASM
  const asciiArt = convertFrameToAscii(
    rgbaData,
    canvasElement.width,
    canvasElement.height,
    ASCII_WIDTH,
    ASCII_HEIGHT
  );

  // Format ASCII output with newlines
  const lines: string[] = [];
  for (let i = 0; i < ASCII_HEIGHT; i++) {
    lines.push(asciiArt.substring(i * ASCII_WIDTH, (i + 1) * ASCII_WIDTH));
  }
  outputElement.textContent = lines.join('\n');
}

/**
 * Show error message
 */
function showError(message: string): void {
  const errorDiv = document.querySelector<HTMLDivElement>('#error-message')!;
  errorDiv.textContent = message;
  errorDiv.classList.remove('hidden');
  console.error('[Mirror]', message);
}
