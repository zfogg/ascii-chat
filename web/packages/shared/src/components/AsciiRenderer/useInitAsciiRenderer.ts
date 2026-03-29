import { type RefObject, useCallback, useEffect, useRef } from "react";
import { getMirrorModule, type MirrorModule } from "../../wasm/mirror";
import { getMatrixRain } from "../../wasm";

interface UseInitAsciiRendererReturn {
  moduleRef: RefObject<MirrorModule | null>;
  setupDoneRef: RefObject<boolean>;
  rendererPtrRef: RefObject<number>;
  resizeTimeoutRef: RefObject<ReturnType<typeof setTimeout> | null>;
  setUpdateDimensions: (fn: (cols: number, rows: number) => void) => void;
  triggerRendererRecreate: () => void;
}

interface UseInitAsciiRendererParams {
  canvasRef: RefObject<HTMLCanvasElement | null>;
  wasmModuleReady: boolean | undefined;
  matrixMode?: boolean;
}

export function useInitAsciiRenderer({
  canvasRef,
  wasmModuleReady,
  matrixMode = false,
}: UseInitAsciiRendererParams): UseInitAsciiRendererReturn {
  const moduleRef = useRef<MirrorModule | null>(null);
  const setupDoneRef = useRef(false);
  const rendererPtrRef = useRef<number>(0);
  const updateDimensionsRef = useRef<
    ((cols: number, rows: number) => void) | null
  >(null);
  const resizeObserverRef = useRef<ResizeObserver | null>(null);
  const resizeTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const pendingDimensionsRef = useRef<{ cols: number; rows: number } | null>(
    null,
  );
  const currentMatrixModeRef = useRef<boolean>(false);

  // Load WASM module when ready
  useEffect(() => {
    if (wasmModuleReady) {
      const module = getMirrorModule();
      moduleRef.current = module;
    }
  }, [wasmModuleReady]);

  // Track matrix mode so resize can recreate renderer with correct font
  useEffect(() => {
    currentMatrixModeRef.current = matrixMode;
  }, [matrixMode]);

  // Write config struct fields to WASM memory
  // Struct layout (WASM 32-bit pointers):
  // int cols (4) + int rows (4) + double font_size_pt (8) + int theme (4) = 20 bytes
  // char font_spec[512] at offset 20 = 512 bytes
  // bool font_is_path (1) + padding (3) = 4 bytes at offset 532
  // uint8_t *font_data (4) at offset 536
  // size_t font_data_size (4) at offset 540
  // Total: 544 bytes
  const writeConfigStructFields = useCallback(
    (configPtr: number, cols: number, rows: number, isMatrixMode: boolean) => {
      if (!moduleRef.current) {
        throw new Error(
          "moduleRef.current is null during writeConfigStructFields",
        );
      }

      const view = new DataView(
        moduleRef.current.HEAPU8.buffer,
        configPtr,
        544,
      );

      view.setInt32(0, cols, true); // cols
      view.setInt32(4, rows, true); // rows
      view.setFloat64(8, 12.0, true); // font_size_pt
      view.setInt32(16, 0, true); // theme

      // Use "Matrix" font_spec when matrix_rain is enabled, otherwise use default font selection
      const fontSpec = isMatrixMode ? "Matrix" : "";
      console.log("[writeConfigStructFields]", {
        cols,
        rows,
        isMatrixMode,
        fontSpec,
      });
      for (let i = 0; i < fontSpec.length && i < 511; i++) {
        moduleRef.current.HEAPU8[configPtr + 20 + i] = fontSpec.charCodeAt(i);
      }
      moduleRef.current.HEAPU8[configPtr + 20 + fontSpec.length] = 0; // null terminate
      moduleRef.current.HEAPU8[configPtr + 532] = 0; // font_is_path

      const fontDataPtr = moduleRef.current._get_font_default_ptr();
      view.setInt32(536, fontDataPtr, true); // font_data pointer

      const fontDataSize = moduleRef.current._get_font_default_size();
      view.setInt32(540, fontDataSize, true); // font_data_size
    },
    [],
  );

  // Allocate and prepare config struct with estimated dimensions
  const createConfigStruct = useCallback(
    (
      containerWidth: number,
      containerHeight: number,
      isMatrixMode: boolean = false,
    ) => {
      try {
        if (!moduleRef.current) {
          throw new Error(
            "moduleRef.current is null during createConfigStruct",
          );
        }

        const configSize = 544;
        const configPtr = moduleRef.current._malloc(configSize);

        if (!configPtr) {
          throw new Error("_malloc returned null for config struct");
        }

        // Estimate cols/rows from container size
        // Rough estimate: 10px wide, 20px tall per cell (for normal fonts)
        // For matrix fonts: cell height is typically 32px (2x width) due to aspect ratio correction
        // BUT: cap container dimensions to prevent cascading resize loops
        const maxContainerHeight = 2000;
        const effectiveHeight = Math.min(containerHeight, maxContainerHeight);
        const estimatedCols = Math.max(80, Math.floor(containerWidth / 10));

        // Use larger pixel-per-row estimate for matrix mode (32px) vs normal mode (20px)
        const pixelsPerRow = isMatrixMode ? 32 : 20;
        const estimatedRows = Math.max(
          24,
          Math.floor(effectiveHeight / pixelsPerRow),
        );

        // Sanity check: ensure grid dimensions are reasonable
        // With capped container height of 2000px:
        //   - Normal fonts: 2000px / 20px per row = max 100 rows
        //   - Matrix fonts: 2000px / 32px per row = max 62 rows
        // Cols are typically 80-256 depending on window width
        const maxReasonableCols = 400;
        const maxReasonableRows = isMatrixMode ? 100 : 150;

        if (
          estimatedCols > maxReasonableCols ||
          estimatedRows > maxReasonableRows
        ) {
          moduleRef.current._free(configPtr);
          const error = new Error(
            `[createConfigStruct] Grid dimensions unreasonable: ${estimatedCols}x${estimatedRows} exceeds ${maxReasonableCols}x${maxReasonableRows} (containerHeight=${containerHeight}px was capped to ${effectiveHeight}px)`,
          );
          console.error("[createConfigStruct] DIMENSION SANITY CHECK FAILED", {
            estimatedCols,
            estimatedRows,
            containerWidth,
            containerHeight,
            effectiveHeight,
            maxReasonableCols,
            maxReasonableRows,
          });
          throw error;
        }

        console.log("[createConfigStruct] ALLOCATING", {
          configPtr,
          configSize,
          containerWidth,
          containerHeight,
          estimatedCols,
          estimatedRows,
          isMatrixMode,
        });

        writeConfigStructFields(
          configPtr,
          estimatedCols,
          estimatedRows,
          isMatrixMode,
        );

        console.log("[createConfigStruct] CONFIG WRITTEN, returning configPtr");

        return { configPtr, estimatedCols, estimatedRows };
      } catch (err) {
        console.error("[createConfigStruct] ERROR:", err);
        throw err;
      }
    },
    [writeConfigStructFields],
  );

  // Call WASM renderer creation and return renderer pointer
  const callRendererCreate = useCallback(
    (configPtr: number, outPtr: number) => {
      if (!moduleRef.current) {
        throw new Error("moduleRef.current is null during callRendererCreate");
      }

      console.log("[callRendererCreate] CALLING _term_renderer_create", {
        configPtr,
        outPtr,
      });

      const result = moduleRef.current._term_renderer_create(configPtr, outPtr);

      console.log("[callRendererCreate] _term_renderer_create returned", {
        result,
      });

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
      console.log("[callRendererCreate] EXTRACTED rendererPtr", {
        rendererPtr: Number(rendererPtr),
      });
      return Number(rendererPtr);
    },
    [],
  );

  // Get actual renderer dimensions (grid and pixel)
  const getRendererDimensions = useCallback(() => {
    if (!moduleRef.current) {
      throw new Error("moduleRef.current is null during getRendererDimensions");
    }

    console.log(
      "[getRendererDimensions] CALLING getter functions with rendererPtr",
      {
        rendererPtr: rendererPtrRef.current,
      },
    );

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

    console.log("[getRendererDimensions] RETRIEVED", {
      cols,
      rows,
      pixelWidth,
      pixelHeight,
    });

    return {
      cols,
      rows,
      pixelWidth,
      pixelHeight,
    };
  }, []);

  // Handle container resize by recreating renderer and updating canvas
  const handleContainerResize = useCallback(
    (newWidth: number, newHeight: number, canvas: HTMLCanvasElement) => {
      if (
        !moduleRef.current ||
        !rendererPtrRef.current ||
        newWidth <= 0 ||
        newHeight <= 0
      ) {
        return;
      }

      // Cap container height to prevent feedback loops where expanding canvas
      // causes ResizeObserver to report larger container, creating even larger renderer
      // Use viewport height as the hard limit - content should fit in visible area
      const cappedHeight = Math.min(newHeight, window.innerHeight);
      if (cappedHeight < newHeight) {
        console.log("[handleContainerResize] Capping height to viewport:", {
          originalHeight: newHeight,
          cappedHeight,
          viewportHeight: window.innerHeight,
        });
      }

      try {
        // Destroy old renderer
        console.log("[handleContainerResize] DESTROYING old renderer", {
          oldRendererPtr: rendererPtrRef.current,
        });
        moduleRef.current._term_renderer_destroy(rendererPtrRef.current);

        // Create new renderer with updated dimensions, using cached matrix mode
        console.log("[handleContainerResize] STARTING RECREATION", {
          newWidth,
          newHeight,
          currentMatrixMode: currentMatrixModeRef.current,
        });
        const { configPtr, estimatedCols, estimatedRows } = createConfigStruct(
          newWidth,
          cappedHeight,
          currentMatrixModeRef.current,
        );
        console.log("[handleContainerResize] createConfigStruct returned", {
          configPtr,
          estimatedCols,
          estimatedRows,
        });
        const outPtr = moduleRef.current._malloc(8);
        console.log("[handleContainerResize] Allocated outPtr", { outPtr });

        console.log(`[handleContainerResize] CALLING callRendererCreate...`);
        const rendererPtr = callRendererCreate(configPtr, outPtr);
        rendererPtrRef.current = rendererPtr;

        console.log("[handleContainerResize] CALLING getRendererDimensions");
        const dims = getRendererDimensions();

        if (dims.pixelHeight === 0) {
          console.error("[handleContainerResize] ERROR: pixelHeight=0!", dims);
        }

        console.log("[handleContainerResize] SETTING canvas dimensions", {
          width: dims.pixelWidth,
          height: dims.pixelHeight,
        });
        canvas.width = dims.pixelWidth;
        canvas.height = dims.pixelHeight;

        // Debounce dimension updates to batch multiple resize events
        // Prevents render loop from restarting on every ResizeObserver fire
        pendingDimensionsRef.current = { cols: dims.cols, rows: dims.rows };
        if (resizeTimeoutRef.current) {
          clearTimeout(resizeTimeoutRef.current);
        }
        resizeTimeoutRef.current = setTimeout(() => {
          if (pendingDimensionsRef.current) {
            updateDimensionsRef.current?.(
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
        console.error("[AsciiRenderer] FATAL: Failed to resize renderer:", err);
        if (err instanceof Error) {
          console.error("[AsciiRenderer] Error message:", err.message);
          console.error("[AsciiRenderer] Stack:", err.stack);

          // If dimension validation failed, try to recover with more conservative dimensions
          if (
            err.message.includes("dimension") ||
            err.message.includes("limit")
          ) {
            console.warn(
              "[AsciiRenderer] Dimension validation failed, attempting recovery with smaller grid",
            );
            try {
              // Retry with a capped grid size to avoid cascading allocation
              const recoveryWidth = Math.min(newWidth, 1280);
              const recoveryHeight = Math.min(cappedHeight, 2000); // Use same max as createConfigStruct
              const recoveryPixelsPerRow = currentMatrixModeRef.current
                ? 32
                : 20;
              const recoveryCols = Math.max(80, Math.floor(recoveryWidth / 10));
              const recoveryRows = Math.max(
                24,
                Math.floor(recoveryHeight / recoveryPixelsPerRow),
              );

              console.log(
                "[AsciiRenderer] Recovery attempt with capped dimensions:",
                {
                  recoveryCols,
                  recoveryRows,
                  originalWidth: newWidth,
                  originalHeight: newHeight,
                  recoveryWidth,
                  recoveryHeight,
                },
              );

              const { configPtr: retryConfigPtr } = createConfigStruct(
                recoveryWidth,
                recoveryHeight,
                currentMatrixModeRef.current,
              );
              const retryOutPtr = moduleRef.current?._malloc(8);
              if (retryOutPtr) {
                const retryRendererPtr = callRendererCreate(
                  retryConfigPtr,
                  retryOutPtr,
                );
                rendererPtrRef.current = retryRendererPtr;
                const retryDims = getRendererDimensions();
                canvas.width = retryDims.pixelWidth;
                canvas.height = retryDims.pixelHeight;
                moduleRef.current?._free(retryConfigPtr);
                moduleRef.current?._free(retryOutPtr);
                console.log(
                  "[AsciiRenderer] Recovery successful with dimensions:",
                  retryDims,
                );
              }
            } catch (recoveryErr) {
              console.error(
                "[AsciiRenderer] Recovery also failed:",
                recoveryErr,
              );
            }
          }
        }
      }
    },
    [createConfigStruct, callRendererCreate, getRendererDimensions],
  );

  // Set up ResizeObserver on container
  const setupResizeObserver = useCallback(
    (container: HTMLElement, canvas: HTMLCanvasElement) => {
      resizeObserverRef.current = new ResizeObserver((entries) => {
        const entry = entries[0];
        if (!entry) return;

        const newWidth = Math.round(entry.contentRect.width);
        const newHeight = Math.round(entry.contentRect.height);

        console.log("[ResizeObserver] size change", {
          newWidth,
          newHeight,
          canvasCurrentWidth: canvas.width,
          canvasCurrentHeight: canvas.height,
        });

        handleContainerResize(newWidth, newHeight, canvas);
      });

      resizeObserverRef.current.observe(container);
    },
    [handleContainerResize],
  );

  // Initialize WASM renderer and set up observation
  const initializeRenderer = useCallback(
    (canvas: HTMLCanvasElement) => {
      if (!moduleRef.current) {
        throw new Error("moduleRef.current is null during initializeRenderer");
      }

      moduleRef.current.canvas = canvas;

      const container = canvas.parentElement;
      const containerRect = container?.getBoundingClientRect();
      const containerWidth = Math.round(containerRect?.width ?? 1200);
      const containerHeight = Math.round(containerRect?.height ?? 1000);

      // Create config and renderer, checking current matrix mode
      const isMatrixMode = getMatrixRain();
      currentMatrixModeRef.current = isMatrixMode;
      const { configPtr } = createConfigStruct(
        containerWidth,
        containerHeight,
        isMatrixMode,
      );
      const outPtr = moduleRef.current._malloc(8);

      const rendererPtr = callRendererCreate(configPtr, outPtr);
      rendererPtrRef.current = rendererPtr;

      // Get actual dimensions and set canvas size
      const dims = getRendererDimensions();
      canvas.width = dims.pixelWidth;
      canvas.height = dims.pixelHeight;

      // Free temporary buffers
      moduleRef.current._free(configPtr);
      moduleRef.current._free(outPtr);

      // Notify parent of initial dimensions
      updateDimensionsRef.current?.(dims.cols, dims.rows);
      setupDoneRef.current = true;

      // Set up resize observation
      if (container) {
        setupResizeObserver(container, canvas);
      }
    },
    [
      createConfigStruct,
      callRendererCreate,
      getRendererDimensions,
      setupResizeObserver,
    ],
  );

  useEffect(() => {
    if (!canvasRef.current) {
      return;
    }

    const canvas = canvasRef.current;

    if (setupDoneRef.current) {
      return;
    }

    // Try to initialize synchronously
    let timeoutId: ReturnType<typeof setTimeout> | undefined;

    try {
      if (moduleRef.current) {
        initializeRenderer(canvas);
      } else {
        // Module not ready yet - wait one microtask for it to load, then retry
        timeoutId = setTimeout(() => {
          if (moduleRef.current && !setupDoneRef.current) {
            initializeRenderer(canvas);
          }
        }, 0);
      }
    } catch (err) {
      console.error("[AsciiRenderer] Initialization failed:", err);
      setupDoneRef.current = false;
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
  }, [wasmModuleReady, canvasRef, initializeRenderer]);

  const triggerRendererRecreate = useCallback(() => {
    if (!moduleRef.current || !canvasRef.current || !setupDoneRef.current) {
      return;
    }
    const canvas = canvasRef.current;
    const container = canvas.parentElement;
    if (container) {
      const rect = container.getBoundingClientRect();
      // Cap dimensions to prevent cascading growth when toggling matrix mode
      // Use viewport dimensions as hard limit - canvas should never exceed visible area
      const maxWidth = Math.min(rect.width, window.innerWidth);
      const maxHeight = Math.min(rect.height, window.innerHeight);
      handleContainerResize(
        Math.round(maxWidth),
        Math.round(maxHeight),
        canvas,
      );
    }
  }, [handleContainerResize, canvasRef]);

  // Recreate renderer when matrix mode changes
  useEffect(() => {
    if (setupDoneRef.current && canvasRef.current) {
      triggerRendererRecreate();
    }
  }, [matrixMode, triggerRendererRecreate, canvasRef]);

  return {
    moduleRef,
    setupDoneRef,
    rendererPtrRef,
    resizeTimeoutRef,
    setUpdateDimensions: (fn: (cols: number, rows: number) => void) => {
      updateDimensionsRef.current = fn;
    },
    triggerRendererRecreate,
  };
}
