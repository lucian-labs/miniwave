package com.waveloop.miniwave.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import com.waveloop.miniwave.MiniwaveConnection
import com.waveloop.miniwave.ui.Retro.bitmapText
import com.waveloop.miniwave.ui.Retro.diamond
import com.waveloop.miniwave.ui.Retro.glowLine
import com.waveloop.miniwave.ui.Retro.scanlines

/**
 * Step sequencer — preset KeySeq patterns as a grid.
 * Tap a pattern to load it, tap again to stop.
 * Bottom section shows a visual step grid for the active pattern.
 */

data class SeqPreset(val name: String, val dsl: String, val steps: Int = 8)

private val PRESETS = listOf(
    SeqPreset("ARP UP",     "t0.2; g0.8; gated; algo; n:root+i*7", 8),
    SeqPreset("ARP DOWN",   "t0.2; g0.8; gated; algo; n:root+(8-i)*7; end:i>=8", 8),
    SeqPreset("ARP UD",     "t0.2; g0.8; gated; algo; n:root+sin(i*0.785)*12", 8),
    SeqPreset("TRILL",      "t0.12; g0.5; gated; algo; n:root+i%2*7", 4),
    SeqPreset("OCTAVE",     "t0.25; g0.9; gated; algo; n:root+i%2*12", 4),
    SeqPreset("STUTTER",    "t0.1; g0.3; gated; algo; n:root", 4),
    SeqPreset("FIFTH",      "t0.25; g0.7; gated; algo; n:root+i%3*7", 6),
    SeqPreset("TRIAD",      "0,4,7", 3),
    SeqPreset("RANDOM",     "t0.2; g0.6; gated; algo; n:root+noise(i*0.5)*24", 8),
    SeqPreset("GLITCH",     "t0.1; g0.4; gated; algo; n:root+noise(i,time)*12; g:0.3+noise(i)*0.5", 16),
    SeqPreset("SWEEP",      "t0.1; g0.4; gated; algo; n:root+i*2; end:i>=16", 16),
    SeqPreset("BOUNCE",     "t0.2; g0.7; gated; algo; n:root+sin(i*0.5)*12", 8),
    SeqPreset("PULSE",      "t0.06; g0.2; gated; algo; n:root", 8),
    SeqPreset("DRIFT",      "t0.25; g0.8; gated; algo; n:root+noiseb(i*0.3)*7; seed:n", 8),
    SeqPreset("SPARSE",     "t0.5; g0.3; gated; algo; n:root+i*5; end:i>=4", 4),
    SeqPreset("WIDE",       "t0.4; g0.9; gated; algo; n:root+i*12-24; end:i>=5", 5),
)

@Composable
fun ExprScreen(
    conn: MiniwaveConnection,
    focusedCh: Int,
    slotInfo: IntArray = IntArray(64),
    modifier: Modifier = Modifier
) {
    var activeIdx by remember { mutableIntStateOf(-1) }
    var enabled by remember { mutableStateOf(false) }

    // Check current state
    LaunchedEffect(focusedCh) {
        enabled = conn.keyseqIsEnabled(focusedCh)
        val currentDsl = conn.keyseqGetDsl(focusedCh)
        activeIdx = PRESETS.indexOfFirst { it.dsl == currentDsl }
    }

    val chColor = Mw.chColors[focusedCh % Mw.chColors.size]

    Canvas(
        modifier = modifier
            .fillMaxSize()
            .background(Mw.bg)
            .pointerInput(focusedCh) {
                detectTapGestures { offset ->
                    val w = size.width.toFloat()
                    val h = size.height.toFloat()
                    // Grid area: 4 columns × 4 rows, starts at y=0.12, ends at y=0.75
                    val gridTop = h * 0.12f
                    val gridBot = h * 0.72f
                    val gridH = gridBot - gridTop
                    val cols = 4
                    val rows = 4
                    val cellW = w / cols
                    val cellH = gridH / rows

                    if (offset.y in gridTop..gridBot) {
                        val col = (offset.x / cellW).toInt().coerceIn(0, cols - 1)
                        val row = ((offset.y - gridTop) / cellH).toInt().coerceIn(0, rows - 1)
                        val idx = row * cols + col

                        if (idx < PRESETS.size) {
                            // Select only — don't auto-start
                            if (slotInfo[focusedCh * 4] == 0) conn.setSlot(focusedCh, "fm-synth")
                            conn.keyseqDsl(focusedCh, PRESETS[idx].dsl)
                            activeIdx = idx
                        }
                    }

                    // START — arm keyseq, waits for key gate
                    if (offset.x in (w * 0.62f)..(w * 0.78f) && offset.y > h * 0.82f) {
                        if (activeIdx >= 0 && !enabled) {
                            if (slotInfo[focusedCh * 4] == 0) conn.setSlot(focusedCh, "fm-synth")
                            conn.keyseqEnable(focusedCh, true)
                            enabled = true
                        }
                    }

                    // STOP
                    if (offset.x > w * 0.80f && offset.y > h * 0.82f) {
                        conn.keyseqStop(focusedCh)
                        conn.keyseqEnable(focusedCh, false)
                        enabled = false
                    }
                }
            }
    ) {
        val w = size.width
        val h = size.height

        scanlines(0.04f, 4f)

        // Header
        bitmapText(10f, 10f, "EXPR", Mw.accent, 6f)
        bitmapText(w * 0.12f, 12f, "CH ${focusedCh + 1}", chColor, 4f)

        if (activeIdx >= 0) {
            bitmapText(w * 0.25f, 12f, PRESETS[activeIdx].name, if (enabled) Mw.good else chColor, 4f)
            if (enabled) bitmapText(w * 0.55f, 12f, "ARMED", Mw.good, 3f)
        }

        // Preset grid: 4×4
        val gridTop = h * 0.12f
        val cols = 4
        val rows = 4
        val cellW = w / cols
        val cellH = (h * 0.60f) / rows

        for (i in PRESETS.indices) {
            val col = i % cols
            val row = i / cols
            val x = col * cellW + 4f
            val y = gridTop + row * cellH + 2f
            val cw = cellW - 8f
            val ch = cellH - 4f
            val isSelected = i == activeIdx
            val isPlaying = isSelected && enabled

            // Cell background
            if (isPlaying) {
                drawRect(chColor.copy(alpha = 0.15f), Offset(x, y), Size(cw, ch))
            } else if (isSelected) {
                drawRect(chColor.copy(alpha = 0.06f), Offset(x, y), Size(cw, ch))
            }

            // Cell border
            drawRect(
                if (isPlaying) chColor else if (isSelected) chColor.copy(alpha = 0.6f) else Mw.border.copy(alpha = 0.4f),
                Offset(x, y), Size(cw, ch),
                style = Stroke(width = if (isSelected) 2f else 1f)
            )

            // Name
            bitmapText(x + 8f, y + 8f, PRESETS[i].name,
                if (isPlaying) chColor else if (isSelected) chColor.copy(alpha = 0.8f) else Mw.dim, 3f)

            // Mini step visualization
            val steps = PRESETS[i].steps
            val stepW = (cw - 16f) / steps
            for (s in 0 until steps) {
                val sx = x + 8f + s * stepW
                val sy = y + ch - 14f
                val barH = 8f * (1f - (s.toFloat() / steps) * 0.3f)
                drawRect(
                    if (isPlaying) chColor.copy(alpha = 0.6f)
                    else if (isSelected) chColor.copy(alpha = 0.3f)
                    else Mw.border.copy(alpha = 0.3f),
                    Offset(sx, sy + 8f - barH), Size(stepW - 2f, barH)
                )
            }

            // Playing indicator
            if (isPlaying) diamond(Offset(x + cw - 12f, y + 12f), 4f, Mw.good)
            // Selected indicator
            else if (isSelected) diamond(Offset(x + cw - 12f, y + 12f), 3f, chColor.copy(alpha = 0.5f))
        }

        // Bottom section: active pattern info + stop button
        val bottomY = h * 0.78f

        glowLine(Offset(12f, bottomY), Offset(w - 12f, bottomY), chColor.copy(alpha = 0.2f), 1f, 6f)

        if (enabled && activeIdx >= 0) {
            // Show DSL
            bitmapText(12f, bottomY + 12f, "DSL:", Mw.dim, 3f)
            bitmapText(70f, bottomY + 12f, PRESETS[activeIdx].dsl.take(40), chColor.copy(alpha = 0.7f), 3f)

            // Visual step line
            val steps = PRESETS[activeIdx].steps
            val lineX = 12f
            val lineW = w * 0.70f
            val stepW = lineW / steps
            for (s in 0 until steps) {
                val sx = lineX + s * stepW
                drawRect(chColor.copy(alpha = 0.5f),
                    Offset(sx, bottomY + 42f), Size(stepW - 3f, 12f))
                drawRect(chColor,
                    Offset(sx, bottomY + 42f), Size(stepW - 3f, 12f),
                    style = Stroke(1f))
            }
        } else if (activeIdx >= 0) {
            bitmapText(12f, bottomY + 12f, "DSL:", Mw.dim, 3f)
            bitmapText(70f, bottomY + 12f, PRESETS[activeIdx].dsl.take(40), chColor.copy(alpha = 0.5f), 3f)
            bitmapText(12f, bottomY + 44f, "SELECTED  >  ARM AND PLAY KEYS TO TRIGGER", Mw.dim.copy(alpha = 0.5f), 3f)
        } else {
            bitmapText(12f, bottomY + 16f, "SELECT A PATTERN", Mw.dim.copy(alpha = 0.5f), 4f)
        }

        // START button
        val startX = w * 0.63f
        val btnY = h * 0.84f
        val btnW = w * 0.15f
        val btnH = h * 0.10f
        val canStart = activeIdx >= 0 && !enabled
        drawRect(if (canStart) Mw.good.copy(alpha = 0.2f) else Mw.panel,
            Offset(startX, btnY), Size(btnW, btnH))
        drawRect(if (canStart) Mw.good else Mw.border.copy(alpha = 0.3f),
            Offset(startX, btnY), Size(btnW, btnH),
            style = Stroke(if (canStart) 2f else 1f))
        bitmapText(startX + 10f, btnY + 12f, "START",
            if (canStart) Mw.good else Mw.dim.copy(alpha = 0.4f), 4f)

        // STOP button
        val stopX = w * 0.82f
        drawRect(if (enabled) Mw.accent.copy(alpha = 0.2f) else Mw.panel,
            Offset(stopX, btnY), Size(btnW, btnH))
        drawRect(if (enabled) Mw.accent else Mw.border.copy(alpha = 0.3f),
            Offset(stopX, btnY), Size(btnW, btnH),
            style = Stroke(if (enabled) 2f else 1f))
        bitmapText(stopX + 14f, btnY + 12f, "STOP",
            if (enabled) Mw.accent else Mw.dim.copy(alpha = 0.4f), 4f)
    }
}
