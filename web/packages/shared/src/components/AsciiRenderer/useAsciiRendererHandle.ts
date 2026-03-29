import {
  type ForwardedRef,
  type RefObject,
  useCallback,
  useImperativeHandle,
  useRef,
} from "react";
import type { AsciiRendererHandle } from "./types";
import type { MirrorModule } from "../../wasm/mirror";

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
  const frameCountForLoggingRef = useRef(0);
  const firstRenderDoneRef = useRef(false);
  const dimensionsRef = useRef({ cols: 0, rows: 0 });
  const fpsUpdateTimeRef = useRef<number | null>(performance.now());
  const frameCountRef = useRef(0);
  const fpsDisplayRef = useRef<HTMLDivElement>(null);

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
      writeFrame(ansiString: string) {
        if (!moduleRef.current || !setupDoneRef.current) {
          return;
        }

        // Skip rendering during resize debounce to avoid dimension mismatches
        if (resizeTimeoutRef.current) {
          return;
        }

        frameCountForLoggingRef.current++;

        try {
          // Encode string to UTF-8 bytes
          const encoder = new TextEncoder();
          const data = encoder.encode(ansiString);

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

                // Read RGBA32 framebuffer from WASM memory (now 4 bytes per pixel)
                const fbData: Uint8Array = new Uint8Array(
                  moduleRef.current.HEAPU8.buffer,
                  fbPtr,
                  h * stride,
                );

                // Create ImageData directly from RGBA data (no conversion needed)
                const imageData = new ImageData(w, h);
                // Copy RGBA data directly - no per-pixel conversion loop
                imageData.data.set(fbData.subarray(0, w * h * 4));

                // Display on canvas
                const ctx = canvas.getContext("2d");
                if (ctx) {
                  ctx.putImageData(imageData, 0, 0);
                } else {
                  throw new Error("[AsciiRenderer] Canvas context not found");
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
          }

          // Mark first render as done so resize can proceed
          if (!firstRenderDoneRef.current) {
            firstRenderDoneRef.current = true;
          }
        } catch (err) {
          console.error("[AsciiRenderer] writeFrame error:", err);
        }

        // Update FPS counter
        if (showFps && fpsUpdateTimeRef.current !== null) {
          frameCountRef.current++;
          const now = performance.now();
          const elapsed = now - fpsUpdateTimeRef.current;

          if (elapsed >= 1000) {
            const fps = Math.round(frameCountRef.current / (elapsed / 1000));
            if (fpsDisplayRef.current) {
              fpsDisplayRef.current.textContent = fps.toString();
            }
            onFpsChange?.(fps);
            frameCountRef.current = 0;
            fpsUpdateTimeRef.current = now;
          }
        }
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
      onFpsChange,
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
