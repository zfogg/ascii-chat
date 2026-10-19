import { useEngineSettings } from "../hooks/useEngineSettings";
import { useLayoutEffect } from "react";
import { sessionUiMedia } from "../utils/sessionUi";
import { MediaSourceType } from "../hooks/useClientLike";
import { useUrlState } from "../hooks/useUrlState";
import { useState, useEffect, useCallback, useRef, useMemo } from "react";
import {
  getMirrorModule,
  initMirrorWasm,
  isWasmReady,
  createWasmModule,
  type MirrorModule,
} from "@ascii-chat/shared/wasm";
import { SITES } from "@ascii-chat/shared/utils";
import { BinarySettings } from "../components/shared/BinarySettings";
import { AsciiRenderer } from "@ascii-chat/shared/components/AsciiRenderer";
import { PageLayout } from "../components/PageLayout";
import { ModeHeader } from "../components/ModeHeader";
import { AsciiChatWebHead } from "../components/AsciiChatWebHead";
import { VideoUploadModal } from "../components/VideoUploadModal";
import type { BinarySettingsConfig } from "../components";
import { AsciiChatMode } from "../utils";
import {
  applyMirrorWasmSettings,
  setMirrorWasmDimensions,
} from "../hooks/useWasmOptions";
import { useClientLike } from "../hooks/useClientLike";
import { useMirrorRenderLoop } from "@ascii-chat/shared/hooks";
import { useCameraFrameRate } from "../hooks/useCameraFrameRate";
import { useMirrorWebcam } from "../hooks/useMirrorWebcam";
import { useMirrorFile } from "../hooks/useMirrorFile";
import { useTestPattern } from "@ascii-chat/shared/hooks";

export function MirrorPage() {
  const testPattern = useTestPattern();
  const [requestedFit, setContentFit] = useUrlState("fit", "contain");
  const contentFit = requestedFit === "cover" ? "cover" : "contain";
  const [playing, setPlaying] = useUrlState("playing", false);
  const resumePending = useRef(playing);
  const previousStartupPlaying = useRef(playing);
  const playingRef = useRef(playing);
  useLayoutEffect(() => {
    playingRef.current = playing;
  }, [playing]);
  const [showUploadModal, setShowUploadModal] = useState(false);
  const [wasmModule, setWasmModule] = useState(() => {
    return getMirrorModule();
  });

  // Memoize WASM callbacks to prevent infinite re-render loops.
  // These are module-level functions that never change, so empty deps are correct.
  const initWasm = useCallback(() => {
    const t0 = performance.now();
    console.log(`[Mirror] initWasm start at ${t0.toFixed(1)}ms`);
    const promise = initMirrorWasm((options) =>
      createWasmModule<MirrorModule>("mirror", options),
    );
    promise
      .then(() => {
        setWasmModule(getMirrorModule());
        const t1 = performance.now();
        console.log(
          `[Mirror] initWasm complete at ${t1.toFixed(1)}ms (took ${(t1 - t0).toFixed(1)}ms)`,
        );
      })
      .catch((err) => {
        console.error(
          `[Mirror] initWasm error at ${performance.now().toFixed(1)}ms:`,
          err,
        );
      });
    return promise;
  }, []);

  const fileVideoRef = useRef<HTMLVideoElement>(null);
  const fileCanvasRef = useRef<HTMLCanvasElement>(null);
  const ignoreMediaFps = useCallback(() => {}, []);
  const fileSourceRef = useRef(false);
  const [mediaFlipX, setMediaFlipX] = useUrlState("mediaFlipX", false);
  const [mediaFlipY, setMediaFlipY] = useUrlState("mediaFlipY", false);
  const mediaFlipRef = useRef({ flipX: mediaFlipX, flipY: mediaFlipY });
  useLayoutEffect(() => {
    mediaFlipRef.current = { flipX: mediaFlipX, flipY: mediaFlipY };
  }, [mediaFlipX, mediaFlipY]);
  const applyWasmSettings = useCallback((settings: BinarySettingsConfig) => {
    applyMirrorWasmSettings(
      fileSourceRef.current
        ? {
            ...settings,
            ...mediaFlipRef.current,
            animationEnabled:
              !!settings.animationEnabled && settings.animation === "matrix",
            matrixRain:
              !!settings.animationEnabled && settings.animation === "matrix",
          }
        : settings,
    );
  }, []);

  const setWasmDimensions = useCallback((cols: number, rows: number) => {
    setMirrorWasmDimensions(cols, rows);
  }, []);

  const optionsManager = useClientLike({
    initWasm,
    isWasmReady,
    applyWasmSettings,
    setWasmDimensions,
  });

  const {
    videoRef,
    canvasRef,
    rendererRef,
    streamRef,
    lastFrameTimeRef,
    frameIntervalRef,
    isWebcamRunning,
    setIsWebcamRunning,
    mediaSource,
    setMediaSource,
    error,
    setError,
    terminalDimensions,
    fps,
    setFps,
    wasmInitialized,
    showSettings,
    setShowSettings,
    settings,
    setSettings,
    handleDimensionsChange,
    stopWebcam,
    debugCountRef,
    firstFrameTimeRef,
  } = optionsManager;

  useEffect(() => {
    fileSourceRef.current = mediaSource === MediaSourceType.FILE;
    if (wasmInitialized) applyWasmSettings(settings);
  }, [
    mediaSource,
    wasmInitialized,
    settings,
    applyWasmSettings,
    mediaFlipX,
    mediaFlipY,
  ]);

  // Handle settings change
  const handleSettingsChange = useCallback(
    (newSettings: BinarySettingsConfig) => {
      if (fileSourceRef.current) {
        mediaFlipRef.current = {
          flipX: !!newSettings.flipX,
          flipY: !!newSettings.flipY,
        };
        setMediaFlipX(!!newSettings.flipX);
        setMediaFlipY(!!newSettings.flipY);
        setSettings({
          ...newSettings,
          flipX: settings.flipX ?? true,
          flipY: settings.flipY ?? false,
        });
      } else {
        setSettings(newSettings);
      }
      // The settings effect applies the resolved per-mode flip preference to C.
    },
    [setSettings, settings.flipX, settings.flipY, setMediaFlipX, setMediaFlipY],
  );

  useEngineSettings(wasmInitialized, settings, handleSettingsChange);

  // Webcam start logic and auto-start effects
  const pendingWebcamSwitch = useRef<object | null>(null);
  const {
    startWebcam,
    cancelStart: cancelWebcamStart,
    isStarting,
  } = useMirrorWebcam({
    settings,
    videoRef,
    canvasRef,
    streamRef,
    lastFrameTimeRef,
    setIsWebcamRunning,
    setMediaSource,
    setError,
    wasmInitialized,
    mediaSource,
    isWebcamRunning,
    terminalDimensions,
  });

  const cancelStart = useCallback(() => {
    pendingWebcamSwitch.current = null;
    cancelWebcamStart();
  }, [cancelWebcamStart]);

  const fileMedia = useMirrorFile({
    settings,
    videoRef: fileVideoRef,
    captureWidth: terminalDimensions.cols * 4,
    canvasRef: fileCanvasRef,
    stopWebcam,
    cancelStart,
    setIsWebcamRunning,
    setMediaSource,
    setError,
  });

  const { start: startFile, stop: stopFile } = fileMedia;

  const fileActive = mediaSource === MediaSourceType.FILE;
  const cameraFps = useCameraFrameRate(
    settings,
    setSettings,
    isWebcamRunning &&
      !fileActive &&
      !(
        settings.animationEnabled &&
        (settings.animation === "waveform" || settings.animation === "fft")
      ),
  );
  const contentSettings = useMemo(
    () =>
      fileActive
        ? { ...settings, flipX: mediaFlipX, flipY: mediaFlipY }
        : settings,
    [fileActive, settings, mediaFlipX, mediaFlipY],
  );
  const toggleMedia = useCallback(() => {
    if (!fileMedia.kind) return;
    if (fileActive && !pendingWebcamSwitch.current) {
      const request = {};
      pendingWebcamSwitch.current = request;
      fileMedia.setSuspended(true);
      // Keep the file frame on screen until the camera can supply a frame.
      // Starting the camera commits the source switch only after video.play().
      void startWebcam().then((started) => {
        if (pendingWebcamSwitch.current !== request) return;
        pendingWebcamSwitch.current = null;
        if (!started) fileMedia.setSuspended(false);
      });
    } else {
      cancelStart();
      stopWebcam(true);
      setMediaSource(MediaSourceType.FILE);
      setIsWebcamRunning(true);
      fileMedia.setSuspended(false);
    }
  }, [
    fileMedia,
    fileActive,
    startWebcam,
    cancelStart,
    stopWebcam,
    setMediaSource,
    setIsWebcamRunning,
  ]);

  // Animations can paint while hardware permission and capture are pending.
  const renderActive =
    isWebcamRunning || (playing && !!settings.animationEnabled && !fileActive);

  // Render loop that captures and converts frames to ASCII
  useMirrorRenderLoop({
    cameraVideoRef: videoRef,
    isWebcamRunning: renderActive,
    terminalDimensions,
    onMediaFps: setFps,
    fileActive,
    contentFit,
    mediaRevision: contentSettings,
    captureFrame: fileActive
      ? fileMedia.captureFrame
      : optionsManager.captureFrame,
    mediaVideoRef:
      fileActive && fileMedia.kind === "video" ? fileVideoRef : undefined,
    canvasRef,
    rendererRef,
    debugCountRef,
    firstFrameTimeRef,
    frameIntervalRef,
    streamRef,
    animation: settings.animation ?? "matrix",
    animationEnabled: settings.animationEnabled ?? false,
  });

  useEffect(() => {
    // URL navigation can reuse the mounted page rather than initialize refs.
    if (playing !== previousStartupPlaying.current) {
      resumePending.current = playing;
      if (!playing) {
        cancelStart();
        stopWebcam();
      }
    }
    previousStartupPlaying.current = playing;
  }, [playing, cancelStart, stopWebcam]);

  useEffect(() => {
    if (
      !resumePending.current ||
      isWebcamRunning ||
      !wasmInitialized ||
      terminalDimensions.cols <= 0 ||
      terminalDimensions.rows <= 0
    )
      return;
    resumePending.current = false;
    // Synthetic test input already has its own startup path.
    if (!testPattern.enabled) void startWebcam();
  }, [
    playing,
    wasmInitialized,
    terminalDimensions,
    startWebcam,
    testPattern.enabled,
    isWebcamRunning,
  ]);

  useEffect(() => {
    if (isWebcamRunning && mediaSource === MediaSourceType.WEBCAM)
      setPlaying(true);
  }, [isWebcamRunning, mediaSource, setPlaying]);

  const stopPlayback = useCallback(() => {
    resumePending.current = false;
    setPlaying(false);
    stopFile();
  }, [setPlaying, stopFile]);

  const handleVideoFileSelect = useCallback(
    (file: File) => {
      // A browser File cannot be reopened from a URL after refresh.
      resumePending.current = false;
      setPlaying(false);
      void startFile(file);
    },
    [startFile, setPlaying],
  );

  // Sync terminal dimensions to WASM module when they change
  useEffect(() => {
    if (
      wasmInitialized &&
      terminalDimensions.cols > 0 &&
      terminalDimensions.rows > 0
    ) {
      try {
        setWasmDimensions(terminalDimensions.cols, terminalDimensions.rows);
        // Reapply all settings after resize to keep C side in sync with JS cache
        applyWasmSettings(settings);
      } catch (err) {
        console.error("Failed to sync dimensions to WASM:", err);
      }
    }
  }, [
    terminalDimensions,
    wasmInitialized,
    setWasmDimensions,
    settings,
    applyWasmSettings,
  ]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      // Fast Refresh preserves refs but stops the stream through this cleanup.
      // Re-arm the saved intent so the replacement effects can resume playback.
      resumePending.current = playingRef.current;
      stopWebcam();
    };
  }, [stopWebcam]);

  return (
    <>
      <video ref={fileVideoRef} playsInline style={{ display: "none" }} />
      <canvas ref={fileCanvasRef} style={{ display: "none" }} />
      <AsciiChatWebHead
        title="Mirror Mode - ascii-chat Web Client"
        description="Test your webcam with real-time ASCII art rendering. See yourself in terminal-style graphics."
        url={`${SITES.WEB}/mirror`}
      />
      <PageLayout
        videoRef={videoRef}
        canvasRef={canvasRef}
        header={
          <ModeHeader
            showSettings={showSettings}
            settingsPanel={
              <BinarySettings
                cameraFps={cameraFps}
                config={contentSettings}
                onChange={handleSettingsChange}
                mode={AsciiChatMode.MIRROR}
                contentFit={contentFit}
                onContentFitChange={setContentFit}
              />
            }
            controlBar={{
              title: testPattern.enabled
                ? "Mirror mode (synthetic test input)"
                : "Mirror mode",
              dimensions: terminalDimensions,
              fps: fileMedia.loading ? 0 : fps,
              targetFps: settings.targetFps,
              showBandwidth: false,
              isWebcamRunning: renderActive || isStarting,
              webcamActionLabels: isStarting
                ? { start: "Start Webcam", stop: "Cancel startup" }
                : undefined,
              mediaSource,
              onStartWebcam: () => {
                stopFile();
                // This click already starts capture; its URL update must not
                // start a second acquisition while permission is pending.
                previousStartupPlaying.current = true;
                resumePending.current = false;
                setPlaying(true);
                void startWebcam();
              },
              onStopWebcam: stopPlayback,
              onUploadClick: () => setShowUploadModal(true),
              videoRef: undefined,
              status: isStarting
                ? "Starting webcam..."
                : fileMedia.status
                  ? `${fileMedia.status} ${fileMedia.fileName}`
                  : fileMedia.fileName,
              statusControls: (
                <>
                  {fileMedia.loading && (
                    <button onClick={stopPlayback}>Cancel</button>
                  )}
                  {fileActive &&
                    fileMedia.kind &&
                    fileMedia.kind !== "image" && (
                      <>
                        <button
                          className={`px-3 py-2 text-black rounded ${
                            fileMedia.ended || fileMedia.paused
                              ? "bg-green-500 hover:bg-green-600"
                              : "bg-orange-500 hover:bg-orange-600"
                          }`}
                          onClick={
                            fileMedia.ended
                              ? fileMedia.restart
                              : fileMedia.togglePause
                          }
                        >
                          {fileMedia.ended
                            ? "Restart"
                            : fileMedia.paused
                              ? "Play"
                              : "Pause"}
                        </button>
                        {!fileMedia.ended && (
                          <button
                            className="px-3 py-2 bg-terminal-8 rounded"
                            onClick={fileMedia.restart}
                          >
                            Restart
                          </button>
                        )}
                      </>
                    )}
                  {fileMedia.kind === "video" && (
                    <>
                      <button
                        className="px-3 py-2 bg-terminal-8 rounded"
                        onClick={() => {
                          if (fileVideoRef.current)
                            fileVideoRef.current.muted =
                              !fileVideoRef.current.muted;
                        }}
                      >
                        {fileMedia.muted ? "Unmute" : "Mute"}
                      </button>
                      <label className="flex items-center gap-2 text-xs">
                        Volume
                        <input
                          aria-label="Volume"
                          type="range"
                          min="0"
                          max="1"
                          step="0.05"
                          value={fileMedia.volume}
                          onChange={(event) => {
                            if (fileVideoRef.current)
                              fileVideoRef.current.volume = Number(
                                event.target.value,
                              );
                          }}
                          className="w-20"
                        />
                      </label>
                    </>
                  )}
                  {fileMedia.kind && (
                    <button
                      className="px-3 py-2 bg-terminal-8 rounded"
                      aria-pressed={fileActive}
                      onClick={toggleMedia}
                    >
                      Toggle Media
                    </button>
                  )}
                </>
              ),
              onSettingsClick: () => setShowSettings(!showSettings),
              showConnectionButton: false,
              showSettingsButton: true,
              settingsOpen: showSettings,
            }}
          />
        }
        renderer={(() => {
          return (
            <AsciiRenderer
              clientLikeKind="mirror"
              ref={rendererRef}
              contentFit={contentFit}
              uiLoading={!renderActive}
              uiMessage={
                error ||
                (fileMedia.status
                  ? `${fileMedia.status} ${fileMedia.fileName}`
                  : "Choose a camera or media file to begin")
              }
              onUiAction={(event) => {
                if (event.key === "[") toggleMedia();
                else if (event.action === 6 && fileActive)
                  fileMedia.togglePause();
                else
                  sessionUiMedia(
                    event,
                    fileActive ? fileVideoRef.current : videoRef.current,
                  );
                if (event.action === 3) stopPlayback();
              }}
              zalgo={settings.zalgo ?? 0}
              columns={settings.width}
              rows={settings.height}
              onDimensionsChange={handleDimensionsChange}
              onFpsChange={
                fileMedia.loading || (fileActive && fileMedia.kind === "video")
                  ? ignoreMediaFps
                  : setFps
              }
              error={error}
              showFps
              wasmModuleReady={!!wasmModule}
              // initMirrorWasm already initialized the C options; initializing
              // them again here can abort the WASM allocator during page load.
              initializeOptions={false}
              matrixMode={!fileActive && (settings.matrixRain ?? false)}
            />
          );
        })()}
        modal={
          <VideoUploadModal
            isOpen={showUploadModal}
            onClose={() => setShowUploadModal(false)}
            onFileSelect={handleVideoFileSelect}
            allowImages
          />
        }
      />
    </>
  );
}
