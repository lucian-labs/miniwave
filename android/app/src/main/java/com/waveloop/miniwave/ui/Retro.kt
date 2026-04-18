package com.waveloop.miniwave.ui

import android.graphics.Paint
import android.graphics.Typeface
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.nativeCanvas
import kotlin.math.*

/**
 * Retro vector rendering utilities — lo-res bitmap font, neon glow lines,
 * perspective grid, scanlines, geometric shapes. Matches miniwave viz aesthetic.
 */
object Retro {

    // 5x7 bitmap font from viz.html
    private val FONT = mapOf(
        'A' to intArrayOf(0x7C,0x12,0x11,0x12,0x7C),
        'B' to intArrayOf(0x7F,0x49,0x49,0x49,0x36),
        'C' to intArrayOf(0x3E,0x41,0x41,0x41,0x22),
        'D' to intArrayOf(0x7F,0x41,0x41,0x22,0x1C),
        'E' to intArrayOf(0x7F,0x49,0x49,0x49,0x41),
        'F' to intArrayOf(0x7F,0x09,0x09,0x09,0x01),
        'G' to intArrayOf(0x3E,0x41,0x49,0x49,0x3A),
        'H' to intArrayOf(0x7F,0x08,0x08,0x08,0x7F),
        'I' to intArrayOf(0x00,0x41,0x7F,0x41,0x00),
        'J' to intArrayOf(0x20,0x40,0x41,0x3F,0x01),
        'K' to intArrayOf(0x7F,0x08,0x14,0x22,0x41),
        'L' to intArrayOf(0x7F,0x40,0x40,0x40,0x40),
        'M' to intArrayOf(0x7F,0x02,0x0C,0x02,0x7F),
        'N' to intArrayOf(0x7F,0x04,0x08,0x10,0x7F),
        'O' to intArrayOf(0x3E,0x41,0x41,0x41,0x3E),
        'P' to intArrayOf(0x7F,0x09,0x09,0x09,0x06),
        'Q' to intArrayOf(0x3E,0x41,0x51,0x21,0x5E),
        'R' to intArrayOf(0x7F,0x09,0x19,0x29,0x46),
        'S' to intArrayOf(0x46,0x49,0x49,0x49,0x31),
        'T' to intArrayOf(0x01,0x01,0x7F,0x01,0x01),
        'U' to intArrayOf(0x3F,0x40,0x40,0x40,0x3F),
        'V' to intArrayOf(0x1F,0x20,0x40,0x20,0x1F),
        'W' to intArrayOf(0x3F,0x40,0x30,0x40,0x3F),
        'X' to intArrayOf(0x63,0x14,0x08,0x14,0x63),
        'Y' to intArrayOf(0x07,0x08,0x70,0x08,0x07),
        'Z' to intArrayOf(0x61,0x51,0x49,0x45,0x43),
        '0' to intArrayOf(0x3E,0x51,0x49,0x45,0x3E),
        '1' to intArrayOf(0x00,0x42,0x7F,0x40,0x00),
        '2' to intArrayOf(0x42,0x61,0x51,0x49,0x46),
        '3' to intArrayOf(0x22,0x41,0x49,0x49,0x36),
        '4' to intArrayOf(0x18,0x14,0x12,0x7F,0x10),
        '5' to intArrayOf(0x27,0x45,0x45,0x45,0x39),
        '6' to intArrayOf(0x3C,0x4A,0x49,0x49,0x30),
        '7' to intArrayOf(0x01,0x71,0x09,0x05,0x03),
        '8' to intArrayOf(0x36,0x49,0x49,0x49,0x36),
        '9' to intArrayOf(0x06,0x49,0x49,0x29,0x1E),
        ':' to intArrayOf(0x00,0x00,0x36,0x36,0x00),
        '-' to intArrayOf(0x08,0x08,0x08,0x08,0x08),
        '#' to intArrayOf(0x14,0x7F,0x14,0x7F,0x14),
        '.' to intArrayOf(0x00,0x60,0x60,0x00,0x00),
        '/' to intArrayOf(0x20,0x10,0x08,0x04,0x02),
        '>' to intArrayOf(0x41,0x22,0x14,0x08,0x00),
        '<' to intArrayOf(0x00,0x08,0x14,0x22,0x41),
        '!' to intArrayOf(0x00,0x00,0x5F,0x00,0x00),
        '%' to intArrayOf(0x23,0x13,0x08,0x64,0x62),
        ' ' to intArrayOf(0x00,0x00,0x00,0x00,0x00),
    )

    /** Draw bitmap text at pixel scale with double-thickness (bold).
     *  Each font pixel renders as a 2×2 block for chunky retro feel. */
    fun DrawScope.bitmapText(x: Float, y: Float, text: String, color: Color, scale: Float = 4f): Float {
        var cx = x
        val dot = scale  // pixel size
        for (ch in text.uppercase()) {
            val glyph = FONT[ch] ?: FONT[' ']!!
            for (col in glyph.indices) {
                for (row in 0 until 7) {
                    if (glyph[col] and (1 shl row) != 0) {
                        // 2×2 bold: draw pixel + right + down + diagonal
                        drawRect(color, Offset(cx + col * dot, y + row * dot), Size(dot + dot * 0.4f, dot + dot * 0.4f))
                    }
                }
            }
            cx += (glyph.size + 1) * dot
        }
        return cx - x
    }

    /** Draw a neon glow line (3-pass: wide dim, medium, thin bright) */
    fun DrawScope.glowLine(
        start: Offset, end: Offset, color: Color,
        width: Float = 2f, glowWidth: Float = 8f
    ) {
        drawLine(color.copy(alpha = 0.1f), start, end, strokeWidth = glowWidth, cap = StrokeCap.Round)
        drawLine(color.copy(alpha = 0.3f), start, end, strokeWidth = width * 2, cap = StrokeCap.Round)
        drawLine(color, start, end, strokeWidth = width, cap = StrokeCap.Round)
    }

    /** Draw perspective grid radiating from vanishing point */
    fun DrawScope.perspectiveGrid(
        vpX: Float, vpY: Float, color: Color = Mw.border.copy(alpha = 0.15f),
        rays: Int = 12
    ) {
        val w = size.width
        val h = size.height

        // Horizontal perspective lines
        for (i in 0..6) {
            val t = i / 6f
            val y = vpY + (h - vpY) * t * t // perspective foreshortening
            val spread = (y - vpY) / (h - vpY) * w * 0.8f
            drawLine(
                color.copy(alpha = 0.08f + t * 0.12f),
                Offset(vpX - spread, y),
                Offset(vpX + spread, y),
                strokeWidth = 1f
            )
        }

        // Radial rays
        for (i in 0 until rays) {
            val angle = (i.toFloat() / rays) * PI.toFloat() - PI.toFloat() / 2
            val endX = vpX + cos(angle) * w
            val endY = vpY + sin(angle) * h
            drawLine(color, Offset(vpX, vpY), Offset(endX, endY), strokeWidth = 1f)
        }
    }

    /** Draw scanline overlay */
    fun DrawScope.scanlines(alpha: Float = 0.06f, spacing: Float = 4f) {
        val h = size.height
        val w = size.width
        var y = 0f
        while (y < h) {
            drawLine(
                Color.Black.copy(alpha = alpha),
                Offset(0f, y), Offset(w, y),
                strokeWidth = 1f
            )
            y += spacing
        }
    }

    /** Draw a diamond/rhombus shape */
    fun DrawScope.diamond(center: Offset, radius: Float, color: Color) {
        val path = androidx.compose.ui.graphics.Path().apply {
            moveTo(center.x, center.y - radius)
            lineTo(center.x + radius, center.y)
            lineTo(center.x, center.y + radius)
            lineTo(center.x - radius, center.y)
            close()
        }
        drawPath(path, color)
    }

    /** Draw a piano octave segment showing which keys are active */
    fun DrawScope.pianoKeys(
        x: Float, y: Float, w: Float, h: Float,
        activeDegrees: IntArray, color: Color, dimColor: Color
    ) {
        val keyW = w / 12f
        val blackKeys = intArrayOf(1, 3, 6, 8, 10)

        // White key outlines
        for (i in 0 until 12) {
            if (i in blackKeys) continue
            val kx = x + i * keyW
            val active = i in activeDegrees.toSet()
            drawRect(
                if (active) color.copy(alpha = 0.8f) else dimColor.copy(alpha = 0.15f),
                Offset(kx, y), Size(keyW - 1f, h)
            )
        }

        // Black keys (shorter, on top)
        for (i in blackKeys) {
            val kx = x + i * keyW
            val active = i in activeDegrees.toSet()
            drawRect(
                if (active) color.copy(alpha = 0.9f) else dimColor.copy(alpha = 0.3f),
                Offset(kx, y), Size(keyW - 1f, h * 0.6f)
            )
        }
    }

    /** Diagonal chevron divider */
    fun DrawScope.chevronDivider(y: Float, color: Color, height: Float = 20f) {
        val w = size.width
        val mid = w / 2f
        val path = androidx.compose.ui.graphics.Path().apply {
            moveTo(0f, y)
            lineTo(mid, y + height)
            lineTo(w, y)
        }
        drawPath(path, Color.Transparent, style = androidx.compose.ui.graphics.drawscope.Stroke(width = 1f))
        drawLine(color.copy(alpha = 0.4f), Offset(0f, y), Offset(mid, y + height), strokeWidth = 1f)
        drawLine(color.copy(alpha = 0.4f), Offset(mid, y + height), Offset(w, y), strokeWidth = 1f)
    }

    /** Eased scroll position — cubic ease out */
    fun easeOut(t: Float): Float {
        val t1 = 1f - t
        return 1f - t1 * t1 * t1
    }
}
