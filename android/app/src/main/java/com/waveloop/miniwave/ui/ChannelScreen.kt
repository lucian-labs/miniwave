package com.waveloop.miniwave.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
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
import com.waveloop.miniwave.ui.Retro.diamond
import com.waveloop.miniwave.ui.Retro.glowLine
import com.waveloop.miniwave.ui.Retro.scanlines
import kotlinx.coroutines.delay
import org.json.JSONObject

data class Param(val name: String, val value: Float, val min: Float = 0f, val max: Float = 1f)

@Composable
fun ChannelScreen(
    conn: MiniwaveConnection,
    ch: Int,
    peaks: FloatArray,
    slotInfo: IntArray,
    modifier: Modifier = Modifier
) {
    val active = slotInfo[ch * 4] != 0
    val mute = slotInfo[ch * 4 + 2] != 0
    val solo = slotInfo[ch * 4 + 3] != 0
    val color = Mw.chColors[ch % Mw.chColors.size]
    val typeName = if (active) conn.getSlotTypeName(ch) else "EMPTY"
    val typeNames = remember { conn.getTypeNames() }

    var volume by remember(ch) { mutableFloatStateOf(conn.getSlotVolume(ch)) }
    var params by remember(ch, typeName) { mutableStateOf(parseParams(conn.getChannelJson(ch))) }
    var dragParam by remember { mutableStateOf<String?>(null) }

    // Refresh from engine when not dragging (tracks MIDI CC + external changes)
    LaunchedEffect(ch, typeName, dragParam) {
        while (dragParam == null) {
            delay(100)
            volume = conn.getSlotVolume(ch)
            params = parseParams(conn.getChannelJson(ch))
        }
    }

    Canvas(
        modifier = modifier
            .fillMaxSize()
            .background(Mw.bg)
            .pointerInput(ch) {
                detectDragGestures(
                    onDragStart = { offset ->
                        val w = size.width.toFloat()
                        val h = size.height.toFloat()
                        val midX = w * 0.50f
                        // Volume slider zone
                        if (offset.x < midX && offset.y in (h * 0.12f)..(h * 0.28f)) {
                            dragParam = "__volume__"
                        }
                        // Param sliders — right half. Don't re-key on params so drag stays locked
                        if (offset.x > midX && params.isNotEmpty()) {
                            val paramH = minOf((h - 60f) / params.size, h * 0.12f)
                            val paramIdx = ((offset.y - h * 0.08f) / paramH).toInt()
                            if (paramIdx in params.indices) {
                                dragParam = params[paramIdx].name
                            }
                        }
                    },
                    onDragEnd = { dragParam = null },
                    onDragCancel = { dragParam = null }
                ) { change, dragAmount ->
                    change.consume() // prevent parent from stealing
                    val w = size.width.toFloat()
                    when (dragParam) {
                        "__volume__" -> {
                            volume = (volume + dragAmount.x / (w * 0.40f)).coerceIn(0f, 1f)
                            conn.setSlotVolume(ch, volume)
                        }
                        null -> {}
                        else -> {
                            val idx = params.indexOfFirst { it.name == dragParam }
                            if (idx >= 0) {
                                val p = params[idx]
                                val range = p.max - p.min
                                val newVal = (p.value + dragAmount.x / (w * 0.36f) * range).coerceIn(p.min, p.max)
                                params = params.toMutableList().also {
                                    it[idx] = p.copy(value = newVal)
                                }
                                conn.setParam(ch, p.name, newVal)
                            }
                        }
                    }
                }
            }
            .pointerInput(ch, typeNames) {
                detectTapGestures { offset ->
                    val w = size.width.toFloat()
                    val h = size.height.toFloat()

                    if (offset.x < w * 0.16f && offset.y in (h * 0.33f)..(h * 0.48f))
                        conn.setSlotMute(ch, !mute)
                    if (offset.x in (w * 0.18f)..(w * 0.34f) && offset.y in (h * 0.33f)..(h * 0.48f))
                        conn.setSlotSolo(ch, !solo)
                    if (offset.x < w * 0.48f && offset.y in (h * 0.54f)..(h * 0.78f)) {
                        val btnW = (w * 0.44f) / ((typeNames.size + 1) / 2)
                        val col = (offset.x / btnW).toInt()
                        val row = if (offset.y > h * 0.66f) 1 else 0
                        val idx = row * ((typeNames.size + 1) / 2) + col
                        if (idx in typeNames.indices) {
                            conn.setSlot(ch, typeNames[idx])
                            params = parseParams(conn.getChannelJson(ch))
                        }
                    }
                    if (offset.x < w * 0.18f && offset.y > h * 0.82f)
                        conn.clearSlot(ch)
                }
            }
    ) {
        val w = size.width
        val h = size.height
        val midX = w * 0.50f

        scanlines(0.04f, 4f)

        // ═══ LEFT HALF ═══

        // Header — big
        bitmapText(10f, 8f, "CH ${ch + 1}", color, 8f)
        bitmapText(w * 0.17f, 14f, typeName, Mw.text, 5f)

        // Peak meter
        val pkL = peaks[ch * 2]; val pkR = peaks[ch * 2 + 1]
        val meterX = w * 0.38f; val meterW = w * 0.08f
        drawRect(Mw.border.copy(alpha = 0.3f), Offset(meterX, 10f), Size(meterW, 14f))
        drawRect(color, Offset(meterX, 10f), Size(meterW * pkL.coerceIn(0f, 1f), 6f))
        drawRect(color, Offset(meterX, 18f), Size(meterW * pkR.coerceIn(0f, 1f), 6f))

        // ── Volume — chunky fader ──
        val volY = h * 0.16f
        bitmapText(10f, volY - 26f, "VOL", Mw.dim, 4f)
        bitmapText(w * 0.30f, volY - 26f, "${(volume * 100).toInt()}%", Mw.text, 4f)
        val trackW = w * 0.44f
        val trackH = 18f
        val isDragVol = dragParam == "__volume__"

        drawRect(Mw.border.copy(alpha = 0.25f), Offset(10f, volY), Size(trackW, trackH))
        drawRect(color.copy(alpha = if (isDragVol) 0.9f else 0.6f), Offset(10f, volY), Size(trackW * volume, trackH))
        // Chunky knob
        val knobX = 10f + trackW * volume
        val knobW = 14f
        val knobH = trackH + 12f
        drawRect(color, Offset(knobX - knobW / 2f, volY - 6f), Size(knobW, knobH))
        drawRect(Mw.text.copy(alpha = 0.4f), Offset(knobX - 1f, volY - 3f), Size(2f, knobH - 6f))

        // ── Mute / Solo — bigger ──
        val btnY = h * 0.35f
        val btnH = h * 0.11f
        val btnW = w * 0.14f

        drawRect(if (mute) Mw.warn.copy(alpha = 0.3f) else Mw.panel, Offset(10f, btnY), Size(btnW, btnH))
        drawRect(if (mute) Mw.warn else Mw.border.copy(alpha = 0.4f), Offset(10f, btnY), Size(btnW, btnH), style = Stroke(if (mute) 2f else 1f))
        bitmapText(18f, btnY + btnH * 0.25f, "MUTE", if (mute) Mw.warn else Mw.dim, 4f)

        drawRect(if (solo) Mw.good.copy(alpha = 0.3f) else Mw.panel, Offset(w * 0.18f, btnY), Size(btnW, btnH))
        drawRect(if (solo) Mw.good else Mw.border.copy(alpha = 0.4f), Offset(w * 0.18f, btnY), Size(btnW, btnH), style = Stroke(if (solo) 2f else 1f))
        bitmapText(w * 0.19f, btnY + btnH * 0.25f, "SOLO", if (solo) Mw.good else Mw.dim, 4f)

        // Divider
        glowLine(Offset(10f, h * 0.52f), Offset(midX - 10f, h * 0.52f), color.copy(alpha = 0.15f), 1f, 4f)

        // ── Instruments — bigger grid ──
        bitmapText(10f, h * 0.54f, "INSTRUMENT", Mw.dim, 3f)
        val instY = h * 0.60f
        val instCols = (typeNames.size + 1) / 2
        val instBtnW = (w * 0.44f) / instCols
        val instBtnH = h * 0.09f

        for ((idx, name) in typeNames.withIndex()) {
            val col = idx % instCols
            val row = idx / instCols
            val ix = col * instBtnW + 4f
            val iy = instY + row * (instBtnH + 6f)
            val selected = name == typeName

            drawRect(if (selected) color.copy(alpha = 0.2f) else Mw.panel, Offset(ix, iy), Size(instBtnW - 8f, instBtnH))
            drawRect(if (selected) color else Mw.border.copy(alpha = 0.3f), Offset(ix, iy), Size(instBtnW - 8f, instBtnH), style = Stroke(if (selected) 2f else 1f))
            bitmapText(ix + 6f, iy + instBtnH * 0.25f, name.take(6), if (selected) color else Mw.dim, 3f)
        }

        // Clear
        bitmapText(10f, h * 0.86f, "CLEAR", Mw.accent.copy(alpha = 0.5f), 4f)

        // ═══ VERTICAL DIVIDER ═══
        glowLine(Offset(midX, 8f), Offset(midX, h - 8f), Mw.border.copy(alpha = 0.2f), 1f, 4f)

        // ═══ RIGHT HALF: TWEAK ═══
        if (params.isEmpty()) {
            bitmapText(midX + 20f, h * 0.4f, "NO PARAMS", Mw.dim.copy(alpha = 0.3f), 5f)
            bitmapText(midX + 20f, h * 0.5f, "SELECT AN", Mw.dim.copy(alpha = 0.2f), 4f)
            bitmapText(midX + 20f, h * 0.58f, "INSTRUMENT", Mw.dim.copy(alpha = 0.2f), 4f)
        } else {
            bitmapText(midX + 10f, 8f, "TWEAK", Mw.accent, 5f)

            val paramH = minOf((h - 60f) / params.size, h * 0.12f)
            val sliderX = midX + 10f
            val sliderW = w * 0.44f
            val sliderH = 16f

            for ((idx, p) in params.withIndex()) {
                val py = 56f + idx * paramH
                val norm = ((p.value - p.min) / (p.max - p.min)).coerceIn(0f, 1f)
                val isDragging = dragParam == p.name

                // Label — bigger
                bitmapText(sliderX, py, p.name.uppercase().take(10), if (isDragging) color else Mw.dim, 3f)
                bitmapText(sliderX + sliderW - 80f, py, String.format("%.2f", p.value), Mw.text.copy(alpha = 0.7f), 3f)

                // Chunky track
                val trackY = py + 24f
                drawRect(Mw.border.copy(alpha = 0.2f), Offset(sliderX, trackY), Size(sliderW, sliderH))
                drawRect(
                    color.copy(alpha = if (isDragging) 0.9f else 0.5f),
                    Offset(sliderX, trackY), Size(sliderW * norm, sliderH)
                )

                // Chunky knob on param
                val pkX = sliderX + sliderW * norm
                val pkW = 12f
                drawRect(
                    if (isDragging) color else color.copy(alpha = 0.7f),
                    Offset(pkX - pkW / 2f, trackY - 4f), Size(pkW, sliderH + 8f)
                )
                drawRect(Mw.text.copy(alpha = 0.3f), Offset(pkX - 1f, trackY - 1f), Size(2f, sliderH + 2f))
            }
        }
    }
}

private fun parseParams(json: String): List<Param> {
    try {
        val obj = JSONObject(json)
        val params = obj.optJSONObject("params") ?: return emptyList()
        val type = obj.optString("instrument_type", "")
        val list = mutableListOf<Param>()
        val keys = params.keys()
        while (keys.hasNext()) {
            val key = keys.next()
            val value = params.optDouble(key, 0.0).toFloat()
            val (min, max) = paramRange(key, type)
            list.add(Param(key, value, min, max))
        }
        return list
    } catch (_: Exception) {
        return emptyList()
    }
}

private fun paramRange(key: String, type: String): Pair<Float, Float> = when (type) {
    "ym2413" -> when {
        key == "feedback" -> 0f to 7f
        key.endsWith("_mult") -> 0f to 15f
        key.endsWith("_tl") -> 0f to 63f
        key.endsWith("_ksl") -> 0f to 3f
        key.endsWith("_am") || key.endsWith("_vibrato") || key.endsWith("_eg") ||
            key.endsWith("_ksr") || key.endsWith("_wave") -> 0f to 1f
        key.endsWith("_attack") || key.endsWith("_decay") ||
            key.endsWith("_sustain") || key.endsWith("_release") -> 0f to 15f
        else -> 0f to 1f
    }
    "phase-dist" -> when {
        key == "mode" -> 0f to 5f
        key == "attack" || key == "decay" || key == "release" -> 0.001f to 2f
        else -> 0f to 1f
    }
    "fm-drums" -> when {
        key.contains("freq") -> 20f to 20000f
        key == "mod_index" -> 0f to 20f
        key == "pitch_sweep" -> -2000f to 2000f
        key == "pitch_decay" -> 0.001f to 1f
        key == "decay" -> 0.001f to 2f
        else -> 0f to 1f
    }
    else -> when {
        key.contains("ratio") -> 0.1f to 16f
        key.contains("index") -> 0f to 20f
        key.contains("attack") || key.contains("decay") || key.contains("release") -> 0.001f to 2f
        key.contains("sustain") -> 0f to 1f
        key.contains("feedback") -> 0f to 1f
        key.contains("detune") -> -100f to 100f
        key.contains("cutoff") || key.contains("freq") -> 20f to 20000f
        key.contains("resonance") || key.contains("q") -> 0f to 1f
        else -> 0f to 1f
    }
}
