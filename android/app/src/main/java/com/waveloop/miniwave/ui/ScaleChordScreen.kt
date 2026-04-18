package com.waveloop.miniwave.ui

import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import androidx.compose.ui.graphics.drawscope.DrawScope
import com.waveloop.miniwave.MiniwaveConnection
import com.waveloop.miniwave.ui.Retro.bitmapText
import com.waveloop.miniwave.ui.Retro.chevronDivider
import com.waveloop.miniwave.ui.Retro.diamond
import com.waveloop.miniwave.ui.Retro.glowLine
import com.waveloop.miniwave.ui.Retro.perspectiveGrid
import com.waveloop.miniwave.ui.Retro.pianoKeys
import com.waveloop.miniwave.ui.Retro.scanlines
import kotlin.math.*

/* ══════════════════════════════════════════════════════════════════════
 *  Scale & Chord Data
 * ══════════════════════════════════════════════════════════════════════ */

private val NOTE_NAMES = arrayOf("C","C#","D","D#","E","F","F#","G","G#","A","A#","B")

data class ScaleEntry(val name: String, val degrees: IntArray) {
    override fun equals(other: Any?) = other is ScaleEntry && name == other.name
    override fun hashCode() = name.hashCode()
}

private val SCALES = listOf(
    ScaleEntry("QUARTERTONE",   intArrayOf(0,1,2,3,4,5,6,7,8,9,10,11)),
    ScaleEntry("JUST MAJ",      intArrayOf(0,2,4,5,7,9,11)),
    ScaleEntry("LYDIAN",        intArrayOf(0,2,4,6,7,9,11)),
    ScaleEntry("BEBOP MAJ",     intArrayOf(0,2,4,5,7,8,9,11)),
    ScaleEntry("MIXOLYDIAN",    intArrayOf(0,2,4,5,7,9,10)),
    ScaleEntry("LYDIAN DOM",    intArrayOf(0,2,4,6,7,9,10)),
    ScaleEntry("MAJ PENTA",     intArrayOf(0,2,4,7,9)),
    ScaleEntry("IONIAN",        intArrayOf(0,2,4,5,7,9,11)),
    ScaleEntry("MAJOR",         intArrayOf(0,2,4,5,7,9,11)),
    ScaleEntry("MINOR",         intArrayOf(0,2,3,5,7,8,10)),
    ScaleEntry("AEOLIAN",       intArrayOf(0,2,3,5,7,8,10)),
    ScaleEntry("MIN PENTA",     intArrayOf(0,3,5,7,10)),
    ScaleEntry("DORIAN",        intArrayOf(0,2,3,5,7,9,10)),
    ScaleEntry("HARMONIC MIN",  intArrayOf(0,2,3,5,7,8,11)),
    ScaleEntry("PHRYGIAN",      intArrayOf(0,1,3,5,7,8,10)),
    ScaleEntry("PHRYGIAN DOM",  intArrayOf(0,1,4,5,7,8,10)),
    ScaleEntry("LOCRIAN",       intArrayOf(0,1,3,5,6,8,10)),
    ScaleEntry("DIMINISHED",    intArrayOf(0,2,3,5,6,8,9,11)),
    ScaleEntry("WHOLE-HALF",    intArrayOf(0,2,3,5,6,8,9,11)),
    ScaleEntry("CHROMATIC",     intArrayOf(0,1,2,3,4,5,6,7,8,9,10,11)),
)
private val CENTER_Y = 8

data class VoicingEntry(val name: String, val intervals: IntArray) {
    override fun equals(other: Any?) = other is VoicingEntry && name == other.name
    override fun hashCode() = name.hashCode()
}

private val VOICINGS = listOf(
    VoicingEntry("ROOT",      intArrayOf(0)),
    VoicingEntry("POWER",     intArrayOf(0,7)),
    VoicingEntry("SHELL 3",   intArrayOf(0,4)),
    VoicingEntry("SHELL 7",   intArrayOf(0,10)),
    VoicingEntry("DYAD 5",    intArrayOf(0,7)),
    VoicingEntry("TRIAD",     intArrayOf(0,4,7)),
    VoicingEntry("MIN TRIAD", intArrayOf(0,3,7)),
    VoicingEntry("7TH",       intArrayOf(0,4,7,11)),
    VoicingEntry("MIN 7",     intArrayOf(0,3,7,10)),
    VoicingEntry("9TH",       intArrayOf(0,4,7,11,14)),
    VoicingEntry("ADD9",      intArrayOf(0,4,7,14)),
    VoicingEntry("11TH",      intArrayOf(0,4,7,11,14,17)),
    VoicingEntry("13TH",      intArrayOf(0,4,7,11,14,17,21)),
    VoicingEntry("WIDE 7",    intArrayOf(0,7,11,16)),
    VoicingEntry("WIDE 9",    intArrayOf(0,7,14,19)),
)
private val VOICING_CENTER = 5

/* ══════════════════════════════════════════════════════════════════════ */

enum class ScaleLevel { PICK_SCALE, PICK_VOICING }

@Composable
fun ScaleChordScreen(
    conn: MiniwaveConnection,
    focusedCh: Int,
    currentRoot: Int = 0,
    currentScaleDegrees: IntArray = intArrayOf(0, 2, 4, 5, 7, 9, 11),
    currentChordIntervals: IntArray? = null,
    onScaleChordChanged: (root: Int, degrees: IntArray, intervals: IntArray?) -> Unit = { _, _, _ -> },
    modifier: Modifier = Modifier
) {
    // Derive initial positions from persisted state
    val initScaleIdx = SCALES.indexOfFirst { it.degrees.contentEquals(currentScaleDegrees) }
        .takeIf { it >= 0 } ?: CENTER_Y
    val initVoicingIdx = if (currentChordIntervals != null)
        VOICINGS.indexOfFirst { it.intervals.contentEquals(currentChordIntervals) }
            .takeIf { it >= 0 } ?: VOICING_CENTER
    else VOICING_CENTER
    val hasChord = currentChordIntervals != null

    var level by remember(focusedCh) { mutableStateOf(if (hasChord) ScaleLevel.PICK_VOICING else ScaleLevel.PICK_SCALE) }

    // Float positions — initialized from persisted state, keyed on focusedCh
    var rootPos by remember(focusedCh) { mutableFloatStateOf(currentRoot.toFloat()) }
    var scalePos by remember(focusedCh) { mutableFloatStateOf(initScaleIdx.toFloat()) }
    var modePos by remember(focusedCh) { mutableFloatStateOf(0f) }
    var voicingPos by remember(focusedCh) { mutableFloatStateOf(initVoicingIdx.toFloat()) }
    var dragging by remember { mutableStateOf(false) }

    // Snap animation targets (only used on release)
    val rootSnap by animateFloatAsState(
        if (dragging) rootPos else rootPos.nearest().toFloat(),
        tween(180, easing = FastOutSlowInEasing)
    )
    val scaleSnap by animateFloatAsState(
        if (dragging) scalePos else scalePos.nearest().coerceIn(0, SCALES.size - 1).toFloat(),
        tween(180, easing = FastOutSlowInEasing)
    )
    val modeSnap by animateFloatAsState(
        if (dragging) modePos else modePos.nearest().toFloat(),
        tween(180, easing = FastOutSlowInEasing)
    )
    val voicingSnap by animateFloatAsState(
        if (dragging) voicingPos else voicingPos.nearest().coerceIn(0, VOICINGS.size - 1).toFloat(),
        tween(180, easing = FastOutSlowInEasing)
    )

    // Snapped integer indices (for data lookup)
    val rootIdx = ((rootSnap.nearest()) % 12 + 12) % 12
    val scaleIdx = scaleSnap.nearest().coerceIn(0, SCALES.size - 1)
    val scale = SCALES[scaleIdx]
    val modeIdx = if (scale.degrees.isNotEmpty()) ((modeSnap.nearest()) % scale.degrees.size + scale.degrees.size) % scale.degrees.size else 0
    val voicingIdx = voicingSnap.nearest().coerceIn(0, VOICINGS.size - 1)

    val chColor = Mw.chColors[focusedCh % Mw.chColors.size]

    // Use the live float for rendering (follows finger), snapped int for data
    val renderRoot = if (dragging) rootPos else rootSnap
    val renderScale = if (dragging) scalePos else scaleSnap
    val renderMode = if (dragging) modePos else modeSnap
    val renderVoicing = if (dragging) voicingPos else voicingSnap

    Canvas(
        modifier = modifier
            .fillMaxSize()
            .background(Mw.bg)
            .pointerInput(level) {
                detectDragGestures(
                    onDragStart = { dragging = true },
                    onDragEnd = {
                        dragging = false
                        // Snap positions to nearest integer
                        rootPos = ((rootPos.nearest()) % 12 + 12 ).toFloat() % 12f
                        scalePos = scalePos.nearest().coerceIn(0, SCALES.size - 1).toFloat()
                        modePos = modePos.nearest().toFloat()
                        voicingPos = voicingPos.nearest().coerceIn(0, VOICINGS.size - 1).toFloat()

                        if (level == ScaleLevel.PICK_VOICING) {
                            applyScaleChord(conn, focusedCh, rootIdx, SCALES[scaleIdx], modeIdx, VOICINGS[voicingIdx], onScaleChordChanged)
                        }
                    },
                    onDragCancel = { dragging = false }
                ) { _, dragAmount ->
                    val cellW = size.width / 5f
                    val cellH = size.height / 2.5f  // more sensitive vertically

                    when (level) {
                        ScaleLevel.PICK_SCALE -> {
                            rootPos -= dragAmount.x / cellW
                            scalePos -= dragAmount.y / cellH  // inverted: drag up = darker, drag down = brighter
                            scalePos = scalePos.coerceIn(-0.5f, SCALES.size - 0.5f)
                        }
                        ScaleLevel.PICK_VOICING -> {
                            modePos -= dragAmount.x / cellW
                            voicingPos -= dragAmount.y / cellH  // inverted: drag up = wider
                            voicingPos = voicingPos.coerceIn(-0.5f, VOICINGS.size - 0.5f)
                        }
                    }
                }
            }
            .pointerInput(level) {
                detectTapGestures { offset ->
                    when (level) {
                        ScaleLevel.PICK_SCALE -> {
                            conn.setScale(focusedCh, rootIdx, scale.degrees)
                            onScaleChordChanged(rootIdx, scale.degrees, null)
                            level = ScaleLevel.PICK_VOICING
                            modePos = 0f
                            voicingPos = VOICING_CENTER.toFloat()
                        }
                        ScaleLevel.PICK_VOICING -> {
                            if (offset.x < size.width * 0.15f && offset.y < size.height * 0.12f) {
                                level = ScaleLevel.PICK_SCALE
                            } else {
                                applyScaleChord(conn, focusedCh, rootIdx, SCALES[scaleIdx], modeIdx, VOICINGS[voicingIdx], onScaleChordChanged)
                            }
                        }
                    }
                }
            }
    ) {
        val w = size.width
        val h = size.height
        val px = 4f  // base pixel scale — chunky

        perspectiveGrid(w * 0.5f, h * 0.1f, Mw.border.copy(alpha = 0.08f))
        scanlines(0.04f, 4f)

        when (level) {
            ScaleLevel.PICK_SCALE -> drawScalePicker(w, h, px, renderRoot, renderScale, scaleIdx, rootIdx, chColor)
            ScaleLevel.PICK_VOICING -> drawVoicingPicker(w, h, px, rootIdx, scaleIdx, renderMode, renderVoicing, modeIdx, voicingIdx, chColor)
        }
    }
}

private fun DrawScope.drawScalePicker(
    w: Float, h: Float, px: Float,
    rootFloat: Float, scaleFloat: Float,
    scaleIdx: Int, rootIdx: Int,
    color: Color
) {
    val midY = h * 0.45f

    // ── Top lane: root note (horizontal, follows finger) ──
    chevronDivider(h * 0.08f, color, 12f)

    val noteSpacing = w / 4f
    val rootFrac = rootFloat - rootFloat.toInt()
    for (offset in -3..3) {
        val noteIdx = ((rootFloat.toInt() + offset) % 12 + 120) % 12
        val x = w * 0.5f - rootFrac * noteSpacing + offset * noteSpacing
        val distFromCenter = abs(x - w * 0.5f) / (w * 0.5f)
        val isCurrent = abs(offset.toFloat() - rootFrac) < 0.5f
        val alpha = max(0f, 1f - distFromCenter * 0.7f)
        val textPx = if (isCurrent) 8f else 4f

        bitmapText(
            x - NOTE_NAMES[noteIdx].length * textPx * 3.5f,
            h * 0.06f,
            NOTE_NAMES[noteIdx],
            if (isCurrent) color else Mw.dim.copy(alpha = alpha),
            textPx
        )
    }

    // ── Chevron ──
    chevronDivider(midY - h * 0.18f, color.copy(alpha = 0.3f), 16f)

    // ── Bottom lane: scale (vertical, follows finger) ──
    val scaleSpacing = h * 0.13f
    val scaleFrac = scaleFloat - scaleFloat.toInt()

    for (offset in -4..4) {
        val idx = scaleFloat.toInt() + offset
        if (idx < 0 || idx >= SCALES.size) continue

        val y = midY - scaleFrac * scaleSpacing + offset * scaleSpacing
        val distFromCenter = abs(y - midY) / (h * 0.4f)
        val isCurrent = abs(offset.toFloat() - scaleFrac) < 0.5f
        val alpha = max(0f, 1f - distFromCenter * 0.6f)
        val entry = SCALES[idx]
        val textPx = if (isCurrent) 6f else px

        // Name — big and bold
        bitmapText(w * 0.03f, y - textPx * 3.5f, entry.name,
            if (isCurrent) color else Mw.dim.copy(alpha = alpha), textPx)

        // Piano keys — wider, taller
        pianoKeys(w * 0.50f, y - 16f, w * 0.44f,
            if (isCurrent) 32f else 20f, entry.degrees,
            if (isCurrent) color else Mw.dim.copy(alpha = alpha), Mw.border)

        // Active key dots
        if (isCurrent) {
            val keyW = w * 0.44f / 12f
            for (d in entry.degrees) {
                drawRect(color.copy(alpha = 0.7f),
                    Offset(w * 0.50f + d * keyW + keyW * 0.15f, y + 18f),
                    Size(keyW * 0.7f, 4f))
            }
        }
    }

    glowLine(Offset(0f, midY), Offset(w, midY), color.copy(alpha = 0.2f), 2f, 8f)
    bitmapText(w * 0.25f, h - px * 12f, "DRAG  ROOT  SCALE    TAP TO ENTER", Mw.dim.copy(alpha = 0.35f), px)
    bitmapText(w * 0.90f, h * 0.08f, "BRIGHT", Mw.good.copy(alpha = 0.35f), 3f)
    bitmapText(w * 0.90f, h * 0.90f, "DARK", Mw.accent.copy(alpha = 0.35f), 3f)
}

private fun DrawScope.drawVoicingPicker(
    w: Float, h: Float, px: Float,
    rootIdx: Int, scaleIdx: Int,
    modeFloat: Float, voicingFloat: Float,
    modeIdx: Int, voicingIdx: Int,
    color: Color
) {
    val scale = SCALES[scaleIdx]
    val voicing = VOICINGS[voicingIdx]
    val modeRoot = (rootIdx + scale.degrees.getOrElse(modeIdx) { 0 }) % 12

    bitmapText(10f, 10f, "< BACK", Mw.accent, 5f)
    chevronDivider(h * 0.08f, color, 14f)

    // ── Top lane: mode (horizontal, follows finger) ──
    val modeSpacing = w / (scale.degrees.size.coerceAtLeast(1) + 1.5f)

    for (i in scale.degrees.indices) {
        val t = i.toFloat() - modeFloat
        val x = w * 0.5f + t * modeSpacing
        val isCurrent = i == modeIdx
        val noteIdx = (rootIdx + scale.degrees[i]) % 12
        val textPx = if (isCurrent) 8f else 4f
        val alpha = max(0f, 1f - abs(t) * 0.22f)

        bitmapText(x - NOTE_NAMES[noteIdx].length * textPx * 3.5f, h * 0.10f,
            NOTE_NAMES[noteIdx],
            if (isCurrent) color else Mw.dim.copy(alpha = alpha), textPx)

        bitmapText(x - 6f, h * 0.10f + textPx * 10f, "${i + 1}",
            if (isCurrent) color.copy(alpha = 0.5f) else Mw.dim.copy(alpha = 0.15f), 3f)

        if (isCurrent) diamond(Offset(x, h * 0.10f + textPx * 14f), 6f, color)
    }

    chevronDivider(h * 0.36f, color.copy(alpha = 0.3f), 18f)

    // ── Bottom lane: voicing (vertical, follows finger) ──
    val midY = h * 0.58f
    val voicingSpacing = h * 0.12f
    val voicingFrac = voicingFloat - voicingFloat.toInt()

    for (offset in -4..4) {
        val idx = voicingFloat.toInt() + offset
        if (idx < 0 || idx >= VOICINGS.size) continue

        val y = midY - voicingFrac * voicingSpacing + offset * voicingSpacing
        val distFromCenter = abs(y - midY) / (h * 0.35f)
        val isCurrent = abs(offset.toFloat() - voicingFrac) < 0.5f
        val alpha = max(0f, 1f - distFromCenter * 0.5f)
        val v = VOICINGS[idx]
        val textPx = if (isCurrent) 6f else px

        bitmapText(w * 0.03f, y - textPx * 3.5f, v.name,
            if (isCurrent) color else Mw.dim.copy(alpha = alpha), textPx)

        val dotX = w * 0.50f
        val dotScale = if (isCurrent) 8f else 5f
        for (interval in v.intervals) {
            diamond(Offset(dotX + interval * dotScale * 1.5f, y),
                dotScale * 0.6f,
                if (isCurrent) color.copy(alpha = 0.9f) else Mw.dim.copy(alpha = alpha))
        }
    }

    glowLine(Offset(0f, midY), Offset(w, midY), color.copy(alpha = 0.2f), 2f, 8f)

    // Chord spelling — big
    val chordText = voicing.intervals.joinToString("  ") { NOTE_NAMES[(modeRoot + it) % 12] }
    bitmapText(w * 0.25f, h * 0.86f, chordText, color, 6f)

    // Piano — full width
    val voicedDegrees = voicing.intervals.map { (modeRoot + it) % 12 }.toIntArray()
    pianoKeys(w * 0.08f, h * 0.78f, w * 0.84f, 28f, voicedDegrees, color, Mw.border)

    bitmapText(w * 0.25f, h - px * 12f, "DRAG  MODE  VOICING    TAP TO APPLY", Mw.dim.copy(alpha = 0.3f), px)
    bitmapText(w * 0.90f, h * 0.38f, "WIDE", Mw.good.copy(alpha = 0.35f), 3f)
    bitmapText(w * 0.90f, h * 0.76f, "SPARSE", Mw.accent.copy(alpha = 0.35f), 3f)
}

private fun applyScaleChord(
    conn: MiniwaveConnection, ch: Int,
    root: Int, scale: ScaleEntry, mode: Int, voicing: VoicingEntry,
    onChanged: (Int, IntArray, IntArray?) -> Unit = { _, _, _ -> }
) {
    val degrees = scale.degrees
    if (degrees.isEmpty()) return
    val rotated = IntArray(degrees.size) { i ->
        val idx = (i + mode) % degrees.size
        ((degrees[idx] - degrees[mode]) % 12 + 12) % 12
    }
    rotated.sort()
    val modeRoot = (root + degrees[mode]) % 12
    conn.setScale(ch, modeRoot, rotated)
    conn.setChord(ch, voicing.intervals)
    onChanged(modeRoot, rotated, voicing.intervals)
}

private fun Float.nearest(): Int = kotlin.math.round(this).toInt()
