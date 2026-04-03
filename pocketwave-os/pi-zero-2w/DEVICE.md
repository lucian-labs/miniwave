# Raspberry Pi Zero 2 W

## Hardware
- **SoC:** BCM2710A1 (quad Cortex-A53), 1GHz, aarch64
- **RAM:** 416MB (usable)
- **Storage:** 32GB SD (25GB free)
- **Audio:** HDMI only onboard (vc4-hdmi) — no headphone jack, needs USB or I2S DAC
- **USB:** 1x micro-USB OTG via hub (QinHeng USB Hub currently attached)
- **Network:** Wi-Fi 2.4GHz only (192.168.0.112)
- **OS:** Debian Trixie 13.2, kernel 6.12.62+rpt-rpi-v8

## SSH
- `waveloop@pocketwave.local` (password: `waveloop`)
- SSH key installed from dev machine

## Connected devices
- QinHeng USB Hub
- Cypress keyboard
- USB-C Digital AV adapter (HDMI out)

## Quirks
- **2.4GHz Wi-Fi only** — cannot connect to 5GHz SSIDs
- No onboard audio output — USB audio or I2S DAC required
- 416MB usable RAM — tight, but miniwave runs fine at this level (PocketCHIP has 512MB)
- Single USB OTG port — hub required for multiple USB devices
- User account is `waveloop`, not `elijah`
