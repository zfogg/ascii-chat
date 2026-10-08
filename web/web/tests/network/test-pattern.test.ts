import { describe, expect, it, vi } from "vite-plus/test";
import {
  createTestPatternVideoSource,
  drawTestPatternFrame2,
  getTestPatternMode,
  isTestMode,
} from "@ascii-chat/shared";

describe("shared test animation selection", () => {
  it("uses the shared default animation for test and test2 for the alternate", () => {
    expect(getTestPatternMode("?test")).toBe("test");
    expect(getTestPatternMode("?test2")).toBe("test2");
    expect(getTestPatternMode("?test&test2")).toBe("test2");
    expect(isTestMode("?test2")).toBe(true);
  });

  it("does not enable a synthetic source for unrelated query keys", () => {
    expect(getTestPatternMode("?testServerUrl=ws://localhost:1")).toBe("none");
    expect(isTestMode("?videoDeviceIndex=1")).toBe(false);
  });

  it("wraps the alternate animation ball beyond each horizontal edge", () => {
    const arc = vi.fn();
    const context = {
      fillRect: vi.fn(),
      beginPath: vi.fn(),
      arc,
      fill: vi.fn(),
      fillText: vi.fn(),
    } as unknown as CanvasRenderingContext2D;

    drawTestPatternFrame2(context, 800, 400, 0);
    drawTestPatternFrame2(context, 800, 400, 2000);
    drawTestPatternFrame2(context, 800, 400, 3999);

    const radius = 50;
    expect(arc.mock.calls[0]?.[0]).toBe(800 + radius);
    expect(arc.mock.calls[1]?.[0]).toBe(400);
    expect(arc.mock.calls[2]?.[0]).toBeLessThan(-radius + 1);
  });

  it("covers the background continuously at every point in its loop", () => {
    const fillRect = vi.fn();
    const context = {
      fillRect,
      beginPath: vi.fn(),
      arc: vi.fn(),
      fill: vi.fn(),
      fillText: vi.fn(),
    } as unknown as CanvasRenderingContext2D;

    drawTestPatternFrame2(context, 800, 400, 2000);

    const stripeWidth = 800 / 12;
    const positions = fillRect.mock.calls
      .slice(1, 14)
      .map(([x]) => x as number)
      .sort((left, right) => left - right);
    expect(positions.length).toBeGreaterThan(0);
    expect(positions[0]!).toBeLessThanOrEqual(0);
    for (let index = 1; index < positions.length; index++)
      expect(positions[index]! - positions[index - 1]!).toBeCloseTo(stripeWidth);
    expect(positions.at(-1)! + stripeWidth).toBeGreaterThanOrEqual(800);
  });

  it("keeps the captured source at the target canvas size after resize", () => {
    let animationFrame: FrameRequestCallback | undefined;
    let notifyResize: (() => void) | undefined;
    const drawSizes: number[][] = [];
    const gradient = { addColorStop: vi.fn() };
    const context = {
      createLinearGradient: (_x: number, _y: number, width: number, height: number) => {
        drawSizes.push([width, height]);
        return gradient;
      },
      fillRect: vi.fn(),
      fillText: vi.fn(),
    } as unknown as CanvasRenderingContext2D;
    const sourceCanvas = {
      width: 0,
      height: 0,
      getContext: () => context,
      captureStream: () => ({}) as MediaStream,
    } as unknown as HTMLCanvasElement;
    const targetCanvas = { width: 640, height: 480 } as HTMLCanvasElement;

    class TestMutationObserver {
      constructor(callback: MutationCallback) {
        notifyResize = () => callback([], this as unknown as MutationObserver);
      }
      observe(): void {}
      disconnect(): void {}
    }

    vi.stubGlobal("document", { createElement: () => sourceCanvas });
    vi.stubGlobal("MutationObserver", TestMutationObserver);
    vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
      animationFrame = callback;
      return 1;
    });
    vi.stubGlobal("cancelAnimationFrame", vi.fn());

    const source = createTestPatternVideoSource(
      60,
      320,
      240,
      "test",
      targetCanvas,
    );
    try {
      expect(sourceCanvas.width).toBe(640);
      expect(sourceCanvas.height).toBe(480);

      targetCanvas.width = 801;
      targetCanvas.height = 603;
      notifyResize?.();
      animationFrame?.(performance.now());

      expect(sourceCanvas.width).toBe(801);
      expect(sourceCanvas.height).toBe(603);
      expect(drawSizes.at(-1)).toEqual([801, 603]);
    } finally {
      source.stop();
      vi.unstubAllGlobals();
    }
  });
});
