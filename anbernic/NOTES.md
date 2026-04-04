# Device Exploration — Cheap Hackable Noisemakers

The vibe: find cheap devices on marketplace, hack them into things that
make sound. Synths, samplers, noisemakers, atmosphere generators, voice
manglers, generative drones, lo-fi toys — literally anything that makes
noise under our control.

The bar is NOT "can it run miniwave." It's "can we get it to open an
audio device and make any amount of noise."

---

# Anbernic RG35XX OG

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

---

# Miyoo Mini Plus

## Status: Research

## Hardware

- Sigmastar SSD202D, dual-core Cortex-A7 @ 1.2GHz
- 128MB DDR3 (in-package, non-upgradeable)
- 3.5" IPS, 640x480
- Mono speaker, 3.5mm headphone jack
- 1x microSD
- Wi-Fi 2.4GHz, no Bluetooth
- USB-C (charge + OTG)
- D-pad, ABXY, L/R, L2/R2, start/select, menu
- ~3000mAh battery
- ~$50-55

## Hackability: YES

OnionOS is clean, well-documented Linux. Write a C program that opens
ALSA, make noise. Done.

128MB RAM is tight for full miniwave + UI but totally fine for:
- Simple generative synths (drones, saw melodies, FM bleeps)
- Sample players (load short clips off SD)
- Atmosphere generators (noise + filters + LFOs)
- Voice recorder/mangler (mic input via USB?)
- Lo-fi drum machines
- Anything single-purpose

Dual A7 @ 1.2GHz is weak compared to the H700 quad A53, but more
than enough for a single noisemaker app.

## Different SoC family

Sigmastar SSD202D ≠ Allwinner H700. Different kernel, different audio
codec, different boot chain. If we're building pocketwave OS for
multiple devices, this is a second platform to maintain.

For a one-off noise toy though — who cares. Flash OnionOS, drop a
binary on the SD card, map the buttons, go.

## Verdict

**Good cheap noise toy.** Don't overthink it. If you see one for $30
on marketplace, grab it.

---

# MagicX Mini Zero 28 V2

## Status: Research — probably skip

## Hardware

- Allwinner A133P, Cortex-A53 @ 1.8GHz
- 2GB DDR4
- 2.8" IPS, 640x480, OCA laminated
- Dual speakers (stereo)
- TF card up to 512GB
- Wi-Fi 2.4GHz
- USB-C
- 130x64x19mm, 129g
- 2900mAh battery
- $38 on sale / $61 regular

## The problem: Android

Runs 64-bit Android with "Dawn Launcher." Not Linux.

Specs are great — better than RG35XX in some ways (2GB RAM, stereo
speakers, Wi-Fi, higher clock). But flashing Linux on an A133P
Android device is uncharted territory. Unknown bootloader situation,
no CFW community for this device.

Could potentially run Termux + a native audio app, but that's janky.

## Verdict

**Skip unless you find confirmed Linux flash instructions for A133P
handhelds.** The specs are tempting but Android is a wall.
If someone cracks the bootloader it becomes the best device on this list.

---

# M5Stack Cardputer (StampS3)

## Status: Research — BUY candidate

## Hardware

- ESP32-S3, dual-core Xtensa LX7 @ 240MHz
- 8MB PSRAM, 8MB flash
- 1.14" LCD, 135x240
- I2S DAC + built-in speaker
- 56-key QWERTY keyboard
- USB-C
- Wi-Fi + Bluetooth
- GPIO broken out
- Credit card sized
- ~$80 CAD on Amazon (~$58 USD)

## Hackability: VERY YES

Not Linux — it's a microcontroller (ESP32-S3, Arduino/ESP-IDF). But
for noisemaking that's actually an advantage:

- **I2S audio** — write directly to DAC, deterministic latency, no
  driver/ALSA headaches
- **56 keys** — enough for a chromatic keyboard layout on QWERTY
- **ESP32 synth scene is huge** — tons of existing Arduino synth libs
- **GPIO** — wire up pots, sensors, expression pedals, whatever
- **Wi-Fi + BT** — OSC over Wi-Fi, MIDI over BT, both built in
- **Tiny** — actually pocketable, not "fits in a cargo pocket" pocketable

Won't run miniwave (no Linux, no ALSA). Needs purpose-built ESP-IDF
or Arduino code. But the keyboard + speaker + wireless makes it the
most instrument-like device on this list out of the box.

## What it's good for

- Tiny keyboard synth (map QWERTY to notes)
- Chiptune / 8-bit noise
- BT MIDI controller (keyboard sends MIDI to another device)
- Generative bleeps triggered by buttons
- OSC control surface for other devices
- Sensor-based noise (hook up a photoresistor, make theremin vibes)

## What it's NOT good for

- High-fidelity audio (small speaker, 8-bit I2S vibes)
- Complex multi-voice synthesis (240MHz is fast for a MCU but slow
  for DSP compared to ARM Linux handhelds)
- Anything that needs a real screen

## Verdict

**Best instrument-feel device on the list.** 56 keys + speaker + wireless
for $80 CAD. Different category from the Linux handhelds — lo-fi
microcontroller territory — but that's a feature not a bug.

---

# Devices evaluated and skipped

| Device | Why | Price seen |
|--------|-----|-----------|
| Oilsky digital player | No-brand, blob firmware, no OS access | — |
| Cayin N6ii (A01/AK4497EQ) | Amazing DAC but $350, Android, overkill | $350 |
| Anbernic RG35XX H | Great but $100 on marketplace, $60 new | $100 |
| Anbernic RG351MP | Old RK3326, no Wi-Fi, $100 asking (overpriced) | $100 |
| Labists Smart Mirror HM1 | Just a Pi in a box, not portable | — |

Note: all prices in CAD unless marked USD.
