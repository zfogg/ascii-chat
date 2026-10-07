import {
  type ForwardedRef,
  type RefObject,
  useCallback,
  useEffect,
  useImperativeHandle,
  useRef,
} from "react";
import type { AsciiRendererHandle } from "./types";
import type { MirrorModule } from "../../wasm/mirror";
import { startGridStabilityProbe } from "./gridStabilityProbe";

interface UseAsciiRendererHandleParams {
  ref: ForwardedRef<AsciiRendererHandle>;
  moduleRef: RefObject<MirrorModule | null>;
  setupDoneRef: RefObject<boolean>;
  rendererPtrRef: RefObject<number>;
  canvasRef: RefObject<HTMLCanvasElement | null>;
  resizeTimeoutRef: RefObject<ReturnType<typeof setTimeout> | null>;
  showFps: boolean;
  onFpsChange: ((fps: number) => void) | undefined;
  onDimensionsChange:
    | ((dims: { cols: number; rows: number }) => void)
    | undefined;
  onRecreateRenderer?: () => void;
}

interface UseAsciiRendererHandleReturn {
  updateDimensions: (cols: number, rows: number) => void;
  fpsDisplayRef: RefObject<HTMLDivElement | null>;
}

export function useAsciiRendererHandle({
  ref,
  moduleRef,
  setupDoneRef,
  rendererPtrRef,
  canvasRef,
  resizeTimeoutRef,
  showFps,
  onFpsChange,
  onDimensionsChange,
  onRecreateRenderer,
}: UseAsciiRendererHandleParams): UseAsciiRendererHandleReturn {
  const firstRenderDoneRef = useRef(false);
  const dimensionsRef = useRef({ cols: 0, rows: 0 });
  const changedFrameTimesRef = useRef<number[]>([]);
  const lastReportedFpsRef = useRef<number | null>(null);
  const fpsDisplayRef = useRef<HTMLDivElement>(null);
  const textEncoderRef = useRef(new TextEncoder());
  const imageDataRef = useRef<ImageData | null>(null);
  const lastFrameChangedRef = useRef(false);

  useEffect(() => {
    if (!showFps) {
      changedFrameTimesRef.current = [];
      lastReportedFpsRef.current = null;
      return;
    }

    const updateFps = () => {
      const now = performance.now();
      const cutoff = now - 1000;
      while (
        changedFrameTimesRef.current.length > 0 &&
        changedFrameTimesRef.current[0]! < cutoff
      ) {
        changedFrameTimesRef.current.shift();
      }
      const fps = changedFrameTimesRef.current.length;
      if (lastReportedFpsRef.current === fps) return;
      lastReportedFpsRef.current = fps;
      if (fpsDisplayRef.current) fpsDisplayRef.current.textContent = String(fps);
      onFpsChange?.(fps);
    };

    updateFps();
    const timer = window.setInterval(updateFps, 250);
    return () => window.clearInterval(timer);
  }, [showFps, onFpsChange]);

  const updateDimensions = useCallback(
    (cols: number, rows: number) => {
      dimensionsRef.current = { cols, rows };
      onDimensionsChange?.({ cols, rows });
    },
    [onDimensionsChange],
  );

  useImperativeHandle(
    ref,
    () => ({
      writeFrame(
        ansiString: string,
      ): boolean {
        lastFrameChangedRef.current = false;
        if (!moduleRef.current || !setupDoneRef.current) {
          return false;
        }

        // Skip rendering during resize debounce to avoid dimension mismatches
        if (resizeTimeoutRef.current) {
          return false;
        }
        try {
          // Encode string to UTF-8 bytes
          const data = textEncoderRef.current.encode(ansiString);

          // Allocate memory in WASM and copy data
          const ptr = moduleRef.current._malloc(data.length);
          if (!ptr) {
            throw new Error(
              "[AsciiRenderer] Failed to allocate memory in WASM",
            );
          }

          const wasmMemory = new Uint8Array(moduleRef.current.HEAPU8.buffer);
          wasmMemory.set(data, ptr);

          // Render frame - call term_renderer_feed with renderer pointer
          moduleRef.current._term_renderer_feed(
            rendererPtrRef.current,
            ptr,
            data.length,
          );

          // Free memory
          moduleRef.current._free(ptr);

          // Display framebuffer on canvas
          try {
            const canvas = canvasRef.current;
            if (
              canvas &&
              canvas.getContext &&
              moduleRef.current._term_renderer_pixels
            ) {
              const fbPtr = moduleRef.current._term_renderer_pixels(
                rendererPtrRef.current,
              );
              const fbStride = moduleRef.current._term_renderer_pitch(
                rendererPtrRef.current,
              );
              // Use canvas dimensions (set to container size earlier)
              const fbWidth = canvas.width;
              const fbHeight = canvas.height;

              if (
                fbPtr &&
                fbWidth &&
                fbHeight &&
                fbWidth > 0 &&
                fbHeight > 0 &&
                typeof fbWidth === "number" &&
                typeof fbHeight === "number"
              ) {
                const w: number = fbWidth as number;
                const h: number = fbHeight as number;
                const stride: number = fbStride ? (fbStride as number) : w * 4;
                const fbData = new Uint8Array(
                  moduleRef.current.HEAPU8.buffer,
                  fbPtr,
                  h * stride,
                );
                const rowBytes = w * 4;
                let imageData = imageDataRef.current;
                if (!imageData || imageData.width !== w || imageData.height !== h) {
                  imageData = new ImageData(w, h);
                  imageDataRef.current = imageData;
                }
                // Frames received from the server are display snapshots. Do
                // not compare their framebuffer byte-by-byte in JavaScript:
                // at typical canvas sizes that costs hundreds of millions of
                // comparisons per second and blocks rAF. Typed-array copies
                // stay in native code, and a received frame is a valid FPS
                // update even when its pixels happen to match the prior one.
                lastFrameChangedRef.current = true;
                if (stride === rowBytes) {
                  imageData.data.set(fbData);
                } else {
                  for (let row = 0; row < h; row++) {
                    const sourceStart = row * stride;
                    const destinationStart = row * rowBytes;
                    imageData.data.set(
                      fbData.subarray(sourceStart, sourceStart + rowBytes),
                      destinationStart,
                    );
                  }
                }

                const ctx = canvas.getContext("2d");
                if (!ctx) {
                  throw new Error("[AsciiRenderer] Canvas context not found");
                }
                ctx.putImageData(imageData, 0, 0);
                if (ctx) {
                  if (
                    new URLSearchParams(window.location.search).get(
                      "verifyGrid",
                    ) === "1"
                  ) {
                    startGridStabilityProbe(canvas);
                  }
                }
              } else {
                throw new Error(
                  `[AsciiRenderer] Invalid framebuffer dimensions fbPtr=${fbPtr} fbWidth=${fbWidth} fbHeight=${fbHeight}`,
                );
              }
            } else {
              throw new Error(
                "[AsciiRenderer] Canvas or _term_renderer_pixels not ready",
              );
            }
          } catch (displayErr) {
            console.error(
              "[AsciiRenderer] Failed to display framebuffer:",
              displayErr,
            );
            return false;
          }

          // Mark first render as done so resize can proceed
          if (!firstRenderDoneRef.current) {
            firstRenderDoneRef.current = true;
          }
        } catch (err) {
          console.error("[AsciiRenderer] writeFrame error:", err);
          return false;
        }

        if (showFps && lastFrameChangedRef.current) {
          const now = performance.now();
          changedFrameTimesRef.current.push(now);
        }
        return true;
      },

      getLastFrameChanged() {
        return lastFrameChangedRef.current;
      },

      getDimensions() {
        return dimensionsRef.current;
      },

      clear() {
        if (!moduleRef.current || !setupDoneRef.current) return;
        // Clear by feeding empty data
        const emptyPtr = moduleRef.current._malloc(1);
        if (emptyPtr) {
          moduleRef.current._term_renderer_feed(
            rendererPtrRef.current,
            emptyPtr,
            0,
          );
          moduleRef.current._free(emptyPtr);
        }
      },

      recreateRenderer() {
        onRecreateRenderer?.();
      },
    }),
    [
      showFps,
      onRecreateRenderer,
      moduleRef,
      setupDoneRef,
      rendererPtrRef,
      resizeTimeoutRef,
      canvasRef,
    ],
  );

  return {
    updateDimensions,
    fpsDisplayRef,
  };
}
