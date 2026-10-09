import {
  useEffect,
  useRef,
  type RefObject,
  type MutableRefObject,
} from "react";
import {
  isWasmReady,
  convertFrameToAscii,
  renderAnalyserAudioVisualization,
  renderAudioVisualizationFrame,
} from "../wasm/mirror";
import type { AsciiRendererHandle } from "../components";
import { fillTestPatternAudioSamples } from "../testPattern";
import { useTestPattern } from "./useTestPattern";

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
  streamRef: MutableRefObject<MediaStream | null>;
  animation?: "matrix" | "waveform" | "fft";
  animationEnabled?: boolean;
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
  streamRef,
  animation,
  animationEnabled,
}: UseMirrorRenderLoopParams) {
  const testPattern = useTestPattern();
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
    const testMode = testPattern.enabled;
    let lastFrameTime = performance.now();
    let lastConversionTime = 0;
    const audioSamples = new Float32Array(1024);
    let audioContext: AudioContext | null = null;
    let analyser: AnalyserNode | null = null;
    const renderFrame = () => {
      if (!isWasmReady() || !rendererRef.current) {
        return;
      }

      const now = performance.now();
      if (firstFrameTimeRef.current === null) {
        firstFrameTimeRef.current = now;
      }

      let frame;
      let asciiArt = "";

      if (
        animationEnabled &&
        (animation === "waveform" || animation === "fft")
      ) {
        if (
          !testMode &&
          !analyser &&
          streamRef.current?.getAudioTracks().length
        ) {
          audioContext = new AudioContext();
          analyser = audioContext.createAnalyser();
          analyser.fftSize = 2048;
          audioContext
            .createMediaStreamSource(streamRef.current)
            .connect(analyser);
        }
        for (let index = 0; index < audioSamples.length; index++) {
          audioSamples[index] = 0;
        }
        if (testMode) fillTestPatternAudioSamples(audioSamples, now);
        asciiArt = testMode
          ? renderAudioVisualizationFrame(
              audioSamples,
              terminalDimensions.cols,
              terminalDimensions.rows,
              { source: "microphone", mode: animation },
            )
          : analyser
            ? renderAnalyserAudioVisualization(
                analyser,
                terminalDimensions.cols,
                terminalDimensions.rows,
                { source: "microphone", mode: animation },
              )
            : "";
      } else if (testMode) {
        // Generate synthetic test frame
        const canvas = canvasRef.current;
        if (!canvas) return;

        const ctx = canvas.getContext("2d", { willReadFrequently: true });
        if (!ctx) return;

        const imageData = testPattern.drawFrame(
          ctx,
          canvas.width,
          canvas.height,
        );
        if (!imageData) return;
        frame = {
          data: new Uint8Array(imageData.data.buffer),
          width: canvas.width,
          height: canvas.height,
        };
      } else {
        frame = captureFrame();
      }

      if (!asciiArt && !frame) {
        return;
      }

      if (!asciiArt) {
        // Verify frame dimensions match expected RGBA size
        const expectedSize = frame!.width * frame!.height * 4;
        if (frame!.data.length !== expectedSize) {
          return;
        }

        // Yield one frame after a slow conversion, then allow conversion again.
        if (lastConversionTime > 100) {
          lastConversionTime = 0;
          return;
        }

        const conversionStartTime = performance.now();
        asciiArt = convertFrameToAscii(
          frame!.data,
          frame!.width,
          frame!.height,
        );
        lastConversionTime = performance.now() - conversionStartTime;
      }

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
    let reportedFrameError = false;
    const animationFrameRef = (time: DOMHighResTimeStamp) => {
      try {
        frameCount++;
        if (frameCount % 60 === 0) {
          console.log(`[${loopId}] Frame ${frameCount}`);
        }

        const elapsed = time - lastFrameTime;
        const interval = frameIntervalRef.current;
        // requestAnimationFrame timestamps can fall a fraction of a millisecond
        // below 1000 / targetFps at a nominal 60 Hz. Requiring a strict interval
        // then skips that callback and halves the render rate. Keep the cadence
        // anchored to the deadline while accepting normal timestamp jitter.
        const tolerance = Math.min(1, interval / 20);

        if (elapsed + tolerance >= interval) {
          const intervalsElapsed = Math.max(
            1,
            Math.floor((elapsed + tolerance) / interval),
          );
          lastFrameTime += intervalsElapsed * interval;
          renderFrame();
          reportedFrameError = false;
        }
      } catch (error) {
        if (!reportedFrameError) {
          console.warn(
            "[Mirror] Frame rendering failed; retrying next frame",
            error,
          );
          reportedFrameError = true;
        }
      } finally {
        // A transient capture/conversion failure must not cancel the RAF chain.
        if (isActive) {
          currentRafHandle = requestAnimationFrame(animationFrameRef);
        }
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
      void audioContext?.close();
    };
  }, [
    isWebcamRunning,
    captureFrame,
    terminalDimensions,
    debugCountRef,
    rendererRef,
    frameIntervalRef,
    canvasRef,
    firstFrameTimeRef,
    animation,
    animationEnabled,
    streamRef,
    testPattern,
  ]);
}
