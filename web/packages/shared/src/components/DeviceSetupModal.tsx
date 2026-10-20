import { acquireMediaStream } from "../devices/acquireMediaStream";
import { playMedia } from "../devices/playMedia";
import {
  useCameraExposure,
  type CameraExposurePreference,
} from "../hooks/useCameraExposure";
import { cameraRateStore } from "../capture/cameraRate";
import { supportsFastExposure } from "../devices/cameraExposure";
import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type Dispatch,
  type SetStateAction,
} from "react";
import { Modal, ModalTitle } from "./Modal";
import { acquireCameraPreview } from "../devices/acquireCameraPreview";
import {
  getMediaDevicePreferences,
  applyAudioOutputDevice,
  type MediaDevicePreferences,
  saveMediaDevicePreferences,
} from "../devices/mediaDevicePreferences";

export type VideoEncoding = "auto" | "hevc" | "h264" | "raw";
const VIDEO_ENCODING_LABELS = {
  hevc: "HEVC (H.265)",
  h264: "H.264",
  raw: "Raw pixels",
};
export interface DeviceSetupModalProps {
  preferences?: MediaDevicePreferences;
  onPreferencesChange?: Dispatch<SetStateAction<MediaDevicePreferences>>;
  readPreferences?: () => MediaDevicePreferences;
  savePreferences?: (preferences: MediaDevicePreferences) => void;
  resolveCamera?: (options: {
    selectedCameraId?: string | undefined;
    cameras?: readonly MediaDeviceInfo[];
  }) => Promise<string>;
  onCameraChange?: () => void;
  exposurePreference?: CameraExposurePreference;
  videoEncodings?: {
    loading: boolean;
    supportedEncodings: ("hevc" | "h264" | "raw")[];
    selectedEncoding: "hevc" | "h264" | "raw";
    bestEncoding: "hevc" | "h264" | "raw";
    selectEncoding: (encoding: VideoEncoding) => void;
    source?: string;
    error?: string | null;
  };

  open: boolean;
  onClose: () => void;
}

interface DeviceChoices {
  cameras: MediaDeviceInfo[];
  microphones: MediaDeviceInfo[];
  speakers: MediaDeviceInfo[];
  defaultMicrophoneId: string;
}

type InputDeviceKey = "cameraId" | "microphoneId";

const INPUT_DEVICE_KEYS: InputDeviceKey[] = ["cameraId", "microphoneId"];

const EMPTY_INPUT_ERRORS: Record<InputDeviceKey, string | null> = {
  cameraId: null,
  microphoneId: null,
};

function isDeviceStartupTimeout(cause: unknown): boolean {
  return (
    (cause instanceof DOMException || cause instanceof Error) &&
    (cause.name === "AbortError" || cause.name === "TimeoutError") &&
    /timeout|timed out/i.test(cause.message)
  );
}

function describeDeviceError(cause: unknown): string {
  const exception =
    cause instanceof DOMException || cause instanceof Error ? cause : null;
  const name = exception?.name;
  if (name === "NotReadableError" || name === "TrackStartError")
    return "In use (unavailable for this tab)";
  if (name === "NotAllowedError" || name === "PermissionDeniedError")
    return "Access denied. Allow access in your browser to use this device.";
  if (isDeviceStartupTimeout(cause))
    return "Startup timed out. This device may be in use by another tab or app.";
  return exception?.message || "Unable to open this device.";
}

const EMPTY_CHOICES: DeviceChoices = {
  cameras: [],
  microphones: [],
  speakers: [],
  defaultMicrophoneId: "",
};

function getComparableDeviceLabel(label: string): string {
  return label
    .replace(/^(default|communications)\s*[-:â€“]\s*/i, "")
    .trim()
    .toLocaleLowerCase();
}

function deduplicateDefaultDevice(
  devices: MediaDeviceInfo[],
  preserveSystemDefault = false,
): MediaDeviceInfo[] {
  const isAlias = (device: MediaDeviceInfo) =>
    device.deviceId === "default" ||
    /^(default|communications)\s*[-:â€“]\s*/i.test(device.label);

  return devices.filter((device) => {
    if (preserveSystemDefault && device.deviceId === "default") return true;
    if (!isAlias(device) || !device.label) return true;
    const comparableLabel = getComparableDeviceLabel(device.label);
    return !devices.some(
      (candidate) =>
        candidate !== device &&
        !isAlias(candidate) &&
        getComparableDeviceLabel(candidate.label) === comparableLabel,
    );
  });
}

const resolveStoredCamera = async ({
  selectedCameraId,
}: {
  selectedCameraId?: string | undefined;
}) => selectedCameraId ?? getMediaDevicePreferences().cameraId;
export function DeviceSetupModal({
  open,
  onClose,
  preferences: controlledPreferences,
  onPreferencesChange,
  readPreferences = getMediaDevicePreferences,
  savePreferences = saveMediaDevicePreferences,
  resolveCamera = resolveStoredCamera,
  onCameraChange,
  exposurePreference,
  videoEncodings,
}: DeviceSetupModalProps) {
  const [encodingDraft, setEncodingDraft] = useState<VideoEncoding | null>(
    null,
  );
  const videoRef = useRef<HTMLVideoElement>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const audioContextRef = useRef<AudioContext | null>(null);
  const microphoneSourceRef = useRef<MediaStreamAudioSourceNode | null>(null);
  const animationFrameRef = useRef<number | null>(null);
  const previewGenerationRef = useRef({ cameraId: 0, microphoneId: 0 });
  const previewOwnersRef = useRef<
    Record<InputDeviceKey, AbortController | null>
  >({ cameraId: null, microphoneId: null });
  const isOpenRef = useRef(open);
  const [choices, setChoices] = useState<DeviceChoices>(EMPTY_CHOICES);
  const [localPreferences, setLocalPreferences] = useState(readPreferences);
  const preferences = controlledPreferences ?? localPreferences;
  const setPreferences = onPreferencesChange ?? setLocalPreferences;
  const [level, setLevel] = useState(0);
  const [testingMicrophone, setTestingMicrophone] = useState(false);
  const [testingSpeakers, setTestingSpeakers] = useState(false);
  const [audioReady, setAudioReady] = useState(false);
  const [audioRunning, setAudioRunning] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [refreshedAt, setRefreshedAt] = useState<number | null>(null);
  useEffect(() => {
    if (refreshedAt === null) return;
    const timer = window.setTimeout(() => setRefreshedAt(null), 3000);
    return () => window.clearTimeout(timer);
  }, [refreshedAt]);
  const [loading, setLoading] = useState({
    cameraId: false,
    microphoneId: false,
  });
  const [hasCamera, setHasCamera] = useState(false);
  const [previewCameraTrack, setPreviewCameraTrack] =
    useState<MediaStreamTrack | null>(null);
  const [hasMicrophone, setHasMicrophone] = useState(false);
  const activeTrack = cameraRateStore.activeTrack();
  const previewTrack = open && hasCamera ? previewCameraTrack : null;
  // The page owns exposure changes for its live capture. Only apply here when
  // device setup owns a standalone preview.
  const exposure = useCameraExposure(
    activeTrack ? null : previewTrack,
    exposurePreference,
  );
  const exposureSupported =
    !!(activeTrack ?? previewTrack) &&
    supportsFastExposure((activeTrack ?? previewTrack)!);

  const [deviceErrors, setDeviceErrors] = useState(EMPTY_INPUT_ERRORS);
  const [unavailableDevices, setUnavailableDevices] = useState<
    Record<string, string>
  >({});

  const refreshDevices = useCallback(async () => {
    const devices = await navigator.mediaDevices.enumerateDevices();
    const microphones = deduplicateDefaultDevice(
      devices.filter((device) => device.kind === "audioinput"),
    );
    const nextChoices = {
      cameras: devices.filter((device) => device.kind === "videoinput"),
      microphones,
      defaultMicrophoneId: "",
      speakers: deduplicateDefaultDevice(
        devices.filter((device) => device.kind === "audiooutput"),
        true,
      ),
    };
    if (isOpenRef.current) setChoices(nextChoices);
    return nextChoices;
  }, []);

  const stopPreview = useCallback(() => {
    for (const key of INPUT_DEVICE_KEYS) {
      previewGenerationRef.current[key]++;
      previewOwnersRef.current[key]?.abort();
      previewOwnersRef.current[key] = null;
    }
    if (animationFrameRef.current !== null) {
      cancelAnimationFrame(animationFrameRef.current);
      animationFrameRef.current = null;
    }
    audioContextRef.current?.close().catch(() => undefined);
    audioContextRef.current = null;
    setAudioReady(false);
    setAudioRunning(false);
    microphoneSourceRef.current = null;
    streamRef.current?.getTracks().forEach((track) => track.stop());
    streamRef.current = null;
    setHasCamera(false);
    setHasMicrophone(false);
    setLoading({ cameraId: false, microphoneId: false });
    if (videoRef.current) videoRef.current.srcObject = null;
    setLevel(0);
    setTestingMicrophone(false);
  }, []);

  const startPreview = useCallback(
    async (
      nextPreferences: typeof preferences,
      changedInput?: InputDeviceKey,
    ) => {
      const inputKeys = changedInput ? [changedInput] : INPUT_DEVICE_KEYS;
      const generations = { ...previewGenerationRef.current };
      const owners = { ...previewOwnersRef.current };
      for (const key of inputKeys) {
        previewOwnersRef.current[key]?.abort();
        owners[key] = new AbortController();
        previewOwnersRef.current[key] = owners[key];
        generations[key] = ++previewGenerationRef.current[key];
        setLoading((current) => ({ ...current, [key]: true }));
        setDeviceErrors((current) => ({ ...current, [key]: null }));
        const preview = streamRef.current;
        const previousTracks =
          key === "cameraId"
            ? preview?.getVideoTracks()
            : preview?.getAudioTracks();
        previousTracks?.forEach((track) => {
          track.stop();
          preview?.removeTrack(track);
        });
        if (key === "cameraId") {
          setHasCamera(false);
          if (videoRef.current) {
            videoRef.current.pause();
            videoRef.current.srcObject = null;
          }
        } else {
          setHasMicrophone(false);
          setTestingMicrophone(false);
          setLevel(0);
          microphoneSourceRef.current?.disconnect();
          microphoneSourceRef.current = null;
          if (animationFrameRef.current !== null)
            cancelAnimationFrame(animationFrameRef.current);
          animationFrameRef.current = null;
        }
      }
      setError(null);
      let devices: DeviceChoices;
      try {
        // Listing devices must not depend on successfully opening a preview.
        devices = await refreshDevices();
        if (!isOpenRef.current) return;
        setPreferences((current) => ({
          ...current,
          speakerId:
            current.speakerId ||
            devices.speakers.find((device) => device.deviceId === "default")
              ?.deviceId ||
            "",
        }));
      } catch (cause) {
        if (isOpenRef.current) {
          setError(describeDeviceError(cause));
          for (const key of inputKeys) {
            if (generations[key] === previewGenerationRef.current[key])
              setLoading((current) => ({ ...current, [key]: false }));
          }
        }
        return;
      }

      await Promise.all(
        inputKeys.map(async (key) => {
          const generation = generations[key];
          const isCurrent = () =>
            isOpenRef.current &&
            generation === previewGenerationRef.current[key];
          if (!isCurrent()) return;
          const owner = owners[key];
          if (!owner) return;
          const isCamera = key === "cameraId";
          const requestedId = nextPreferences[key];
          // Permission can hide IDs during enumeration. Try an explicit saved
          // microphone directly, as we do for cameras, instead of discarding it.
          let deviceId = isCamera ? "" : requestedId;

          let stream: MediaStream | null = null;
          let requestedDefault = false;
          try {
            if (isCamera) {
              deviceId = await resolveCamera({
                selectedCameraId:
                  changedInput === "cameraId"
                    ? nextPreferences.cameraId
                    : undefined,
                cameras: devices.cameras,
              });
              if (!isCurrent()) return;
            }
            requestedDefault = !deviceId;
            const selectedDeviceId = deviceId;
            setPreferences((current) => ({
              ...current,
              [key]: selectedDeviceId,
            }));
            const constraints = deviceId
              ? { deviceId: { exact: deviceId } }
              : true;
            stream = isCamera
              ? await acquireCameraPreview(deviceId, owner.signal)
              : await acquireMediaStream(
                  { video: false, audio: constraints },
                  owner.signal,
                );
            if (!isCurrent()) {
              stream.getTracks().forEach((track) => track.stop());
              return;
            }
            deviceId =
              stream.getTracks()[0]?.getSettings().deviceId || deviceId;
            if (!requestedDefault)
              setPreferences((current) => ({ ...current, [key]: deviceId }));
            setUnavailableDevices((current) => {
              const next = { ...current };
              delete next[`${key}:${deviceId}`];
              return next;
            });
            stream
              .getTracks()
              .forEach((track) => streamRef.current?.addTrack(track));
            if (isCamera) {
              if (videoRef.current) {
                videoRef.current.srcObject = stream;
                await playMedia(videoRef.current, owner.signal, 10_000);
              }
              if (!isCurrent()) return;
              setPreviewCameraTrack(stream.getVideoTracks()[0] ?? null);
              setHasCamera(stream.getVideoTracks().length > 0);
            } else {
              setHasMicrophone(stream.getAudioTracks().length > 0);
              setTestingMicrophone(true);
              const audioContext = audioContextRef.current;
              if (audioContext) {
                const source = audioContext.createMediaStreamSource(stream);
                microphoneSourceRef.current = source;
                const analyser = audioContext.createAnalyser();
                analyser.fftSize = 512;
                source.connect(analyser);
                const samples = new Float32Array(analyser.fftSize);
                const readLevel = () => {
                  if (!isCurrent()) return;
                  if (audioContext.state === "running") {
                    analyser.getFloatTimeDomainData(samples);
                    let sum = 0;
                    for (const sample of samples) sum += sample * sample;
                    const rms = Math.sqrt(sum / samples.length);
                    // Map -60 dBFS to silence and 0 dBFS to a full meter.
                    const decibels = 20 * Math.log10(Math.max(rms, 1e-8));
                    setLevel(Math.max(0, Math.min(1, (decibels + 60) / 60)));
                  } else {
                    setLevel(0);
                  }
                  animationFrameRef.current = requestAnimationFrame(readLevel);
                };
                animationFrameRef.current = requestAnimationFrame(readLevel);
              }
            }
          } catch (cause) {
            if (!isCurrent()) return;
            stream?.getTracks().forEach((track) => {
              track.stop();
              streamRef.current?.removeTrack(track);
            });
            if (isCamera) {
              setHasCamera(false);
              if (videoRef.current?.srcObject === stream) {
                videoRef.current.pause();
                videoRef.current.srcObject = null;
              }
            } else {
              setHasMicrophone(false);
            }
            if (requestedDefault) {
              const revealedDevices = await refreshDevices().catch(
                () => devices,
              );
              if (!isCurrent()) return;
              deviceId =
                (isCamera
                  ? revealedDevices.cameras[0]?.deviceId
                  : revealedDevices.defaultMicrophoneId) || "";
            }
            const message = describeDeviceError(cause);
            setDeviceErrors((current) => ({ ...current, [key]: message }));
            if (deviceId)
              setUnavailableDevices((current) => ({
                ...current,
                [`${key}:${deviceId}`]: isDeviceStartupTimeout(cause)
                  ? "Startup timed out (unavailable for this tab)"
                  : message.startsWith("In use")
                    ? message
                    : "Unavailable for this tab",
              }));
          } finally {
            if (isCurrent()) {
              if (previewOwnersRef.current[key] === owner)
                previewOwnersRef.current[key] = null;
              setLoading((current) => ({ ...current, [key]: false }));
              // Permission changes can reveal additional devices and their labels.
              void refreshDevices().catch(() => undefined);
            }
          }
        }),
      );
    },
    [refreshDevices, resolveCamera, setPreferences],
  );

  const attachPreview = useCallback(
    (element: HTMLVideoElement | null) => {
      videoRef.current = element;
      if (!element) return;
      isOpenRef.current = true;
      streamRef.current = new MediaStream();
      setUnavailableDevices({});
      setDeviceErrors(EMPTY_INPUT_ERRORS);
      let previewAudio: AudioContext | null = null;
      const syncAudioState = () => {
        if (audioContextRef.current === previewAudio)
          setAudioRunning(previewAudio?.state === "running");
      };
      try {
        // Output tests do not require a working camera or microphone.
        previewAudio = new AudioContext();
        audioContextRef.current = previewAudio;
        previewAudio.addEventListener("statechange", syncAudioState);
        setAudioReady(true);
        syncAudioState();
        // A URL-opened modal may need a later click to satisfy autoplay rules.
        void previewAudio.resume().catch(() => undefined);
      } catch (cause) {
        setError(describeDeviceError(cause));
      }
      setEncodingDraft(null);
      const preferences = readPreferences();
      setPreferences(preferences);
      if (
        audioContextRef.current &&
        preferences.speakerId &&
        preferences.speakerId !== "default"
      ) {
        void applyAudioOutputDevice(
          audioContextRef.current,
          preferences.speakerId,
        ).catch(() => setError("This browser cannot select a speaker output."));
      }
      void startPreview(preferences);
      const onDeviceChange = () => {
        void refreshDevices().catch(() => undefined);
      };
      navigator.mediaDevices.addEventListener("devicechange", onDeviceChange);
      return () => {
        previewAudio?.removeEventListener("statechange", syncAudioState);
        isOpenRef.current = false;
        navigator.mediaDevices.removeEventListener(
          "devicechange",
          onDeviceChange,
        );
        stopPreview();
        videoRef.current = null;
      };
    },
    [
      startPreview,
      stopPreview,
      readPreferences,
      refreshDevices,
      setPreferences,
    ],
  );

  if (!open) return null;

  const microphoneTestRunning =
    testingMicrophone && audioRunning && hasMicrophone && !loading.microphoneId;
  const displayedLevel = microphoneTestRunning ? level : 0;

  const changeDevice = (key: keyof typeof preferences, value: string) => {
    const nextPreferences = { ...preferences, [key]: value };
    setPreferences(nextPreferences);
    if (key === "cameraId") onCameraChange?.();
    if (key === "speakerId") {
      const audioContext = audioContextRef.current;
      if (audioContext) {
        void applyAudioOutputDevice(audioContext, value).catch(
          (cause: unknown) =>
            setError(
              cause instanceof Error
                ? cause.message
                : "Unable to select this speaker.",
            ),
        );
      }
    } else {
      void startPreview(nextPreferences, key);
    }
  };

  const handleSave = () => {
    try {
      if (encodingDraft !== null) videoEncodings?.selectEncoding(encodingDraft);
      savePreferences(preferences);
      onClose();
    } catch (cause) {
      setError(
        cause instanceof Error
          ? cause.message
          : "Unable to save device preferences.",
      );
    }
  };

  const playSpeakerTest = async () => {
    const audioContext = audioContextRef.current;
    if (!audioContext)
      throw new Error("Speaker test audio is not ready. Reopen device setup.");
    setTestingSpeakers(true);
    await audioContext.resume();
    if (audioContext.state !== "running")
      throw new Error("The browser did not start speaker test audio.");
    const oscillator = audioContext.createOscillator();
    const gain = audioContext.createGain();
    oscillator.type = "sine";
    oscillator.frequency.setValueAtTime(660, audioContext.currentTime);
    oscillator.frequency.exponentialRampToValueAtTime(
      880,
      audioContext.currentTime + 0.65,
    );
    gain.gain.setValueAtTime(0, audioContext.currentTime);
    gain.gain.linearRampToValueAtTime(0.18, audioContext.currentTime + 0.04);
    gain.gain.setValueAtTime(0.18, audioContext.currentTime + 0.58);
    gain.gain.linearRampToValueAtTime(0, audioContext.currentTime + 0.7);
    oscillator.connect(gain);
    gain.connect(audioContext.destination);
    oscillator.addEventListener(
      "ended",
      () => {
        oscillator.disconnect();
        gain.disconnect();
        setTestingSpeakers(false);
      },
      { once: true },
    );
    oscillator.start();
    oscillator.stop(audioContext.currentTime + 0.7);
  };

  return (
    <Modal
      open={open}
      onClose={onClose}
      className="max-w-3xl rounded-lg border border-gray-500 bg-gray-950 text-gray-100 shadow-2xl"
    >
      <header className="flex items-start justify-between border-b border-gray-500 px-6 py-5">
        <div>
          <ModalTitle
            as="h2"
            className="text-xl font-semibold"
            id="device-setup-title"
          >
            Device setup
          </ModalTitle>
          <p className="mt-1 text-sm text-gray-500">
            Choose your camera and microphone, then check your levels.
          </p>
        </div>
        <button
          aria-label="Close device setup"
          className="rounded px-2 text-2xl leading-none text-gray-500 hover:bg-gray-500 hover:text-gray-100"
          onClick={onClose}
          type="button"
        >
          ×
        </button>
      </header>

      <div className="grid gap-6 p-6 md:grid-cols-2">
        <div>
          <div className="relative aspect-video overflow-hidden rounded border border-gray-500 bg-black">
            <video
              aria-label="Camera preview"
              autoPlay
              className="h-full w-full object-cover"
              muted
              playsInline
              ref={attachPreview}
            />
            {loading.cameraId && (
              <div className="absolute inset-0 grid place-items-center text-sm text-gray-500">
                Starting camera preview…
              </div>
            )}
            {!loading.cameraId && !hasCamera && (
              <div className="absolute inset-0 grid place-items-center text-sm text-gray-500">
                Camera preview unavailable
              </div>
            )}
            <span className="absolute left-3 top-3 rounded bg-black/70 px-2 py-1 text-xs text-gray-100">
              Camera preview
            </span>
          </div>
          <p className="mt-2 text-xs text-gray-500">
            Preview stays local to this browser.
          </p>
          <button
            className={`mt-3 text-xs underline ${refreshedAt !== null ? "text-green-400" : ""}`}
            onClick={() => {
              setUnavailableDevices({});
              setRefreshedAt(null);
              void refreshDevices().then(
                () => setRefreshedAt(Date.now()),
                (cause: unknown) => setError(describeDeviceError(cause)),
              );
            }}
            type="button"
          >
            {refreshedAt !== null ? "Refreshed!" : "Refresh device list"}
          </button>
        </div>

        <div className="space-y-5">
          <div>
            <label className="block text-sm">
              <span className="mb-2 block font-medium">Camera</span>
              <select
                className="w-full rounded border border-gray-500 bg-gray-950 px-3 py-2 text-gray-100 focus:border-cyan-400 focus:outline-none"
                aria-describedby={
                  deviceErrors.cameraId ? "camera-device-error" : undefined
                }
                disabled={choices.cameras.length === 0}
                onChange={(event) =>
                  changeDevice("cameraId", event.currentTarget.value)
                }
                value={preferences.cameraId}
              >
                {choices.cameras.length > 0 && (
                  <option value="">System default</option>
                )}
                {choices.cameras.length === 0 && (
                  <option value="">No cameras found</option>
                )}
                {preferences.cameraId &&
                  !choices.cameras.some(
                    (camera) => camera.deviceId === preferences.cameraId,
                  ) && (
                    <option value={preferences.cameraId}>
                      Selected camera (not currently listed)
                    </option>
                  )}
                {choices.cameras.map((camera, index) => (
                  <option key={camera.deviceId} value={camera.deviceId}>
                    {camera.label || `Camera ${index + 1}`}
                    {unavailableDevices[`cameraId:${camera.deviceId}`] &&
                      ` — ${unavailableDevices[`cameraId:${camera.deviceId}`]}`}
                  </option>
                ))}
              </select>
            </label>
            <label className="mt-3 flex items-center gap-2 text-sm">
              <input
                type="checkbox"
                aria-label="Fast camera exposure"
                checked={exposure.enabled}
                disabled={!exposureSupported}
                onChange={(event) => exposure.select(event.target.checked)}
              />
              Fast camera exposure (debug)
            </label>
            <p className="text-xs text-gray-500">
              Shortens exposure for camera timing tests and may darken the
              image. Off by default.
            </p>
            {exposure.error && <p role="alert">{exposure.error}</p>}
            {deviceErrors.cameraId && (
              <div
                className="mt-2 text-xs text-yellow-400"
                id="camera-device-error"
                role="status"
              >
                <p>
                  {deviceErrors.cameraId} Choose another camera or retry when it
                  is available.
                </p>
                <button
                  className="mt-1 cursor-pointer underline disabled:cursor-not-allowed disabled:opacity-50"
                  type="button"
                  disabled={loading.cameraId}
                  onClick={() => void startPreview(preferences, "cameraId")}
                >
                  Retry camera
                </button>
              </div>
            )}
          </div>

          {videoEncodings && (
            <div>
              <label className="block text-sm">
                <span className="mb-2 block font-medium">Video encoding</span>
                <select
                  className="w-full rounded border border-gray-500 bg-gray-950 px-3 py-2 text-gray-100 focus:border-cyan-400 focus:outline-none"
                  aria-label="Video encoding"
                  disabled={videoEncodings.loading}
                  value={encodingDraft ?? videoEncodings.selectedEncoding}
                  onChange={(event) =>
                    setEncodingDraft(event.currentTarget.value as VideoEncoding)
                  }
                >
                  <option value="auto">
                    Automatic (
                    {VIDEO_ENCODING_LABELS[videoEncodings.bestEncoding]})
                  </option>
                  {videoEncodings.supportedEncodings.map((encoding) => (
                    <option key={encoding} value={encoding}>
                      {VIDEO_ENCODING_LABELS[encoding]}
                    </option>
                  ))}
                  {!videoEncodings.supportedEncodings.includes(
                    videoEncodings.selectedEncoding,
                  ) && (
                    <option value={videoEncodings.selectedEncoding} disabled>
                      {VIDEO_ENCODING_LABELS[videoEncodings.selectedEncoding]}{" "}
                      (unavailable)
                    </option>
                  )}
                </select>
              </label>
              <p className="mt-2 text-xs text-gray-500">
                Used for Client and Discovery uploads. Automatic prefers HEVC,
                then H.264, then raw pixels.
              </p>
              {videoEncodings.source === "url" && (
                <p className="mt-1 text-xs text-gray-500">
                  The URL overrides your saved encoding. Choose an encoding and
                  press Done to save a new preference.
                </p>
              )}
              {videoEncodings.error && (
                <p className="mt-1 text-sm text-red-400">
                  {videoEncodings.error}
                </p>
              )}
            </div>
          )}
          <label className="block text-sm">
            <span className="mb-2 block font-medium">Speakers</span>
            <select
              className="w-full rounded border border-gray-500 bg-gray-950 px-3 py-2 text-gray-100 focus:border-cyan-400 focus:outline-none"
              disabled={choices.speakers.length === 0}
              onChange={(event) =>
                changeDevice("speakerId", event.currentTarget.value)
              }
              value={preferences.speakerId}
            >
              {choices.speakers.length === 0 && (
                <option value="">No speakers found</option>
              )}
              {preferences.speakerId &&
                !choices.speakers.some(
                  (device) => device.deviceId === preferences.speakerId,
                ) && (
                  <option value={preferences.speakerId}>
                    Selected speaker (not currently listed)
                  </option>
                )}
              {choices.speakers.map((speaker, index) => (
                <option key={speaker.deviceId} value={speaker.deviceId}>
                  {speaker.deviceId === "default"
                    ? "System default"
                    : speaker.label || `Speaker ${index + 1}`}
                </option>
              ))}
            </select>
          </label>
          <button
            className="rounded border border-gray-500 px-3 py-1.5 text-xs hover:bg-gray-500 disabled:opacity-50"
            disabled={
              choices.speakers.length === 0 || testingSpeakers || !audioReady
            }
            onClick={() =>
              void playSpeakerTest().catch((cause: unknown) => {
                setTestingSpeakers(false);
                setError(
                  cause instanceof Error
                    ? cause.message
                    : "Speaker test failed.",
                );
              })
            }
            type="button"
          >
            {testingSpeakers ? "Playing test tone…" : "Test speakers"}
          </button>

          <div>
            <label className="block text-sm">
              <span className="mb-2 block font-medium">Microphone</span>
              <select
                className="w-full rounded border border-gray-500 bg-gray-950 px-3 py-2 text-gray-100 focus:border-cyan-400 focus:outline-none"
                aria-describedby={
                  deviceErrors.microphoneId
                    ? "microphone-device-error"
                    : undefined
                }
                disabled={choices.microphones.length === 0}
                onChange={(event) =>
                  changeDevice("microphoneId", event.currentTarget.value)
                }
                value={preferences.microphoneId}
              >
                {choices.microphones.length > 0 && (
                  <option value="">System default</option>
                )}
                {choices.microphones.length === 0 && (
                  <option value="">No microphones found</option>
                )}
                {preferences.microphoneId &&
                  !choices.microphones.some(
                    (device) => device.deviceId === preferences.microphoneId,
                  ) && (
                    <option value={preferences.microphoneId}>
                      Selected microphone (not currently listed)
                    </option>
                  )}
                {choices.microphones.map((microphone, index) => (
                  <option key={microphone.deviceId} value={microphone.deviceId}>
                    {microphone.label || `Microphone ${index + 1}`}
                    {unavailableDevices[
                      `microphoneId:${microphone.deviceId}`
                    ] &&
                      ` — ${unavailableDevices[`microphoneId:${microphone.deviceId}`]}`}
                  </option>
                ))}
              </select>
            </label>
            {deviceErrors.microphoneId && (
              <div
                className="mt-2 text-xs text-yellow-400"
                id="microphone-device-error"
                role="status"
              >
                <p>
                  {deviceErrors.microphoneId} Choose another microphone or retry
                  when it is available.
                </p>
                <button
                  className="mt-1 cursor-pointer underline disabled:cursor-not-allowed disabled:opacity-50"
                  type="button"
                  disabled={loading.microphoneId}
                  onClick={() => void startPreview(preferences, "microphoneId")}
                >
                  Retry microphone
                </button>
              </div>
            )}
          </div>

          <div>
            <div className="mb-2 flex items-center justify-between">
              <span className="text-sm font-medium">Microphone level</span>
              <button
                className="rounded border border-gray-500 px-3 py-1.5 text-xs hover:bg-gray-500 disabled:opacity-50"
                disabled={!hasMicrophone || loading.microphoneId}
                onClick={() => {
                  if (microphoneTestRunning) {
                    setTestingMicrophone(false);
                    setLevel(0);
                    return;
                  }
                  const context = audioContextRef.current;
                  if (!context) return;
                  setTestingMicrophone(true);
                  void context.resume().then(
                    () => {
                      if (audioContextRef.current === context)
                        setAudioRunning(context.state === "running");
                    },
                    (cause: unknown) => {
                      if (audioContextRef.current === context)
                        setError(describeDeviceError(cause));
                    },
                  );
                }}
                type="button"
              >
                {microphoneTestRunning ? "Stop test" : "Test microphone"}
              </button>
            </div>
            <div
              aria-label="Microphone input level"
              className="h-3 overflow-hidden rounded bg-gray-500"
              role="meter"
              aria-valuemin={0}
              aria-valuemax={100}
              aria-valuenow={Math.round(displayedLevel * 100)}
            >
              <div
                className="h-full bg-green-400 transition-[width] duration-100"
                style={{ width: `${displayedLevel * 100}%` }}
              />
            </div>
            <p className="mt-2 text-xs text-gray-500">
              {microphoneTestRunning
                ? "Speak to see your input level."
                : !audioRunning
                  ? "Click Test microphone to enable microphone levels."
                  : "Start the test and speak to check your mic."}
            </p>
          </div>
        </div>
      </div>

      {error && (
        <p className="mx-6 mb-4 rounded border border-red-400/50 bg-red-400/10 px-3 py-2 text-sm text-red-400">
          {error}
        </p>
      )}

      <footer className="flex justify-end gap-2 border-t border-gray-500 px-6 py-4">
        <button
          className="rounded border border-gray-500 px-4 py-2 text-sm hover:bg-gray-500"
          onClick={onClose}
          type="button"
        >
          Cancel
        </button>
        <button
          className="rounded bg-green-400 px-4 py-2 text-sm font-medium text-gray-950 hover:bg-green-500"
          onClick={handleSave}
          type="button"
        >
          Done
        </button>
      </footer>
    </Modal>
  );
}
