package com.waveloop.miniwave

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Bundle
import android.os.IBinder
import android.util.Log
import android.view.View
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import com.waveloop.miniwave.ui.*
import com.waveloop.miniwave.ui.NUM_STEPS
import com.waveloop.miniwave.ui.NUM_ROWS
import com.waveloop.miniwave.ui.gridRowToNotes
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import android.content.SharedPreferences
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream

class MainActivity : ComponentActivity() {

    companion object {
        private const val TAG = "miniwave"
        private const val HTTP_PORT = 8080
        private const val PREFS = "miniwave_prefs"
    }

    private var service: MiniwaveService? = null
    private var midi: MidiHandler? = null
    private var bound = false

    private val serviceReady = mutableStateOf(false)
    private val activeConnection = mutableStateOf<MiniwaveConnection?>(null)

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            service = (binder as MiniwaveService.LocalBinder).getService()
            bound = true
            serviceReady.value = true
            Log.i(TAG, "service bound")
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            bound = false
            service = null
            serviceReady.value = false
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        extractWebAssets()

        // Bind service (needed for local mode)
        val intent = Intent(this, MiniwaveService::class.java)
        startForegroundService(intent)
        bindService(intent, connection, Context.BIND_AUTO_CREATE)

        setContent {
            val conn by activeConnection
            val svcReady by serviceReady

            if (conn != null) {
                MiniwaveApp(conn = conn!!, prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE))
            } else {
                val prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                val lastHost = prefs.getString("last_host", "pocketwave.local") ?: "pocketwave.local"

                ConnectScreen(
                    lastHost = lastHost,
                    onLocal = {
                        if (svcReady && service != null) {
                            val local = LocalConnection(service!!.engine)
                            local.start()
                            activeConnection.value = local
                            midi = MidiHandler(this@MainActivity, local)
                            midi?.start()
                        }
                    },
                    onRemote = { host ->
                        prefs.edit().putString("last_host", host).apply()
                        val remote = RemoteConnection(host, HTTP_PORT)
                        remote.start()
                        activeConnection.value = remote
                        midi = MidiHandler(this@MainActivity, remote)
                        midi?.start()
                    }
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        hideSystemUI()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUI()
    }

    override fun onDestroy() {
        activeConnection.value?.stop()
        midi?.stop()
        if (bound) {
            unbindService(connection)
            bound = false
        }
        super.onDestroy()
    }

    private fun hideSystemUI() {
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            or View.SYSTEM_UI_FLAG_FULLSCREEN
            or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        )
    }

    private fun extractWebAssets() {
        val webDir = File(filesDir, "web")
        val versionFile = File(webDir, ".version")
        val currentVersion = try {
            packageManager.getPackageInfo(packageName, 0).versionName ?: "0"
        } catch (_: Exception) { "0" }

        if (versionFile.exists() && versionFile.readText().trim() == currentVersion) return

        webDir.mkdirs()
        val webAssets = assets.list("web") ?: return
        for (filename in webAssets) {
            try {
                assets.open("web/$filename").use { input ->
                    FileOutputStream(File(webDir, filename)).use { output ->
                        input.copyTo(output)
                    }
                }
            } catch (_: Exception) {}
        }
        versionFile.writeText(currentVersion)
        Log.i(TAG, "extracted ${webAssets.size} web assets")
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Root Compose UI — swipeable pager between views
 * ══════════════════════════════════════════════════════════════════════ */

enum class Page(val label: String) {
    RACK("RACK"),
    CHANNEL("SYN"),
    KEYS("KEY"),
    PLAY("PLAY"),
    EXPR("EXPR"),
    MIX("MIX"),
    VIZ("VIZ"),
    SYS("SYS"),
}

class ChSeq {
    val grid = Array(NUM_STEPS) { IntArray(NUM_ROWS) }
    var playing = false
    var octave = 4
    var lastNotes = listOf<Int>()
    var root = 0
    var scaleDegrees = intArrayOf(0, 2, 4, 5, 7, 9, 11)
    var chordIntervals: IntArray? = null
}

private fun saveSeqState(prefs: SharedPreferences, chSeqs: Array<ChSeq>) {
    val editor = prefs.edit()
    for (ch in chSeqs.indices) {
        val seq = chSeqs[ch]
        val obj = JSONObject()
        val gridArr = JSONArray()
        for (step in 0 until NUM_STEPS) {
            val stepArr = JSONArray()
            for (row in 0 until NUM_ROWS) stepArr.put(seq.grid[step][row])
            gridArr.put(stepArr)
        }
        obj.put("grid", gridArr)
        obj.put("octave", seq.octave)
        obj.put("root", seq.root)
        obj.put("scaleDegrees", JSONArray(seq.scaleDegrees.toList()))
        seq.chordIntervals?.let { obj.put("chordIntervals", JSONArray(it.toList())) }
        editor.putString("ch_seq_$ch", obj.toString())
    }
    editor.apply()
}

private fun loadSeqState(prefs: SharedPreferences, chSeqs: Array<ChSeq>) {
    for (ch in chSeqs.indices) {
        val json = prefs.getString("ch_seq_$ch", null) ?: continue
        try {
            val obj = JSONObject(json)
            val seq = chSeqs[ch]
            val gridArr = obj.optJSONArray("grid")
            if (gridArr != null) {
                for (step in 0 until minOf(gridArr.length(), NUM_STEPS)) {
                    val stepArr = gridArr.optJSONArray(step) ?: continue
                    for (row in 0 until minOf(stepArr.length(), NUM_ROWS)) {
                        seq.grid[step][row] = stepArr.optInt(row, 0)
                    }
                }
            }
            seq.octave = obj.optInt("octave", 4)
            seq.root = obj.optInt("root", 0)
            val degs = obj.optJSONArray("scaleDegrees")
            if (degs != null) seq.scaleDegrees = IntArray(degs.length()) { degs.optInt(it) }
            val chord = obj.optJSONArray("chordIntervals")
            seq.chordIntervals = if (chord != null) IntArray(chord.length()) { chord.optInt(it) } else null
        } catch (_: Exception) {}
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun MiniwaveApp(conn: MiniwaveConnection, prefs: SharedPreferences) {
    val scope = rememberCoroutineScope()
    val pagerState = rememberPagerState(initialPage = 0, pageCount = { Page.entries.size })

    // Pre-allocated arrays for hot-path reads
    val peaks = remember { FloatArray(36) }
    val scopeBuf = remember { FloatArray(512) }
    val slotInfo = remember { IntArray(64) }

    var focusedCh by remember { mutableIntStateOf(0) }
    var bpm by remember { mutableFloatStateOf(120f) }
    var masterVol by remember { mutableFloatStateOf(0.75f) }
    var connected by remember { mutableStateOf(conn.isConnected) }
    var tick by remember { mutableIntStateOf(0) }

    // ── Per-channel sequencer state (loaded from prefs) ──
    val chSeqs = remember { Array(16) { ChSeq() }.also { loadSeqState(prefs, it) } }
    var globalStep by remember { mutableIntStateOf(0) }
    var anyPlaying by remember { mutableStateOf(false) }

    // ── BPM tap tempo ──
    var lastTapTime by remember { mutableLongStateOf(0L) }
    var tapCount by remember { mutableIntStateOf(0) }
    var tapAccum by remember { mutableLongStateOf(0L) }

    // ── Persistence: 2s debounce save ──
    var saveVersion by remember { mutableIntStateOf(0) }
    LaunchedEffect(saveVersion) {
        if (saveVersion > 0) {
            delay(2000)
            saveSeqState(prefs, chSeqs)
        }
    }

    // Poll engine state at ~30fps
    LaunchedEffect(conn) {
        while (true) {
            conn.pollPeaks(peaks)
            conn.getSlotInfo(slotInfo)
            focusedCh = conn.getFocusedCh()
            bpm = conn.getBpm()
            masterVol = conn.getMasterVolume()
            connected = conn.isConnected
            if (pagerState.currentPage == Page.VIZ.ordinal) conn.getScope(scopeBuf)
            tick++
            delay(33)
        }
    }

    // ── Master sequencer clock — accumulating nanoTime prevents drift ──
    LaunchedEffect(anyPlaying, bpm) {
        if (!anyPlaying) return@LaunchedEffect

        val nsPerStep = (60_000_000_000.0 / bpm.toDouble() / 4.0).toLong().coerceAtLeast(50_000_000L)
        var nextStepNs = System.nanoTime()

        while (anyPlaying) {
            // Tick all 16 channels at once
            for (ch in 0 until 16) {
                val seq = chSeqs[ch]
                if (!seq.playing) continue

                // Note off previous step
                for (note in seq.lastNotes) conn.noteOff(ch, note)

                // Note on current step — scale/chord-aware
                val notes = mutableListOf<Int>()
                for (row in 0 until NUM_ROWS) {
                    val vel = seq.grid[globalStep][row]
                    if (vel > 0) {
                        val rowNotes = gridRowToNotes(
                            row, seq.octave, seq.root,
                            seq.scaleDegrees, seq.chordIntervals
                        )
                        for (n in rowNotes) {
                            conn.noteOn(ch, n, vel)
                            notes.add(n)
                        }
                    }
                }
                seq.lastNotes = notes
            }

            globalStep = (globalStep + 1) % NUM_STEPS

            // Accumulating target — catches up if a tick was late, never drifts
            nextStepNs += nsPerStep
            val sleepMs = ((nextStepNs - System.nanoTime()) / 1_000_000L).coerceAtLeast(1L)
            delay(sleepMs)
        }

        // Stop all notes on all channels
        for (ch in 0 until 16) {
            val seq = chSeqs[ch]
            for (note in seq.lastNotes) conn.noteOff(ch, note)
            seq.lastNotes = emptyList()
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Mw.bg)
    ) {
        // Transport header
        Canvas(
            modifier = Modifier
                .fillMaxWidth()
                .height(28.dp)
                .background(Mw.panel)
                .pointerInput(Unit) {
                    detectTapGestures { offset ->
                        val w = size.width.toFloat()
                        // Play button
                        if (offset.x in (w * 0.54f)..(w * 0.62f)) {
                            val chSeq = chSeqs[focusedCh]
                            if (!chSeq.playing && slotInfo[focusedCh * 4] == 0)
                                conn.setSlot(focusedCh, "fm-synth")
                            chSeq.playing = !chSeq.playing
                            if (!chSeq.playing) {
                                for (note in chSeq.lastNotes) conn.noteOff(focusedCh, note)
                                chSeq.lastNotes = emptyList()
                            }
                            anyPlaying = chSeqs.any { it.playing }
                            if (!anyPlaying) globalStep = 0
                        }
                        // Stop all button
                        if (offset.x in (w * 0.63f)..(w * 0.72f)) {
                            for (ch in 0 until 16) {
                                val seq = chSeqs[ch]
                                if (seq.playing) {
                                    for (note in seq.lastNotes) conn.noteOff(ch, note)
                                    seq.lastNotes = emptyList()
                                    seq.playing = false
                                }
                            }
                            anyPlaying = false
                            globalStep = 0
                        }
                    }
                }
        ) {
            Retro.run {
                bitmapText(12f, 4f, "POCKETWAVE", Mw.accent, 3f)
                val chColor = Mw.chColors[focusedCh % Mw.chColors.size]
                val typeName = if (slotInfo[focusedCh * 4] != 0) conn.getSlotTypeName(focusedCh) else "---"
                bitmapText(size.width * 0.25f, 4f, "CH ${focusedCh + 1}", chColor, 3f)
                bitmapText(size.width * 0.36f, 4f, typeName, Mw.dim, 3f)
                // Transport: play/stop for focused ch + stop-all
                val chPlaying = chSeqs[focusedCh].playing
                val playLabel = if (chPlaying) "||" else ">"
                val playColor = if (chPlaying) Mw.good else Mw.dim
                bitmapText(size.width * 0.55f, 2f, playLabel, playColor, 4f)
                bitmapText(size.width * 0.64f, 4f, "STOP", if (anyPlaying) Mw.accent else Mw.dim.copy(alpha = 0.4f), 3f)
                bitmapText(size.width * 0.76f, 4f, "${bpm.toInt()} BPM", if (anyPlaying) Mw.good else Mw.dim, 3f)
                bitmapText(size.width * 0.90f, 4f, "${(masterVol * 100).toInt()}%", Mw.dim, 3f)
            }
        }

        // Pages
        Box(
            modifier = Modifier
                .weight(1f)
                .pointerInput(Unit) {
                    awaitEachGesture {
                        awaitFirstDown(pass = PointerEventPass.Initial)
                        var totalX = 0f
                        var fired = false
                        while (true) {
                            val event = awaitPointerEvent(PointerEventPass.Initial)
                            val pointers = event.changes.filter { it.pressed }
                            if (pointers.size >= 2 && !fired) {
                                val dx = pointers.sumOf { (it.position.x - it.previousPosition.x).toDouble() }.toFloat() / pointers.size
                                totalX += dx
                                if (kotlin.math.abs(totalX) > 80f) {
                                    fired = true
                                    val target = if (totalX < 0)
                                        (pagerState.currentPage + 1).coerceAtMost(Page.entries.size - 1)
                                    else
                                        (pagerState.currentPage - 1).coerceAtLeast(0)
                                    scope.launch { pagerState.animateScrollToPage(target) }
                                }
                            }
                            if (pointers.isEmpty()) break
                        }
                    }
                }
        ) {
            HorizontalPager(
                state = pagerState,
                userScrollEnabled = false,
                modifier = Modifier.fillMaxSize()
            ) { page ->
                tick.let { _ ->
                    Box(modifier = Modifier.fillMaxSize().padding(horizontal = 24.dp, vertical = 8.dp)) {
                        when (Page.entries[page]) {
                            Page.RACK -> RackScreen(
                                conn = conn, peaks = peaks, slotInfo = slotInfo,
                                focusedCh = focusedCh,
                                onSlotClick = { ch ->
                                    conn.setFocusedCh(ch)
                                    scope.launch { pagerState.animateScrollToPage(Page.CHANNEL.ordinal) }
                                }
                            )
                            Page.CHANNEL -> ChannelScreen(
                                conn = conn, ch = focusedCh, peaks = peaks, slotInfo = slotInfo
                            )
                            Page.KEYS -> ScaleChordScreen(
                                conn = conn, focusedCh = focusedCh,
                                currentRoot = chSeqs[focusedCh].root,
                                currentScaleDegrees = chSeqs[focusedCh].scaleDegrees,
                                currentChordIntervals = chSeqs[focusedCh].chordIntervals,
                                onScaleChordChanged = { root, degrees, intervals ->
                                    chSeqs[focusedCh].root = root
                                    chSeqs[focusedCh].scaleDegrees = degrees
                                    saveVersion++
                                    chSeqs[focusedCh].chordIntervals = intervals
                                }
                            )
                            Page.PLAY -> {
                                val chSeq = chSeqs[focusedCh]
                                PlayScreen(
                                    conn = conn, focusedCh = focusedCh, slotInfo = slotInfo,
                                    bpm = bpm, grid = chSeq.grid, playing = chSeq.playing,
                                    currentStep = globalStep, octave = chSeq.octave,
                                    onTogglePlay = {
                                        if (!chSeq.playing && slotInfo[focusedCh * 4] == 0)
                                            conn.setSlot(focusedCh, "fm-synth")
                                        chSeq.playing = !chSeq.playing
                                        if (!chSeq.playing) {
                                            for (note in chSeq.lastNotes) conn.noteOff(focusedCh, note)
                                            chSeq.lastNotes = emptyList()
                                        }
                                        anyPlaying = chSeqs.any { it.playing }
                                        if (!anyPlaying) globalStep = 0
                                    },
                                    onClear = { for (s in chSeq.grid) s.fill(0); saveVersion++ },
                                    onToggleCell = { step, row ->
                                        chSeq.grid[step][row] = if (chSeq.grid[step][row] > 0) 0 else 100
                                        saveVersion++
                                    },
                                    onSetVelocity = { step, row, vel -> chSeq.grid[step][row] = vel; saveVersion++ },
                                    onOctaveChange = { chSeq.octave = it; saveVersion++ }
                                )
                            }
                            Page.EXPR -> ExprScreen(
                                conn = conn, focusedCh = focusedCh, slotInfo = slotInfo
                            )
                            Page.MIX -> MixScreen(
                                conn = conn, peaks = peaks, slotInfo = slotInfo,
                                focusedCh = focusedCh, masterVol = masterVol,
                                onMasterVolChange = { masterVol = it; conn.setMasterVolume(it) }
                            )
                            Page.VIZ -> VizScreen(
                                scope = scopeBuf, peaks = peaks, focusedCh = focusedCh
                            )
                            Page.SYS -> SysScreen(
                                conn = conn, peaks = peaks, slotInfo = slotInfo,
                                focusedCh = focusedCh, bpm = bpm, masterVol = masterVol
                            )
                        }
                    }
                }
            }
        }

        // Tab bar
        Canvas(
            modifier = Modifier
                .fillMaxWidth()
                .height(40.dp)
                .background(Mw.panel)
                .pointerInput(Unit) {
                    detectTapGestures { offset ->
                        val tabW = size.width / (Page.entries.size + 1)
                        val idx = (offset.x / tabW).toInt()
                        if (idx < Page.entries.size) {
                            scope.launch { pagerState.animateScrollToPage(idx) }
                        } else {
                            // Tapped BPM area → tap tempo
                            val now = System.currentTimeMillis()
                            if (now - lastTapTime < 2000 && lastTapTime > 0) {
                                tapAccum += now - lastTapTime
                                tapCount++
                                if (tapCount >= 2) {
                                    val avgMs = tapAccum.toFloat() / tapCount
                                    val newBpm = (60000f / avgMs).coerceIn(30f, 300f)
                                    bpm = newBpm
                                    conn.setBpm(newBpm)
                                }
                            } else {
                                tapCount = 0
                                tapAccum = 0
                            }
                            lastTapTime = now
                        }
                    }
                }
        ) {
            val px = 4f
            val tabW = size.width / (Page.entries.size + 1)
            Page.entries.forEachIndexed { i, page ->
                val selected = pagerState.currentPage == i
                Retro.run {
                    bitmapText(tabW * i + 16f, 8f, page.label,
                        if (selected) Mw.accent else Mw.dim.copy(alpha = 0.5f), px)
                }
                if (selected) {
                    drawRect(Mw.accent, Offset(tabW * i + 8f, size.height - 3f), Size(tabW - 16f, 2f))
                }
            }
            // BPM — tappable
            val bpmText = "${bpm.toInt()} BPM"
            val bpmColor = if (anyPlaying) Mw.good else Mw.dim.copy(alpha = 0.5f)
            Retro.run {
                bitmapText(size.width - bpmText.length * px * 6f - 12f, 10f, bpmText, bpmColor, px)
            }
        }
    }
}
