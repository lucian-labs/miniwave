#!/bin/bash
# PocketWave OS Installer for PocketCHIP
# Run as root: sudo ./install.sh
set -e

echo "==============================="
echo "  PocketWave OS Installer"
echo "  Device: PocketCHIP"
echo "==============================="
echo ""

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: run with sudo"
    exit 1
fi

CHIP_HOME="/home/chip"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── 1. Build binaries ──────────────────────────────────
echo "[1/8] Building binaries..."
cd "$CHIP_HOME/miniwave/pocketchip"
make clean && make
echo "  miniwave: $(wc -c < miniwave) bytes"
echo "  pocketwave-ui: $(wc -c < pocketwave-ui) bytes"

# ── 2. Install binaries ────────────────────────────────
echo "[2/8] Installing binaries..."
install -m 755 miniwave      /usr/local/bin/miniwave
install -m 755 pocketwave-ui /usr/local/bin/pocketwave-ui

# Symlink web UI
mkdir -p /usr/local/share/miniwave
ln -sf "$CHIP_HOME/miniwave/common/web" /usr/local/share/miniwave/web

echo "  Binaries installed to /usr/local/bin/"

# ── 3. Install scripts ─────────────────────────────────
echo "[3/8] Installing scripts..."
install -m 755 "$SCRIPT_DIR/scripts/pocketwave-launch.sh"   /usr/local/bin/pocketwave-launch
install -m 755 "$SCRIPT_DIR/scripts/pocketwave-startx"      /usr/local/bin/pocketwave-startx
install -m 755 "$SCRIPT_DIR/scripts/pocketwave-restart"     /usr/local/bin/pocketwave-restart
install -m 755 "$SCRIPT_DIR/scripts/pocketwave-alsa-setup"  /usr/local/bin/pocketwave-alsa-setup

# ── 4. Config ──────────────────────────────────────────
echo "[4/8] Setting up config..."
mkdir -p "$CHIP_HOME/.config/pocketwave"
if [ ! -f "$CHIP_HOME/.config/pocketwave/config" ]; then
    install -m 644 -o chip -g chip "$SCRIPT_DIR/config/pocketwave.conf" \
        "$CHIP_HOME/.config/pocketwave/config"
    echo "  Default config written"
else
    echo "  Config already exists, keeping"
fi

# ── 5. X11 autostart ──────────────────────────────────
echo "[5/8] Configuring X11 autostart..."
install -m 755 -o chip -g chip "$SCRIPT_DIR/config/xinitrc" "$CHIP_HOME/.xinitrc"

if ! command -v unclutter >/dev/null 2>&1; then
    echo "  Installing unclutter..."
    apt-get install -y --force-yes unclutter >/dev/null 2>&1 || echo "  WARNING: unclutter not available"
fi

if ! dpkg -l xfonts-terminus 2>/dev/null | grep -q ^ii; then
    echo "  Installing Terminus font..."
    apt-get install -y --force-yes xfonts-terminus >/dev/null 2>&1 || echo "  WARNING: xfonts-terminus not available"
fi

# ── 6. Systemd services ───────────────────────────────
echo "[6/8] Installing systemd services..."
install -m 644 "$SCRIPT_DIR/services/pocketwave-x.service" /etc/systemd/system/
install -m 644 "$SCRIPT_DIR/services/pocketwave.service"   /etc/systemd/system/

# ── 7. USB hotplug rules ──────────────────────────────
echo "[7/8] Installing udev rules..."
install -m 644 "$SCRIPT_DIR/config/99-pocketwave-usb.rules" /etc/udev/rules.d/
udevadm control --reload-rules

# ── 8. Kill stock OS, enable PocketWave ────────────────
echo "[8/8] Switching boot target..."

# Mask stock (mask > disable — prevents reactivation)
systemctl mask lightdm.service       2>/dev/null || true
systemctl mask pocket-home.service   2>/dev/null || true
systemctl stop lightdm.service       2>/dev/null || true
systemctl stop pocket-home.service   2>/dev/null || true

# Enable PocketWave
systemctl daemon-reload
systemctl enable pocketwave-x.service
systemctl enable pocketwave.service

echo ""
echo "==============================="
echo "  PocketWave OS installed!"
echo "==============================="
echo ""
echo "  Binaries:  /usr/local/bin/miniwave, pocketwave-ui"
echo "  Config:    ~/.config/pocketwave/config"
echo "  Logs:      /tmp/miniwave.log"
echo ""
echo "  Reboot to start PocketWave OS:"
echo "    sudo reboot"
echo ""
echo "  To revert:"
echo "    sudo systemctl unmask lightdm"
echo "    sudo systemctl disable pocketwave-x pocketwave"
echo "    sudo systemctl enable lightdm"
echo "    sudo reboot"
