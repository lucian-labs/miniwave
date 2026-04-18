package com.waveloop.miniwave

import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL
import kotlin.concurrent.thread

/**
 * Remote connection to a miniwave instance on the LAN.
 * SSE for state updates, HTTP POST /api for commands.
 * Reads return from a local cache updated by SSE — always fast.
 */
class RemoteConnection(private val host: String, private val port: Int = 8080) : MiniwaveConnection {

    companion object {
        private const val TAG = "miniwave-remote"
    }

    override val isLocal = false
    override var isConnected = false
        private set

    // Cached state — updated by SSE thread
    private val peakCache = FloatArray(36)
    private val slotInfoCache = IntArray(64)
    private val slotVolumeCache = FloatArray(16)
    private val slotTypeCache = Array(16) { "" }
    private var focusedChCache = 0
    private var bpmCache = 120f
    private var masterVolCache = 0.75f
    private var typeNamesCache = arrayOf<String>()
    private val scopeCache = FloatArray(512)

    @Volatile private var running = false
    private var sseThread: Thread? = null
    private var scopeThread: Thread? = null

    private val baseUrl get() = "http://$host:$port"

    // ── Hot-path reads: return from cache ──

    override fun pollPeaks(out: FloatArray) {
        System.arraycopy(peakCache, 0, out, 0, minOf(out.size, 36))
    }

    override fun getScope(out: FloatArray) {
        System.arraycopy(scopeCache, 0, out, 0, minOf(out.size, 512))
    }

    override fun getSlotInfo(out: IntArray) {
        System.arraycopy(slotInfoCache, 0, out, 0, minOf(out.size, 64))
    }

    override fun getFocusedCh() = focusedChCache
    override fun getBpm() = bpmCache
    override fun getMasterVolume() = masterVolCache

    override fun getSlotVolume(ch: Int): Float {
        if (ch < 0 || ch >= 16) return 0f
        return slotVolumeCache[ch]
    }

    override fun getSlotTypeName(ch: Int): String {
        if (ch < 0 || ch >= 16) return ""
        return slotTypeCache[ch]
    }

    override fun getTypeNames(): Array<String> = typeNamesCache

    // ── Cold-path reads: HTTP fetch ──

    override fun getChannelJson(ch: Int): String {
        return apiCall("""{"type":"ch_status","channel":$ch}""")
    }

    override fun getRackJson(): String {
        return apiCall("""{"type":"rack_status"}""")
    }

    // ── Writes: HTTP POST /api ──

    override fun setFocusedCh(ch: Int) {
        focusedChCache = ch
        apiAsync("""{"type":"focus_ch","channel":$ch}""")
    }

    override fun setMasterVolume(value: Float) {
        masterVolCache = value
        apiAsync("""{"type":"master_volume","value":$value}""")
    }

    override fun setSlotVolume(ch: Int, value: Float) {
        if (ch in 0..15) slotVolumeCache[ch] = value
        apiAsync("""{"type":"slot_volume","channel":$ch,"value":$value}""")
    }

    override fun setSlotMute(ch: Int, mute: Boolean) {
        apiAsync("""{"type":"slot_mute","channel":$ch,"value":${if (mute) 1 else 0}}""")
    }

    override fun setSlotSolo(ch: Int, solo: Boolean) {
        apiAsync("""{"type":"slot_solo","channel":$ch,"value":${if (solo) 1 else 0}}""")
    }

    override fun setSlot(ch: Int, typeName: String): Boolean {
        val resp = apiCall("""{"type":"slot_set","channel":$ch,"instrument":"$typeName"}""")
        return resp.contains("\"ok\"")
    }

    override fun clearSlot(ch: Int) {
        apiAsync("""{"type":"slot_clear","channel":$ch}""")
    }

    override fun setBpm(value: Float) {
        bpmCache = value
        apiAsync("""{"type":"bpm","value":$value}""")
    }

    override fun noteOn(ch: Int, note: Int, vel: Int) {
        apiAsync("""{"type":"note_on","channel":$ch,"note":$note,"velocity":$vel}""")
    }

    override fun noteOff(ch: Int, note: Int) {
        apiAsync("""{"type":"note_off","channel":$ch,"note":$note}""")
    }

    override fun panic() {
        apiAsync("""{"type":"panic"}""")
    }

    override fun setParam(ch: Int, name: String, value: Float) {
        apiAsync("""{"type":"ch","channel":$ch,"path":"/$name","fargs":[$value]}""")
    }

    override fun keyseqDsl(ch: Int, dsl: String) {
        apiAsync("""{"type":"keyseq_dsl","channel":$ch,"dsl":"$dsl"}""")
    }
    override fun keyseqEnable(ch: Int, enable: Boolean) {
        apiAsync("""{"type":"keyseq_enable","channel":$ch,"enabled":${if (enable) 1 else 0}}""")
    }
    override fun keyseqStop(ch: Int) {
        apiAsync("""{"type":"keyseq_stop","channel":$ch}""")
    }
    override fun keyseqGetDsl(ch: Int): String = "" // would need ch_status parse
    override fun keyseqIsEnabled(ch: Int): Boolean = false

    override fun saveState() {}

    override fun setScale(ch: Int, root: Int, degrees: IntArray?) {
        val arr = degrees?.joinToString(",") ?: ""
        apiAsync("""{"type":"set_scale","channel":$ch,"root":$root,"degrees":[$arr]}""")
    }

    override fun setChord(ch: Int, intervals: IntArray?) {
        val arr = intervals?.joinToString(",") ?: ""
        apiAsync("""{"type":"set_chord","channel":$ch,"intervals":[$arr]}""")
    }

    override fun clearScale(ch: Int) {
        apiAsync("""{"type":"set_scale","channel":$ch,"root":-1,"degrees":[]}""")
        apiAsync("""{"type":"set_chord","channel":$ch,"intervals":[]}""")
    }

    // ── Lifecycle ──

    override fun start() {
        running = true
        sseThread = thread(name = "sse-reader") { sseLoop() }
        scopeThread = thread(name = "scope-poll") { scopeLoop() }
    }

    override fun stop() {
        running = false
        sseThread?.interrupt()
        scopeThread?.interrupt()
    }

    // ── SSE: parses rack_status events to fill caches ──

    private fun sseLoop() {
        while (running) {
            try {
                val url = URL("$baseUrl/events")
                val conn = (url.openConnection() as HttpURLConnection).apply {
                    connectTimeout = 3000
                    readTimeout = 0 // SSE is long-lived
                    setRequestProperty("Accept", "text/event-stream")
                }

                isConnected = true
                Log.i(TAG, "SSE connected to $host:$port")

                // Also fetch type names once on connect
                fetchTypeNames()

                BufferedReader(InputStreamReader(conn.inputStream)).use { reader ->
                    var eventType = ""
                    val dataBuilder = StringBuilder()

                    while (running) {
                        val line = reader.readLine() ?: break

                        when {
                            line.startsWith("event:") -> {
                                eventType = line.removePrefix("event:").trim()
                            }
                            line.startsWith("data:") -> {
                                dataBuilder.append(line.removePrefix("data:").trim())
                            }
                            line.isEmpty() && dataBuilder.isNotEmpty() -> {
                                // End of event
                                val data = dataBuilder.toString()
                                dataBuilder.clear()
                                parseEvent(eventType, data)
                                eventType = ""
                            }
                        }
                    }
                }
            } catch (e: Exception) {
                if (running) {
                    isConnected = false
                    Log.w(TAG, "SSE disconnected: ${e.message}, reconnecting...")
                    Thread.sleep(1000)
                }
            }
        }
    }

    private fun parseEvent(type: String, data: String) {
        try {
            val json = JSONObject(data)

            when (type) {
                "rack_status" -> {
                    val slots = json.optJSONArray("slots") ?: return
                    for (i in 0 until minOf(slots.length(), 16)) {
                        val s = slots.getJSONObject(i)
                        slotInfoCache[i * 4] = if (s.optInt("active", 0) != 0) 1 else 0
                        slotInfoCache[i * 4 + 1] = -1 // type_idx not exposed, use name
                        slotInfoCache[i * 4 + 2] = s.optInt("mute", 0)
                        slotInfoCache[i * 4 + 3] = s.optInt("solo", 0)

                        val pk = s.optJSONArray("pk")
                        peakCache[i * 2] = pk?.optDouble(0, 0.0)?.toFloat() ?: 0f
                        peakCache[i * 2 + 1] = pk?.optDouble(1, 0.0)?.toFloat() ?: 0f

                        slotVolumeCache[i] = s.optDouble("volume", 0.0).toFloat()
                        slotTypeCache[i] = s.optString("type", "")
                    }

                    val masterPk = json.optJSONArray("master_pk")
                    peakCache[32] = masterPk?.optDouble(0, 0.0)?.toFloat() ?: 0f
                    peakCache[33] = masterPk?.optDouble(1, 0.0)?.toFloat() ?: 0f
                    val masterHold = json.optJSONArray("master_hold")
                    peakCache[34] = masterHold?.optDouble(0, 0.0)?.toFloat() ?: 0f
                    peakCache[35] = masterHold?.optDouble(1, 0.0)?.toFloat() ?: 0f

                    focusedChCache = json.optInt("focused_ch", 0)
                    bpmCache = json.optDouble("bpm", 120.0).toFloat()
                    masterVolCache = json.optDouble("master_volume", 0.75).toFloat()
                }

                "rack_types" -> {
                    val types = json.optJSONArray("types") ?: return
                    typeNamesCache = Array(types.length()) { types.getString(it) }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "parse error: ${e.message}")
        }
    }

    // ── Scope polling (separate thread, ~20fps) ──

    private fun scopeLoop() {
        while (running) {
            try {
                val resp = apiCall("""{"type":"scope"}""")
                val json = JSONObject(resp)
                val samples = json.optJSONArray("samples") ?: continue
                for (i in 0 until minOf(samples.length(), 512)) {
                    scopeCache[i] = samples.optDouble(i, 0.0).toFloat()
                }
                Thread.sleep(50) // ~20fps
            } catch (e: Exception) {
                if (running) Thread.sleep(500)
            }
        }
    }

    // ── HTTP helpers ──

    private fun apiCall(body: String): String {
        return try {
            val url = URL("$baseUrl/api")
            val conn = (url.openConnection() as HttpURLConnection).apply {
                requestMethod = "POST"
                connectTimeout = 2000
                readTimeout = 2000
                doOutput = true
                setRequestProperty("Content-Type", "application/json")
            }
            conn.outputStream.use { it.write(body.toByteArray()) }
            conn.inputStream.bufferedReader().readText()
        } catch (e: Exception) {
            Log.w(TAG, "API error: ${e.message}")
            "{\"error\":\"${e.message}\"}"
        }
    }

    private fun apiAsync(body: String) {
        thread(name = "api-call") { apiCall(body) }
    }

    private fun fetchTypeNames() {
        try {
            val resp = apiCall("""{"type":"rack_types"}""")
            val json = JSONObject(resp)
            val types = json.optJSONArray("types") ?: return
            typeNamesCache = Array(types.length()) { types.getString(it) }
        } catch (_: Exception) {}
    }
}
