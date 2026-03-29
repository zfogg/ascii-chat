import {
  useImperativeHandle,
  useRef,
  useCallback,
  useEffect,
  type ForwardedRef,
  type RefObject,
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
  fpsDisplayRef: RefObject<HTMLDivElement | null>;
  frameCountRef: RefObject<number>;
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
  fpsDisplayRef,
  frameCountRef,
}: UseAsciiRendererHandleParams) {
  const frameCountForLoggingRef = useRef(0);
  const frameHistoryRef = useRef<{ frame: number; lines: string[] }[]>([]);
  const firstRenderDoneRef = useRef(false);
  const dimensionsRef = useRef({ cols: 0, rows: 0 });
  const fpsUpdateTimeRef = useRef<number | null>(null);

  useEffect(() => {
    fpsUpdateTimeRef.current = performance.now();
  }, []);

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
            console.error("[AsciiRenderer] Failed to allocate memory in WASM");
            return;
          }

          const wasmMemory = new Uint8Array(moduleRef.current.HEAPU8.buffer);
          wasmMemory.set(data, ptr);

          // Render frame - call term_renderer_feed with renderer pointer
          try {
            moduleRef.current._term_renderer_feed(
              rendererPtrRef.current,
              ptr,
              data.length,
            );
          } catch (wasmError) {
            console.error(
              `[WASM-ERROR-DETAILS] Caught error calling _term_renderer_feed:`,
              {
                message:
                  wasmError instanceof Error
                    ? wasmError.message
                    : String(wasmError),
                name: wasmError instanceof Error ? wasmError.name : "Unknown",
                stack:
                  wasmError instanceof Error ? wasmError.stack : "No stack",
                rendererPtr: rendererPtrRef.current,
                ptr: ptr,
                len: data.length,
                moduleType: typeof moduleRef.current,
                functionName: "_term_renderer_feed",
              },
            );
            throw wasmError;
          }

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
                }
              }
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

          // Detect content bouncing by finding 80% similar text blocks that move
          try {
            const lines = ansiString.split("\n");
            const history = frameHistoryRef.current;

            // Keep only last 60 frames (roughly 1-2 seconds at 30-60 fps)
            if (history.length > 60) {
              history.shift();
            }

            // Look back through recent frames
            if (history.length > 5) {
              const currentFrame = history[history.length - 1];
              const olderFrame = history[Math.max(0, history.length - 20)]; // ~0.3-0.6 sec ago

              if (
                currentFrame &&
                olderFrame &&
                currentFrame.lines.length > 0 &&
                olderFrame.lines.length > 0
              ) {
                // Find a contiguous block of 5+ lines that are 80%+ similar between frames
                for (
                  let blockStart = 0;
                  blockStart < currentFrame.lines.length - 4;
                  blockStart++
                ) {
                  let matchedOffset = -999;
                  let blockSimilarity = 0;

                  // Check if this block of 5+ lines matches somewhere in older frame
                  for (let offset = -15; offset <= 15; offset++) {
                    let similarLines = 0;
                    for (let i = 0; i < 5; i++) {
                      const currIdx = blockStart + i;
                      const oldIdx = blockStart + i + offset;
                      const currLine = currentFrame.lines[currIdx];
                      const oldLine = olderFrame.lines[oldIdx];
                      if (
                        currIdx < currentFrame.lines.length &&
                        oldIdx >= 0 &&
                        oldIdx < olderFrame.lines.length &&
                        currLine &&
                        oldLine
                      ) {
                        // Compare first 50 chars
                        const currSig = currLine.substring(0, 50);
                        const oldSig = oldLine.substring(0, 50);
                        if (currSig === oldSig) {
                          similarLines++;
                        } else {
                          // Check 80% character similarity
                          let charMatch = 0;
                          const maxLen = Math.max(
                            currSig.length,
                            oldSig.length,
                          );
                          for (
                            let c = 0;
                            c < Math.min(currSig.length, oldSig.length);
                            c++
                          ) {
                            if (currSig[c] === oldSig[c]) charMatch++;
                          }
                          if (charMatch / maxLen >= 0.8) {
                            similarLines++;
                          }
                        }
                      }
                    }

                    // Found a matching block that moved
                    if (similarLines >= 4 && offset !== 0) {
                      matchedOffset = offset;
                      blockSimilarity = similarLines / 5;
                      break;
                    }
                  }

                  if (blockSimilarity >= 0.8 && matchedOffset !== -999) {
                    break; // Only log once per frame
                  }
                }
              }
            }

            history.push({ frame: frameCountForLoggingRef.current, lines });
          } catch {
            // ignore
          }
        } catch (err) {
          console.error("[AsciiRenderer] writeFrame error:", err);
          if (
            err instanceof Error &&
            err.message.includes("signature mismatch")
          ) {
            console.error(
              "[WASM Signature Mismatch] _term_renderer_feed issue - function type:",
              typeof moduleRef.current._term_renderer_feed,
              "moduleRef:",
              moduleRef.current,
            );
          }
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
    }),
    [showFps, onFpsChange],
  );

  return updateDimensions;
}
