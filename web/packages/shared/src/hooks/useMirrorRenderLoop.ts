import {
  useEffect,
  useRef,
  type RefObject,
  type MutableRefObject,
} from "react";
import {
  isWasmReady,
  convertFrameToAscii,
  renderAudioVisualization,
  submitAudioVisualizationSamples,
  renderAudioVisualizationFrame,
} from "../wasm/mirror";
import type { AsciiRendererHandle } from "../components";
import { fillTestPatternAudioSamples } from "../testPattern";
import { useTestPattern } from "./useTestPattern";
import { captureVisualizationAudio } from "../audio/visualizationCapture";
import { VideoFrameClock } from "./videoFrameClock";
import {
  reportCameraTrack,
  reportCameraFrame,
  reportCameraVideoFrame,
} from "../capture/cameraRate";

interface UseMirrorRenderLoopParams {
  isWebcamRunning: boolean;
  terminalDimensions: { cols: number; rows: number };
  captureFrame: (drawVideo?: boolean) => {
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
  mediaVideoRef?: RefObject<HTMLVideoElement | null> | undefined;
  cameraVideoRef?: RefObject<HTMLVideoElement | null> | undefined;
  onMediaFps?: (fps: number) => void;
  mediaRevision?: unknown;
  fileActive?: boolean;
  contentFit?: "contain" | "cover";
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
  mediaVideoRef,
  cameraVideoRef,
  onMediaFps,
  mediaRevision,
  fileActive = false,
  contentFit = "contain",
}: UseMirrorRenderLoopParams) {
  const testPattern = useTestPattern();
  // A media element may be connected to only one Web Audio source. Retain its
  // graph across resize, render-mode changes and media/webcam toggles.
  const mediaAudioRef = useRef<{
    video: HTMLVideoElement;
    context: AudioContext;
    dispose: () => void;
  } | null>(null);
  const microphoneAudioRef = useRef<{
    stream: MediaStream;
    context: AudioContext;
    dispose: () => void;
  } | null>(null);
  const audioInputRef = useRef({ enabled: false, fileActive });
  audioInputRef.current = {
    enabled:
      isWebcamRunning &&
      !!animationEnabled &&
      (animation === "waveform" || animation === "fft"),
    fileActive,
  };
  useEffect(
    () => () => {
      mediaAudioRef.current?.dispose();
      void mediaAudioRef.current?.context.close();
      microphoneAudioRef.current?.dispose();
      void microphoneAudioRef.current?.context.close();
      microphoneAudioRef.current = null;
      mediaAudioRef.current = null;
    },
    [],
  );
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
        console.debug(`[${loopId}] Effect triggered by: ${changes.join(", ")}`);
      }
    }
    prevDepsRef.current = { isWebcamRunning, terminalDimensions, captureFrame };

    console.debug(
      `[${loopId}] Starting render loop with dimensions ${terminalDimensions.cols}x${terminalDimensions.rows}`,
    );

    let isActive = true;
    let currentRafHandle = 0;
    const testMode = testPattern.enabled && !fileActive;
    let lastFrameTime = performance.now();
    const mediaVideo = mediaVideoRef?.current;
    const audioAnimation =
      animationEnabled && (animation === "waveform" || animation === "fft");
    const usesVideoClock =
      !!mediaVideo?.requestVideoFrameCallback && !audioAnimation;
    const presented: number[] = [];
    const fpsTimer =
      mediaVideo && onMediaFps
        ? window.setInterval(() => {
            const cutoff = performance.now() - 1000;
            while (presented.length && presented[0]! < cutoff)
              presented.shift();
            onMediaFps(presented.length);
          }, 250)
        : undefined;
    let videoHandle = 0;
    let trackReader: ReadableStreamDefaultReader<VideoFrame> | null = null;
    let cameraTrack: MediaStreamTrack | undefined;
    let trackCapture = false;
    let pendingVideoFrame = false;
    const mediaClock = new VideoFrameClock();
    let lastConversionTime = 0;
    const audioSamples = new Float32Array(1024);
    const renderFrame = (directCameraFrame = false) => {
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
        if (mediaVideo) {
          if (mediaAudioRef.current?.video !== mediaVideo) {
            mediaAudioRef.current?.dispose();
            void mediaAudioRef.current?.context.close();
            const context = new AudioContext({ sampleRate: 48000 });
            const source = context.createMediaElementSource(mediaVideo);
            source.connect(context.destination);
            const dispose = captureVisualizationAudio(
              context,
              source,
              (samples) => {
                if (
                  isWasmReady() &&
                  audioInputRef.current.enabled &&
                  audioInputRef.current.fileActive &&
                  !mediaVideo.paused
                )
                  submitAudioVisualizationSamples(samples, "media");
              },
            );
            mediaAudioRef.current = { video: mediaVideo, context, dispose };
          }
          if (mediaAudioRef.current.context.state === "suspended")
            void mediaAudioRef.current.context.resume();
        } else if (!testMode && streamRef.current?.getAudioTracks().length) {
          if (microphoneAudioRef.current?.stream !== streamRef.current) {
            microphoneAudioRef.current?.dispose();
            void microphoneAudioRef.current?.context.close();
            const context = new AudioContext({ sampleRate: 48000 });
            const source = context.createMediaStreamSource(streamRef.current);
            const dispose = captureVisualizationAudio(
              context,
              source,
              (samples) => {
                if (
                  isWasmReady() &&
                  audioInputRef.current.enabled &&
                  !audioInputRef.current.fileActive
                )
                  submitAudioVisualizationSamples(samples, "microphone");
              },
            );
            microphoneAudioRef.current = {
              stream: streamRef.current,
              context,
              dispose,
            };
          }
          if (microphoneAudioRef.current.context.state === "suspended")
            void microphoneAudioRef.current.context.resume();
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
              { source: mediaVideo ? "media" : "microphone", mode: animation },
            )
          : renderAudioVisualization(
              terminalDimensions.cols,
              terminalDimensions.rows,
              mediaVideo ? "media" : "microphone",
              animation,
            );
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
        frame = captureFrame(!directCameraFrame);
      }

      if (
        !asciiArt &&
        !frame &&
        animationEnabled &&
        animation === "matrix" &&
        !fileActive
      ) {
        // Neutral pixels let the existing C rain effect run before the first
        // camera frame. Replace them with live capture as soon as it is ready.
        const data = new Uint8Array(32 * 32 * 4).fill(160);
        for (let i = 3; i < data.length; i += 4) data[i] = 255;
        frame = { data, width: 32, height: 32 };
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
          contentFit,
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

      const painted = rendererRef.current!.writeFrame(asciiArt);
      if (painted && debugCountRef.current === 0)
        console.debug(
          `[Mirror] First content frame at ${performance.now().toFixed(1)}ms`,
        );
      if (painted && mediaVideo) presented.push(performance.now());

      debugCountRef.current++;
    };

    let reportedFrameError = false;
    const animationFrameRef = (time: DOMHighResTimeStamp) => {
      try {
        if (trackCapture) return;
        const video = cameraVideoRef?.current;
        const cameraSource = video?.srcObject as MediaStream | null;
        if (video && cameraSource?.getVideoTracks) {
          const track = cameraSource.getVideoTracks()[0];
          if (track) {
            reportCameraTrack(track);
            reportCameraVideoFrame(video);
          }
        }
        if (usesVideoClock) {
          if (pendingVideoFrame) {
            pendingVideoFrame = false;
            renderFrame();
          }
          return;
        }
        if (mediaVideo?.paused) return;
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

    const cameraStream =
      !fileActive && !testMode && !audioAnimation ? streamRef.current : null;
    const captureCameraTrack = () => {
      const track = cameraStream?.getVideoTracks()[0];
      if (track === cameraTrack) return;
      cameraTrack = track;
      void trackReader?.cancel().catch(() => {});
      trackReader = null;
      trackCapture = false;
      const Processor = (
        window as Window & {
          MediaStreamTrackProcessor?: new (options: {
            track: MediaStreamTrack;
          }) => { readable: ReadableStream<VideoFrame> };
        }
      ).MediaStreamTrackProcessor;
      if (!track || !Processor || !canvasRef.current) return;
      reportCameraTrack(track);
      try {
        const reader = new Processor({ track }).readable.getReader();
        trackReader = reader;
        trackCapture = true;
        const clock = new VideoFrameClock();
        void (async () => {
          try {
            while (isActive && reader === trackReader) {
              const { value: frame, done } = await reader.read();
              if (done) break;
              try {
                if (!isActive || reader !== trackReader) break;
                reportCameraFrame(frame.timestamp / 1000);
                const canvas = canvasRef.current;
                const context = canvas?.getContext("2d");
                if (
                  !canvas ||
                  !context ||
                  !clock.accept(frame.timestamp / 1e6, frameIntervalRef.current)
                )
                  continue;
                // Capture the delivered camera frame, rather than sampling a
                // hidden video's compositor on an independent RAF clock.
                context.drawImage(frame, 0, 0, canvas.width, canvas.height);
                renderFrame(true);
                reportedFrameError = false;
              } catch (error) {
                if (!reportedFrameError)
                  console.warn(
                    "[Mirror] Camera frame failed; retrying next frame",
                    error,
                  );
                reportedFrameError = true;
              } finally {
                frame.close();
              }
            }
          } catch (error) {
            if (isActive && reader === trackReader)
              console.warn(
                "[Mirror] Direct camera capture failed; using video element",
                error,
              );
          } finally {
            if (reader === trackReader) trackCapture = false;
            reader.releaseLock();
          }
        })();
      } catch (error) {
        console.warn(
          "[Mirror] Direct camera capture unavailable; using video element",
          error,
        );
      }
    };
    captureCameraTrack();
    cameraStream?.addEventListener?.("addtrack", captureCameraTrack);
    cameraStream?.addEventListener?.("removetrack", captureCameraTrack);

    lastFrameTime = performance.now();
    const videoFrame = (_now: number, metadata: VideoFrameCallbackMetadata) => {
      if (!isActive || !mediaVideo) return;
      videoHandle = mediaVideo.requestVideoFrameCallback(videoFrame);
      // Pace against the media timeline, including fractional and variable FPS.
      // Never count a duplicate decoded frame or burst to catch up after a stall.
      if (mediaClock.accept(metadata.mediaTime, frameIntervalRef.current)) {
        pendingVideoFrame = true;
      }
    };
    if (mediaVideo && audioAnimation) renderFrame();
    if (usesVideoClock && mediaVideo.paused && mediaVideo.readyState >= 2)
      renderFrame();
    if (usesVideoClock)
      videoHandle = mediaVideo.requestVideoFrameCallback(videoFrame);
    const rafHandle = requestAnimationFrame(animationFrameRef);
    currentRafHandle = rafHandle;

    return () => {
      console.debug(`[${loopId}] Cleanup - stopping render loop`);
      isActive = false;
      cameraStream?.removeEventListener?.("addtrack", captureCameraTrack);
      cameraStream?.removeEventListener?.("removetrack", captureCameraTrack);
      void trackReader?.cancel().catch(() => {});
      window.clearInterval(fpsTimer);
      if (videoHandle) mediaVideo?.cancelVideoFrameCallback(videoHandle);
      cancelAnimationFrame(rafHandle);
      cancelAnimationFrame(currentRafHandle);
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
    mediaVideoRef,
    cameraVideoRef,
    onMediaFps,
    mediaRevision,
    fileActive,
    contentFit,
  ]);
}
