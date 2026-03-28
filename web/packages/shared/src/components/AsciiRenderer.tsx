import {
  forwardRef,
  useCallback,
  useEffect,
  useImperativeHandle,
  useRef,
} from "react";
import { getMirrorModule } from "../wasm/mirror";

// Local type definition to avoid import dependency on wasm module
interface MirrorModule {
  canvas?: HTMLCanvasElement;
  _ascii_renderer_init(width: number, height: number): void;
  _ascii_renderer_render_frame(ptr: number, len: number): void;
  _ascii_renderer_resize(width: number, height: number): void;
  _ascii_renderer_get_cols(): number;
  _ascii_renderer_get_rows(): number;
  _ascii_renderer_get_framebuffer(): number;
  _ascii_renderer_get_framebuffer_width(): number;
  _ascii_renderer_get_framebuffer_height(): number;
  _ascii_renderer_get_framebuffer_stride(): number;
  _ascii_renderer_shutdown(): void;
  _malloc(size: number): number;
  _free(ptr: number): void;
  HEAPU8: Uint8Array;
  UTF8ToString(ptr: number): string;
}

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
  const renderTime = performance.now();
  console.log(
    `[AsciiRenderer] RENDER: wasmModuleReady=${wasmModuleReady} at ${renderTime.toFixed(0)}ms`,
  );
  const previousWasmReadyRef = useRef<boolean | undefined>(undefined);
  if (previousWasmReadyRef.current !== wasmModuleReady) {
    console.log(
      `[AsciiRenderer] ✓ wasmModuleReady CHANGED from ${previousWasmReadyRef.current} to ${wasmModuleReady} at ${renderTime.toFixed(0)}ms - both effects should fire`,
    );
    previousWasmReadyRef.current = wasmModuleReady;
  }
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const moduleRef = useRef<MirrorModule | null>(null);
  const setupDoneRef = useRef(false);
  const dimensionsRef = useRef({ cols: 0, rows: 0 });
  const firstRenderDoneRef = useRef(false);

  // FPS tracking
  const frameCountRef = useRef(0);
  const fpsUpdateTimeRef = useRef<number | null>(null);
  const fpsDisplayRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    fpsUpdateTimeRef.current = performance.now();
  }, []);

  useEffect(() => {
    const effectTime = performance.now();
    console.log(
      `[AsciiRenderer] EFFECT-1 (wasmModuleReady) FIRED at ${effectTime.toFixed(0)}ms: wasmModuleReady=${wasmModuleReady}`,
    );
    if (wasmModuleReady) {
      const getModuleStart = performance.now();
      const module = getMirrorModule();
      const getModuleEnd = performance.now();
      console.log(
        `[AsciiRenderer] getMirrorModule at ${getModuleStart.toFixed(0)}ms returned ${!!module} (took ${(getModuleEnd - getModuleStart).toFixed(1)}ms)`,
      );
      moduleRef.current = module;
      console.log(
        `[AsciiRenderer] moduleRef.current set at ${performance.now().toFixed(0)}ms`,
      );
    }
    return () => {
      console.log(
        `[AsciiRenderer] EFFECT-1 CLEANUP at ${performance.now().toFixed(0)}ms`,
      );
    };
  }, [wasmModuleReady]);

  const updateDimensions = useCallback(
    (cols: number, rows: number) => {
      console.log(
        `[AsciiRenderer] updateDimensions called with ${cols}x${rows}, onDimensionsChange=${!!onDimensionsChange}`,
      );
      dimensionsRef.current = { cols, rows };
      onDimensionsChange?.({ cols, rows });
      console.log(`[AsciiRenderer] onDimensionsChange callback invoked`);
    },
    [onDimensionsChange],
  );

  useEffect(() => {
    const effectStartTime = performance.now();
    console.log(
      `[AsciiRenderer] EFFECT-2 (init) FIRED at ${effectStartTime.toFixed(0)}ms: wasmModuleReady=${wasmModuleReady}, canvas=${!!canvasRef.current}, module=${!!moduleRef.current}, setupDone=${setupDoneRef.current}`,
    );
    if (!canvasRef.current || setupDoneRef.current) {
      console.log(
        `[AsciiRenderer] EFFECT-2 early exit at ${performance.now().toFixed(0)}ms - canvas=${!!canvasRef.current}, setupDone=${setupDoneRef.current}`,
      );
      return;
    }

    console.log(
      `[AsciiRenderer] EFFECT-2 BODY START at ${performance.now().toFixed(0)}ms - initializing canvas`,
    );
    const canvas = canvasRef.current;
    console.log(
      `[AsciiRenderer] Canvas ref assigned at ${performance.now().toFixed(0)}ms`,
    );

    // Initialize WASM renderer with canvas dimensions
    const initRenderer = () => {
      const rendererStartTime = performance.now();
      console.log(
        `[AsciiRenderer initRenderer] STARTED at ${rendererStartTime.toFixed(0)}ms`,
      );

      try {
        const width = canvas.clientWidth || 1280;
        const height = canvas.clientHeight || 720;

        console.log(
          `[AsciiRenderer initRenderer] dimensions: width=${width}, height=${height} at ${performance.now().toFixed(0)}ms`,
        );

        // Set canvas on module for raylib/Emscripten
        if (!moduleRef.current) {
          throw new Error("moduleRef.current is null during initRenderer");
        }

        const canvasSetTime = performance.now();
        moduleRef.current.canvas = canvas;
        console.log(
          `[AsciiRenderer initRenderer] Canvas set on module at ${canvasSetTime.toFixed(0)}ms (took ${(performance.now() - canvasSetTime).toFixed(1)}ms)`,
        );

        // Ensure WebGL context is ready before InitWindow
        // requestAnimationFrame guarantees the canvas is properly laid out and WebGL is available
        const doInit = () => {
          const initCallTime = performance.now();
          console.log(
            `[AsciiRenderer initRenderer] Calling _ascii_renderer_init(${width}, ${height}) at ${initCallTime.toFixed(0)}ms`,
          );
          moduleRef.current!._ascii_renderer_init(width, height);
          console.log(
            `[AsciiRenderer initRenderer] _ascii_renderer_init returned at ${performance.now().toFixed(0)}ms (took ${(performance.now() - initCallTime).toFixed(1)}ms)`,
          );
        };

        const beforeDoInit = performance.now();
        doInit();
        const afterDoInit = performance.now();
        console.log(
          `[AsciiRenderer initRenderer] doInit completed at ${afterDoInit.toFixed(0)}ms (${(afterDoInit - beforeDoInit).toFixed(1)}ms total)`,
        );

        const getDimsStart = performance.now();
        const cols = moduleRef.current!._ascii_renderer_get_cols();
        const rows = moduleRef.current!._ascii_renderer_get_rows();

        console.log(
          `[AsciiRenderer initRenderer] Got dimensions at ${performance.now().toFixed(0)}ms: ${cols}x${rows} (took ${(performance.now() - getDimsStart).toFixed(1)}ms)`,
        );

        if (cols === 0 || rows === 0) {
          console.warn(`[AsciiRenderer] Invalid dimensions: ${cols}x${rows}`);
        }

        const updateDimsTime = performance.now();
        updateDimensions(cols, rows);
        console.log(
          `[AsciiRenderer initRenderer] updateDimensions called at ${updateDimsTime.toFixed(0)}ms (took ${(performance.now() - updateDimsTime).toFixed(1)}ms)`,
        );

        setupDoneRef.current = true;
        const totalTime = performance.now() - rendererStartTime;
        console.log(
          `[AsciiRenderer initRenderer] Setup complete at ${performance.now().toFixed(0)}ms (TOTAL: ${totalTime.toFixed(1)}ms)`,
        );
      } catch (err) {
        console.error("[AsciiRenderer] Initialization failed:", err);
        setupDoneRef.current = false;
      }
    };

    console.log(
      `[AsciiRenderer] initRenderer function defined at ${performance.now().toFixed(0)}ms`,
    );

    // Try to initialize synchronously
    let timeoutId: ReturnType<typeof setTimeout> | undefined;
    if (moduleRef.current) {
      console.log(
        `[AsciiRenderer] EFFECT-2: Module ready, initializing at ${performance.now().toFixed(0)}ms`,
      );
      initRenderer();
    } else {
      // Module not ready yet - wait one microtask for it to load, then retry
      console.log(
        `[AsciiRenderer] EFFECT-2: Module not ready, scheduling retry at ${performance.now().toFixed(0)}ms`,
      );
      timeoutId = setTimeout(() => {
        if (moduleRef.current && !setupDoneRef.current) {
          console.log(
            `[AsciiRenderer] EFFECT-2: Retry - module now ready at ${performance.now().toFixed(0)}ms, initializing`,
          );
          initRenderer();
        } else if (setupDoneRef.current) {
          console.log(`[AsciiRenderer] EFFECT-2: Retry - already initialized`);
        } else {
          console.log(
            `[AsciiRenderer] EFFECT-2: Retry - module still not ready at ${performance.now().toFixed(0)}ms`,
          );
        }
      }, 0);
    }

    return () => {
      const cleanupTime = performance.now();
      const timeSinceSchedule = cleanupTime - effectStartTime;
      console.log(
        `[AsciiRenderer] EFFECT-2 CLEANUP at ${cleanupTime.toFixed(0)}ms: setupDone=${setupDoneRef.current}, timeSinceScheduled=${timeSinceSchedule.toFixed(1)}ms`,
      );
      if (timeoutId !== undefined) {
        clearTimeout(timeoutId);
      }
    };
  }, [wasmModuleReady, updateDimensions]);

  // Handle canvas resizes
  useEffect(() => {
    if (!canvasRef.current || !moduleRef.current || !setupDoneRef.current)
      return;

    const canvas = canvasRef.current;
    const handleResize = () => {
      const width = canvas.clientWidth;
      const height = canvas.clientHeight;

      // CRITICAL: Never resize to 0x0 dimensions. ResizeObserver fires before layout is complete,
      // and 0x0 would reset our valid dimensions from the initial render.
      if (width === 0 || height === 0) {
        return;
      }

      if (firstRenderDoneRef.current && moduleRef.current) {
        moduleRef.current._ascii_renderer_resize(width, height);
        const cols = moduleRef.current._ascii_renderer_get_cols();
        const rows = moduleRef.current._ascii_renderer_get_rows();

        updateDimensions(cols, rows);
      }
    };

    const resizeObserver = new ResizeObserver(handleResize);
    resizeObserver.observe(canvas);

    window.addEventListener("resize", handleResize);

    return () => {
      resizeObserver.disconnect();
      window.removeEventListener("resize", handleResize);
    };
  }, [moduleRef, updateDimensions]);

  const frameCountForLoggingRef = useRef(0);

  useImperativeHandle(
    ref,
    () => ({
      writeFrame(ansiString: string) {
        if (!moduleRef.current || !setupDoneRef.current) {
          console.warn("[AsciiRenderer] writeFrame called but not ready:", {
            hasModule: !!moduleRef.current,
            setupDone: setupDoneRef.current,
            stringLength: ansiString.length,
          });
          return;
        }

        // Log middle 100 chars of first frame and every 60th frame
        frameCountForLoggingRef.current++;
        if (
          frameCountForLoggingRef.current === 1 ||
          frameCountForLoggingRef.current % 60 === 0
        ) {
          const start = Math.max(0, Math.floor(ansiString.length / 2) - 50);
          const middle100 = ansiString.substring(start, start + 100);
          console.log(
            `[Frame ${frameCountForLoggingRef.current}] Middle 100 chars:`,
            middle100,
          );
        }

        try {
          console.log(
            `[Frame ${frameCountForLoggingRef.current}] writeFrame called with ${ansiString.length} bytes`,
          );

          // Encode string to UTF-8 bytes
          const encoder = new TextEncoder();
          const data = encoder.encode(ansiString);

          console.log(
            "[DEBUG writeFrame] ansiString first 50 chars:",
            ansiString.substring(0, 50),
          );
          console.log(
            "[DEBUG writeFrame] ansiString.length:",
            ansiString.length,
          );
          console.log("[DEBUG writeFrame] encoded data.length:", data.length);
          console.log(
            "[DEBUG writeFrame] data is Uint8Array:",
            data instanceof Uint8Array,
          );

          // Allocate memory in WASM and copy data
          const ptr = moduleRef.current._malloc(data.length);
          console.log(
            "[DEBUG writeFrame] _malloc(",
            data.length,
            ") returned:",
            ptr,
          );
          console.log("[DEBUG writeFrame] typeof ptr:", typeof ptr);

          if (!ptr) {
            console.error("[AsciiRenderer] Failed to allocate memory in WASM");
            return;
          }

          const wasmMemory = new Uint8Array(moduleRef.current.HEAPU8.buffer);
          console.log(
            "[DEBUG writeFrame] wasmMemory.length:",
            wasmMemory.length,
          );
          console.log(
            "[DEBUG writeFrame] About to copy data to WASM memory at offset",
            ptr,
          );

          wasmMemory.set(data, ptr);
          console.log("[DEBUG writeFrame] Memory copy successful");

          // Check the function before calling it
          console.log(
            "[DEBUG writeFrame] moduleRef.current._ascii_renderer_render_frame type:",
            typeof moduleRef.current._ascii_renderer_render_frame,
          );
          console.log(
            "[DEBUG writeFrame] Function exists:",
            !!moduleRef.current._ascii_renderer_render_frame,
          );

          // Log what we're about to call
          console.log(
            "[DEBUG writeFrame] About to call _ascii_renderer_render_frame with:",
            {
              ptr: ptr,
              "data.length": data.length,
              ptr_type: typeof ptr,
              len_type: typeof data.length,
            },
          );

          // Render frame - direct call to WASM function
          console.log(
            `[writeFrame] Calling _ascii_renderer_render_frame(${ptr}, ${data.length})`,
          );
          console.log(
            `[writeFrame] ansiString contains: ${ansiString.substring(0, 200).replace(/\n/g, "\\n")}`,
          );
          moduleRef.current._ascii_renderer_render_frame(ptr, data.length);
          console.log(
            `[writeFrame] _ascii_renderer_render_frame returned successfully`,
          );

          // Free memory
          moduleRef.current._free(ptr);

          // Display framebuffer on canvas
          try {
            const canvas = canvasRef.current;
            if (
              canvas &&
              canvas.getContext &&
              moduleRef.current._ascii_renderer_get_framebuffer
            ) {
              const fbPtr =
                moduleRef.current._ascii_renderer_get_framebuffer?.();
              const fbWidth =
                moduleRef.current._ascii_renderer_get_framebuffer_width?.();
              const fbHeight =
                moduleRef.current._ascii_renderer_get_framebuffer_height?.();
              const fbStride =
                moduleRef.current._ascii_renderer_get_framebuffer_stride?.();

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
                const stride: number = fbStride ? (fbStride as number) : w * 3;

                // Read RGB24 framebuffer from WASM memory
                const fbData: Uint8Array = new Uint8Array(
                  moduleRef.current.HEAPU8.buffer,
                  fbPtr,
                  h * stride,
                );

                // Create ImageData (RGBA)
                const imageData = new ImageData(w, h);
                let dstIdx = 0;

                // Convert RGB24 to RGBA32
                for (let y = 0; y < h; y++) {
                  const rowStart: number = y * stride;
                  for (let x = 0; x < w; x++) {
                    const srcIdx: number = rowStart + x * 3;
                    const r: number = fbData[srcIdx] ?? 0;
                    const g: number = fbData[srcIdx + 1] ?? 0;
                    const b: number = fbData[srcIdx + 2] ?? 0;
                    imageData.data[dstIdx++] = r;
                    imageData.data[dstIdx++] = g;
                    imageData.data[dstIdx++] = b;
                    imageData.data[dstIdx++] = 255;
                  }
                }

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
        moduleRef.current._ascii_renderer_render_frame(0, 0);
      },
    }),
    [showFps, onFpsChange],
  );

  return (
    <>
      {/* ASCII canvas output */}
      <div className="h-full flex flex-col flex-1 overflow-hidden min-h-0 relative">
        <canvas
          ref={canvasRef}
          className="flex flex-1 w-full h-full rounded bg-terminal-bg"
          style={{ display: "block" }}
          width={1280}
          height={720}
        />
        {connectionState === 0 && (
          <div className="absolute inset-0 flex items-center justify-center bg-black/40 rounded pointer-events-none">
            <div className="text-5xl font-bold text-red-500 drop-shadow-lg">
              DISCONNECTED
            </div>
          </div>
        )}
      </div>

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
    </>
  );
});
