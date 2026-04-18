package com.waveloop.miniwave.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import com.waveloop.miniwave.MiniwaveConnection
import com.waveloop.miniwave.ui.Retro.bitmapText
import com.waveloop.miniwave.ui.Retro.glowLine
import com.waveloop.miniwave.ui.Retro.scanlines
import kotlin.math.max

/** 16-channel rack grid — retro vector style */
@Composable
fun RackScreen(
    conn: MiniwaveConnection,
    peaks: FloatArray,
    slotInfo: IntArray,
    focusedCh: Int,
    onSlotClick: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    // Cache type names (avoid JNI call per frame)
    val typeNames = Array(16) { i ->
        if (slotInfo[i * 4] != 0) conn.getSlotTypeName(i) else ""
    }

    Canvas(
        modifier = modifier
            .fillMaxSize()
            .background(Mw.bg)
            .pointerInput(Unit) {
                detectTapGestures { offset ->
                    val cols = 4
                    val rows = 4
                    val slotW = size.width / cols
                    val slotH = (size.height - 40f) / rows // leave room for header
                    val col = (offset.x / slotW).toInt().coerceIn(0, cols - 1)
                    val row = ((offset.y - 36f) / slotH).toInt().coerceIn(0, rows - 1)
                    val ch = row * cols + col
                    if (ch in 0..15) onSlotClick(ch)
                }
            }
    ) {
        val w = size.width
        val h = size.height
        val px = 3f

        // Scanlines
        scanlines(0.04f, 3f)

        // Header
        bitmapText(10f, 8f, "RACK", Mw.accent, 6f)

        // Master meter (top right)
        val masterL = peaks[32]
        val masterR = peaks[33]
        val meterW = 60f
        val meterH = 8f
        val meterX = w - meterW - 12f
        drawRect(Mw.border.copy(alpha = 0.3f), Offset(meterX, 6f), Size(meterW, meterH))
        drawRect(Mw.accent, Offset(meterX, 6f), Size(meterW * masterL.coerceIn(0f, 1f), meterH / 2f - 1f))
        drawRect(Mw.accent, Offset(meterX, 6f + meterH / 2f + 1f), Size(meterW * masterR.coerceIn(0f, 1f), meterH / 2f - 1f))

        // 16 slots — 4 columns × 4 rows
        val cols = 4
        val rows = 4
        val startY = 50f
        val slotW = w / cols
        val slotH = (h - startY) / rows

        for (i in 0 until 16) {
            val col = i % cols
            val row = i / cols
            val x = col * slotW
            val y = startY + row * slotH

            val active = slotInfo[i * 4] != 0
            val mute = slotInfo[i * 4 + 2] != 0
            val solo = slotInfo[i * 4 + 3] != 0
            val isFocused = i == focusedCh
            val color = Mw.chColors[i]
            val pkL = peaks[i * 2]
            val pkR = peaks[i * 2 + 1]

            // Slot border
            val borderColor = if (isFocused) color else Mw.border.copy(alpha = 0.3f)
            drawRect(borderColor, Offset(x + 2f, y + 1f), Size(slotW - 4f, slotH - 2f),
                style = androidx.compose.ui.graphics.drawscope.Stroke(width = if (isFocused) 2f else 1f))

            // Background glow for focused
            if (isFocused) {
                drawRect(color.copy(alpha = 0.05f), Offset(x + 3f, y + 2f), Size(slotW - 6f, slotH - 4f))
            }

            // Channel number (top-left corner, small)
            bitmapText(x + 6f, y + 4f, "${i + 1}", if (isFocused) color.copy(alpha = 0.7f) else Mw.dim.copy(alpha = 0.4f), 2f)

            // Mute/Solo indicators (top-right)
            if (active) {
                if (mute) bitmapText(x + slotW - 30f, y + 4f, "M", Mw.warn, 2f)
                if (solo) bitmapText(x + slotW - 16f, y + 4f, "S", Mw.good, 2f)
            }

            // Center content
            val centerY = y + slotH / 2f
            if (active) {
                val name = typeNames[i]
                // Type name — centered in cell, big
                val textPx = 4f
                val textW = name.length * textPx * 6.4f  // approx width with bold
                bitmapText(x + (slotW - textW) / 2f, centerY - textPx * 5f, name,
                    if (isFocused) Mw.text else Mw.text.copy(alpha = 0.6f), textPx)

                // Peak meter bars (bottom of cell, full width)
                val mBarX = x + 8f
                val mBarW = slotW - 16f
                val mBarY = y + slotH - 14f
                drawRect(Mw.border.copy(alpha = 0.15f), Offset(mBarX, mBarY), Size(mBarW, 3f))
                drawRect(Mw.border.copy(alpha = 0.15f), Offset(mBarX, mBarY + 5f), Size(mBarW, 3f))
                drawRect(color.copy(alpha = 0.8f), Offset(mBarX, mBarY), Size(mBarW * pkL.coerceIn(0f, 1f), 3f))
                drawRect(color.copy(alpha = 0.8f), Offset(mBarX, mBarY + 5f), Size(mBarW * pkR.coerceIn(0f, 1f), 3f))
            } else {
                // Empty slot — dim channel number centered
                val numText = "${i + 1}"
                val numW = numText.length * 5f * 6.4f
                bitmapText(x + (slotW - numW) / 2f, centerY - 14f, numText,
                    Mw.dim.copy(alpha = 0.15f), 5f)
            }
        }
    }
}
