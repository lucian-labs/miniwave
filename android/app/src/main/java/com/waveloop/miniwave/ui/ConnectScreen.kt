package com.waveloop.miniwave.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import com.waveloop.miniwave.ui.Retro.bitmapText
import com.waveloop.miniwave.ui.Retro.diamond
import com.waveloop.miniwave.ui.Retro.glowLine
import com.waveloop.miniwave.ui.Retro.scanlines

@Composable
fun ConnectScreen(
    lastHost: String,
    onLocal: () -> Unit,
    onRemote: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    var host by remember { mutableStateOf(lastHost.ifEmpty { "pocketwave.local" }) }

    Box(modifier = modifier.fillMaxSize().background(Mw.bg)) {
        Canvas(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(Unit) {
                    detectTapGestures { offset ->
                        val h = size.height.toFloat()
                        val w = size.width.toFloat()
                        if (offset.y in (h * 0.30f)..(h * 0.58f)) onLocal()
                        if (offset.x > w * 0.70f && offset.y > h * 0.80f) onRemote(host)
                    }
                }
        ) {
            val w = size.width
            val h = size.height

            scanlines(0.04f, 4f)

            // ── Logo — big and centered ──
            val logoText = "POCKETWAVE"
            val logoPx = 8f
            val logoW = logoText.length * logoPx * 6.4f
            bitmapText((w - logoW) / 2f, h * 0.04f, logoText, Mw.accent, logoPx)

            // Decorative waveform under logo
            val waveY = h * 0.17f
            for (i in 0..80) {
                val x = w * 0.1f + i * (w * 0.8f / 80f)
                val amp = kotlin.math.sin(i * 0.35f) * 16f * (1f - kotlin.math.abs(i - 40f) / 45f)
                drawRect(Mw.accent.copy(alpha = 0.35f), Offset(x, waveY - amp), Size(4f, 4f))
            }
            glowLine(Offset(w * 0.1f, waveY + 12f), Offset(w * 0.9f, waveY + 12f), Mw.accent.copy(alpha = 0.2f), 1f, 8f)

            // ── LOCAL ENGINE — big tappable box ──
            val boxL = w * 0.06f
            val boxW = w * 0.88f
            val boxY = h * 0.28f
            val boxH = h * 0.28f

            drawRect(Mw.good.copy(alpha = 0.12f), Offset(boxL, boxY), Size(boxW, boxH))
            drawRect(Mw.good.copy(alpha = 0.6f), Offset(boxL, boxY), Size(boxW, boxH), style = Stroke(2f))

            bitmapText(boxL + 20f, boxY + 16f, "LOCAL ENGINE", Mw.good, 7f)
            bitmapText(boxL + 20f, boxY + 80f, "RUN SYNTH ON THIS DEVICE", Mw.dim.copy(alpha = 0.6f), 4f)
            bitmapText(boxL + 20f, boxY + 120f, "AAUDIO   USB MIDI   16 CH", Mw.dim.copy(alpha = 0.3f), 3f)

            diamond(Offset(boxL + boxW - 30f, boxY + boxH / 2f), 8f, Mw.good.copy(alpha = 0.5f))

            // ── REMOTE CONTROL — bottom box ──
            val rBoxY = h * 0.62f
            val rBoxH = h * 0.30f

            drawRect(Mw.accent.copy(alpha = 0.08f), Offset(boxL, rBoxY), Size(boxW, rBoxH))
            drawRect(Mw.accent.copy(alpha = 0.4f), Offset(boxL, rBoxY), Size(boxW, rBoxH), style = Stroke(1f))

            bitmapText(boxL + 20f, rBoxY + 16f, "REMOTE CONTROL", Mw.accent, 6f)
            bitmapText(boxL + 20f, rBoxY + 72f, "CONNECT TO MINIWAVE ON NETWORK", Mw.dim.copy(alpha = 0.5f), 3f)

            // Host value — bitmap font, greyed, inline in the box
            bitmapText(boxL + 20f, rBoxY + rBoxH - 70f, host.uppercase(), Mw.dim.copy(alpha = 0.35f), 5f)

            // CONNECT button
            val cBtnX = w * 0.74f
            val cBtnY = rBoxY + rBoxH - 50f
            drawRect(Mw.accent, Offset(cBtnX, cBtnY), Size(w * 0.18f, 40f))
            bitmapText(cBtnX + 14f, cBtnY + 10f, "CONNECT", Mw.bg, 4f)
        }

        // Invisible text field for keyboard input — overlaid on the host text area
        BasicTextField(
            value = host,
            onValueChange = { host = it },
            textStyle = TextStyle(color = Mw.text.copy(alpha = 0f), fontSize = Mw.bodySize, fontFamily = Mw.mono),
            cursorBrush = SolidColor(Mw.accent),
            singleLine = true,
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(start = 40.dp, bottom = 52.dp)
                .width(320.dp)
                .height(40.dp)
        )
    }
}
