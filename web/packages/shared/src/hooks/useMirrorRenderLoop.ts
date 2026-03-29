import {
  useEffect,
  useRef,
  type RefObject,
  type MutableRefObject,
} from "react";
import { isWasmReady, convertFrameToAscii } from "../wasm/mirror";
import type { AsciiRendererHandle } from "../components/AsciiRenderer";

interface UseMirrorRenderLoopParams {
  isWebcamRunning: boolean;
  terminalDimensions: { cols: number; rows: number };
  captureFrame: () => {
    data: Uint8Array;
    width: number;
    height: number;
  } | null;
  canvasRef: RefObject<HTMLCanvasElement | null>;
  rendererRef: RefObject<AsciiRendererHandle | null>;
  debugCountRef: MutableRefObject<number>;
  firstFrameTimeRef: MutableRefObject<number | null>;
  frameIntervalRef: MutableRefObject<number>;
}

let loopCounter = 0;

export function useMirrorRenderLoop({
  isWebcamRunning,
  terminalDimensions,
  captureFrame,
  canvasRef,
  rendererRef,
  debugCountRef,
  firstFrameTimeRef,
  frameIntervalRef,
}: UseMirrorRenderLoopParams) {
  const prevDepsRef = useRef<{
    isWebcamRunning: boolean;
    terminalDimensions: { cols: number; rows: number };
    captureFrame: Function;
  } | null>(null);

  useEffect(() => {
    const loopId = `loop${++loopCounter}`;

    if (!isWebcamRunning) {
      return;
    }

    // Skip rendering until terminal dimensions are initialized
    // Without this, WASM tries to render with dst_width=0/dst_height=0, causing memory access out of bounds
    if (terminalDimensions.cols <= 0 || terminalDimensions.rows <= 0) {
      return;
    }

    // Check what changed
    if (prevDepsRef.current) {
      const {
        isWebcamRunning: prevRunning,
        terminalDimensions: prevDims,
        captureFrame: prevCapture,
      } = prevDepsRef.current;
      const changes = [];
      if (prevRunning !== isWebcamRunning) changes.push("isWebcamRunning");
      if (
        prevDims.cols !== terminalDimensions.cols ||
        prevDims.rows !== terminalDimensions.rows
      )
        changes.push(
          `terminalDimensions(${prevDims.cols}x${prevDims.rows}->${terminalDimensions.cols}x${terminalDimensions.rows})`,
        );
      if (prevCapture !== captureFrame) changes.push("captureFrame");
      if (changes.length > 0) {
        console.log(`[${loopId}] Effect triggered by: ${changes.join(", ")}`);
      }
    }
    prevDepsRef.current = { isWebcamRunning, terminalDimensions, captureFrame };

    console.log(
      `[${loopId}] Starting render loop with dimensions ${terminalDimensions.cols}x${terminalDimensions.rows}`,
    );

    let isActive = true;
    let currentRafHandle = 0;
    const isTestMode = new URLSearchParams(window.location.search).has("test");
    let lastFrameTime = performance.now();
    let lastConversionTime = 0;

    const renderFrame = () => {
      if (!isWasmReady() || !rendererRef.current) {
        return;
      }

      const now = performance.now();
      if (firstFrameTimeRef.current === null) {
        firstFrameTimeRef.current = now;
      }

      let frame;

      if (isTestMode) {
        // Generate synthetic test frame
        const canvas = canvasRef.current;
        if (!canvas) return;

        const ctx = canvas.getContext("2d", { willReadFrequently: true });
        if (!ctx) return;

        // Create gradient pattern for testing
        const time = (Date.now() % 10000) / 10000; // Cycle every 10 seconds
        const gradient = ctx.createLinearGradient(0, 0, canvas.width, 0);
        gradient.addColorStop(0, `hsl(${time * 360}, 100%, 50%)`);
        gradient.addColorStop(1, `hsl(${(time + 0.5) * 360}, 100%, 50%)`);

        ctx.fillStyle = gradient;
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        // Add some animated bars
        ctx.fillStyle = "rgba(255, 255, 255, 0.5)";
        const barHeight = canvas.height * 0.1;
        ctx.fillRect(0, canvas.height * time, canvas.width, barHeight);

        const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
        frame = {
          data: new Uint8Array(imageData.data),
          width: canvas.width,
          height: canvas.height,
        };
      } else {
        frame = captureFrame();
      }

      if (!frame) {
        return;
      }

      // Verify frame dimensions match expected RGBA size
      const expectedSize = frame.width * frame.height * 4;
      if (frame.data.length !== expectedSize) {
        return;
      }

      // If last conversion took > 100ms, skip this frame to prevent blocking
      if (lastConversionTime > 100) {
        return;
      }

      const conversionStartTime = performance.now();
      const asciiArt = convertFrameToAscii(
        frame.data,
        frame.width,
        frame.height,
      );
      lastConversionTime = performance.now() - conversionStartTime;

      if (!asciiArt) {
        return;
      }

      // Expose last ANSI frame for E2E test access
      const win = window as unknown as Record<string, unknown>;
      win["__lastAnsiFrame"] = asciiArt;
      win["__lastAnsiFrameTime"] = performance.now();
      win["__lastAnsiFrameCount"] =
        ((win["__lastAnsiFrameCount"] as number) || 0) + 1;

      rendererRef.current!.writeFrame(asciiArt);

      debugCountRef.current++;
    };

    let frameCount = 0;
    const animationFrameRef = (time: DOMHighResTimeStamp) => {
      try {
        frameCount++;
        if (frameCount % 60 === 0) {
          console.log(`[${loopId}] Frame ${frameCount}`);
        }

        const elapsed = time - lastFrameTime;
        const interval = frameIntervalRef.current;

        if (elapsed >= interval) {
          lastFrameTime = time;
          renderFrame();
        }

        if (isActive) {
          currentRafHandle = requestAnimationFrame(animationFrameRef);
        }
      } catch {
        // Silent error catch - don't log in hot loop
      }
    };

    lastFrameTime = performance.now();
    const rafHandle = requestAnimationFrame(animationFrameRef);
    currentRafHandle = rafHandle;

    return () => {
      console.log(`[${loopId}] Cleanup - stopping render loop`);
      isActive = false;
      cancelAnimationFrame(rafHandle);
      cancelAnimationFrame(currentRafHandle);
    };
  }, [isWebcamRunning, captureFrame, terminalDimensions]);
}
