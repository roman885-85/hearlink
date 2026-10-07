# Hardware and schematics

[← back to main page](../../README.md) · [Українська](../uk/hardware.md)

## What you need

### Transmitter (1 pc.)

| Part | What exactly | Notes |
|---|---|---|
| Module | **ESP32-4848S040** (also known as ESP32-4848S040C_I): ESP32-S3-WROOM-1U **N16R8**, 4″ 480×480 display (ST7701), GT911 capacitive touch sensor, microSD slot, CH340 USB bridge | it must be the version with 16 MB flash and 8 MB PSRAM |
| Antenna | external 2.4 GHz antenna for the IPEX (U.FL) connector | a "1U" module without an antenna is barely heard: on the bench −45…−68 dBm instead of −14 |
| Power | 5 V supply, **at least 1 A** | do not power it from a laptop port: the pulse current is high and the port "drops off" |
| Audio input | 4 resistors of 10 kΩ, capacitors of 1 µF and 4.7 nF, bias filter 1 kΩ + 100 µF | there is no 3.3 V on the H1 connector, so the bias is taken from 5 V through a filter |
| Isolation (recommended) | 3.5 mm ground loop isolator between the mixer and the transmitter | removes 50 Hz hum |
| Memory card (optional) | microSD, FAT32 | recording the broadcast, files on air, updates |
| External ADC (optional) | **PCM1808** module | stereo and lower noise; supported in the firmware, **not verified on the 4848 module** |

### Receiver (one per listener)

| Part | What exactly | Notes |
|---|---|---|
| Board | **ESP32-S3-DevKitC-1 N16R8** (or a compatible one with 16 MB flash and 8 MB PSRAM) | PSRAM is needed for the over-the-air update: the 2.2 MB image is received into memory as a whole |
| Display with knob | "M75" module: OLED **SH1106 1.3″ 128×64 I2C** + **EC11** encoder with a button | power it **from 3.3 V only**: the I2C pull-ups on the module go to VCC |
| Audio output | headphones directly: 1 kΩ + 10 nF on each leg and a 1 µF coupling capacitor | or a headphone amplifier with a differential input (MAX97220) |
| External DAC (optional) | **PCM5102** module | supported in the firmware, not verified on the boards |
| Power | power bank or a battery with a 5 V output | the display is optional: without it the receiver simply plays |

The receiver board can also have no display and no knob — it is then controlled from the transmitter (volume,
turning off, all settings).

## Pins

### Receiver (ESP32-S3)

| Purpose | Pin |
|---|---|
| Display SDA, SCL | 8, 9 |
| Knob: A (TRA), B (TRB), button (PSH) | 5, 6, 7 |
| Module buttons CON, BAK (not used, pulled up) | 15, 16 |
| Audio output "+", "−" (one-bit PDM) | 17, 18 |
| PCM5102: BCK, LCK, DIN | 11, 12, 13 |
| On-board RGB LED (WS2812) | 48 (38 on newer boards) |

### Transmitter on the ESP32-4848S040 module

| Purpose | Pin |
|---|---|
| Audio input (built-in ADC) | IO2 |
| PCM1808: MCLK, BCK, LRCK, DOUT | IO43 (TXD), IO1, IO2, IO40 |
| Memory card SPI: SCK, MOSI, MISO, CS | 48, 47, 41, 42 |
| GT911 sensor SDA | 19 |
| Backlight | 38 |

The module has only three free pins: IO1, IO2, IO40 (on the H1 connector they are labeled L1–L3; their order on the
connector is found with the serial command `H`). When the PCM1808 is in use, the serial output (TXD) is taken by the
ADC clock — the transmitter prints nothing to the port but still accepts commands.

### Transmitter on a plain ESP32-S3 board (no display)

Audio input — pin 4 (ADC), PCM1808 — 10 (SCK), 11 (BCK), 12 (LRC), 13 (OUT). Control — from the serial port only.

## Schematics

Transmitter on the module with a display, input — built-in ADC:

![Transmitter](../skhema-peredatchik-4848.png)

Input: R1, R2 of 10 kΩ each sum the left and right channels of the mixer, R3, R4 of 10 kΩ each set the bias at
mid-level, C1 of 1 µF blocks the DC component, C2 of 4.7 nF cuts off radio frequencies. The signal is attenuated by
half; up to 2 V can be applied to the input.

Receiver, headphones directly to the board:

![Receiver with headphones](../skhema-priemnik-s3-navushnyky.png)

Receiver with a headphone amplifier ("antiphase" output):

![Receiver](../skhema-priemnik-s3.png)

Variants with external converters (not verified on the boards):
[transmitter + PCM1808](../skhema-peredatchik-4848-pcm1808.png),
[receiver + PCM5102](../skhema-priemnik-s3-pcm5102.png).

The schematics are drawn by `tools/gen_schema.py`; they are also available as SVG.

## Two ways to connect headphones

In the receiver menu «Виводи» (Pins) (and on the transmitter: receiver window → «Налаштування» (Settings) →
«Вихід звуку» (Audio output)):

- **«2 канали» (2 channels)** — the left channel on pin 17, the right one on pin 18; headphones directly or a stereo
  amplifier. Stereo and balance work.
- **«протифаза» (antiphase)** — one and the same sound on both pins in antiphase: twice the swing for an amplifier
  with a differential input. With headphones connected directly, the sound in this mode is "inside out" — do not
  turn it on.

Pins 17 and 18 are set to the highest drive current (up to 40 mA). The volume in directly connected headphones
depends on their impedance: with 32 Ω it is quieter than with an amplifier.

## Assembly sequence

1. First flash the board ([Installation](install.md)) and make sure it starts.
2. Connect **the display only** — check the splash screen. Then the knob. Then the audio. One part at a time: this
   way you can see exactly what does not work.
3. Before applying power, check that the display VCC is on 3V3, not on 5V.
4. Connect the audio from the mixer last, starting from a low level.
