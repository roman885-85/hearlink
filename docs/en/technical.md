# How it works: full technical description

[← back to main page](../../README.md) · [Українська](../uk/technical.md)

Contents: [general layout](#general-layout) · [transmitter](#transmitter) · [receiver](#receiver) ·
[radio](#radio) · [packet and audio](#packet-and-audio) · [feedback](#feedback-and-commands) ·
[security](#security) · [update](#firmware-update) · [memory](#board-memory) ·
[firmware files](#firmware-files) · [quirks and pitfalls](#quirks-and-pitfalls) · [not verified](#not-verified)

## General layout

```
mixer ──line output──► [transmitter: ADC → frames → (compression) → cipher+signature] ══ ESP-NOW, 2.4 GHz ══►
      ══► [receiver: signature check → decryption → loss recovery → buffer → pace adjustment →
           clarity → volume → limiter → balance → one-bit output] ──► headphones
receiver ──(once every 2 s: information about itself)──► transmitter ──(commands)──► receiver
```

- One transmitter **broadcasts**; there can be any number of receivers, up to 16 in the transmitter's list.
- The processor everywhere is an ESP32-S3 (two cores, 240 MHz, hardware AES), 16 MB flash, 8 MB PSRAM.
- The audio rate is 32,000 samples per second, 16 bit. The audio is cut into chunks of 64 samples (2 ms).
- The protocol version is 4 (`PROTO_VER`); packets with another version are discarded.

## Transmitter

### Design
ESP32-4848S040 module: ESP32-S3-WROOM-1U, 480×480 display with a parallel RGB bus (ST7701 controller), GT911 touch
sensor (I2C), microSD card (SPI), CH340 USB bridge, a relay and a speaker amplifier (not used).

### Audio path
1. **Input.** The built-in ADC in continuous mode takes 64,000 samples per second; pairs are summed — the result is
   32,000 (this also gives a small gain in noise). The pace of the whole kit is set by the input. The external
   PCM1808 is connected over I2S, stereo.
2. **Gain** (−12…+24 dB), measurement of level, peak and overload (the edge of the ADC scale → the `FLAG_CLIP` flag
   in the packet → the receiver shows «Гучно на вході!» (Input too loud!)).
3. **Source**: the input, a test sound from the `assets` partition (voice — uncompressed, tunes — MP3 decoded on the
   fly by a separate task with a one-second buffer in PSRAM) or a file from the card.
4. **Frame**: 1, 2, 4 or 6 chunks depending on the quality. For 16 kHz — a half-band filter and decimation by two
   (flat up to 6 kHz, −3 dB at 7.5 kHz, −35 dB at 10 kHz).
5. **Compression**: IMA ADPCM (4 bits per sample) with selection of the initial step: the encoder tries several
   steps and takes the one with which the frame comes out more accurate (on sharp sound onsets this reduces channel
   crosstalk from −9 to −38 dB). The «найвища» (highest) quality goes uncompressed.
6. **Packet**: header + current frame + 1–2 previous frames (spare copies) → encryption and signature → ESP-NOW.

### Tasks (FreeRTOS)
| Task | Core | Priority | What it does |
|---|---|---|---|
| `tx` | 0 | 20 | input → frames → packets; commands to receivers; announcement of a channel hop |
| Wi-Fi (ESP-IDF core) | 0 | 23 | radio |
| `mp3` | 1 | 1…3 | MP3 decoding: priority 1 while the buffer is above 3/4, and 3 when it is below 45 % |
| `ui` | 1 | 2 | display and touch sensor |
| `rec` (card) | 1 | 1 | WAV recording, file manager, looking through the `UPDATE` folder — only this task works with the card |
| `touch` | 1 | 4 | polling the sensor, the queue of touches |
| `scan` | 1 | 3 | measuring channel occupancy (for the duration of the search) |
| `ota` | 0 | 19 | distributing firmware to receivers (for the duration of the update) |
| main loop | 1 | 1 | port, reports, saving settings, radio and ADC watchdogs |

### Screen
- Drawing goes not to the display but to a "shadow" sheet in memory; at the end of a frame only the changed
  rectangle is transferred to the display — no intermediate states are visible.
- Frame output to the RGB panel is custom: in portions of 10 lines through an intermediate buffer with pre-loading
  into the cache. Any flash read through `esp_partition_read` turns the cache off on both cores for fractions of a
  millisecond, and the picture jerks — which is why the sounds partition is **mapped into memory** (`mmap`).
- The firmware chooses the buffer for every portion itself: portion No. s always goes out of buffer s % 2 (output
  restarts from buffer 0 in every frame). The ESP-IDF 5.5.1 driver picks the buffer by the parity of an interrupt
  counter which, with `CONFIG_LCD_RGB_RESTART_IN_VSYNC`, it never resets: one merge of two interrupts and the picture
  stays broken until a restart (fixed in later ESP-IDF). The count of such faults is in the `u` report.
- While the transmitter's own firmware is being written, the cache is turned off for a long time — so the writing
  goes in cycles: backlight off → 1.7 s of writing → backlight on for 1.5 s. The screen warns about the blinking in
  advance.
- Page flipping is a slide lasting 284 ms (12 steps); MP3 decoding is paused for this time.

### Transmitter radio: resilience
- **Searching for a free channel**: 2 s of silence, 150 ms on each of the 13 channels; the share of busy time is
  counted, taking adjacent channels into account.
- **Congestion**: a second is "bad" if packets waited long for the air or the driver refused them. Five bad ones
  out of eight — a hop; if the occupancy is already known — without a pause (a `CMD_HOP` announcement to all
  receivers: "from packet N I am on channel C"), otherwise — with a search. It moves to a new channel only if that
  channel is freer by at least a third.
- **Thinning**: when the driver queue grows, the transmitter deliberately skips packets (`FLAG_THIN`); the receiver
  takes their frames from the spare copies — this is not loss.
- **Reviving the radio**: if the driver stops responding to sends (all 32 buffers are taken, error
  `ESP_ERR_ESPNOW_NO_MEM`), the transmitter revives it step by step: ESP-NOW restart → Wi-Fi restart → a quick
  restart of the board without the splash screen. The "black box" (`bbox.h`) remembers the last 40 events (channel,
  settings write, card, messages) and at the moment of the first failure takes a snapshot — prints it to the port
  («# РАДІО СТАЛО…», "# RADIO STOPPED…"), writes it to `/LOG/` on the card and carries it across the restart in the
  RTC memory. The cause of such a stall has not been found yet.

## Receiver

### Design
ESP32-S3-DevKitC-1 N16R8 board; SH1106 display (I2C at 800 kHz if the display can handle it, otherwise 400), EC11
encoder, on-board WS2812 RGB LED. The audio output is one-bit (PDM) on two pins through simple RC filters.

### Audio path
1. **Reception** (Wi-Fi task): own kit and version → signature check → replay protection → decryption.
2. **Recovery**: the packet number shows how many frames were lost. The last one or two are taken from the spare
   copies of the next packet; the rest are replaced by a smooth fade to silence, and the first frame after silence
   fades in smoothly (2–4 ms).
3. **16 → 32 kHz** with the same half-band filter (delay 0.72 ms).
4. **Buffer** — a ring of 256 ms; the sound starts playing once the set number of milliseconds has accumulated.
   The «сам» (auto) mode: buffer = the deepest dip over the last minute + 4 ms; after a dropout it immediately grows
   by the length of the dropout. The limits depend on the frame length: from 4 + frame/2 to 26 + 2·frame ms.
5. **Pace adjustment**: the crystals of the two boards run differently, so the receiver plays fractions of a
   percent faster or slower (cubic interpolation), holding the buffer. No more than ±0.3 % — otherwise a "drift" of
   pitch is audible on music.
6. **Speech clarity**: a "shelf" filter (+12 dB above 2 kHz) and a mix of "the sound + its boosted copy" in a
   proportion of 0.196 / 0.507 / 1 — that is +4 / +8 / +12 dB. "Strong": 1 kHz +0.9 dB, 2 kHz +6.0, 3 kHz +9.8,
   4 kHz +11.2, 8 kHz +12.0.
7. **Volume**: 21 steps of 2 dB; step 20 (100 %) — the audio as it is, below it — quieter (step 1 is −38 dB).
   There is no gain above “as it is” (since 2.40) — just like in an ordinary player or radio. Before 2.40 “as it is”
   was step 12 (60 %), and above it came gain up to +16 dB held by the limiter: on music mastered to full scale the
   limiter worked almost all the time above 60 % (at 85 % volume — 84 % of the time, up to −10.8 dB), and the sound
   “sagged” on every bass note. A quiet source is amplified at the transmitter: «Звук» (Sound) → «Підсилення входу»
   (Input gain). The volume changes smoothly. After an update from an older version the stored volume and its limit
   are converted once (+8 steps, not above 100 %), so that it sounds as before.
8. **Look-ahead limiter** (since 2.37) — common to both channels. The audio passes through a 1.5 ms delay
   (48 samples) and the gain is computed from the largest sample in that window: it goes down smoothly BEFORE the
   peak arrives and comes back slowly (a quarter of a second). When there is nothing to limit, the audio passes
   unchanged. Since 2.40 the threshold is full scale, so without “Clarity” the limiter does not work at all (the
   one-bit output still gets no more than 30 000 of 32 767). The previous limiter (instant attack, 80 ms release) clipped the top of each peak and its gain
   wandered by 1–2 dB within one bass period — with the DAC this was heard as overload in the bass, especially
   with “Clarity” switched on.
9. **Balance** — attenuation of one channel by 3 dB per step.
10. **Output**: PDM 32 kHz on pins 17/18. After power-on the pins rise to mid-level in 0.7 s; before sleep they
    drop to ground in 0.5 s — so that the capacitors do not click in the headphones.

### States
```
SEARCH (splash 4 s, waits up to 10 s) ─signal─► STARTUP (4.4 s; sound from 1.5 s) ─► RUNNING
RUNNING ─10 s without a signal─► LEAVING (6 s of a message) ─► STANDBY (sleep)
STANDBY ─heard the transmitter / knob pressed─► WAKE-UP (splash) ─► STARTUP
any ─"turn off" from the transmitter─► LEAVING ─► STANDBY (woken only by "turn on" or a double press)
```
The sleep cycle: radio off, light sleep of the processor for 0.5 s (woken by the timer or the knob button), then
70 ms of listening to its own channel; every fourth cycle — another 12 channels at 25 ms each. If the transmitter is
heard — once every ~2 s the receiver reports "I am asleep" and waits 80 ms for a command in reply.

### Tasks
| Task | Core | Priority | What it does |
|---|---|---|---|
| `rx` | 1 | 20 | audio output, in blocks of 1 ms |
| Wi-Fi | 0 | 23 | receiving packets |
| `ui` | 1 | 2 | screen at 60 frames/s, knob, menu |
| `led` | 0 | 3 | LED 200 times/s (smooth "breathing" by dithering between adjacent levels) |
| main loop | 1 | 1 | port, reports, states, sleep, information to the transmitter, update |

### Receiver screen
The frame is drawn by the U8g2 library into its own sheet; what goes to the display is **custom output**: the frame
is compared with the previous one, and for each of the eight pages of the display one transfer is sent with only the
changed columns. This gives 60 frames/s (the library gave 33). "Halftones" on the one-bit display — with a 4×4
raster; transitions between screens — by blending with the previous frame (dissolve, slide, circular reveal,
curtain).

## Radio
- **ESP-NOW**, broadcast address, channels 1–13 of the 2.4 GHz band, 20 MHz bandwidth.
- Rates: 1, 2, 5.5, 11 Mbit/s (DSSS, 802.11b), 6…54 Mbit/s (OFDM, 802.11g), 0.5 Mbit/s (Espressif's "long range"
  mode). On the bench, 3–8 % of packets were lost at 6–24 Mbit/s and 0–2 % at 1–11: a single carrier holds up
  better against narrow interference such as Bluetooth.
- The stream "fits" if a packet occupies no more than 45 % of the interval between packets.
- The transmitter listens to the air before every transmission, like an ordinary Wi-Fi participant, so it does not
  harm neighboring networks.
- Wi-Fi power saving is turned off (`WIFI_PS_NONE`).

## Packet and audio

Header (14 bytes, sent in the clear but signed):

| Field | Bytes | Content |
|---|---|---|
| `magic` | 2 | `0x4C48` ("HL") |
| `ver` | 1 | 4 |
| `kit` | 1 | kit number |
| `boot` | 4 | "epoch" — the number of the transmitter's power-on |
| `seq` | 4 | packet number |
| `flags` | 1 | low bits: tone, overload, silence, thinning; high 4 — the transmitter's channel number |
| `q` | 1 | quality (0–3), `0x80` — stereo, `0x40` — the interface language is English |

Then, encrypted: the current frame (uncompressed or an ADPCM block: first sample 2 bytes, step 1 byte, 4 bits per
sample), in stereo — a block of the channel difference, then the spare copies, and at the end — an 8-byte signature.

| Quality | Frame | Copies (mono/stereo) | Packet | Packets/s | Stream |
|---|---|---|---|---|---|
| «найвища» (highest) | 64 samples, 2 ms | 2 / 1 | ~220 bytes | 500 | ~850 kbit/s |
| «стандартна» (standard) | 128, 4 ms | 2 / 1 | ~223 | 250 | ~430 |
| «мова» (speech) | 128 @16 kHz, 8 ms | 2 / 1 | ~223 | 125 | ~215 |
| «дальня» (far) | 192 @16 kHz, 12 ms | 1 / — | ~220 | 83 | ~141 |

An ESP-NOW packet is no longer than 250 bytes — hence the frame lengths and the number of copies.

**Stereo**: what goes on air is the "mid" (L+R)/2 — the same sound as in mono — and the "difference" (L−R)/2. A
receiver in mono mode does not touch the difference; the spare copies carry only the mid (a recovered frame sounds
in mono).

**Channel number in the header**: adjacent Wi-Fi channels overlap, and a receiver may hear a packet while sitting
on an adjacent one; by this number it tunes exactly.

## Feedback and commands

| Message | Tag | Who → whom | Content |
|---|---|---|---|
| `RxStatus` / `RxStatus2` | `0x5348` | receiver → transmitter, once every 2 s | volume, flags (mute, turned off, screen, sound is playing, sleep, stereo, output alive), signal, buffer, loss, dropouts, uptime, version, name (13 or 32 letters) |
| `RxInfo` (since 2.32) | `0x5348`, different length | receiver → transmitter | clarity, balance, volume limit, view, LED, language, whether a headphone test is running |
| `TxCommand` / `TxCommand2` | `0x4348` | transmitter → receiver | command and argument (below) |
| `PairReq` / `PairRsp` | `0x5048` / `0x5148` | adding a receiver | X25519 public keys, the kit key under the shared secret |
| `KeyMsg` / `KeyReq` | `0x4B48` / `0x5248` | kit key change | the new key under the receiver's personal key |
| OTA | `0x4F48` | update | announcement, blocks, polling, status, cancellation |
| debugging | `0x4748` | both ways | serial lines and commands over the radio |

Transmitter commands: 1 "identify yourself", 2 name, 3 turn on/off, 4 volume, 5 mute, 6 output stereo/antiphase,
7 output check by ear, 8 "forget the kit", 9 channel hop (to everyone), 10 setting (number × 32 + value:
0 clarity, 1 balance+5, 2 volume limit, 3 view, 4 LED, 5 language), 11 headphone test.
Each command is sent four times; the receiver executes it once (power-on number + counter).

**Compatibility**: new messages are distinguished by length or by a new tag, old ones do not change — an old
transmitter simply skips the new ones. The over-the-air update format never changes: old receivers are updated
through it.

## Security
- **Kit key** — 32 random bytes, born in the transmitter, stored in the boards, never goes on air or to the port.
- **Cipher** AES-128-CTR, **signature** AES-128 CBC-MAC (8 bytes in the packet); encryption first, then the
  signature; on reception the signature is checked first. It is computed by the chip's **hardware AES**, block by
  block, directly through the registers — tens of microseconds per packet with no memory allocation. The ready-made
  AES-CCM from mbedTLS cost 1.2 ms per packet (60 % of the processor at 500 packets/s), so it is not used.
- **Nonce**: packet type + epoch + packet number (+ receiver number). Every message type has its own type number —
  the nonce never repeats.
- **Replays**: the receiver remembers the highest epoch and packet number; yesterday's recording of the air cannot
  be slipped to it.
- **Adding**: an X25519 exchange, the four-digit code is a fingerprint of the receiver's public key; the operator
  compares it by eye. The transmitter listens for requests only while the adding window is open.
- **The receiver's personal key** is derived from the same exchange: the new kit key is protected with it when
  someone is removed. A removed receiver cannot obtain the new key.
- What the security does not give: the air can still be jammed with interference, as before.

## Firmware update

**Over the radio** (`ota.h`):
1. The transmitter stops the sound and for 2.5 s announces: size, number of blocks, SHA-256 fingerprint, version.
2. The receiver compares the fingerprint with its own firmware: the same — "I have this one"; different — it
   takes memory for the whole image (PSRAM, ~2.2 MB) and answers "receiving".
3. Blocks of 200 bytes in a row (~11,000 blocks in ~11–18 s). Once every 0.3 s the receiver reports which blocks
   are missing; the transmitter resends them.
4. The receiver verifies the fingerprint of the whole image, writes it to the second firmware partition, makes this
   partition the boot one and restarts.

**From the card** (`upd.h`): the file is checked (an image for ESP32-S3, length, fingerprint in the tail, the tag
`HEARLINK-FW:<version>`); first to the receivers over the radio, then the transmitter writes itself. The order is
deliberate: while the distribution is in progress, both sides run the old firmware and are certain to understand
each other.

## Board memory

`hearlink/partitions.csv` (16 MB flash):

| Partition | Address | Size | What |
|---|---|---|---|
| nvs | 0x9000 | 20 KB | settings, keys, list of receivers |
| otadata | 0xE000 | 8 KB | which firmware partition to boot |
| app0 | 0x10000 | 3 MB | firmware |
| assets | 0x310000 | 9.9 MB | transmitter test sounds |
| app1 | 0xCF0000 | 3 MB | second firmware partition (update) |
| coredump | 0xFF0000 | 64 KB | dump of the last crash |

In NVS: namespace `hearlink`, entry `cfg` — the `Settings` structure in one piece (new fields are added only at the
end: an entry from an older version is shorter, from a newer one — longer, the beginning is read); `hl-peers` /
`list3` — the list of receivers with personal keys; a separate namespace — the kit key, the epoch, the key
generation.

The sounds partition: "HLPK", the number of entries, the entries (name 16 bytes, offset, length); the last byte of
the name is the type: 0 — uncompressed sound 32 kHz mono, 1 — MP3.

## Firmware files
| File | What is in it |
|---|---|
| `hearlink.ino` | startup, serial commands, reports, settings |
| `config.h` | version, sample rate, pins, settings structure |
| `proto.h` | packets, qualities, ADPCM, 16↔32 kHz conversion |
| `radio.h` | ESP-NOW, rates, parsing of incoming packets, reviving the radio |
| `sec.h` | cipher, signature, keys, adding and removing receivers |
| `bbox.h` | the radio "black box" |
| `txaudio.h`, `txmp3.h`, `assets.h` | input, test sounds, MP3 |
| `txpeers.h` | list of receivers, commands, "auto" rate |
| `txscan.h` | channel search, congestion, hop |
| `txrec.h`, `txfiles.h`, `fm.h`, `fmui.h`, `cardfmt.h` | card: recording, files, formatting |
| `ota.h`, `upd.h` | update over the radio and from the card |
| `panel4848.h`, `m2gfx.*`, `txui.h`, `ui_tx.h`, `slide.h`, fonts `f*.h`, `m2*.h` | transmitter screen |
| `rxaudio.h` | reception, buffer, audio output |
| `rxlink.h`, `rxled.h` | information to the transmitter, commands, states, sleep, LED |
| `rxfx.h`, `rxscreens.h`, `ui_rx.h` | receiver screen and menu |
| `lang.h`, `lang_en.h` | translation of on-screen texts |
| `dbgair.h`, `dbgtee.h` | debugging over the radio |

## Quirks and pitfalls

Things that have already been stepped on — so as not to step on them again.

- **A software restart of the ESP32 does not reset the pins.** The Arduino core's `pinMode` takes the pin's
  "previous interrupt type" and enables it by itself. In sleep, the knob button is a wake-up source with a "while
  the level is low" interrupt; after "sleep → over-the-air update → restart" the first press hung the board
  (interrupt watchdog). The cure: remove the interrupt and the wake-up source at startup and after every wake-up.
- **`attachInterrupt` installs the handler through a service task with a 1 KB stack** — if an audio interrupt
  arrives at that moment, the stack overflows ("Stack canary watchpoint triggered (ipc1)"). The knob interrupts are
  attached directly (`esp_intr_alloc` from the screen task).
- **The task watchdog watches only the idle task of core 0 (5 s)**: a high-priority task on core 0 must really
  block, otherwise there is a restart.
- **Flash operations turn the cache off on both cores** — the transmitter's picture jerks; read through `mmap`.
- **ESP-NOW packets longer than 250 bytes** are formally possible, but the driver sent them 2.5 times slower and
  went silent.
- **`WiFi.mode(WIFI_OFF)` in core 3.3.3 does not remove the driver**; the handler of the "station started" event
  turns power saving on by itself — it has to be turned off again.
- **The clear part of the packet under the signature must be no longer than 16 bytes**: with 20 the cipher silently
  refused.
- **The version in the receiver's message is ×10 with rounding** (2.31 and 2.34 are both "23") — versions cannot be
  compared by it; the transmitter learns the receiver's capabilities from which messages it sends.
- **A counter changed by two tasks without protection "drifts"** — make two counters (put / took).
- **The built-in ADC of the ESP32-S3** stops from time to time — there is a watchdog that restarts it.
- **Pins 17 and 18** are at their weakest after a reset (10 mA) — they are set to the highest drive current.
- **The 4848S040 module**: there is no 3.3 V on the connectors; the CH340 bridge makes errors at speeds above
  230,400; do not power it from a laptop port.

## Not verified
- Range in a large hall. One measurement: at 8 m with the module's internal antenna the receiver lost the signal;
  after the external antenna was connected there were no systematic measurements.
- The "input to headphones" delay acoustically: the estimate is 13–17 ms (since 2.37 including the 1.5 ms of the limiter) for "highest", ~20, 30–35 and 40–50 ms for
  the others.
- Receiver power consumption in operation and in sleep, battery run time.
- PCM1808 on the transmitter module. (PCM5102 on the receiver does produce sound — verified on 2026-10-08; its sound quality is still being evaluated.)
- More than two receivers at the same time.
- The cause of the rare stall of the transmitter's Wi-Fi driver (it revives itself; the details are written by the
  "black box").
