package com.waveloop.miniwave

/**
 * JNI bridge to the native miniwave synth engine.
 * Direct struct access for hot-path reads, JSON for cold-path detail.
 */
class MiniwaveEngine {

    companion object {
        init { System.loadLibrary("miniwave") }
        const val MAX_SLOTS = 16
    }

    // Lifecycle
    external fun nativeStart(filesDir: String, httpPort: Int): Boolean
    external fun nativeStop()

    // MIDI
    external fun nativeMidiEvent(status: Int, d1: Int, d2: Int)
    external fun nativeNoteOn(ch: Int, note: Int, vel: Int)
    external fun nativeNoteOff(ch: Int, note: Int)
    external fun nativePanic()
    external fun nativeSetScale(ch: Int, root: Int, degrees: IntArray?)
    external fun nativeSetChord(ch: Int, intervals: IntArray?)
    external fun nativeClearScale(ch: Int)

    // Hot-path reads — pre-allocated arrays, no allocation per call
    external fun nativePollPeaks(out: FloatArray)       // 36 floats
    external fun nativeGetScope(out: FloatArray)         // 512 floats
    external fun nativeGetSlotInfo(out: IntArray)        // 64 ints (active, type_idx, mute, solo) × 16
    external fun nativeGetFocusedCh(): Int
    external fun nativeGetBpm(): Float
    external fun nativeGetMasterVolume(): Float
    external fun nativeGetSlotVolume(ch: Int): Float
    external fun nativeGetSlotTypeName(ch: Int): String
    external fun nativeGetTypeNames(): Array<String>

    // Cold-path reads — JSON
    external fun nativeGetChannelJson(ch: Int): String
    external fun nativeGetRackJson(): String

    // Direct writes — no JSON overhead
    external fun nativeSetFocusedCh(ch: Int)
    external fun nativeSetMasterVolume(value: Float)
    external fun nativeSetSlotVolume(ch: Int, value: Float)
    external fun nativeSetSlotMute(ch: Int, mute: Boolean)
    external fun nativeSetSlotSolo(ch: Int, solo: Boolean)
    external fun nativeSetSlot(ch: Int, typeName: String): Boolean
    external fun nativeClearSlot(ch: Int)
    external fun nativeSetBpm(value: Float)
    external fun nativeSaveState()

    // Instrument params
    external fun nativeSetParam(ch: Int, name: String, value: Float)

    // KeySeq / Sequencer
    external fun nativeKeyseqDsl(ch: Int, dsl: String)
    external fun nativeKeyseqEnable(ch: Int, enable: Boolean)
    external fun nativeKeyseqStop(ch: Int)
    external fun nativeKeyseqGetDsl(ch: Int): String
    external fun nativeKeyseqIsEnabled(ch: Int): Boolean

    // Audio info
    external fun nativeGetSampleRate(): Int
    external fun nativeGetBurstSize(): Int
}
