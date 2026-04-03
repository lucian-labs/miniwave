# pocketwave-os

Custom OS configs for miniwave hardware devices. Each subfolder contains everything needed to turn a stock device into a dedicated pocketwave instrument.

## Devices

| Device | Dir | Status |
|--------|-----|--------|
| PocketCHIP | `pocketchip/` | Active — boots into pocketwave-x, lightdm masked |
| Raspberry Pi 4 | `pi4/` | Planned |
| Raspberry Pi Zero 2 W | `pi-zero-2w/` | Planned |

## How it works

Each device folder contains:
- `install.sh` — idempotent installer, run as root
- `services/` — systemd units
- `config/` — default config files, udev rules, xinitrc
- `scripts/` — launch, restart, mixer setup scripts
- `DEVICE.md` — device-specific notes, quirks, hardware info
