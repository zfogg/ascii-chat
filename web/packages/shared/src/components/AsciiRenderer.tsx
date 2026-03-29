import {
  forwardRef,
  useCallback,
  useEffect,
  useImperativeHandle,
  useRef,
} from "react";
import { getMirrorModule, type MirrorModule } from "../wasm/mirror";

export interface AsciiRendererHandle {
  writeFrame(ansiString: string): void;
  getDimensions(): { cols: number; rows: number };
  clear(): void;
}

export interface AsciiRendererProps {
  onDimensionsChange?: (dims: { cols: number; rows: number }) => void;
  onFpsChange?: (fps: number) => void;
  error?: string;
  showFps?: boolean;
  connectionState?: number;
  wasmModuleReady?: boolean;
}

export const AsciiRenderer = forwardRef<
  AsciiRendererHandle,
  AsciiRendererProps
>(function AsciiRenderer(
  {
    onDimensionsChange,
    onFpsChange,
    error,
    showFps = true,
    connectionState,
    wasmModuleReady,
  },
  ref,
) {
  console.log("[AsciiRenderer] Render with wasmModuleReady=" + wasmModuleReady);
  const previousWasmReadyRef = useRef<boolean | undefined>(undefined);
  if (previousWasmReadyRef.current !== wasmModuleReady) {
    previousWasmReadyRef.current = wasmModuleReady;
  }
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const moduleRef = useRef<MirrorModule | null>(null);
  const rendererPtrRef = useRef<number>(0);
  const setupDoneRef = useRef(false);
  const dimensionsRef = useRef({ cols: 0, rows: 0 });
  const firstRenderDoneRef = useRef(false);
  const pendingDimensionsRef = useRef<{ cols: number; rows: number } | null>(
    null,
  );
  const resizeTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const resizeObserverRef = useRef<ResizeObserver | null>(null);

  // FPS tracking
  const frameCountRef = useRef(0);
  const fpsUpdateTimeRef = useRef<number | null>(null);
  const fpsDisplayRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    fpsUpdateTimeRef.current = performance.now();
  }, []);

  // NOTE: Canvas sizing is handled by the renderer initialization code
  // We set canvas to the renderer's actual pixel output dimensions, not the container size

  useEffect(() => {
    if (wasmModuleReady) {
      const module = getMirrorModule();
      moduleRef.current = module;
    }
  }, [wasmModuleReady]);

  const updateDimensions = useCallback(
    (cols: number, rows: number) => {
      dimensionsRef.current = { cols, rows };
      onDimensionsChange?.({ cols, rows });
    },
    [onDimensionsChange],
  );

  useEffect(() => {
    console.log(
      "[EFFECT] Starting, setupDone=" +
        setupDoneRef.current +
        ", canvasReady=" +
        !!canvasRef.current,
    );
    if (!canvasRef.current) {
      return;
    }

    const canvas = canvasRef.current;

    if (setupDoneRef.current) {
      return;
    }

    console.log("[INIT START] Canvas element exists, about to init renderer");

    // Initialize WASM renderer with canvas dimensions
    const initRenderer = () => {
      console.log("[INIT RENDERER] Function called");
      try {
        // Set canvas on module for raylib/Emscripten
        if (!moduleRef.current) {
          throw new Error("moduleRef.current is null during initRenderer");
        }

        moduleRef.current.canvas = canvas;

        // Allocate and initialize term_renderer_config_t struct
        // Struct layout (WASM 32-bit pointers):
        // int cols (4) + int rows (4) + double font_size_pt (8) + int theme (4) = 20 bytes
        // char font_spec[512] at offset 20 = 512 bytes
        // bool font_is_path (1) + padding (3) = 4 bytes at offset 532
        // uint8_t *font_data (4) at offset 536
        // size_t font_data_size (4) at offset 540
        // Total: 544 bytes
        const configSize = 544;
        const configPtr = moduleRef.current!._malloc(configSize);
        const outPtr = moduleRef.current!._malloc(8); // pointer to renderer

        // Write config struct to WASM memory using direct offsets
        const view = new DataView(
          moduleRef.current!.HEAPU8.buffer,
          configPtr,
          configSize,
        );

        // DEBUG: walk up DOM tree to see parents and their sizes
        console.log("===== [AsciiRenderer] INIT START =====");
        console.log("[AsciiRenderer] canvas element:", {
          tag: canvas.tagName,
          className: canvas.className,
          id: canvas.id,
          clientWidth: canvas.clientWidth,
          clientHeight: canvas.clientHeight,
          offsetWidth: canvas.offsetWidth,
          offsetHeight: canvas.offsetHeight,
        });

        let current: HTMLElement = canvas;
        let level = 0;
        const parentChain = [];
        while (current && level < 15) {
          const elem = current as HTMLElement;
          const w = elem.clientWidth || 0;
          const h = elem.clientHeight || 0;
          const style = window.getComputedStyle(elem);
          const info = {
            level,
            tag: elem.tagName,
            className: elem.className,
            id: elem.id || "(none)",
            clientSize: `${w}x${h}`,
            offsetSize: `${elem.offsetWidth}x${elem.offsetHeight}`,
            display: style.display,
            position: style.position,
            visibility: style.visibility,
            opacity: style.opacity,
          };
          parentChain.push(info);
          console.log(
            `[Level ${level}] ${elem.tagName}.${elem.className} id=${elem.id} | client:${w}x${h} offset:${elem.offsetWidth}x${elem.offsetHeight} | display:${style.display}`,
          );
          current = elem.parentElement as HTMLElement;
          level++;
        }
        console.log("[AsciiRenderer] Full parent chain:", parentChain);

        // Get container dimensions from .ascii-canvas-container parent
        const container = canvas.parentElement;
        const containerRect = container?.getBoundingClientRect();
        const containerWidth = Math.round(containerRect?.width ?? 1200);
        const containerHeight = Math.round(containerRect?.height ?? 1000);

        console.log(
          "[AsciiRenderer] Container (.ascii-canvas-container): " +
            containerWidth +
            "x" +
            containerHeight,
        );

        // NOTE: Canvas dimensions will be set to renderer's actual pixel output
        // after the renderer is created (below), not to container dimensions

        // Estimate cell size (will be refined after renderer init)
        // Rough estimate: 10px wide, 20px tall per cell
        let estimatedCols = Math.max(80, Math.floor(containerWidth / 10));
        let estimatedRows = Math.max(24, Math.floor(containerHeight / 20));
        console.log(
          "[AsciiRenderer] Estimated grid:",
          estimatedCols,
          "x",
          estimatedRows,
        );

        // cols (int32) at offset 0
        view.setInt32(0, estimatedCols, true);
        // rows (int32) at offset 4
        view.setInt32(4, estimatedRows, true);
        // font_size_pt (float64) at offset 8
        view.setFloat64(8, 12.0, true);
        // theme (int32) at offset 16
        view.setInt32(16, 0, true);
        // font_spec (char[512]) at offset 20
        const fontSpec = "Courier New";
        for (let i = 0; i < fontSpec.length && i < 511; i++) {
          moduleRef.current!.HEAPU8[configPtr + 20 + i] =
            fontSpec.charCodeAt(i);
        }
        moduleRef.current!.HEAPU8[configPtr + 20 + fontSpec.length] = 0; // null terminate
        // font_is_path (bool) at offset 532
        moduleRef.current!.HEAPU8[configPtr + 532] = 0;
        // font_data (pointer) at offset 536 (WASM 32-bit pointers)
        const fontDataPtr = moduleRef.current!._get_font_default_ptr();
        view.setInt32(536, fontDataPtr, true);

        // font_data_size (size_t) at offset 540 (WASM 32-bit size_t)
        const fontDataSize = moduleRef.current!._get_font_default_size();
        view.setInt32(540, fontDataSize, true);

        const doInit = () => {
          const result = moduleRef.current!._term_renderer_create(
            configPtr,
            outPtr,
          );

          if (result !== 0) {
            throw new Error(
              `term_renderer_create failed with error code ${result}`,
            );
          }

          // Read renderer pointer from outPtr
          const rendererPtr = new DataView(
            moduleRef.current!.HEAPU8.buffer,
            outPtr,
            8,
          ).getBigInt64(0, true);
          return Number(rendererPtr);
        };

        const rendererPtr = doInit();
        rendererPtrRef.current = rendererPtr;
        console.log("[AsciiRenderer] Renderer created, ptr:", rendererPtr);

        // Get actual dimensions from renderer (grid AND pixel)
        const cols = moduleRef.current!._term_renderer_get_cols(
          rendererPtrRef.current,
        );
        const rows = moduleRef.current!._term_renderer_get_rows(
          rendererPtrRef.current,
        );
        const pixelWidth = moduleRef.current!._term_renderer_width_px(
          rendererPtrRef.current,
        );
        const pixelHeight = moduleRef.current!._term_renderer_height_px(
          rendererPtrRef.current,
        );
        console.log(
          "[RENDERER DIMS] cols=" +
            cols +
            ", rows=" +
            rows +
            ", pixels=" +
            pixelWidth +
            "x" +
            pixelHeight,
        );

        // CRITICAL: Set canvas to renderer's actual pixel output dimensions, not container dimensions
        canvas.width = pixelWidth;
        canvas.height = pixelHeight;
        console.log(
          "[AsciiRenderer] Canvas set to renderer pixel dims: " +
            canvas.width +
            "x" +
            canvas.height,
        );
        console.log(
          "[RENDERER DIMS] Calling updateDimensions(" +
            cols +
            ", " +
            rows +
            ")",
        );

        // Free temporary config/output buffers after renderer creation
        moduleRef.current!._free(configPtr);
        moduleRef.current!._free(outPtr);

        updateDimensions(cols, rows);
        setupDoneRef.current = true;
        console.log(
          "[SETUP DONE] Setup complete, canvas=" +
            canvas.width +
            "x" +
            canvas.height,
        );

        // Set up ResizeObserver on .ascii-canvas-container parent
        console.log(
          "[AsciiRenderer] ResizeObserver container:",
          !!container,
          container?.className,
        );
        if (container) {
          resizeObserverRef.current = new ResizeObserver((entries) => {
            const entry = entries[0];
            if (!entry) return;
            const newWidth = Math.round(entry.contentRect.width);
            const newHeight = Math.round(entry.contentRect.height);
            console.log("[AsciiRenderer] ResizeObserver fired!");
            console.log("[AsciiRenderer] New contentRect:", {
              width: entry.contentRect.width,
              height: entry.contentRect.height,
            });
            console.log(
              "[AsciiRenderer] Rounded to:",
              newWidth,
              "x",
              newHeight,
            );
            console.log(
              "[AsciiRenderer] Current canvas:",
              canvas.width,
              "x",
              canvas.height,
            );
            console.log(
              "[AsciiRenderer] ResizeObserver fired:",
              newWidth,
              "x",
              newHeight,
            );

            if (
              newWidth > 0 &&
              newHeight > 0 &&
              moduleRef.current &&
              rendererPtrRef.current
            ) {
              console.log("[RESIZE HANDLER] Updating canvas and renderer");
              try {
                // Destroy old renderer
                moduleRef.current._term_renderer_destroy(
                  rendererPtrRef.current,
                );

                // Create new renderer with updated dimensions
                const configSize = 544;
                const configPtr = moduleRef.current._malloc(configSize);
                const outPtr = moduleRef.current._malloc(8);

                const view = new DataView(
                  moduleRef.current.HEAPU8.buffer,
                  configPtr,
                  configSize,
                );

                // Estimate cols/rows from container size
                let estimatedCols = Math.max(80, Math.floor(newWidth / 10));
                let estimatedRows = Math.max(24, Math.floor(newHeight / 20));

                view.setInt32(0, estimatedCols, true);
                view.setInt32(4, estimatedRows, true);
                view.setFloat64(8, 12.0, true);
                view.setInt32(16, 0, true);

                const fontSpec = "Courier New";
                for (let i = 0; i < fontSpec.length && i < 511; i++) {
                  moduleRef.current.HEAPU8[configPtr + 20 + i] =
                    fontSpec.charCodeAt(i);
                }
                moduleRef.current.HEAPU8[configPtr + 20 + fontSpec.length] = 0;
                moduleRef.current.HEAPU8[configPtr + 532] = 0;

                const fontDataPtr = moduleRef.current._get_font_default_ptr();
                view.setInt32(536, fontDataPtr, true);

                const fontDataSize = moduleRef.current._get_font_default_size();
                view.setInt32(540, fontDataSize, true);

                const result = moduleRef.current._term_renderer_create(
                  configPtr,
                  outPtr,
                );

                if (result !== 0) {
                  throw new Error(
                    `term_renderer_create failed with error code ${result}`,
                  );
                }

                const rendererPtr = new DataView(
                  moduleRef.current.HEAPU8.buffer,
                  outPtr,
                  8,
                ).getBigInt64(0, true);
                rendererPtrRef.current = Number(rendererPtr);

                // Get actual dimensions from new renderer (grid AND pixel)
                const cols = moduleRef.current._term_renderer_get_cols(
                  rendererPtrRef.current,
                );
                const rows = moduleRef.current._term_renderer_get_rows(
                  rendererPtrRef.current,
                );
                const pixelWidth = moduleRef.current._term_renderer_width_px(
                  rendererPtrRef.current,
                );
                const pixelHeight = moduleRef.current._term_renderer_height_px(
                  rendererPtrRef.current,
                );

                // CRITICAL: Update canvas to match renderer's actual pixel output dimensions
                canvas.width = pixelWidth;
                canvas.height = pixelHeight;
                console.log(
                  "[RESIZE HANDLER] Canvas updated to renderer pixel dims: " +
                    canvas.width +
                    "x" +
                    canvas.height,
                );

                // Debounce dimension updates with longer timeout to batch multiple resize events
                // This prevents the render loop from restarting on every single ResizeObserver fire
                pendingDimensionsRef.current = { cols, rows };
                if (resizeTimeoutRef.current) {
                  clearTimeout(resizeTimeoutRef.current);
                }
                resizeTimeoutRef.current = setTimeout(() => {
                  if (pendingDimensionsRef.current) {
                    updateDimensions(
                      pendingDimensionsRef.current.cols,
                      pendingDimensionsRef.current.rows,
                    );
                    pendingDimensionsRef.current = null;
                  }
                  resizeTimeoutRef.current = null;
                }, 300);

                moduleRef.current._free(configPtr);
                moduleRef.current._free(outPtr);
              } catch (err) {
                console.error(
                  "[AsciiRenderer] Failed to resize renderer:",
                  err,
                );
              }
            }
          });

          console.log(
            "[AsciiRenderer] Attaching ResizeObserver to:",
            container?.className,
          );
          resizeObserverRef.current.observe(container);
          console.log("[AsciiRenderer] ResizeObserver attached");
        } else {
          console.warn(
            "[AsciiRenderer] Container not found for ResizeObserver",
          );
        }
      } catch (err) {
        console.error("[AsciiRenderer] Initialization failed:", err);
        setupDoneRef.current = false;
      }
    };

    // Try to initialize synchronously
    let timeoutId: ReturnType<typeof setTimeout> | undefined;
    if (moduleRef.current) {
      initRenderer();
    } else {
      // Module not ready yet - wait one microtask for it to load, then retry
      timeoutId = setTimeout(() => {
        if (moduleRef.current && !setupDoneRef.current) {
          initRenderer();
        }
      }, 0);
    }

    return () => {
      if (timeoutId !== undefined) {
        clearTimeout(timeoutId);
      }
      if (resizeTimeoutRef.current) {
        clearTimeout(resizeTimeoutRef.current);
        resizeTimeoutRef.current = null;
      }
    };
  }, [wasmModuleReady, updateDimensions]);

  const frameCountForLoggingRef = useRef(0);
  const frameHistoryRef = useRef<{ frame: number; lines: string[] }[]>([]);

  useImperativeHandle(
    ref,
    () => ({
      writeFrame(ansiString: string) {
        if (!moduleRef.current || !setupDoneRef.current) {
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
                    const direction = matchedOffset > 0 ? "DOWN" : "UP";
                    const magnitude = Math.abs(matchedOffset);
                    console.log(
                      `[AsciiRenderer] Frame ${frameCountForLoggingRef.current}: Content shifted ${direction} by ${magnitude} rows (${Math.round(
                        blockSimilarity * 5,
                      )} lines) | rows=${dimensionsRef.current.rows}`,
                    );
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

  return (
    <div className="ascii-canvas-container w-full h-full flex flex-col items-center justify-center overflow-hidden relative flex-1">
      <style>
        {`
          canvas {
            display: block;
            image-rendering: pixelated;
            image-rendering: crisp-edges;
          }
        `}
      </style>
      <canvas ref={canvasRef} className="ascii-canvas" />
      {connectionState === 0 && (
        <div className="absolute inset-0 flex items-center justify-center bg-black/40 rounded pointer-events-none">
          <div className="text-5xl font-bold text-red-500 drop-shadow-lg">
            DISCONNECTED
          </div>
        </div>
      )}

      {/* FPS counter - hidden, displayed in control bar instead */}
      {showFps && (
        <div ref={fpsDisplayRef} style={{ display: "none" }}>
          --
        </div>
      )}

      {/* Error bar */}
      {error && (
        <div className="px-4 pb-2">
          <div className="p-4 bg-terminal-1 text-terminal-fg rounded">
            {error}
          </div>
        </div>
      )}
    </div>
  );
});
