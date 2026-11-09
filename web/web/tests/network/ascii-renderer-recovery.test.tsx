import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vite-plus/test";
import { useInitAsciiRenderer } from "../../../packages/shared/src/components/AsciiRenderer/useInitAsciiRenderer";

const state = vi.hoisted(() => ({ module: null as unknown }));
vi.mock("../../../packages/shared/src/wasm/mirror", () => ({
  getMirrorModule: () => state.module,
}));
afterEach(() => {
  cleanup();
  vi.useRealTimers();
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});

it("does not publish a stale debounced grid after resize recovery", () => {
  vi.useFakeTimers();
  vi.spyOn(console, "debug").mockImplementation(() => {});
  vi.spyOn(console, "error").mockImplementation(() => {});
  vi.spyOn(console, "warn").mockImplementation(() => {});
  let notify!: ResizeObserverCallback;
  vi.stubGlobal(
    "ResizeObserver",
    class {
      constructor(callback: ResizeObserverCallback) {
        notify = callback;
      }
      observe() {}
      disconnect() {}
    },
  );
  state.module = {
    TerminalRendererBinding: class {
      constructor(
        private columns: number,
        private rowsCount: number,
      ) {}
      valid() {
        return true;
      }
      error() {
        return 0;
      }
      cols() {
        return this.columns;
      }
      rows() {
        return this.rowsCount;
      }
      width() {
        return this.columns * 10;
      }
      height() {
        return this.rowsCount * 20;
      }
      delete() {}
    },
  };
  const parent = document.createElement("div");
  const canvas = document.createElement("canvas");
  parent.append(canvas);
  vi.spyOn(parent, "getBoundingClientRect").mockReturnValue({
    width: 800,
    height: 400,
  } as DOMRect);
  const canvasRef = { current: canvas };
  const { result } = renderHook(() =>
    useInitAsciiRenderer({
      canvasRef,
      wasmModuleReady: true,
    }),
  );
  const update = vi.fn();
  result.current.setUpdateDimensions(update);
  const resize = (width: number, height: number) =>
    act(() => {
      notify(
        [{ contentRect: { width, height } } as ResizeObserverEntry],
        {} as ResizeObserver,
      );
    });
  resize(1000, 600);
  expect(result.current.resizeTimeoutRef.current).not.toBeNull();
  resize(5000, 600);
  expect(update).toHaveBeenLastCalledWith(128, 30);
  expect(result.current.resizeTimeoutRef.current).toBeNull();
  act(() => vi.advanceTimersByTime(301));
  expect(update).toHaveBeenCalledTimes(1);
  expect(update).toHaveBeenLastCalledWith(128, 30);
});
