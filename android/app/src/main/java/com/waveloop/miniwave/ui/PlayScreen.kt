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
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import com.waveloop.miniwave.MiniwaveConnection
import com.waveloop.miniwave.ui.Retro.bitmapText
import com.waveloop.miniwave.ui.Retro.diamond
import com.waveloop.miniwave.ui.Retro.glowLine
import com.waveloop.miniwave.ui.Retro.scanlines
import kotlin.math.abs
import kotlin.math.roundToInt

const val NUM_STEPS = 16
const val NUM_ROWS = 8

/** Note names for display at a given octave */
private val DEGREE_NAMES = arrayOf("C", "D", "E", "F", "G", "A", "B", "C")

/**
 * PLAY — step sequencer grid.
 * Transport state (grid, playing, currentStep, octave) is hoisted to MiniwaveApp
 * so playback continues when navigating to other pages.
 */
@Composable
fun PlayScreen(
    conn: MiniwaveConnection,
    focusedCh: Int,
    slotInfo: IntArray,
    bpm: Float,
    grid: Array<IntArray>,
    playing: Boolean,
    currentStep: Int,
    octave: Int,
    onTogglePlay: () -> Unit,
    onClear: () -> Unit,
    onToggleCell: (step: Int, row: Int) -> Unit,
    onSetVelocity: (step: Int, row: Int, vel: Int) -> Unit,
    onOctaveChange: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    val chColor = Mw.chColors[focusedCh % Mw.chColors.size]

    Canvas(
        modifier = modifier
            .fillMaxSize()
            .background(Mw.bg)
            .pointerInput(focusedCh, octave) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    val pos = down.position
                    val w = size.width.toFloat()
                    val h = size.height.toFloat()

                    val gridL = w * 0.08f
                    val gridT = h * 0.14f
                    val gridW = w * 0.90f
                    val gridH = h * 0.76f
                    val cellW = gridW / NUM_STEPS
                    val cellH = gridH / NUM_ROWS

                    val inGrid = pos.x >= gridL && pos.y >= gridT &&
                                 pos.x < gridL + gridW && pos.y < gridT + gridH

                    if (inGrid) {
                        down.consume()
                        val step = ((pos.x - gridL) / cellW).toInt().coerceIn(0, NUM_STEPS - 1)
                        val row = (NUM_ROWS - 1) - ((pos.y - gridT) / cellH).toInt().coerceIn(0, NUM_ROWS - 1)
                        onToggleCell(step, row)

                        // Drag up/down to set velocity if note is now on
                        if (grid[step][row] > 0) {
                            val startY = pos.y
                            while (true) {
                                val event = awaitPointerEvent()
                                val change = event.changes.firstOrNull { it.id == down.id } ?: break
                                if (!change.pressed) break
                                change.consume()
                                val dy = startY - change.position.y
                                val newVel = (100 + (dy / cellH * 64).toInt()).coerceIn(1, 127)
                                onSetVelocity(step, row, newVel)
                            }
                        }
                    } else if (pos.x < gridL && pos.y >= gridT && pos.y < gridT + gridH) {
                        // Note label area — audition: note on while held
                        down.consume()
                        val row = (NUM_ROWS - 1) - ((pos.y - gridT) / cellH).toInt().coerceIn(0, NUM_ROWS - 1)
                        val notes = gridRowToNotes(row, octave,
                            0, intArrayOf(0, 2, 4, 5, 7, 9, 11), null)
                        for (n in notes) conn.noteOn(focusedCh, n, 100)
                        // Hold until release
                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) break
                            change.consume()
                        }
                        for (n in notes) conn.noteOff(focusedCh, n)
                    } else {
                        if (pos.x > w * 0.82f && pos.y < h * 0.10f) {
                            if (!playing && slotInfo[focusedCh * 4] == 0) {
                                conn.setSlot(focusedCh, "fm-synth")
                            }
                            onTogglePlay()
                        } else if (pos.x in (w * 0.65f)..(w * 0.80f) && pos.y < h * 0.10f) {
                            onClear()
                        }
                    }
                }
            }
            // Two-finger vertical scroll → octave shift
            .pointerInput(octave) {
                awaitEachGesture {
                    awaitFirstDown(pass = PointerEventPass.Initial)
                    var totalY = 0f
                    var fired = false
                    while (true) {
                        val event = awaitPointerEvent(PointerEventPass.Initial)
                        val pointers = event.changes.filter { it.pressed }
                        if (pointers.size >= 2 && !fired) {
                            val dy = pointers.sumOf { (it.position.y - it.previousPosition.y).toDouble() }.toFloat() / pointers.size
                            totalY += dy
                            if (abs(totalY) > 60f) {
                                fired = true
                                val newOctave = if (totalY > 0)
                                    (octave + 1).coerceAtMost(7)
                                else
                                    (octave - 1).coerceAtLeast(0)
                                onOctaveChange(newOctave)
                            }
                        }
                        if (pointers.isEmpty()) break
                    }
                }
            }
    ) {
        val w = size.width
        val h = size.height

        scanlines(0.04f, 4f)

        // Header
        bitmapText(10f, 10f, "PLAY", Mw.accent, 6f)
        bitmapText(w * 0.14f, 14f, "CH ${focusedCh + 1}", chColor, 4f)
        bitmapText(w * 0.28f, 14f, "${bpm.toInt()} BPM", Mw.dim, 3f)
        bitmapText(w * 0.44f, 14f, "OCT ${octave}", Mw.dim, 3f)

        // Play/Stop button
        val playBtnX = w * 0.83f
        val btnColor = if (playing) Mw.good else Mw.accent
        drawRect(btnColor.copy(alpha = 0.2f), Offset(playBtnX, 6f), Size(w * 0.14f, h * 0.07f))
        drawRect(btnColor, Offset(playBtnX, 6f), Size(w * 0.14f, h * 0.07f), style = Stroke(2f))
        bitmapText(playBtnX + 10f, 14f, if (playing) "STOP" else "PLAY", btnColor, 4f)

        // Clear button
        bitmapText(w * 0.67f, 14f, "CLEAR", Mw.dim, 3f)

        // Grid
        val gridL = w * 0.08f
        val gridT = h * 0.14f
        val gridW = w * 0.90f
        val gridH = h * 0.76f
        val cellW = gridW / NUM_STEPS
        val cellH = gridH / NUM_ROWS

        // Zebra lanes + row labels (full row height)
        for (row in 0 until NUM_ROWS) {
            val y = gridT + (NUM_ROWS - 1 - row) * cellH
            val label = "${DEGREE_NAMES[row]}${octave + row / 7}"

            // Zebra stripe on even rows
            if (row % 2 == 0) {
                drawRect(Mw.border.copy(alpha = 0.06f), Offset(0f, y), Size(w, cellH))
            }

            // Note name — fills row height
            val labelPx = (cellH / 10f).coerceIn(3f, 6f)
            bitmapText(2f, y + (cellH - labelPx * 7f) / 2f, label, Mw.dim, labelPx)
        }

        // Grid cells
        for (step in 0 until NUM_STEPS) {
            for (row in 0 until NUM_ROWS) {
                val cx = gridL + step * cellW
                val cy = gridT + (NUM_ROWS - 1 - row) * cellH
                val vel = grid[step][row]
                val isOn = vel > 0
                val velNorm = vel / 127f
                val isCurrentStep = step == currentStep && playing

                val bgAlpha = when {
                    isOn && isCurrentStep -> 0.3f + velNorm * 0.4f
                    isOn -> 0.1f + velNorm * 0.25f
                    isCurrentStep -> 0.06f
                    step % 4 == 0 -> 0.02f
                    else -> 0f
                }
                if (bgAlpha > 0f) {
                    drawRect(
                        if (isOn) chColor.copy(alpha = bgAlpha) else Mw.border.copy(alpha = bgAlpha),
                        Offset(cx + 1f, cy + 1f), Size(cellW - 2f, cellH - 2f)
                    )
                }
                drawRect(
                    if (isOn) chColor.copy(alpha = 0.3f + velNorm * 0.4f) else Mw.border.copy(alpha = 0.08f),
                    Offset(cx + 1f, cy + 1f), Size(cellW - 2f, cellH - 2f),
                    style = Stroke(1f)
                )
                if (isOn) {
                    diamond(
                        Offset(cx + cellW / 2f, cy + cellH / 2f),
                        minOf(cellW, cellH) * (0.15f + velNorm * 0.15f),
                        if (isCurrentStep) chColor else chColor.copy(alpha = 0.4f + velNorm * 0.4f)
                    )
                }
            }
        }

        // Playhead
        if (playing) {
            val px = gridL + currentStep * cellW + cellW / 2f
            glowLine(Offset(px, gridT), Offset(px, gridT + gridH), chColor.copy(alpha = 0.4f), 2f, 8f)
        }

        // Beat markers
        val markerY = gridT + gridH + 4f
        for (step in 0 until NUM_STEPS) {
            val mx = gridL + step * cellW + cellW / 2f
            if (step % 4 == 0) {
                bitmapText(mx - 4f, markerY, "${step / 4 + 1}", Mw.dim.copy(alpha = 0.3f), 2f)
            } else {
                drawRect(Mw.dim.copy(alpha = 0.15f), Offset(mx, markerY + 2f), Size(2f, 4f))
            }
        }
    }
}

/** Map grid row to MIDI note(s) — scale-aware, with optional chord voicing.
 *  Rows map to scale degrees; chords are snapped diatonically. */
fun gridRowToNotes(
    row: Int, octave: Int,
    root: Int = 0,
    scaleDegrees: IntArray = intArrayOf(0, 2, 4, 5, 7, 9, 11),
    chordIntervals: IntArray? = null
): List<Int> {
    if (scaleDegrees.isEmpty()) return listOf(octave * 12 + root + row * 2)

    val degreeIdx = row % scaleDegrees.size
    val octaveBoost = row / scaleDegrees.size
    val baseNote = (octave + octaveBoost) * 12 + root + scaleDegrees[degreeIdx]

    if (chordIntervals == null || chordIntervals.size <= 1) {
        return listOf(baseNote)
    }

    // Build diatonic chord: snap each interval to nearest scale tone
    return chordIntervals.map { interval ->
        if (interval == 0) baseNote
        else snapToScale(baseNote + interval, root, scaleDegrees)
    }
}

/** Snap a MIDI note to the nearest tone in the given scale */
private fun snapToScale(note: Int, root: Int, degrees: IntArray): Int {
    val baseOct = (note - root).floorDiv(12)
    var closest = note
    var minDist = Int.MAX_VALUE
    for (octOffset in -1..1) {
        for (d in degrees) {
            val candidate = root + (baseOct + octOffset) * 12 + d
            val dist = abs(candidate - note)
            if (dist < minDist) { minDist = dist; closest = candidate }
        }
    }
    return closest
}
