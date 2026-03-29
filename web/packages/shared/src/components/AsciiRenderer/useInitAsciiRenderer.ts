import { type RefObject, useCallback, useEffect, useRef } from "react";
import { getMirrorModule, type MirrorModule } from "../../wasm/mirror";

interface UseInitAsciiRendererReturn {
  moduleRef: RefObject<MirrorModule | null>;
  setupDoneRef: RefObject<boolean>;
  rendererPtrRef: RefObject<number>;
  resizeTimeoutRef: RefObject<ReturnType<typeof setTimeout> | null>;
  setUpdateDimensions: (fn: (cols: number, rows: number) => void) => void;
}

interface UseInitAsciiRendererParams {
  canvasRef: RefObject<HTMLCanvasElement | null>;
  wasmModuleReady: boolean | undefined;
}

export function useInitAsciiRenderer({
  canvasRef,
  wasmModuleReady,
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

  // Load WASM module when ready
  useEffect(() => {
    if (wasmModuleReady) {
      const module = getMirrorModule();
      moduleRef.current = module;
    }
  }, [wasmModuleReady]);

  // Create renderer config struct in WASM memory with estimated dimensions
  const createConfigStruct = useCallback(
    (containerWidth: number, containerHeight: number) => {
      if (!moduleRef.current) {
        throw new Error("moduleRef.current is null during createConfigStruct");
      }

      const configSize = 544;
      const configPtr = moduleRef.current._malloc(configSize);

      const view = new DataView(
        moduleRef.current.HEAPU8.buffer,
        configPtr,
        configSize,
      );

      // Estimate cols/rows from container size
      // Rough estimate: 10px wide, 20px tall per cell
      const estimatedCols = Math.max(80, Math.floor(containerWidth / 10));
      const estimatedRows = Math.max(24, Math.floor(containerHeight / 20));

      // Write config struct to WASM memory using direct offsets
      // Struct layout (WASM 32-bit pointers):
      // int cols (4) + int rows (4) + double font_size_pt (8) + int theme (4) = 20 bytes
      // char font_spec[512] at offset 20 = 512 bytes
      // bool font_is_path (1) + padding (3) = 4 bytes at offset 532
      // uint8_t *font_data (4) at offset 536
      // size_t font_data_size (4) at offset 540
      // Total: 544 bytes
      view.setInt32(0, estimatedCols, true); // cols
      view.setInt32(4, estimatedRows, true); // rows
      view.setFloat64(8, 12.0, true); // font_size_pt
      view.setInt32(16, 0, true); // theme

      const fontSpec = "Courier New";
      for (let i = 0; i < fontSpec.length && i < 511; i++) {
        moduleRef.current.HEAPU8[configPtr + 20 + i] = fontSpec.charCodeAt(i);
      }
      moduleRef.current.HEAPU8[configPtr + 20 + fontSpec.length] = 0; // null terminate
      moduleRef.current.HEAPU8[configPtr + 532] = 0; // font_is_path

      const fontDataPtr = moduleRef.current._get_font_default_ptr();
      view.setInt32(536, fontDataPtr, true); // font_data pointer

      const fontDataSize = moduleRef.current._get_font_default_size();
      view.setInt32(540, fontDataSize, true); // font_data_size

      return { configPtr, estimatedCols, estimatedRows };
    },
    [],
  );

  // Call WASM renderer creation and return renderer pointer
  const callRendererCreate = useCallback(
    (configPtr: number, outPtr: number) => {
      if (!moduleRef.current) {
        throw new Error("moduleRef.current is null during callRendererCreate");
      }

      const result = moduleRef.current._term_renderer_create(configPtr, outPtr);

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
      return Number(rendererPtr);
    },
    [],
  );

  // Get actual renderer dimensions (grid and pixel)
  const getRendererDimensions = useCallback(() => {
    if (!moduleRef.current) {
      throw new Error("moduleRef.current is null during getRendererDimensions");
    }

    return {
      cols: moduleRef.current._term_renderer_get_cols(rendererPtrRef.current),
      rows: moduleRef.current._term_renderer_get_rows(rendererPtrRef.current),
      pixelWidth: moduleRef.current._term_renderer_width_px(
        rendererPtrRef.current,
      ),
      pixelHeight: moduleRef.current._term_renderer_height_px(
        rendererPtrRef.current,
      ),
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

      try {
        // Destroy old renderer
        moduleRef.current._term_renderer_destroy(rendererPtrRef.current);

        // Create new renderer with updated dimensions
        const { configPtr } = createConfigStruct(newWidth, newHeight);
        const outPtr = moduleRef.current._malloc(8);

        const rendererPtr = callRendererCreate(configPtr, outPtr);
        rendererPtrRef.current = rendererPtr;

        const dims = getRendererDimensions();
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
        console.error("[AsciiRenderer] Failed to resize renderer:", err);
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

      // Create config and renderer
      const { configPtr } = createConfigStruct(containerWidth, containerHeight);
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
  }, [wasmModuleReady, initializeRenderer]);

  return {
    moduleRef,
    setupDoneRef,
    rendererPtrRef,
    resizeTimeoutRef,
    setUpdateDimensions: (fn: (cols: number, rows: number) => void) => {
      updateDimensionsRef.current = fn;
    },
  };
}
