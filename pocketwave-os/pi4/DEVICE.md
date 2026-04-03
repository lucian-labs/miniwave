# Raspberry Pi 4

## Hardware
- **SoC:** BCM2711 (quad Cortex-A72), 1.5GHz, aarch64
- **RAM:** 4GB
- **Storage:** 32GB SD (23GB free)
- **Audio:** Universal Audio Volt 276 (USB, card 3), bcm2835 headphone jack (card 0), 2x HDMI
- **MIDI:** Volt 276 MIDI (hw:3,0,0), Corne v4 keyboard (hw:4,0,0)
- **Display:** HyperPixel 4 (DPI, rotated 270)
- **Network:** Wi-Fi (192.168.0.16), Gigabit Ethernet (down), BT 5.0 (down)
- **OS:** Debian Trixie 13.2, kernel 6.12.47+rpt-rpi-v8

## SSH
- `elijah@waveloop.local`

## Connected devices
- Universal Audio Volt 276 — USB audio interface + MIDI (port 28:0)
- foostan Corne v4 — split keyboard, also exposes MIDI (port 32:0)
- VIA Labs USB hub

## Boot config
- HyperPixel 4 display via `dtoverlay=vc4-kms-dpi-hyperpixel4,rotate=270`
- Onboard audio enabled (`dtparam=audio=on`)
- UART enabled
