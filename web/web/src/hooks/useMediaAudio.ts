import {
  useCallback,
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
} from "react";
import {
  AudioPipeline,
  type AudioPipelineOptions,
} from "../audio/AudioPipeline";
import type { AudioLevels } from "../components/MediaAudioControls";

/** Own the shared controls, input graph, playback gate and late-start cleanup. */
export function useMediaAudio(options: () => AudioPipelineOptions) {
  const callbacks = useRef(options);
  useLayoutEffect(() => {
    callbacks.current = options;
  });
  const audioRef = useRef<AudioPipeline | null>(null);
  const [audioEnabled, setAudioEnabled] = useState(false);
  const [micEnabled, setMicEnabled] = useState(false);
  const micEnabledRef = useRef(false);
  const mounted = useRef(true);
  const microphoneRequest = useRef(0);
  const [audioLevels, setAudioLevels] = useState<AudioLevels>({
    microphone: 0,
    playback: 0,
    sent: 0,
    played: 0,
    playedSamples: 0,
    underruns: 0,
  });
  useLayoutEffect(() => {
    micEnabledRef.current = micEnabled;
  }, [micEnabled]);
  const ensureAudio = useCallback(() => {
    return (audioRef.current ??= new AudioPipeline({
      ...callbacks.current(),
      onLevels: (levels) => {
        if (mounted.current) setAudioLevels(levels);
      },
      onError: (error) => {
        micEnabledRef.current = false;
        if (mounted.current) setMicEnabled(false);
        callbacks.current().onError?.(error);
      },
      onMicrophoneSamples: (samples) =>
        callbacks.current().onMicrophoneSamples?.(samples),
      onPlaybackSamples: (samples) =>
        callbacks.current().onPlaybackSamples?.(samples),
      onAudioData: (payload) => callbacks.current().onAudioData?.(payload),
    }));
  }, []);
  const closeAudio = useCallback(() => {
    microphoneRequest.current++;
    audioRef.current?.close();
    audioRef.current = null;
    micEnabledRef.current = false;
    if (mounted.current) {
      setAudioEnabled(false);
      setMicEnabled(false);
    }
  }, []);
  const clearDisplayAudio = useCallback(() => {
    void audioRef.current?.setDisplayStream(null);
  }, []);
  const toggleMicrophone = useCallback(
    async (prepare?: () => Promise<boolean>) => {
      const request = ++microphoneRequest.current;
      try {
        if (micEnabledRef.current) {
          audioRef.current?.stopCapture();
          micEnabledRef.current = false;
          setMicEnabled(false);
          return;
        }
        if (prepare && !(await prepare())) return;
        if (!mounted.current || request !== microphoneRequest.current) return;
        const pipeline = ensureAudio();
        await pipeline.startCapture();
        if (
          mounted.current &&
          audioRef.current === pipeline &&
          pipeline.captureActive
        ) {
          micEnabledRef.current = true;
          setMicEnabled(true);
        }
      } catch (cause) {
        callbacks
          .current()
          .onError?.(cause instanceof Error ? cause : new Error(String(cause)));
      }
    },
    [ensureAudio],
  );
  const toggleSpeakers = useCallback(async () => {
    try {
      const pipeline = ensureAudio();
      if (audioEnabled) pipeline.disablePlayback();
      else await pipeline.enablePlayback();
      if (mounted.current && audioRef.current === pipeline)
        setAudioEnabled(!audioEnabled);
    } catch (cause) {
      callbacks
        .current()
        .onError?.(cause instanceof Error ? cause : new Error(String(cause)));
    }
  }, [audioEnabled, ensureAudio]);
  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
      closeAudio();
    };
  }, [closeAudio]);
  return {
    audioRef,
    ensureAudio,
    closeAudio,
    clearDisplayAudio,
    audioEnabled,
    setAudioEnabled,
    micEnabled,
    setMicEnabled,
    micEnabledRef,
    audioLevels,
    toggleMicrophone,
    toggleSpeakers,
  };
}
