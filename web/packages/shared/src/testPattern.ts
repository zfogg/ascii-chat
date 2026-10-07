export interface TestPatternVideoSource {
  stream: MediaStream;
  stop: () => void;
}

export interface TestPatternAudioSource {
  stream: MediaStream;
  stop: () => void;
}

export function isTestMode(search = window.location.search): boolean {
  return new URLSearchParams(search).has("test");
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

export function createTestPatternVideoSource(
  targetFps: number,
  width = 640,
  height = 480,
): TestPatternVideoSource {
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d");
  if (!context) throw new Error("Could not create test-pattern canvas");

  let animationFrame = 0;
  const draw = () => {
    drawTestPatternFrame(context, width, height);
    animationFrame = requestAnimationFrame(draw);
  };
  draw();

  return {
    stream: canvas.captureStream(targetFps),
    stop: () => cancelAnimationFrame(animationFrame),
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
