import {
  cameraConstraints,
  microphoneConstraints,
} from "@ascii-chat/shared/devices/mediaDevicePreferences";
import {
  type MutableRefObject,
  type RefObject,
  useCallback,
  useEffect,
  useRef,
  useState,
} from "react";
import { acquireMediaStream } from "../utils/acquireMediaStream";
import type { BinarySettingsConfig } from "../components";
import { type MediaSource, MediaSourceType } from "./useClientLike";
import { applyMirrorWasmSettings } from "./useWasmOptions";
import { useTestPattern } from "@ascii-chat/shared/hooks";
import {
  cameraAcquisitionFps,
  preferCameraMaximum,
} from "../../../packages/shared/src/capture/cameraRate";
import {
  getMediaDevicePreferences,
  resolveCameraDeviceId,
  MEDIA_DEVICE_PREFERENCES_CHANGED,
  type MediaDevicePreferencesChange,
} from "../utils/mediaDevicePreferences";

interface UseMirrorWebcamParams {
  settings: BinarySettingsConfig;
  videoRef: RefObject<HTMLVideoElement | null>;
  canvasRef: RefObject<HTMLCanvasElement | null>;
  streamRef: MutableRefObject<MediaStream | null>;
  lastFrameTimeRef: MutableRefObject<number>;
  setIsWebcamRunning: (running: boolean) => void;
  setMediaSource: (source: MediaSource) => void;
  setError: (error: string) => void;
  wasmInitialized: boolean;
  isWebcamRunning: boolean;
  mediaSource?: MediaSource;
  terminalDimensions?: { cols: number; rows: number };
}

function sizeCaptureCanvas(
  canvas: HTMLCanvasElement | null,
  track: MediaStreamTrack | undefined,
  video: HTMLVideoElement | null,
): void {
  const camera = track?.getSettings?.();
  const width = camera?.width || video?.videoWidth;
  const height = camera?.height || video?.videoHeight;
  if (!canvas || !width || !height) return;
  const scale = Math.min(1, 320 / width, 240 / height);
  canvas.width = Math.max(1, Math.round(width * scale));
  canvas.height = Math.max(1, Math.round(height * scale));
}

export function useMirrorWebcam({
  settings,
  videoRef,
  canvasRef,
  streamRef,
  lastFrameTimeRef,
  setIsWebcamRunning,
  setMediaSource,
  setError,
  wasmInitialized,
  isWebcamRunning,
  mediaSource,
  terminalDimensions,
}: UseMirrorWebcamParams) {
  const testPattern = useTestPattern();
  const devAutoStartRef = useRef(false);
  const startGenerationRef = useRef(0);
  const startupOwner = useRef<AbortController | null>(null);
  const startupPromise = useRef<Promise<boolean> | null>(null);
  const [isStarting, setIsStarting] = useState(false);
  const permissionGranted = true;
  const audioOnly =
    !!settings.animationEnabled &&
    (settings.animation === "waveform" || settings.animation === "fft");

  const startCapture = useCallback(async () => {
    const generation = ++startGenerationRef.current;
    const owner = new AbortController();
    startupOwner.current = owner;
    setError("");
    const clickTime = performance.now();
    console.log(
      `[Mirror] startWebcam CALLED at ${clickTime.toFixed(0)}ms, ${new Date().toISOString()}`,
    );
    console.time("[Mirror] Total startWebcam time");

    console.log(
      `[Mirror] Checking videoRef=${!!videoRef.current}, canvasRef=${!!canvasRef.current}`,
    );
    if (!videoRef.current || !canvasRef.current) {
      console.error("[Mirror] Video or canvas element not ready");
      setError("Video or canvas element not ready");
      return false;
    }

    let acquiredStream: MediaStream | null = null;
    try {
      const tryStartTime = performance.now();
      console.log(
        `[Mirror] startWebcam: entering try block at ${tryStartTime.toFixed(0)}ms`,
      );
      if (!applyMirrorWasmSettings(settings))
        throw new Error("Renderer settings are not initialized");

      // Check for test mode via query parameter
      console.log(`[Mirror] Checking test mode at ${performance.now()}`);
      const testMode = testPattern.enabled;
      console.log(`[Mirror] isTestMode=${testMode}`);

      if (testMode) {
        console.log(
          `[Mirror] Test mode enabled at ${performance.now()} - generating synthetic frames`,
        );
        lastFrameTimeRef.current = performance.now();
        setIsWebcamRunning(true);
        setMediaSource(MediaSourceType.WEBCAM);
        console.timeEnd("[Mirror] Total startWebcam time");
        return true;
      }

      console.log(
        `[Mirror] About to call getUserMedia at ${performance.now()}`,
      );
      console.time("[Mirror] getUserMedia (incl browser permission)");
      console.log("[Mirror] Calling getUserMedia...");
      const devicePreferences = getMediaDevicePreferences();
      const cameraId = audioOnly ? "" : await resolveCameraDeviceId();
      if (generation !== startGenerationRef.current) return false;
      const mediaConstraints: MediaStreamConstraints = {
        video: audioOnly
          ? false
          : cameraConstraints(cameraId, {
              width: { ideal: 1280 },
              height: { ideal: 720 },
              frameRate: { ideal: cameraAcquisitionFps() },
              facingMode: "user",
            }),
        // Keep the microphone track available so waveform/FFT can be enabled
        // after the camera has started, without requiring a reconnect.
        audio: microphoneConstraints(devicePreferences.microphoneId),
      };
      let stream: MediaStream | undefined;
      for (let attempt = 0; ; attempt++) {
        owner.signal.throwIfAborted();
        try {
          stream = await acquireMediaStream(mediaConstraints, owner.signal);
          acquiredStream = stream;
          break;
        } catch (cause) {
          const busy =
            cause instanceof DOMException &&
            (cause.name === "NotReadableError" ||
              cause.name === "TrackStartError");
          if (!busy || attempt >= 5 || owner.signal.aborted) throw cause;
          // Closing the previous tab/stream can release a Windows camera
          // asynchronously. Keep one cancellable startup while it settles.
          await new Promise<void>((resolve, reject) => {
            const aborted = () => {
              clearTimeout(timer);
              reject(owner.signal.reason);
            };
            const timer = window.setTimeout(
              () => {
                owner.signal.removeEventListener("abort", aborted);
                resolve();
              },
              Math.min(2000, 250 * 2 ** attempt),
            );
            owner.signal.addEventListener("abort", aborted, { once: true });
          });
        }
      }
      if (generation !== startGenerationRef.current) {
        stream.getTracks().forEach((track) => track.stop());
        return false;
      }
      console.timeEnd("[Mirror] getUserMedia (incl browser permission)");
      console.log(`[Mirror] getUserMedia returned at ${performance.now()}`);
      console.log(
        `[Mirror] Stream received after ${
          performance.now() - clickTime
        }ms from button click`,
      );

      streamRef.current?.getTracks().forEach((track) => track.stop());
      streamRef.current = stream;
      await preferCameraMaximum(stream.getVideoTracks?.()[0]);
      if (generation !== startGenerationRef.current) {
        stream.getTracks().forEach((track) => track.stop());
        return false;
      }
      if (audioOnly) {
        console.info(
          `[Mirror] Audio visualization started: ${stream.getVideoTracks().length} camera tracks, ${stream.getAudioTracks().length} microphone tracks`,
        );
        videoRef.current.srcObject = null;
        lastFrameTimeRef.current = performance.now();
        setIsWebcamRunning(true);
        setMediaSource(MediaSourceType.WEBCAM);
        return true;
      }
      const video = videoRef.current;
      console.time("[Mirror] loadedmetadata");
      const directCapture =
        typeof (window as Window & { MediaStreamTrackProcessor?: unknown })
          .MediaStreamTrackProcessor === "function";
      const metadataReady =
        directCapture || video.readyState >= HTMLMediaElement.HAVE_METADATA
          ? Promise.resolve()
          : new Promise<void>((resolve) => {
              video.addEventListener("loadedmetadata", () => resolve(), {
                once: true,
              });
            });
      video.muted = true;
      video.srcObject = stream;
      const playback = video.play();
      if (directCapture) {
        // Direct capture consumes camera frames independently of this hidden
        // video's compositor. Its play promise must not block C rendering.
        void playback.catch((cause: unknown) => {
          if (
            !owner.signal.aborted &&
            generation === startGenerationRef.current
          )
            console.warn(
              "[Mirror] Hidden video playback unavailable; using direct camera capture",
              cause,
            );
        });
      } else {
        await playback;
        await metadataReady;
      }
      if (generation !== startGenerationRef.current) {
        stream.getTracks().forEach((track) => track.stop());
        if (video.srcObject === stream) video.srcObject = null;
        return false;
      }
      console.timeEnd("[Mirror] loadedmetadata");

      // Direct track capture can start before video metadata; use the negotiated
      // camera geometry so the default HTML canvas size cannot stretch the crop.
      sizeCaptureCanvas(canvasRef.current, stream.getVideoTracks()[0], video);

      lastFrameTimeRef.current = performance.now();
      setIsWebcamRunning(true);
      setMediaSource(MediaSourceType.WEBCAM);
      console.timeEnd("[Mirror] Total startWebcam time");
      return true;
    } catch (err) {
      acquiredStream?.getTracks().forEach((track) => track.stop());
      if (acquiredStream && streamRef.current === acquiredStream) {
        streamRef.current = null;
        if (videoRef.current?.srcObject === acquiredStream)
          videoRef.current.srcObject = null;
      }
      if (generation === startGenerationRef.current)
        setError(`Failed to start webcam: ${String(err)}`);
      return false;
    }
  }, [
    audioOnly,
    testPattern.enabled,
    settings,
    videoRef,
    canvasRef,
    streamRef,
    lastFrameTimeRef,
    setIsWebcamRunning,
    setMediaSource,
    setError,
  ]);

  const startWebcam = useCallback(() => {
    if (startupPromise.current) return startupPromise.current;
    setIsStarting(true);
    const pending = startCapture().finally(() => {
      if (startupPromise.current === pending) {
        startupPromise.current = null;
        setIsStarting(false);
      }
    });
    startupPromise.current = pending;
    return pending;
  }, [startCapture]);

  const cancelStart = useCallback(() => {
    startGenerationRef.current++;
    startupOwner.current?.abort();
    startupOwner.current = null;
    startupPromise.current = null;
    setIsStarting(false);
  }, []);

  // Keep the microphone stream alive while releasing the camera hardware.
  // Reacquisition belongs to this effect so a late permission result cannot
  // resurrect the camera after Stop, a media switch, or another mode change.
  useEffect(() => {
    const stream = streamRef.current;
    if (
      !isWebcamRunning ||
      mediaSource !== MediaSourceType.WEBCAM ||
      !stream ||
      testPattern.enabled
    )
      return;
    if (audioOnly) {
      for (const track of stream.getVideoTracks()) {
        track.stop();
        stream.removeTrack(track);
      }
      console.info(
        `[Mirror] Camera released for audio visualization; ${stream.getAudioTracks().length} microphone tracks retained`,
      );
      if (videoRef.current) {
        videoRef.current.pause();
        videoRef.current.srcObject = null;
      }
      return;
    }
    if (stream.getVideoTracks().some((track) => track.readyState === "live"))
      return;
    let cancelled = false;
    const restore = async () => {
      let camera: MediaStream | undefined;
      try {
        const cameraId = await resolveCameraDeviceId();
        if (cancelled || streamRef.current !== stream) return;
        const video = {
          width: { ideal: 1280 },
          height: { ideal: 720 },
          frameRate: { ideal: cameraAcquisitionFps() },
          facingMode: "user",
        };
        camera = await navigator.mediaDevices.getUserMedia({
          video: {
            ...video,
            ...(cameraId ? { deviceId: { exact: cameraId } } : {}),
          },
          audio: false,
        });
        if (cancelled || streamRef.current !== stream) {
          camera.getTracks().forEach((track) => track.stop());
          return;
        }
        await preferCameraMaximum(camera.getVideoTracks()[0]);
        if (cancelled || streamRef.current !== stream) {
          camera.getTracks().forEach((track) => track.stop());
          return;
        }
        camera.getVideoTracks().forEach((track) => stream.addTrack(track));
        sizeCaptureCanvas(
          canvasRef.current,
          camera.getVideoTracks()[0],
          videoRef.current,
        );
        console.info(
          `[Mirror] Camera restored: ${stream.getVideoTracks().length} video tracks, ${stream.getAudioTracks().length} microphone tracks`,
        );
        if (videoRef.current) {
          videoRef.current.srcObject = stream;
          await videoRef.current.play();
        }
      } catch (error) {
        camera?.getTracks().forEach((track) => {
          track.stop();
          stream.removeTrack(track);
        });
        if (!cancelled) setError(`Failed to restore webcam: ${String(error)}`);
      }
    };
    void restore();
    return () => {
      cancelled = true;
    };
  }, [
    audioOnly,
    isWebcamRunning,
    mediaSource,
    settings.targetFps,
    streamRef,
    videoRef,
    testPattern.enabled,
    canvasRef,
    setError,
  ]);

  useEffect(() => {
    const handleDevicePreferencesChanged = (event: Event) => {
      const change = (event as CustomEvent<MediaDevicePreferencesChange>)
        .detail;
      if (
        !change ||
        (!change.changedKeys.includes("cameraId") &&
          !change.changedKeys.includes("microphoneId"))
      )
        return;

      const stream = streamRef.current;
      if (!stream) return;
      const activeCameraId =
        stream.getVideoTracks()[0]?.getSettings().deviceId ?? "";
      const activeMicrophoneId =
        stream.getAudioTracks()[0]?.getSettings().deviceId ?? "";
      if (
        activeCameraId === change.preferences.cameraId &&
        activeMicrophoneId === change.preferences.microphoneId
      )
        return;

      cancelStart();
      stream.getTracks().forEach((track) => track.stop());
      streamRef.current = null;
      if (videoRef.current?.srcObject === stream) {
        videoRef.current.pause();
        videoRef.current.srcObject = null;
      }
      setIsWebcamRunning(false);
      setMediaSource(null);
      window.setTimeout(() => void startWebcam(), 0);
    };

    window.addEventListener(
      MEDIA_DEVICE_PREFERENCES_CHANGED,
      handleDevicePreferencesChanged,
    );
    return () =>
      window.removeEventListener(
        MEDIA_DEVICE_PREFERENCES_CHANGED,
        handleDevicePreferencesChanged,
      );
  }, [
    setIsWebcamRunning,
    setMediaSource,
    startWebcam,
    streamRef,
    videoRef,
    cancelStart,
  ]);

  // DISABLED: Permission request on page load was blocking React for 30+ seconds
  // The getUserMedia call would hang waiting for browser permission dialog.
  // Instead, we'll request permission when user clicks "Start Webcam" button.
  // useEffect(() => {
  //   const requestPermission = async () => {
  //     try {
  //       console.log("[Mirror] Requesting camera permission on page load");
  //       // Request minimal 1x1 stream just to trigger permission dialog
  //       const stream = await navigator.mediaDevices.getUserMedia({
  //         video: { width: { ideal: 1 }, height: { ideal: 1 } },
  //         audio: false,
  //       });
  //       console.log("[Mirror] Permission granted, stopping stream");
  //       stream.getTracks().forEach((track) => track.stop());
  //       // Add a small delay to ensure Firefox properly releases the camera
  //       // before the auto-start tries to access it
  //       await new Promise((resolve) => setTimeout(resolve, 100));
  //       setPermissionGranted(true);
  //     } catch (err) {
  //       const message = err instanceof Error ? err.message : String(err);
  //       console.log(
  //         "[Mirror] Permission request failed (user may have denied):",
  //         message,
  //       );
  //       // Even if permission request fails, proceed with auto-start
  //       // (user may deny and then manually grant, or have already granted)
  //       setPermissionGranted(true);
  //     }
  //   };
  //   void requestPermission();
  // }, []);

  // Set permissionGranted immediately so auto-start can proceed
  // The startWebcam function will request permission when needed

  // Fast Refresh can preserve the running state after it has discarded the
  // stream ref. Do not leave the renderer in a false-running state with no
  // camera or microphone samples to display.
  useEffect(() => {
    const testMode = testPattern.enabled;
    const video = videoRef.current;
    const hasMediaSource =
      streamRef.current !== null ||
      video?.srcObject !== null ||
      (video?.src !== undefined && video.src !== "");
    if (
      isWebcamRunning &&
      mediaSource !== MediaSourceType.FILE &&
      !testMode &&
      !hasMediaSource
    ) {
      setIsWebcamRunning(false);
      setMediaSource(null);
      setError(
        "Camera and microphone are no longer active. Start Webcam to reconnect.",
      );
    }
  }, [
    testPattern.enabled,
    mediaSource,
    isWebcamRunning,
    videoRef,
    streamRef,
    setIsWebcamRunning,
    setMediaSource,
    setError,
  ]);

  // Test pages use a synthetic input source. Keep that automation scoped to ?test;
  // a normal mirror page must only request camera or microphone access after a click.
  useEffect(() => {
    const effectTime = performance.now();
    const hasDimensions =
      terminalDimensions &&
      terminalDimensions.cols > 0 &&
      terminalDimensions.rows > 0;

    const nodeEnv = import.meta.env["NODE_ENV"];
    const testMode = testPattern.enabled;
    console.log(
      `[Mirror] TEST AUTO-START EFFECT at ${effectTime.toFixed(0)}ms: NODE_ENV=${nodeEnv}, testMode=${testMode}, wasmInit=${wasmInitialized}, permGranted=${permissionGranted}, webcamRunning=${isWebcamRunning}, alreadyStarted=${devAutoStartRef.current}, hasDims=${hasDimensions}`,
    );

    const shouldStart =
      nodeEnv !== "production" &&
      testMode &&
      wasmInitialized &&
      permissionGranted &&
      !isWebcamRunning &&
      !devAutoStartRef.current &&
      hasDimensions;

    console.log(
      `[Mirror] TEST AUTO-START check: shouldStart=${shouldStart} (nodeEnv OK=${nodeEnv !== "production"}, testMode=${testMode}, wasmInit=${wasmInitialized}, permGranted=${permissionGranted}, !webcamRunning=${!isWebcamRunning}, !alreadyStarted=${!devAutoStartRef.current}, hasDims=${hasDimensions})`,
    );

    if (shouldStart) {
      devAutoStartRef.current = true;
      console.log(
        `[Mirror] starting synthetic test input at ${performance.now().toFixed(0)}ms with dimensions ${terminalDimensions.cols}x${terminalDimensions.rows}`,
      );
      console.time("[Mirror] startWebcam()");
      void Promise.resolve()
        .then(() => {
          console.log(
            `[Mirror] Calling startWebcam at ${performance.now().toFixed(0)}ms`,
          );
          return startWebcam();
        })
        .then(() => {
          console.log(
            `[Mirror] startWebcam completed at ${performance.now().toFixed(0)}ms`,
          );
          console.timeEnd("[Mirror] startWebcam()");
        })
        .catch((err) => {
          console.error(`[Mirror] startWebcam failed: ${err}`);
        });
    }
  }, [
    testPattern.enabled,
    wasmInitialized,
    permissionGranted,
    isWebcamRunning,
    startWebcam,
    terminalDimensions,
  ]);

  useEffect(() => cancelStart, [cancelStart]);

  return { startWebcam, cancelStart, isStarting };
}
