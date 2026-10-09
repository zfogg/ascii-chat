// Canvas reference from 37028bd7e. Only the cadence overlay is made optional.
// This file is test-only and is never imported by the application.

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
  cadence = false,
): void {
  const phase = (time % 4000) / 4000;
  context.fillStyle = "#101820";
  context.fillRect(0, 0, width, height);

  const stripeWidth = Math.max(8, width / 12);
  const stripeOffset = phase * width;
  const firstStripe = Math.ceil(-stripeOffset / stripeWidth);
  for (let offset = 0; offset <= 12; offset++) {
    const index = firstStripe + offset;
    const hue = (((index % 12) + 12) % 12) * 30;
    const x = index * stripeWidth + stripeOffset;
    context.fillStyle = `hsl(${hue}, 100%, 55%)`;
    context.fillRect(x - stripeWidth, 0, stripeWidth, height);
  }

  const radius = Math.max(12, Math.min(width, height) / 8);
  const x = width + radius - phase * (width + radius * 2);
  const y = height / 2 + (Math.sin(phase * Math.PI * 4) * height) / 4;
  context.beginPath();
  context.arc(x, y, radius, 0, Math.PI * 2);
  context.fillStyle = "#ffffff";
  context.fill();
  context.fillStyle = "#101820";
  context.font = `${Math.max(18, Math.min(width, height) / 10)}px sans-serif`;
  context.fillText("ascii-chat test2", 24, Math.max(36, height / 8));

  // Encode source-frame cadence as a gray-code bar panel. The stripes and
  // circle can move without crossing an ASCII cell boundary on every source
  // frame; one full-height eighth changes on each frame, even after downsampling.
  if (!cadence) return;
  const frameNumber = Math.floor(time / (1000 / 60));
  const frameCode = frameNumber ^ (frameNumber >> 1);
  const markerWidth = Math.max(2, width / 8);
  const bitSlots = [3, 4, 2, 5, 1, 6, 0, 7];
  for (let bit = 0; bit < 8; bit++) {
    context.fillStyle = frameCode & (1 << bit) ? "#ffffff" : "#000000";
    context.fillRect(bitSlots[bit]! * markerWidth, 0, markerWidth, height);
  }
}
