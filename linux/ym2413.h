#ifndef YM2413_H
#define YM2413_H

#include "instruments.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define OPLL_TAU            (2.0f * (float)M_PI)
#define OPLL_CHANNELS       9
#define OPLL_LIMITER_THRESH 0.7f
#define OPLL_LIMITER_CEIL   0.95f
#define OPLL_SINE_BITS      10
#define OPLL_SINE_LEN       (1 << OPLL_SINE_BITS)
#define OPLL_PHASE_BITS     19
#define OPLL_PHASE_MASK     ((1 << OPLL_PHASE_BITS) - 1)

/* ── Envelope states ───────────────────────────────────────────────── */

enum {
    OPLL_ENV_OFF     = 0,
    OPLL_ENV_ATTACK  = 1,
    OPLL_ENV_DECAY   = 2,
    OPLL_ENV_SUSTAIN = 3,
    OPLL_ENV_RELEASE = 4
};

/* ── ROM patch data (15 instruments + 5 rhythm) ────────────────────── */

static const uint8_t OPLL_ROM_PATCHES[15][8] = {
    /* 1  Violin       */ { 0x71, 0x61, 0x1E, 0x17, 0xD0, 0x78, 0x00, 0x17 },
    /* 2  Guitar       */ { 0x13, 0x41, 0x1A, 0x0D, 0xD8, 0xF7, 0x23, 0x13 },
    /* 3  Piano        */ { 0x13, 0x01, 0x99, 0x00, 0xF2, 0xC4, 0x11, 0x23 },
    /* 4  Flute        */ { 0x31, 0x61, 0x0E, 0x07, 0xA8, 0x64, 0x70, 0x27 },
    /* 5  Clarinet     */ { 0x32, 0x21, 0x1E, 0x06, 0xE0, 0x76, 0x00, 0x28 },
    /* 6  Oboe         */ { 0x31, 0x22, 0x16, 0x05, 0xE0, 0x71, 0x00, 0x18 },
    /* 7  Trumpet      */ { 0x21, 0x61, 0x1D, 0x07, 0x82, 0x81, 0x10, 0x07 },
    /* 8  Organ        */ { 0x23, 0x21, 0x2D, 0x14, 0xA2, 0x72, 0x00, 0x07 },
    /* 9  Horn         */ { 0x61, 0x61, 0x1B, 0x06, 0x64, 0x65, 0x10, 0x17 },
    /* 10 Synthesizer  */ { 0x41, 0x61, 0x0B, 0x18, 0x85, 0xF7, 0x71, 0x07 },
    /* 11 Harpsichord  */ { 0x13, 0x01, 0x83, 0x11, 0xFA, 0xE4, 0x10, 0x04 },
    /* 12 Vibraphone   */ { 0x17, 0xC1, 0x24, 0x07, 0xF8, 0xF8, 0x22, 0x12 },
    /* 13 Synth Bass   */ { 0x61, 0x50, 0x0C, 0x05, 0xC2, 0xF5, 0x20, 0x42 },
    /* 14 Acoustic Bass */ { 0x01, 0x01, 0x55, 0x03, 0xC9, 0x95, 0x03, 0x02 },
    /* 15 Elec Guitar  */ { 0x61, 0x41, 0x89, 0x03, 0xF1, 0xE4, 0x40, 0x13 },
};

static const uint8_t OPLL_RHYTHM_PATCHES[5][8] = {
    /* BD (Bass Drum)  */ { 0x01, 0x01, 0x18, 0x0F, 0xDF, 0xF8, 0x6A, 0x6D },
    /* HH (Hi-Hat)     */ { 0x01, 0x01, 0x00, 0x00, 0xC8, 0xD8, 0xA7, 0x48 },
    /* SD (Snare)      */ { 0x01, 0x01, 0x00, 0x00, 0xC8, 0xD8, 0xA7, 0x48 },
    /* TM (Tom-Tom)    */ { 0x05, 0x01, 0x00, 0x00, 0xF8, 0xAA, 0x59, 0x55 },
    /* TC (Top Cymbal) */ { 0x05, 0x01, 0x00, 0x00, 0xF8, 0xAA, 0x59, 0x55 },
};

static const char *OPLL_PATCH_NAMES[16] = {
    "Custom", "Violin", "Guitar", "Piano", "Flute",
    "Clarinet", "Oboe", "Trumpet", "Organ", "Horn",
    "Synthesizer", "Harpsichord", "Vibraphone",
    "Synth Bass", "Acoustic Bass", "Elec Guitar"
};

static const float OPLL_MUL_TABLE[16] = {
    0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f,
    8.0f, 9.0f, 10.0f, 10.0f, 12.0f, 12.0f, 15.0f, 15.0f
};

/* KSL (Key Scale Level) attenuation in dB, indexed by fnum>>5 (8 entries) */
static const float OPLL_KSL_TABLE[8] = {
    0.0f, 18.0f, 24.0f, 27.75f, 30.0f, 32.25f, 33.75f, 35.25f
};

/* ── Operator envelope state ───────────────────────────────────────── */

typedef struct {
    int   state;        /* OPLL_ENV_xxx */
    float level;        /* attenuation in dB: 0 = loudest, 48+ = silent */
    float rate_counter; /* fractional counter for rate-based stepping */
} OPLLEnvelope;

/* ── Per-channel state ─────────────────────────────────────────────── */

typedef struct {
    uint32_t    mod_phase;      /* 19-bit modulator phase accumulator */
    uint32_t    car_phase;      /* 19-bit carrier phase accumulator */
    float       mod_prev[2];    /* modulator feedback memory (2 samples) */
    int         key_on;
    int         instrument;     /* 0-15: 0 = custom, 1-15 = ROM patches */
    int         fnum;           /* 10-bit F-number */
    int         block;          /* 3-bit octave block */
    int         volume;         /* 4-bit channel volume: 0 = loudest, 15 = silent */
    int         midi_note;      /* currently sounding MIDI note (-1 = none) */
    OPLLEnvelope mod_env;
    OPLLEnvelope car_env;
} OPLLChannel;

/* ── Main synth state ──────────────────────────────────────────────── */

typedef struct {
    OPLLChannel channels[OPLL_CHANNELS];
    uint8_t     custom_patch[8];    /* user-definable patch 0 */
    int         rhythm_mode;        /* 0 = 9 melody, 1 = 6 melody + 5 rhythm */
    uint32_t    noise_lfsr;         /* 23-bit LFSR for rhythm noise */
    float       am_phase;           /* AM tremolo LFO phase */
    float       vib_phase;          /* vibrato LFO phase */
    int         current_instrument; /* default ROM patch for new notes */
    float       limiter_env;        /* per-instance limiter envelope */
    float       volume;             /* output volume 0.0-1.0 */

    /* Precomputed sine table (logsin-derived) */
    float       sine_table[OPLL_SINE_LEN];
    /* Half-sine table (positive half only, negative = 0) */
    float       halfsine_table[OPLL_SINE_LEN];
} YM2413State;

/* ── Lookup table init ─────────────────────────────────────────────── */

static void opll_build_tables(YM2413State *s) {
    /*
     * The real YM2413 uses a logsin ROM + exp ROM for waveshaping.
     * We approximate: sin with slight quantization to 256 amplitude levels
     * for that characteristic lo-fi OPLL grittiness.
     */
    for (int i = 0; i < OPLL_SINE_LEN; i++) {
        float phase = (float)i / (float)OPLL_SINE_LEN * OPLL_TAU;
        float raw = sinf(phase);

        /* Quantize to ~8-bit amplitude resolution for OPLL character */
        float quantized = roundf(raw * 255.0f) / 255.0f;
        s->sine_table[i] = quantized;

        /* Half-sine: zero out negative half */
        s->halfsine_table[i] = (i < OPLL_SINE_LEN / 2) ? quantized : 0.0f;
    }
}

/* ── Patch data access ─────────────────────────────────────────────── */

static const uint8_t *opll_get_patch(const YM2413State *s, int instrument) {
    if (instrument <= 0 || instrument > 15)
        return s->custom_patch;
    return OPLL_ROM_PATCHES[instrument - 1];
}

/* Decode patch byte fields */
static inline int opll_patch_mod_am(const uint8_t *p)   { return (p[0] >> 7) & 1; }
static inline int opll_patch_mod_vib(const uint8_t *p)  { return (p[0] >> 6) & 1; }
static inline int opll_patch_mod_eg(const uint8_t *p)   { return (p[0] >> 5) & 1; }
static inline int opll_patch_mod_ksr(const uint8_t *p)  { return (p[0] >> 4) & 1; }
static inline int opll_patch_mod_mul(const uint8_t *p)  { return p[0] & 0x0F; }

static inline int opll_patch_car_am(const uint8_t *p)   { return (p[1] >> 7) & 1; }
static inline int opll_patch_car_vib(const uint8_t *p)  { return (p[1] >> 6) & 1; }
static inline int opll_patch_car_eg(const uint8_t *p)   { return (p[1] >> 5) & 1; }
static inline int opll_patch_car_ksr(const uint8_t *p)  { return (p[1] >> 4) & 1; }
static inline int opll_patch_car_mul(const uint8_t *p)  { return p[1] & 0x0F; }

static inline int opll_patch_mod_ksl(const uint8_t *p)  { return (p[2] >> 6) & 3; }
static inline int opll_patch_mod_tl(const uint8_t *p)   { return p[2] & 0x3F; }

static inline int opll_patch_car_ksl(const uint8_t *p)  { return (p[3] >> 6) & 3; }
static inline int opll_patch_car_wf(const uint8_t *p)   { return (p[3] >> 4) & 1; }
static inline int opll_patch_mod_wf(const uint8_t *p)   { return (p[3] >> 3) & 1; }
static inline int opll_patch_fb(const uint8_t *p)       { return p[3] & 0x07; }

static inline int opll_patch_mod_ar(const uint8_t *p)   { return (p[4] >> 4) & 0x0F; }
static inline int opll_patch_mod_dr(const uint8_t *p)   { return p[4] & 0x0F; }
static inline int opll_patch_car_ar(const uint8_t *p)   { return (p[5] >> 4) & 0x0F; }
static inline int opll_patch_car_dr(const uint8_t *p)   { return p[5] & 0x0F; }

static inline int opll_patch_mod_sl(const uint8_t *p)   { return (p[6] >> 4) & 0x0F; }
static inline int opll_patch_mod_rr(const uint8_t *p)   { return p[6] & 0x0F; }
static inline int opll_patch_car_sl(const uint8_t *p)   { return (p[7] >> 4) & 0x0F; }
static inline int opll_patch_car_rr(const uint8_t *p)   { return p[7] & 0x0F; }

/* ── Envelope processing ───────────────────────────────────────────── */

/*
 * The YM2413 envelope operates in log domain:
 *   - Attack: exponential rise (linear in log domain, fast then slow)
 *   - Decay/Release: linear fall in log domain (exponential in linear)
 *   - Sustain level: 4-bit, 0 = 0dB, 15 = 45dB attenuation (3dB steps)
 *
 * Rate is determined by: rate_value * 4 + key_scale_rate
 * We simplify but keep the character: rate 0 = no change, 15 = instant.
 */

static float opll_rate_to_time(int rate, int ksr_bit, int fnum, int block) {
    if (rate == 0) return 100.0f; /* effectively infinite */
    if (rate >= 15) return 0.0f;  /* instant */

    /* Key scale rate: higher notes = faster envelopes */
    int ksr = 0;
    if (ksr_bit) {
        ksr = (block << 1) | ((fnum >> 9) & 1);
    } else {
        ksr = block >> 1;
    }

    int eff_rate = rate * 4 + ksr;
    if (eff_rate > 63) eff_rate = 63;

    /*
     * Map effective rate to time in seconds.
     * Real chip: rate 63 ~ instant, rate 4 ~ several seconds.
     * Approximation: time = 2^((63 - eff_rate) / 8) * base_time
     */
    float time_s = powf(2.0f, (63.0f - (float)eff_rate) / 8.0f) * 0.004f;
    return time_s;
}

static void opll_env_key_on(OPLLEnvelope *env) {
    env->state = OPLL_ENV_ATTACK;
    env->rate_counter = 0.0f;
    /* Don't reset level — allows re-trigger from current position */
}

static void opll_env_key_off(OPLLEnvelope *env) {
    if (env->state != OPLL_ENV_OFF) {
        env->state = OPLL_ENV_RELEASE;
        env->rate_counter = 0.0f;
    }
}

static void opll_env_tick(OPLLEnvelope *env, float dt,
                          int ar, int dr, int sl, int rr, int eg_typ,
                          int ksr_bit, int fnum, int block)
{
    float target_time;
    float sl_db = (float)sl * 3.0f; /* sustain level in dB (0-45) */

    switch (env->state) {
    case OPLL_ENV_OFF:
        env->level = 48.0f;
        break;

    case OPLL_ENV_ATTACK:
        target_time = opll_rate_to_time(ar, ksr_bit, fnum, block);
        if (target_time <= 0.0f) {
            env->level = 0.0f;
            env->state = OPLL_ENV_DECAY;
            env->rate_counter = 0.0f;
        } else {
            /*
             * Attack in OPLL is exponential in linear domain,
             * meaning: in log domain, it moves fast at start, slow near 0dB.
             * Approximation: level = 48 * (1 - (t/T))^4
             */
            env->rate_counter += dt;
            float t = env->rate_counter / target_time;
            if (t >= 1.0f) {
                env->level = 0.0f;
                env->state = OPLL_ENV_DECAY;
                env->rate_counter = 0.0f;
            } else {
                float rem = 1.0f - t;
                env->level = 48.0f * rem * rem * rem * rem;
            }
        }
        break;

    case OPLL_ENV_DECAY:
        target_time = opll_rate_to_time(dr, ksr_bit, fnum, block);
        if (target_time <= 0.0f) {
            env->level = sl_db;
            env->state = OPLL_ENV_SUSTAIN;
            env->rate_counter = 0.0f;
        } else {
            /* Linear rise in dB (exponential decay in linear domain) */
            float rate_db_per_sec = sl_db / target_time;
            env->level += rate_db_per_sec * dt;
            if (env->level >= sl_db) {
                env->level = sl_db;
                env->state = OPLL_ENV_SUSTAIN;
                env->rate_counter = 0.0f;
            }
        }
        break;

    case OPLL_ENV_SUSTAIN:
        if (eg_typ == 0) {
            /* EG type 0: percussive — continue decaying after sustain */
            float rr_time = opll_rate_to_time(5, ksr_bit, fnum, block);
            if (rr_time > 0.0f) {
                float rate = (48.0f - sl_db) / rr_time;
                env->level += rate * dt;
                if (env->level >= 48.0f) {
                    env->level = 48.0f;
                    env->state = OPLL_ENV_OFF;
                }
            }
        }
        /* EG type 1: sustained — hold at sustain level */
        break;

    case OPLL_ENV_RELEASE:
        target_time = opll_rate_to_time(rr, ksr_bit, fnum, block);
        if (target_time <= 0.0f) {
            env->level = 48.0f;
            env->state = OPLL_ENV_OFF;
        } else {
            float rate = (48.0f - env->level) / target_time;
            if (rate < 1.0f) rate = 48.0f / target_time;
            env->level += rate * dt;
            if (env->level >= 48.0f) {
                env->level = 48.0f;
                env->state = OPLL_ENV_OFF;
            }
        }
        break;
    }

    /* Clamp */
    if (env->level < 0.0f) env->level = 0.0f;
    if (env->level > 48.0f) env->level = 48.0f;
}

/* Convert envelope dB attenuation to linear amplitude */
static inline float opll_env_to_linear(float level_db) {
    if (level_db >= 48.0f) return 0.0f;
    /* 10^(-dB/20) */
    return powf(10.0f, -level_db / 20.0f);
}

/* ── F-number / block from MIDI note ───────────────────────────────── */

/*
 * The OPLL generates frequency as:
 *   freq = fnum * 2^(block - 1) * Fmaster / 2^19
 * where Fmaster = chip_clock / 72 (typically 49716 Hz for 3.58 MHz clock).
 *
 * For software synthesis at arbitrary sample rates, we compute:
 *   phase_inc = fnum * 2^block * MUL / 2^(PHASE_BITS - 1)
 *   (scaled relative to sample rate in the render loop)
 *
 * From MIDI note:
 *   freq = 440 * 2^((note-69)/12)
 *   Pick block so fnum is in 256-511 range for best resolution.
 *   fnum = freq * 2^(20 - block) / (sample_rate / 72)
 */

static void opll_midi_note_to_fnum(int note, int sample_rate,
                                    int *out_fnum, int *out_block)
{
    float freq = 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);

    /* Virtual master clock rate: we use sample_rate * 72 as our reference */
    float fmaster = (float)sample_rate;

    /* Try each block, pick one that gives best fnum range */
    int best_block = 0;
    int best_fnum = 0;

    for (int b = 0; b < 8; b++) {
        /*
         * freq = fnum * 2^(b-1) * fmaster / 2^19
         * fnum = freq * 2^19 / (2^(b-1) * fmaster)
         * fnum = freq * 2^(20-b) / fmaster
         */
        float fn = freq * (float)(1 << (20 - b)) / fmaster;
        int fnum = (int)(fn + 0.5f);

        if (fnum >= 0 && fnum <= 1023) {
            if (fnum >= 256 || b == 7) {
                best_block = b;
                best_fnum = fnum;
                break;
            }
            best_block = b;
            best_fnum = fnum;
        }
    }

    if (best_fnum > 1023) best_fnum = 1023;
    if (best_fnum < 0) best_fnum = 0;

    *out_fnum = best_fnum;
    *out_block = best_block;
}

/* ── Phase increment calculation ───────────────────────────────────── */

static inline uint32_t opll_phase_inc(int fnum, int block, int mul_idx,
                                       int sample_rate)
{
    /*
     * Real chip: phase_inc = fnum * 2^block * MUL
     * We scale to our 19-bit phase accumulator relative to sample rate.
     *
     * freq = fnum * 2^(block-1) * sample_rate / 2^19
     * phase_inc_per_sample = fnum * 2^block * MUL
     *   (the accumulator wraps at 2^19, so one full cycle = 2^19)
     *
     * But we need: phase_inc such that freq = phase_inc * sample_rate / 2^19
     * => phase_inc = freq * 2^19 / sample_rate = fnum * 2^(block-1)
     *
     * With MUL: phase_inc = fnum * 2^(block-1) * MUL_TABLE[mul_idx]
     */
    (void)sample_rate;
    float mul = OPLL_MUL_TABLE[mul_idx];
    float inc = (float)fnum * (float)(1 << block) * mul * 0.5f;
    return (uint32_t)(inc + 0.5f);
}

/* ── Noise LFSR (23-bit, XOR taps at bits 0 and 14) ───────────────── */

static inline int opll_noise_bit(uint32_t *lfsr) {
    int bit = *lfsr & 1;
    *lfsr = (*lfsr >> 1) | (((*lfsr ^ (*lfsr >> 14)) & 1) << 22);
    return bit;
}

/* ── Per-instance limiter ──────────────────────────────────────────── */

static inline float opll_limiter(YM2413State *s, float sample) {
    float absval = fabsf(sample);

    if (absval > s->limiter_env)
        s->limiter_env += (absval - s->limiter_env) * 0.01f;
    else
        s->limiter_env += (absval - s->limiter_env) * 0.0001f;

    float gain = 1.0f;
    if (s->limiter_env > OPLL_LIMITER_THRESH) {
        gain = OPLL_LIMITER_THRESH / s->limiter_env;
    }

    sample *= gain;

    if (fabsf(sample) > OPLL_LIMITER_THRESH) {
        sample = tanhf(sample) * OPLL_LIMITER_CEIL;
    }

    return sample;
}

/* ── KSL calculation ───────────────────────────────────────────────── */

static inline float opll_ksl_atten(int ksl, int fnum, int block) {
    if (ksl == 0) return 0.0f;
    int idx = (fnum >> 7) & 7; /* top 3 bits of 10-bit fnum */
    float base = OPLL_KSL_TABLE[idx];
    float atten = base - (float)(7 - block) * 6.0f;
    if (atten < 0.0f) atten = 0.0f;

    /* KSL scaling: 1 = full, 2 = half, 3 = quarter */
    switch (ksl) {
    case 1: return atten;
    case 2: return atten * 0.5f;
    case 3: return atten * 0.25f;
    default: return 0.0f;
    }
}

/* ── InstrumentType interface ──────────────────────────────────────── */

static void ym2413_init(void *state) {
    YM2413State *s = (YM2413State *)state;
    memset(s, 0, sizeof(*s));

    s->volume = 1.0f;
    s->limiter_env = 0.0f;
    s->current_instrument = 1; /* Default to Violin */
    s->noise_lfsr = 1;         /* Must be nonzero */

    /* Init all channels */
    for (int i = 0; i < OPLL_CHANNELS; i++) {
        OPLLChannel *ch = &s->channels[i];
        ch->instrument = 1;
        ch->volume = 0;        /* 0 = loudest */
        ch->midi_note = -1;
        ch->mod_env.state = OPLL_ENV_OFF;
        ch->mod_env.level = 48.0f;
        ch->car_env.state = OPLL_ENV_OFF;
        ch->car_env.level = 48.0f;
    }

    /* Default custom patch: copy Violin */
    memcpy(s->custom_patch, OPLL_ROM_PATCHES[0], 8);

    opll_build_tables(s);
}

static void ym2413_destroy(void *state) {
    (void)state;
}

/* ── Note management ───────────────────────────────────────────────── */

static void opll_note_on(YM2413State *s, int note, int vel, int sample_rate) {
    /* Find a free channel (or steal the oldest releasing one) */
    int slot = -1;
    int steal = -1;
    float worst_level = -1.0f;

    for (int i = 0; i < OPLL_CHANNELS; i++) {
        OPLLChannel *ch = &s->channels[i];

        /* In rhythm mode, channels 6-8 are reserved */
        if (s->rhythm_mode && i >= 6) continue;

        /* Kill duplicate note */
        if (ch->key_on && ch->midi_note == note) {
            slot = i;
            break;
        }

        /* Free channel */
        if (!ch->key_on && ch->car_env.state == OPLL_ENV_OFF) {
            if (slot < 0) slot = i;
            continue;
        }

        /* Track best steal candidate (most attenuated) */
        if (ch->car_env.level > worst_level) {
            worst_level = ch->car_env.level;
            steal = i;
        }
    }

    if (slot < 0) slot = steal;
    if (slot < 0) slot = 0; /* last resort */

    OPLLChannel *ch = &s->channels[slot];

    /* Calculate F-number and block */
    opll_midi_note_to_fnum(note, sample_rate, &ch->fnum, &ch->block);

    ch->instrument = s->current_instrument;
    ch->key_on = 1;
    ch->midi_note = note;
    ch->mod_phase = 0;
    ch->car_phase = 0;
    ch->mod_prev[0] = 0.0f;
    ch->mod_prev[1] = 0.0f;

    /* Velocity to volume: OPLL has 4-bit volume (0 = max, 15 = silent) */
    ch->volume = 15 - (vel * 15 / 127);
    if (ch->volume < 0) ch->volume = 0;
    if (ch->volume > 15) ch->volume = 15;

    /* Trigger envelopes */
    opll_env_key_on(&ch->mod_env);
    opll_env_key_on(&ch->car_env);
}

static void opll_note_off(YM2413State *s, int note) {
    for (int i = 0; i < OPLL_CHANNELS; i++) {
        OPLLChannel *ch = &s->channels[i];
        if (ch->key_on && ch->midi_note == note) {
            ch->key_on = 0;
            opll_env_key_off(&ch->mod_env);
            opll_env_key_off(&ch->car_env);
        }
    }
}

/* ── Rhythm mode trigger ───────────────────────────────────────────── */

static void opll_rhythm_trigger(YM2413State *s, int drum, int vel,
                                 int sample_rate)
{
    /*
     * Rhythm channels: BD=6, SD/HH=7, TM/TC=8
     * drum: 0=BD, 1=HH, 2=SD, 3=TM, 4=TC
     */
    if (!s->rhythm_mode) return;

    int ch_idx;
    switch (drum) {
    case 0: ch_idx = 6; break;  /* BD */
    case 1: /* fall through */
    case 2: ch_idx = 7; break;  /* HH, SD share channel 7 */
    case 3: /* fall through */
    case 4: ch_idx = 8; break;  /* TM, TC share channel 8 */
    default: return;
    }

    OPLLChannel *ch = &s->channels[ch_idx];

    /* Set appropriate F-number for percussion */
    switch (drum) {
    case 0: /* BD: ~200 Hz */
        opll_midi_note_to_fnum(48, sample_rate, &ch->fnum, &ch->block);
        break;
    case 1: /* HH: high noise */
    case 2: /* SD: mid noise */
        opll_midi_note_to_fnum(72, sample_rate, &ch->fnum, &ch->block);
        break;
    case 3: /* TM: ~300 Hz */
        opll_midi_note_to_fnum(60, sample_rate, &ch->fnum, &ch->block);
        break;
    case 4: /* TC: high metallic */
        opll_midi_note_to_fnum(84, sample_rate, &ch->fnum, &ch->block);
        break;
    }

    ch->instrument = -(drum + 1); /* Negative = rhythm patch index */
    ch->key_on = 1;
    ch->midi_note = -1;
    ch->mod_phase = 0;
    ch->car_phase = 0;
    ch->mod_prev[0] = 0.0f;
    ch->mod_prev[1] = 0.0f;
    ch->volume = 15 - (vel * 15 / 127);
    if (ch->volume < 0) ch->volume = 0;
    if (ch->volume > 15) ch->volume = 15;

    opll_env_key_on(&ch->mod_env);
    opll_env_key_on(&ch->car_env);
}

/* ── MIDI handler ──────────────────────────────────────────────────── */

static void ym2413_midi(void *state, uint8_t status, uint8_t d1, uint8_t d2) {
    YM2413State *s = (YM2413State *)state;
    uint8_t type = status & 0xF0;

    switch (type) {
    case 0x90: /* Note On */
        if (d2 > 0) {
            if (s->rhythm_mode && d1 >= 35 && d1 <= 39) {
                /* GM drum map: 35=BD, 36=BD, 37=SD, 38=SD, 39=HH */
                int drum_map[] = { 0, 0, 2, 2, 1 };
                opll_rhythm_trigger(s, drum_map[d1 - 35], d2, 48000);
            } else if (s->rhythm_mode && d1 >= 40 && d1 <= 44) {
                /* 40=HH, 41=TM, 42=HH, 43=TM, 44=TC */
                int drum_map[] = { 1, 3, 1, 3, 4 };
                opll_rhythm_trigger(s, drum_map[d1 - 40], d2, 48000);
            } else {
                opll_note_on(s, d1, d2, 48000);
            }
        } else {
            opll_note_off(s, d1);
        }
        break;

    case 0x80: /* Note Off */
        opll_note_off(s, d1);
        break;

    case 0xC0: /* Program Change: select ROM instrument 0-14 -> patches 1-15 */
        if (d1 < 16) {
            s->current_instrument = d1 + 1;
            if (s->current_instrument > 15) s->current_instrument = 15;
            fprintf(stderr, "[ym2413] program %d: %s\n",
                    s->current_instrument,
                    OPLL_PATCH_NAMES[s->current_instrument]);
        }
        break;

    case 0xB0: /* CC */
        if (d1 == 1) {
            /* Mod wheel: adjust modulator total level of custom patch */
            s->custom_patch[2] = (s->custom_patch[2] & 0xC0) |
                                 (63 - (d2 * 63 / 127));
        } else if (d1 == 74) {
            /* Brightness: adjust feedback */
            int fb = d2 * 7 / 127;
            s->custom_patch[3] = (s->custom_patch[3] & 0xF8) | (fb & 0x07);
        } else if (d1 == 80) {
            /* Toggle rhythm mode */
            s->rhythm_mode = d2 >= 64 ? 1 : 0;
            fprintf(stderr, "[ym2413] rhythm mode: %s\n",
                    s->rhythm_mode ? "ON" : "OFF");
            if (s->rhythm_mode) {
                /* Release rhythm channels */
                for (int i = 6; i < 9; i++) {
                    s->channels[i].key_on = 0;
                    opll_env_key_off(&s->channels[i].mod_env);
                    opll_env_key_off(&s->channels[i].car_env);
                }
            }
        }
        break;
    }
}

/* ── Render ────────────────────────────────────────────────────────── */

static void ym2413_render(void *state, float *stereo_buf, int frames,
                           int sample_rate)
{
    YM2413State *s = (YM2413State *)state;
    const float dt = 1.0f / (float)sample_rate;

    /* LFO rates: 6.4 Hz for both AM and vibrato */
    const float lfo_rate = 6.4f;
    const float am_depth_db = 1.0f;    /* ~1 dB tremolo depth */
    const float vib_depth = 0.035f;    /* ~6 cents vibrato depth */

    for (int i = 0; i < frames; i++) {
        /* Update LFOs */
        s->am_phase += lfo_rate * dt;
        if (s->am_phase >= 1.0f) s->am_phase -= 1.0f;
        s->vib_phase += lfo_rate * dt;
        if (s->vib_phase >= 1.0f) s->vib_phase -= 1.0f;

        float am_val = (1.0f + sinf(s->am_phase * OPLL_TAU)) * 0.5f;
        float am_atten_db = am_val * am_depth_db;

        float vib_val = sinf(s->vib_phase * OPLL_TAU);
        float vib_mul = 1.0f + vib_val * vib_depth;

        /* Clock noise LFSR a few times per sample for higher rate */
        opll_noise_bit(&s->noise_lfsr);

        float mix = 0.0f;

        for (int c = 0; c < OPLL_CHANNELS; c++) {
            OPLLChannel *ch = &s->channels[c];

            /* Skip if fully silent */
            if (!ch->key_on &&
                ch->mod_env.state == OPLL_ENV_OFF &&
                ch->car_env.state == OPLL_ENV_OFF)
                continue;

            /* Get patch data */
            const uint8_t *patch;
            int is_rhythm = 0;
            if (ch->instrument < 0) {
                /* Rhythm patch */
                int ridx = -(ch->instrument) - 1;
                if (ridx < 0 || ridx > 4) ridx = 0;
                patch = OPLL_RHYTHM_PATCHES[ridx];
                is_rhythm = 1;
            } else {
                patch = opll_get_patch(s, ch->instrument);
            }

            /* Decode patch parameters */
            int mod_mul = opll_patch_mod_mul(patch);
            int car_mul = opll_patch_car_mul(patch);
            int mod_tl  = opll_patch_mod_tl(patch);
            int fb      = opll_patch_fb(patch);
            int mod_wf  = opll_patch_mod_wf(patch);
            int car_wf  = opll_patch_car_wf(patch);
            int mod_am  = opll_patch_mod_am(patch);
            int car_am  = opll_patch_car_am(patch);
            int mod_vib = opll_patch_mod_vib(patch);
            int car_vib = opll_patch_car_vib(patch);
            int mod_ksl = opll_patch_mod_ksl(patch);
            int car_ksl = opll_patch_car_ksl(patch);

            /* Envelope parameters */
            int mod_ar  = opll_patch_mod_ar(patch);
            int mod_dr  = opll_patch_mod_dr(patch);
            int mod_sl  = opll_patch_mod_sl(patch);
            int mod_rr  = opll_patch_mod_rr(patch);
            int mod_eg  = opll_patch_mod_eg(patch);
            int mod_ksr = opll_patch_mod_ksr(patch);

            int car_ar  = opll_patch_car_ar(patch);
            int car_dr  = opll_patch_car_dr(patch);
            int car_sl  = opll_patch_car_sl(patch);
            int car_rr  = opll_patch_car_rr(patch);
            int car_eg  = opll_patch_car_eg(patch);
            int car_ksr = opll_patch_car_ksr(patch);

            /* Tick envelopes */
            opll_env_tick(&ch->mod_env, dt, mod_ar, mod_dr, mod_sl, mod_rr,
                          mod_eg, mod_ksr, ch->fnum, ch->block);
            opll_env_tick(&ch->car_env, dt, car_ar, car_dr, car_sl, car_rr,
                          car_eg, car_ksr, ch->fnum, ch->block);

            /* Phase increments */
            uint32_t mod_inc = opll_phase_inc(ch->fnum, ch->block,
                                               mod_mul, sample_rate);
            uint32_t car_inc = opll_phase_inc(ch->fnum, ch->block,
                                               car_mul, sample_rate);

            /* Apply vibrato */
            if (mod_vib) {
                mod_inc = (uint32_t)((float)mod_inc * vib_mul);
            }
            if (car_vib) {
                car_inc = (uint32_t)((float)car_inc * vib_mul);
            }

            /* ── Modulator ── */

            /* Feedback: average of previous 2 outputs */
            float fb_val = 0.0f;
            if (fb > 0) {
                fb_val = (ch->mod_prev[0] + ch->mod_prev[1]) * 0.5f;
                /* Feedback scaling: 2^(fb-1) / 16 as modulation index */
                fb_val *= (float)(1 << (fb - 1)) / 16.0f;
            }

            /* Modulator phase to table index */
            float mod_phase_f = (float)(ch->mod_phase & OPLL_PHASE_MASK) /
                                (float)(1 << OPLL_PHASE_BITS);
            mod_phase_f += fb_val;
            /* Wrap to [0,1) */
            mod_phase_f -= floorf(mod_phase_f);

            int mod_idx = (int)(mod_phase_f * (float)OPLL_SINE_LEN)
                          & (OPLL_SINE_LEN - 1);
            float mod_out = mod_wf ? s->halfsine_table[mod_idx]
                                   : s->sine_table[mod_idx];

            /* Modulator envelope + total level + KSL + AM */
            float mod_atten_db = ch->mod_env.level;
            mod_atten_db += (float)mod_tl * 0.75f; /* TL: 6-bit, 0.75 dB/step */
            mod_atten_db += opll_ksl_atten(mod_ksl, ch->fnum, ch->block);
            if (mod_am) mod_atten_db += am_atten_db;

            float mod_amp = opll_env_to_linear(mod_atten_db);
            mod_out *= mod_amp;

            /* Store feedback */
            ch->mod_prev[1] = ch->mod_prev[0];
            ch->mod_prev[0] = mod_out;

            /* ── Carrier ── */

            /* Carrier phase modulated by modulator output */
            float car_phase_f = (float)(ch->car_phase & OPLL_PHASE_MASK) /
                                (float)(1 << OPLL_PHASE_BITS);
            /* Modulation index: mod_out scaled to phase offset */
            car_phase_f += mod_out * 2.0f;
            car_phase_f -= floorf(car_phase_f);

            int car_idx = (int)(car_phase_f * (float)OPLL_SINE_LEN)
                          & (OPLL_SINE_LEN - 1);
            float car_out = car_wf ? s->halfsine_table[car_idx]
                                   : s->sine_table[car_idx];

            /* Carrier envelope + channel volume + KSL + AM */
            float car_atten_db = ch->car_env.level;
            car_atten_db += (float)ch->volume * 3.0f; /* 4-bit vol, 3 dB/step */
            car_atten_db += opll_ksl_atten(car_ksl, ch->fnum, ch->block);
            if (car_am) car_atten_db += am_atten_db;

            float car_amp = opll_env_to_linear(car_atten_db);
            car_out *= car_amp;

            /* Rhythm noise injection for percussion channels */
            if (is_rhythm && s->rhythm_mode) {
                int ridx = -(ch->instrument) - 1;
                float noise = opll_noise_bit(&s->noise_lfsr) ? 1.0f : -1.0f;

                switch (ridx) {
                case 1: /* HH: mostly noise */
                    car_out = noise * car_amp * 0.8f + car_out * 0.2f;
                    break;
                case 2: /* SD: noise + tone */
                    car_out = (noise * 0.5f + car_out * 0.5f) * car_amp;
                    break;
                case 4: /* TC: metallic (phase-XOR noise) */
                    car_out = (noise * 0.3f + car_out * 0.7f);
                    break;
                default:
                    break;
                }
            }

            /* NaN guard */
            if (!isfinite(car_out)) {
                ch->mod_phase = 0;
                ch->car_phase = 0;
                ch->mod_prev[0] = 0.0f;
                ch->mod_prev[1] = 0.0f;
                continue;
            }

            mix += car_out;

            /* Advance phase accumulators */
            ch->mod_phase = (ch->mod_phase + mod_inc) & OPLL_PHASE_MASK;
            ch->car_phase = (ch->car_phase + car_inc) & OPLL_PHASE_MASK;
        }

        /* Scale and limit */
        mix *= s->volume * 0.25f; /* headroom for 9 channels */
        mix = opll_limiter(s, mix);

        stereo_buf[i * 2]     = mix;
        stereo_buf[i * 2 + 1] = mix;
    }
}

/* ── OSC handler ───────────────────────────────────────────────────── */

static void ym2413_osc_handle(void *state, const char *sub_path,
                               const int32_t *iargs, int ni,
                               const float *fargs, int nf)
{
    YM2413State *s = (YM2413State *)state;
    (void)fargs;
    (void)nf;

    if (strcmp(sub_path, "/instrument") == 0 && ni >= 1) {
        int inst = iargs[0];
        if (inst >= 0 && inst <= 15) {
            s->current_instrument = inst;
            fprintf(stderr, "[ym2413] OSC instrument %d: %s\n",
                    inst, OPLL_PATCH_NAMES[inst]);
        }
    }
    else if (strcmp(sub_path, "/rhythm") == 0 && ni >= 1) {
        s->rhythm_mode = iargs[0] ? 1 : 0;
        fprintf(stderr, "[ym2413] OSC rhythm mode: %s\n",
                s->rhythm_mode ? "ON" : "OFF");
    }
    else if (strcmp(sub_path, "/volume") == 0 && nf >= 1) {
        float vol = fargs[0];
        if (vol < 0.0f) vol = 0.0f;
        if (vol > 1.0f) vol = 1.0f;
        s->volume = vol;
        fprintf(stderr, "[ym2413] OSC volume: %.2f\n", vol);
    }
    else if (strncmp(sub_path, "/custom/", 8) == 0 && ni >= 1) {
        int reg = atoi(sub_path + 8);
        if (reg >= 0 && reg < 8) {
            s->custom_patch[reg] = (uint8_t)(iargs[0] & 0xFF);
            fprintf(stderr, "[ym2413] OSC custom reg %d = 0x%02X\n",
                    reg, s->custom_patch[reg]);
        }
    }
    else if (strncmp(sub_path, "/channel/", 9) == 0) {
        /* Parse channel number: /channel/N/param */
        const char *rest = sub_path + 9;
        int ch_num = atoi(rest);
        if (ch_num < 0 || ch_num >= OPLL_CHANNELS) return;

        const char *slash = strchr(rest, '/');
        if (!slash) return;

        OPLLChannel *ch = &s->channels[ch_num];

        if (strcmp(slash, "/fnum") == 0 && ni >= 1) {
            ch->fnum = iargs[0] & 0x3FF;
        }
        else if (strcmp(slash, "/block") == 0 && ni >= 1) {
            ch->block = iargs[0] & 0x07;
        }
        else if (strcmp(slash, "/volume") == 0 && ni >= 1) {
            ch->volume = iargs[0] & 0x0F;
        }
        else if (strcmp(slash, "/keyon") == 0 && ni >= 1) {
            if (iargs[0]) {
                ch->key_on = 1;
                opll_env_key_on(&ch->mod_env);
                opll_env_key_on(&ch->car_env);
            } else {
                ch->key_on = 0;
                opll_env_key_off(&ch->mod_env);
                opll_env_key_off(&ch->car_env);
            }
        }
    }
    else if (strcmp(sub_path, "/param/reset") == 0) {
        ym2413_init(state);
        fprintf(stderr, "[ym2413] OSC reset\n");
    }
}

/* ── OSC status ────────────────────────────────────────────────────── */

static int ym2413_osc_status(void *state, uint8_t *buf, int max_len) {
    YM2413State *s = (YM2413State *)state;
    int pos = 0;
    int w;

    /* Address */
    w = osc_write_string(buf + pos, max_len - pos, "/status");
    if (w < 0) return 0;
    pos += w;

    /* Type tags: i s i i */
    w = osc_write_string(buf + pos, max_len - pos, ",isii");
    if (w < 0) return 0;
    pos += w;

    /* current_instrument (i) */
    if (pos + 4 > max_len) return 0;
    osc_write_i32(buf + pos, s->current_instrument);
    pos += 4;

    /* instrument_name (s) */
    const char *name = (s->current_instrument >= 0 && s->current_instrument <= 15)
                       ? OPLL_PATCH_NAMES[s->current_instrument] : "Unknown";
    w = osc_write_string(buf + pos, max_len - pos, name);
    if (w < 0) return 0;
    pos += w;

    /* rhythm_mode (i) */
    if (pos + 4 > max_len) return 0;
    osc_write_i32(buf + pos, s->rhythm_mode);
    pos += 4;

    /* active_channels (i) */
    if (pos + 4 > max_len) return 0;
    int active = 0;
    for (int c = 0; c < OPLL_CHANNELS; c++) {
        if (s->channels[c].key_on ||
            s->channels[c].car_env.state != OPLL_ENV_OFF)
            active++;
    }
    osc_write_i32(buf + pos, active);
    pos += 4;

    return pos;
}

/* ── Exported type descriptor ──────────────────────────────────────── */

InstrumentType ym2413_type = {
    .name         = "ym2413",
    .display_name = "YM2413 OPLL",
    .state_size   = sizeof(YM2413State),
    .init         = ym2413_init,
    .destroy      = ym2413_destroy,
    .midi         = ym2413_midi,
    .render       = ym2413_render,
    .osc_handle   = ym2413_osc_handle,
    .osc_status   = ym2413_osc_status,
};

#endif /* YM2413_H */
