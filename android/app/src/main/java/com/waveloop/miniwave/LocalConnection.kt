package com.waveloop.miniwave

/**
 * Local engine connection — direct JNI calls to the native synth.
 * All calls are synchronous and return immediately (struct reads).
 */
class LocalConnection(val engine: MiniwaveEngine) : MiniwaveConnection {

    override val isLocal = true
    override val isConnected = true

    override fun pollPeaks(out: FloatArray) = engine.nativePollPeaks(out)
    override fun getScope(out: FloatArray) = engine.nativeGetScope(out)
    override fun getSlotInfo(out: IntArray) = engine.nativeGetSlotInfo(out)
    override fun getFocusedCh() = engine.nativeGetFocusedCh()
    override fun getBpm() = engine.nativeGetBpm()
    override fun getMasterVolume() = engine.nativeGetMasterVolume()
    override fun getSlotVolume(ch: Int) = engine.nativeGetSlotVolume(ch)
    override fun getSlotTypeName(ch: Int) = engine.nativeGetSlotTypeName(ch)
    override fun getTypeNames() = engine.nativeGetTypeNames()

    override fun getChannelJson(ch: Int) = engine.nativeGetChannelJson(ch)
    override fun getRackJson() = engine.nativeGetRackJson()

    override fun setFocusedCh(ch: Int) = engine.nativeSetFocusedCh(ch)
    override fun setMasterVolume(value: Float) = engine.nativeSetMasterVolume(value)
    override fun setSlotVolume(ch: Int, value: Float) = engine.nativeSetSlotVolume(ch, value)
    override fun setSlotMute(ch: Int, mute: Boolean) = engine.nativeSetSlotMute(ch, mute)
    override fun setSlotSolo(ch: Int, solo: Boolean) = engine.nativeSetSlotSolo(ch, solo)
    override fun setSlot(ch: Int, typeName: String) = engine.nativeSetSlot(ch, typeName)
    override fun clearSlot(ch: Int) = engine.nativeClearSlot(ch)
    override fun setBpm(value: Float) = engine.nativeSetBpm(value)
    override fun noteOn(ch: Int, note: Int, vel: Int) = engine.nativeNoteOn(ch, note, vel)
    override fun noteOff(ch: Int, note: Int) = engine.nativeNoteOff(ch, note)
    override fun panic() = engine.nativePanic()
    override fun saveState() = engine.nativeSaveState()

    override fun setParam(ch: Int, name: String, value: Float) = engine.nativeSetParam(ch, name, value)

    override fun keyseqDsl(ch: Int, dsl: String) = engine.nativeKeyseqDsl(ch, dsl)
    override fun keyseqEnable(ch: Int, enable: Boolean) = engine.nativeKeyseqEnable(ch, enable)
    override fun keyseqStop(ch: Int) = engine.nativeKeyseqStop(ch)
    override fun keyseqGetDsl(ch: Int) = engine.nativeKeyseqGetDsl(ch)
    override fun keyseqIsEnabled(ch: Int) = engine.nativeKeyseqIsEnabled(ch)

    override fun setScale(ch: Int, root: Int, degrees: IntArray?) = engine.nativeSetScale(ch, root, degrees)
    override fun setChord(ch: Int, intervals: IntArray?) = engine.nativeSetChord(ch, intervals)
    override fun clearScale(ch: Int) = engine.nativeClearScale(ch)

    override fun start() {}
    override fun stop() {}
}
