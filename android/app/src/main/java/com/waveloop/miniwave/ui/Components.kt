package com.waveloop.miniwave.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

/** Vertical peak meter bar */
@Composable
fun PeakMeter(
    peakL: Float,
    peakR: Float,
    color: Color,
    modifier: Modifier = Modifier
) {
    Canvas(modifier = modifier) {
        val w = size.width
        val h = size.height
        val barW = w / 2f - 1f

        // Left channel
        val hL = h * peakL.coerceIn(0f, 1f)
        drawRect(color.copy(alpha = 0.3f), Offset(0f, 0f), Size(barW, h))
        drawRect(color, Offset(0f, h - hL), Size(barW, hL))

        // Right channel
        val hR = h * peakR.coerceIn(0f, 1f)
        drawRect(color.copy(alpha = 0.3f), Offset(barW + 2f, 0f), Size(barW, h))
        drawRect(color, Offset(barW + 2f, h - hR), Size(barW, hR))
    }
}

/** Single slot card in the rack grid */
@Composable
fun SlotCard(
    index: Int,
    active: Boolean,
    typeName: String,
    peakL: Float,
    peakR: Float,
    volume: Float,
    mute: Boolean,
    solo: Boolean,
    focused: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val color = Mw.chColors[index % Mw.chColors.size]
    val borderColor = if (focused) color else Mw.border
    val bgColor = if (active) Mw.card else Mw.panel

    Row(
        modifier = modifier
            .clip(RoundedCornerShape(Mw.cardRadius))
            .background(bgColor)
            .border(1.dp, borderColor, RoundedCornerShape(Mw.cardRadius))
            .clickable(onClick = onClick)
            .padding(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        // Channel number
        Text(
            text = "${index + 1}",
            color = if (focused) color else Mw.dim,
            fontSize = Mw.labelSize,
            fontFamily = Mw.mono,
            modifier = Modifier.width(20.dp)
        )

        // Meter
        if (active) {
            PeakMeter(
                peakL = peakL,
                peakR = peakR,
                color = color,
                modifier = Modifier
                    .width(12.dp)
                    .fillMaxHeight()
                    .padding(vertical = 2.dp)
            )
            Spacer(Modifier.width(8.dp))
        }

        // Info
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = if (active) typeName else "—",
                color = if (active) Mw.text else Mw.dim,
                fontSize = Mw.bodySize,
                fontFamily = Mw.mono,
                maxLines = 1
            )
            if (active) {
                Text(
                    text = "vol ${(volume * 100).toInt()}%${if (mute) " M" else ""}${if (solo) " S" else ""}",
                    color = Mw.dim,
                    fontSize = Mw.labelSize,
                    fontFamily = Mw.mono
                )
            }
        }
    }
}

/** Horizontal slider with label */
@Composable
fun MwSlider(
    label: String,
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    color: Color = Mw.accent
) {
    Column(modifier = modifier) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label, color = Mw.dim, fontSize = Mw.labelSize, fontFamily = Mw.mono)
            Text(
                "${(value * 100).toInt()}%",
                color = Mw.text,
                fontSize = Mw.labelSize,
                fontFamily = Mw.mono
            )
        }
        Spacer(Modifier.height(4.dp))
        androidx.compose.material3.Slider(
            value = value,
            onValueChange = onValueChange,
            colors = androidx.compose.material3.SliderDefaults.colors(
                thumbColor = color,
                activeTrackColor = color,
                inactiveTrackColor = Mw.border
            )
        )
    }
}
