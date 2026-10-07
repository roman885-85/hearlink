# Installation

[← back to main page](../../README.md) · [Українська](../uk/install.md)

There is one firmware for all boards. A freshly flashed board is a **receiver**; two serial commands turn it into a
transmitter.

## What you will need

- a USB cable **with data lines** (with a charge-only cable the board is not visible);
- a computer with macOS, Windows or Linux;
- the archive `hearlink-<version>-flash-kit.zip` from the [releases](../../../releases).

In the archive:

| File | Flash address | What it is |
|---|---|---|
| `firmware/bootloader.bin` | `0x0` | bootloader |
| `firmware/partitions.bin` | `0x8000` | partition table |
| `firmware/boot_app0.bin` | `0xe000` | firmware partition selector |
| `firmware/hearlink.bin` | `0x10000` | firmware |
| `firmware/assets.bin` | `0x310000` | transmitter test sounds (voice, music) — the receiver does not need them |
| `на карту/UPDATE/hearlink-<version>.bin` | — | file for updating via the memory card (the folder name `на карту` is Russian for "onto the card") |
| `tools/esptool` | — | flashing program for macOS |
| `1 Прошить приёмник.command` … | — | scripts for macOS (double-click); the name is Russian for "1 Flash the receiver" |
| `firmware/ВЕРСИЯ.txt` | — | version and SHA-256 checksums (`ВЕРСИЯ` is Russian for "VERSION") |

## The flashing program — esptool

- **macOS** — already in the archive (`tools/esptool`).
- **Windows** — `esptool-…-win64.zip` from the page <https://github.com/espressif/esptool/releases>.
- **Linux** — `pip install esptool` or the distribution package.
- USB bridge driver: the transmitter module has a CH340, the S3 boards have a CH343 or "native" USB. In
  Windows 10/11 and macOS they are usually already present; if the port does not appear — the CH34x driver from the
  WCH website.

Port name: macOS — `/dev/cu.usbserial-…` or `/dev/cu.usbmodem…`, Linux — `/dev/ttyUSB0` or `/dev/ttyACM0`,
Windows — `COM5` (look it up in Device Manager).

In the commands below, `ПОРТ` ("PORT") is a placeholder for your port name.

## Receiver

macOS: run `1 Прошить приёмник.command`.

Any system, from the archive folder:

```
esptool --chip esp32s3 --port ПОРТ --baud 460800 write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware/bootloader.bin 0x8000 firmware/partitions.bin 0xe000 firmware/boot_app0.bin 0x10000 firmware/hearlink.bin
```

After flashing, the board restarts: the splash screen appears, then «НЕ ПІДКЛЮЧЕНО» (NOT CONNECTED) with a code —
the receiver is asking for access to the kit (see [User guide](user-guide.md#adding-a-receiver)).

## Transmitter

macOS: `2 Прошить передатчик.command` (Russian for "2 Flash the transmitter"); when asked about the sounds for a
new module, answer «д» (Russian "да", yes).

Any system (the speed is 230400 — the CH340 bridge of this module makes errors at a higher one):

```
esptool --chip esp32s3 --port ПОРТ --baud 230400 write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware/bootloader.bin 0x8000 firmware/partitions.bin 0xe000 firmware/boot_app0.bin 0x10000 firmware/hearlink.bin 0x310000 firmware/assets.bin
```

The sounds (`assets.bin`, 8.7 MB) take about 9 minutes to upload and are needed only once; on subsequent flashes the
last pair can be left out.

Then assign the role. Open the port at **115200 baud** (any terminal program: the Arduino IDE serial monitor,
PuTTY, `screen`) and send two lines:

```
t
b1
```

`t` — "you are a transmitter", `b1` — "board with a display"; the board restarts after each. Back: `r` (receiver),
`b0` (plain board). From macOS/Linux, `python3 tools/port.py ПОРТ 6 t` does the same.

## If flashing does not work

- Put the board into flashing mode manually: hold **BOOT**, briefly press **RESET** (or apply power), release
  BOOT — and repeat the command.
- Reduce the speed: `--baud 115200`.
- Power the transmitter module from a 5 V ≥ 1 A supply, not from a port with no current headroom.
- Full erase (this also erases the settings and the access to the kit — the receiver will have to be added again):
  `esptool --chip esp32s3 --port ПОРТ erase-flash`, then flash again.

The settings, the kit key and the list of receivers are kept in a separate area of memory — ordinary flashing
preserves them.

## What is on the port?

`3 Что подключено.command` (macOS; Russian for "3 What is connected") or
`esptool --chip esp32s3 --port ПОРТ chip-id`. The board itself prints the firmware version and its role in response
to `?` on the port.

> If you have other boards with a CH340 bridge nearby (Arduino-compatible ones and so on) — disconnect them: esptool
> has no business with them, and the ports look the same.

## Updating a kit that is already working

No cable is needed. Put the file `hearlink-<version>.bin` on a memory card (FAT32) into the `UPDATE` folder in the
card root: `UPDATE/hearlink-2.34.bin` (any name, the `.bin` extension is required), insert the card into the
transmitter and press «Оновити» (Update) — see [User guide → Firmware update](user-guide.md#firmware-update).

## Building from source

1. Arduino IDE 2.x (or `arduino-cli`), in the Boards Manager — **esp32 by Espressif 3.3.3** (ESP-IDF 5.5).
   It has not been tested with other core versions.
2. The **U8g2** library (olikraus) via the Library Manager. Nothing else needs to be installed.
3. Open `hearlink/hearlink.ino`. Board: *ESP32S3 Dev Module*; **PSRAM: OPI PSRAM**; **Flash Size: 16MB**.
   The IDE picks up the partition table from `hearlink/partitions.csv` by itself.
4. Build and upload. For the transmitter module — Upload Speed 230400.

From the command line (macOS/Linux):

```
./build.sh hearlink s3                                    # build only → build/hearlink-s3/
./build.sh hearlink s3 /dev/cu.usbmodemXXXX               # build and upload (receiver)
SPEED=230400 ./build.sh hearlink s3 /dev/cu.usbserial-XX  # the same for the transmitter module
```

`build.sh` calls `arduino-cli` with the settings
`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB`.

Test sounds: put your own `assets-src/golos.wav` (voice), `muzyka.mp3`, `muzyka2.mp3`… and the titles in
`assets-src/nazvy.txt`, then run `python3 tools/make_sounds.py` and `tools/flash_assets.sh ПОРТ`.

The flashing kit (what is published in the releases) is assembled by `tools/make_kit.sh`.

### After changing on-screen texts

All on-screen texts are in Ukrainian in the program source; the English translation is the dictionary
`tools/lang_en.py`. `python3 tools/make_lang.py` builds `hearlink/lang_en.h` and shows the texts that have no
translation.

### Screens without a board

- `tools/sim/run.sh` — transmitter menu → pictures `tools/sim/out/*.png` (the same `txui.h` that goes into the
  board);
- `tools/simrx/run.sh` — receiver screens → `tools/simrx/out/kadry.png` and `pryimach.gif`;
- `tools/host/run.sh` — a check of the audio compression and the cipher on the computer.
