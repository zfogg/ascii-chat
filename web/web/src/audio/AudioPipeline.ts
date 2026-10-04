import { OpusEncoder } from "./OpusEncoder";
import { getClientModule, PacketType } from "../wasm/client";

export interface AudioPipelineOptions {
  onAudioData?: (payload: Uint8Array) => void;
}

/** Mono 48 kHz audio using the native ACIP Opus batch format. */
export class AudioPipeline {
  private context: AudioContext | null = null;
  private stream: MediaStream | null = null;
  private source: MediaStreamAudioSourceNode | null = null;
  private processor: ScriptProcessorNode | null = null;
  private gain: GainNode | null = null;
  private codec: OpusEncoder | null = null;
  private pending: number[] = [];
  private playAt = 0;
  private sources = new Set<AudioBufferSourceNode>();
  private generation = 0;

  constructor(private options: AudioPipelineOptions = {}) {}
  async enablePlayback(): Promise<void> {
    if (!this.context) this.context = new AudioContext({ sampleRate: 48000 });
    await this.context.resume();
    if (!this.codec) {
      const module = getClientModule();
      if (!module) throw new Error("Connect before enabling audio");
      this.codec = new OpusEncoder({ sampleRate: 48000, channelCount: 1 });
      await this.codec.init(module);
    }
  }
  async startCapture(): Promise<void> {
    const generation = ++this.generation;
    await this.enablePlayback();
    const stream = await navigator.mediaDevices.getUserMedia({
      audio: {
        channelCount: 1,
        sampleRate: 48000,
        echoCancellation: true,
        noiseSuppression: true,
      },
      video: false,
    });
    if (generation !== this.generation || !this.context) {
      stream.getTracks().forEach((track) => track.stop());
      return;
    }
    this.stream = stream;
    this.source = this.context.createMediaStreamSource(stream);
    this.processor = this.context.createScriptProcessor(2048, 1, 1);
    this.gain = this.context.createGain();
    this.gain.gain.value = 0;
    this.processor.onaudioprocess = (event) => {
      for (const sample of event.inputBuffer.getChannelData(0))
        this.pending.push(sample);
      while (this.pending.length >= 960 && this.codec) {
        const frame = this.pending.splice(0, 960);
        const pcm = Int16Array.from(frame, (sample) =>
          Math.round(
            Math.max(-1, Math.min(1, sample)) * (sample < 0 ? 32768 : 32767),
          ),
        );
        const opus = this.codec.encode(pcm);
        const payload = new Uint8Array(18 + opus.length);
        const view = new DataView(payload.buffer);
        view.setUint32(0, 48000, false);
        view.setUint32(4, 20, false);
        view.setUint32(8, 1, false);
        view.setUint16(16, opus.length, false);
        payload.set(opus, 18);
        this.options.onAudioData?.(payload);
      }
    };
    this.source.connect(this.processor);
    this.processor.connect(this.gain);
    this.gain.connect(this.context.destination);
  }
  playPacket(type: number, payload: Uint8Array): void {
    if (!this.context || !this.codec || this.context.state !== "running")
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
    source.connect(context.destination);
    this.sources.add(source);
    source.onended = () => {
      this.sources.delete(source);
      source.disconnect();
    };
    this.playAt = Math.max(context.currentTime + 0.01, this.playAt);
    source.start(this.playAt);
    this.playAt += buffer.duration;
  }
  stopCapture(): void {
    this.generation++;
    if (this.processor) this.processor.onaudioprocess = null;
    this.processor?.disconnect();
    this.source?.disconnect();
    this.gain?.disconnect();
    this.stream?.getTracks().forEach((track) => track.stop());
    this.processor = null;
    this.source = null;
    this.gain = null;
    this.stream = null;
    this.pending = [];
  }
  close(): void {
    this.stopCapture();
    for (const source of this.sources) source.stop();
    this.sources.clear();
    this.codec?.cleanup();
    this.codec = null;
    void this.context?.close();
    this.context = null;
    this.playAt = 0;
  }
}
