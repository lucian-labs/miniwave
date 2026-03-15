/* miniwave — modular rack host for waveOS
 *
 * 16 instrument slots, MIDI-by-channel routing, stereo mix,
 * OSC control surface on UDP port 9000.
 */

#define _GNU_SOURCE
#define __USE_MISC
#define ALSA_PCM_NEW_HW_PARAMS_API

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <alsa/asoundlib.h>

/* ── OSC helpers (defined before fm-synth.h which uses them) ────────── */

#define OSC_BUF_SIZE 2048

static inline int osc_pad4(int n) { return (n + 3) & ~3; }

static inline int32_t osc_read_i32(const uint8_t *b) {
    return (int32_t)((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
}

static inline float osc_read_f32(const uint8_t *b) {
    union { uint32_t u; float f; } conv;
    conv.u = (uint32_t)((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
    return conv.f;
}

static inline void osc_write_i32(uint8_t *b, int32_t v) {
    b[0] = (v >> 24) & 0xFF;
    b[1] = (v >> 16) & 0xFF;
    b[2] = (v >> 8)  & 0xFF;
    b[3] =  v        & 0xFF;
}

static inline void osc_write_f32(uint8_t *b, float v) {
    union { uint32_t u; float f; } conv;
    conv.f = v;
    osc_write_i32(b, (int32_t)conv.u);
}

static int osc_write_string(uint8_t *buf, int max, const char *str) {
    int len = (int)strlen(str) + 1;
    int padded = osc_pad4(len);
    if (padded > max) return -1;
    memcpy(buf, str, (size_t)len);
    memset(buf + len, 0, (size_t)(padded - len));
    return padded;
}

/* ── Instrument headers ─────────────────────────────────────────────── */

#include "instruments.h"
#include "fm-synth.h"

/* ── Constants ──────────────────────────────────────────────────────── */

#define SAMPLE_RATE      48000
#define CHANNELS         2
#define DEFAULT_PERIOD   64
#define DEFAULT_OSC_PORT 9000

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LIMITER_THRESHOLD 0.7f
#define LIMITER_CEILING   0.95f

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

    if (fabsf(sample) > LIMITER_THRESHOLD) {
        sample = tanhf(sample) * LIMITER_CEILING;
    }

    return sample;
}

/* ── Instrument Registry ────────────────────────────────────────────── */

#define MAX_INSTRUMENT_TYPES 32

static InstrumentType *g_type_registry[MAX_INSTRUMENT_TYPES];
static int             g_n_types = 0;

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
static char g_midi_device_name[128] = "";

static void rack_init(void) {
    memset(&g_rack, 0, sizeof(g_rack));
    g_rack.master_volume = 0.8f;
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
}

static int rack_set_slot(int channel, const char *type_name) {
    if (channel < 0 || channel >= MAX_SLOTS) return -1;

    int tidx = rack_find_type(type_name);
    if (tidx < 0) {
        fprintf(stderr, "[miniwave] unknown instrument type: %s\n", type_name);
        return -1;
    }

    RackSlot *slot = &g_rack.slots[channel];

    /* Clear existing instrument if any */
    if (slot->active && slot->state) {
        InstrumentType *old_type = g_type_registry[slot->type_idx];
        old_type->destroy(slot->state);
        free(slot->state);
        slot->state = NULL;
        slot->active = 0;
    }

    InstrumentType *itype = g_type_registry[tidx];
    void *state = calloc(1, itype->state_size);
    if (!state) return -1;

    itype->init(state);

    slot->state = state;
    slot->type_idx = tidx;
    slot->volume = 1.0f;
    slot->mute = 0;
    slot->solo = 0;
    slot->active = 1;

    fprintf(stderr, "[miniwave] slot %d = %s\n", channel, itype->display_name);
    return 0;
}

static void rack_clear_slot(int channel) {
    if (channel < 0 || channel >= MAX_SLOTS) return;

    RackSlot *slot = &g_rack.slots[channel];
    if (slot->active && slot->state) {
        InstrumentType *itype = g_type_registry[slot->type_idx];
        itype->destroy(slot->state);
        free(slot->state);
    }
    slot->state = NULL;
    slot->type_idx = -1;
    slot->active = 0;
    slot->volume = 1.0f;
    slot->mute = 0;
    slot->solo = 0;

    fprintf(stderr, "[miniwave] slot %d cleared\n", channel);
}

/* ── ALSA MIDI Auto-detect ──────────────────────────────────────────── */

static int find_midi_device(char *out, size_t out_len, char *name_out, size_t name_len) {
    int card = -1;
    while (snd_card_next(&card) >= 0 && card >= 0) {
        snd_ctl_t *ctl = NULL;
        char cname[32];
        snprintf(cname, sizeof(cname), "hw:%d", card);
        if (snd_ctl_open(&ctl, cname, 0) < 0) continue;

        int device = -1;
        while (snd_ctl_rawmidi_next_device(ctl, &device) >= 0 && device >= 0) {
            snd_rawmidi_info_t *info;
            snd_rawmidi_info_alloca(&info);
            snd_rawmidi_info_set_device(info, (unsigned int)device);
            snd_rawmidi_info_set_subdevice(info, 0);
            snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);

            if (snd_ctl_rawmidi_info(ctl, info) >= 0) {
                const char *devname = snd_rawmidi_info_get_name(info);
                if (devname && strstr(devname, "Midi Through") == NULL) {
                    snprintf(out, out_len, "hw:%d,%d", card, device);
                    if (name_out) snprintf(name_out, name_len, "%s", devname);
                    snd_ctl_close(ctl);
                    fprintf(stderr, "[miniwave] MIDI: %s (%s)\n", out, devname);
                    return 0;
                }
            }
        }
        snd_ctl_close(ctl);
    }
    return -1;
}

/* List all available MIDI devices. Returns count. */
static int list_midi_devices(char devices[][64], char names[][128], int max_devices) {
    int count = 0;
    int card = -1;
    while (snd_card_next(&card) >= 0 && card >= 0 && count < max_devices) {
        snd_ctl_t *ctl = NULL;
        char cname[32];
        snprintf(cname, sizeof(cname), "hw:%d", card);
        if (snd_ctl_open(&ctl, cname, 0) < 0) continue;

        int device = -1;
        while (snd_ctl_rawmidi_next_device(ctl, &device) >= 0 && device >= 0
               && count < max_devices) {
            snd_rawmidi_info_t *info;
            snd_rawmidi_info_alloca(&info);
            snd_rawmidi_info_set_device(info, (unsigned int)device);
            snd_rawmidi_info_set_subdevice(info, 0);
            snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);

            if (snd_ctl_rawmidi_info(ctl, info) >= 0) {
                const char *devname = snd_rawmidi_info_get_name(info);
                snprintf(devices[count], 64, "hw:%d,%d", card, device);
                snprintf(names[count], 128, "%s", devname ? devname : "Unknown");
                count++;
            }
        }
        snd_ctl_close(ctl);
    }
    return count;
}

/* ── MIDI Thread ────────────────────────────────────────────────────── */

typedef struct {
    snd_rawmidi_t *midi_in;
} MidiThreadCtx;

static void *midi_thread_fn(void *arg) {
    MidiThreadCtx *ctx = (MidiThreadCtx *)arg;
    uint8_t midi_byte = 0;
    uint8_t midi_msg[3] = {0};
    int midi_pos = 0;
    int midi_expected = 0;
    uint8_t midi_running = 0;

    int npfds = snd_rawmidi_poll_descriptors_count(ctx->midi_in);
    struct pollfd *pfds = calloc((size_t)npfds, sizeof(struct pollfd));
    if (!pfds) return NULL;
    snd_rawmidi_poll_descriptors(ctx->midi_in, pfds, (unsigned int)npfds);

    while (!g_quit) {
        int ret = poll(pfds, (nfds_t)npfds, 50);
        if (ret <= 0) continue;

        for (;;) {
            ssize_t r = snd_rawmidi_read(ctx->midi_in, &midi_byte, 1);
            if (r <= 0) break;

            /* Skip realtime */
            if (midi_byte >= 0xF8) continue;
            /* Skip system common */
            if (midi_byte >= 0xF0) { midi_pos = 0; continue; }

            if (midi_byte & 0x80) {
                midi_msg[0] = midi_byte;
                midi_running = midi_byte;
                midi_pos = 1;
                uint8_t type = midi_byte & 0xF0;
                midi_expected = (type == 0xC0 || type == 0xD0) ? 2 : 3;
            } else if (midi_running) {
                if (midi_pos == 0) {
                    midi_msg[0] = midi_running;
                    midi_pos = 1;
                    uint8_t type = midi_running & 0xF0;
                    midi_expected = (type == 0xC0 || type == 0xD0) ? 2 : 3;
                }
                midi_msg[midi_pos++] = midi_byte;
            }

            if (midi_pos >= midi_expected && midi_expected > 0) {
                /* Route by channel */
                int channel = midi_msg[0] & 0x0F;
                RackSlot *slot = &g_rack.slots[channel];
                if (slot->active && slot->state) {
                    InstrumentType *itype = g_type_registry[slot->type_idx];
                    itype->midi(slot->state, midi_msg[0],
                                midi_expected >= 2 ? midi_msg[1] : 0,
                                midi_expected >= 3 ? midi_msg[2] : 0);
                }
                midi_pos = 0;
            }
        }
    }

    free(pfds);
    return NULL;
}

/* ── OSC Server Thread ──────────────────────────────────────────────── */

typedef struct {
    int          port;
    snd_rawmidi_t **midi_in_ptr; /* pointer so we can swap devices */
    char         *midi_dev;      /* current MIDI device string */
} OscThreadCtx;

static void *osc_thread_fn(void *arg) {
    OscThreadCtx *ctx = (OscThreadCtx *)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "[miniwave] OSC: socket error: %s\n", strerror(errno));
        return NULL;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)ctx->port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[miniwave] OSC: bind error on port %d: %s\n",
                ctx->port, strerror(errno));
        close(sock);
        return NULL;
    }

    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    fprintf(stderr, "[miniwave] OSC listening on port %d\n", ctx->port);

    uint8_t buf[OSC_BUF_SIZE];
    struct sockaddr_in sender;
    socklen_t sender_len = 0;

    while (!g_quit) {
        sender_len = sizeof(sender);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr *)&sender, &sender_len);
        if (n <= 0) continue;

        /* Parse OSC address */
        if (buf[0] != '/') continue;
        int addr_end = 0;
        while (addr_end < n && buf[addr_end] != '\0') addr_end++;
        if (addr_end >= n) continue;
        const char *osc_addr = (const char *)buf;
        int pos = osc_pad4(addr_end + 1);

        /* Parse type tag string */
        const char *types = "";
        int n_args = 0;
        if (pos < n && buf[pos] == ',') {
            types = (const char *)&buf[pos + 1];
            int tag_end = pos;
            while (tag_end < n && buf[tag_end] != '\0') tag_end++;
            n_args = tag_end - pos - 1;
            pos = osc_pad4(tag_end + 1);
        }

        /* Read args */
        int32_t arg_i[8] = {0};
        float   arg_f[8] = {0};
        char    arg_s[4][128];
        int ai = 0, afi = 0, asi = 0;
        memset(arg_s, 0, sizeof(arg_s));

        for (int t = 0; t < n_args && pos < (int)n; t++) {
            switch (types[t]) {
            case 'i':
                if (pos + 4 <= (int)n && ai < 8)
                    arg_i[ai++] = osc_read_i32(&buf[pos]);
                pos += 4;
                break;
            case 'f':
                if (pos + 4 <= (int)n && afi < 8)
                    arg_f[afi++] = osc_read_f32(&buf[pos]);
                pos += 4;
                break;
            case 's': {
                int sstart = pos;
                while (pos < (int)n && buf[pos] != '\0') pos++;
                if (asi < 4) {
                    int slen = pos - sstart;
                    if (slen > 127) slen = 127;
                    memcpy(arg_s[asi], &buf[sstart], (size_t)slen);
                    arg_s[asi][slen] = '\0';
                    asi++;
                }
                pos = osc_pad4(pos + 1);
            } break;
            default:
                break;
            }
        }

        /* ── Dispatch ───────────────────────────────────────────── */

        /* /rack/slot/set  is  — set channel to instrument type */
        if (strcmp(osc_addr, "/rack/slot/set") == 0 && ai >= 1 && asi >= 1) {
            rack_set_slot(arg_i[0], arg_s[0]);
        }
        /* /rack/slot/clear  i */
        else if (strcmp(osc_addr, "/rack/slot/clear") == 0 && ai >= 1) {
            rack_clear_slot(arg_i[0]);
        }
        /* /rack/slot/volume  if */
        else if (strcmp(osc_addr, "/rack/slot/volume") == 0 && ai >= 1 && afi >= 1) {
            int ch = arg_i[0];
            if (ch >= 0 && ch < MAX_SLOTS) {
                float vol = arg_f[0];
                if (vol < 0.0f) vol = 0.0f;
                if (vol > 1.0f) vol = 1.0f;
                g_rack.slots[ch].volume = vol;
            }
        }
        /* /rack/slot/mute  ii */
        else if (strcmp(osc_addr, "/rack/slot/mute") == 0 && ai >= 2) {
            int ch = arg_i[0];
            if (ch >= 0 && ch < MAX_SLOTS) {
                g_rack.slots[ch].mute = arg_i[1] ? 1 : 0;
            }
        }
        /* /rack/slot/solo  ii */
        else if (strcmp(osc_addr, "/rack/slot/solo") == 0 && ai >= 2) {
            int ch = arg_i[0];
            if (ch >= 0 && ch < MAX_SLOTS) {
                g_rack.slots[ch].solo = arg_i[1] ? 1 : 0;
            }
        }
        /* /rack/master  f */
        else if (strcmp(osc_addr, "/rack/master") == 0 && afi >= 1) {
            float vol = arg_f[0];
            if (vol < 0.0f) vol = 0.0f;
            if (vol > 1.0f) vol = 1.0f;
            g_rack.master_volume = vol;
        }
        /* /rack/status */
        else if (strcmp(osc_addr, "/rack/status") == 0) {
            uint8_t reply[OSC_BUF_SIZE];
            int rpos = 0;
            int w;

            w = osc_write_string(reply, (int)sizeof(reply), "/rack/status");
            if (w < 0) continue;
            rpos += w;

            /* Build type tag: for each slot isifi, then f (master_volume) s (midi_device) */
            char ttag[256] = ",";
            int tpos = 1;
            for (int i = 0; i < MAX_SLOTS; i++) {
                /* active(i) type_name(s) volume(f) mute(i) solo(i) */
                ttag[tpos++] = 'i';
                ttag[tpos++] = 's';
                ttag[tpos++] = 'f';
                ttag[tpos++] = 'i';
                ttag[tpos++] = 'i';
            }
            ttag[tpos++] = 'f'; /* master_volume */
            ttag[tpos++] = 's'; /* midi_device */
            ttag[tpos] = '\0';

            w = osc_write_string(reply + rpos, (int)sizeof(reply) - rpos, ttag);
            if (w < 0) continue;
            rpos += w;

            /* Slot data */
            for (int i = 0; i < MAX_SLOTS; i++) {
                RackSlot *slot = &g_rack.slots[i];
                if (rpos + 4 > (int)sizeof(reply)) break;
                osc_write_i32(reply + rpos, slot->active); rpos += 4;

                const char *tname = "";
                if (slot->active && slot->type_idx >= 0 && slot->type_idx < g_n_types)
                    tname = g_type_registry[slot->type_idx]->name;
                w = osc_write_string(reply + rpos, (int)sizeof(reply) - rpos, tname);
                if (w < 0) break;
                rpos += w;

                if (rpos + 12 > (int)sizeof(reply)) break;
                osc_write_f32(reply + rpos, slot->volume); rpos += 4;
                osc_write_i32(reply + rpos, slot->mute);   rpos += 4;
                osc_write_i32(reply + rpos, slot->solo);   rpos += 4;
            }

            if (rpos + 4 <= (int)sizeof(reply)) {
                osc_write_f32(reply + rpos, g_rack.master_volume); rpos += 4;
            }
            w = osc_write_string(reply + rpos, (int)sizeof(reply) - rpos, g_midi_device_name);
            if (w >= 0) rpos += w;

            sendto(sock, reply, (size_t)rpos, 0,
                   (struct sockaddr *)&sender, sender_len);
        }
        /* /rack/types */
        else if (strcmp(osc_addr, "/rack/types") == 0) {
            uint8_t reply[OSC_BUF_SIZE];
            int rpos = 0;
            int w;

            w = osc_write_string(reply, (int)sizeof(reply), "/rack/types");
            if (w < 0) continue;
            rpos += w;

            /* Type tag: one 's' per type */
            char ttag[128] = ",";
            int tpos = 1;
            for (int i = 0; i < g_n_types && tpos < 126; i++) {
                ttag[tpos++] = 's';
            }
            ttag[tpos] = '\0';

            w = osc_write_string(reply + rpos, (int)sizeof(reply) - rpos, ttag);
            if (w < 0) continue;
            rpos += w;

            for (int i = 0; i < g_n_types; i++) {
                w = osc_write_string(reply + rpos, (int)sizeof(reply) - rpos,
                                     g_type_registry[i]->name);
                if (w < 0) break;
                rpos += w;
            }

            sendto(sock, reply, (size_t)rpos, 0,
                   (struct sockaddr *)&sender, sender_len);
        }
        /* /midi/devices */
        else if (strcmp(osc_addr, "/midi/devices") == 0) {
            char devs[16][64];
            char devnames[16][128];
            int ndevs = list_midi_devices(devs, devnames, 16);

            uint8_t reply[OSC_BUF_SIZE];
            int rpos = 0;
            int w;

            w = osc_write_string(reply, (int)sizeof(reply), "/midi/devices");
            if (w < 0) continue;
            rpos += w;

            char ttag[128] = ",";
            int tpos = 1;
            for (int i = 0; i < ndevs && tpos < 124; i++) {
                ttag[tpos++] = 's';
                ttag[tpos++] = 's';
            }
            ttag[tpos] = '\0';

            w = osc_write_string(reply + rpos, (int)sizeof(reply) - rpos, ttag);
            if (w < 0) continue;
            rpos += w;

            for (int i = 0; i < ndevs; i++) {
                w = osc_write_string(reply + rpos, (int)sizeof(reply) - rpos, devs[i]);
                if (w < 0) break;
                rpos += w;
                w = osc_write_string(reply + rpos, (int)sizeof(reply) - rpos, devnames[i]);
                if (w < 0) break;
                rpos += w;
            }

            sendto(sock, reply, (size_t)rpos, 0,
                   (struct sockaddr *)&sender, sender_len);
        }
        /* /midi/device  s  — switch MIDI input */
        else if (strcmp(osc_addr, "/midi/device") == 0 && asi >= 1) {
            fprintf(stderr, "[miniwave] OSC MIDI device switch to: %s\n", arg_s[0]);
            /* Close old */
            if (ctx->midi_in_ptr && *ctx->midi_in_ptr) {
                snd_rawmidi_close(*ctx->midi_in_ptr);
                *ctx->midi_in_ptr = NULL;
            }
            /* Open new */
            snd_rawmidi_t *new_midi = NULL;
            int err = snd_rawmidi_open(&new_midi, NULL, arg_s[0],
                                       SND_RAWMIDI_NONBLOCK);
            if (err < 0) {
                fprintf(stderr, "[miniwave] can't open MIDI %s: %s\n",
                        arg_s[0], snd_strerror(err));
            } else {
                if (ctx->midi_in_ptr) *ctx->midi_in_ptr = new_midi;
                snprintf(ctx->midi_dev, 64, "%s", arg_s[0]);
                snprintf(g_midi_device_name, sizeof(g_midi_device_name), "%s", arg_s[0]);
                fprintf(stderr, "[miniwave] MIDI switched to %s\n", arg_s[0]);
            }
        }
        /* /ch/N/... — per-instrument OSC */
        else if (strncmp(osc_addr, "/ch/", 4) == 0) {
            /* Parse channel number */
            const char *p = osc_addr + 4;
            char *end = NULL;
            long ch = strtol(p, &end, 10);
            if (end && end != p && ch >= 0 && ch < MAX_SLOTS) {
                const char *sub_path = end; /* e.g. "/preset", "/param/mod_index", "/status" */
                RackSlot *slot = &g_rack.slots[ch];

                if (slot->active && slot->state) {
                    if (strcmp(sub_path, "/status") == 0) {
                        /* Call instrument's osc_status */
                        InstrumentType *itype = g_type_registry[slot->type_idx];
                        uint8_t reply[512];
                        int rlen = itype->osc_status(slot->state, reply, (int)sizeof(reply));
                        if (rlen > 0) {
                            sendto(sock, reply, (size_t)rlen, 0,
                                   (struct sockaddr *)&sender, sender_len);
                        }
                    } else {
                        /* Forward to instrument's osc_handle */
                        InstrumentType *itype = g_type_registry[slot->type_idx];
                        itype->osc_handle(slot->state, sub_path,
                                          arg_i, ai, arg_f, afi);
                    }
                }
            }
        }
    }

    close(sock);
    return NULL;
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

/* ── Usage ──────────────────────────────────────────────────────────── */

static void usage(const char *prog) {
    fprintf(stderr,
        "miniwave — modular rack host for waveOS\n"
        "Usage: %s [options]\n"
        "  -m DEV    ALSA MIDI input (default: auto-detect)\n"
        "  -o DEV    ALSA audio output (default: hw:0,0)\n"
        "  -c N      Pre-configure channels 1-N with FM synth (default: 0)\n"
        "  -O PORT   OSC port (default: 9000)\n"
        "  -P SIZE   Audio period size (default: 64)\n"
        "  -h        Help\n", prog);
}

/* ── Main ───────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    char midi_dev[64] = "";
    char audio_dev[64] = "hw:0,0";
    int pre_config = 0;
    int osc_port = DEFAULT_OSC_PORT;
    int period_size = DEFAULT_PERIOD;

    int opt;
    while ((opt = getopt(argc, argv, "m:o:c:O:P:h")) != -1) {
        switch (opt) {
        case 'm': strncpy(midi_dev, optarg, sizeof(midi_dev) - 1); break;
        case 'o': strncpy(audio_dev, optarg, sizeof(audio_dev) - 1); break;
        case 'c': pre_config = atoi(optarg); break;
        case 'O': osc_port = atoi(optarg); break;
        case 'P': period_size = atoi(optarg); break;
        case 'h': /* fall through */
        default:
            usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

    if (pre_config < 0) pre_config = 0;
    if (pre_config > MAX_SLOTS) pre_config = MAX_SLOTS;

    /* ── Init rack ─────────────────────────────────────────────── */

    rack_init();

    /* Pre-configure channels with FM synth */
    for (int i = 0; i < pre_config; i++) {
        rack_set_slot(i, "fm-synth");
    }

    /* ── ALSA MIDI ─────────────────────────────────────────────── */

    if (midi_dev[0] == '\0') {
        char detected_name[128] = "";
        if (find_midi_device(midi_dev, sizeof(midi_dev),
                             detected_name, sizeof(detected_name)) == 0) {
            snprintf(g_midi_device_name, sizeof(g_midi_device_name), "%s", detected_name);
        } else {
            fprintf(stderr, "[miniwave] WARN: no MIDI device found\n");
        }
    } else {
        snprintf(g_midi_device_name, sizeof(g_midi_device_name), "%s", midi_dev);
    }

    snd_rawmidi_t *midi_in = NULL;
    if (midi_dev[0] != '\0') {
        int err = snd_rawmidi_open(&midi_in, NULL, midi_dev, SND_RAWMIDI_NONBLOCK);
        if (err < 0) {
            fprintf(stderr, "[miniwave] WARN: can't open MIDI %s: %s\n",
                    midi_dev, snd_strerror(err));
            midi_in = NULL;
        }
    }

    /* ── ALSA Audio ────────────────────────────────────────────── */

    snd_pcm_t *pcm = NULL;
    int err = snd_pcm_open(&pcm, audio_dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "[miniwave] ERROR: can't open audio %s: %s\n",
                audio_dev, snd_strerror(err));
        if (midi_in) snd_rawmidi_close(midi_in);
        return 1;
    }

    {
        snd_pcm_hw_params_t *hw;
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(pcm, hw);
        snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
        unsigned int rate = SAMPLE_RATE;
        snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, NULL);
        snd_pcm_hw_params_set_channels(pcm, hw, CHANNELS);
        snd_pcm_uframes_t ps = (snd_pcm_uframes_t)period_size;
        snd_pcm_hw_params_set_period_size_near(pcm, hw, &ps, NULL);
        snd_pcm_uframes_t bs = ps * 2;
        snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &bs);

        err = snd_pcm_hw_params(pcm, hw);
        if (err < 0) {
            fprintf(stderr, "[miniwave] ERROR: hw_params: %s\n", snd_strerror(err));
            snd_pcm_close(pcm);
            if (midi_in) snd_rawmidi_close(midi_in);
            return 1;
        }

        snd_pcm_hw_params_get_period_size(hw, &ps, NULL);
        period_size = (int)ps;
        fprintf(stderr, "[miniwave] audio: %s @ %uHz period=%d\n",
                audio_dev, rate, period_size);
    }

    /* ── Shared memory bus ─────────────────────────────────────── */

    WaveosBus *bus = NULL;
    int bus_slot = -1;

    {
        int fd = shm_open(WAVEOS_BUS_SHM_NAME, O_RDWR, 0);
        if (fd >= 0) {
            bus = mmap(NULL, sizeof(WaveosBus), PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
            close(fd);
            if (bus == MAP_FAILED) {
                bus = NULL;
                fprintf(stderr, "[miniwave] WARN: bus mmap failed\n");
            } else if (bus->magic != 0x57415645) {
                fprintf(stderr, "[miniwave] WARN: bus magic mismatch\n");
                munmap(bus, sizeof(WaveosBus));
                bus = NULL;
            } else {
                for (int i = 0; i < WAVEOS_BUS_SLOTS; i++) {
                    int expected = 0;
                    if (atomic_compare_exchange_strong(&bus->slots[i].active,
                                                      &expected, 1)) {
                        bus_slot = i;
                        snprintf(bus->slots[i].name, 32, "miniwave");
                        fprintf(stderr, "[miniwave] bus slot %d claimed\n", i);
                        break;
                    }
                }
                if (bus_slot < 0) {
                    fprintf(stderr, "[miniwave] WARN: no free bus slots\n");
                    munmap(bus, sizeof(WaveosBus));
                    bus = NULL;
                }
            }
        } else {
            fprintf(stderr, "[miniwave] bus not available, ALSA only\n");
        }
    }

    /* ── Audio buffers ─────────────────────────────────────────── */

    int16_t *audio_buf = calloc((size_t)(period_size * CHANNELS), sizeof(int16_t));
    float   *mix_buf   = calloc((size_t)(period_size * CHANNELS), sizeof(float));
    float   *slot_buf  = calloc((size_t)(period_size * CHANNELS), sizeof(float));
    if (!audio_buf || !mix_buf || !slot_buf) {
        fprintf(stderr, "[miniwave] ERROR: alloc failed\n");
        goto cleanup;
    }

    fprintf(stderr, "[miniwave] running [ALSA]%s — %d slots registered\n",
            (bus && bus_slot >= 0) ? " [BUS]" : "",
            g_n_types);

    /* ── Start MIDI thread ─────────────────────────────────────── */

    pthread_t midi_tid = 0;
    MidiThreadCtx midi_ctx;
    memset(&midi_ctx, 0, sizeof(midi_ctx));
    midi_ctx.midi_in = midi_in;

    if (midi_in) {
        if (pthread_create(&midi_tid, NULL, midi_thread_fn, &midi_ctx) != 0) {
            fprintf(stderr, "[miniwave] ERROR: can't create MIDI thread\n");
            goto cleanup;
        }
        fprintf(stderr, "[miniwave] MIDI thread started (%s)\n", midi_dev);
    }

    /* ── Start OSC thread ──────────────────────────────────────── */

    pthread_t osc_tid = 0;
    OscThreadCtx osc_ctx;
    memset(&osc_ctx, 0, sizeof(osc_ctx));
    osc_ctx.port = osc_port;
    osc_ctx.midi_in_ptr = &midi_in;
    osc_ctx.midi_dev = midi_dev;

    if (osc_port > 0) {
        if (pthread_create(&osc_tid, NULL, osc_thread_fn, &osc_ctx) != 0) {
            fprintf(stderr, "[miniwave] WARN: can't create OSC thread\n");
        }
    }

    /* ── Audio render loop ─────────────────────────────────────── */

    while (!g_quit) {
        /* Zero the mix buffer */
        memset(mix_buf, 0, sizeof(float) * (size_t)(period_size * CHANNELS));

        /* Check if any slot has solo enabled */
        int any_solo = 0;
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (g_rack.slots[i].active && g_rack.slots[i].solo) {
                any_solo = 1;
                break;
            }
        }

        /* Render each active slot */
        for (int i = 0; i < MAX_SLOTS; i++) {
            RackSlot *slot = &g_rack.slots[i];
            if (!slot->active || !slot->state) continue;
            if (slot->mute) continue;
            if (any_solo && !slot->solo) continue;

            InstrumentType *itype = g_type_registry[slot->type_idx];

            /* Clear slot render buffer */
            memset(slot_buf, 0, sizeof(float) * (size_t)(period_size * CHANNELS));

            /* Render instrument into slot buffer */
            itype->render(slot->state, slot_buf, period_size, SAMPLE_RATE);

            /* Mix into main buffer with slot volume */
            float vol = slot->volume;
            for (int j = 0; j < period_size * CHANNELS; j++) {
                mix_buf[j] += slot_buf[j] * vol;
            }
        }

        /* Apply master volume and limiter */
        float mv = g_rack.master_volume;
        for (int j = 0; j < period_size * CHANNELS; j++) {
            mix_buf[j] *= mv;
            mix_buf[j] = master_limiter(mix_buf[j]);
        }

        /* Write to bus if available */
        if (bus && bus_slot >= 0) {
            bus_write(bus, bus_slot, mix_buf, period_size);
        }

        /* Convert to S16 for ALSA */
        for (int j = 0; j < period_size * CHANNELS; j++) {
            float s = mix_buf[j];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            audio_buf[j] = (int16_t)(s * 32000.0f);
        }

        /* Write to ALSA */
        snd_pcm_sframes_t frames = snd_pcm_writei(pcm, audio_buf, (snd_pcm_uframes_t)period_size);
        if (frames < 0) {
            frames = snd_pcm_recover(pcm, (int)frames, 1);
            if (frames < 0) {
                fprintf(stderr, "[miniwave] audio write error: %s\n",
                        snd_strerror((int)frames));
            }
        }
    }

    /* ── Shutdown ───────────────────────────────────────────────── */

    fprintf(stderr, "[miniwave] shutting down...\n");

    if (midi_tid) pthread_join(midi_tid, NULL);
    if (osc_tid)  pthread_join(osc_tid, NULL);

cleanup:
    /* Destroy all active slots */
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (g_rack.slots[i].active && g_rack.slots[i].state) {
            InstrumentType *itype = g_type_registry[g_rack.slots[i].type_idx];
            itype->destroy(g_rack.slots[i].state);
            free(g_rack.slots[i].state);
            g_rack.slots[i].state = NULL;
            g_rack.slots[i].active = 0;
        }
    }

    if (bus && bus_slot >= 0) {
        atomic_store(&bus->slots[bus_slot].active, 0);
        memset(bus->slots[bus_slot].name, 0, 32);
        munmap(bus, sizeof(WaveosBus));
    }
    if (midi_in) snd_rawmidi_close(midi_in);
    if (pcm) snd_pcm_close(pcm);
    free(audio_buf);
    free(mix_buf);
    free(slot_buf);

    fprintf(stderr, "[miniwave] done\n");
    return 0;
}
