package com.waveloop.miniwave

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.IBinder

class MiniwaveService : Service() {

    companion object {
        const val CHANNEL_ID = "miniwave_engine"
        const val NOTIF_ID = 1
        const val HTTP_PORT = 8080
    }

    inner class LocalBinder : Binder() {
        fun getService(): MiniwaveService = this@MiniwaveService
    }

    private val binder = LocalBinder()
    val engine = MiniwaveEngine()
    var isRunning = false
        private set

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (!isRunning) {
            startForeground(NOTIF_ID, buildNotification())
            startEngine()
        }
        return START_STICKY
    }

    override fun onDestroy() {
        stopEngine()
        super.onDestroy()
    }

    private fun startEngine() {
        val ok = engine.nativeStart(filesDir.absolutePath, HTTP_PORT)
        isRunning = ok
        if (ok) {
            android.util.Log.i("miniwave", "engine started, HTTP on port $HTTP_PORT")
        } else {
            android.util.Log.e("miniwave", "engine failed to start")
            stopSelf()
        }
    }

    private fun stopEngine() {
        if (isRunning) {
            engine.nativeStop()
            isRunning = false
            android.util.Log.i("miniwave", "engine stopped")
        }
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            getString(R.string.notification_channel_name),
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "Keeps the synth engine running"
            setShowBadge(false)
        }
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(channel)
    }

    private fun buildNotification(): Notification {
        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP
        }
        val pi = PendingIntent.getActivity(
            this, 0, intent, PendingIntent.FLAG_IMMUTABLE
        )

        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.notification_title))
            .setContentText(getString(R.string.notification_text))
            .setSmallIcon(R.drawable.ic_notification)
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
    }
}
