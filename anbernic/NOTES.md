# Anbernic RG35XX OG — Synth Exploration

## Status: Research

## Hardware

- Allwinner H700, quad-core Cortex-A53 @ 1.5GHz
- 1GB LPDDR4
- 640x480 3.5" IPS
- Integrated SoC audio codec, 3.5mm stereo jack, mono speaker
- USB-C (OTG), 2x microSD
- No Wi-Fi, no Bluetooth, no GPIO
- ~3300mAh battery
- ~$55 new

## vs PocketCHIP

| | PocketCHIP | RG35XX OG |
|---|---|---|
| CPU | R8 Cortex-A8 1GHz single | H700 Cortex-A53 1.5GHz quad |
| RAM | 512MB | 1GB |
| Screen | 480x272 | 640x480 |
| Wi-Fi | Yes | No |
| USB ports | 2 (full + micro) | 1 (USB-C OTG) |
| GPIO | Yes | No |
| Keyboard | QWERTY | D-pad + 8 buttons |
| Audio codec | sun4i (period min ~1024) | H700 integrated (unknown) |

~6x more CPU. Much worse connectivity.

## Synth viability

### Pros
- CPU is overkill for audio DSP — all 16 channels easily
- Linux CFW ecosystem (GarlicOS, MuOS, Batocera)
- miniwave already targets Linux/ALSA
- Battery powered, pocketable
- Buttons mappable via evdev

### Cons
- No Wi-Fi kills OSC remote control / WaveUI from phone
- Single USB-C means MIDI adapter OR charging, not both (without OTG hub)
- No GPIO for pots/knobs/CV
- Integrated codec quality and latency behavior unknown
- No one has characterized real-time audio latency on H700

### USB MIDI
- USB OTG works, class-compliant devices show up via ALSA
- Need OTG hub with power passthrough (~$10) for MIDI + charging

## Port approach (if we do it)

Same pattern as pocketchip/:
1. Cross-compile for aarch64, `-mcpu=cortex-a53 -DNO_JACK`, ALSA only
2. Terminal UI (pocketwave-ui style) or framebuffer direct
3. Map gamepad buttons via evdev
4. Custom boot script, skip emulator frontend
5. Figure out H700 ALSA codec path and min period size

## Open questions

- What period sizes does the H700 codec accept? Latency floor?
- Which CFW gives best low-level access? GarlicOS vs MuOS?
- Can we get a custom kernel with RT patches on this SoC?
- Is the RG35XX Plus (with Wi-Fi) worth the extra $10?

## Recommendation

The Plus model ($60-65) adds Wi-Fi which unlocks the entire OSC/WaveUI
control surface. If buying new, get the Plus. The OG is only worth it
if you find one cheap on marketplace.
