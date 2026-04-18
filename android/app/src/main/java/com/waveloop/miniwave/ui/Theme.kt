package com.waveloop.miniwave.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

object Mw {
    val bg      = Color(0xFF0D0D0D)
    val panel   = Color(0xFF111111)
    val card    = Color(0xFF0E1525)
    val accent  = Color(0xFFE94560)
    val good    = Color(0xFF00D4AA)
    val text    = Color(0xFFEEEEEE)
    val dim     = Color(0xFFCCCCCC)
    val border  = Color(0xFF2A2A4A)
    val warn    = Color(0xFFFF8800)

    val chColors = listOf(
        Color(0xFFE94560), Color(0xFF00D4AA), Color(0xFF8866CC), Color(0xFFFF8800),
        Color(0xFF00AAFF), Color(0xFFFFD700), Color(0xFFFF66AA), Color(0xFF66FFCC),
        Color(0xFFAA44FF), Color(0xFFFF4400), Color(0xFF44DDFF), Color(0xFFAAFF00),
        Color(0xFFFF88CC), Color(0xFF44FF88), Color(0xFFFFAA44), Color(0xFF88AAFF),
    )

    val mono = FontFamily.Monospace
    val labelSize = 11.sp
    val bodySize = 14.sp
    val headerSize = 18.sp

    val cardRadius = 8.dp
    val cardPadding = 12.dp
}
