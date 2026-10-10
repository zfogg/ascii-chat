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
import type { AsciiFrame } from "../network/AsciiFrameParser";
import type { BinarySettingsConfig } from "../components";
import { getMediaDevicePreferences } from "../utils/mediaDevicePreferences";
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
  captureFrame: (
    drawVideo?: boolean,
    sourceCanvas?: HTMLCanvasElement,
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

  const generationRef = useRef(0);
  const startingRef = useRef(false);
  const captureReadyRef = useRef(false);
  const streamRef = useRef<MediaStream | null>(null);
  const videoEncoderRef = useRef<VideoUploadEncoder | null>(null);
  const webcamCaptureLoopRef = useRef<
    ((drawVideo?: boolean, sourceCanvas?: HTMLCanvasElement) => void) | null
  >(null);
  const captureTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const testPatternSourceRef = useRef<TestPatternVideoSource | null>(null);

  const captureLoopFrameCountRef = useRef(0);
  const lastFrameHashRef = useRef(0);
  const uniqueFrameCountRef = useRef(0);
  const videFrameUpdateCountRef = useRef(0); // Track actual VIDEO updates

  const [isWebcamRunning, setIsWebcamRunning] = useState(false);

  const configureUpload = useCallback(
    async (
      width: number,
      height: number,
      generation: number,
      after?: CompressedVideoEncoding,
    ) => {
      const conn = clientRef.current;
      if (!conn) return;
      const encoder = await createEncoder(
        width,
        height,
        settings.targetFps,
        (chunk, codec) => {
          if (generation !== generationRef.current || !captureReadyRef.current)
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
      if (generation !== generationRef.current) {
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
          settings.targetFps,
          settings.colorMode,
          settings.colorFilter,
          settings.palette,
          settings.paletteChars,
          settings.matrixRain,
          conn.videoCodecCapabilities,
        ),
      );
      console.info(
        `[Client] Upload encoding: ${encoder?.codec ?? "raw"} ${width}x${height}`,
      );
    },
    [clientRef, settings, terminalDimensions, createEncoder],
  );

  // Inner loop function that doesn't have dependencies - this prevents RAF recursion from breaking
  const createWebcamCaptureLoop = useCallback(() => {
    // Timer-based frame sending to match server render loop (not RAF-based)
    // RAF fires at monitor refresh rate (60+ Hz) regardless of frame send interval
    // Timer ensures we send at exactly the target FPS, matching C client behavior
    const sendOneFrame = (
      drawVideo = true,
      sourceCanvas?: HTMLCanvasElement,
    ) => {
      const now = performance.now();

      // Call captureAndSendFrame through ref to get the latest version
      const conn = clientRef.current;
      if (
        captureReadyRef.current &&
        conn &&
        connectionState === ConnectionState.CONNECTED
      ) {
        if ((conn.getBufferedAmount?.() ?? 0) > 256 * 1024) return;
        const frame = captureFrame(drawVideo, sourceCanvas);
        if (frame && frame.data) {
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

          if (videoEncoderRef.current) {
            const encoderCanvas = sourceCanvas ?? canvasRef.current;
            if (!encoderCanvas) return;
            let videoFrame: VideoFrame | undefined;
            try {
              videoFrame = new VideoFrame(encoderCanvas, {
                timestamp: Math.round(now * 1000),
              });
              videoEncoderRef.current.encode(videoFrame, settings.targetFps);
            } catch (error) {
              const failedCodec = videoEncoderRef.current.codec;
              const failedLabel = videoEncoderRef.current.label;
              videoEncoderRef.current.destroy();
              videoEncoderRef.current = null;
              if (encodingPreference !== "auto") {
                captureReadyRef.current = false;
                streamRef.current?.getTracks().forEach((track) => track.stop());
                streamRef.current = null;
                testPatternSourceRef.current?.stop();
                testPatternSourceRef.current = null;
                try {
                  conn.sendUnencryptedAcipPacket(
                    PacketType.STREAM_STOP,
                    buildStreamStartPacket(false),
                  );
                } catch (stopError) {
                  console.debug(
                    "[Client] Could not stop failed capture",
                    stopError,
                  );
                }
                setIsWebcamRunning(false);
                setError(
                  `${failedLabel} encoding failed: ${String(error)} Use ?encoding=raw.`,
                );
              } else {
                captureReadyRef.current = false;
                const generation = generationRef.current;
                void configureUpload(
                  frame.width,
                  frame.height,
                  generation,
                  failedCodec,
                )
                  .then(() => {
                    if (generation === generationRef.current)
                      captureReadyRef.current = true;
                  })
                  .catch((fallbackError: unknown) => {
                    if (generation === generationRef.current) {
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
    captureFrame,
    configureUpload,
    encodingPreference,
    connectionState,
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
      if (videoTrack) {
        void videoTrack
          .applyConstraints({
            width: { ideal: 1280 },
            height: { ideal: 720 },
            frameRate: { ideal: settings.targetFps },
          })
          .catch((error) =>
            console.warn("[Client] Unable to update webcam settings:", error),
          );
      }
      const sendInterval = 1000 / settings.targetFps;
      const video = videoRef.current;
      let stopped = false;
      let animationFrameId: number | null = null;
      let lastSentAt = 0;
      let lastVideoTime = Number.NaN;
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
                const now = performance.now();
                const canvas = canvasRef.current;
                const context = canvas?.getContext("2d");
                if (canvas && context && now - lastSentAt >= sendInterval) {
                  if (settings.flipX) {
                    context.save();
                    context.translate(canvas.width, 0);
                    context.scale(-1, 1);
                    context.drawImage(frame, 0, 0, canvas.width, canvas.height);
                    context.restore();
                  } else {
                    context.drawImage(frame, 0, 0, canvas.width, canvas.height);
                  }
                  webcamCaptureLoopRef.current?.(false);
                  lastSentAt = now;
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
        const onVideoFrame = (now: number) => {
          if (stopped) return;
          if (now - lastSentAt >= sendInterval) {
            webcamCaptureLoopRef.current?.();
            lastSentAt = now;
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
        const pollDecodedFrame = (now: number) => {
          if (stopped) return;
          if (video) {
            const videoTime = video.currentTime;
            if (
              video.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA &&
              !video.paused &&
              videoTime !== lastVideoTime &&
              now - lastSentAt >= sendInterval
            ) {
              webcamCaptureLoopRef.current?.();
              lastVideoTime = videoTime;
              lastSentAt = now;
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
    settings.flipX,
    settings,
    videoRef,
  ]);

  const startWebcam = useCallback(async () => {
    if (startingRef.current || streamRef.current) return;
    captureReadyRef.current = false;
    console.log("[Client] startWebcam() called");
    console.log(
      `[DEBUG] videoRef.current=${!!videoRef.current}, canvasRef.current=${!!canvasRef.current}`,
    );

    if (!videoRef.current || !canvasRef.current) {
      console.error("[Client] Video or canvas element not ready");
      setError("Video or canvas element not ready");
      return;
    }

    console.log(
      `[DEBUG] connectionState=${connectionState} vs CONNECTED=${ConnectionState.CONNECTED}`,
    );
    if (connectionState !== ConnectionState.CONNECTED) {
      console.error(
        `[Client] Not connected (state=${connectionState}), cannot start webcam`,
      );
      setError("Must be connected to server before starting webcam");
      return;
    }

    console.log("[Client] Passed all initial checks");

    startingRef.current = true;
    const generation = ++generationRef.current;
    try {
      validateEncoding();
      setError("");
      // Send STREAM_START to notify server we're about to send video
      if (clientRef.current) {
        console.log("[Client] Sending STREAM_START before webcam...");
        const streamPayload = buildStreamStartPacket(includeAudio);
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

      const preferredCameraId = getMediaDevicePreferences().cameraId;
      const cameraIndexValue = preferredCameraId
        ? null
        : new URLSearchParams(window.location.search).get("videoDeviceIndex");
      const cameraIndex =
        cameraIndexValue === null ? null : Number(cameraIndexValue);
      let videoConstraints: MediaTrackConstraints = {
        width: { ideal: w },
        height: { ideal: h },
        frameRate: { ideal: settings.targetFps },
      };
      if (cameraIndex !== null) {
        if (!Number.isInteger(cameraIndex) || cameraIndex < 0) {
          throw new Error("videoDeviceIndex must be a non-negative integer");
        }
        const cameras = (
          await navigator.mediaDevices.enumerateDevices()
        ).filter((device) => device.kind === "videoinput");
        const camera = cameras[cameraIndex];
        if (!camera?.deviceId) {
          throw new Error(
            `Camera index ${cameraIndex} is unavailable (${cameras.length} camera(s) found)`,
          );
        }
        videoConstraints = {
          ...videoConstraints,
          deviceId: { exact: camera.deviceId },
        };
        console.log(
          `[Client] Selecting camera ${cameraIndex}: ${camera.label}`,
        );
      } else if (preferredCameraId) {
        videoConstraints = {
          ...videoConstraints,
          deviceId: { exact: preferredCameraId },
        };
      }

      let stream: MediaStream;
      if (testPattern.enabled) {
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
        try {
          stream = await navigator.mediaDevices.getUserMedia({
            video: videoConstraints,
            audio: false,
          });
        } catch (err) {
          if (cameraIndex !== null) throw err;
          console.error(
            "[Client] getUserMedia failed (trying fallback without constraints):",
            err,
          );
          try {
            stream = await navigator.mediaDevices.getUserMedia({
              video: true,
              audio: false,
            });
          } catch (err2) {
            console.error("[Client] getUserMedia failed completely:", err2);
            throw err2;
          }
        }
      }

      // Some Windows UVC cameras only expose 60 FPS at 720p or widescreen
      // VGA, while the default 640x480 mode is limited to 30 FPS. ASCII input
      // is downscaled below, so negotiate a 720p source only when the current
      // mode is below target and this camera advertises a high-FPS mode.
      const videoTrack = stream.getVideoTracks()[0];
      if (videoTrack?.getCapabilities && videoTrack.getSettings) {
        const capabilities = videoTrack.getCapabilities();
        const currentSettings = videoTrack.getSettings();
        const currentFps = currentSettings.frameRate;
        const maximumFps = capabilities.frameRate?.max;
        if (
          maximumFps !== undefined &&
          maximumFps >= Math.min(settings.targetFps, 45) &&
          (currentFps === undefined ||
            currentFps + 1 < Math.min(settings.targetFps, maximumFps))
        ) {
          try {
            await videoTrack.applyConstraints({
              width: { ideal: Math.max(w, 1280) },
              height: { ideal: Math.max(h, 720) },
              frameRate: { ideal: Math.min(settings.targetFps, maximumFps) },
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
        generation !== generationRef.current ||
        clientRef.current?.getState() !== ConnectionState.CONNECTED
      ) {
        stream.getTracks().forEach((track) => track.stop());
        return;
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

      // Set up metadata listener BEFORE playing to catch the event
      const metadataPromise = new Promise<void>((resolve) => {
        const handleMetadata = () => {
          const video = videoRef.current!;
          const canvas = canvasRef.current!;
          console.log(
            `[Client] Webcam metadata loaded: ${video.videoWidth}x${video.videoHeight}, videoTime=${video.currentTime}, paused=${video.paused}`,
          );
          console.log("[DEBUG] Metadata event - full video element state:", {
            videoWidth: video.videoWidth,
            videoHeight: video.videoHeight,
            readyState: video.readyState,
            networkState: video.networkState,
            currentTime: video.currentTime,
            duration: video.duration,
            paused: video.paused,
            srcObject: !!video.srcObject,
            src: video.src,
          });

          // Check if dimensions are valid before resizing
          if (video.videoWidth > 0 && video.videoHeight > 0) {
            canvas.width = video.videoWidth;
            canvas.height = video.videoHeight;
            console.log(
              `[Client] Canvas resized to: ${canvas.width}x${canvas.height}`,
            );
            console.log("[DEBUG] Canvas state after resize:", {
              width: canvas.width,
              height: canvas.height,
              clientWidth: canvas.clientWidth,
              clientHeight: canvas.clientHeight,
            });
          } else {
            console.warn(
              `[DEBUG] Invalid video dimensions: ${video.videoWidth}x${video.videoHeight}, not resizing canvas`,
            );
          }
          video.removeEventListener("loadedmetadata", handleMetadata);
          resolve();
        };
        videoRef.current!.addEventListener("loadedmetadata", handleMetadata);
        console.log("[DEBUG] loadedmetadata listener attached");
      });

      // Now play the video (metadata event may already be queued)
      console.log("[Client] Attempting to play video...");
      console.log("[DEBUG] Video element before play():", {
        videoWidth: video.videoWidth,
        videoHeight: video.videoHeight,
        readyState: video.readyState,
        paused: video.paused,
        srcObject: !!video.srcObject,
      });
      try {
        await video.play();
        console.log("[Client] Video is playing");
        console.log("[DEBUG] Video element after play():", {
          videoWidth: video.videoWidth,
          videoHeight: video.videoHeight,
          readyState: video.readyState,
          paused: video.paused,
        });
      } catch (playErr) {
        console.error("[Client] Video play failed (may be expected):", playErr);
        console.error("[DEBUG] Video state when play() failed:", {
          videoWidth: video.videoWidth,
          videoHeight: video.videoHeight,
          readyState: video.readyState,
          srcObject: !!video.srcObject,
        });
      }

      // Wait for metadata with a timeout (5 seconds) in case it never fires
      const timeoutPromise = new Promise<void>((resolve) => {
        setTimeout(() => {
          console.warn(
            "[Client] Metadata timeout - setting canvas dimensions from current video properties",
          );
          if (
            generation === generationRef.current &&
            !streamRef.current?.active &&
            videoRef.current &&
            canvasRef.current
          ) {
            const video = videoRef.current;
            const canvas = canvasRef.current;
            if (video.videoWidth > 0 && video.videoHeight > 0) {
              canvas.width = video.videoWidth;
              canvas.height = video.videoHeight;
              console.log(
                `[Client] Canvas resized from timeout: ${canvas.width}x${canvas.height}`,
              );
            }
          }
          resolve();
        }, 5000);
      });

      await Promise.race([metadataPromise, timeoutPromise]);

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

      if (generation !== generationRef.current) return;
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
      if (generation !== generationRef.current) return;
      captureReadyRef.current = true;
      setIsWebcamRunning(true);

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
    } catch (err) {
      const errMsg = `Failed to start webcam: ${String(err)}`;
      console.error("[Client]", errMsg);
      console.error("[Client] Error:", err);
      if (generation === generationRef.current) {
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
      videoEncoderRef.current?.destroy();
      videoEncoderRef.current = null;
      streamRef.current?.getTracks().forEach((track) => track.stop());
      streamRef.current = null;
      testPatternSourceRef.current?.stop();
      testPatternSourceRef.current = null;
    } finally {
      startingRef.current = false;
    }
  }, [
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

  const stopWebcam = useCallback(() => {
    generationRef.current++;
    captureReadyRef.current = false;
    // A stopped browser capture must also stop being a server-side video
    // source. Otherwise the server keeps compositing its last (often black)
    // frame over the remaining participants until the connection closes.
    if (
      isWebcamRunning &&
      connectionState === ConnectionState.CONNECTED &&
      clientRef.current
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
  }, [clientRef, connectionState, frameQueueRef, isWebcamRunning, videoRef]);

  const captureActionsRef = useRef({ startWebcam, stopWebcam });
  captureActionsRef.current = { startWebcam, stopWebcam };
  const previousEncodingRef = useRef(encodingPreference);
  useEffect(() => {
    if (previousEncodingRef.current === encodingPreference) return;
    previousEncodingRef.current = encodingPreference;
    if (!streamRef.current && !startingRef.current) return;
    let cancelled = false;
    captureActionsRef.current.stopWebcam();
    void (async () => {
      while (startingRef.current)
        await new Promise<void>((resolve) => window.setTimeout(resolve, 10));
      if (
        !cancelled &&
        clientRef.current?.getState() === ConnectionState.CONNECTED
      )
        await captureActionsRef.current.startWebcam();
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

  return {
    startWebcam,
    stopWebcam,
    isWebcamRunning,
  };
}
