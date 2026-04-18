package com.waveloop.miniwave.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
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
fun MixScreen(
    conn: MiniwaveConnection,
    peaks: FloatArray,
    slotInfo: IntArray,
    focusedCh: Int,
    masterVol: Float,
    onMasterVolChange: (Float) -> Unit,
    modifier: Modifier = Modifier
) {
    // Local volume cache — synced from engine
    val volumes = remember { FloatArray(16) }
    for (i in 0 until 16) volumes[i] = conn.getSlotVolume(i)

    var dragCh by remember { mutableStateOf(-1) }

    Canvas(
        modifier = modifier
            .fillMaxSize()
            .background(Mw.bg)
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    down.consume()
                    val pos = down.position
                    val w = size.width.toFloat()
                    val h = size.height.toFloat()
                    val stripW = w / 17f
                    val ch = (pos.x / stripW).toInt().coerceIn(0, 16)

                    val btnY = h * 0.88f
                    val isBtnArea = ch in 0..15 && pos.y > btnY
                    val isHeaderArea = ch in 0..15 && pos.y < h * 0.10f

                    if (isBtnArea) {
                        // Mute/Solo — handle on release
                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) {
                                val btnMid = pos.x - ch * stripW
                                if (btnMid < stripW * 0.5f) {
                                    val mute = slotInfo[ch * 4 + 2] != 0
                                    conn.setSlotMute(ch, !mute)
                                } else {
                                    val solo = slotInfo[ch * 4 + 3] != 0
                                    conn.setSlotSolo(ch, !solo)
                                }
                                break
                            }
                        }
                    } else if (isHeaderArea) {
                        conn.setFocusedCh(ch)
                    } else {
                        // Fader drag
                        dragCh = ch
                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) break
                            change.consume()
                            val dy = -(change.position.y - change.previousPosition.y) / (h * 0.55f)
                            when (ch) {
                                in 0..15 -> {
                                    volumes[ch] = (volumes[ch] + dy).coerceIn(0f, 1f)
                                    conn.setSlotVolume(ch, volumes[ch])
                                }
                                16 -> onMasterVolChange((masterVol + dy).coerceIn(0f, 1f))
                            }
                        }
                        dragCh = -1
                    }
                }
            }
    ) {
        val w = size.width
        val h = size.height
        val stripW = w / 17f

        scanlines(0.04f, 4f)
        bitmapText(10f, 6f, "MIX", Mw.accent, 6f)

        // 16 channel strips
        for (ch in 0 until 16) {
            val x = ch * stripW
            val active = slotInfo[ch * 4] != 0
            val mute = slotInfo[ch * 4 + 2] != 0
            val solo = slotInfo[ch * 4 + 3] != 0
            val color = Mw.chColors[ch % Mw.chColors.size]
            val focused = ch == focusedCh

            // Strip divider
            if (ch > 0) {
                drawLine(Mw.border.copy(alpha = 0.15f), Offset(x, 0f), Offset(x, h), 1f)
            }

            // Channel number
            val numColor = if (focused) color else if (active) Mw.text else Mw.dim.copy(alpha = 0.3f)
            bitmapText(x + 4f, 8f, "${ch + 1}", numColor, 3f)

            // Instrument name
            if (active) {
                val name = conn.getSlotTypeName(ch).take(4).uppercase()
                bitmapText(x + 4f, 32f, name, Mw.dim.copy(alpha = 0.5f), 2f)
            }

            // Fader track
            val faderX = x + stripW * 0.2f
            val faderW = stripW * 0.3f
            val faderTop = h * 0.12f
            val faderBot = h * 0.82f
            val faderH = faderBot - faderTop
            val vol = volumes[ch]
            val isDrag = dragCh == ch

            // Track bg
            drawRect(Mw.border.copy(alpha = 0.15f), Offset(faderX, faderTop), Size(faderW, faderH))
            // Fill
            val fillH = faderH * vol
            drawRect(
                color.copy(alpha = if (isDrag) 0.8f else if (mute) 0.15f else 0.5f),
                Offset(faderX, faderBot - fillH), Size(faderW, fillH)
            )
            // Knob
            val knobY = faderBot - fillH
            val knobH = 10f
            drawRect(color, Offset(faderX - 2f, knobY - knobH / 2f), Size(faderW + 4f, knobH))
            drawRect(Mw.text.copy(alpha = 0.3f), Offset(faderX + faderW * 0.3f, knobY - 1f), Size(faderW * 0.4f, 2f))

            // Peak meter
            val meterX = x + stripW * 0.6f
            val meterW = stripW * 0.15f
            val pkL = peaks[ch * 2].coerceIn(0f, 1f)
            val pkR = peaks[ch * 2 + 1].coerceIn(0f, 1f)
            drawRect(Mw.border.copy(alpha = 0.1f), Offset(meterX, faderTop), Size(meterW, faderH))
            drawRect(Mw.border.copy(alpha = 0.1f), Offset(meterX + meterW + 2f, faderTop), Size(meterW, faderH))
            val pkLH = faderH * pkL
            val pkRH = faderH * pkR
            val pkColor = if (mute) Mw.dim.copy(alpha = 0.2f) else color
            drawRect(pkColor, Offset(meterX, faderBot - pkLH), Size(meterW, pkLH))
            drawRect(pkColor, Offset(meterX + meterW + 2f, faderBot - pkRH), Size(meterW, pkRH))

            // Volume label
            bitmapText(x + 4f, faderBot + 4f, "${(vol * 100).toInt()}", Mw.dim, 2f)

            // Mute / Solo buttons
            val btnY = h * 0.90f
            val btnW = stripW * 0.42f
            val btnH = h * 0.08f

            drawRect(
                if (mute) Mw.warn.copy(alpha = 0.3f) else Mw.panel,
                Offset(x + 2f, btnY), Size(btnW, btnH)
            )
            drawRect(
                if (mute) Mw.warn else Mw.border.copy(alpha = 0.3f),
                Offset(x + 2f, btnY), Size(btnW, btnH), style = Stroke(1f)
            )
            bitmapText(x + 4f, btnY + btnH * 0.2f, "M", if (mute) Mw.warn else Mw.dim.copy(alpha = 0.4f), 2f)

            drawRect(
                if (solo) Mw.good.copy(alpha = 0.3f) else Mw.panel,
                Offset(x + btnW + 4f, btnY), Size(btnW, btnH)
            )
            drawRect(
                if (solo) Mw.good else Mw.border.copy(alpha = 0.3f),
                Offset(x + btnW + 4f, btnY), Size(btnW, btnH), style = Stroke(1f)
            )
            bitmapText(x + btnW + 6f, btnY + btnH * 0.2f, "S", if (solo) Mw.good else Mw.dim.copy(alpha = 0.4f), 2f)

            // Focus glow
            if (focused) {
                drawRect(color.copy(alpha = 0.15f), Offset(x, 0f), Size(stripW, h))
            }
        }

        // Master strip
        val mx = 16 * stripW
        drawLine(Mw.accent.copy(alpha = 0.4f), Offset(mx, 0f), Offset(mx, h), 2f)
        bitmapText(mx + 4f, 8f, "MST", Mw.accent, 3f)

        val mFaderX = mx + stripW * 0.2f
        val mFaderW = stripW * 0.35f
        val mFaderTop = h * 0.12f
        val mFaderBot = h * 0.82f
        val mFaderH = mFaderBot - mFaderTop
        val isDragM = dragCh == 16

        drawRect(Mw.border.copy(alpha = 0.2f), Offset(mFaderX, mFaderTop), Size(mFaderW, mFaderH))
        val mFillH = mFaderH * masterVol
        drawRect(Mw.accent.copy(alpha = if (isDragM) 0.9f else 0.6f), Offset(mFaderX, mFaderBot - mFillH), Size(mFaderW, mFillH))
        val mKnobY = mFaderBot - mFillH
        drawRect(Mw.accent, Offset(mFaderX - 2f, mKnobY - 6f), Size(mFaderW + 4f, 12f))
        drawRect(Mw.text.copy(alpha = 0.3f), Offset(mFaderX + mFaderW * 0.2f, mKnobY - 1f), Size(mFaderW * 0.6f, 2f))

        // Master peaks
        val mpkL = peaks[32].coerceIn(0f, 1f)
        val mpkR = peaks[33].coerceIn(0f, 1f)
        val mmX = mx + stripW * 0.65f
        val mmW = stripW * 0.12f
        drawRect(Mw.border.copy(alpha = 0.1f), Offset(mmX, mFaderTop), Size(mmW, mFaderH))
        drawRect(Mw.border.copy(alpha = 0.1f), Offset(mmX + mmW + 2f, mFaderTop), Size(mmW, mFaderH))
        drawRect(Mw.accent, Offset(mmX, mFaderBot - mFaderH * mpkL), Size(mmW, mFaderH * mpkL))
        drawRect(Mw.accent, Offset(mmX + mmW + 2f, mFaderBot - mFaderH * mpkR), Size(mmW, mFaderH * mpkR))

        bitmapText(mx + 4f, mFaderBot + 4f, "${(masterVol * 100).toInt()}", Mw.accent, 2f)
    }
}
