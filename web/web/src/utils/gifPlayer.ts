import { parseGIF, decompressFrame } from "gifuct-js";

/** Composite GIF patches explicitly: canvas drawImage(img) only sees frame zero. */
export function createGifPlayer(data: ArrayBuffer) {
  const gif = parseGIF(data);
  const frames = gif.frames.filter((frame) => "image" in frame);
  const { width, height } = gif.lsd;
  if (!frames.length || !width || !height || width * height > 16_777_216)
    throw new Error("GIF is empty or exceeds the 16 megapixel limit.");
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext("2d", { willReadFrequently: true })!;
  const patch = document.createElement("canvas");
  const patchCtx = patch.getContext("2d")!;
  const delays = frames.map((frame) =>
    Math.max(20, (frame.gce?.delay || 10) * 10),
  );
  const duration = delays.reduce((sum, delay) => sum + delay, 0);
  let index = -1;
  let cycle = -1;
  let previous: ReturnType<typeof decompressFrame> | undefined;
  let restore: ImageData | undefined;

  return {
    width,
    height,
    draw(elapsed: number) {
      const nextCycle = Math.floor(elapsed / duration);
      let position = elapsed % duration;
      let target = 0;
      while (target < delays.length - 1 && position >= delays[target]!) {
        position -= delays[target++]!;
      }
      if (nextCycle !== cycle || target < index) {
        ctx.clearRect(0, 0, width, height);
        index = -1;
        previous = undefined;
        restore = undefined;
        cycle = nextCycle;
      }
      while (index < target) {
        if (previous?.disposalType === 2) {
          const d = previous.dims;
          ctx.clearRect(d.left, d.top, d.width, d.height);
          const background = gif.gct?.[gif.lsd.backgroundColorIndex];
          if (previous.transparentIndex === undefined && background) {
            ctx.fillStyle = `rgb(${background.join(",")})`;
            ctx.fillRect(d.left, d.top, d.width, d.height);
          }
        } else if (previous?.disposalType === 3 && restore) {
          ctx.putImageData(restore, 0, 0);
        }
        const frame = decompressFrame(frames[++index]!, gif.gct, true);
        restore =
          frame.disposalType === 3
            ? ctx.getImageData(0, 0, width, height)
            : undefined;
        patch.width = frame.dims.width;
        patch.height = frame.dims.height;
        const pixels = patchCtx.createImageData(patch.width, patch.height);
        pixels.data.set(frame.patch);
        patchCtx.putImageData(pixels, 0, 0);
        ctx.drawImage(patch, frame.dims.left, frame.dims.top);
        previous = frame;
      }
      return canvas;
    },
  };
}
