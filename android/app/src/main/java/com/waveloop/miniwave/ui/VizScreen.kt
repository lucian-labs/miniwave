package com.waveloop.miniwave.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.*
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import com.waveloop.miniwave.ui.Retro.scanlines
import kotlin.math.*

private const val FFT_SIZE = 256
private const val NUM_BANDS = 64

@Composable
fun VizScreen(
    scope: FloatArray,
    peaks: FloatArray,
    focusedCh: Int,
    modifier: Modifier = Modifier
) {
    val color = Mw.chColors[focusedCh % Mw.chColors.size]
    val smoothed = remember { FloatArray(NUM_BANDS) }
    val fftRe = remember { FloatArray(FFT_SIZE) }
    val fftIm = remember { FloatArray(FFT_SIZE) }
    var peakScale by remember { mutableFloatStateOf(1f) }

    Canvas(
        modifier = modifier
            .fillMaxSize()
            .background(Mw.bg)
    ) {
        val w = size.width
        val h = size.height

        // ── FFT ──
        for (i in 0 until FFT_SIZE) {
            val l = scope.getOrElse(i * 2) { 0f }
            val r = scope.getOrElse(i * 2 + 1) { 0f }
            val mono = (l + r) * 0.5f
            // Hann window
            val win = 0.5f * (1f - cos(2f * PI.toFloat() * i / (FFT_SIZE - 1)))
            fftRe[i] = mono * win
            fftIm[i] = 0f
        }
        fft256(fftRe, fftIm)

        val halfN = FFT_SIZE / 2
        val bandW = w / NUM_BANDS

        for (band in 0 until NUM_BANDS) {
            // Log-spaced bin grouping (quadratic → more low-freq resolution)
            val t0 = band.toFloat() / NUM_BANDS
            val t1 = (band + 1).toFloat() / NUM_BANDS
            val startBin = (t0 * t0 * (halfN - 1)).toInt() + 1
            val endBin = (t1 * t1 * (halfN - 1)).toInt().coerceAtLeast(startBin + 1)

            var mag = 0f
            val count = (endBin.coerceAtMost(halfN) - startBin).coerceAtLeast(1)
            for (bin in startBin until endBin.coerceAtMost(halfN)) {
                val re = fftRe[bin]
                val im = fftIm[bin]
                mag += sqrt(re * re + im * im)
            }
            mag /= count
            // Peak-hold with decay
            smoothed[band] = maxOf(mag, smoothed[band] * 0.86f)
        }

        // Auto-scale: find current max, ease toward it
        val currentMax = smoothed.max().coerceAtLeast(0.01f)
        peakScale += (1f / currentMax - peakScale) * 0.08f // ease toward fitting

        for (band in 0 until NUM_BANDS) {
            val barMag = (smoothed[band] * peakScale).coerceIn(0f, 1f)
            val barH = barMag * h * 0.7f
            val x = band * bandW + 1f
            val bw = bandW - 2f

            val hue = band.toFloat() / NUM_BANDS
            val bandColor = hueToColor(hue, color)

            // Fill bar — full height
            drawRect(
                bandColor.copy(alpha = 0.4f + barMag * 0.4f),
                Offset(x, h - barH), Size(bw, barH)
            )
            // Bright cap line
            if (barH > 2f) {
                drawRect(
                    bandColor.copy(alpha = 0.95f),
                    Offset(x, h - barH), Size(bw, 3f)
                )
            }
        }

        // ── Scope waveform overlay ──
        val scopePath = Path()
        val midY = h * 0.30f
        var started = false

        for (i in 0 until FFT_SIZE) {
            val l = scope.getOrElse(i * 2) { 0f }
            val r = scope.getOrElse(i * 2 + 1) { 0f }
            val mono = (l + r) * 0.5f
            val x = (i.toFloat() / FFT_SIZE) * w
            val y = midY - mono * h * 0.25f

            if (!started) { scopePath.moveTo(x, y); started = true }
            else scopePath.lineTo(x, y)
        }

        drawPath(scopePath, color.copy(alpha = 0.15f), style = Stroke(width = 10f, cap = StrokeCap.Round))
        drawPath(scopePath, color.copy(alpha = 0.6f), style = Stroke(width = 2f, cap = StrokeCap.Round))

        // ── Channel peak dots ──
        val dotSpacing = w / 16f
        for (i in 0 until 16) {
            val pk = maxOf(peaks[i * 2], peaks[i * 2 + 1])
            if (pk < 0.01f) continue
            val dotColor = Mw.chColors[i]
            drawCircle(dotColor.copy(alpha = 0.8f), 3f + pk * 8f,
                Offset(dotSpacing * i + dotSpacing / 2f, h - 16f))
        }

        scanlines(0.03f, 4f)
    }
}

/** In-place radix-2 Cooley-Tukey FFT (256-point) */
private fun fft256(re: FloatArray, im: FloatArray) {
    val n = re.size
    // Bit-reversal permutation
    var j = 0
    for (i in 1 until n) {
        var bit = n shr 1
        while (j and bit != 0) { j = j xor bit; bit = bit shr 1 }
        j = j xor bit
        if (i < j) {
            val tr = re[i]; re[i] = re[j]; re[j] = tr
            val ti = im[i]; im[i] = im[j]; im[j] = ti
        }
    }
    // Butterfly stages
    var len = 2
    while (len <= n) {
        val ang = -2.0 * PI / len
        val wRe = cos(ang).toFloat()
        val wIm = sin(ang).toFloat()
        var i = 0
        while (i < n) {
            var curRe = 1f; var curIm = 0f
            for (k in 0 until len / 2) {
                val idx = i + k + len / 2
                val tRe = curRe * re[idx] - curIm * im[idx]
                val tIm = curRe * im[idx] + curIm * re[idx]
                re[idx] = re[i + k] - tRe
                im[idx] = im[i + k] - tIm
                re[i + k] += tRe
                im[i + k] += tIm
                val newCur = curRe * wRe - curIm * wIm
                curIm = curRe * wIm + curIm * wRe
                curRe = newCur
            }
            i += len
        }
        len *= 2
    }
}

private fun hueToColor(t: Float, base: Color): Color = Color(
    red = base.red * (1f - t * 0.5f),
    green = base.green * 0.3f + t * 0.7f,
    blue = t * 0.8f + 0.2f,
    alpha = 1f
)
