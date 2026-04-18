package com.waveloop.miniwave.ui

import android.os.Debug
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import com.waveloop.miniwave.MiniwaveConnection
import com.waveloop.miniwave.ui.Retro.bitmapText
import com.waveloop.miniwave.ui.Retro.glowLine
import com.waveloop.miniwave.ui.Retro.scanlines

@Composable
fun SysScreen(
    conn: MiniwaveConnection,
    peaks: FloatArray,
    slotInfo: IntArray,
    focusedCh: Int,
    bpm: Float,
    masterVol: Float,
    modifier: Modifier = Modifier
) {
    val rt = Runtime.getRuntime()
    val nativeHeap = Debug.getNativeHeapAllocatedSize() / 1024
    val nativeTotal = Debug.getNativeHeapSize() / 1024
    val javaUsed = (rt.totalMemory() - rt.freeMemory()) / 1024
    val javaMax = rt.maxMemory() / 1024

    var activeSlots = 0
    var mutedSlots = 0
    var soloSlots = 0
    for (i in 0 until 16) {
        if (slotInfo[i * 4] != 0) activeSlots++
        if (slotInfo[i * 4 + 2] != 0) mutedSlots++
        if (slotInfo[i * 4 + 3] != 0) soloSlots++
    }

    Canvas(
        modifier = modifier
            .fillMaxSize()
            .background(Mw.bg)
            .pointerInput(Unit) {
                detectTapGestures { offset ->
                    val w = size.width.toFloat()
                    val h = size.height.toFloat()
                    // PANIC button
                    if (offset.x > w * 0.02f && offset.x < w * 0.25f && offset.y > h * 0.88f) {
                        conn.panic()
                    }
                }
            }
    ) {
        val w = size.width
        val h = size.height
        val px = 3f
        val lineH = px * 12f

        scanlines(0.04f, 4f)

        bitmapText(10f, 6f, "SYSTEM", Mw.accent, 6f)
        glowLine(Offset(10f, 60f), Offset(w * 0.48f, 60f), Mw.accent.copy(alpha = 0.2f), 1f, 4f)

        var y = 76f
        val col1 = 10f
        val col2 = w * 0.52f

        // ── AUDIO ──
        bitmapText(col1, y, "AUDIO", Mw.good, 4f)
        y += lineH * 1.5f
        bitmapText(col1, y, "SAMPLE RATE", Mw.dim, px); bitmapText(col1 + 220f, y, "48000 HZ", Mw.text, px)
        y += lineH
        bitmapText(col1, y, "CHANNELS", Mw.dim, px); bitmapText(col1 + 220f, y, "2 STEREO", Mw.text, px)
        y += lineH
        bitmapText(col1, y, "BUFFER", Mw.dim, px); bitmapText(col1 + 220f, y, "96 FRAMES  2.0MS", Mw.text, px)
        y += lineH
        bitmapText(col1, y, "MODE", Mw.dim, px); bitmapText(col1 + 220f, y, if (conn.isLocal) "AAUDIO EXCLUSIVE" else "REMOTE HTTP", Mw.text, px)
        y += lineH
        bitmapText(col1, y, "FX BUFFER", Mw.dim, px); bitmapText(col1 + 220f, y, "512 FRAMES  10.7MS", Mw.text, px)

        // ── ENGINE ──
        y += lineH * 2f
        bitmapText(col1, y, "ENGINE", Mw.good, 4f)
        y += lineH * 1.5f
        bitmapText(col1, y, "ACTIVE SLOTS", Mw.dim, px); bitmapText(col1 + 220f, y, "$activeSlots / 16", Mw.text, px)
        y += lineH
        bitmapText(col1, y, "MUTED", Mw.dim, px); bitmapText(col1 + 220f, y, "$mutedSlots", if (mutedSlots > 0) Mw.warn else Mw.text, px)
        y += lineH
        bitmapText(col1, y, "SOLO", Mw.dim, px); bitmapText(col1 + 220f, y, "$soloSlots", if (soloSlots > 0) Mw.good else Mw.text, px)
        y += lineH
        bitmapText(col1, y, "FOCUSED CH", Mw.dim, px); bitmapText(col1 + 220f, y, "${focusedCh + 1}", Mw.chColors[focusedCh % Mw.chColors.size], px)
        y += lineH
        bitmapText(col1, y, "BPM", Mw.dim, px); bitmapText(col1 + 220f, y, "${bpm.toInt()}", Mw.text, px)
        y += lineH
        bitmapText(col1, y, "MASTER VOL", Mw.dim, px); bitmapText(col1 + 220f, y, "${(masterVol * 100).toInt()}%", Mw.text, px)

        // ── MEMORY ──  (right column)
        var ry = 76f
        bitmapText(col2, ry, "MEMORY", Mw.good, 4f)
        ry += lineH * 1.5f
        bitmapText(col2, ry, "NATIVE HEAP", Mw.dim, px); bitmapText(col2 + 220f, ry, "${nativeHeap}K / ${nativeTotal}K", Mw.text, px)
        ry += lineH
        bitmapText(col2, ry, "JAVA HEAP", Mw.dim, px); bitmapText(col2 + 220f, ry, "${javaUsed}K / ${javaMax}K", Mw.text, px)
        ry += lineH
        bitmapText(col2, ry, "SLOT CACHE", Mw.dim, px); bitmapText(col2 + 220f, ry, "16K X 8 TYPES", Mw.text, px)
        ry += lineH
        bitmapText(col2, ry, "SCOPE BUF", Mw.dim, px); bitmapText(col2 + 220f, ry, "512 FLOATS", Mw.text, px)

        // ── SLOTS ──
        ry += lineH * 2f
        bitmapText(col2, ry, "SLOTS", Mw.good, 4f)
        ry += lineH * 1.5f
        for (ch in 0 until 16) {
            val active = slotInfo[ch * 4] != 0
            val mute = slotInfo[ch * 4 + 2] != 0
            val solo = slotInfo[ch * 4 + 3] != 0
            val color = Mw.chColors[ch % Mw.chColors.size]
            val typeName = if (active) conn.getSlotTypeName(ch).take(8) else "---"
            val flags = buildString {
                if (mute) append("M")
                if (solo) append("S")
            }
            val pkL = peaks[ch * 2].coerceIn(0f, 1f)

            bitmapText(col2, ry, "${ch + 1}", color, 2f)
            bitmapText(col2 + 30f, ry, typeName.uppercase(), if (active) Mw.text else Mw.dim.copy(alpha = 0.2f), 2f)
            bitmapText(col2 + 170f, ry, flags, if (mute) Mw.warn else Mw.good, 2f)

            // Tiny peak bar
            val barX = col2 + 200f
            val barW = 120f
            drawRect(Mw.border.copy(alpha = 0.1f), Offset(barX, ry + 2f), Size(barW, 8f))
            if (active) drawRect(color.copy(alpha = 0.6f), Offset(barX, ry + 2f), Size(barW * pkL, 8f))

            ry += lineH * 0.8f
        }

        // ── MASTER PEAK ──
        val mpkL = peaks[32].coerceIn(0f, 1f)
        val mpkR = peaks[33].coerceIn(0f, 1f)
        glowLine(Offset(col2, ry + 10f), Offset(col2 + 340f, ry + 10f), Mw.accent.copy(alpha = 0.15f), 1f, 4f)
        ry += 20f
        bitmapText(col2, ry, "MASTER L", Mw.dim, 2f)
        drawRect(Mw.border.copy(alpha = 0.1f), Offset(col2 + 120f, ry + 2f), Size(200f, 8f))
        drawRect(Mw.accent, Offset(col2 + 120f, ry + 2f), Size(200f * mpkL, 8f))
        ry += lineH * 0.8f
        bitmapText(col2, ry, "MASTER R", Mw.dim, 2f)
        drawRect(Mw.border.copy(alpha = 0.1f), Offset(col2 + 120f, ry + 2f), Size(200f, 8f))
        drawRect(Mw.accent, Offset(col2 + 120f, ry + 2f), Size(200f * mpkR, 8f))

        // PANIC button
        val panicY = h * 0.88f
        drawRect(Mw.accent.copy(alpha = 0.15f), Offset(col1, panicY), Size(w * 0.22f, h * 0.10f))
        drawRect(Mw.accent, Offset(col1, panicY), Size(w * 0.22f, h * 0.10f), style = Stroke(2f))
        bitmapText(col1 + 14f, panicY + 10f, "PANIC", Mw.accent, 5f)
        bitmapText(col1 + 14f, panicY + 50f, "ALL NOTES OFF", Mw.dim, 2f)

        // Build info
        bitmapText(col1 + w * 0.26f, h - 30f, "POCKETWAVE ANDROID  LOCAL ENGINE", Mw.dim.copy(alpha = 0.2f), 2f)
    }
}
