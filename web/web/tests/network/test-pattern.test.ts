import { describe, expect, it, vi, afterEach } from "vite-plus/test";
import {
  getTestPatternMode,
  drawSelectedTestPatternFrame,
  releaseTestPatternFrame,
  isTestMode,
} from "../../../packages/shared/src/testPattern";

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
});

// Exercise ownership at the WASM boundary; pixel geometry is checked by Playwright.
const bridge = vi.hoisted(() => ({ module: null as unknown }));
vi.mock("../../../packages/shared/src/wasm/mirror", () => ({
  getMirrorModule: () => bridge.module,
}));
afterEach(() => {
  bridge.module = null;
});
it("reuses the C source across resize and frees it exactly once", () => {
  const bytes = new Uint8Array(1024).fill(255);
  const module = {
    HEAPU8: bytes,
    _wasm_test_pattern_create: vi.fn(() => 1),
    _wasm_test_pattern_render_rgba: vi.fn(() => 16),
    _wasm_test_pattern_destroy: vi.fn(),
  };
  bridge.module = module;
  const context = {
    createImageData: (width: number, height: number) => ({
      width,
      height,
      data: new Uint8ClampedArray(width * height * 4),
    }),
    putImageData: vi.fn(),
  } as unknown as CanvasRenderingContext2D;
  const first = drawSelectedTestPatternFrame("test", context, 4, 4, 0, false);
  expect(first?.data[3]).toBe(255);
  drawSelectedTestPatternFrame("test2", context, 8, 4, 2000, true);
  expect(module._wasm_test_pattern_create).toHaveBeenCalledTimes(1);
  expect(module._wasm_test_pattern_render_rgba).toHaveBeenLastCalledWith(
    1,
    8,
    4,
    1,
    2000,
    1,
  );
  releaseTestPatternFrame(context);
  releaseTestPatternFrame(context);
  expect(module._wasm_test_pattern_destroy).toHaveBeenCalledExactlyOnceWith(1);
});
