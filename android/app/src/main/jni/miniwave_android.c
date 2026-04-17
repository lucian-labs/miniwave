/* miniwave — Android platform (AAudio + native UI via JNI)
 *
 * All portable code lives in common/ headers.
 * This file implements platform_* functions for Android,
 * AAudio output, and JNI exports for direct native UI access.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#include <jni.h>
#include <android/log.h>
#include <aaudio/AAudio.h>

#define LOG_TAG "miniwave"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* ── Stub out JACK (not available on Android) ─────────────────────── */

static int jack_init(void) { return -1; }
static int jack_start(void) { return -1; }
static void jack_cleanup(void) {}

typedef struct { void *bus; int bus_slot; } JackCtx;
static JackCtx g_jack = {0};

/* ── Common code ──────────────────────────────────────────────────── */

#include "rack.h"
#include "server.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Platform: MIDI (events pushed from Java via JNI)
 * ══════════════════════════════════════════════════════════════════════ */

static int platform_midi_init(void) {
    LOGI("MIDI: events pushed from Java via JNI");
    return 0;
}

static int platform_midi_connect(const char *addr) {
    (void)addr;
    return 0;
}

static void platform_midi_disconnect(void) {}

static int platform_midi_list_devices(char devices[][64], char names[][128], int max) {
    (void)devices; (void)names; (void)max;
    return 0;
}

static void *platform_midi_thread(void *arg) {
    (void)arg;
    while (!g_quit) usleep(100000);
    return NULL;
}

static void platform_midi_cleanup(void) {}

/* ══════════════════════════════════════════════════════════════════════
 *  Platform: file paths
 * ══════════════════════════════════════════════════════════════════════ */

static char g_files_dir[512] = "";

static int platform_exe_dir(char *buf, int max) {
    if (!g_files_dir[0]) return -1;
    snprintf(buf, max, "%s/", g_files_dir);
    return 0;
}

static const char *platform_audio_fallback_name(void) {
    return "AAudio";
}

/* ══════════════════════════════════════════════════════════════════════
 *  Audio: AAudio
 * ══════════════════════════════════════════════════════════════════════ */

static AAudioStream *g_stream = NULL;
static float *g_mix_buf = NULL;
static float *g_slot_buf = NULL;
static float *g_fx_buf = NULL;     /* effects chain scratch buffer */
static int    g_buf_frames = 0;
#define FX_BUF_FRAMES 512          /* effects scratch — matches audio callback size */

static aaudio_data_callback_result_t audio_callback(
    AAudioStream *stream, void *userData,
    void *audioData, int32_t numFrames)
{
    (void)stream; (void)userData;
    int frames = numFrames;
    if (frames > g_buf_frames) frames = g_buf_frames;

    render_mix(g_mix_buf, g_slot_buf, frames, SAMPLE_RATE);

    float *out = (float *)audioData;
    memcpy(out, g_mix_buf, sizeof(float) * (size_t)(frames * CHANNELS));

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void audio_error_callback(
    AAudioStream *stream, void *userData, aaudio_result_t error)
{
    (void)stream; (void)userData;
    LOGE("AAudio error: %s", AAudio_convertResultToText(error));
}

static int audio_init(void) {
    AAudioStreamBuilder *builder = NULL;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK) {
        LOGE("can't create stream builder: %s", AAudio_convertResultToText(result));
        return -1;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSampleRate(builder, SAMPLE_RATE);
    AAudioStreamBuilder_setChannelCount(builder, CHANNELS);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setDataCallback(builder, audio_callback, NULL);
    AAudioStreamBuilder_setErrorCallback(builder, audio_error_callback, NULL);

    result = AAudioStreamBuilder_openStream(builder, &g_stream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK) {
        LOGE("can't open audio stream: %s", AAudio_convertResultToText(result));
        return -1;
    }

    int32_t burst = AAudioStream_getFramesPerBurst(g_stream);
    g_buf_frames = burst > 0 ? burst * 2 : 512;
    g_period_size = burst > 0 ? burst : 256;
    g_actual_srate = AAudioStream_getSampleRate(g_stream);
    snprintf(g_audio_device, sizeof(g_audio_device), "AAudio");

    g_mix_buf = calloc((size_t)(g_buf_frames * CHANNELS), sizeof(float));
    g_slot_buf = calloc((size_t)(g_buf_frames * CHANNELS), sizeof(float));
    g_fx_buf = calloc((size_t)(FX_BUF_FRAMES * CHANNELS), sizeof(float));
    if (!g_mix_buf || !g_slot_buf || !g_fx_buf) {
        LOGE("can't allocate audio buffers");
        AAudioStream_close(g_stream);
        g_stream = NULL;
        return -1;
    }

    LOGI("audio: AAudio @ %dHz burst=%d bufFrames=%d", g_actual_srate, burst, g_buf_frames);
    return 0;
}

static int audio_start(void) {
    if (!g_stream) return -1;
    aaudio_result_t result = AAudioStream_requestStart(g_stream);
    if (result != AAUDIO_OK) {
        LOGE("can't start audio: %s", AAudio_convertResultToText(result));
        return -1;
    }
    LOGI("audio started");
    return 0;
}

static void audio_stop(void) {
    if (g_stream) {
        AAudioStream_requestStop(g_stream);
        AAudioStream_close(g_stream);
        g_stream = NULL;
    }
    free(g_mix_buf);  g_mix_buf = NULL;
    free(g_slot_buf); g_slot_buf = NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 *  JNI: Engine lifecycle
 * ══════════════════════════════════════════════════════════════════════ */

#define JNI_FN(name) Java_com_waveloop_miniwave_MiniwaveEngine_##name

JNIEXPORT jboolean JNICALL
JNI_FN(nativeStart)(JNIEnv *env, jobject obj, jstring filesDir, jint httpPort) {
    (void)obj;

    const char *path = (*env)->GetStringUTFChars(env, filesDir, NULL);
    snprintf(g_files_dir, sizeof(g_files_dir), "%s", path);
    (*env)->ReleaseStringUTFChars(env, filesDir, path);

    LOGI("starting engine, filesDir=%s", g_files_dir);
    setenv("HOME", g_files_dir, 1);
    signal(SIGPIPE, SIG_IGN);

    rack_init();
    state_init_path();
    state_load();
    keyseq_wire_graph_broadcast();
    LOGI("rack initialized, %d types", g_n_types);

    platform_midi_init();

    if (audio_init() != 0) { LOGE("audio init failed"); return JNI_FALSE; }

    /* HTTP server — for remote control from other devices on LAN */
    if (httpPort > 0) {
        http_load_html();
        static HttpThreadCtx http_ctx;
        memset(&http_ctx, 0, sizeof(http_ctx));
        http_ctx.port = (int)httpPort;
        pthread_t http_tid;
        if (pthread_create(&http_tid, NULL, http_thread_fn, &http_ctx) == 0)
            pthread_detach(http_tid);
        LOGI("HTTP server on port %d (remote control)", (int)httpPort);
    }

    static OscThreadCtx osc_ctx;
    memset(&osc_ctx, 0, sizeof(osc_ctx));
    osc_ctx.port = DEFAULT_OSC_PORT;
    pthread_t osc_tid;
    if (pthread_create(&osc_tid, NULL, osc_thread_fn, &osc_ctx) == 0)
        pthread_detach(osc_tid);

    if (mcast_init() == 0) {
        pthread_t mcast_tid;
        if (pthread_create(&mcast_tid, NULL, mcast_thread_fn, NULL) == 0)
            pthread_detach(mcast_tid);
    }

    if (audio_start() != 0) { audio_stop(); return JNI_FALSE; }

    g_use_jack = 0;
    g_bus_active = 0;
    g_bus_slot = -1;
    LOGI("engine running");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
JNI_FN(nativeStop)(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    LOGI("stopping engine");
    g_quit = 1;
    state_save();
    audio_stop();
    platform_midi_cleanup();
    LOGI("engine stopped");
}

/* ══════════════════════════════════════════════════════════════════════
 *  JNI: MIDI input
 * ══════════════════════════════════════════════════════════════════════ */

JNIEXPORT void JNICALL
JNI_FN(nativeMidiEvent)(JNIEnv *env, jobject obj, jint status, jint d1, jint d2) {
    (void)env; (void)obj;
    uint8_t data[3] = { (uint8_t)status, (uint8_t)d1, (uint8_t)d2 };
    midi_dispatch_raw(data, 3);
}

/* ══════════════════════════════════════════════════════════════════════
 *  JNI: Hot-path state reads (called at ~30fps from UI thread)
 * ══════════════════════════════════════════════════════════════════════ */

/* Peaks: [slot0_L, slot0_R, slot1_L, slot1_R, ... slot15_R, master_L, master_R, master_holdL, master_holdR]
 * = 36 floats */
JNIEXPORT void JNICALL
JNI_FN(nativePollPeaks)(JNIEnv *env, jobject obj, jfloatArray out) {
    (void)obj;
    float buf[36];
    for (int i = 0; i < MAX_SLOTS; i++) {
        buf[i * 2]     = g_rack.slots[i].peak_l;
        buf[i * 2 + 1] = g_rack.slots[i].peak_r;
    }
    buf[32] = g_rack.master_peak_l;
    buf[33] = g_rack.master_peak_r;
    buf[34] = g_rack.master_hold_l;
    buf[35] = g_rack.master_hold_r;
    (*env)->SetFloatArrayRegion(env, out, 0, 36, buf);
}

/* Scope: 512 interleaved L/R floats */
JNIEXPORT void JNICALL
JNI_FN(nativeGetScope)(JNIEnv *env, jobject obj, jfloatArray out) {
    (void)obj;
    (*env)->SetFloatArrayRegion(env, out, 0, 512, g_rack.scope_buf);
}

/* Slot info: [active, type_idx, mute, solo] × 16 = 64 ints */
JNIEXPORT void JNICALL
JNI_FN(nativeGetSlotInfo)(JNIEnv *env, jobject obj, jintArray out) {
    (void)obj;
    int buf[64];
    for (int i = 0; i < MAX_SLOTS; i++) {
        buf[i * 4]     = atomic_load(&g_rack.slots[i].active);
        buf[i * 4 + 1] = g_rack.slots[i].type_idx;
        buf[i * 4 + 2] = g_rack.slots[i].mute;
        buf[i * 4 + 3] = g_rack.slots[i].solo;
    }
    (*env)->SetIntArrayRegion(env, out, 0, 64, buf);
}

JNIEXPORT jint JNICALL
JNI_FN(nativeGetFocusedCh)(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    return (jint)g_rack.focused_ch;
}

JNIEXPORT jfloat JNICALL
JNI_FN(nativeGetBpm)(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    return (jfloat)g_bpm;
}

JNIEXPORT jfloat JNICALL
JNI_FN(nativeGetMasterVolume)(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    return (jfloat)g_rack.master_volume;
}

JNIEXPORT jfloat JNICALL
JNI_FN(nativeGetSlotVolume)(JNIEnv *env, jobject obj, jint ch) {
    (void)env; (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS) return 0.0f;
    return (jfloat)g_rack.slots[ch].volume;
}

/* Get instrument type name for a slot */
JNIEXPORT jstring JNICALL
JNI_FN(nativeGetSlotTypeName)(JNIEnv *env, jobject obj, jint ch) {
    (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS || !g_rack.slots[ch].active ||
        g_rack.slots[ch].type_idx < 0 || g_rack.slots[ch].type_idx >= g_n_types) {
        return (*env)->NewStringUTF(env, "");
    }
    return (*env)->NewStringUTF(env, g_type_registry[g_rack.slots[ch].type_idx]->name);
}

/* Get all registered instrument type names */
JNIEXPORT jobjectArray JNICALL
JNI_FN(nativeGetTypeNames)(JNIEnv *env, jobject obj) {
    (void)obj;
    jclass strClass = (*env)->FindClass(env, "java/lang/String");
    jobjectArray arr = (*env)->NewObjectArray(env, g_n_types, strClass, NULL);
    for (int i = 0; i < g_n_types; i++) {
        jstring s = (*env)->NewStringUTF(env, g_type_registry[i]->name);
        (*env)->SetObjectArrayElement(env, arr, i, s);
        (*env)->DeleteLocalRef(env, s);
    }
    return arr;
}

/* ══════════════════════════════════════════════════════════════════════
 *  JNI: Cold-path state reads (on demand, returns JSON)
 * ══════════════════════════════════════════════════════════════════════ */

/* Full channel detail — calls instrument vtable json_status */
JNIEXPORT jstring JNICALL
JNI_FN(nativeGetChannelJson)(JNIEnv *env, jobject obj, jint ch) {
    (void)obj;
    char buf[16384];
    slot_read_begin();
    build_ch_status_json_inner((int)ch, buf, sizeof(buf));
    slot_read_end();
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL
JNI_FN(nativeGetRackJson)(JNIEnv *env, jobject obj) {
    (void)obj;
    char buf[32768];
    slot_read_begin();
    build_rack_status_json(buf, sizeof(buf));
    slot_read_end();
    return (*env)->NewStringUTF(env, buf);
}

/* ══════════════════════════════════════════════════════════════════════
 *  JNI: Direct state writes (no JSON overhead)
 * ══════════════════════════════════════════════════════════════════════ */

JNIEXPORT void JNICALL
JNI_FN(nativeSetFocusedCh)(JNIEnv *env, jobject obj, jint ch) {
    (void)env; (void)obj;
    if (ch >= 0 && ch < MAX_SLOTS) {
        g_rack.focused_ch = (int)ch;
        atomic_store(&g_sse_detail_dirty, 1);
    }
}

JNIEXPORT void JNICALL
JNI_FN(nativeSetMasterVolume)(JNIEnv *env, jobject obj, jfloat val) {
    (void)env; (void)obj;
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    g_rack.master_volume = val;
}

JNIEXPORT void JNICALL
JNI_FN(nativeSetSlotVolume)(JNIEnv *env, jobject obj, jint ch, jfloat val) {
    (void)env; (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS) return;
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    g_rack.slots[ch].volume = val;
}

JNIEXPORT void JNICALL
JNI_FN(nativeSetSlotMute)(JNIEnv *env, jobject obj, jint ch, jboolean mute) {
    (void)env; (void)obj;
    if (ch >= 0 && ch < MAX_SLOTS)
        g_rack.slots[ch].mute = mute ? 1 : 0;
}

JNIEXPORT void JNICALL
JNI_FN(nativeSetSlotSolo)(JNIEnv *env, jobject obj, jint ch, jboolean solo) {
    (void)env; (void)obj;
    if (ch >= 0 && ch < MAX_SLOTS)
        g_rack.slots[ch].solo = solo ? 1 : 0;
}

JNIEXPORT jboolean JNICALL
JNI_FN(nativeSetSlot)(JNIEnv *env, jobject obj, jint ch, jstring typeName) {
    (void)obj;
    const char *name = (*env)->GetStringUTFChars(env, typeName, NULL);
    int err = rack_set_slot((int)ch, name);
    (*env)->ReleaseStringUTFChars(env, typeName, name);
    if (err == 0) {
        state_mark_dirty();
        sse_mark_dirty();
    }
    return err == 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
JNI_FN(nativeClearSlot)(JNIEnv *env, jobject obj, jint ch) {
    (void)env; (void)obj;
    rack_clear_slot((int)ch);
    state_mark_dirty();
    sse_mark_dirty();
}

JNIEXPORT void JNICALL
JNI_FN(nativeSetBpm)(JNIEnv *env, jobject obj, jfloat val) {
    (void)env; (void)obj;
    if (val >= 20.0f && val <= 300.0f) g_bpm = val;
}

JNIEXPORT void JNICALL
JNI_FN(nativeNoteOn)(JNIEnv *env, jobject obj, jint ch, jint note, jint vel) {
    (void)env; (void)obj;
    uint8_t data[3] = { (uint8_t)(0x90 | (ch & 0x0F)), (uint8_t)(note & 0x7F), (uint8_t)(vel & 0x7F) };
    midi_dispatch_raw(data, 3);
}

JNIEXPORT void JNICALL
JNI_FN(nativeNoteOff)(JNIEnv *env, jobject obj, jint ch, jint note) {
    (void)env; (void)obj;
    uint8_t data[3] = { (uint8_t)(0x80 | (ch & 0x0F)), (uint8_t)(note & 0x7F), 0 };
    midi_dispatch_raw(data, 3);
}

/* ── Instrument params ── */

JNIEXPORT void JNICALL
JNI_FN(nativeSetParam)(JNIEnv *env, jobject obj, jint ch, jstring name, jfloat value) {
    (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS) return;
    RackSlot *slot = &g_rack.slots[ch];
    if (!slot->active || !slot->state || slot->type_idx < 0) return;
    InstrumentType *itype = g_type_registry[slot->type_idx];
    if (!itype->set_param) return;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    itype->set_param(slot->state, n, value);
    (*env)->ReleaseStringUTFChars(env, name, n);
    state_mark_dirty();
}

/* ── Scale / Chord ── */

JNIEXPORT void JNICALL
JNI_FN(nativeSetScale)(JNIEnv *env, jobject obj, jint ch, jint root, jintArray degrees) {
    (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS) return;
    RackSlot *slot = &g_rack.slots[ch];
    slot->scale_root = (int)root;
    if (degrees) {
        jint *d = (*env)->GetIntArrayElements(env, degrees, NULL);
        int n = (*env)->GetArrayLength(env, degrees);
        if (n > MAX_SCALE_DEGREES) n = MAX_SCALE_DEGREES;
        for (int i = 0; i < n; i++) slot->scale_degrees[i] = (uint8_t)d[i];
        slot->scale_len = n;
        (*env)->ReleaseIntArrayElements(env, degrees, d, JNI_ABORT);
    } else {
        slot->scale_len = 0;
    }
    slot->scale_program = 0;
    state_mark_dirty();
    sse_mark_dirty();
}

JNIEXPORT void JNICALL
JNI_FN(nativeSetChord)(JNIEnv *env, jobject obj, jint ch, jintArray intervals) {
    (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS) return;
    RackSlot *slot = &g_rack.slots[ch];
    if (intervals) {
        jint *d = (*env)->GetIntArrayElements(env, intervals, NULL);
        int n = (*env)->GetArrayLength(env, intervals);
        if (n > MAX_CHORD_INTERVALS) n = MAX_CHORD_INTERVALS;
        for (int i = 0; i < n; i++) slot->chord_intervals[i] = (uint8_t)d[i];
        slot->chord_len = n;
        (*env)->ReleaseIntArrayElements(env, intervals, d, JNI_ABORT);
    } else {
        slot->chord_len = 0;
    }
    state_mark_dirty();
    sse_mark_dirty();
}

JNIEXPORT void JNICALL
JNI_FN(nativeClearScale)(JNIEnv *env, jobject obj, jint ch) {
    (void)env; (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS) return;
    g_rack.slots[ch].scale_root = -1;
    g_rack.slots[ch].scale_len = 0;
    g_rack.slots[ch].chord_len = 0;
    state_mark_dirty();
    sse_mark_dirty();
}

JNIEXPORT void JNICALL
JNI_FN(nativePanic)(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    for (int ch = 0; ch < MAX_SLOTS; ch++) {
        RackSlot *slot = &g_rack.slots[ch];
        if (!atomic_load(&slot->active) || !slot->state) continue;
        InstrumentType *itype = g_type_registry[slot->type_idx];
        itype->midi(slot->state, (uint8_t)(0xB0 | ch), 123, 0);
        if (slot->keyseq) keyseq_stop(slot->keyseq);
    }
    LOGI("PANIC — all notes off");
}

JNIEXPORT void JNICALL
JNI_FN(nativeSaveState)(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    state_save();
}

/* ── KeySeq / Sequencer ── */

JNIEXPORT void JNICALL
JNI_FN(nativeKeyseqDsl)(JNIEnv *env, jobject obj, jint ch, jstring dsl) {
    (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS) return;
    RackSlot *slot = &g_rack.slots[ch];
    if (!slot->active || !slot->keyseq) return;
    const char *str = (*env)->GetStringUTFChars(env, dsl, NULL);
    keyseq_parse(slot->keyseq, str);
    (*env)->ReleaseStringUTFChars(env, dsl, str);
    state_mark_dirty();
    sse_mark_dirty();
}

JNIEXPORT void JNICALL
JNI_FN(nativeKeyseqEnable)(JNIEnv *env, jobject obj, jint ch, jboolean enable) {
    (void)env; (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS) return;
    RackSlot *slot = &g_rack.slots[ch];
    if (!slot->keyseq) return;
    slot->keyseq->enabled = enable ? 1 : 0;
    if (!enable) keyseq_stop(slot->keyseq);
    state_mark_dirty();
    sse_mark_dirty();
}

JNIEXPORT void JNICALL
JNI_FN(nativeKeyseqStop)(JNIEnv *env, jobject obj, jint ch) {
    (void)env; (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS) return;
    RackSlot *slot = &g_rack.slots[ch];
    if (slot->keyseq) keyseq_stop(slot->keyseq);
}

JNIEXPORT jstring JNICALL
JNI_FN(nativeKeyseqGetDsl)(JNIEnv *env, jobject obj, jint ch) {
    (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS) return (*env)->NewStringUTF(env, "");
    RackSlot *slot = &g_rack.slots[ch];
    if (!slot->keyseq || !slot->keyseq->source[0])
        return (*env)->NewStringUTF(env, "");
    return (*env)->NewStringUTF(env, slot->keyseq->source);
}

JNIEXPORT jboolean JNICALL
JNI_FN(nativeKeyseqIsEnabled)(JNIEnv *env, jobject obj, jint ch) {
    (void)env; (void)obj;
    if (ch < 0 || ch >= MAX_SLOTS) return JNI_FALSE;
    RackSlot *slot = &g_rack.slots[ch];
    if (!slot->keyseq) return JNI_FALSE;
    return slot->keyseq->enabled ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
JNI_FN(nativeGetSampleRate)(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    return (jint)g_actual_srate;
}

JNIEXPORT jint JNICALL
JNI_FN(nativeGetBurstSize)(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    return (jint)g_period_size;
}
