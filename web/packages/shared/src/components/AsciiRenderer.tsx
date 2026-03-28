import {
  forwardRef,
  useCallback,
  useEffect,
  useImperativeHandle,
  useRef,
} from "react";

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
  wasmModule?: any;
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
    wasmModule,
  },
  ref,
) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const moduleRef = useRef<any>(null);
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
    moduleRef.current = wasmModule;
  }, [wasmModule]);

  const updateDimensions = useCallback(
    (cols: number, rows: number) => {
      dimensionsRef.current = { cols, rows };
      onDimensionsChange?.({ cols, rows });
    },
    [onDimensionsChange],
  );

  useEffect(() => {
    console.log(
      `[AsciiRenderer] Init effect check: canvas=${!!canvasRef.current}, module=${!!moduleRef.current}, setupDone=${setupDoneRef.current}`
    );

    if (!canvasRef.current || !moduleRef.current || setupDoneRef.current) {
      console.log("[AsciiRenderer] Skipping init (missing dependency or already done)");
      return;
    }

    const canvas = canvasRef.current;

    // Initialize WASM renderer with canvas dimensions
    const initRenderer = () => {
      console.log(
        `[AsciiRenderer] initRenderer called. Canvas dims: ${canvas.clientWidth}x${canvas.clientHeight}`
      );

      try {
        const width = canvas.clientWidth || 1280;
        const height = canvas.clientHeight || 720;

        console.log(`[AsciiRenderer] Using dimensions: ${width}x${height}`);

        // Set canvas on module for raylib/Emscripten
        if (!moduleRef.current) {
          throw new Error("moduleRef.current is null during initRenderer");
        }

        moduleRef.current.canvas = canvas;
        console.log("[AsciiRenderer] Canvas set on module");

        console.log(
          `[AsciiRenderer] Calling _ascii_renderer_init(${width}, ${height})`
        );
        const initStart = performance.now();
        moduleRef.current._ascii_renderer_init(width, height);
        const initTime = performance.now() - initStart;
        console.log(`[AsciiRenderer] _ascii_renderer_init returned in ${initTime.toFixed(2)}ms`);

        console.log(`[AsciiRenderer] Getting dimensions...`);
        const cols = moduleRef.current._ascii_renderer_get_cols();
        const rows = moduleRef.current._ascii_renderer_get_rows();

        console.log(`[AsciiRenderer] Got dimensions: ${cols}x${rows}`);

        if (cols === 0 || rows === 0) {
          console.warn(
            `[AsciiRenderer] WARNING: Got invalid dimensions from WASM: ${cols}x${rows}`
          );
        }

        updateDimensions(cols, rows);
        setupDoneRef.current = true;

        console.log(
          `[AsciiRenderer] Initialized: ${width}x${height}px, ${cols}x${rows} cells`
        );
      } catch (err) {
        console.error("[AsciiRenderer] Initialization failed:", err);
        setupDoneRef.current = false;
      }
    };

    // Wait for canvas to have layout
    let timer: number | undefined;
    if (canvas.clientWidth > 0 && canvas.clientHeight > 0) {
      console.log("[AsciiRenderer] Canvas has dimensions, initializing immediately");
      initRenderer();
    } else {
      console.log(
        `[AsciiRenderer] Canvas has no dimensions (${canvas.clientWidth}x${canvas.clientHeight}), waiting 100ms...`
      );
      timer = window.setTimeout(initRenderer, 100);
    }

    return () => {
      if (timer !== undefined) {
        console.log("[AsciiRenderer] Cleanup: clearing init timeout");
        clearTimeout(timer);
      }
    };
  }, [wasmModule, updateDimensions]);

  // Handle canvas resizes
  useEffect(() => {
    if (!canvasRef.current || !moduleRef.current || !setupDoneRef.current)
      return;

    const canvas = canvasRef.current;
    const handleResize = () => {
      const width = canvas.clientWidth;
      const height = canvas.clientHeight;

      console.log(
        `[AsciiRenderer] ResizeObserver fired: canvas=${width}x${height}px, firstRenderDone=${firstRenderDoneRef.current}`
      );

      // CRITICAL: Never resize to 0x0 dimensions. ResizeObserver fires before layout is complete,
      // and 0x0 would reset our valid dimensions from the initial render.
      if (width === 0 || height === 0) {
        console.log(
          `[AsciiRenderer] Ignoring resize to 0x0 (layout not ready yet)`
        );
        return;
      }

      if (firstRenderDoneRef.current) {
        console.log(
          `[AsciiRenderer] Calling _ascii_renderer_resize(${width}, ${height})`
        );
        moduleRef.current._ascii_renderer_resize(width, height);
        const cols = moduleRef.current._ascii_renderer_get_cols();
        const rows = moduleRef.current._ascii_renderer_get_rows();

        console.log(
          `[AsciiRenderer] WASM returned new dimensions: ${cols}x${rows}`
        );

        updateDimensions(cols, rows);

        console.log(`[AsciiRenderer] Resized to ${cols}x${rows} cells`);
      } else {
        console.log(
          `[AsciiRenderer] Skipping resize: first render not done yet`
        );
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

        try {
          // Encode string to UTF-8 bytes
          const encoder = new TextEncoder();
          const data = encoder.encode(ansiString);

          // Allocate memory in WASM and copy data
          const ptr = moduleRef.current._malloc(data.length);
          if (!ptr) {
            console.error(
              "[AsciiRenderer] Failed to allocate memory in WASM"
            );
            return;
          }

          const wasmMemory = new Uint8Array(moduleRef.current.HEAPU8.buffer);
          wasmMemory.set(data, ptr);

          // Render frame - direct call to WASM function
          const renderStart = performance.now();
          moduleRef.current._ascii_renderer_render_frame(ptr, data.length);
          const renderTime = performance.now() - renderStart;

          if (frameCountRef.current === 0) {
            console.log(
              `[AsciiRenderer] First frame rendered in ${renderTime.toFixed(2)}ms, data size ${data.length} bytes`
            );
          }

          // Free memory
          moduleRef.current._free(ptr);

          // Mark first render as done so resize can proceed
          if (!firstRenderDoneRef.current) {
            firstRenderDoneRef.current = true;
            console.log("[AsciiRenderer] First render complete, resize now enabled");
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
        console.log("[AsciiRenderer] clear()");
        moduleRef.current._ascii_renderer_render_frame(0, 0);
      },
    }),
    [showFps, onFpsChange]
  );

  return (
    <>
      {/* ASCII canvas output */}
      <div className="h-full flex flex-col flex-1 overflow-hidden min-h-0 relative">
        <canvas
          ref={canvasRef}
          className="flex flex-1 w-full h-full rounded bg-terminal-bg"
          style={{ display: "block" }}
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
