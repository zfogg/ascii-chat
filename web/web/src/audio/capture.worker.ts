import { OpusEncoder } from "./OpusEncoder";
import { createWasmModule } from "@ascii-chat/shared/wasm";

const send = (message: unknown, transfer: Transferable[] = []) =>
  (self as unknown as Worker).postMessage(message, transfer);

type DspModule = Parameters<OpusEncoder["init"]>[0] & {
  HEAPF32: Float32Array;
  _wasm_audio_processor_create_stereo(rate: number, bypass: boolean): number;
  _wasm_audio_processor_process(
    handle: number,
    samples: number,
    count: number,
  ): number;
  _wasm_audio_processor_destroy(handle: number): void;
};

self.onmessage = async ({
  data,
}: MessageEvent<{
  port: MessagePort;
  bypass: boolean;
  sampleRate: number;
  encoderChannels: 1 | 2;
}>) => {
  const { port, bypass, sampleRate, encoderChannels } = data;
  let module: DspModule | undefined;
  let handle = 0;
  let scratch = 0;
  const codec = new OpusEncoder({
    sampleRate,
    channelCount: encoderChannels,
    bitrate: encoderChannels === 2 ? 128000 : 32000,
  });
  const fail = (error: unknown) => {
    port.onmessage = null;
    codec.cleanup();
    if (module) {
      if (handle) module._wasm_audio_processor_destroy(handle);
      if (scratch) module._free(scratch);
    }
    handle = scratch = 0;
    send({ error: error instanceof Error ? error.message : String(error) });
  };
  try {
    module = await createWasmModule<DspModule>("client");
    if (!module) throw new Error("Audio WASM did not initialize");
    handle = module._wasm_audio_processor_create_stereo(sampleRate, bypass);
    if (!handle) throw new Error("Cannot create shared audio processor");
    scratch = module._malloc(1920 * Float32Array.BYTES_PER_ELEMENT);
    if (!scratch) throw new Error("Cannot allocate audio processing buffer");
    await codec.init(module);
    const pcm = new Int16Array(960 * encoderChannels);
    port.onmessage = ({
      data: frame,
    }: MessageEvent<{ microphone: Float32Array; media: Float32Array }>) => {
      try {
        const input = frame.microphone;
        if (input.length !== 1920 || frame.media.length !== 1920)
          throw new Error("Invalid microphone frame size");
        module!.HEAPF32.set(input, scratch >> 2);
        const result = module!._wasm_audio_processor_process(
          handle,
          scratch,
          input.length,
        );
        if (result !== 0)
          throw new Error(`Audio enhancement failed (${result})`);
        const samples = module!.HEAPF32.slice(
          scratch >> 2,
          (scratch >> 2) + input.length,
        );
        for (let i = 0; i < samples.length; i++)
          samples[i] = Math.max(-1, Math.min(1, samples[i]! + frame.media[i]!));
        for (let i = 0; i < pcm.length; ++i) {
          const sample =
            encoderChannels === 2
              ? samples[i]!
              : (samples[i * 2]! + samples[i * 2 + 1]!) * 0.5;
          pcm[i] = Math.round(sample * (sample < 0 ? 32768 : 32767));
        }
        const opus = codec.encode(pcm);
        const payload = new Uint8Array(18 + opus.length);
        const view = new DataView(payload.buffer);
        view.setUint32(0, sampleRate, false);
        view.setUint32(4, 20, false);
        view.setUint32(8, 1, false);
        view.setUint16(16, opus.length, false);
        payload.set(opus, 18);
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
