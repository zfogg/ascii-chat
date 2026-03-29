import { useEffect, type RefObject } from "react";
import type { MirrorModule } from "../../wasm/mirror";

interface UseInitAsciiRendererParams {
  canvasRef: RefObject<HTMLCanvasElement | null>;
  moduleRef: RefObject<MirrorModule | null>;
  rendererPtrRef: RefObject<number>;
  setupDoneRef: RefObject<boolean>;
  resizeObserverRef: RefObject<ResizeObserver | null>;
  resizeTimeoutRef: RefObject<ReturnType<typeof setTimeout> | null>;
  pendingDimensionsRef: RefObject<{ cols: number; rows: number } | null>;
  updateDimensions: (cols: number, rows: number) => void;
  wasmModuleReady: boolean | undefined;
}

export function useInitAsciiRenderer({
  canvasRef,
  moduleRef,
  rendererPtrRef,
  setupDoneRef,
  resizeObserverRef,
  resizeTimeoutRef,
  pendingDimensionsRef,
  updateDimensions,
  wasmModuleReady,
}: UseInitAsciiRendererParams) {
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
}
