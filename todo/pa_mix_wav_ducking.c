// NOTE: This file is just an example. We want to add proper audio mixing
// with things like compression and ducking and etc at some point. We'll
// use this file as a reference when we get started.

// Build:
//   macOS:   brew install portaudio
//            gcc pa_live_ducking.c -o pa_live_ducking -lportaudio -lm
//   Linux:   sudo apt-get install libportaudio2 portaudio19-dev
//            gcc pa_live_ducking.c -o pa_live_ducking -lportaudio -lm
// Run (examples):
//   ./pa_live_ducking 4 2       # 4 mono inputs -> 2ch (stereo) output
//   ./pa_live_ducking 3 1 48000 # 3 mono inputs -> 1ch (mono) output @ 48 kHz
//
// Notes:
// - Program opens ONE full-duplex stream with inputChannels = numTalkers (mono per talker),
//   outputChannels = outChannels (1 or 2).
// - You can pick specific devices by replacing Pa_OpenDefaultStream with Pa_OpenStream
//   and filling PaStreamParameters for input/output (device index, latency, etc).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <portaudio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------- Utils ----------
static inline float db_to_lin(float dB) {
  return powf(10.0f, dB / 20.0f);
}
static inline float lin_to_db(float lin) {
  return 20.0f * log10f(fmaxf(lin, 1e-12f));
}
static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

// ---------- Bus Compressor ----------
typedef struct {
  float threshold_dB, knee_dB, ratio, attack_ms, release_ms, makeup_dB;
  float fs, env, gain_lin, att_coeff, rel_coeff;
} Compressor;

static void comp_init(Compressor *c, float fs) {
  c->fs = fs;
  c->env = 0.0f;
  c->gain_lin = 1.0f;
  float att_tau = c->attack_ms / 1000.0f;
  float rel_tau = c->release_ms / 1000.0f;
  c->att_coeff = expf(-1.0f / (att_tau * fs + 1e-12f));
  c->rel_coeff = expf(-1.0f / (rel_tau * fs + 1e-12f));
}

static inline float comp_gain_reduction_dB(const Compressor *c, float level_dB) {
  float over = level_dB - c->threshold_dB, k = c->knee_dB;
  if (k > 0.0f) {
    if (over <= -k * 0.5f)
      return 0.0f;
    if (over >= k * 0.5f)
      return (1.0f / c->ratio - 1.0f) * over;
    float x = over + k * 0.5f;
    return (1.0f / c->ratio - 1.0f) * (x * x) / (2.0f * k);
  } else {
    if (over <= 0.0f)
      return 0.0f;
    return (1.0f / c->ratio - 1.0f) * over;
  }
}

static inline float comp_process_sample(Compressor *c, float sc) {
  float x = fabsf(sc);
  if (x > c->env)
    c->env = c->att_coeff * c->env + (1.0f - c->att_coeff) * x;
  else
    c->env = c->rel_coeff * c->env + (1.0f - c->rel_coeff) * x;
  float level_dB = lin_to_db(c->env);
  float gr_dB = comp_gain_reduction_dB(c, level_dB);
  float target_lin = db_to_lin(gr_dB + c->makeup_dB);
  if (target_lin < c->gain_lin)
    c->gain_lin = c->att_coeff * c->gain_lin + (1.0f - c->att_coeff) * target_lin;
  else
    c->gain_lin = c->rel_coeff * c->gain_lin + (1.0f - c->rel_coeff) * target_lin;
  return c->gain_lin;
}

// ---------- Active-speaker Ducking ----------
typedef struct {
  float threshold_dB;    // below this, track isn't "speaking"
  float leaderMargin_dB; // within this of the loudest = leader (not ducked)
  float atten_dB;        // attenuation for non-leaders
  float attack_ms;       // ducking attack
  float release_ms;      // ducking release
  float att_coeff, rel_coeff;
  float *env;  // per-track envelope (linear)
  float *gain; // per-track ducking gain (linear)
} Ducking;

static void duck_init(Ducking *d, int numTracks, float fs) {
  float att_tau = d->attack_ms / 1000.0f;
  float rel_tau = d->release_ms / 1000.0f;
  d->att_coeff = expf(-1.0f / (att_tau * fs + 1e-12f));
  d->rel_coeff = expf(-1.0f / (rel_tau * fs + 1e-12f));
  d->env = (float *)calloc(numTracks, sizeof(float));
  d->gain = (float *)malloc(sizeof(float) * numTracks);
  for (int i = 0; i < numTracks; ++i)
    d->gain[i] = 1.0f;
}

static inline void duck_free(Ducking *d) {
  free(d->env);
  free(d->gain);
}

// ---------- Mixer State ----------
typedef struct {
  int numTalkers;  // number of mono input channels
  int outChannels; // 1 or 2
  int sampleRate;

  // Loud-with-few, quiet-with-many
  float crowdAlpha; // scale ~ 1 / active^alpha
  float baseGain;

  // Processing
  Ducking duck;
  Compressor comp;
} Mixer;

// ---------- PortAudio Callback ----------
static int paCallback(const void *input, void *output, unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
  Mixer *m = (Mixer *)userData;
  const float *in = (const float *)input; // interleaved: [t0, t1, ..., tN-1, t0, t1, ...]
  float *out = (float *)output;

  if (!in) {
    // No input available; output silence
    memset(out, 0, sizeof(float) * framesPerBuffer * m->outChannels);
    return paContinue;
  }

  const int T = m->numTalkers;
  const int OC = m->outChannels;

  for (unsigned long f = 0; f < framesPerBuffer; ++f) {
    // 1) Read current frame per talker (mono per talker)
    //    Update per-track envelopes (attack/release)
    float s[128]; // up to 128 talkers; expand if needed
    if (T > 128)
      return paAbort;

    int activeTalkers = 0;
    for (int t = 0; t < T; ++t) {
      float v = in[f * T + t]; // mono channel for talker t at frame f
      s[t] = v;

      float x = fabsf(v);
      if (x > m->duck.env[t])
        m->duck.env[t] = m->duck.att_coeff * m->duck.env[t] + (1.0f - m->duck.att_coeff) * x;
      else
        m->duck.env[t] = m->duck.rel_coeff * m->duck.env[t] + (1.0f - m->duck.rel_coeff) * x;

      // consider "active" if above a small floor (way below threshold_dB)
      if (m->duck.env[t] > db_to_lin(-70.0f))
        activeTalkers++;
    }
    if (activeTalkers == 0) {
      // nobody speaking -> output silence this frame (or keep ambience if you like)
      if (OC == 1)
        out[f] = 0.0f;
      else {
        out[f * OC] = 0.0f;
        out[f * OC + 1] = 0.0f;
      }
      continue;
    }

    // 2) Identify leaders (tracks within leaderMargin_dB of loudest & above threshold)
    float max_dB = -120.0f;
    float env_dB[128];
    for (int t = 0; t < T; ++t) {
      env_dB[t] = lin_to_db(m->duck.env[t]);
      if (env_dB[t] > max_dB)
        max_dB = env_dB[t];
    }

    float leaderCut = db_to_lin(m->duck.atten_dB); // e.g., -12 dB -> ~0.25
    for (int t = 0; t < T; ++t) {
      int isSpeaking = env_dB[t] > m->duck.threshold_dB;
      int isLeader = isSpeaking && (env_dB[t] >= max_dB - m->duck.leaderMargin_dB);
      float target = isLeader ? 1.0f : (isSpeaking ? leaderCut : 1.0f);
      if (target < m->duck.gain[t])
        m->duck.gain[t] = m->duck.att_coeff * m->duck.gain[t] + (1.0f - m->duck.att_coeff) * target;
      else
        m->duck.gain[t] = m->duck.rel_coeff * m->duck.gain[t] + (1.0f - m->duck.rel_coeff) * target;
    }

    // 3) Crowd scaling (few louder, many quieter)
    float crowdGain = 1.0f / powf((float)activeTalkers, m->crowdAlpha);
    float preBus = m->baseGain * crowdGain;

    // 4) Mix this frame with ducking & crowd scaling
    float mixL = 0.0f, mixR = 0.0f;
    for (int t = 0; t < T; ++t) {
      float g = m->duck.gain[t];
      float v = s[t] * g;
      mixL += v;
      mixR += v;
    }
    mixL *= preBus;
    mixR *= preBus;

    // 5) Bus compression (sidechain from absolute mean)
    float sc = 0.5f * (fabsf(mixL) + fabsf(mixR));
    float g = comp_process_sample(&m->comp, sc);
    mixL *= g;
    mixR *= g;

    // 6) Output (mono or stereo), clamp
    if (OC == 1) {
      out[f] = clampf(mixL, -1.0f, 1.0f);
    } else {
      out[f * OC + 0] = clampf(mixL, -1.0f, 1.0f);
      out[f * OC + 1] = clampf(mixR, -1.0f, 1.0f);
    }
  }

  return paContinue;
}

static void die(const char *msg) {
  fprintf(stderr, "Error: %s\n", msg);
  exit(1);
}

int main(int argc, char **argv) {
  int numTalkers = 2;     // default: 2 mono inputs
  int outChannels = 2;    // default: stereo out
  int sampleRate = 48000; // default SR

  if (argc >= 2)
    numTalkers = atoi(argv[1]);
  if (argc >= 3)
    outChannels = atoi(argv[2]);
  if (argc >= 4)
    sampleRate = atoi(argv[3]);
  if (numTalkers <= 0 || outChannels < 1 || outChannels > 2) {
    fprintf(stderr, "Usage: %s [numTalkers>=1] [outChannels 1|2] [sampleRate]\n", argv[0]);
    return 1;
  }

  PaError err = Pa_Initialize();
  if (err != paNoError)
    die(Pa_GetErrorText(err));

  // Optional: print default devices & channel counts
  const PaDeviceInfo *inDev = Pa_GetDeviceInfo(Pa_GetDefaultInputDevice());
  const PaDeviceInfo *outDev = Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
  if (!inDev || !outDev)
    die("No default input/output device.");
  if (inDev->maxInputChannels < numTalkers) {
    fprintf(stderr, "Default input device has only %d input channels; need %d.\n", inDev->maxInputChannels, numTalkers);
    fprintf(stderr, "Choose/aggregate an input device with >= %d channels.\n", numTalkers);
    Pa_Terminate();
    return 1;
  }
  if (outDev->maxOutputChannels < outChannels) {
    fprintf(stderr, "Default output device has only %d output channels; need %d.\n", outDev->maxOutputChannels,
            outChannels);
    Pa_Terminate();
    return 1;
  }

  Mixer m = {.numTalkers = numTalkers,
             .outChannels = outChannels,
             .sampleRate = sampleRate,
             .crowdAlpha = 0.5f, // 0.4–0.7 feels natural for conferences
             .baseGain = 0.9f,
             .duck =
                 {
                     .threshold_dB = -45.0f,  // gate for "speaking"
                     .leaderMargin_dB = 6.0f, // within 6 dB of loudest = leader
                     .atten_dB = -12.0f,      // duck others by ~12 dB
                     .attack_ms = 12.0f,      // fast enough to respect interruptions
                     .release_ms = 160.0f     // avoids chattery pumping
                 },
             .comp = {.threshold_dB = -12.0f,
                      .knee_dB = 6.0f,
                      .ratio = 4.0f,
                      .attack_ms = 8.0f,
                      .release_ms = 120.0f,
                      .makeup_dB = 3.0f}};
  duck_init(&m.duck, numTalkers, (float)sampleRate);
  comp_init(&m.comp, (float)sampleRate);

  // Use default duplex stream. For specific devices, switch to Pa_OpenStream with PaStreamParameters.
  PaStream *stream;
  err = Pa_OpenDefaultStream(&stream,
                             numTalkers,  // input channels (mono per talker)
                             outChannels, // output channels
                             paFloat32,   // sample format
                             sampleRate,
                             paFramesPerBufferUnspecified, // let PA choose (or try 240/256 for ~5ms @48k)
                             paCallback, &m);
  if (err != paNoError)
    die(Pa_GetErrorText(err));

  err = Pa_StartStream(stream);
  if (err != paNoError)
    die(Pa_GetErrorText(err));

  printf("Running live mixer: %d talkers -> %dch out @ %d Hz. Press Ctrl+C to quit.\n", numTalkers, outChannels,
         sampleRate);

  // Simple run loop (block until stream stops)
  while (Pa_IsStreamActive(stream) == 1)
    Pa_Sleep(100);

  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();

  duck_free(&m.duck);
  printf("Done.\n");
  return 0;
}
