import { useLayoutEffect } from "react";
import { sessionUiSettings, sessionUiMedia } from "../utils/sessionUi";
import { MediaSourceType } from "../hooks/useClientLike";
import { useUrlState } from "../hooks/useUrlState";
import { useState, useEffect, useCallback, useRef } from "react";
import {
  getMirrorModule,
  initMirrorWasm,
  isWasmReady,
} from "@ascii-chat/shared/wasm";
// @ts-expect-error - Generated file without types
import MirrorModuleFactory from "../wasm/dist/mirror.js";
import { SITES } from "@ascii-chat/shared/utils";
import {
  BinarySettings,
  AsciiRenderer,
  PageLayout,
  ModeHeader,
  AsciiChatWebHead,
  VideoUploadModal,
} from "../components";
import type { BinarySettingsConfig } from "../components";
import { AsciiChatMode } from "../utils";
import {
  applyMirrorWasmSettings,
  useClientLike,
  useMirrorRenderLoop,
  useMirrorWebcam,
  setMirrorWasmDimensions,
} from "../hooks";
import { useMirrorFile } from "../hooks/useMirrorFile";
import { useTestPattern } from "@ascii-chat/shared/hooks";

export function MirrorPage() {
  const testPattern = useTestPattern();
  const [playing, setPlaying] = useUrlState("playing", false);
  const resumePending = useRef(playing);
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
    const promise = initMirrorWasm(MirrorModuleFactory);
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
  const applyWasmSettings = useCallback((settings: BinarySettingsConfig) => {
    applyMirrorWasmSettings(
      fileSourceRef.current
        ? {
            ...settings,
            flipX: false,
            animationEnabled: false,
            matrixRain: false,
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
  }, [mediaSource, wasmInitialized, settings, applyWasmSettings]);

  // Handle settings change
  const handleSettingsChange = useCallback(
    (newSettings: BinarySettingsConfig) => {
      setSettings(newSettings);
      // Apply WASM settings immediately so renderer recreates if needed
      applyWasmSettings(newSettings);
    },
    [setSettings, applyWasmSettings],
  );

  // Webcam start logic and auto-start effects
  const pendingWebcamSwitch = useRef<object | null>(null);
  const { startWebcam, cancelStart: cancelWebcamStart } = useMirrorWebcam({
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

  // Render loop that captures and converts frames to ASCII
  useMirrorRenderLoop({
    isWebcamRunning,
    terminalDimensions,
    onMediaFps: setFps,
    fileActive,
    mediaRevision: settings,
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
    animationEnabled: !fileActive && (settings.animationEnabled ?? false),
  });

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
                config={settings}
                onChange={handleSettingsChange}
                mode={AsciiChatMode.MIRROR}
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
              isWebcamRunning,
              mediaSource,
              onStartWebcam: () => {
                stopFile();
                void startWebcam();
              },
              onStopWebcam: stopPlayback,
              onUploadClick: () => setShowUploadModal(true),
              videoRef: undefined,
              status: fileMedia.status
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
              ref={rendererRef}
              uiLoading={!isWebcamRunning}
              uiMessage={
                error ||
                (fileMedia.status
                  ? `${fileMedia.status} ${fileMedia.fileName}`
                  : "Choose a camera or media file to begin")
              }
              onUiAction={(event) => {
                if (event.action === 1 || event.action === 7)
                  handleSettingsChange(sessionUiSettings(event, settings));
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
