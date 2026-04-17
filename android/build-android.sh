#!/usr/bin/env bash
# Build and optionally install miniwave Android app
set -e

cd "$(dirname "$0")"

echo "=== miniwave Android build ==="

# Check for Android SDK
if [ -z "$ANDROID_HOME" ] && [ -z "$ANDROID_SDK_ROOT" ]; then
    # Common locations
    for d in ~/Android/Sdk /opt/android-sdk "$HOME/Library/Android/sdk"; do
        if [ -d "$d" ]; then
            export ANDROID_HOME="$d"
            break
        fi
    done
fi

if [ -z "$ANDROID_HOME" ]; then
    echo "ERROR: ANDROID_HOME not set and SDK not found"
    echo "Install Android Studio or set ANDROID_HOME"
    exit 1
fi

echo "SDK: $ANDROID_HOME"

# Ensure gradlew exists
if [ ! -f gradlew ]; then
    echo "Generating gradle wrapper..."
    gradle wrapper --gradle-version 8.5 2>/dev/null || {
        echo "ERROR: gradle not found. Install gradle or Android Studio."
        exit 1
    }
fi

chmod +x gradlew

# Build
echo "Building debug APK..."
./gradlew assembleDebug

APK="app/build/outputs/apk/debug/app-debug.apk"

if [ ! -f "$APK" ]; then
    echo "ERROR: APK not found at $APK"
    exit 1
fi

echo "APK: $APK ($(du -h "$APK" | cut -f1))"

# Install if --install flag or device connected
if [ "$1" = "--install" ] || [ "$1" = "-i" ]; then
    echo "Installing via ADB..."
    adb install -r "$APK"
    echo "Launching..."
    adb shell am start -n com.waveloop.miniwave/.MainActivity
    echo "Done. Logs: adb logcat -s miniwave"
else
    echo "Run with --install to push to device"
fi
