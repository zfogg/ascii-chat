const { AudioWorkletProcessor, registerProcessor, sampleRate } = globalThis;

class PcmPlaybackProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.channels = [new Float32Array(32768), new Float32Array(32768)];
    this.read = 0;
    this.count = 0;
    this.started = false;
    this.last = [0, 0];
    this.fade = 0;
    this.underruns = 0;
    this.fadeOut = 0;
    this.port.onmessage = ({ data }) => {
      const { samples, channels, rate } = data;
      if (rate !== sampleRate || channels < 1 || channels > 2) return;
      const frames = samples.length / channels;
      // Keep at most 200 ms of recent audio when a delayed network burst arrives.
      const limit = Math.floor(sampleRate * 0.2);
      const discard = Math.max(0, this.count + frames - limit);
      const old = Math.min(discard, this.count);
      if (discard) {
        this.read = (this.read + old) % 32768;
        this.count -= old;
        this.fade = 128;
      }
      const skip = discard - old;
      for (let i = skip; i < frames; i++) {
        const index = (this.read + this.count) % 32768;
        this.channels[0][index] = samples[i * channels];
        this.channels[1][index] =
          samples[i * channels + (channels === 2 ? 1 : 0)];
        this.count++;
      }
    };
  }

  process(_inputs, outputs) {
    const output = outputs[0];
    if (!output || !output.length) return true;
    if (!this.started) {
      if (this.count < sampleRate * 0.06) {
        for (let i = 0; i < output[0].length; i++) {
          const gain = Math.max(0, --this.fadeOut / 64);
          for (let channel = 0; channel < output.length; channel++)
            output[channel][i] = this.last[channel] * gain;
        }
        this.fadeOut = Math.max(0, this.fadeOut);
        return true;
      }
      this.started = true;
      this.fade = 128;
    }
    for (let i = 0; i < output[0].length; i++) {
      if (!this.count) {
        // Fade the final sample to zero, then refill before resuming playback.
        this.fadeOut = 64;
        for (let j = i; j < output[0].length; j++) {
          const gain = Math.max(0, --this.fadeOut / 64);
          for (let channel = 0; channel < output.length; channel++)
            output[channel][j] = this.last[channel] * gain;
        }
        this.fadeOut = Math.max(0, this.fadeOut);
        this.started = false;
        this.underruns++;
        this.port.postMessage({ underruns: this.underruns });
        break;
      }
      const gain = this.fade ? (129 - this.fade--) / 128 : 1;
      for (let channel = 0; channel < output.length; channel++) {
        const value = this.channels[channel][this.read];
        output[channel][i] = value * gain;
        this.last[channel] = output[channel][i];
      }
      this.read = (this.read + 1) % 32768;
      this.count--;
    }
    return true;
  }
}

registerProcessor("ascii-chat-pcm-playback", PcmPlaybackProcessor);
