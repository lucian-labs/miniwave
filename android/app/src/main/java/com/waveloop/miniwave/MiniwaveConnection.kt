package com.waveloop.miniwave

/**
 * Abstraction over local engine (JNI) and remote instance (HTTP/SSE).
 * UI polls this at ~30fps — implementations must return fast.
 */
interface MiniwaveConnection {

    val isLocal: Boolean
    val isConnected: Boolean

    // Hot-path reads — fill pre-allocated arrays, no allocation
    fun pollPeaks(out: FloatArray)       // 36 floats
    fun getScope(out: FloatArray)        // 512 floats
    fun getSlotInfo(out: IntArray)       // 64 ints
    fun getFocusedCh(): Int
    fun getBpm(): Float
    fun getMasterVolume(): Float
    fun getSlotVolume(ch: Int): Float
    fun getSlotTypeName(ch: Int): String
    fun getTypeNames(): Array<String>

    // Cold-path reads
    fun getChannelJson(ch: Int): String
    fun getRackJson(): String

    // Writes
    fun setFocusedCh(ch: Int)
    fun setMasterVolume(value: Float)
    fun setSlotVolume(ch: Int, value: Float)
    fun setSlotMute(ch: Int, mute: Boolean)
    fun setSlotSolo(ch: Int, solo: Boolean)
    fun setSlot(ch: Int, typeName: String): Boolean
    fun clearSlot(ch: Int)
    fun setBpm(value: Float)
    fun noteOn(ch: Int, note: Int, vel: Int)
    fun noteOff(ch: Int, note: Int)
    fun panic()
    fun saveState()

    // Instrument params
    fun setParam(ch: Int, name: String, value: Float)

    // Scale / Chord
    fun setScale(ch: Int, root: Int, degrees: IntArray?)
    fun setChord(ch: Int, intervals: IntArray?)
    fun clearScale(ch: Int)

    // KeySeq / Sequencer
    fun keyseqDsl(ch: Int, dsl: String)
    fun keyseqEnable(ch: Int, enable: Boolean)
    fun keyseqStop(ch: Int)
    fun keyseqGetDsl(ch: Int): String
    fun keyseqIsEnabled(ch: Int): Boolean

    // Lifecycle
    fun start()
    fun stop()
}
