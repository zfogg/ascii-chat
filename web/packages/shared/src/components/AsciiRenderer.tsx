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

  // FPS tracking
  const frameCountRef = useRef(0);
  const fpsUpdateTimeRef = useRef<number | null>(null);
  const fpsDisplayRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    fpsUpdateTimeRef.current = performance.now();
  }, []);

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
    if (!canvasRef.current || setupDoneRef.current) {
      return;
    }

    const canvas = canvasRef.current;

    // Initialize WASM renderer with canvas dimensions
    const initRenderer = () => {
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

        // cols (int32) at offset 0
        view.setInt32(0, 80, true);
        // rows (int32) at offset 4
        view.setInt32(4, 24, true);
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

        // Get actual dimensions from renderer
        const cols = moduleRef.current!._term_renderer_get_cols(
          rendererPtrRef.current,
        );
        const rows = moduleRef.current!._term_renderer_get_rows(
          rendererPtrRef.current,
        );

        updateDimensions(cols, rows);
        setupDoneRef.current = true;
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
    };
  }, [wasmModuleReady, updateDimensions]);

  // Handle canvas resizes
  useEffect(() => {
    if (!canvasRef.current || !moduleRef.current || !setupDoneRef.current) {
      return;
    }

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
        const cols = moduleRef.current._term_renderer_get_cols(
          rendererPtrRef.current,
        );
        const rows = moduleRef.current._term_renderer_get_rows(
          rendererPtrRef.current,
        );

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
              const fbWidth = moduleRef.current._term_renderer_width_px(
                rendererPtrRef.current,
              );
              const fbHeight = moduleRef.current._term_renderer_height_px(
                rendererPtrRef.current,
              );
              const fbStride = moduleRef.current._term_renderer_pitch(
                rendererPtrRef.current,
              );

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
