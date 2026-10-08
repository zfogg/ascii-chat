import { useCallback, useEffect, useRef, useState } from "react";
import {
  getMediaDevicePreferences,
  saveMediaDevicePreferences,
} from "../utils/mediaDevicePreferences";

interface DeviceSetupModalProps {
  open: boolean;
  onClose: () => void;
}

interface DeviceChoices {
  cameras: MediaDeviceInfo[];
  microphones: MediaDeviceInfo[];
  speakers: MediaDeviceInfo[];
}

const EMPTY_CHOICES: DeviceChoices = {
  cameras: [],
  microphones: [],
  speakers: [],
};

function getComparableDeviceLabel(label: string): string {
  return label
    .replace(/^(default|communications)\s*[-:–]\s*/i, "")
    .trim()
    .toLocaleLowerCase();
}

async function setAudioOutputDevice(
  context: AudioContext,
  deviceId: string,
): Promise<void> {
  const contextWithSink = context as AudioContext & {
    setSinkId?: (sinkId: string) => Promise<void>;
  };
  if (!contextWithSink.setSinkId)
    throw new Error("Speaker selection is not supported by this browser.");
  await contextWithSink.setSinkId(deviceId);
}

function deduplicateDefaultDevice(
  devices: MediaDeviceInfo[],
  preserveSystemDefault = false,
): MediaDeviceInfo[] {
  const isAlias = (device: MediaDeviceInfo) =>
    device.deviceId === "default" || /^(default|communications)\s*[-:–]\s*/i.test(device.label);

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

export function DeviceSetupModal({ open, onClose }: DeviceSetupModalProps) {
  const videoRef = useRef<HTMLVideoElement>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const audioContextRef = useRef<AudioContext | null>(null);
  const animationFrameRef = useRef<number | null>(null);
  const previewGenerationRef = useRef(0);
  const isOpenRef = useRef(open);
  const [choices, setChoices] = useState<DeviceChoices>(EMPTY_CHOICES);
  const [preferences, setPreferences] = useState(getMediaDevicePreferences);
  const [level, setLevel] = useState(0);
  const [testingMicrophone, setTestingMicrophone] = useState(false);
  const [testingSpeakers, setTestingSpeakers] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  const stopPreview = useCallback((invalidate = true) => {
    if (invalidate) previewGenerationRef.current++;
    if (animationFrameRef.current !== null) {
      cancelAnimationFrame(animationFrameRef.current);
      animationFrameRef.current = null;
    }
    audioContextRef.current?.close().catch(() => undefined);
    audioContextRef.current = null;
    streamRef.current?.getTracks().forEach((track) => track.stop());
    streamRef.current = null;
    if (videoRef.current) videoRef.current.srcObject = null;
    setLevel(0);
    setTestingMicrophone(false);
  }, []);

  const startPreview = useCallback(
    async (nextPreferences: typeof preferences) => {
      const generation = ++previewGenerationRef.current;
      setLoading(true);
      setError(null);
      stopPreview(false);
      try {
        let stream: MediaStream;
        try {
          stream = await navigator.mediaDevices.getUserMedia({
            video: nextPreferences.cameraId
              ? { deviceId: { exact: nextPreferences.cameraId } }
              : true,
            audio: nextPreferences.microphoneId
              ? { deviceId: { exact: nextPreferences.microphoneId } }
              : true,
          });
        } catch (cause) {
          if (!nextPreferences.cameraId && !nextPreferences.microphoneId)
            throw cause;
          // A saved device may have been unplugged since the previous visit.
          stream = await navigator.mediaDevices.getUserMedia({
            video: true,
            audio: true,
          });
        }
        if (!isOpenRef.current || generation !== previewGenerationRef.current) {
          stream.getTracks().forEach((track) => track.stop());
          return;
        }
        streamRef.current = stream;
        if (videoRef.current) {
          videoRef.current.srcObject = stream;
          await videoRef.current.play().catch(() => undefined);
        }

        const devices = await navigator.mediaDevices.enumerateDevices();
        if (!isOpenRef.current || generation !== previewGenerationRef.current) {
          stream.getTracks().forEach((track) => track.stop());
          return;
        }
        const cameras = devices.filter((device) => device.kind === "videoinput");
        const microphones = deduplicateDefaultDevice(
          devices.filter((device) => device.kind === "audioinput"),
        );
        const speakers = deduplicateDefaultDevice(
          devices.filter((device) => device.kind === "audiooutput"),
          true,
        );
        setChoices({
          cameras,
          microphones,
          speakers,
        });
        const actualCameraId =
          stream.getVideoTracks()[0]?.getSettings().deviceId || "";
        const actualMicrophoneId =
          stream.getAudioTracks()[0]?.getSettings().deviceId || "";
        setPreferences({
          cameraId:
            cameras.some((device) => device.deviceId === actualCameraId)
              ? actualCameraId
              : "",
          microphoneId:
            microphones.some((device) => device.deviceId === actualMicrophoneId)
              ? actualMicrophoneId
              : "",
          speakerId: speakers.some(
            (device) => device.deviceId === nextPreferences.speakerId,
          )
            ? nextPreferences.speakerId
            : speakers.find((device) => device.deviceId === "default")
                ?.deviceId || speakers[0]?.deviceId || "",
        });

        const audioContext = new AudioContext();
        audioContextRef.current = audioContext;
        await audioContext.resume();
        if (!isOpenRef.current || generation !== previewGenerationRef.current) {
          await audioContext.close().catch(() => undefined);
          return;
        }
        if (nextPreferences.speakerId && nextPreferences.speakerId !== "default") {
          await setAudioOutputDevice(audioContext, nextPreferences.speakerId).catch(
            () => setError("This browser cannot select a speaker output."),
          );
        }
        const source = audioContext.createMediaStreamSource(stream);
        const analyser = audioContext.createAnalyser();
        analyser.fftSize = 512;
        source.connect(analyser);
        const samples = new Uint8Array(analyser.fftSize);
        const readLevel = () => {
          if (!isOpenRef.current) return;
          analyser.getByteTimeDomainData(samples);
          let sum = 0;
          for (const sample of samples) {
            const amplitude = (sample - 128) / 128;
            sum += amplitude * amplitude;
          }
          setLevel(Math.min(1, Math.sqrt(sum / samples.length) * 3));
          animationFrameRef.current = requestAnimationFrame(readLevel);
        };
        animationFrameRef.current = requestAnimationFrame(readLevel);
      } catch (cause) {
        if (generation === previewGenerationRef.current) {
          setError(
            cause instanceof Error
              ? cause.message
              : "Unable to access camera or microphone.",
          );
        }
      } finally {
        if (generation === previewGenerationRef.current) setLoading(false);
      }
    },
    [stopPreview],
  );

  useEffect(() => {
    isOpenRef.current = open;
    if (open) {
      setPreferences(getMediaDevicePreferences());
      void startPreview(getMediaDevicePreferences());
    } else {
      stopPreview();
    }
    return () => {
      isOpenRef.current = false;
      stopPreview();
    };
  }, [open, startPreview, stopPreview]);

  if (!open) return null;

  const changeDevice = (key: keyof typeof preferences, value: string) => {
    const nextPreferences = { ...preferences, [key]: value };
    setPreferences(nextPreferences);
    if (key === "speakerId") {
      const audioContext = audioContextRef.current;
      if (audioContext) {
        void setAudioOutputDevice(audioContext, value).catch((cause: unknown) =>
          setError(
            cause instanceof Error
              ? cause.message
              : "Unable to select this speaker.",
          ),
        );
      }
    } else {
      void startPreview(nextPreferences);
    }
  };

  const handleSave = () => {
    saveMediaDevicePreferences(preferences);
    onClose();
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
    oscillator.addEventListener("ended", () => {
      oscillator.disconnect();
      gain.disconnect();
      setTestingSpeakers(false);
    }, { once: true });
    oscillator.start();
    oscillator.stop(audioContext.currentTime + 0.7);
  };

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 p-4 backdrop-blur-sm"
      onMouseDown={(event) => {
        if (event.target === event.currentTarget) onClose();
      }}
      onKeyDown={(event) => {
        if (event.key === "Escape") onClose();
      }}
    >
      <section
        aria-labelledby="device-setup-title"
        aria-modal="true"
        className="w-full max-w-3xl overflow-hidden rounded-lg border border-terminal-8 bg-terminal-bg text-terminal-fg shadow-2xl"
        role="dialog"
      >
        <header className="flex items-start justify-between border-b border-terminal-8 px-6 py-5">
          <div>
            <h2 className="text-xl font-semibold" id="device-setup-title">
              Audio device setup
            </h2>
            <p className="mt-1 text-sm text-terminal-8">
              Choose your camera and microphone, then check your levels.
            </p>
          </div>
          <button
            aria-label="Close device setup"
            className="rounded px-2 text-2xl leading-none text-terminal-8 hover:bg-terminal-8 hover:text-terminal-fg"
            onClick={onClose}
            type="button"
          >
            ×
          </button>
        </header>

        <div className="grid gap-6 p-6 md:grid-cols-2">
          <div>
            <div className="relative aspect-video overflow-hidden rounded border border-terminal-8 bg-black">
              <video
                aria-label="Camera preview"
                autoPlay
                className="h-full w-full object-cover"
                muted
                playsInline
                ref={videoRef}
              />
              {loading && (
                <div className="absolute inset-0 grid place-items-center text-sm text-terminal-8">
                  Starting camera preview…
                </div>
              )}
              {!loading && !streamRef.current?.getVideoTracks().length && (
                <div className="absolute inset-0 grid place-items-center text-sm text-terminal-8">
                  Camera preview unavailable
                </div>
              )}
              <span className="absolute left-3 top-3 rounded bg-black/70 px-2 py-1 text-xs text-terminal-fg">
                Camera preview
              </span>
            </div>
            <p className="mt-2 text-xs text-terminal-8">
              Preview stays local to this browser.
            </p>
          </div>

          <div className="space-y-5">
            <label className="block text-sm">
              <span className="mb-2 block font-medium">Camera</span>
              <select
                className="w-full rounded border border-terminal-8 bg-terminal-bg px-3 py-2 text-terminal-fg focus:border-terminal-4 focus:outline-none"
                disabled={loading || choices.cameras.length === 0}
                onChange={(event) =>
                  changeDevice("cameraId", event.currentTarget.value)
                }
                value={preferences.cameraId}
              >
                {choices.cameras.map((camera, index) => (
                  <option key={camera.deviceId} value={camera.deviceId}>
                    {camera.label || `Camera ${index + 1}`}
                  </option>
                ))}
              </select>
            </label>

            <label className="block text-sm">
              <span className="mb-2 block font-medium">Speakers</span>
              <select
                className="w-full rounded border border-terminal-8 bg-terminal-bg px-3 py-2 text-terminal-fg focus:border-terminal-4 focus:outline-none"
                disabled={loading || choices.speakers.length === 0}
                onChange={(event) =>
                  changeDevice("speakerId", event.currentTarget.value)
                }
                value={preferences.speakerId}
              >
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
              className="rounded border border-terminal-8 px-3 py-1.5 text-xs hover:bg-terminal-8 disabled:opacity-50"
              disabled={loading || choices.speakers.length === 0 || testingSpeakers}
              onClick={() =>
                void playSpeakerTest().catch((cause: unknown) => {
                  setTestingSpeakers(false);
                  setError(
                    cause instanceof Error ? cause.message : "Speaker test failed.",
                  );
                })
              }
              type="button"
            >
              {testingSpeakers ? "Playing test tone…" : "Test speakers"}
            </button>

            <label className="block text-sm">
              <span className="mb-2 block font-medium">Microphone</span>
              <select
                className="w-full rounded border border-terminal-8 bg-terminal-bg px-3 py-2 text-terminal-fg focus:border-terminal-4 focus:outline-none"
                disabled={loading || choices.microphones.length === 0}
                onChange={(event) =>
                  changeDevice("microphoneId", event.currentTarget.value)
                }
                value={preferences.microphoneId}
              >
                {choices.microphones.map((microphone, index) => (
                  <option key={microphone.deviceId} value={microphone.deviceId}>
                    {microphone.label || `Microphone ${index + 1}`}
                  </option>
                ))}
              </select>
            </label>

            <div>
              <div className="mb-2 flex items-center justify-between">
                <span className="text-sm font-medium">Microphone level</span>
                <button
                  className="rounded border border-terminal-8 px-3 py-1.5 text-xs hover:bg-terminal-8"
                  onClick={() => setTestingMicrophone((testing) => !testing)}
                  type="button"
                >
                  {testingMicrophone ? "Stop test" : "Test microphone"}
                </button>
              </div>
              <div
                aria-label={`Microphone input level ${Math.round(level * 100)} percent`}
                className="h-3 overflow-hidden rounded bg-terminal-8"
                role="meter"
                aria-valuemin={0}
                aria-valuemax={100}
                aria-valuenow={Math.round(level * 100)}
              >
                <div
                  className="h-full bg-terminal-2 transition-[width] duration-100"
                  style={{ width: `${testingMicrophone ? level * 100 : 0}%` }}
                />
              </div>
              <p className="mt-2 text-xs text-terminal-8">
                {testingMicrophone
                  ? "Speak to see your input level."
                  : "Start the test and speak to check your mic."}
              </p>
            </div>
          </div>
        </div>

        {error && (
          <p className="mx-6 mb-4 rounded border border-terminal-1/50 bg-terminal-1/10 px-3 py-2 text-sm text-terminal-1">
            {error}
          </p>
        )}

        <footer className="flex justify-end gap-2 border-t border-terminal-8 px-6 py-4">
          <button
            className="rounded border border-terminal-8 px-4 py-2 text-sm hover:bg-terminal-8"
            onClick={onClose}
            type="button"
          >
            Cancel
          </button>
          <button
            className="rounded bg-terminal-2 px-4 py-2 text-sm font-medium text-terminal-bg hover:bg-terminal-10"
            onClick={handleSave}
            type="button"
          >
            Done
          </button>
        </footer>
      </section>
    </div>
  );
}
