# Android Miniwave Remote — Viz & Control App

## Goal

Standalone Android app that connects to a miniwave instance on the LAN for **both visualization and synth control**. WebView loads the full SPA shell (`http://<host>:8080/`) — all nav tabs, channel switching, instrument editing, viz, and touch control in one app.

This replaces the current workflow of opening Chrome on the phone, navigating to the URL, dealing with HTTPS warnings, adding to homescreen, etc.

## Why an App

- No HTTPS required (WebView can load HTTP freely)
- Fullscreen immersive mode (no browser chrome, no URL bar)
- Auto-discover miniwave on LAN via mDNS
- Can persist the last-known host
- One tap launch from home screen
- Native features: orientation lock, keep-screen-on, wake lock
- **Full control surface** — not just eye candy, actually play and program the synth

## Target Devices

- Galaxy S10 (Android 12+, dev mode enabled)
- Pixel 2 XL (Android 11+, dev mode enabled)
- Sideloaded via `adb install`

## Architecture

### Minimal Android App (Kotlin)

Single-activity app with a fullscreen WebView. No native rendering needed — the viz page already runs great in a browser, just needs the WebView wrapper.

```
android-viz/
  app/
    src/main/
      java/com/waveloop/miniwaveviz/
        MainActivity.kt        — fullscreen WebView, immersive mode
        DiscoverActivity.kt    — mDNS scan, host picker
      res/
        layout/activity_main.xml
        mipmap/                — app icon (use miniwave icon)
      AndroidManifest.xml
  build.gradle.kts
```

### MainActivity.kt — Core Logic

```kotlin
class MainActivity : AppCompatActivity() {
    private lateinit var webView: WebView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Fullscreen immersive
        window.setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        )
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE

        webView = WebView(this)
        webView.settings.javaScriptEnabled = true
        webView.settings.domStorageEnabled = true
        webView.settings.mediaPlaybackRequiresUserGesture = false

        // Allow HTTP (cleartext)
        // Already handled by android:usesCleartextTraffic="true" in manifest

        setContentView(webView)

        val host = intent.getStringExtra("host") ?: getLastHost() ?: "pocketwave.local"
        webView.loadUrl("http://$host:8080/")  // Full SPA shell — viz, control, all tabs
    }
}
```

### AndroidManifest.xml — Key Entries

```xml
<uses-permission android:name="android.permission.INTERNET" />
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
<uses-permission android:name="android.permission.ACCESS_WIFI_STATE" />

<application
    android:usesCleartextTraffic="true"
    android:theme="@style/Theme.AppCompat.NoActionBar">
```

### mDNS Discovery (Optional but Nice)

Use Android NSD (Network Service Discovery) to find `_http._tcp` services named "miniwave" on the LAN:

```kotlin
val nsdManager = getSystemService(Context.NSD_SERVICE) as NsdManager
nsdManager.discoverServices("_http._tcp", NsdManager.PROTOCOL_DNS_SD, listener)
```

Or simpler: just try `pocketwave.local:8080` and `waveloop.local:8080` in sequence.

### Build & Sideload

```bash
# Build
cd android-viz
./gradlew assembleDebug

# Install via ADB
adb install app/build/outputs/apk/debug/app-debug.apk
```

## Features

### MVP (v1)

- Fullscreen WebView loading `/` (full SPA shell with nav tabs)
- Portrait + landscape support (landscape locks for viz, portrait for control UI)
- Keep screen on (wake lock)
- Hardcoded host with settings override
- `android:usesCleartextTraffic="true"` (no HTTPS needed)
- All existing pages work: rack detail/grid, keyseq, effex, chords, config, viz, touch

### v2

- mDNS auto-discovery
- Host picker (remember last used)
- Reconnect on network change
- SSE connection status indicator
- App icon from miniwave branding
- Quick-toggle button: swap between full control UI and fullscreen viz overlay
- Hardware volume buttons → master volume API calls

## Viz Page Compatibility

The viz page (`common/web/viz.html`) already targets these phones:
- Canvas2D with lo-res (320x180) nearest-neighbor upscale
- Touch handling for logo click (navigate back)
- Portrait rotation message
- No external dependencies

The WebView just needs to load it and stay out of the way.

## Notes

- `pocketwave.local` mDNS resolution works on Android if Bonjour/mDNS is available on the network. If not, use IP directly (192.168.x.x)
- SSE (EventSource) works in Android WebView
- Canvas2D with `image-rendering: pixelated` works in Android WebView (Chromium-based)
- The viz page already handles all the data fetching — the app is purely a container
