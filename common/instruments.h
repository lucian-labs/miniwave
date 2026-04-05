#ifndef INSTRUMENTS_H
#define INSTRUMENTS_H

#include <stdatomic.h>
#include <stdint.h>

#define MAX_SLOTS 16

/* Forward declarations — full definitions in seq.h and keyseq.h */
typedef struct MiniSeq MiniSeq;
typedef struct KeySeq KeySeq;

/* Every instrument type implements this interface.
 *
 * Required: name, display_name, state_size, init, destroy, midi, render
 * Optional (NULL = not implemented): calc, set_param, json_status,
 *          json_save, json_load, osc_handle, osc_status
 *
 * Migration path: instruments implement calc() and render() becomes a
 * thin wrapper. Once all instruments have calc(), render_mix switches
 * to calling calc() per-sample and render() is removed.
 */
typedef struct InstrumentType {
    const char *name;           /* "fm-synth", "drum-machine", etc */
    const char *display_name;   /* "FM Synth (yama-bruh)" */
    size_t state_size;          /* sizeof the instrument's state struct */

    /* Lifecycle */
    void (*init)(void *state);
    void (*destroy)(void *state);

    /* MIDI — pure note/CC handling, no keyseq intercept */
    void (*midi)(void *state, uint8_t status, uint8_t d1, uint8_t d2);

    /* Audio — buffer-based (current) */
    void (*render)(void *state, float *stereo_buf, int frames, int sample_rate);

    /* Audio — per-sample (target, optional during migration) */
    void (*calc)(void *state, float *L, float *R, float dt, float cents_mod);

    /* Named param setter — replaces osc_handle /param/ paths */
    void (*set_param)(void *state, const char *name, float value);

    /* Self-reported JSON — replaces per-instrument switches in server.h */
    int  (*json_status)(void *state, char *buf, int max);

    /* State persistence — instrument saves/loads its own params */
    int  (*json_save)(void *state, char *buf, int max);
    int  (*json_load)(void *state, const char *json);

    /* Legacy OSC — retained for non-param paths (/preset, /volume, /seq/) */
    void (*osc_handle)(void *state, const char *sub_path,
                       const int32_t *iargs, int ni,
                       const float *fargs, int nf);
    int  (*osc_status)(void *state, uint8_t *buf, int max_len);
} InstrumentType;

/* A slot in the rack.
 *
 * `active` and `gen` are atomic — render/MIDI threads read them without locks.
 * Slot swaps: set active=0, barrier, wait for gen to propagate, swap, set active=1.
 *
 * KeySeq and MiniSeq live here, not in instrument state — the slot layer
 * handles MIDI interception, seq ticking, and param routing. Instruments
 * are pure synthesis black boxes.
 */
typedef struct {
    _Atomic int      active;        /* 0 = empty slot */
    int              type_idx;      /* index into registered instrument types (-1 = none) */
    void            *state;         /* instrument state (malloc'd) */
    float            volume;        /* 0.0 - 1.0 */
    int              mute;
    int              solo;
    _Atomic uint32_t gen;           /* generation — readers snapshot, writers bump */

    /* Sequencing — per-slot, not per-instrument */
    MiniSeq         *seq;           /* allocated on slot init, NULL if empty */
    KeySeq          *keyseq;        /* allocated on slot init, NULL if empty */
    float            cents_mod;     /* output from keyseq, fed to calc() */

    /* Pitch bend + mod wheel (vibrato LFO) — per-slot, all instruments */
    float            pitch_bend;       /* -1.0 to +1.0 from pitch wheel */
    float            pitch_bend_range; /* semitones per direction, default 2 */
    float            mod_wheel;        /* 0.0 to 1.0 from CC1 */
    float            vibrato_phase;    /* LFO phase accumulator */

    /* State cache — preserves instrument params when cycling types */
    #define SLOT_CACHE_TYPES 8
    #define SLOT_CACHE_SIZE  4096
    char             state_cache[SLOT_CACHE_TYPES][SLOT_CACHE_SIZE];
    int              state_cache_valid[SLOT_CACHE_TYPES];

    /* Mono / Legato — per-slot */
    int              mono;          /* 1 = mono mode (kill prev note on new) */
    int              legato;        /* 1 = legato (portamento glide) */
    float            glide_from;    /* source freq for portamento (Hz) */
    float            glide_to;      /* target freq (Hz) */
    float            glide_pos;     /* 0→1 progress */
    float            glide_rate;    /* glide speed (1/seconds) */
    int              last_note;     /* last played note (-1 = none) */

    /* User presets — per-slot param snapshots */
    #define MAX_USER_PRESETS 64
    #define MAX_PRESET_NAME  32
    #define MAX_PRESET_JSON  2048
    struct {
        char name[MAX_PRESET_NAME];
        char json[MAX_PRESET_JSON];
    } user_presets[MAX_USER_PRESETS];
    int              num_user_presets;
    int              current_user_preset;  /* -1 = none */

    /* Scale — per-slot, "from C" programming */
    #define MAX_SCALE_DEGREES 12
    int              scale_root;       /* MIDI note of root (-1 = none) */
    uint8_t          scale_degrees[MAX_SCALE_DEGREES]; /* semitone offsets from root */
    int              scale_len;        /* 0 = chromatic/off */
    int              scale_program;    /* 1 = programming mode active */

    /* Chord shape — per-slot */
    #define MAX_CHORD_INTERVALS 8
    uint8_t          chord_intervals[MAX_CHORD_INTERVALS]; /* semitone offsets */
    int              chord_len;        /* 0 = off */

    /* Note tracking — maps input note to what was actually sent */
    #define MAX_HELD_NOTES 32
    struct {
        uint8_t input;          /* original input MIDI note */
        uint8_t sent[MAX_CHORD_INTERVALS + 1]; /* notes actually sent to instrument */
        int     sent_count;
        int     active;
    } held_notes[MAX_HELD_NOTES];
} RackSlot;

/* The rack */
typedef struct {
    RackSlot         slots[MAX_SLOTS];
    float            master_volume;
    int              local_mute;    /* 1 = silence ALSA output, only send to bus */
    int              focused_ch;    /* global focused channel — UI follows this */
    int              n_types;       /* number of registered instrument types */
    InstrumentType  *types;         /* array of registered types */
} Rack;

#endif
