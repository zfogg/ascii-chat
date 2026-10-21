import { applyAudioOutputDevice } from "@ascii-chat/shared/devices/mediaDevicePreferences";
import { AVState } from "../wasm/AVState";
import { OpusEncoder } from "./OpusEncoder";
import { AudioCapture } from "./AudioCapture";
import { getClientModule, PacketType } from "../wasm/client";
import {
  createTestPatternAudioSource,
  isTestMode,
  type TestPatternAudioSource,
} from "@ascii-chat/shared";
import { getMediaDevicePreferences } from "../utils/mediaDevicePreferences";

export interface AudioPipelineOptions {
  /** Local capture/visualization only; never decode or monitor captured audio. */
  localOnly?: boolean;
  onError?: (error: Error) => void;
  onAudioData?: (payload: Uint8Array) => void;
  /** Receives mixed display/microphone PCM while the capture buffer is valid. */
  onMicrophoneSamples?: (samples: Float32Array) => void;
  onPlaybackSamples?: (samples: Float32Array) => void;
  onLevels?: (levels: {
    microphone: number;
    playback: number;
    sent: number;
    played: number;
    playedSamples: number;
    underruns: number;
  }) => void;
}

/** Mono 48 kHz audio using the native ACIP Opus batch format. */
export class AudioPipeline {
  private context: AudioContext | null = null;
  private stream: MediaStream | null = null;
  private source: MediaStreamAudioSourceNode | null = null;
  private processor: AudioCapture | null = null;
  private processorInit: Promise<void> | null = null;
  private processorGeneration = 0;
  private displayStream: MediaStream | null = null;
  private displaySource: MediaStreamAudioSourceNode | null = null;
  private displayGeneration = 0;
  private outputGain: GainNode | null = null;
  private volume = 1;
  private playbackEnabled = false;
  private recording: MediaStreamAudioDestinationNode | null = null;
  private recordingMediaCleanup: (() => void) | null = null;

  /** Replace the file-audio tap independently of element mute and speaker volume. */
  setRecordingMedia(element: HTMLMediaElement | null): void {
    this.recordingMediaCleanup?.();
    this.recordingMediaCleanup = null;
    if (!element || !this.recording || !this.context) return;
    const capture = element as HTMLMediaElement & {
      captureStream?: () => MediaStream;
      mozCaptureStream?: () => MediaStream;
    };
    const captureStream = capture.captureStream ?? capture.mozCaptureStream;
    if (!captureStream)
      throw new Error(
        "This browser cannot record file audio. Use a camera or screen source.",
      );
    // Element capture is independent of the element's local volume/mute state.
    // Never route it back into the network or speaker graph.
    const stream = captureStream.call(element);
    let source: MediaStreamAudioSourceNode | null = null;
    const update = () => {
      source?.disconnect();
      source = null;
      if (!this.context || !this.recording) return;
      const tracks = stream
        .getAudioTracks()
        .filter((track) => track.readyState === "live");
      if (tracks.length) {
        source = this.context.createMediaStreamSource(new MediaStream(tracks));
        source.connect(this.recording);
      }
    };
    stream.addEventListener("addtrack", update);
    stream.addEventListener("removetrack", update);
    this.recordingMediaCleanup = () => {
      stream.removeEventListener("addtrack", update);
      stream.removeEventListener("removetrack", update);
      source?.disconnect();
      stream.getTracks().forEach((track) => track.stop());
    };
    update();
  }

  /** A separate output bus: local inputs never connect to the loudspeakers. */
  async startRecording(): Promise<MediaStream> {
    if (this.closed) throw new Error("Audio pipeline is closed");
    await this.initializeAudio();
    if (this.closed || !this.context)
      throw new Error("Audio pipeline is closed");
    if (!this.recording) {
      this.recording = this.context.createMediaStreamDestination();
      this.source?.connect(this.recording);
      this.displaySource?.connect(this.recording);
      this.playback?.connect(this.recording);
      for (const source of this.sources) source.connect(this.recording);
    }
    return this.recording.stream;
  }

  /** Detach recording taps while preserving microphone capture and monitoring. */
  stopRecording(): void {
    this.recordingMediaCleanup?.();
    this.recordingMediaCleanup = null;
    const bus = this.recording;
    if (!bus) return;
    this.recording = null;
    this.source?.disconnect(bus);
    this.displaySource?.disconnect(bus);
    this.playback?.disconnect(bus);
    for (const source of this.sources) source.disconnect(bus);
    bus.stream.getTracks().forEach((track) => track.stop());
    bus.disconnect();
    if (!this.playbackEnabled) this.disablePlayback();
  }

  setVolume(value: number): void {
    this.volume = Math.min(1, Math.max(0, value));
    if (this.outputGain)
      this.outputGain.gain.value = this.playbackEnabled ? this.volume : 0;
  }
  private codec: OpusEncoder | null = null;
  private playback: AudioWorkletNode | null = null;
  private playbackInit: Promise<void> | null = null;
  private playAt = 0;
  private sources = new Set<AudioBufferSourceNode>();
  private avState = new AVState();
  private starting: Promise<void> | null = null;
  private closed = false;
  private microphoneChangeGeneration = 0;
  private speakerChangeQueue: Promise<void> = Promise.resolve();
  private sent = 0;
  private played = 0;
  private playedSamples = 0;
  private underruns = 0;
  private microphoneLevel = 0;
  private playbackLevel = 0;
  private lastLevelUpdate = 0;
  private testPatternSource: TestPatternAudioSource | null = null;

  private reportLevels(): void {
    const now = performance.now();
    if (now - this.lastLevelUpdate < 500) return;
    this.lastLevelUpdate = now;
    this.options.onLevels?.({
      microphone: this.microphoneLevel,
      playback: this.playbackLevel,
      sent: this.sent,
      played: this.played,
      playedSamples: this.playedSamples,
      underruns: this.underruns,
    });
  }

  constructor(private options: AudioPipelineOptions = {}) {}
  async enablePlayback(): Promise<void> {
    if (this.closed) throw new Error("Audio pipeline is closed");
    await this.initializeAudio();
    this.playbackEnabled = true;
    this.setVolume(this.volume);
  }
  disablePlayback(): void {
    this.playbackEnabled = false;
    this.setVolume(this.volume);
    if (this.recording) return;
    for (const source of this.sources) source.stop();
    this.sources.clear();
    this.playback?.port.postMessage({ reset: true });
    this.playAt = 0;
    this.playbackLevel = 0;
  }
  private async initializeAudio(): Promise<void> {
    if (!this.context) this.context = new AudioContext({ sampleRate: 48000 });
    if (!this.outputGain) {
      this.outputGain = this.context.createGain();
      this.outputGain.gain.value = this.playbackEnabled ? this.volume : 0;
      this.outputGain.connect(this.context.destination);
    }
    const playbackContext = this.context;
    await playbackContext.resume();
    if (this.context !== playbackContext) return;
    const speakerId = getMediaDevicePreferences().speakerId;
    if (speakerId && speakerId !== "default") {
      await this.setSpeakerDevice(speakerId).catch((error: unknown) =>
        console.warn("[Audio] Unable to select the saved speaker:", error),
      );
    }
    if (this.context.audioWorklet && !this.playbackInit) {
      const context = this.context;
      this.playbackInit = context.audioWorklet
        .addModule(new URL("./playback-worklet.js", import.meta.url))
        .then(() => {
          if (this.context !== context) return;
          this.playback = new AudioWorkletNode(
            context,
            "ascii-chat-pcm-playback",
            {
              numberOfInputs: 0,
              numberOfOutputs: 1,
              outputChannelCount: [2],
            },
          );
          this.playback.port.onmessage = ({ data }) => {
            this.underruns = data.underruns;
            this.reportLevels();
          };
          this.playback.connect(this.outputGain!);
        });
    }
    await this.playbackInit;
    if (this.context !== playbackContext) return;
    if (!this.codec && !this.options.localOnly) {
      const module = getClientModule();
      if (!module) throw new Error("Connect before enabling audio");
      this.codec = new OpusEncoder({ sampleRate: 48000, channelCount: 1 });
      await this.codec.init(module);
    }
  }
  get captureActive(): boolean {
    return this.avState.active;
  }

  startCapture(): Promise<void> {
    if (this.closed)
      return Promise.reject(new Error("Audio pipeline is closed"));
    if (this.starting && this.avState.current(this.avState.generation))
      return this.starting;
    const previous = this.starting;
    let generation: number;
    try {
      generation = this.avState.begin();
    } catch (error) {
      return Promise.reject(error);
    }
    if (!generation) return Promise.resolve();
    const pending = (
      previous ? previous.catch(() => undefined) : Promise.resolve()
    )
      .then(() => {
        if (this.avState.current(generation))
          return this.startCaptureHost(generation);
        return undefined;
      })
      .catch((error: unknown) => {
        if (this.avState.current(generation)) this.stopCapture();
        throw error;
      })
      .finally(() => {
        if (this.starting !== pending) return;
        this.starting = null;
        if (this.closed) this.avState.dispose();
      });
    this.starting = pending;
    return pending;
  }

  private async startCaptureHost(generation: number): Promise<void> {
    await this.initializeAudio();
    if (!this.avState.current(generation)) return;

    const microphoneId = getMediaDevicePreferences().microphoneId;
    const stream = isTestMode()
      ? (() => {
          this.testPatternSource = createTestPatternAudioSource(this.context!);
          return this.testPatternSource.stream;
        })()
      : await (async () => {
          const audio: MediaTrackConstraints = {
            channelCount: 1,
            sampleRate: 48000,
            echoCancellation: true,
            noiseSuppression: false,
            autoGainControl: false,
            ...(microphoneId ? { deviceId: { exact: microphoneId } } : {}),
          };
          try {
            return await navigator.mediaDevices.getUserMedia({
              audio,
              video: false,
            });
          } catch (error) {
            if (!microphoneId) throw error;
            console.warn(
              "[Audio] Saved microphone is unavailable; using browser default",
              error,
            );
            return navigator.mediaDevices.getUserMedia({
              audio: {
                channelCount: 1,
                sampleRate: 48000,
                echoCancellation: true,
                noiseSuppression: false,
                autoGainControl: false,
              },
              video: false,
            });
          }
        })();
    if (!this.avState.current(generation) || !this.context) {
      stream.getTracks().forEach((track) => track.stop());
      this.testPatternSource?.stop();
      this.testPatternSource = null;
      return;
    }
    this.stream = stream;
    this.source = this.context.createMediaStreamSource(stream);
    if (this.recording) this.source.connect(this.recording);
    // A display transition may retire an in-flight processor. Rejoin the
    // current graph instead of treating that transition as microphone failure.
    do {
      await this.ensureCaptureGraph();
      if (!this.avState.current(generation) || this.closed) return;
    } while (!this.processor);
    this.source!.connect(this.processor.node);
    this.avState.complete(generation);
  }

  private ensureCaptureGraph(): Promise<void> {
    if (this.processor) return Promise.resolve();
    if (this.processorInit) return this.processorInit;
    const generation = ++this.processorGeneration;
    const pending = AudioCapture.create(this.context!, {
      bypass: isTestMode() || !!this.displayStream,
      onFrame: (payload, input) => {
        if (generation !== this.processorGeneration) return;
        this.options.onMicrophoneSamples?.(input);
        this.microphoneLevel = Math.sqrt(
          input.reduce((sum, value) => sum + value * value, 0) / input.length,
        );
        this.options.onAudioData?.(payload);
        this.sent++;
        this.reportLevels();
      },
      onError: (error) => {
        if (generation !== this.processorGeneration) return;
        this.stopCapture();
        void this.setDisplayStream(null);
        console.error("[Audio] Capture processing failed", error);
        this.options.onError?.(error);
      },
    })
      .then((processor) => {
        if (generation !== this.processorGeneration || this.closed)
          processor.close();
        else this.processor = processor;
      })
      .finally(() => {
        if (this.processorInit === pending) this.processorInit = null;
      });
    this.processorInit = pending;
    return pending;
  }

  private releaseCaptureGraph(): void {
    ++this.processorGeneration;
    this.processor?.close();
    this.processor = null;
    this.processorInit = null;
  }

  /** Mix display audio and microphone into one ordered Opus stream, without monitoring either input. */
  async setDisplayStream(stream: MediaStream | null): Promise<void> {
    if (this.closed) {
      if (stream) throw new Error("Audio pipeline is closed");
      return;
    }
    const generation = ++this.displayGeneration;
    const hadDisplayAudio = this.displayStream !== null;
    this.displaySource?.disconnect();
    this.displaySource = null;
    this.displayStream = stream
      ?.getAudioTracks()
      .some((track) => track.readyState === "live")
      ? stream
      : null;
    if (!this.displayStream) {
      if (!this.source) {
        if (!this.starting) this.releaseCaptureGraph();
        return;
      }
      if (!hadDisplayAudio || isTestMode()) return;
      // Restore microphone-only enhancement after the display/music mix ends.
      this.source.disconnect();
      if (this.recording) this.source.connect(this.recording);
      this.releaseCaptureGraph();
      await this.ensureCaptureGraph();
      if (
        generation !== this.displayGeneration ||
        this.closed ||
        !this.processor
      )
        return;
      this.source?.connect(this.processor.node);
      return;
    }
    await this.initializeAudio();
    if (generation !== this.displayGeneration || this.closed || !this.context)
      return;
    // Screen/music samples must bypass microphone-only enhancement. Reconnect
    // the retained microphone into that same processor instead of duplicating it.
    this.source?.disconnect();
    if (this.recording) this.source?.connect(this.recording);
    this.releaseCaptureGraph();
    await this.ensureCaptureGraph();
    if (generation !== this.displayGeneration || this.closed || !this.processor)
      return;
    this.source?.connect(this.processor.node);
    this.displaySource = this.context.createMediaStreamSource(
      new MediaStream(this.displayStream!.getAudioTracks()),
    );
    this.displaySource.connect(this.processor.node);
    if (this.recording) this.displaySource.connect(this.recording);
  }
  async replaceMicrophone(deviceId: string): Promise<void> {
    if (
      !this.stream ||
      !this.processor ||
      !this.context ||
      this.testPatternSource
    )
      return;

    const generation = this.avState.generation;
    const requestGeneration = ++this.microphoneChangeGeneration;
    const audio: MediaTrackConstraints = {
      channelCount: 1,
      sampleRate: 48000,
      echoCancellation: true,
      noiseSuppression: false,
      autoGainControl: false,
      ...(deviceId ? { deviceId: { exact: deviceId } } : {}),
    };
    let nextStream: MediaStream;
    try {
      nextStream = await navigator.mediaDevices.getUserMedia({
        audio,
        video: false,
      });
    } catch (error) {
      if (!deviceId) throw error;
      console.warn(
        "[Audio] Selected microphone is unavailable; using browser default",
        error,
      );
      nextStream = await navigator.mediaDevices.getUserMedia({
        audio: {
          channelCount: 1,
          sampleRate: 48000,
          echoCancellation: true,
          noiseSuppression: false,
          autoGainControl: false,
        },
        video: false,
      });
    }
    if (
      !this.avState.current(generation) ||
      requestGeneration !== this.microphoneChangeGeneration ||
      !this.processor ||
      !this.context
    ) {
      nextStream.getTracks().forEach((track) => track.stop());
      return;
    }

    const nextSource = this.context.createMediaStreamSource(nextStream);
    nextSource.connect(this.processor.node);
    if (this.recording) nextSource.connect(this.recording);
    const previousSource = this.source;
    const previousStream = this.stream;
    this.source = nextSource;
    this.stream = nextStream;
    previousSource?.disconnect();
    previousStream.getTracks().forEach((track) => track.stop());
  }
  async setSpeakerDevice(deviceId: string): Promise<void> {
    if (!this.context || !deviceId) return;
    const context = this.context;
    const operation = this.speakerChangeQueue.then(async () => {
      if (this.context !== context) return;
      await applyAudioOutputDevice(context, deviceId);
    });
    this.speakerChangeQueue = operation.catch(() => undefined);
    await operation;
  }
  playPacket(type: number, payload: Uint8Array): void {
    if (
      (!this.playbackEnabled && !this.recording) ||
      !this.context ||
      !this.codec ||
      this.context.state !== "running"
    )
      return;
    const view = new DataView(
      payload.buffer,
      payload.byteOffset,
      payload.byteLength,
    );
    if (payload.length < 16) throw new Error("Truncated audio packet");
    if (type === PacketType.AUDIO_OPUS_BATCH) {
      const sampleRate = view.getUint32(0, false),
        duration = view.getUint32(4, false),
        count = view.getUint32(8, false);
      if (
        sampleRate !== 48000 ||
        duration < 1 ||
        duration > 120 ||
        count > 1000 ||
        payload.length < 16 + count * 2
      )
        throw new Error("Invalid Opus batch header");
      let offset = 16 + count * 2;
      for (let i = 0; i < count; i++) {
        const size = view.getUint16(16 + i * 2, false);
        if (!size || offset + size > payload.length)
          throw new Error("Truncated Opus frame");
        const pcm = this.codec.decode(
          payload.slice(offset, offset + size),
          5760,
        );
        this.schedule(
          Float32Array.from(pcm, (sample) => sample / 32768),
          48000,
          1,
        );
        offset += size;
      }
      if (offset !== payload.length)
        throw new Error("Invalid Opus batch length");
    } else if (type === PacketType.AUDIO_BATCH) {
      // Native audio_batch_packet_t and float samples are sent in host byte order.
      const count = view.getUint32(4, true),
        sampleRate = view.getUint32(8, true),
        channels = view.getUint32(12, true);
      if (
        ![1, 2].includes(channels) ||
        sampleRate < 8000 ||
        sampleRate > 192000 ||
        count % channels ||
        16 + count * 4 !== payload.length
      )
        throw new Error("Invalid PCM audio batch");
      const samples = new Float32Array(count);
      for (let i = 0; i < count; i++)
        samples[i] = view.getFloat32(16 + i * 4, true);
      this.schedule(samples, sampleRate, channels);
    }
  }
  private schedule(
    samples: Float32Array,
    sampleRate: number,
    channels: number,
  ): void {
    const context = this.context!;
    this.options.onPlaybackSamples?.(samples);
    if (this.playback && sampleRate === context.sampleRate) {
      this.playbackLevel = Math.sqrt(
        samples.reduce((sum, value) => sum + value * value, 0) / samples.length,
      );
      this.played++;
      this.playedSamples += samples.length / channels;
      this.playback.port.postMessage({ samples, channels, rate: sampleRate }, [
        samples.buffer,
      ]);
      this.reportLevels();
      return;
    }
    if (this.playAt > context.currentTime + 0.3) return;
    const buffer = context.createBuffer(
      channels,
      samples.length / channels,
      sampleRate,
    );
    for (let channel = 0; channel < channels; channel++) {
      const output = buffer.getChannelData(channel);
      for (let i = 0; i < output.length; i++)
        output[i] = samples[i * channels + channel]!;
    }
    const source = context.createBufferSource();
    source.buffer = buffer;
    source.connect(this.outputGain!);
    if (this.recording) source.connect(this.recording);
    this.sources.add(source);
    source.onended = () => {
      this.sources.delete(source);
      source.disconnect();
    };
    if (this.playAt > 0 && this.playAt < context.currentTime) this.underruns++;
    // Buffer two Opus frames at startup/recovery; steady playback preserves
    // adjacent sample deadlines instead of adding a gap to every packet.
    if (this.playAt < context.currentTime + 0.005)
      this.playAt = context.currentTime + 0.04;
    source.start(this.playAt);
    this.playAt += buffer.duration;
    this.played++;
    this.playedSamples += samples.length / channels;
    this.playbackLevel = Math.sqrt(
      samples.reduce((sum, value) => sum + value * value, 0) / samples.length,
    );
    this.reportLevels();
  }
  stopCapture(): void {
    this.avState.stop(() => {
      this.microphoneChangeGeneration++;
      this.source?.disconnect();
      this.stream?.getTracks().forEach((track) => track.stop());
      this.testPatternSource?.stop();
      this.testPatternSource = null;
      this.source = null;
      this.stream = null;
      if (!this.displaySource) this.releaseCaptureGraph();
    });
  }
  close(): void {
    this.stopRecording();
    this.closed = true;
    this.playbackEnabled = false;

    ++this.displayGeneration;
    this.displaySource?.disconnect();
    this.displaySource = null;
    this.displayStream = null;
    this.releaseCaptureGraph();

    this.stopCapture();
    if (!this.starting) this.avState.dispose();
    for (const source of this.sources) source.stop();
    this.sources.clear();
    this.playback?.disconnect();
    this.playback?.port.close();
    this.playback = null;
    this.outputGain?.disconnect();
    this.outputGain = null;
    this.playbackInit = null;
    this.codec?.cleanup();
    this.codec = null;
    void this.context?.close();
    this.context = null;
    this.playAt = 0;
  }
}
