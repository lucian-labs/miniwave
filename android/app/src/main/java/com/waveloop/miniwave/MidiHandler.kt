package com.waveloop.miniwave

import android.content.Context
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.media.midi.MidiOutputPort
import android.media.midi.MidiReceiver
import android.os.Handler
import android.os.Looper
import android.util.Log

/**
 * Handles Android MIDI API → MiniwaveConnection dispatch.
 * Works in both local (JNI) and remote (HTTP) modes.
 * Opens USB MIDI devices and forwards raw events through the connection interface.
 */
class MidiHandler(
    private val context: Context,
    private val conn: MiniwaveConnection
) {
    companion object {
        private const val TAG = "miniwave-midi"
    }

    private val midiManager: MidiManager? =
        context.getSystemService(Context.MIDI_SERVICE) as? MidiManager

    private var openPort: MidiOutputPort? = null
    private val handler = Handler(Looper.getMainLooper())

    private val receiver = object : MidiReceiver() {
        override fun onSend(msg: ByteArray, offset: Int, count: Int, timestamp: Long) {
            var i = offset
            while (i < offset + count) {
                val status = msg[i].toInt() and 0xFF
                if (status and 0x80 == 0) { i++; continue }

                val needed = when (status and 0xF0) {
                    0x80, 0x90, 0xA0, 0xB0, 0xE0 -> 3
                    0xC0, 0xD0 -> 2
                    0xF0 -> { i++; continue }
                    else -> { i++; continue }
                }

                if (i + needed > offset + count) break

                val d1 = if (needed > 1) msg[i + 1].toInt() and 0x7F else 0
                val d2 = if (needed > 2) msg[i + 2].toInt() and 0x7F else 0

                // Route through connection — works for both local (JNI) and remote (HTTP)
                val ch = status and 0x0F
                when (status and 0xF0) {
                    0x90 -> if (d2 > 0) conn.noteOn(ch, d1, d2) else conn.noteOff(ch, d1)
                    0x80 -> conn.noteOff(ch, d1)
                    else -> {
                        // CC, pitch bend, aftertouch — use midi_raw for local, API for remote
                        if (conn is LocalConnection) {
                            (conn as LocalConnection).engine.nativeMidiEvent(status, d1, d2)
                        }
                        // Remote: TODO — add midi_raw API endpoint for full MIDI passthrough
                    }
                }

                i += needed
            }
        }
    }

    private val deviceCallback = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(device: MidiDeviceInfo) {
            Log.i(TAG, "MIDI device added: ${device.properties}")
            if (openPort == null) connectDevice(device)
        }

        override fun onDeviceRemoved(device: MidiDeviceInfo) {
            Log.i(TAG, "MIDI device removed")
            openPort?.close()
            openPort = null
        }
    }

    fun start() {
        midiManager ?: run {
            Log.w(TAG, "MIDI not available on this device")
            return
        }

        midiManager.registerDeviceCallback(deviceCallback, handler)

        val devices = midiManager.devices
        for (device in devices) {
            if (connectDevice(device)) break
        }
    }

    fun stop() {
        midiManager?.unregisterDeviceCallback(deviceCallback)
        openPort?.close()
        openPort = null
    }

    private fun connectDevice(info: MidiDeviceInfo): Boolean {
        val outputPorts = info.ports.filter {
            it.type == MidiDeviceInfo.PortInfo.TYPE_OUTPUT
        }
        if (outputPorts.isEmpty()) return false

        midiManager?.openDevice(info, { device ->
            if (device == null) {
                Log.e(TAG, "failed to open MIDI device")
                return@openDevice
            }

            val port = device.openOutputPort(outputPorts[0].portNumber)
            if (port != null) {
                port.connect(receiver)
                openPort = port
                Log.i(TAG, "connected to MIDI device")
            }
        }, handler)

        return true
    }
}
