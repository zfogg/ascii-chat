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
  console.log(
    `[AsciiRenderer-Props] Component called with wasmModule=${!!wasmModule} at ${performance.now().toFixed(0)}ms`,
  );
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

  // Track prop changes
  useEffect(() => {
    console.log(
      `[AsciiRenderer-PropTrack] wasmModule prop changed to ${!!wasmModule} at ${performance.now().toFixed(0)}ms`,
    );
  }, [wasmModule]);

  useEffect(() => {
    const now = performance.now();
    console.log(
      `[AsciiRenderer-ModuleRef] *** MODULEREF EFFECT ENTERED at ${now.toFixed(0)}ms, wasmModule=${!!wasmModule} ***`,
    );
    moduleRef.current = wasmModule;
    console.log(
      `[AsciiRenderer-ModuleRef] moduleRef.current assigned at ${performance.now().toFixed(0)}ms`,
    );
  }, [wasmModule]);

  const updateDimensions = useCallback(
    (cols: number, rows: number) => {
      dimensionsRef.current = { cols, rows };
      onDimensionsChange?.({ cols, rows });
    },
    [onDimensionsChange],
  );

  useEffect(() => {
    const now = performance.now();
    console.log(
      `[AsciiRenderer-Layout] *** LAYOUT EFFECT ENTERED at ${now.toFixed(0)}ms: canvas=${!!canvasRef.current}, moduleRef=${!!moduleRef.current}, setupDone=${setupDoneRef.current} ***`,
    );

    if (!canvasRef.current || !moduleRef.current || setupDoneRef.current) {
      const reasons = [];
      if (!canvasRef.current) reasons.push("no-canvas");
      if (!moduleRef.current) reasons.push("no-moduleRef");
      if (setupDoneRef.current) reasons.push("already-setup");
      console.log(
        `[AsciiRenderer-Layout] Skipping: ${reasons.join(", ")} at ${performance.now().toFixed(0)}ms`,
      );
      return;
    }

    const canvas = canvasRef.current;

    // Initialize WASM renderer with canvas dimensions
    const initRenderer = () => {
      const initTime = performance.now();
      console.log(
        `[AsciiRenderer-Init] initRenderer called at ${initTime.toFixed(0)}ms. Canvas dims: ${canvas.clientWidth}x${canvas.clientHeight}`,
      );

      try {
        const width = canvas.clientWidth || 1280;
        const height = canvas.clientHeight || 720;

        console.log(
          `[AsciiRenderer-Init] Using dimensions: ${width}x${height}`,
        );

        // Set canvas on module for raylib/Emscripten
        if (!moduleRef.current) {
          throw new Error("moduleRef.current is null during initRenderer");
        }

        console.log(
          `[AsciiRenderer-Init] Setting canvas on module at ${performance.now().toFixed(0)}ms`,
        );
        moduleRef.current.canvas = canvas;
        console.log(`[AsciiRenderer-Init] Canvas set successfully at ${performance.now().toFixed(0)}ms`);

        // Ensure WebGL context is ready before InitWindow
        // requestAnimationFrame guarantees the canvas is properly laid out and WebGL is available
        const doInit = () => {
          const beforeInit = performance.now();
          console.log(
            `[AsciiRenderer-Init] *** CALLING _ascii_renderer_init at ${beforeInit.toFixed(0)}ms (${width}x${height}) ***`,
          );
          console.time("[TIMING] _ascii_renderer_init");
          moduleRef.current._ascii_renderer_init(width, height);
          console.timeEnd("[TIMING] _ascii_renderer_init");
          console.log(
            `[AsciiRenderer-Init] _ascii_renderer_init returned at ${performance.now().toFixed(0)}ms`,
          );
        };

        doInit();

        const beforeGetCols = performance.now();
        console.log(`[AsciiRenderer-Init] Getting cols at ${beforeGetCols.toFixed(0)}ms...`);
        const cols = moduleRef.current._ascii_renderer_get_cols();
        console.log(
          `[AsciiRenderer-Init] Got cols=${cols} at ${performance.now().toFixed(0)}ms`,
        );
        const beforeGetRows = performance.now();
        console.log(`[AsciiRenderer-Init] Getting rows at ${beforeGetRows.toFixed(0)}ms...`);
        const rows = moduleRef.current._ascii_renderer_get_rows();
        console.log(
          `[AsciiRenderer-Init] Got rows=${rows} at ${performance.now().toFixed(0)}ms`,
        );

        console.log(`[AsciiRenderer-Init] Dimensions: ${cols}x${rows}`);

        if (cols === 0 || rows === 0) {
          console.warn(
            `[AsciiRenderer-Init] WARNING: Invalid dimensions: ${cols}x${rows}`,
          );
        }

        console.log(`[AsciiRenderer-Init] Calling updateDimensions(${cols}, ${rows}) at ${performance.now().toFixed(0)}ms`);
        updateDimensions(cols, rows);
        setupDoneRef.current = true;

        console.log(
          `[AsciiRenderer-Init] *** INITIALIZATION COMPLETE at ${performance.now().toFixed(0)}ms: ${width}x${height}px, ${cols}x${rows} cells ***`,
        );
      } catch (err) {
        console.error("[AsciiRenderer-Init] Initialization failed:", err);
        setupDoneRef.current = false;
      }
    };

    // Wait for canvas to have layout by using requestAnimationFrame
    // This lets the browser complete layout calculations before we check dimensions
    let rafId: number | undefined;
    let retryCount = 0;
    let startTime = performance.now();
    const maxRetries = 10; // Only retry 10 times (~160ms with RAF)
    const maxWaitTime = 500; // Max 500ms wait before using fallback dimensions

    const checkAndInit = () => {
      retryCount++;
      const elapsed = performance.now() - startTime;
      const dims = canvas ? `${canvas.clientWidth}x${canvas.clientHeight}` : 'NO_CANVAS';

      console.log(
        `[AsciiRenderer-RAF] Attempt ${retryCount} at ${elapsed.toFixed(0)}ms: canvas=${dims}`,
      );

      if (canvas.clientWidth > 0 && canvas.clientHeight > 0) {
        console.log(
          `[AsciiRenderer-RAF] ✓ Canvas has dimensions on attempt ${retryCount} at ${elapsed.toFixed(0)}ms - CALLING initRenderer`,
        );
        initRenderer();
      } else if (retryCount < maxRetries && elapsed < maxWaitTime) {
        console.log(
          `[AsciiRenderer-RAF] Retrying ${retryCount}/${maxRetries} at ${performance.now().toFixed(0)}ms...`,
        );
        // Use RAF to wait for next layout cycle
        rafId = requestAnimationFrame(checkAndInit);
      } else {
        console.warn(
          `[AsciiRenderer-RAF] TIMEOUT after ${retryCount} attempts/${elapsed.toFixed(0)}ms - forcing initRenderer with fallback`,
        );
        // Force initialization with fallback dimensions after timeout
        initRenderer();
      }
    };

    // Start checking on next animation frame
    const rafStartTime = performance.now();
    console.log(
      `[AsciiRenderer-RAF] *** STARTING RAF LOOP at ${rafStartTime.toFixed(0)}ms ***`,
    );
    rafId = requestAnimationFrame(checkAndInit);

    return () => {
      if (rafId !== undefined) {
        console.log("[AsciiRenderer] Cleanup: canceling RAF");
        cancelAnimationFrame(rafId);
      }
    };
  }, [wasmModule]);

  // Handle canvas resizes
  useEffect(() => {
    if (!canvasRef.current || !moduleRef.current || !setupDoneRef.current)
      return;

    const canvas = canvasRef.current;
    const handleResize = () => {
      const width = canvas.clientWidth;
      const height = canvas.clientHeight;

      console.log(
        `[AsciiRenderer] ResizeObserver fired: canvas=${width}x${height}px, firstRenderDone=${firstRenderDoneRef.current}`,
      );

      // CRITICAL: Never resize to 0x0 dimensions. ResizeObserver fires before layout is complete,
      // and 0x0 would reset our valid dimensions from the initial render.
      if (width === 0 || height === 0) {
        console.log(
          `[AsciiRenderer] Ignoring resize to 0x0 (layout not ready yet)`,
        );
        return;
      }

      if (firstRenderDoneRef.current) {
        console.log(
          `[AsciiRenderer] Calling _ascii_renderer_resize(${width}, ${height})`,
        );
        moduleRef.current._ascii_renderer_resize(width, height);
        const cols = moduleRef.current._ascii_renderer_get_cols();
        const rows = moduleRef.current._ascii_renderer_get_rows();

        console.log(
          `[AsciiRenderer] WASM returned new dimensions: ${cols}x${rows}`,
        );

        updateDimensions(cols, rows);

        console.log(`[AsciiRenderer] Resized to ${cols}x${rows} cells`);
      } else {
        console.log(
          `[AsciiRenderer] Skipping resize: first render not done yet`,
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
            console.error("[AsciiRenderer] Failed to allocate memory in WASM");
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
              `[AsciiRenderer] First frame rendered in ${renderTime.toFixed(2)}ms, data size ${data.length} bytes`,
            );
          }

          // Free memory
          moduleRef.current._free(ptr);

          // TODO: Display framebuffer on canvas
          // Currently disabled - testing if writeFrame itself works first
          // try {
          //   const canvas = canvasRef.current;
          //   if (
          //     canvas &&
          //     canvas.getContext &&
          //     moduleRef.current._ascii_renderer_get_framebuffer
          //   ) {
          //     const fbPtr = moduleRef.current._ascii_renderer_get_framebuffer?.();
          //     const fbWidth = moduleRef.current._ascii_renderer_get_framebuffer_width?.();
          //     const fbHeight = moduleRef.current._ascii_renderer_get_framebuffer_height?.();
          //     const fbStride = moduleRef.current._ascii_renderer_get_framebuffer_stride?.();

          //     if (
          //       fbPtr &&
          //       fbWidth &&
          //       fbHeight &&
          //       fbWidth > 0 &&
          //       fbHeight > 0 &&
          //       typeof fbWidth === "number" &&
          //       typeof fbHeight === "number"
          //     ) {
          //       const w: number = fbWidth as number;
          //       const h: number = fbHeight as number;
          //       const stride: number = fbStride ? (fbStride as number) : w * 3;

          //       // Read RGB24 framebuffer from WASM memory
          //       const fbData: Uint8Array = new Uint8Array(
          //         moduleRef.current.HEAPU8.buffer,
          //         fbPtr,
          //         h * stride
          //       );

          //       // Create ImageData (RGBA)
          //       const imageData = new ImageData(w, h);
          //       let dstIdx = 0;

          //       // Convert RGB24 to RGBA32
          //       for (let y = 0; y < h; y++) {
          //         const rowStart: number = y * stride;
          //         for (let x = 0; x < w; x++) {
          //           const srcIdx: number = rowStart + x * 3;
          //           const r: number = fbData[srcIdx] ?? 0;
          //           const g: number = fbData[srcIdx + 1] ?? 0;
          //           const b: number = fbData[srcIdx + 2] ?? 0;
          //           imageData.data[dstIdx++] = r;
          //           imageData.data[dstIdx++] = g;
          //           imageData.data[dstIdx++] = b;
          //           imageData.data[dstIdx++] = 255;
          //         }
          //       }

          //       // Display on canvas
          //       const ctx = canvas.getContext("2d");
          //       if (ctx) {
          //         ctx.putImageData(imageData, 0, 0);
          //       }
          //     }
          //   }
          // } catch (displayErr) {
          //   console.error("[AsciiRenderer] Failed to display framebuffer:", displayErr);
          // }

          // Mark first render as done so resize can proceed
          if (!firstRenderDoneRef.current) {
            firstRenderDoneRef.current = true;
            console.log(
              "[AsciiRenderer] First render complete, resize now enabled",
            );
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
