import { drawFaceFollow } from "../../../packages/shared/src/capture/faceFollow";
import { AVState } from "../wasm/AVState";
import { VideoFrameClock } from "../../../packages/shared/src/hooks/videoFrameClock";
import {
  cameraNeedsFrameLimit,
  reportCameraTrack,
  reportCameraFrame,
  reportCameraVideoFrame,
  cameraAcquisitionFps,
} from "../../../packages/shared/src/capture/cameraRate";
import { useLayoutEffect } from "react";
import { useCallback, useEffect, useRef, useState } from "react";
import { ConnectionState, PacketType } from "../wasm/client";
import {
  VideoUploadEncoder,
  buildStreamStartPacket,
  buildCapabilitiesPacket,
  VIDEO_CODEC_CAP_RGBA,
  VIDEO_CODEC_CAP_H265,
  VIDEO_CODEC_CAP_H264,
  buildImageFramePayload,
  buildImageFrameH265Payload,
} from "../network";
import { useVideoEncodings } from "./useVideoEncodings";
import type { CompressedVideoEncoding } from "../network/VideoUploadEncoder";
import type { ClientSession } from "../network/Transport";
import { orientUpload } from "../utils/orientUpload";
import type { AsciiFrame } from "../network/AsciiFrameParser";
import type { BinarySettingsConfig } from "../components";
import { resolveCameraDeviceId } from "../utils/mediaDevicePreferences";
import {
  MEDIA_DEVICE_PREFERENCES_CHANGED,
  type MediaDevicePreferencesChange,
} from "../utils/mediaDevicePreferences";
import {
  createTestPatternVideoSource,
  useTestPattern,
  type TestPatternVideoSource,
} from "@ascii-chat/shared";

// Helper to compute simple frame hash
const computeFrameHash = (data: Uint8Array): number => {
  let hash = 0;
  // Sample every 256th byte for speed
  for (let i = 0; i < data.length; i += 256) {
    hash = (hash << 5) - hash + (data[i] ?? 0);
    hash = hash & hash;
  }
  return Math.abs(hash);
};

interface UseWebcamStreamOptions {
  terminalDimensions: { cols: number; rows: number };
  clientRef: React.RefObject<ClientSession | null>;
  connectionState: ConnectionState;
  settings: BinarySettingsConfig;
  receiveFps?: number;
  captureFrame: (
    drawVideo?: boolean,
    sourceCanvas?: HTMLCanvasElement,
    readPixels?: boolean,
  ) => {
    data: Uint8Array;
    width: number;
    height: number;
  } | null;
  canvasRef: React.RefObject<HTMLCanvasElement | null>;
  videoRef: React.RefObject<HTMLVideoElement | null>;
  frameIntervalRef: React.MutableRefObject<number>;
  lastFrameTimeRef: React.MutableRefObject<number>;
  frameQueueRef: React.MutableRefObject<AsciiFrame[]>;
  setError: (error: string) => void;
  includeAudio?: boolean;
}

export function useWebcamStream(options: UseWebcamStreamOptions) {
  const testPattern = useTestPattern();
  const {
    clientRef,
    terminalDimensions,
    connectionState,
    settings,
    receiveFps = settings.targetFps,
    captureFrame,
    canvasRef,
    videoRef,
    frameIntervalRef,
    lastFrameTimeRef,
    frameQueueRef,
    setError,
    includeAudio = false,
  } = options;

  const {
    preference: encodingPreference,
    createEncoder,
    validate: validateEncoding,
  } = useVideoEncodings({ fps: settings.targetFps });

  const [avState] = useState(() => new AVState());
  const startingRef = useRef(false);
  const mountedRef = useRef(true);
  const cancelMetadataRef = useRef<(() => void) | null>(null);
  const captureReadyRef = useRef(false);
  const streamRef = useRef<MediaStream | null>(null);
  const displayStreamRef = useRef<MediaStream | null>(null);
  const pendingDisplayRef = useRef<MediaStream | undefined>(undefined);
  const videoEncoderRef = useRef<VideoUploadEncoder | null>(null);
  const webcamCaptureLoopRef = useRef<
    | ((
        drawVideo?: boolean,
        sourceCanvas?: HTMLCanvasElement,
        fileFrame?: { data: Uint8Array; width: number; height: number },
      ) => void)
    | null
  >(null);
  const captureTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const testPatternSourceRef = useRef<TestPatternVideoSource | null>(null);

  const captureLoopFrameCountRef = useRef(0);
  const lastFrameHashRef = useRef(0);
  const uniqueFrameCountRef = useRef(0);
  const videFrameUpdateCountRef = useRef(0); // Track actual VIDEO updates

  const [isWebcamRunning, setIsWebcamRunning] = useState(false);
  const [captureRevision, setCaptureRevision] = useState(0);

  const stopWebcam = useCallback(() => {
    const wasRunning = avState.current(avState.generation);
    avState.stop(() => {
      cancelMetadataRef.current?.();
      captureReadyRef.current = false;
      // A stopped browser capture must also stop being a server-side video
      // source. Otherwise the server keeps compositing its last (often black)
      // frame over the remaining participants until the connection closes.
      if (
        wasRunning &&
        clientRef.current?.getState() === ConnectionState.CONNECTED
      ) {
        try {
          clientRef.current.sendUnencryptedAcipPacket(
            PacketType.STREAM_STOP,
            buildStreamStartPacket(false),
          );
        } catch (error) {
          console.debug(
            "[Client] STREAM_STOP skipped while closing capture:",
            error,
          );
        }
      }
      // Stop timer (connection state change will also stop it)
      if (captureTimerRef.current) {
        clearInterval(captureTimerRef.current);
        captureTimerRef.current = null;
      }

      if (streamRef.current) {
        streamRef.current.getTracks().forEach((track) => track.stop());
        streamRef.current = null;
      }
      displayStreamRef.current = null;
      testPatternSourceRef.current?.stop();
      testPatternSourceRef.current = null;

      if (videoRef.current) {
        videoRef.current.srcObject = null;
      }

      // Clean up the video encoder
      if (videoEncoderRef.current) {
        videoEncoderRef.current.destroy();
        videoEncoderRef.current = null;
      }

      frameQueueRef.current = [];
      setIsWebcamRunning(false);
    });
  }, [clientRef, frameQueueRef, videoRef, avState]);

  const configureUpload = useCallback(
    async (
      width: number,
      height: number,
      generation: number,
      after?: CompressedVideoEncoding,
    ) => {
      const conn = clientRef.current;
      if (!conn || conn.getState() !== ConnectionState.CONNECTED) return;
      const encoder = conn.sendVideoFrame
        ? null
        : await createEncoder(
            width,
            height,
            settings.targetFps,
            (chunk, codec) => {
              if (
                !avState.current(generation) ||
                !captureReadyRef.current ||
                clientRef.current !== conn ||
                conn.getState() !== ConnectionState.CONNECTED
              )
                return;
              conn.sendPacket(
                codec === "hevc"
                  ? PacketType.IMAGE_FRAME_H265
                  : PacketType.IMAGE_FRAME_H264,
                buildImageFrameH265Payload(
                  chunk.flags,
                  chunk.width,
                  chunk.height,
                  chunk.data,
                ),
              );
            },
            after,
          );
      if (
        !avState.current(generation) ||
        clientRef.current !== conn ||
        conn.getState() !== ConnectionState.CONNECTED
      ) {
        encoder?.destroy();
        return;
      }
      videoEncoderRef.current = encoder;
      conn.videoCodecCapabilities =
        VIDEO_CODEC_CAP_RGBA |
        (encoder?.codec === "hevc"
          ? VIDEO_CODEC_CAP_H265
          : encoder?.codec === "h264"
            ? VIDEO_CODEC_CAP_H264
            : 0);
      conn.sendPacket(
        PacketType.CLIENT_CAPABILITIES,
        buildCapabilitiesPacket(
          terminalDimensions.cols || 80,
          terminalDimensions.rows || 40,
          receiveFps,
          settings.colorMode,
          settings.colorFilter,
          settings.palette,
          settings.paletteChars,
          settings.matrixRain,
          conn.videoCodecCapabilities,
          settings.renderMode,
        ),
      );
      console.info(
        `[Client] Upload encoding: ${encoder?.codec ?? "raw"} ${width}x${height}`,
      );
    },
    [
      clientRef,
      settings,
      receiveFps,
      terminalDimensions,
      createEncoder,
      avState,
    ],
  );

  // Inner loop function that doesn't have dependencies - this prevents RAF recursion from breaking
  const createWebcamCaptureLoop = useCallback(() => {
    // Timer-based frame sending to match server render loop (not RAF-based)
    // RAF fires at monitor refresh rate (60+ Hz) regardless of frame send interval
    // Timer ensures we send at exactly the target FPS, matching C client behavior
    const sendOneFrame = (
      drawVideo = true,
      sourceCanvas?: HTMLCanvasElement,
      fileFrame?: { data: Uint8Array; width: number; height: number },
    ) => {
      const now = performance.now();

      // Call captureAndSendFrame through ref to get the latest version
      const conn = clientRef.current;
      if (
        (captureReadyRef.current || fileFrame) &&
        conn &&
        conn.getState() === ConnectionState.CONNECTED
      ) {
        videoEncoderRef.current?.observeTransport(
          conn.getUploadFeedback?.() ?? {
            bufferedBytes: conn.getBufferedAmount?.() ?? 0,
          },
        );
        if ((conn.getBufferedAmount?.() ?? 0) > 256 * 1024) return;
        let frame =
          fileFrame ??
          captureFrame(
            drawVideo,
            sourceCanvas,
            !videoEncoderRef.current && !conn.sendVideoFrame,
          );
        if (frame && frame.data) {
          const uploadCanvas = sourceCanvas ?? canvasRef.current;
          if (uploadCanvas) frame = orientUpload(uploadCanvas, frame);
          captureLoopFrameCountRef.current++;
          const frameHash = computeFrameHash(frame.data);

          // Log EVERY frame sent, not just unique ones
          const isNewFrame = frameHash !== lastFrameHashRef.current;
          if (isNewFrame) {
            uniqueFrameCountRef.current++;
            videFrameUpdateCountRef.current++;
            lastFrameHashRef.current = frameHash;
            // console.log(
            //   `[Client] SEND #${captureLoopFrameCountRef.current} (UNIQUE #${uniqueFrameCountRef.current}): hash=0x${frameHash.toString(
            //     16,
            //   )}, size=${frame.data.length}`,
            // );
          } else {
            // console.log(
            //   `[Client] SEND #${captureLoopFrameCountRef.current} (DUPLICATE): hash=0x${frameHash.toString(
            //     16,
            //   )}, size=${frame.data.length}`,
            // );
          }

          if (conn.sendVideoFrame) {
            const source = sourceCanvas ?? canvasRef.current;
            if (source)
              conn.sendVideoFrame(
                new VideoFrame(source, { timestamp: Math.round(now * 1000) }),
              );
            return;
          }
          if (videoEncoderRef.current) {
            const encoderCanvas = sourceCanvas ?? canvasRef.current;
            if (!encoderCanvas) return;
            let videoFrame: VideoFrame | undefined;
            try {
              videoFrame = new VideoFrame(encoderCanvas, {
                timestamp: Math.round(now * 1000),
              });
              videoEncoderRef.current.encode(
                videoFrame,
                settings.targetFps,
                clientRef.current?.needsKeyframe?.(
                  videoEncoderRef.current.codec === "hevc"
                    ? PacketType.IMAGE_FRAME_H265
                    : PacketType.IMAGE_FRAME_H264,
                ) ?? false,
              );
            } catch (error) {
              const failedCodec = videoEncoderRef.current.codec;
              const failedLabel = videoEncoderRef.current.label;
              videoEncoderRef.current.destroy();
              videoEncoderRef.current = null;
              if (encodingPreference !== "auto") {
                stopWebcam();
                setError(
                  `${failedLabel} encoding failed: ${String(error)} Use ?encoding=raw.`,
                );
              } else {
                captureReadyRef.current = false;
                const generation = avState.generation;
                void configureUpload(
                  frame.width,
                  frame.height,
                  generation,
                  failedCodec,
                )
                  .then(() => {
                    if (avState.current(generation))
                      captureReadyRef.current = true;
                  })
                  .catch((fallbackError: unknown) => {
                    if (avState.current(generation)) {
                      setError(
                        `Failed to change upload encoding: ${String(fallbackError)}`,
                      );
                    }
                  });
              }
            } finally {
              videoFrame?.close();
            }
            return;
          }

          {
            const payload = buildImageFramePayload(
              frame.data,
              frame.width,
              frame.height,
            );

            try {
              conn.sendPacket(PacketType.IMAGE_FRAME, payload);
            } catch (err) {
              console.error("[Client] Failed to send IMAGE_FRAME:", err);
            }
          }
        }
      }
    };

    return sendOneFrame;
  }, [
    stopWebcam,
    avState,
    captureFrame,
    configureUpload,
    encodingPreference,
    clientRef,
    canvasRef,
    setError,
    settings.targetFps,
  ]);

  // Create capture function ref
  useEffect(() => {
    webcamCaptureLoopRef.current = createWebcamCaptureLoop();
  }, [createWebcamCaptureLoop]);

  // Capture on the camera track's frame delivery callback. Polling a hidden
  // video element's currentTime is subject to compositor throttling, which can
  // reduce a healthy webcam to only a few frames per second.
  useEffect(() => {
    if (
      connectionState === ConnectionState.CONNECTED &&
      isWebcamRunning &&
      webcamCaptureLoopRef.current
    ) {
      if (testPatternSourceRef.current) {
        console.log(
          "[Client] Sending test-pattern frames directly from the source canvas",
        );
        return;
      }
      const videoTrack = streamRef.current?.getVideoTracks()[0];
      if (videoTrack && !displayStreamRef.current) {
        reportCameraTrack(videoTrack);
        void videoTrack
          .applyConstraints({
            width: { ideal: 1280 },
            height: { ideal: 720 },
            frameRate: { ideal: cameraAcquisitionFps(videoTrack) },
          })
          .catch((error) =>
            console.warn("[Client] Unable to update webcam settings:", error),
          );
      }
      const sendInterval = 1000 / settings.targetFps;
      const mediaClock = new VideoFrameClock();
      const video = videoRef.current;
      let stopped = false;
      let animationFrameId: number | null = null;
      let captureCallbackHandle: number | null = null;
      let frameReader: ReadableStreamDefaultReader<VideoFrame> | null = null;
      const frameVideo = video as
        | (HTMLVideoElement & {
            requestVideoFrameCallback?: (
              callback: (now: number, metadata: { mediaTime: number }) => void,
            ) => number;
            cancelVideoFrameCallback?: (handle: number) => void;
          })
        | null;

      // Read camera frames from the track directly when the browser supports
      // MediaStreamTrackProcessor. A hidden, offscreen video element can be
      // compositor-throttled even while the camera itself is producing frames.
      const TrackProcessor = (
        window as Window & {
          MediaStreamTrackProcessor?: new (options: {
            track: MediaStreamTrack;
          }) => { readable: ReadableStream<VideoFrame> };
        }
      ).MediaStreamTrackProcessor;
      if (TrackProcessor && videoTrack) {
        try {
          frameReader = new TrackProcessor({
            track: videoTrack,
          }).readable.getReader();
          const readFrames = async () => {
            while (!stopped && frameReader) {
              const { value: frame, done } = await frameReader.read();
              if (done) break;
              try {
                reportCameraFrame(frame.timestamp / 1000);
                const canvas = canvasRef.current;
                const context = canvas?.getContext("2d");
                // Pace against camera timestamps; callback delivery jitter must
                // not discard every other frame of a healthy 60 FPS track.
                if (
                  canvas &&
                  context &&
                  (!cameraNeedsFrameLimit(settings.targetFps) ||
                    mediaClock.accept(frame.timestamp / 1e6, sendInterval))
                ) {
                  drawFaceFollow(context, frame, canvas);
                  webcamCaptureLoopRef.current?.(false);
                }
              } finally {
                frame.close();
              }
            }
          };
          void readFrames().catch((error: unknown) => {
            if (!stopped) {
              console.warn(
                "[Client] Camera track reader stopped; video frame capture may be throttled:",
                error,
              );
            }
          });
        } catch (error) {
          console.warn(
            "[Client] Direct camera track capture unavailable; using video callback:",
            error,
          );
          frameReader = null;
        }
      }

      if (frameReader) {
        console.log(
          `[Client] Capturing webcam frames directly from the media track, capped at ${settings.targetFps} FPS`,
        );
      } else if (frameVideo?.requestVideoFrameCallback) {
        const onVideoFrame = (
          _now: number,
          metadata: { mediaTime: number },
        ) => {
          if (stopped) return;
          reportCameraFrame(metadata.mediaTime * 1000);
          if (
            !cameraNeedsFrameLimit(settings.targetFps) ||
            mediaClock.accept(metadata.mediaTime, sendInterval)
          ) {
            webcamCaptureLoopRef.current?.();
          }
          captureCallbackHandle =
            frameVideo.requestVideoFrameCallback!(onVideoFrame);
        };
        captureCallbackHandle =
          frameVideo.requestVideoFrameCallback(onVideoFrame);
        console.log(
          `[Client] Capturing webcam frames from the media track, capped at ${settings.targetFps} FPS`,
        );
      } else {
        const pollDecodedFrame = () => {
          if (stopped) return;
          if (video) {
            reportCameraVideoFrame(video);
            if (
              video.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA &&
              !video.paused &&
              mediaClock.accept(video.currentTime, sendInterval)
            ) {
              webcamCaptureLoopRef.current?.();
            }
          }
          animationFrameId = requestAnimationFrame(pollDecodedFrame);
        };
        animationFrameId = requestAnimationFrame(pollDecodedFrame);
      }

      return () => {
        stopped = true;
        if (captureCallbackHandle !== null) {
          frameVideo?.cancelVideoFrameCallback?.(captureCallbackHandle);
        }
        if (frameReader) {
          void frameReader.cancel().catch(() => undefined);
          frameReader = null;
        }
        if (animationFrameId !== null) cancelAnimationFrame(animationFrameId);
        if (captureTimerRef.current) {
          clearInterval(captureTimerRef.current);
          captureTimerRef.current = null;
        }
      };
    } else {
      // Stop timer when disconnected
      if (captureTimerRef.current) {
        clearInterval(captureTimerRef.current);
        captureTimerRef.current = null;
        console.log("[Client] Stopped frame send timer");
      }
    }

    return () => {
      if (captureTimerRef.current) {
        clearInterval(captureTimerRef.current);
        captureTimerRef.current = null;
      }
    };
  }, [
    canvasRef,
    connectionState,
    isWebcamRunning,
    captureRevision,
    settings,
    videoRef,
  ]);

  const startWebcam = useCallback(async () => {
    const providedStream = pendingDisplayRef.current;
    pendingDisplayRef.current = undefined;
    if (startingRef.current || streamRef.current) return false;
    captureReadyRef.current = false;
    console.log("[Client] startWebcam() called");
    console.log(
      `[DEBUG] videoRef.current=${!!videoRef.current}, canvasRef.current=${!!canvasRef.current}`,
    );

    if (!videoRef.current || !canvasRef.current) {
      console.error("[Client] Video or canvas element not ready");
      setError("Video or canvas element not ready");
      return false;
    }

    console.log(
      `[DEBUG] connectionState=${connectionState} vs CONNECTED=${ConnectionState.CONNECTED}`,
    );
    if (connectionState !== ConnectionState.CONNECTED) {
      console.error(
        `[Client] Not connected (state=${connectionState}), cannot start webcam`,
      );
      setError("Must be connected to server before starting webcam");
      return false;
    }

    console.log("[Client] Passed all initial checks");

    const generation = avState.begin();
    if (!generation) return false;
    startingRef.current = true;
    let captureStarted = false;
    try {
      validateEncoding();
      setError("");
      // Send STREAM_START to notify server we're about to send video
      if (clientRef.current) {
        console.log("[Client] Sending STREAM_START before webcam...");
        const streamPayload = buildStreamStartPacket(
          includeAudio || !!providedStream?.getAudioTracks().length,
        );
        // Send as unencrypted ACIP packet (like native client does)
        clientRef.current.sendUnencryptedAcipPacket(
          PacketType.STREAM_START,
          streamPayload,
        );
        console.log("[Client] STREAM_START sent");
      } else {
        console.log(
          "[Client] clientRef.current is null, skipping STREAM_START",
        );
      }

      const w = 1280;
      const h = 720;
      console.log(`[Client] Requesting webcam stream: ${w}x${h}`);
      console.log(
        `[DEBUG] videoRef.current before getUserMedia:`,
        videoRef.current,
      );

      const preferredCameraId =
        providedStream || testPattern.enabled
          ? ""
          : await resolveCameraDeviceId();
      const videoConstraints: MediaTrackConstraints = {
        width: { ideal: w },
        height: { ideal: h },
        frameRate: { ideal: cameraAcquisitionFps() },
        ...(preferredCameraId
          ? { deviceId: { exact: preferredCameraId } }
          : {}),
      };

      if (!avState.current(generation)) return false;
      let stream: MediaStream;
      if (providedStream) {
        stream = providedStream;
        displayStreamRef.current = providedStream;
      } else if (testPattern.enabled) {
        const frameInterval = 1000 / settings.targetFps;
        let nextPatternFrameAt = 0;
        const source = createTestPatternVideoSource(
          settings.targetFps,
          canvasRef.current.width || w,
          canvasRef.current.height || h,
          testPattern.mode === "none" ? "test" : testPattern.mode,
          canvasRef.current,
          (sourceCanvas) => {
            const now = performance.now();
            if (nextPatternFrameAt === 0) nextPatternFrameAt = now;
            const elapsed = now - nextPatternFrameAt;
            const tolerance = Math.min(1, frameInterval / 20);
            if (elapsed + tolerance < frameInterval) return;
            const intervalsElapsed = Math.max(
              1,
              Math.floor((elapsed + tolerance) / frameInterval),
            );
            nextPatternFrameAt += intervalsElapsed * frameInterval;
            webcamCaptureLoopRef.current?.(false, sourceCanvas);
          },
        );
        testPatternSourceRef.current = source;
        stream = source.stream;
      } else {
        stream = await navigator.mediaDevices.getUserMedia({
          video: videoConstraints,
          audio: false,
        });
      }

      if (!avState.current(generation)) {
        stream.getTracks().forEach((track) => track.stop());
        return false;
      }
      // Publish ownership before any further awaits so stop can release the device.
      streamRef.current = stream;

      // Some Windows UVC cameras only expose 60 FPS at 720p or widescreen
      // VGA, while the default 640x480 mode is limited to 30 FPS. ASCII input
      // is downscaled below, so negotiate a 720p source only when the current
      // mode is below target and this camera advertises a high-FPS mode.
      const videoTrack = stream.getVideoTracks()[0];
      if (!videoTrack || videoTrack.readyState !== "live") {
        throw new Error("Selected camera did not provide a live video track");
      }
      if (
        !providedStream &&
        videoTrack?.getCapabilities &&
        videoTrack.getSettings
      ) {
        const capabilities = videoTrack.getCapabilities();
        const currentSettings = videoTrack.getSettings();
        const currentFps = currentSettings.frameRate;
        const maximumFps = capabilities.frameRate?.max;
        const captureFps = cameraAcquisitionFps(videoTrack);
        if (
          maximumFps !== undefined &&
          maximumFps >= Math.min(captureFps, 45) &&
          (currentFps === undefined ||
            currentFps + 1 < Math.min(captureFps, maximumFps))
        ) {
          try {
            await videoTrack.applyConstraints({
              width: { ideal: Math.max(w, 1280) },
              height: { ideal: Math.max(h, 720) },
              frameRate: { ideal: Math.min(captureFps, maximumFps) },
            });
            let negotiatedSettings = videoTrack.getSettings();
            if (
              currentFps !== undefined &&
              negotiatedSettings.frameRate !== undefined &&
              negotiatedSettings.frameRate <= currentFps + 1
            ) {
              // Keep the smaller mode if the higher-resolution request does
              // not improve frame rate; it costs less to capture and scale.
              await videoTrack.applyConstraints(videoConstraints);
              negotiatedSettings = videoTrack.getSettings();
            }
            console.log(
              `[Client] Camera mode: ${videoTrack.label}, ${negotiatedSettings.width ?? "?"}x${negotiatedSettings.height ?? "?"} @ ${negotiatedSettings.frameRate ?? "?"} FPS (device max ${maximumFps})`,
            );
          } catch (error) {
            console.warn(
              "[Client] Unable to negotiate the camera's high-FPS mode:",
              error,
            );
          }
        } else {
          console.log(
            `[Client] Camera mode: ${videoTrack.label}, ${currentSettings.width ?? "?"}x${currentSettings.height ?? "?"} @ ${currentFps ?? "?"} FPS (device max ${maximumFps ?? "?"})`,
          );
        }
      }

      console.log("[Client] Webcam stream acquired");
      console.log(`[DEBUG] Stream object:`, stream);
      console.log(
        `[Client] Stream tracks: ${stream.getTracks().length}, active=${stream.active}`,
      );

      // Log track details
      stream.getTracks().forEach((track, idx) => {
        console.log(`[DEBUG] Track ${idx}:`, {
          kind: track.kind,
          enabled: track.enabled,
          readyState: track.readyState,
          label: track.label,
          settings: track.getSettings ? track.getSettings() : "N/A",
        });
      });

      if (
        !avState.current(generation) ||
        clientRef.current?.getState() !== ConnectionState.CONNECTED
      ) {
        stream.getTracks().forEach((track) => track.stop());
        return false;
      }
      streamRef.current = stream;
      const video = videoRef.current!;
      console.log("[DEBUG] Before setting srcObject, video element:", {
        videoWidth: video.videoWidth,
        videoHeight: video.videoHeight,
        readyState: video.readyState,
        networkState: video.networkState,
      });

      video.srcObject = stream;
      console.log("[DEBUG] After setting srcObject");
      console.log("[DEBUG] Video element after srcObject:", {
        videoWidth: video.videoWidth,
        videoHeight: video.videoHeight,
        srcObject: !!video.srcObject,
      });

      // Monitor stream for unexpected end
      stream.getTracks().forEach((track) => {
        track.onended = () => {
          console.warn(
            `[Client] Media track ended (${track.kind}): readyState=${track.readyState}`,
          );
        };
        track.onmute = () => {
          console.warn(`[Client] Media track muted (${track.kind})`);
        };
        track.onunmute = () => {
          console.log(`[Client] Media track unmuted (${track.kind})`);
        };
      });

      // Resolve or cancel readiness without leaving listeners/timers after stop.
      const metadataPromise = new Promise<void>((resolve) => {
        const finish = () => {
          video.removeEventListener("loadedmetadata", finish);
          window.clearTimeout(timeout);
          cancelMetadataRef.current = null;
          resolve();
        };
        const timeout = window.setTimeout(finish, 5000);
        cancelMetadataRef.current = finish;
        video.addEventListener("loadedmetadata", finish);
        if (video.videoWidth && video.videoHeight) finish();
      });
      try {
        await video.play();
      } catch (error) {
        if (
          typeof (window as Window & { MediaStreamTrackProcessor?: unknown })
            .MediaStreamTrackProcessor !== "function" ||
          !videoTrack
        )
          throw error;
        // Direct track capture does not require playback of the preview element.
        cancelMetadataRef.current?.();
        const { width, height } = videoTrack.getSettings();
        if (canvasRef.current && width && height) {
          canvasRef.current.width = width;
          canvasRef.current.height = height;
        }
      }
      await metadataPromise;
      if (!avState.current(generation)) return false;
      if (video.videoWidth && video.videoHeight && canvasRef.current) {
        canvasRef.current.width = video.videoWidth;
        canvasRef.current.height = video.videoHeight;
      }

      // Validate that canvas has valid dimensions before proceeding
      if (
        !canvasRef.current ||
        canvasRef.current.width === 0 ||
        canvasRef.current.height === 0
      ) {
        const video = videoRef.current;
        console.warn(
          `[startWebcam] Canvas dimensions still invalid after metadata wait: canvas=${canvasRef.current?.width}x${canvasRef.current?.height}, video=${video?.videoWidth}x${video?.videoHeight}`,
        );
        // If video has dimensions, use them as fallback
        if (video && video.videoWidth > 0 && video.videoHeight > 0) {
          if (canvasRef.current) {
            canvasRef.current.width = video.videoWidth;
            canvasRef.current.height = video.videoHeight;
            console.log(
              `[startWebcam] Using video dimensions as fallback: ${video.videoWidth}x${video.videoHeight}`,
            );
          }
        } else {
          // Video still has no dimensions - cannot proceed
          throw new Error(
            "Failed to obtain video dimensions after 5-second wait. Browser may not have granted camera permissions or device is not available.",
          );
        }
      }

      console.log(
        `[startWebcam] About to start capture loop, videoRef=${
          videoRef.current ? "OK" : "NULL"
        }, canvasRef=${canvasRef.current ? "OK" : "NULL"}`,
      );
      if (videoRef.current) {
        console.log(
          `[startWebcam] Video: playing=${!videoRef.current
            .paused}, width=${videoRef.current.videoWidth}, height=${videoRef.current.videoHeight}`,
        );
      }

      if (!avState.current(generation)) return false;
      const scale = Math.min(
        1,
        320 / canvasRef.current.width,
        240 / canvasRef.current.height,
      );
      canvasRef.current.width = Math.max(
        2,
        Math.floor((canvasRef.current.width * scale) / 2) * 2,
      );
      canvasRef.current.height = Math.max(
        2,
        Math.floor((canvasRef.current.height * scale) / 2) * 2,
      );
      lastFrameTimeRef.current = performance.now();
      frameIntervalRef.current = 1000 / settings.targetFps;
      frameQueueRef.current = [];

      await configureUpload(
        canvasRef.current.width,
        canvasRef.current.height,
        generation,
      );
      if (!avState.current(generation)) return false;
      if (!avState.complete(generation)) return false;
      captureReadyRef.current = true;
      setCaptureRevision((revision) => revision + 1);
      setIsWebcamRunning(avState.active);

      console.log("[Client] Starting render loops...");
      console.log(
        `[Client] frameInterval set to: ${frameIntervalRef.current}ms (${settings.targetFps} FPS)`,
      );

      // Log stream state before starting capture
      if (streamRef.current) {
        console.log(
          `[Client] Stream state before capture: active=${streamRef.current.active}, tracks=${streamRef.current.getTracks().length}`,
        );
        streamRef.current.getTracks().forEach((track) => {
          console.log(
            `[Client] Track (${track.kind}): readyState=${track.readyState}, enabled=${track.enabled}, muted=${track.muted}`,
          );
        });
      }

      console.log("[Client] Webcam started successfully");
      captureStarted = true;
      return true;
    } catch (err) {
      const errMsg = `Failed to start webcam: ${String(err)}`;
      console.error("[Client]", errMsg);
      console.error("[Client] Error:", err);
      if (avState.current(generation)) {
        if (clientRef.current?.getState() === ConnectionState.CONNECTED) {
          try {
            clientRef.current.sendUnencryptedAcipPacket(
              PacketType.STREAM_STOP,
              buildStreamStartPacket(false),
            );
          } catch (error) {
            console.debug("[Client] Could not stop failed capture", error);
          }
        }
        if (videoRef.current) videoRef.current.srcObject = null;
        captureReadyRef.current = false;
        setIsWebcamRunning(false);
        setError(errMsg);
      }
      if (avState.current(generation))
        avState.stop(() => {
          videoEncoderRef.current?.destroy();
          videoEncoderRef.current = null;
          streamRef.current?.getTracks().forEach((track) => track.stop());
          streamRef.current = null;
          testPatternSourceRef.current?.stop();
          testPatternSourceRef.current = null;
        });
      return false;
    } finally {
      if (!captureStarted && displayStreamRef.current === providedStream)
        displayStreamRef.current = null;
      cancelMetadataRef.current?.();
      startingRef.current = false;
      if (!mountedRef.current) avState.dispose();
    }
  }, [
    avState,
    configureUpload,
    connectionState,
    includeAudio,
    validateEncoding,
    settings,
    videoRef,
    canvasRef,
    clientRef,
    frameIntervalRef,
    lastFrameTimeRef,
    frameQueueRef,
    setError,
    testPattern,
  ]);

  const captureActionsRef = useRef({ startWebcam, stopWebcam });
  useLayoutEffect(() => {
    captureActionsRef.current = { startWebcam, stopWebcam };
  }, [startWebcam, stopWebcam]);
  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
      captureActionsRef.current.stopWebcam();
      if (!startingRef.current) avState.dispose();
    };
  }, [avState]);

  const previousEncodingRef = useRef(encodingPreference);
  useEffect(() => {
    if (previousEncodingRef.current === encodingPreference) return;
    previousEncodingRef.current = encodingPreference;
    if (!streamRef.current && !startingRef.current) return;
    let cancelled = false;
    // Retain the selected display track while rebuilding the upload encoder.
    const display = displayStreamRef.current;
    if (display) streamRef.current = null;
    captureActionsRef.current.stopWebcam();
    void (async () => {
      while (startingRef.current)
        await new Promise<void>((resolve) => window.setTimeout(resolve, 10));
      if (
        !cancelled &&
        clientRef.current?.getState() === ConnectionState.CONNECTED
      ) {
        pendingDisplayRef.current = display ?? undefined;
        await captureActionsRef.current.startWebcam();
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [encodingPreference, clientRef]);

  useEffect(() => {
    const handleDevicePreferencesChanged = (event: Event) => {
      const change = (event as CustomEvent<MediaDevicePreferencesChange>)
        .detail;
      if (
        !change?.changedKeys.includes("cameraId") ||
        !!displayStreamRef.current ||
        (!streamRef.current && !startingRef.current) ||
        testPattern.enabled
      )
        return;

      const activeCameraId =
        streamRef.current?.getVideoTracks()[0]?.getSettings().deviceId ?? "";
      if (streamRef.current && activeCameraId === change.preferences.cameraId)
        return;

      stopWebcam();
      void (async () => {
        // If a camera request was already in flight, let it observe the stop
        // generation and release its stream before starting the new device.
        while (startingRef.current) {
          await new Promise<void>((resolve) => window.setTimeout(resolve, 10));
        }
        await startWebcam();
      })();
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
  }, [isWebcamRunning, startWebcam, stopWebcam, testPattern.enabled]);

  const startVideoStream = useCallback(
    async (stream?: MediaStream) => {
      stopWebcam();
      const generation = avState.generation;
      while (startingRef.current && mountedRef.current)
        await new Promise<void>((resolve) => window.setTimeout(resolve, 10));
      if (
        !mountedRef.current ||
        avState.generation !== generation ||
        (stream && stream.getVideoTracks()[0]?.readyState !== "live")
      )
        return false;
      pendingDisplayRef.current = stream;
      return (await startWebcam()) === true;
    },
    [startWebcam, stopWebcam, avState],
  );

  return {
    startVideoStream,
    startWebcam,
    stopWebcam,
    isWebcamRunning,
    sendFileFrame: (
      frame: { data: Uint8Array; width: number; height: number },
      canvas: HTMLCanvasElement,
    ) => webcamCaptureLoopRef.current?.(false, canvas, frame),
  };
}
