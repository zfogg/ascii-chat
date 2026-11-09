import { AsciiRenderer } from "ascii-chat";
import { useLayoutEffect } from "react";
import { RasterWorker } from "./RasterWorker";
import { browserKeyCode } from "./keyboard";
import type { SessionUiEvent } from "./types";
import type { SessionUiBinding } from "../../wasm/generated/mirror-bindings";
import type { KeyboardEvent } from "react";
import {
  type ForwardedRef,
  type RefObject,
  useCallback,
  useEffect,
  useImperativeHandle,
  useRef,
} from "react";
import type { AsciiRendererHandle } from "./types";
import type { MirrorModule } from "../../wasm/mirror";
import type { AsciiRendererBinding } from "../../wasm/generated/mirror-bindings";
import type { ClientLikeKind } from "../../wasm/assetUrl";
import { startGridStabilityProbe } from "./gridStabilityProbe";

interface UseAsciiRendererHandleParams {
  ref: ForwardedRef<AsciiRendererHandle>;
  clientLikeKind: ClientLikeKind;
  workerRendering?: boolean;
  matrixMode?: boolean;
  moduleRef: RefObject<MirrorModule | null>;
  setupDoneRef: RefObject<boolean>;
  rendererPtrRef: RefObject<AsciiRendererBinding | null>;
  canvasRef: RefObject<HTMLCanvasElement | null>;
  resizeTimeoutRef: RefObject<ReturnType<typeof setTimeout> | null>;
  showFps: boolean;
  uiLoading?: boolean;
  uiMessage?: string | undefined;
  onUiAction?: ((event: SessionUiEvent) => void) | undefined;
  zalgo?: number;
  onFpsChange: ((fps: number) => void) | undefined;
  onDimensionsChange:
    | ((dims: { cols: number; rows: number }) => void)
    | undefined;
  onRecreateRenderer?: () => void;
}

interface UseAsciiRendererHandleReturn {
  onKeyDown: (event: KeyboardEvent<HTMLCanvasElement>) => void;
  updateDimensions: (cols: number, rows: number) => void;
  fpsDisplayRef: RefObject<HTMLDivElement | null>;
}

export function useAsciiRendererHandle({
  ref,
  clientLikeKind,
  workerRendering = false,
  matrixMode = false,
  moduleRef,
  setupDoneRef,
  rendererPtrRef,
  canvasRef,
  resizeTimeoutRef,
  showFps,
  uiLoading = false,
  uiMessage = "Initializing...",
  onUiAction,
  zalgo = 0,
  onFpsChange,
  onDimensionsChange,
  onRecreateRenderer,
}: UseAsciiRendererHandleParams): UseAsciiRendererHandleReturn {
  const firstRenderDoneRef = useRef(false);
  const presentedRasterRef = useRef<{
    renderer: AsciiRendererBinding | null;
    width: number;
    height: number;
  }>({ renderer: null, width: 0, height: 0 });
  const dimensionsRef = useRef({ cols: 0, rows: 0 });
  const presentedFrameTimesRef = useRef<number[]>([]);
  const lastReportedFpsRef = useRef<number | null>(null);
  const fpsDisplayRef = useRef<HTMLDivElement>(null);
  const imageDataRef = useRef<ImageData | null>(null);
  const lastFrameChangedRef = useRef(false);
  const lastContentRef = useRef("");

  useEffect(() => {
    if (!showFps) {
      presentedFrameTimesRef.current = [];
      lastReportedFpsRef.current = null;
      return;
    }

    const updateFps = () => {
      const now = performance.now();
      const cutoff = now - 1000;
      while (
        presentedFrameTimesRef.current.length > 0 &&
        presentedFrameTimesRef.current[0]! < cutoff
      ) {
        presentedFrameTimesRef.current.shift();
      }
      const fps = presentedFrameTimesRef.current.length;
      if (lastReportedFpsRef.current === fps) return;
      lastReportedFpsRef.current = fps;
      if (fpsDisplayRef.current)
        fpsDisplayRef.current.textContent = String(fps);
      onFpsChange?.(fps);
    };

    updateFps();
    const timer = window.setInterval(updateFps, 250);
    return () => window.clearInterval(timer);
  }, [showFps, onFpsChange]);

  const updateDimensions = useCallback(
    (cols: number, rows: number) => {
      dimensionsRef.current = { cols, rows };
      onDimensionsChange?.({ cols, rows });
    },
    [onDimensionsChange],
  );

  const uiRef = useRef<SessionUiBinding | null>(null);
  const uiModuleRef = useRef<MirrorModule | null>(null);
  const uiRendererRef = useRef<AsciiRendererBinding | null>(null);
  const uiProps = useRef({ uiLoading, uiMessage, onUiAction });
  useLayoutEffect(() => {
    uiProps.current = { uiLoading, uiMessage, onUiAction };
  }, [uiLoading, uiMessage, onUiAction]);
  const ensureUi = useCallback(() => {
    const module = moduleRef.current;
    if (!module || !setupDoneRef.current) return null;
    if (uiModuleRef.current !== module || !uiRef.current) {
      uiRef.current?.delete();
      uiRef.current = null;
      uiModuleRef.current = null;
      const binding = new module.SessionUiBinding();
      if (!binding.valid()) {
        binding.delete();
        throw new Error("Cannot create session UI");
      }
      uiRef.current = binding;
      uiModuleRef.current = module;
    }
    const ui = uiRef.current;
    if (uiRendererRef.current !== rendererPtrRef.current) {
      uiRendererRef.current = rendererPtrRef.current;
      ui.redraw();
    }
    ui.resize(dimensionsRef.current.cols, dimensionsRef.current.rows);
    ui.loading(uiProps.current.uiLoading, uiProps.current.uiMessage);
    return ui;
  }, [moduleRef, setupDoneRef, rendererPtrRef]);

  const rasterWorkerRef = useRef<RasterWorker | null>(null);
  useEffect(() => {
    if (!workerRendering) return;
    const worker = new RasterWorker(
      clientLikeKind,
      ({ pixels, width, height, changed }) => {
        const canvas = canvasRef.current;
        if (
          !canvas ||
          resizeTimeoutRef.current ||
          canvas.width !== width ||
          canvas.height !== height
        ) {
          uiRef.current?.redraw();
          return;
        }
        const context = canvas.getContext("2d");
        if (!context) return;
        context.putImageData(new ImageData(pixels, width, height), 0, 0);
        presentedRasterRef.current = {
          renderer: rendererPtrRef.current,
          width,
          height,
        };
        firstRenderDoneRef.current = true;
        lastFrameChangedRef.current = changed;
        if (showFps) presentedFrameTimesRef.current.push(performance.now());
      },
      (error) => {
        console.error(
          "[AsciiRenderer] Worker failed; using synchronous renderer",
          error,
        );
        worker.close();
        rasterWorkerRef.current = null;
        uiRef.current?.redraw();
      },
    );
    rasterWorkerRef.current = worker;
    return () => {
      worker.close();
      rasterWorkerRef.current = null;
    };
  }, [
    clientLikeKind,
    workerRendering,
    canvasRef,
    resizeTimeoutRef,
    rendererPtrRef,
    showFps,
  ]);

  const paintFrame = useCallback(
    (ansiString: string): boolean => {
      lastFrameChangedRef.current = false;
      if (!moduleRef.current || !setupDoneRef.current) {
        return false;
      }

      // Skip rendering during resize debounce to avoid dimension mismatches
      if (resizeTimeoutRef.current) {
        return false;
      }
      if (rasterWorkerRef.current) {
        rasterWorkerRef.current.write({
          text: ansiString,
          ...dimensionsRef.current,
          matrix: matrixMode,
          zalgo:
            Number.isInteger(zalgo) && zalgo >= 1 && zalgo <= 10 ? zalgo : 0,
        });
        return true;
      }
      try {
        const renderer = rendererPtrRef.current;
        const canvas = canvasRef.current;
        if (!renderer || !canvas)
          throw new Error("[AsciiRenderer] Renderer or canvas is unavailable");
        const w = canvas.width,
          h = canvas.height;
        const rendererDimensions = renderer.dimensions();
        if (
          w <= 0 ||
          h <= 0 ||
          rendererDimensions.width !== w ||
          rendererDimensions.height !== h
        )
          throw new Error(
            `[AsciiRenderer] Invalid framebuffer dimensions ${w}x${h}`,
          );
        const pixels = AsciiRenderer.rasterize(
          renderer,
          ansiString,
          Number.isInteger(zalgo) && zalgo >= 1 && zalgo <= 10 ? zalgo : 0,
        );
        lastFrameChangedRef.current = ansiString !== lastContentRef.current;
        lastContentRef.current = ansiString;
        const ctx = canvas.getContext("2d");
        if (!ctx) throw new Error("[AsciiRenderer] Canvas context not found");
        ctx.putImageData(new ImageData(pixels, w, h), 0, 0);
        presentedRasterRef.current = { renderer, width: w, height: h };
        if (
          new URLSearchParams(window.location.search).get("verifyGrid") === "1"
        )
          startGridStabilityProbe(canvas);
        // Mark first render as done so resize can proceed
        if (!firstRenderDoneRef.current) {
          firstRenderDoneRef.current = true;
        }
      } catch (err) {
        console.error("[AsciiRenderer] writeFrame error:", err);
        return false;
      }

      if (showFps) {
        const now = performance.now();
        presentedFrameTimesRef.current.push(now);
      }
      return true;
    },
    [
      moduleRef,
      setupDoneRef,
      resizeTimeoutRef,
      rendererPtrRef,
      canvasRef,
      zalgo,
      matrixMode,
      showFps,
    ],
  );

  useEffect(() => {
    let id = 0;
    let lastMessage = "";
    const tick = () => {
      const ui = ensureUi();
      if (ui && !resizeTimeoutRef.current) {
        if (lastMessage !== uiProps.current.uiMessage) {
          ui.log(uiProps.current.uiMessage);
          lastMessage = uiProps.current.uiMessage;
        }
        const frame = ui.tick(performance.now());
        if (frame !== undefined && !paintFrame(frame)) ui.redraw();
      }
      id = requestAnimationFrame(tick);
    };
    id = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(id);
  }, [ensureUi, paintFrame, resizeTimeoutRef]);
  useEffect(
    () => () => {
      uiRef.current?.delete();
      uiRef.current = null;
      uiModuleRef.current = null;
    },
    [],
  );

  const onKeyDown = useCallback(
    (event: KeyboardEvent<HTMLCanvasElement>) => {
      if (event.altKey || event.ctrlKey || event.metaKey) return;
      const ui = ensureUi();
      if (!ui) return;
      const code = browserKeyCode(event.key, event.shiftKey);
      const action = code ? ui.key(code) : 0;
      if (action) {
        event.preventDefault();
        uiProps.current.onUiAction?.({
          action,
          key: event.key,
          module: moduleRef.current!,
        });
        const frame = ui.tick(performance.now());
        if (frame !== undefined && !paintFrame(frame)) ui.redraw();
      }
    },
    [ensureUi, paintFrame],
  );

  useImperativeHandle(
    ref,
    () => ({
      getCanvas: () => {
        const canvas = canvasRef.current;
        const presented = presentedRasterRef.current;
        // Resizing clears the backing surface before the replacement renderer
        // presents. Consumers must hold their last frame through that interval.
        return canvas &&
          !resizeTimeoutRef.current &&
          presented.renderer === rendererPtrRef.current &&
          presented.width === canvas.width &&
          presented.height === canvas.height
          ? canvas
          : null;
      },
      writeFrame(ansiString: string): boolean {
        lastFrameChangedRef.current = false;
        const ui = ensureUi();
        if (!ui) return false;
        try {
          if (ui.media(ansiString) !== 0)
            throw new Error("Session UI rejected media frame");
          // Keep the newest frame while help/loading owns presentation.
          const frame = ui.tick(performance.now());
          if (frame === undefined) return true;
          const painted = paintFrame(frame);
          if (!painted) ui.redraw();
          return painted;
        } catch (error) {
          console.error("[AsciiRenderer] Frame rejected:", error);
          return false;
        }
      },

      getLastFrameChanged() {
        return lastFrameChangedRef.current;
      },

      getDimensions() {
        return dimensionsRef.current;
      },

      clear() {
        const ui = ensureUi();
        if (!ui) return;
        ui.clear();
        const frame = ui.tick(performance.now());
        if (frame !== undefined && !paintFrame(frame)) ui.redraw();
      },

      recreateRenderer() {
        onRecreateRenderer?.();
      },
    }),
    [
      ensureUi,
      paintFrame,
      onRecreateRenderer,
      canvasRef,
      resizeTimeoutRef,
      rendererPtrRef,
    ],
  );

  return {
    onKeyDown,
    updateDimensions,
    fpsDisplayRef,
  };
}
