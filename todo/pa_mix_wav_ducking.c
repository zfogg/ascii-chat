// NOTE: This file is just an example. We want to add proper audio mixing
// with things like compression and ducking and etc at some point. We'll
// use this file as a reference when we get started.

// Build:
//   macOS:   brew install portaudio libsndfile
//            gcc pa_mix_wav_ducking.c -o pa_mix_wav -lportaudio -lsndfile -lm
//   Linux:   sudo apt-get install libportaudio2 portaudio19-dev libsndfile1 libsndfile1-dev
//            gcc pa_mix_wav_ducking.c -o pa_mix_wav -lportaudio -lsndfile -lm
// Run:
//   ./pa_mix_wav voice1.wav voice2.wav voice3.wav

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <portaudio.h>
#include <sndfile.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------- Utility ----------
static inline float db_to_lin(float dB) { return powf(10.0f, dB / 20.0f); }
static inline float lin_to_db(float lin) { return 20.0f * log10f(fmaxf(lin, 1e-12f)); }

// ---------- Bus Compressor ----------
typedef struct {
    float threshold_dB, knee_dB, ratio, attack_ms, release_ms, makeup_dB;
    float fs, env, gain_lin, att_coeff, rel_coeff;
} Compressor;

static void comp_init(Compressor* c, float fs) {
    c->fs = fs; c->env = 0.0f; c->gain_lin = 1.0f;
    float att_tau = c->attack_ms / 1000.0f;
    float rel_tau = c->release_ms / 1000.0f;
    c->att_coeff = expf(-1.0f / (att_tau * fs + 1e-12f));
    c->rel_coeff = expf(-1.0f / (rel_tau * fs + 1e-12f));
}

static inline float comp_gain_reduction_dB(const Compressor* c, float level_dB) {
    float over = level_dB - c->threshold_dB, k = c->knee_dB;
    if (k > 0.0f) {
        if (over <= -k * 0.5f) return 0.0f;
        if (over >=  k * 0.5f) return (1.0f / c->ratio - 1.0f) * over;
        float x = over + k * 0.5f;
        return (1.0f / c->ratio - 1.0f) * (x * x) / (2.0f * k);
    } else {
        if (over <= 0.0f) return 0.0f;
        return (1.0f / c->ratio - 1.0f) * over;
    }
}

static inline float comp_process_sample(Compressor* c, float sc) {
    float x = fabsf(sc);
    if (x > c->env) c->env = c->att_coeff * c->env + (1.0f - c->att_coeff) * x;
    else            c->env = c->rel_coeff * c->env + (1.0f - c->rel_coeff) * x;
    float level_dB = lin_to_db(c->env);
    float gr_dB = comp_gain_reduction_dB(c, level_dB);
    float target_lin = db_to_lin(gr_dB + c->makeup_dB);
    if (target_lin < c->gain_lin) c->gain_lin = c->att_coeff * c->gain_lin + (1.0f - c->att_coeff) * target_lin;
    else                          c->gain_lin = c->rel_coeff * c->gain_lin + (1.0f - c->rel_coeff) * target_lin;
    return c->gain_lin;
}

// ---------- Active-speaker Ducking ----------
typedef struct {
    float threshold_dB;     // gate: below this, track isn't "speaking"
    float leaderMargin_dB;  // tracks within this of the loudest are NOT ducked
    float atten_dB;         // attenuation applied to non-leaders (e.g., -12 dB)
    float attack_ms;        // ducking attack
    float release_ms;       // ducking release
    float att_coeff, rel_coeff;
    float *env;             // per-track envelope (linear)
    float *gain;            // per-track ducking gain (linear)
} Ducking;

static void duck_init(Ducking* d, int numTracks, float fs) {
    float att_tau = d->attack_ms / 1000.0f;
    float rel_tau = d->release_ms / 1000.0f;
    d->att_coeff = expf(-1.0f / (att_tau * fs + 1e-12f));
    d->rel_coeff = expf(-1.0f / (rel_tau * fs + 1e-12f));
    d->env  = (float*)calloc(numTracks, sizeof(float));
    d->gain = (float*)malloc(sizeof(float)*numTracks);
    for (int i = 0; i < numTracks; ++i) d->gain[i] = 1.0f;
}

static inline void duck_free(Ducking* d) {
    free(d->env); free(d->gain);
}

// ---------- Mixer ----------
typedef struct {
    float **tracks;
    sf_count_t *frames;
    sf_count_t *pos;
    int numTracks, channels, sampleRate;

    // Loud-with-few, quiet-with-many
    float crowdAlpha;   // 1 / active^alpha
    float baseGain;

    // Processing blocks
    Ducking duck;
    Compressor comp;
} Mixer;

static int paCallback(
    const void *input, void *output, unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData)
{
    Mixer *m = (Mixer*)userData;
    float *out = (float*)output;

    // If nothing left, exit
    int anyRemaining = 0;
    for (int t = 0; t < m->numTracks; ++t) if (m->pos[t] < m->frames[t]) { anyRemaining = 1; break; }
    if (!anyRemaining) return paComplete;

    // Active track count (coarse) for crowd scaling
    int activeTracks = 0;
    for (int t = 0; t < m->numTracks; ++t) if (m->pos[t] < m->frames[t]) activeTracks++;

    float crowdGain = 1.0f / powf((float)activeTracks, m->crowdAlpha);
    float preBusGain = m->baseGain * crowdGain;

    // Process frame-by-frame to compute ducking based on instantaneous envelopes
    for (unsigned long f = 0; f < framesPerBuffer; ++f) {
        // 1) Read one frame from each track (or silence)
        float sampleL[64]; // support up to 64 tracks; enlarge if needed
        float sampleR[64];
        if (m->numTracks > 64) return paAbort; // keep demo simple

        for (int t = 0; t < m->numTracks; ++t) {
            if (m->pos[t] < m->frames[t]) {
                // Read current frame for this track
                float L = m->tracks[t][m->pos[t]*m->channels + 0];
                float R = (m->channels > 1) ? m->tracks[t][m->pos[t]*m->channels + 1] : L;
                sampleL[t] = L;
                sampleR[t] = R;

                // Update envelope from mono-ish absolute level
                float sc = 0.5f * (fabsf(L) + fabsf(R));
                if (sc > m->duck.env[t]) m->duck.env[t] = m->duck.att_coeff * m->duck.env[t] + (1.0f - m->duck.att_coeff) * sc;
                else                     m->duck.env[t] = m->duck.rel_coeff * m->duck.env[t] + (1.0f - m->duck.rel_coeff) * sc;
            } else {
                sampleL[t] = sampleR[t] = 0.0f;
                // decay envelope when finished
                m->duck.env[t] = m->duck.rel_coeff * m->duck.env[t];
            }
        }

        // 2) Find the loudest speaker and mark leaders (within margin)
        float max_dB = -120.0f;
        float env_dB[64];
        for (int t = 0; t < m->numTracks; ++t) {
            env_dB[t] = lin_to_db(m->duck.env[t]);
            if (env_dB[t] > max_dB) max_dB = env_dB[t];
        }

        // 3) Compute target gains (leaders=1.0, non-leaders=atten)
        float leaderCut = db_to_lin(m->duck.atten_dB); // e.g., ~0.25 for -12 dB
        for (int t = 0; t < m->numTracks; ++t) {
            int isSpeaking = env_dB[t] > m->duck.threshold_dB;
            int isLeader   = isSpeaking && (env_dB[t] >= max_dB - m->duck.leaderMargin_dB);
            float target = isLeader ? 1.0f : (isSpeaking ? leaderCut : 1.0f);
            // Smooth per-track ducking gain
            if (target < m->duck.gain[t]) m->duck.gain[t] = m->duck.att_coeff * m->duck.gain[t] + (1.0f - m->duck.att_coeff) * target;
            else                          m->duck.gain[t] = m->duck.rel_coeff * m->duck.gain[t] + (1.0f - m->duck.rel_coeff) * target;
        }

        // 4) Mix this frame with ducking & crowd scaling
        float mixL = 0.0f, mixR = 0.0f;
        for (int t = 0; t < m->numTracks; ++t) {
            mixL += sampleL[t] * m->duck.gain[t];
            mixR += sampleR[t] * m->duck.gain[t];
            if (m->pos[t] < m->frames[t]) m->pos[t]++; // advance if we consumed a frame
        }
        mixL *= preBusGain; mixR *= preBusGain;

        // 5) Bus compression (sidechain = abs mean of L/R)
        float sc = 0.5f * (fabsf(mixL) + fabsf(mixR));
        float g  = comp_process_sample(&m->comp, sc);
        mixL *= g; mixR *= g;

        // 6) Store & clamp
        if (m->channels == 1) {
            out[f] = fmaxf(-1.0f, fminf(1.0f, mixL));
        } else {
            out[f*m->channels + 0] = fmaxf(-1.0f, fminf(1.0f, mixL));
            out[f*m->channels + 1] = fmaxf(-1.0f, fminf(1.0f, mixR));
        }
    }

    // Are we done?
    int allDone = 1;
    for (int t = 0; t < m->numTracks; ++t) if (m->pos[t] < m->frames[t]) { allDone = 0; break; }
    return allDone ? paComplete : paContinue;
}

static void die(const char *msg) { fprintf(stderr, "Error: %s\n", msg); exit(1); }

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <wav1> [wav2 ...]\n", argv[0]); return 1; }

    int numTracks = argc - 1, channels = -1, sampleRate = -1;
    float **tracks = (float**)calloc(numTracks, sizeof(float*));
    sf_count_t *frames = (sf_count_t*)calloc(numTracks, sizeof(sf_count_t));
    sf_count_t *pos    = (sf_count_t*)calloc(numTracks, sizeof(sf_count_t));

    // Load WAVs (float32)
    for (int i = 0; i < numTracks; ++i) {
        SF_INFO info = {0};
        SNDFILE *sf = sf_open(argv[i+1], SFM_READ, &info);
        if (!sf) { fprintf(stderr, "Failed to open %s\n", argv[i+1]); return 1; }
        if (channels == -1) channels = info.channels;
        if (sampleRate == -1) sampleRate = info.samplerate;
        if (info.channels != channels || info.samplerate != sampleRate) {
            sf_close(sf); die("All WAVs must share sample rate and channel count.");
        }
        frames[i] = info.frames;
        tracks[i] = (float*)malloc(sizeof(float) * info.frames * info.channels);
        if (sf_readf_float(sf, tracks[i], info.frames) != info.frames) { sf_close(sf); die("Short read."); }
        sf_close(sf);
    }

    PaError err = Pa_Initialize();
    if (err != paNoError) die(Pa_GetErrorText(err));

    Mixer m = {
        .tracks = tracks, .frames = frames, .pos = pos,
        .numTracks = numTracks, .channels = channels, .sampleRate = sampleRate,
        .crowdAlpha = 0.5f, .baseGain = 0.9f,
        .duck = {
            .threshold_dB = -45.0f,   // gate level for "is speaking"
            .leaderMargin_dB = 6.0f,  // within 6 dB of loudest = not ducked
            .atten_dB = -12.0f,       // attenuation for non-leaders
            .attack_ms = 15.0f,       // snappy engage
            .release_ms = 150.0f      // natural recovery
        },
        .comp = {
            .threshold_dB = -12.0f, .knee_dB = 6.0f, .ratio = 4.0f,
            .attack_ms = 8.0f, .release_ms = 120.0f, .makeup_dB = 3.0f
        }
    };
    duck_init(&m.duck, numTracks, (float)sampleRate);
    comp_init(&m.comp, (float)sampleRate);

    PaStream *stream;
    err = Pa_OpenDefaultStream(&stream, 0, channels, paFloat32, sampleRate,
                               paFramesPerBufferUnspecified, paCallback, &m);
    if (err != paNoError) die(Pa_GetErrorText(err));
    if ((err = Pa_StartStream(stream)) != paNoError) die(Pa_GetErrorText(err));

    while (Pa_IsStreamActive(stream) == 1) Pa_Sleep(50);

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    duck_free(&m.duck);
    for (int i = 0; i < numTracks; ++i) free(tracks[i]);
    free(tracks); free(frames); free(pos);
    printf("Done.\n");
    return 0;
}