import { renderHook, cleanup } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vite-plus/test";
import { useCanvasCapture } from "../../../packages/shared/src/hooks/useCanvasCapture";
afterEach(cleanup);
it("draws codec input without reading pixels, and preserves raw capture", () => {
  const context = {
    drawImage: vi.fn(),
    getImageData: vi.fn(() => ({ data: new Uint8ClampedArray(16) })),
  };
  const canvas = {
    width: 2,
    height: 2,
    getContext: () => context,
  } as unknown as HTMLCanvasElement;
  const video = { videoWidth: 2, videoHeight: 2 } as HTMLVideoElement;
  const { result } = renderHook(() =>
    useCanvasCapture({ current: video }, { current: canvas }),
  );
  expect(result.current.captureFrame(true, undefined, false)?.width).toBe(2);
  expect(context.drawImage).toHaveBeenCalledOnce();
  expect(context.getImageData).not.toHaveBeenCalled();
  expect(result.current.captureFrame(false)?.data).toHaveLength(16);
  expect(context.getImageData).toHaveBeenCalledOnce();
});
