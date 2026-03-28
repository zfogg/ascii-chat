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
    if (!canvasRef.current || !moduleRef.current || setupDoneRef.current) {
      return;
    }

    const canvas = canvasRef.current;

    // Initialize WASM renderer with canvas dimensions
    const initRenderer = () => {
      try {
        const width = canvas.clientWidth || 1280;
        const height = canvas.clientHeight || 720;

        // Set canvas on module for raylib/Emscripten
        moduleRef.current.canvas = canvas;

        console.log(
          `[AsciiRenderer] Calling _ascii_renderer_init(${width}, ${height})`
        );
        moduleRef.current._ascii_renderer_init(width, height);

        console.log(`[AsciiRenderer] Getting dimensions...`);
        const cols = moduleRef.current._ascii_renderer_get_cols();
        const rows = moduleRef.current._ascii_renderer_get_rows();

        console.log(
          `[AsciiRenderer] Got dimensions: ${cols}x${rows}`
        );
        updateDimensions(cols, rows);
        setupDoneRef.current = true;

        console.log(
          `[AsciiRenderer] Initialized: ${width}x${height}px, ${cols}x${rows} cells`
        );
      } catch (err) {
        console.error("[AsciiRenderer] Initialization failed:", err);
      }
    };

    // Wait for canvas to have layout
    let timer: number | undefined;
    if (canvas.clientWidth > 0 && canvas.clientHeight > 0) {
      initRenderer();
    } else {
      timer = window.setTimeout(initRenderer, 100);
    }

    return () => {
      if (timer !== undefined) {
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

      if (width > 0 && height > 0 && firstRenderDoneRef.current) {
        moduleRef.current._ascii_renderer_resize(width, height);
        const cols = moduleRef.current._ascii_renderer_get_cols();
        const rows = moduleRef.current._ascii_renderer_get_rows();
        updateDimensions(cols, rows);

        console.log(`[AsciiRenderer] Resized to ${cols}x${rows} cells`);
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
          });
          return;
        }

        try {
          // Encode string to UTF-8 bytes
          const encoder = new TextEncoder();
          const data = encoder.encode(ansiString);

          // Allocate memory in WASM and copy data
          const ptr = moduleRef.current._malloc(data.length);
          const wasmMemory = new Uint8Array(moduleRef.current.HEAPU8.buffer);
          wasmMemory.set(data, ptr);

          // Render frame - direct call to WASM function
          moduleRef.current._ascii_renderer_render_frame(ptr, data.length);

          // Free memory
          moduleRef.current._free(ptr);

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
