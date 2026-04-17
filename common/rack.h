/* miniwave — core rack engine
 *
 * Constants, types, globals, rack management, render mixer,
 * state persistence, limiter, bus write, MIDI dispatch.
 * All platform-independent.
 */

#ifndef MINIWAVE_RACK_H
#define MINIWAVE_RACK_H

#define MINIWAVE_VERSION "0.12.0"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "osc.h"
#include "json-helpers.h"
#include "instruments.h"
#include "seq.h"
#include "keyseq.h"
#include "fm-synth.h"
#include "ym2413.h"
#include "sub-synth.h"
#include "fm-drums.h"
#include "additive.h"
#include "phase-dist.h"
#include "bird.h"
#include "platform.h"

/* ── Constants ──────────────────────────────────────────────────────── */

#define SAMPLE_RATE      48000
#define CHANNELS         2
#define DEFAULT_PERIOD   64
#define DEFAULT_OSC_PORT 9000
#define DEFAULT_HTTP_PORT 8080
#define DEFAULT_MCAST_PORT 9001
#define MCAST_GROUP "239.0.0.42"  /* link-local multicast group */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LIMITER_THRESHOLD 0.7f
#define LIMITER_CEILING   0.95f

/* ── HTTP / SSE Server Constants ───────────────────────────────────── */

#define MAX_HTTP_CLIENTS  16
#define HTTP_BUF_SIZE     16384
#define SSE_BUF_SIZE      32768

/* ── Shared Memory Bus ──────────────────────────────────────────────── */

#define WAVEOS_BUS_SLOTS     8
#define WAVEOS_BUS_RING_SIZE 4096
#define WAVEOS_BUS_SHM_NAME  "/waveos-bus"

typedef struct {
    _Atomic int64_t write_pos;
    float           ring[WAVEOS_BUS_RING_SIZE * 2];
    char            name[32];
    _Atomic int     active;
} WaveosBusSlot;

typedef struct {
    uint32_t        magic;
    int             sample_rate;
    WaveosBusSlot   slots[WAVEOS_BUS_SLOTS];
} WaveosBus;

/* ── Globals ────────────────────────────────────────────────────────── */

static volatile int g_quit = 0;

static void sighandler(int sig) {
    (void)sig;
    g_quit = 1;
}

/* ── MIDI channel routing ───────────────────────────────────────────── */
/* Each incoming MIDI channel routes to a destination:
 *   mode=0 "slot"    → fixed slot (target = slot index)
 *   mode=1 "focused" → follows g_rack.focused_ch
 *   mode=2 "off"     → ignore this channel
 * Default: mode=0, target=ch (legacy 1:1 mapping)
 */
#define MIDI_ROUTE_SLOT    0
#define MIDI_ROUTE_FOCUSED 1
#define MIDI_ROUTE_OFF     2

typedef struct {
    int mode;    /* MIDI_ROUTE_* */
    int target;  /* slot index (for SLOT mode) */
} MidiRoute;

static MidiRoute g_midi_routes[16];

/* Global CC remap table: g_cc_map[input_cc] = output_cc (-1 = pass-through) */
#define CC_MAP_SIZE 128
static int8_t g_cc_map[CC_MAP_SIZE];

static void cc_map_init(void) {
    memset(g_cc_map, -1, sizeof(g_cc_map));
}

static void midi_routes_init(void) {
    for (int i = 0; i < 16; i++) {
        g_midi_routes[i].mode = MIDI_ROUTE_SLOT;
        g_midi_routes[i].target = i;
    }
}

/* Forward declaration — defined after g_rack */
static inline int midi_route_resolve(int midi_ch);

/* ── Audio backend flag (set at startup) ────────────────────────────── */

static int g_use_jack = 0; /* 1 = JACK active, 0 = native fallback */
static int g_period_size = 256; /* audio buffer period size */
static int g_actual_srate = 48000; /* actual sample rate from audio device */
static char g_audio_device[128] = ""; /* audio device name e.g. "hw:0,0" */
static float g_cpu_load = 0.0f; /* audio thread CPU usage 0.0-1.0 */
static int g_bus_active = 0;    /* 1 = shared memory bus connected */
static int g_bus_slot = -1;     /* which bus slot we claimed */
static int g_mcast_active = 0;  /* 1 = multicast broadcasting */

/* ── JSON string escaping (shared utility) ─────────────────────────── */

#ifndef JSON_ESCAPE_DEFINED
#define JSON_ESCAPE_DEFINED
static int json_escape(char *dst, int max, const char *src) {
    int j = 0;
    for (int i = 0; src[i] && j < max - 2; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') { if (j+2>=max) break; dst[j++]='\\'; dst[j++]=c; }
        else if (c == '\n') { if (j+2>=max) break; dst[j++]='\\'; dst[j++]='n'; }
        else if (c == '\r') { if (j+2>=max) break; dst[j++]='\\'; dst[j++]='r'; }
        else if ((unsigned char)c < 0x20) continue;
        else dst[j++] = c;
    }
    dst[j] = '\0';
    return j;
}
#endif

/* ── Deferred free queue (RCU-lite) ─────────────────────────────────── */
/* Slot swaps queue old state here. The render thread drains it at the
 * start of each render_mix call — safe because render is the only
 * consumer that could have been holding a pointer to the old state. */

#define DEFERRED_FREE_MAX 32

typedef struct {
    void *ptr;
    void (*destroy)(void *state);  /* instrument destructor, or NULL for plain free */
} DeferredFree;

static DeferredFree g_deferred_free[DEFERRED_FREE_MAX];
static _Atomic int  g_deferred_free_count = 0;

static void deferred_free_push(void *ptr, void (*destroy)(void *)) {
    int idx = atomic_fetch_add(&g_deferred_free_count, 1);
    if (idx < DEFERRED_FREE_MAX) {
        g_deferred_free[idx].ptr = ptr;
        g_deferred_free[idx].destroy = destroy;
    } else {
        /* Queue full — fall back to immediate free (risky but bounded) */
        if (destroy) destroy(ptr);
        else free(ptr);
        atomic_fetch_sub(&g_deferred_free_count, 1);
    }
}

/* Reader count — HTTP/SSE handlers increment while reading slot state.
 * deferred_free_drain waits for readers to clear before freeing. */
static _Atomic int g_slot_readers = 0;

/* Debug counters — zero-cost when not logging */
static _Atomic uint64_t g_slot_read_enters = 0;
static _Atomic uint64_t g_slot_read_exits = 0;
static _Atomic uint64_t g_deferred_free_delayed = 0;
static _Atomic uint64_t g_deferred_free_drained = 0;

static inline void slot_read_begin(void) {
    atomic_fetch_add(&g_slot_readers, 1);
    atomic_fetch_add(&g_slot_read_enters, 1);
}
static inline void slot_read_end(void) {
    atomic_fetch_sub(&g_slot_readers, 1);
    atomic_fetch_add(&g_slot_read_exits, 1);
}

static void deferred_free_drain(void) {
    /* Don't free while any reader (HTTP/SSE/OSC/MIDI) is active */
    if (atomic_load(&g_slot_readers) > 0) {
        atomic_fetch_add(&g_deferred_free_delayed, 1);
        return;
    }

    int count = atomic_exchange(&g_deferred_free_count, 0);
    for (int i = 0; i < count && i < DEFERRED_FREE_MAX; i++) {
        DeferredFree *df = &g_deferred_free[i];
        if (df->ptr) {
            if (df->destroy) df->destroy(df->ptr);
            free(df->ptr);
            df->ptr = NULL;
            df->destroy = NULL;
            atomic_fetch_add(&g_deferred_free_drained, 1);
        }
    }
}

/* ── Master Limiter ─────────────────────────────────────────────────── */

static float g_master_limiter_env = 0.0f;

static inline float master_limiter(float sample) {
    float absval = fabsf(sample);

    if (absval > g_master_limiter_env)
        g_master_limiter_env += (absval - g_master_limiter_env) * 0.01f;
    else
        g_master_limiter_env += (absval - g_master_limiter_env) * 0.0001f;

    float gain = 1.0f;
    if (g_master_limiter_env > LIMITER_THRESHOLD) {
        gain = LIMITER_THRESHOLD / g_master_limiter_env;
    }

    sample *= gain;

    /* Soft clip — smooth knee at threshold, asymptotic to ceiling.
     * Linear below threshold, tanh-shaped saturation above.
     * Continuous first derivative at the knee. */
    float ax = fabsf(sample);
    if (ax > LIMITER_THRESHOLD) {
        float over = ax - LIMITER_THRESHOLD;
        float knee = LIMITER_CEILING - LIMITER_THRESHOLD;
        float soft = LIMITER_THRESHOLD + knee * tanhf(over / knee);
        sample = (sample > 0.0f) ? soft : -soft;
    }

    return sample;
}

/* ── Instrument Registry ────────────────────────────────────────────── */

#define MAX_INSTRUMENT_TYPES 32

static InstrumentType *g_type_registry[MAX_INSTRUMENT_TYPES];
static int             g_n_types = 0;

/* Forward declarations */
static void state_mark_dirty(void);

static void rack_register_type(InstrumentType *type) {
    if (g_n_types < MAX_INSTRUMENT_TYPES) {
        g_type_registry[g_n_types++] = type;
        fprintf(stderr, "[miniwave] registered type: %s (%s)\n",
                type->name, type->display_name);
    }
}

static int rack_find_type(const char *name) {
    for (int i = 0; i < g_n_types; i++) {
        if (strcmp(g_type_registry[i]->name, name) == 0) return i;
    }
    return -1;
}

/* ── Rack Management ────────────────────────────────────────────────── */

static Rack g_rack;

/* Resolve a MIDI channel to a rack slot index */
static inline int midi_route_resolve(int midi_ch) {
    if (midi_ch < 0 || midi_ch >= 16) return -1;
    MidiRoute *r = &g_midi_routes[midi_ch];
    switch (r->mode) {
    case MIDI_ROUTE_FOCUSED: return g_rack.focused_ch;
    case MIDI_ROUTE_OFF:     return -1;
    default:                 return r->target;
    }
}
static char g_midi_device_name[128] = "";

static void rack_init(void) {
    memset(&g_rack, 0, sizeof(g_rack));
    g_rack.master_volume = 0.8f;
    midi_routes_init();
    cc_map_init();
    for (int i = 0; i < MAX_SLOTS; i++) {
        g_rack.slots[i].active = 0;
        g_rack.slots[i].type_idx = -1;
        g_rack.slots[i].state = NULL;
        g_rack.slots[i].volume = 1.0f;
        g_rack.slots[i].mute = 0;
        g_rack.slots[i].solo = 0;
    }

    /* Register built-in types */
    rack_register_type(&fm_synth_type);
    rack_register_type(&ym2413_type);
    rack_register_type(&sub_synth_type);
    rack_register_type(&fm_drums_type);
    rack_register_type(&additive_type);
    rack_register_type(&phase_dist_type);
    rack_register_type(&bird_type);
}

static void state_save(void);  /* forward decl */
static void scale_chord_midi_shim(void *, uint8_t, uint8_t, uint8_t);  /* forward decl */

static int rack_set_slot(int channel, const char *type_name) {
    if (channel < 0 || channel >= MAX_SLOTS) return -1;

    /* Save current state before destroying old instrument */
    state_save();

    int tidx = rack_find_type(type_name);
    if (tidx < 0) {
        fprintf(stderr, "[miniwave] unknown instrument type: %s\n", type_name);
        return -1;
    }

    RackSlot *slot = &g_rack.slots[channel];

    /* Prepare new state BEFORE touching the slot (avoids race with render thread) */
    InstrumentType *itype = g_type_registry[tidx];
    void *new_state = calloc(1, itype->state_size);
    if (!new_state) return -1;
    itype->init(new_state);

    /* Capture old state */
    void *old_state = slot->state;
    int old_type_idx = slot->type_idx;
    int was_active = slot->active;

    /* Cache old instrument state before destroying it */
    if (was_active && old_state && old_type_idx >= 0
        && old_type_idx < SLOT_CACHE_TYPES) {
        InstrumentType *old_type = g_type_registry[old_type_idx];
        if (old_type->json_save) {
            int n = old_type->json_save(old_state,
                        slot->state_cache[old_type_idx],
                        SLOT_CACHE_SIZE);
            if (n > 0 && n < SLOT_CACHE_SIZE) {
                slot->state_cache_valid[old_type_idx] = 1;
            } else {
                slot->state_cache_valid[old_type_idx] = 0;
                fprintf(stderr, "[miniwave] WARN: state cache overflow for %s (%d bytes)\n",
                        old_type->name, n);
            }
        }
    }

    /* Restore cached state for the new instrument type if available */
    if (tidx < SLOT_CACHE_TYPES
        && slot->state_cache_valid[tidx] && itype->json_load) {
        itype->json_load(new_state, slot->state_cache[tidx]);
        fprintf(stderr, "[miniwave] restored cached state for %s\n", itype->name);
    }

    /* Deactivate slot — atomic store ensures render/MIDI see it immediately */
    atomic_store(&slot->active, 0);
    atomic_fetch_add(&slot->gen, 1);

    /* Wait for in-flight readers to finish.
     * Readers (HTTP/SSE/OSC/MIDI) check active before dereferencing.
     * Once active=0, no new readers enter. Wait for existing ones. */
    int wait_us = 0;
    while (atomic_load(&g_slot_readers) > 0 && wait_us < 100000) {
        usleep(1000);
        wait_us += 1000;
    }
    /* Also wait one audio buffer cycle so render_mix finishes any
     * in-flight render that snapshotted state before active=0 */
    usleep(25000); /* ~1 buffer at 48kHz/1024 */

    /* Capture old slot-level seq/keyseq */
    MiniSeq *old_seq = slot->seq;
    KeySeq  *old_keyseq = slot->keyseq;

    /* Swap in new instrument */
    slot->state = new_state;
    slot->type_idx = tidx;
    slot->volume = 1.0f;
    slot->mute = 0;
    slot->solo = 0;
    slot->cents_mod = 0.0f;
    slot->pitch_bend = 0.0f;
    slot->pitch_bend_range = 2.0f;
    slot->mod_wheel = 0.0f;
    slot->vibrato_phase = 0.0f;

    /* Allocate slot-level seq and keyseq */
    slot->seq = calloc(1, sizeof(MiniSeq));
    slot->keyseq = calloc(1, sizeof(KeySeq));
    if (slot->seq) {
        seq_init(slot->seq);
        seq_bind(slot->seq, new_state, scale_chord_midi_shim, (uint8_t)(channel & 0x0F));
    }
    if (slot->keyseq) {
        keyseq_init(slot->keyseq);
        keyseq_bind(slot->keyseq, new_state, scale_chord_midi_shim,
                     itype->set_param ? itype->set_param : NULL,
                     (uint8_t)(channel & 0x0F));
        slot->keyseq->real_midi_fn = itype->midi;
        slot->keyseq->slot_ptr = slot;
    }

    atomic_fetch_add(&slot->gen, 1);
    atomic_store(&slot->active, 1);

    /* Deferred free — old state cleaned up by render thread next cycle */
    if (was_active && old_state && old_type_idx >= 0) {
        InstrumentType *old_type = g_type_registry[old_type_idx];
        deferred_free_push(old_state, old_type->destroy);
    }
    if (old_seq) deferred_free_push(old_seq, NULL);
    if (old_keyseq) deferred_free_push(old_keyseq, NULL);

    fprintf(stderr, "[miniwave] slot %d = %s\n", channel, itype->display_name);
    state_mark_dirty();
    return 0;
}

static void rack_clear_slot(int channel) {
    if (channel < 0 || channel >= MAX_SLOTS) return;

    RackSlot *slot = &g_rack.slots[channel];
    void *old_state = slot->state;
    int old_type_idx = slot->type_idx;
    int was_active = slot->active;

    atomic_store(&slot->active, 0);
    atomic_fetch_add(&slot->gen, 1);

    /* Wait for readers + one render cycle */
    { int wu = 0; while (atomic_load(&g_slot_readers) > 0 && wu < 100000) { usleep(1000); wu += 1000; } }
    usleep(25000);

    MiniSeq *old_seq = slot->seq;
    KeySeq  *old_keyseq = slot->keyseq;

    slot->state = NULL;
    slot->type_idx = -1;
    slot->volume = 1.0f;
    slot->mute = 0;
    slot->solo = 0;
    slot->seq = NULL;
    slot->keyseq = NULL;
    slot->cents_mod = 0.0f;

    /* Deferred free */
    if (was_active && old_state && old_type_idx >= 0) {
        InstrumentType *itype = g_type_registry[old_type_idx];
        deferred_free_push(old_state, itype->destroy);
    }
    if (old_seq) deferred_free_push(old_seq, NULL);
    if (old_keyseq) deferred_free_push(old_keyseq, NULL);

    fprintf(stderr, "[miniwave] slot %d cleared\n", channel);
    state_mark_dirty();
}

/* ══════════════════════════════════════════════════════════════════════
 *  State Persistence — auto-save/load rack to ~/.config/miniwave/rack.json
 * ══════════════════════════════════════════════════════════════════════ */

static char g_state_path[512] = "";
static char g_patches_path[512] = "";
static char g_keyseq_presets_path[512] = "";
static float g_bpm = 120.0f; /* global tempo */
static volatile int g_state_dirty = 0;

/* ── User patch storage ───────────────────────────────────────────────── */
/* Simple JSON file: array of {name, type, params:{...}} objects.
 * Max 256 patches. Loaded at startup, saved on mutation. */

#define MAX_USER_PATCHES 256
#define PATCH_NAME_MAX 64
#define PATCH_DATA_MAX 2048

typedef struct {
    char name[PATCH_NAME_MAX];
    char type[32];              /* instrument type name */
    char data[PATCH_DATA_MAX];  /* JSON params blob from json_save */
    char keyseq_dsl[512];       /* keyseq DSL string */
    char seq_dsl[256];          /* seq DSL string */
} UserPatch;

static UserPatch g_patches[MAX_USER_PATCHES];
static int g_num_patches = 0;

static void patches_save(void) {
    if (!g_patches_path[0]) return;
    FILE *f = fopen(g_patches_path, "w");
    if (!f) return;
    fprintf(f, "[\n");
    for (int i = 0; i < g_num_patches; i++) {
        char esc_name[128], esc_data[PATCH_DATA_MAX], esc_ks[1024], esc_seq[512];
        json_escape(esc_name, sizeof(esc_name), g_patches[i].name);
        json_escape(esc_data, sizeof(esc_data), g_patches[i].data);
        json_escape(esc_ks, sizeof(esc_ks), g_patches[i].keyseq_dsl);
        json_escape(esc_seq, sizeof(esc_seq), g_patches[i].seq_dsl);
        fprintf(f, "  {\"name\":\"%s\",\"type\":\"%s\",\"params\":\"%s\"",
                esc_name, g_patches[i].type, esc_data);
        if (g_patches[i].keyseq_dsl[0])
            fprintf(f, ",\"keyseq_dsl\":\"%s\"", esc_ks);
        if (g_patches[i].seq_dsl[0])
            fprintf(f, ",\"seq_dsl\":\"%s\"", esc_seq);
        fprintf(f, "}%s\n", (i < g_num_patches - 1) ? "," : "");
    }
    fprintf(f, "]\n");
    fclose(f);
}

static void patches_load(void) {
    if (!g_patches_path[0]) return;
    FILE *f = fopen(g_patches_path, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 512 * 1024) { fclose(f); return; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);

    g_num_patches = 0;
    /* Simple array-of-objects parser */
    const char *p = buf;
    while (g_num_patches < MAX_USER_PATCHES) {
        const char *obj = strchr(p, '{');
        if (!obj) break;
        const char *end = strchr(obj, '}');
        if (!end) break;

        int olen = (int)(end - obj + 1);
        char *ojson = calloc(1, (size_t)(olen + 1));
        if (!ojson) break;
        memcpy(ojson, obj, (size_t)olen);

        UserPatch *up = &g_patches[g_num_patches];
        memset(up, 0, sizeof(*up));
        json_get_string(ojson, "name", up->name, PATCH_NAME_MAX);
        json_get_string(ojson, "type", up->type, sizeof(up->type));
        json_get_string(ojson, "params", up->data, PATCH_DATA_MAX);
        json_get_string(ojson, "keyseq_dsl", up->keyseq_dsl, sizeof(up->keyseq_dsl));
        json_get_string(ojson, "seq_dsl", up->seq_dsl, sizeof(up->seq_dsl));

        if (up->name[0] && up->type[0]) g_num_patches++;
        free(ojson);
        p = end + 1;
    }
    free(buf);
    fprintf(stderr, "[miniwave] loaded %d user patches from %s\n", g_num_patches, g_patches_path);
}

static int patch_find(const char *name) {
    for (int i = 0; i < g_num_patches; i++)
        if (strcmp(g_patches[i].name, name) == 0) return i;
    return -1;
}

/* ── KeySeq preset storage ─────────────────────────────────────────── */

#define MAX_KEYSEQ_PRESETS 64

typedef struct {
    char name[PATCH_NAME_MAX];
    char dsl[512];
} KeySeqPreset;

static KeySeqPreset g_ks_presets[MAX_KEYSEQ_PRESETS];
static int g_num_ks_presets = 0;

static const char *FACTORY_KS_PRESETS[][2] = {
    {"My Sequence",    "t0.125; gated; algo; n:n+1; v:v-0.03; end:v<=0"},
    {"Shimmer",        "t0.125; g0.9; loop; 0,12,7,12; v1,0.7,0.5,0.3"},
    {"Climb",          "t0.125; gated; algo; n:n+1; v:v-0.03; end:v<=0"},
    {"Siren",          "t0.5; gated; algo; n:root+i%2*10-5; v:v; end:v<=0"},
    {"Rapid Fire",     "t0.2; g0.5; gated; algo; n:n; v:v-0.005; end:v<=0"},
    {"Circle of 5ths", "t0.125; gated; algo; n:root+i*7%12; v:v-0.01; end:v<=0"},
    {"Bounce",         "t0.125; gated; algo; n:root+abs(i%8-4)*3; v:v-0.02; end:v<=0"},
    {"Scatter",        "t0.1; g0.8; gated; algo; n:root+i*13%24-12; v:v-0.015; end:v<=0"},
    {NULL, NULL}
};

static void ks_presets_save(void) {
    if (!g_keyseq_presets_path[0]) return;
    FILE *f = fopen(g_keyseq_presets_path, "w");
    if (!f) return;
    fprintf(f, "[\n");
    for (int i = 0; i < g_num_ks_presets; i++) {
        char en[128], ed[1024];
        json_escape(en, sizeof(en), g_ks_presets[i].name);
        json_escape(ed, sizeof(ed), g_ks_presets[i].dsl);
        fprintf(f, "  {\"name\":\"%s\",\"dsl\":\"%s\"}%s\n",
                en, ed, (i < g_num_ks_presets - 1) ? "," : "");
    }
    fprintf(f, "]\n");
    fclose(f);
}

static void ks_presets_load(void) {
    if (!g_keyseq_presets_path[0]) return;
    FILE *f = fopen(g_keyseq_presets_path, "r");
    if (!f) {
        /* Seed factory presets */
        for (int i = 0; FACTORY_KS_PRESETS[i][0] && g_num_ks_presets < MAX_KEYSEQ_PRESETS; i++) {
            strncpy(g_ks_presets[g_num_ks_presets].name, FACTORY_KS_PRESETS[i][0], PATCH_NAME_MAX - 1);
            strncpy(g_ks_presets[g_num_ks_presets].dsl, FACTORY_KS_PRESETS[i][1], 511);
            g_num_ks_presets++;
        }
        ks_presets_save();
        fprintf(stderr, "[miniwave] seeded %d factory keyseq presets\n", g_num_ks_presets);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 128 * 1024) { fclose(f); return; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0'; fclose(f);

    g_num_ks_presets = 0;
    const char *p = buf;
    while (g_num_ks_presets < MAX_KEYSEQ_PRESETS) {
        const char *obj = strchr(p, '{');
        if (!obj) break;
        const char *end = strchr(obj, '}');
        if (!end) break;
        int olen = (int)(end - obj + 1);
        char *oj = calloc(1, (size_t)(olen + 1));
        if (!oj) break;
        memcpy(oj, obj, (size_t)olen);
        KeySeqPreset *kp = &g_ks_presets[g_num_ks_presets];
        memset(kp, 0, sizeof(*kp));
        json_get_string(oj, "name", kp->name, PATCH_NAME_MAX);
        json_get_string(oj, "dsl", kp->dsl, sizeof(kp->dsl));
        if (kp->name[0]) g_num_ks_presets++;
        free(oj);
        p = end + 1;
    }
    free(buf);
    fprintf(stderr, "[miniwave] loaded %d keyseq presets\n", g_num_ks_presets);
}

static int ks_preset_find(const char *name) {
    for (int i = 0; i < g_num_ks_presets; i++)
        if (strcmp(g_ks_presets[i].name, name) == 0) return i;
    return -1;
}

static void sse_mark_dirty(void); /* forward decl */
static void state_mark_dirty(void) { g_state_dirty = 1; sse_mark_dirty(); }

/* SSE push-on-change flags */
static _Atomic int g_sse_rack_dirty = 1;    /* 1 = broadcast rack_status on next check */
static _Atomic int g_sse_detail_dirty = 1;  /* 1 = broadcast ch_status on next check */

/* MIDI event ring buffer for SSE monitor */
#define MIDI_RING_SIZE 64
typedef struct { uint8_t status, d1, d2; } MidiEvent;
static MidiEvent g_midi_ring[MIDI_RING_SIZE];
static _Atomic int g_midi_ring_head = 0;
static _Atomic int g_midi_ring_tail = 0;

static void midi_ring_push(uint8_t status, uint8_t d1, uint8_t d2) {
    int head = atomic_load(&g_midi_ring_head);
    int next = (head + 1) % MIDI_RING_SIZE;
    g_midi_ring[head] = (MidiEvent){status, d1, d2};
    atomic_store(&g_midi_ring_head, next);
}

static void sse_mark_dirty(void) {
    atomic_store(&g_sse_rack_dirty, 1);
    atomic_store(&g_sse_detail_dirty, 1);
}

static void state_init_path(void) {
    const char *cfg = getenv("XDG_CONFIG_HOME");
    if (cfg && cfg[0]) {
        snprintf(g_state_path, sizeof(g_state_path), "%s/miniwave/rack.json", cfg);
        snprintf(g_patches_path, sizeof(g_patches_path), "%s/miniwave/patches.json", cfg);
        snprintf(g_keyseq_presets_path, sizeof(g_keyseq_presets_path), "%s/miniwave/keyseq_presets.json", cfg);
    } else {
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";
        snprintf(g_state_path, sizeof(g_state_path), "%s/.config/miniwave/rack.json", home);
        snprintf(g_patches_path, sizeof(g_patches_path), "%s/.config/miniwave/patches.json", home);
        snprintf(g_keyseq_presets_path, sizeof(g_keyseq_presets_path), "%s/.config/miniwave/keyseq_presets.json", home);
    }
}

static void state_mkdir(void) {
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", g_state_path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        char *s2 = strrchr(dir, '/');
        if (s2) { *s2 = '\0'; mkdir(dir, 0755); *s2 = '/'; }
        mkdir(dir, 0755);
    }
}

static void state_save(void) {
    if (!g_state_path[0]) return;
    state_mkdir();

    FILE *f = fopen(g_state_path, "w");
    if (!f) return;

    /* Save MIDI routes */
    fprintf(f, "{\n  \"master_volume\": %.4f,\n  \"focused_ch\": %d,\n  \"midi_routes\": [",
            (double)g_rack.master_volume, g_rack.focused_ch);
    for (int r = 0; r < 16; r++) {
        fprintf(f, "%s{\"mode\":%d,\"target\":%d}",
                r ? "," : "", g_midi_routes[r].mode, g_midi_routes[r].target);
    }
    fprintf(f, "],\n  \"cc_map\": {");
    int first_cc = 1;
    for (int cc = 0; cc < CC_MAP_SIZE; cc++) {
        if (g_cc_map[cc] >= 0) {
            fprintf(f, "%s\"%d\":%d", first_cc ? "" : ",", cc, g_cc_map[cc]);
            first_cc = 0;
        }
    }
    fprintf(f, "},\n  \"slots\": [\n");

    for (int i = 0; i < MAX_SLOTS; i++) {
        RackSlot *slot = &g_rack.slots[i];
        fprintf(f, "    {");

        if (slot->active && slot->state && slot->type_idx >= 0) {
            InstrumentType *itype = g_type_registry[slot->type_idx];
            fprintf(f, "\"type\":\"%s\",\"volume\":%.4f,\"mute\":%d,\"solo\":%d",
                    itype->name, (double)slot->volume, slot->mute, slot->solo);

            if (itype->json_save) {
                char inst_buf[4096];
                int n = itype->json_save(slot->state, inst_buf, (int)sizeof(inst_buf));
                if (n > 0) fprintf(f, ",%s", inst_buf);
            }
            /* Save slot-level keyseq DSL + enabled state */
            if (slot->keyseq && slot->keyseq->source[0]) {
                char esc[1024];
                json_escape(esc, sizeof(esc), slot->keyseq->source);
                fprintf(f, ",\"keyseq_dsl\":\"%s\",\"keyseq_enabled\":%d",
                        esc, slot->keyseq->enabled ? 1 : 0);
            }
            /* Save slot-level seq DSL */
            if (slot->seq && slot->seq->source[0]) {
                char esc[1024];
                json_escape(esc, sizeof(esc), slot->seq->source);
                fprintf(f, ",\"seq_dsl\":\"%s\"", esc);
            }
            /* Save user presets */
            if (slot->num_user_presets > 0) {
                fprintf(f, ",\"user_presets\":[");
                for (int p = 0; p < slot->num_user_presets; p++) {
                    char esc_name[64];
                    json_escape(esc_name, sizeof(esc_name), slot->user_presets[p].name);
                    fprintf(f, "%s{\"name\":\"%s\",\"data\":{%s}}",
                            p ? "," : "", esc_name, slot->user_presets[p].json);
                }
                fprintf(f, "],\"current_user_preset\":%d", slot->current_user_preset);
            }
            /* Save scale */
            if (slot->scale_len > 0 && slot->scale_root >= 0) {
                fprintf(f, ",\"scale_root\":%d,\"scale\":[", slot->scale_root);
                for (int s = 0; s < slot->scale_len; s++)
                    fprintf(f, "%s%d", s ? "," : "", slot->scale_degrees[s]);
                fprintf(f, "]");
            }
            /* Save chord */
            if (slot->chord_len > 0) {
                fprintf(f, ",\"chord\":[");
                for (int c = 0; c < slot->chord_len; c++)
                    fprintf(f, "%s%d", c ? "," : "", slot->chord_intervals[c]);
                fprintf(f, "]");
            }
        }

        fprintf(f, "}%s\n", (i < MAX_SLOTS - 1) ? "," : "");
    }

    fprintf(f, "  ]\n}\n");
    fclose(f);
}

static void state_load(void) {
    if (!g_state_path[0]) return;

    FILE *f = fopen(g_state_path, "r");
    if (!f) {
        fprintf(stderr, "[miniwave] no saved state at %s\n", g_state_path);
        return;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0 || len > 65536) { fclose(f); return; }
    fseek(f, 0, SEEK_SET);

    char *json = calloc(1, (size_t)(len + 1));
    if (!json) { fclose(f); return; }
    size_t nread = fread(json, 1, (size_t)len, f);
    fclose(f);
    if (nread == 0) { free(json); return; }

    float mv;
    if (json_get_float(json, "master_volume", &mv) == 0)
        g_rack.master_volume = mv;
    int fch = 0;
    if (json_get_int(json, "focused_ch", &fch) == 0 && fch >= 0 && fch < MAX_SLOTS)
        g_rack.focused_ch = fch;

    /* Restore MIDI routes */
    {
        const char *routes = strstr(json, "\"midi_routes\"");
        if (routes) {
            routes = strchr(routes, '[');
            if (routes) {
                routes++;
                for (int r = 0; r < 16; r++) {
                    const char *obj = strchr(routes, '{');
                    if (!obj) break;
                    int mode = 0, target = r;
                    /* Simple parse: find "mode":N,"target":N */
                    const char *m = strstr(obj, "\"mode\"");
                    const char *t = strstr(obj, "\"target\"");
                    if (m) { m = strchr(m, ':'); if (m) mode = atoi(m + 1); }
                    if (t) { t = strchr(t, ':'); if (t) target = atoi(t + 1); }
                    g_midi_routes[r].mode = mode;
                    g_midi_routes[r].target = target;
                    routes = strchr(obj, '}');
                    if (routes) routes++;
                }
            }
        }
    }

    /* Restore global CC map */
    {
        const char *ccm = strstr(json, "\"cc_map\"");
        if (ccm) {
            ccm = strchr(ccm, '{');
            if (ccm) {
                ccm++;
                while (*ccm && *ccm != '}') {
                    while (*ccm == ' ' || *ccm == ',') ccm++;
                    if (*ccm == '"') {
                        int from_cc = atoi(ccm + 1);
                        const char *colon = strchr(ccm, ':');
                        if (colon && from_cc >= 0 && from_cc < CC_MAP_SIZE) {
                            g_cc_map[from_cc] = (int8_t)atoi(colon + 1);
                        }
                        ccm = colon ? colon + 1 : ccm + 1;
                        while (*ccm && *ccm != ',' && *ccm != '}') ccm++;
                    } else break;
                }
            }
        }
    }

    const char *slots_start = strstr(json, "\"slots\"");
    if (!slots_start) { free(json); return; }
    slots_start = strchr(slots_start, '[');
    if (!slots_start) { free(json); return; }
    slots_start++;

    for (int i = 0; i < MAX_SLOTS; i++) {
        const char *obj = strchr(slots_start, '{');
        if (!obj) break;

        int depth = 0;
        const char *end = obj;
        do {
            if (*end == '{') depth++;
            else if (*end == '}') depth--;
            end++;
        } while (depth > 0 && *end);

        int slen = (int)(end - obj);
        char *slot_json = calloc(1, (size_t)(slen + 1));
        if (!slot_json) break;
        memcpy(slot_json, obj, (size_t)slen);

        char type_name[64] = "";
        if (json_get_string(slot_json, "type", type_name, sizeof(type_name)) == 0 && type_name[0]) {
            if (rack_set_slot(i, type_name) == 0) {
                RackSlot *slot = &g_rack.slots[i];
                InstrumentType *itype = g_type_registry[slot->type_idx];

                float vol;
                int ival;
                if (json_get_float(slot_json, "volume", &vol) == 0) slot->volume = vol;
                if (json_get_int(slot_json, "mute", &ival) == 0) slot->mute = ival;
                if (json_get_int(slot_json, "solo", &ival) == 0) slot->solo = ival;

                if (itype->json_load) {
                    itype->json_load(slot->state, slot_json);
                }

                /* Restore slot-level keyseq DSL + enabled state */
                char ks_dsl[512] = "";
                if (slot->keyseq && json_get_string(slot_json, "keyseq_dsl", ks_dsl, sizeof(ks_dsl)) == 0 && ks_dsl[0]) {
                    keyseq_parse(slot->keyseq, ks_dsl);
                    int ks_en = 1;
                    if (json_get_int(slot_json, "keyseq_enabled", &ks_en) == 0)
                        slot->keyseq->enabled = ks_en;
                }
                /* Restore slot-level seq DSL */
                char seq_dsl[512] = "";
                if (slot->seq && json_get_string(slot_json, "seq_dsl", seq_dsl, sizeof(seq_dsl)) == 0 && seq_dsl[0]) {
                    seq_parse(slot->seq, seq_dsl);
                }
                /* Restore scale */
                int scale_root;
                if (json_get_int(slot_json, "scale_root", &scale_root) == 0) {
                    slot->scale_root = scale_root;
                    int degrees[MAX_SCALE_DEGREES];
                    int n = json_get_iarray(slot_json, "scale", degrees, MAX_SCALE_DEGREES);
                    for (int s = 0; s < n; s++) slot->scale_degrees[s] = (uint8_t)degrees[s];
                    slot->scale_len = n;
                }
                /* Restore chord */
                {
                    int intervals[MAX_CHORD_INTERVALS];
                    int n = json_get_iarray(slot_json, "chord", intervals, MAX_CHORD_INTERVALS);
                    for (int c = 0; c < n; c++) slot->chord_intervals[c] = (uint8_t)intervals[c];
                    slot->chord_len = n;
                }
            }
        }

        free(slot_json);
        slots_start = end;
    }

    free(json);
    fprintf(stderr, "[miniwave] state restored from %s\n", g_state_path);
}

/* ── User Preset System ──────────────────────────────────────────────── */

/* Snapshot current instrument state as a user preset */
static void slot_preset_snapshot(RackSlot *slot, InstrumentType *itype) {
    if (!slot->state || !itype->json_save) return;
    if (slot->num_user_presets >= MAX_USER_PRESETS) return;

    int idx = slot->num_user_presets;
    snprintf(slot->user_presets[idx].name, MAX_PRESET_NAME, "preset %d", idx + 1);
    char buf[MAX_PRESET_JSON];
    int len = itype->json_save(slot->state, buf, MAX_PRESET_JSON);
    if (len > 0 && len < MAX_PRESET_JSON) {
        memcpy(slot->user_presets[idx].json, buf, (size_t)len);
        slot->user_presets[idx].json[len] = '\0';
    }
    slot->num_user_presets++;
    slot->current_user_preset = idx;
    fprintf(stderr, "[preset] saved '%s' (slot has %d presets)\n",
            slot->user_presets[idx].name, slot->num_user_presets);
}

/* Load a user preset by index */
static void slot_preset_load(RackSlot *slot, InstrumentType *itype, int idx) {
    if (idx < 0 || idx >= slot->num_user_presets) return;
    if (!slot->state || !itype->json_load) return;

    itype->json_load(slot->state, slot->user_presets[idx].json);
    slot->current_user_preset = idx;
    fprintf(stderr, "[preset] loaded '%s' (%d/%d)\n",
            slot->user_presets[idx].name, idx + 1, slot->num_user_presets);
}

/* CC81 = next preset (or create new) */
static void slot_preset_next(RackSlot *slot, InstrumentType *itype) {
    if (slot->num_user_presets == 0 ||
        slot->current_user_preset >= slot->num_user_presets - 1) {
        /* Past the end — snapshot current state as new preset */
        slot_preset_snapshot(slot, itype);
    } else {
        slot_preset_load(slot, itype, slot->current_user_preset + 1);
    }
}

/* CC80 = prev preset */
static void slot_preset_prev(RackSlot *slot, InstrumentType *itype) {
    if (slot->num_user_presets == 0) {
        slot_preset_snapshot(slot, itype);
        return;
    }
    int prev = slot->current_user_preset - 1;
    if (prev < 0) prev = slot->num_user_presets - 1;
    slot_preset_load(slot, itype, prev);
}

/* ── Scale + Chord Helpers ────────────────────────────────────────── */

/* Quantize a MIDI note to the nearest scale degree */
static inline uint8_t scale_quantize(const RackSlot *slot, uint8_t note) {
    if (slot->scale_len <= 0 || slot->scale_root < 0) return note;
    int root = slot->scale_root;
    int rel = ((int)note - root) % 12;
    if (rel < 0) rel += 12;
    int best = 0, best_dist = 99;
    for (int i = 0; i < slot->scale_len; i++) {
        int d = abs(rel - (int)slot->scale_degrees[i]);
        int d_wrap = 12 - d;
        if (d < best_dist) { best_dist = d; best = slot->scale_degrees[i]; }
        if (d_wrap < best_dist) { best_dist = d_wrap; best = slot->scale_degrees[i]; }
    }
    int octave_offset = (int)note - root;
    int octave = octave_offset / 12;
    if (octave_offset < 0 && (octave_offset % 12) != 0) octave--;
    int result = root + octave * 12 + best;
    if (result < 0) result = 0;
    if (result > 127) result = 127;
    return (uint8_t)result;
}

/* Note tracking — record what was actually sent for each input note */
static inline int held_note_find(RackSlot *slot, uint8_t input) {
    for (int i = 0; i < MAX_HELD_NOTES; i++)
        if (slot->held_notes[i].active && slot->held_notes[i].input == input)
            return i;
    return -1;
}

static inline int held_note_alloc(RackSlot *slot) {
    for (int i = 0; i < MAX_HELD_NOTES; i++)
        if (!slot->held_notes[i].active) return i;
    /* Steal oldest (slot 0) — force release first */
    return 0;
}

static void (*g_midi_broadcast)(int ch, int note, int vel, int is_on) = NULL;

static inline void held_note_release(RackSlot *slot, int idx, InstrumentType *itype, void *st, int ch) {
    if (!slot->held_notes[idx].active) return;
    for (int j = 0; j < slot->held_notes[idx].sent_count; j++) {
        uint8_t n = slot->held_notes[idx].sent[j];
        itype->midi(st, (uint8_t)(0x80 | ch), n, 0);
    }
    if (g_midi_broadcast)
        g_midi_broadcast(ch, slot->held_notes[idx].input, 0, 0);
    slot->held_notes[idx].active = 0;
    slot->held_notes[idx].sent_count = 0;
}

/* ── Scale/chord-aware MIDI shim for keyseq/seq ──────────────────── */

/* Wrapper that keyseq fires through — applies scale quantize + chord fan-out */
static void scale_chord_midi_shim(void *inst_state, uint8_t status, uint8_t d1, uint8_t d2) {
    /* Find which slot this belongs to */
    RackSlot *slot = NULL;
    int ch = status & 0x0F;
    if (ch < MAX_SLOTS && g_rack.slots[ch].state == inst_state)
        slot = &g_rack.slots[ch];

    /* No slot found — scan all slots */
    if (!slot) {
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (g_rack.slots[i].state == inst_state && g_rack.slots[i].type_idx >= 0) {
                slot = &g_rack.slots[i];
                ch = i;
                break;
            }
        }
    }

    /* No scale/chord active — pass through directly */
    if (!slot || slot->type_idx < 0 ||
        (slot->scale_len <= 0 && slot->chord_len <= 0)) {
        if (slot && slot->type_idx >= 0)
            g_type_registry[slot->type_idx]->midi(inst_state, status, d1, d2);
        return;
    }

    InstrumentType *itype = g_type_registry[slot->type_idx];
    uint8_t type = status & 0xF0;

    if (type == 0x90 && d2 > 0) {
        /* Note-on: quantize, chord expand, track */
        uint8_t base = scale_quantize(slot, d1);
        int hi = held_note_alloc(slot);
        if (slot->held_notes[hi].active)
            held_note_release(slot, hi, itype, inst_state, ch);
        slot->held_notes[hi].input = d1;
        slot->held_notes[hi].active = 1;
        slot->held_notes[hi].sent_count = 0;

        if (slot->chord_len > 0) {
            for (int ci = 0; ci < slot->chord_len; ci++) {
                uint8_t cn = base + slot->chord_intervals[ci];
                if (cn > 127) continue;
                slot->held_notes[hi].sent[slot->held_notes[hi].sent_count++] = cn;
                itype->midi(inst_state, status, cn, d2);
            }
        } else {
            slot->held_notes[hi].sent[0] = base;
            slot->held_notes[hi].sent_count = 1;
            itype->midi(inst_state, status, base, d2);
        }
    } else if (type == 0x80 || (type == 0x90 && d2 == 0)) {
        /* Note-off: release all tracked notes for this input */
        int idx = held_note_find(slot, d1);
        if (idx >= 0) {
            held_note_release(slot, idx, itype, inst_state, ch);
        } else {
            /* Not tracked — pass through raw (safety) */
            itype->midi(inst_state, status, d1, d2);
        }
    } else {
        /* Non-note messages pass through */
        itype->midi(inst_state, status, d1, d2);
    }
}

/* ── MIDI dispatch (raw bytes → rack) ──────────────────────────────── */

/* Optional callback for broadcasting MIDI events (set by server after init) */

static inline void midi_dispatch_raw(const uint8_t *data, int len) {
    if (len < 1) return;
    uint8_t status = data[0];
    uint8_t d1_raw = (len > 1) ? data[1] : 0;
    uint8_t d2_raw = (len > 2) ? data[2] : 0;
    midi_ring_push(status, d1_raw, d2_raw);
    int midi_ch = status & 0x0F;
    int ch = midi_route_resolve(midi_ch);
    if (ch < 0 || ch >= MAX_SLOTS) return;

    slot_read_begin();
    RackSlot *slot = &g_rack.slots[ch];
    if (!atomic_load(&slot->active)) { slot_read_end(); return; }
    void *st = slot->state;
    if (!st) { slot_read_end(); return; }
    InstrumentType *itype = g_type_registry[slot->type_idx];

    uint8_t type = status & 0xF0;
    uint8_t d1 = (len > 1) ? data[1] : 0;
    uint8_t d2 = (len > 2) ? data[2] : 0;

    /* Slot-level keyseq intercept — catches notes before instrument sees them */
    KeySeq *ks = slot->keyseq;
    if (ks && !ks->firing && ks->enabled && ks->num_steps > 0) {
        if (type == 0x90 && d2 > 0) {
            if (keyseq_note_on(ks, d1, d2)) {
                if (g_midi_broadcast) g_midi_broadcast(ch, d1, d2, 1);
                slot_read_end(); return;
            }
        } else if (type == 0x80 || (type == 0x90 && d2 == 0)) {
            if (keyseq_note_off(ks, d1)) {
                if (g_midi_broadcast) g_midi_broadcast(ch, d1, 0, 0);
                slot_read_end(); return;
            }
        }
    }

    switch (type) {
    case 0x90:
        if (d2 > 0 && slot->scale_program) {
            /* Scale programming: first note = root, rest = intervals */
            if (slot->scale_root < 0) {
                slot->scale_root = d1;
                slot->scale_degrees[0] = 0;
                slot->scale_len = 1;
            } else {
                int interval = ((int)d1 - slot->scale_root) % 12;
                if (interval < 0) interval += 12;
                int dup = 0;
                for (int i = 0; i < slot->scale_len; i++)
                    if (slot->scale_degrees[i] == (uint8_t)interval) { dup = 1; break; }
                if (!dup && slot->scale_len < MAX_SCALE_DEGREES)
                    slot->scale_degrees[slot->scale_len++] = (uint8_t)interval;
            }
            fprintf(stderr, "[scale] ch%d root=%d degrees=%d\n", ch, slot->scale_root, slot->scale_len);
            state_mark_dirty();
            sse_mark_dirty();
            slot_read_end();
            return;
        }
        if (d2 > 0 && (slot->scale_len > 0 || slot->chord_len > 0)) {
            /* Note-on with scale/chord: quantize, expand, track */
            uint8_t base = scale_quantize(slot, d1);

            if (slot->mono && slot->last_note >= 0 && slot->last_note != d1) {
                int prev = held_note_find(slot, (uint8_t)slot->last_note);
                if (prev >= 0) held_note_release(slot, prev, itype, st, ch);
                else itype->midi(st, (uint8_t)(0x80 | ch), (uint8_t)slot->last_note, 0);
            }
            if (slot->legato && slot->last_note >= 0) {
                slot->glide_from = 440.0f * powf(2.0f, (float)(slot->last_note - 69) / 12.0f);
                slot->glide_to   = 440.0f * powf(2.0f, (float)(base - 69) / 12.0f);
                slot->glide_pos  = 0.0f;
            }
            slot->last_note = d1;

            int hi = held_note_alloc(slot);
            if (slot->held_notes[hi].active)
                held_note_release(slot, hi, itype, st, ch);
            slot->held_notes[hi].input = d1;
            slot->held_notes[hi].active = 1;
            slot->held_notes[hi].sent_count = 0;

            if (slot->chord_len > 0) {
                for (int ci = 0; ci < slot->chord_len; ci++) {
                    uint8_t cn = base + slot->chord_intervals[ci];
                    if (cn > 127) continue;
                    slot->held_notes[hi].sent[slot->held_notes[hi].sent_count++] = cn;
                    itype->midi(st, status, cn, d2);
                }
            } else {
                slot->held_notes[hi].sent[0] = base;
                slot->held_notes[hi].sent_count = 1;
                itype->midi(st, status, base, d2);
            }
            if (g_midi_broadcast) g_midi_broadcast(ch, d1, d2, 1);
        } else if (d2 > 0) {
            /* Normal note-on — no scale/chord, original path */
            if (slot->mono && slot->last_note >= 0 && slot->last_note != d1)
                itype->midi(st, (uint8_t)(0x80 | (status & 0x0F)),
                            (uint8_t)slot->last_note, 0);
            if (slot->legato && slot->last_note >= 0) {
                slot->glide_from = 440.0f * powf(2.0f, (float)(slot->last_note - 69) / 12.0f);
                slot->glide_to   = 440.0f * powf(2.0f, (float)(d1 - 69) / 12.0f);
                slot->glide_pos  = 0.0f;
            }
            slot->last_note = d1;
            itype->midi(st, status, d1, d2);
            if (g_midi_broadcast) g_midi_broadcast(ch, d1, d2, 1);
        } else {
            /* vel=0 note-on = note-off */
            if (slot->mono) slot->last_note = -1;
            int idx = held_note_find(slot, d1);
            if (idx >= 0) held_note_release(slot, idx, itype, st, ch);
            else itype->midi(st, status, d1, d2);
        }
        atomic_store(&g_sse_detail_dirty, 1);
        break;
    case 0x80:
        if (slot->mono && d1 == slot->last_note) slot->last_note = -1;
        {
            int idx = held_note_find(slot, d1);
            if (idx >= 0) held_note_release(slot, idx, itype, st, ch);
            else itype->midi(st, status, d1, d2);
        }
        atomic_store(&g_sse_detail_dirty, 1);
        break;
    case 0xE0: {
        /* Pitch bend: 14-bit value, center=8192 → -1.0 to +1.0 */
        int bend14 = (int)d1 | ((int)d2 << 7);
        slot->pitch_bend = (float)(bend14 - 8192) / 8192.0f;
        itype->midi(st, status, d1, d2);
        break;
    }
    case 0xB0:
        /* Global CC remap */
        if (d1 < CC_MAP_SIZE && g_cc_map[d1] >= 0) {
            d1 = (uint8_t)g_cc_map[d1];
        }
        /* CC1 = mod wheel → vibrato LFO depth */
        if (d1 == 1) {
            slot->mod_wheel = (float)d2 / 127.0f;
        }
        /* CC82 = toggle scale programming mode */
        if (d1 == 82 && d2 == 127) {
            slot->scale_program = !slot->scale_program;
            if (slot->scale_program) {
                slot->scale_root = -1;
                slot->scale_len = 0;
            }
            fprintf(stderr, "[scale] ch%d program mode: %s\n", ch,
                    slot->scale_program ? "ON" : "OFF");
            state_mark_dirty();
            sse_mark_dirty();
            slot_read_end();
            return;
        }
        /* CC80/81 = user preset down/up */
        if (d1 == 80 && d2 == 127) {
            slot_preset_prev(slot, itype);
            state_mark_dirty();
            sse_mark_dirty();
            slot_read_end();
            return;
        }
        if (d1 == 81 && d2 == 127) {
            slot_preset_next(slot, itype);
            state_mark_dirty();
            sse_mark_dirty();
            slot_read_end();
            return;
        }
        /* CC83 = mono toggle */
        if (d1 == 83 && d2 == 127) {
            slot->mono = !slot->mono;
            slot->last_note = -1;
            state_mark_dirty();
            sse_mark_dirty();
            slot_read_end();
            return;
        }
        /* CC84 = legato toggle */
        if (d1 == 84 && d2 == 127) {
            slot->legato = !slot->legato;
            state_mark_dirty();
            sse_mark_dirty();
            slot_read_end();
            return;
        }
        /* CC85 = instrument prev, CC86 = next */
        if ((d1 == 85 || d1 == 86) && d2 == 127) {
            int dir = (d1 == 86) ? 1 : -1;
            int cur = slot->active ? slot->type_idx : -dir;
            int next = (cur + dir + g_n_types) % g_n_types;
            slot_read_end();
            rack_set_slot(ch, g_type_registry[next]->name);
            state_mark_dirty();
            sse_mark_dirty();
            return;
        }
        /* CC87 = focus channel down, CC88 = up */
        if ((d1 == 87 || d1 == 88) && d2 == 127) {
            int dir = (d1 == 88) ? 1 : -1;
            g_rack.focused_ch = (g_rack.focused_ch + dir + MAX_SLOTS) % MAX_SLOTS;
            state_mark_dirty();
            sse_mark_dirty();
            slot_read_end();
            return;
        }
        itype->midi(st, status, d1, d2);
        state_mark_dirty();
        atomic_store(&g_sse_detail_dirty, 1);
        break;
    case 0xC0: case 0xD0:
        itype->midi(st, status, d1, 0);
        state_mark_dirty();
        atomic_store(&g_sse_detail_dirty, 1);
        break;
    }
    slot_read_end();
}

/* ── Render Mixer ──────────────────────────────────────────────────── */

static void render_mix(float *mix_buf, float *slot_buf, int frames, int sample_rate) {
    /* Drain deferred free queue — safe here because render is the
     * thread that was holding pointers to old slot state. */
    deferred_free_drain();

    memset(mix_buf, 0, sizeof(float) * (size_t)(frames * CHANNELS));

    /* Snapshot slot active state — avoid atomic loads in inner loop */
    int slot_active[MAX_SLOTS];
    int any_solo = 0;
    for (int i = 0; i < MAX_SLOTS; i++) {
        slot_active[i] = atomic_load(&g_rack.slots[i].active);
        if (slot_active[i] && g_rack.slots[i].solo) any_solo = 1;
    }

    float buffer_dt = (float)frames / (float)sample_rate;

    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!slot_active[i]) continue;
        RackSlot *slot = &g_rack.slots[i];
        void *st = slot->state;
        if (!st) continue;
        if (slot->mute) continue;
        if (any_solo && !slot->solo) continue;

        InstrumentType *itype = g_type_registry[slot->type_idx];

        /* Snapshot seq/keyseq pointers — stable for this render cycle */
        MiniSeq *s_seq = slot->seq;
        KeySeq  *s_ks  = slot->keyseq;

        /* Tick seq/keyseq once per buffer (not per sample) */
        if (s_seq) seq_tick(s_seq, buffer_dt);
        if (s_ks)  keyseq_tick(s_ks, buffer_dt);

        /* Legato portamento glide → cents_mod */
        if (slot->legato && slot->glide_pos < 1.0f && slot->glide_to > 0.01f) {
            slot->glide_pos += buffer_dt * slot->glide_rate;
            if (slot->glide_pos > 1.0f) slot->glide_pos = 1.0f;
            float t = slot->glide_pos;
            t = t * t * (3.0f - 2.0f * t);
            float freq = slot->glide_from + (slot->glide_to - slot->glide_from) * t;
            float cents = 1200.0f * log2f(freq / slot->glide_to);
            slot->cents_mod += cents;
        }

        /* Pitch bend → cents */
        if (slot->pitch_bend != 0.0f) {
            slot->cents_mod += slot->pitch_bend * slot->pitch_bend_range * 100.0f;
        }

        /* Mod wheel → vibrato LFO (5.5 Hz sine, depth scaled by mod_wheel) */
        if (slot->mod_wheel > 0.001f) {
            slot->vibrato_phase += 5.5f * buffer_dt * 6.2831853f;
            if (slot->vibrato_phase > 6.2831853f)
                slot->vibrato_phase -= 6.2831853f;
            slot->cents_mod += sinf(slot->vibrato_phase) * slot->mod_wheel * 50.0f;
        }

        /* Copy keyseq cents_mod to slot, then to instrument via type_idx */
        if (s_ks) slot->cents_mod += s_ks->cents_mod;
        {
            float cm = slot->cents_mod;
            switch (slot->type_idx) {
            case 0: ((FMSynth *)st)->cents_mod = cm; break;
            case 1: ((YM2413State *)st)->cents_mod = cm; break;
            case 2: ((SubSynth *)st)->cents_mod = cm; break;
            case 3: ((FMDrumState *)st)->cents_mod = cm; break;
            case 4: ((AdditiveState *)st)->cents_mod = cm; break;
            case 5: ((PhaseDistState *)st)->cents_mod = cm; break;
            case 6: ((BirdState *)st)->cents_mod = cm; break;
            }
        }
        slot->cents_mod = 0;

        memset(slot_buf, 0, sizeof(float) * (size_t)(frames * CHANNELS));
        itype->render(st, slot_buf, frames, sample_rate);

        float vol = slot->volume;
        float pk_l = 0, pk_r = 0;
        for (int j = 0; j < frames; j++) {
            float l = slot_buf[j*2] * vol;
            float r = slot_buf[j*2+1] * vol;
            mix_buf[j*2] += l;
            mix_buf[j*2+1] += r;
            float al = fabsf(l), ar = fabsf(r);
            if (al > pk_l) pk_l = al;
            if (ar > pk_r) pk_r = ar;
        }
        /* Peak hold with decay + persistent hold */
        slot->peak_l = (pk_l > slot->peak_l) ? pk_l : slot->peak_l * 0.95f;
        slot->peak_r = (pk_r > slot->peak_r) ? pk_r : slot->peak_r * 0.95f;
        if (pk_l > slot->hold_l) slot->hold_l = pk_l;
        if (pk_r > slot->hold_r) slot->hold_r = pk_r;
    }

    float mv = g_rack.master_volume;
    float mpk_l = 0, mpk_r = 0;
    for (int j = 0; j < frames; j++) {
        mix_buf[j*2] *= mv;
        mix_buf[j*2+1] *= mv;
        mix_buf[j*2] = master_limiter(mix_buf[j*2]);
        mix_buf[j*2+1] = master_limiter(mix_buf[j*2+1]);
        float al = fabsf(mix_buf[j*2]), ar = fabsf(mix_buf[j*2+1]);
        if (al > mpk_l) mpk_l = al;
        if (ar > mpk_r) mpk_r = ar;
    }
    g_rack.master_peak_l = (mpk_l > g_rack.master_peak_l) ? mpk_l : g_rack.master_peak_l * 0.95f;
    g_rack.master_peak_r = (mpk_r > g_rack.master_peak_r) ? mpk_r : g_rack.master_peak_r * 0.95f;
    if (mpk_l > g_rack.master_hold_l) g_rack.master_hold_l = mpk_l;
    if (mpk_r > g_rack.master_hold_r) g_rack.master_hold_r = mpk_r;

    /* Accumulate scope buffer across callbacks until SCOPE_SIZE frames */
    {
        int to_copy = frames;
        int src_off = 0;
        if (g_rack.scope_pos + to_copy > SCOPE_SIZE) {
            /* Would overflow — take the tail end that fits */
            src_off = (g_rack.scope_pos + to_copy - SCOPE_SIZE) * 2;
            to_copy = SCOPE_SIZE - g_rack.scope_pos;
        }
        if (to_copy > 0) {
            memcpy(g_rack.scope_buf + g_rack.scope_pos * 2,
                   mix_buf + src_off, (size_t)(to_copy * 2) * sizeof(float));
            g_rack.scope_pos += to_copy;
        }
        if (g_rack.scope_pos >= SCOPE_SIZE) {
            g_rack.scope_ready = 1;
            g_rack.scope_pos = 0;
        }
    }
}

/* ── Bus Write ──────────────────────────────────────────────────────── */

static void bus_write(WaveosBus *bus, int slot, float *stereo_frames, int count) {
    WaveosBusSlot *s = &bus->slots[slot];
    int64_t wp = atomic_load(&s->write_pos);
    for (int i = 0; i < count; i++) {
        int idx = (int)((wp + i) % WAVEOS_BUS_RING_SIZE) * 2;
        s->ring[idx]     = stereo_frames[i * 2];
        s->ring[idx + 1] = stereo_frames[i * 2 + 1];
    }
    atomic_store(&s->write_pos, wp + count);
}

#endif
