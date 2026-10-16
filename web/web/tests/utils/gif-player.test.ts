import { afterEach, expect, test, vi } from "vite-plus/test";
import { createGifPlayer } from "../../src/utils/gifPlayer";

vi.mock("gifuct-js", () => ({
  parseGIF: () => ({
    lsd: { width: 2, height: 1 },
    frames: [
      { image: {}, gce: { delay: 10 }, disposalType: 2 },
      { image: {}, gce: { delay: 20 }, disposalType: 3 },
      { image: {}, gce: { delay: 30 }, disposalType: 1 },
    ],
  }),
  decompressFrame: (frame: { disposalType: number }) => ({
    dims: { left: 0, top: 0, width: 1, height: 1 },
    patch: new Uint8ClampedArray([255, 0, 0, 255]),
    disposalType: frame.disposalType,
  }),
}));
afterEach(() => vi.restoreAllMocks());

test("GIF timing composites patches, restores disposal 2/3, and repeats from frame zero", () => {
  const saved = {} as ImageData;
  const context = {
    clearRect: vi.fn(),
    drawImage: vi.fn(),
    putImageData: vi.fn(),
    getImageData: vi.fn(() => saved),
    createImageData: vi.fn(() => ({ data: new Uint8ClampedArray(4) })),
  };
  vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockReturnValue(
    context as unknown as CanvasRenderingContext2D,
  );
  const player = createGifPlayer(new ArrayBuffer(0));
  player.draw(0);
  player.draw(99);
  expect(context.drawImage).toHaveBeenCalledTimes(1);
  player.draw(100);
  expect(context.clearRect).toHaveBeenLastCalledWith(0, 0, 1, 1);
  expect(context.getImageData).toHaveBeenCalledOnce();
  player.draw(300);
  expect(context.putImageData).toHaveBeenCalledWith(saved, 0, 0);
  expect(context.drawImage).toHaveBeenCalledTimes(3);
  player.draw(600);
  expect(context.clearRect).toHaveBeenLastCalledWith(0, 0, 2, 1);
  expect(context.drawImage).toHaveBeenCalledTimes(4);
  // Restarting within a cycle also clears the old composition.
  player.draw(350);
  player.draw(0);
  expect(context.clearRect).toHaveBeenLastCalledWith(0, 0, 2, 1);
});
