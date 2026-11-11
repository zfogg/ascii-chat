import {
  AUDIO_CHANNELS,
  OPUS_FRAME_SAMPLES,
  OPUS_FRAME_MS,
  OPUS_BATCH_HEADER_SIZE,
  OPUS_SINGLE_FRAME_HEADER_SIZE,
} from "./limits";
import { OpusEncoder } from "./OpusEncoder";
import {
  createWasmModule,
  type AudioProcessorBinding,
  type ClientWasmBindings,
} from "@ascii-chat/shared/wasm";

const send = (message: unknown, transfer: Transferable[] = []) =>
  (self as unknown as Worker).postMessage(message, transfer);

type DspModule = Parameters<OpusEncoder["init"]>[0] & ClientWasmBindings;

self.onmessage = async ({
  data,
}: MessageEvent<{
  port: MessagePort;
  bypass: boolean;
  sampleRate: number;
  encoderChannels: 1 | 2;
  bitrate: number;
}>) => {
  const { port, bypass, sampleRate, encoderChannels, bitrate } = data;
  let module: DspModule | undefined;
  let processor: AudioProcessorBinding | null = null;
  const codec = new OpusEncoder({
    sampleRate,
    channelCount: encoderChannels,
    bitrate,
  });
  const fail = (error: unknown) => {
    port.onmessage = null;
    codec.cleanup();
    processor?.delete();
    processor = null;
    send({ error: error instanceof Error ? error.message : String(error) });
  };
  try {
    module = await createWasmModule<DspModule>("client");
    if (!module) throw new Error("Audio WASM did not initialize");
    processor = new module.AudioProcessorBinding(
      sampleRate,
      bypass,
      OPUS_FRAME_SAMPLES * AUDIO_CHANNELS,
    );
    if (!processor.valid())
      throw new Error("Cannot create shared audio processor");
    await codec.init(module);
    const pcm = new Int16Array(OPUS_FRAME_SAMPLES * encoderChannels);
    port.onmessage = ({
      data: frame,
    }: MessageEvent<{ microphone: Float32Array; media: Float32Array }>) => {
      try {
        const input = frame.microphone;
        if (
          input.length !== OPUS_FRAME_SAMPLES * AUDIO_CHANNELS ||
          frame.media.length !== OPUS_FRAME_SAMPLES * AUDIO_CHANNELS
        )
          throw new Error("Invalid microphone frame size");
        const samples = new Float32Array(input.length);
        const result = processor!.process(input, samples);
        if (result !== 0)
          throw new Error(`Audio enhancement failed (${result})`);
        for (let i = 0; i < samples.length; ++i)
          samples[i] = Math.max(-1, Math.min(1, samples[i]! + frame.media[i]!));
        for (let i = 0; i < pcm.length; ++i) {
          const sample =
            encoderChannels === 2
              ? samples[i]!
              : (samples[i * 2]! + samples[i * 2 + 1]!) * 0.5;
          pcm[i] = Math.round(sample * (sample < 0 ? 32768 : 32767));
        }
        const opus = codec.encode(pcm);
        const payload = new Uint8Array(
          OPUS_SINGLE_FRAME_HEADER_SIZE + opus.length,
        );
        const view = new DataView(payload.buffer);
        view.setUint32(0, sampleRate, false);
        view.setUint32(4, OPUS_FRAME_MS, false);
        view.setUint32(8, 1, false);
        view.setUint16(OPUS_BATCH_HEADER_SIZE, opus.length, false);
        payload.set(opus, OPUS_SINGLE_FRAME_HEADER_SIZE);
        send({ payload, samples }, [payload.buffer, samples.buffer]);
        port.postMessage(frame, [frame.microphone.buffer, frame.media.buffer]);
      } catch (error) {
        fail(error);
      }
    };
    port.start();
    send({ ready: true });
  } catch (error) {
    fail(error);
  }
};
