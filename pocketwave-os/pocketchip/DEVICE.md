# PocketCHIP

## Hardware
- **SoC:** Allwinner R8 (ARM Cortex-A8), 1GHz
- **RAM:** 512MB
- **Display:** 480x272 touchscreen
- **Audio:** sun4i-codec onboard (fallback), Behringer UCA202 USB preferred
- **OS:** Debian Jessie (stock NTC image)

## Display
- Font: Terminus Bold 24 = 40 cols x 11 rows
- Unicode block chars are double-width in stterm — use ASCII `#` for bars
- Colors: cyan (labels), green (values), yellow (warnings). White is invisible.

## Boot sequence
1. systemd starts `pocketwave-x.service` (multi-user.target)
2. pocketwave-startx waits for VT, launches X via xinit
3. xinitrc: xhost, disable blanking, hide cursor, start engine, launch stterm+UI
4. USB hotplug (udev) restarts engine on audio connect/disconnect

## Quirks
- lightdm must be **masked** (not just disabled) or it comes back
- Enter key doesn't work in stterm — Tab is confirm
- USB audio card number shifts on replug — launch script auto-detects
- `cp: Text file busy` — must kill running binary before overwrite
- Stale X locks at `/tmp/.X0-lock` block restart

## SSH
- `chip@chip.local` (or static IP)
- Password: `waveloop`
- SSH key installed for passwordless access
- sudo password: `waveloop`
