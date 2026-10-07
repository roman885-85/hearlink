# Development and debugging

[← back to main page](../../README.md) · [Українська](../uk/development.md)

## Environment
- Arduino IDE 2.x or `arduino-cli`; core **esp32 3.3.3** (ESP-IDF 5.5); the **U8g2** library.
- Python 3 with Pillow (simulator pictures), a C++ compiler (simulators on the computer).
- Building: `./build.sh hearlink s3 [port]`; the result is `build/hearlink-s3/hearlink.ino.bin` (the update file)
  and `hearlink.ino.elf` (**keep it next to the .bin** — without it a crash dump cannot be analyzed).

## Workflow for a version
1. Change `FW_VERSION` in `hearlink/config.h`.
2. `python3 tools/make_lang.py` — it must say «без перекладу: 0» (untranslated: 0).
3. `tools/simrx/run.sh` and/or `tools/sim/run.sh` — look at the pictures of the screens that were changed.
4. Build, upload **to the transmitter** by cable; to the receivers — over the radio: the `M1` command or the
   «Оновлення» (Update) window.
5. Check on the hardware (below). Only then — "done".
6. `tools/make_kit.sh` — the flashing kit; save a copy of the source and the `.elf`.

Rules the code sticks to:
- new settings fields — **only at the end** of `Settings`;
- the over-the-air update format does not change; new features — as new packet types or a new length;
- every type of encrypted message has its own type number (`SEC_…`);
- new large buffers — in PSRAM; the transmitter must keep at least 40 KB of free internal memory;
- everything that shows "I am working" must move; a new on-screen text — in Ukrainian in the code and a translation
  in `tools/lang_en.py`.

## Mandatory check after every change

In the commands below, `ПОРТ_ПЕРЕДАВАЧА` and `ПОРТ_ПРИЙМАЧА` are placeholders for the transmitter port and the
receiver port; `конец` (Russian for "end") is part of the `sleeptest.py` syntax.

```
# air: 60 s, transmitter and receiver on cables
QUIET=1 python3 tools/watch2.py ПОРТ_ПЕРЕДАВАЧА ПОРТ_ПРИЙМАЧА 60
#   expected: відмов 0, пропущено 0, провалів у приймача 0 мс
#             (refusals 0, skipped 0, receiver dropouts 0 ms)

# sleep and wake-up
python3 tools/sleeptest.py ПОРТ_ПЕРЕДАВАЧА 4:z1 26:z0 конец:42
#   expected: «не на зв'язку» → after z0 «СПИТЬ, вихід СТОЇТЬ» → in 2–5 s «вихід живий»
#             ("offline" → "ASLEEP, output STOPPED" → "output alive")

# knob after sleep and after an over-the-air update (over the radio, from the transmitter's port)
python3 tools/txlog.py ПОРТ_ПЕРЕДАВАЧА 30 '1:G1' '3:@* n8=1' '6:@* n8=1000' '10:@* n7=3' '13:@* n8=3' '15:@* n8=4' '18:@* ?' '25:G0'
#   expected: interrupt type 0, enabled 0; after the presses «працює N с» ("uptime N s") has not been reset
```

The frame rate of the receiver screen is in its report («екран: 62 кадрів за секунду» — "screen: 62 frames per
second"). The smoothness of the transmitter screen — `tools/fliptest.py`, `tools/idletest.py`.

## Screen simulators
- **Transmitter**: `tools/sim/run.sh` builds the real `txui.h` with stubs and puts snapshots of the pages into
  `tools/sim/out/`. The scenes are in `tools/sim/sim.cpp` (touches `tap(x, y)`, saving `save("назва")`, where
  `назва` is a name).
- **Receiver**: `tools/simrx/run.sh` builds the real `rxscreens.h` with the U8g2 library, makes frames every 40 ms,
  a sheet of key frames `out/kadry.png` and an animation `out/pryimach.gif`.
- Check a drawing change **by the picture**, not only by the program state.

## Checking audio and cipher on the computer
`tools/host/run.sh`: takes the real `proto.h` and runs audio through all four qualities with losses of 0, 5 and
20 %; stereo — signal/noise per channel and crosstalk; `sec_test.cpp`, `sec_kat.py` — a check of the cipher against
reference vectors.

## Crash dump
On a crash the board prints "Guru Meditation Error…" to the port with the cause and addresses, and saves a dump to
the `coredump` partition. Analysis without third-party programs (everything is in the Arduino packages):

```
E=~/Library/Arduino15/packages/esp32/tools
# 1. read the partition
$E/esptool_py/*/esptool --chip esp32s3 -p ПОРТ -b 460800 read_flash 0xFF0000 0x10000 core.bin
# 2. cut off the 24 bytes of header (the first 4 bytes are the length)
python3 -c "import struct;d=open('core.bin','rb').read();n=struct.unpack('<I',d[:4])[0];open('core.elf','wb').write(d[24:n])"
# 3. what all the tasks were doing
$E/xtensa-esp-elf-gdb/*/bin/xtensa-esp32s3-elf-gdb -batch -ex "info threads" -ex "thread apply all bt" hearlink.ino.elf core.elf
# a single address from the report
$E/esp-x32/*/bin/xtensa-esp32s3-elf-addr2line -pfiaC -e hearlink.ino.elf 0x4038175f
```

The start reason is in `?`:

| Text | What it means |
|---|---|
| увімкнення живлення (power-on) | a normal start, a reset by the button or by the cable |
| перезапуск програмою (restart by software) | update, command, menu |
| ПРОСІДАННЯ ЖИВЛЕННЯ (POWER SAG) | the power supply cannot cope |
| ЗБІЙ ПРОГРАМИ (PROGRAM CRASH) | an exception — take a dump |
| ЗАВИСАННЯ (сторож переривань) (HANG (interrupt watchdog)) | an endless interrupt or a critical section that is too long |
| ЗАВИСАННЯ (сторож) (HANG (watchdog)) | a task on core 0 does not yield the processor |

## The radio "black box"
`Q` — counters, the last snapshot and the events of the current minute. A snapshot: the second, the channel, how
many packets the driver holds, how long it has been silent, the error code, memory, and the 40 events before the
failure with their time relative to it (channel change, hop, channel measurement, settings write, card, message,
serial command, screen page, revival attempts).

## How to add…

**…a receiver setting controlled from both sides:**
1. a field at the end of `Settings` (`config.h`) + a range check in `settingsLoad`;
2. an `RXP_…` number in `proto.h`, branches in `rxParamGet` / `rxParamSet` (`rxlink.h`) — this is the only "door"
   for the knob, the command and the port; values over the radio are 0…31;
3. a menu item in `ui_rx.h` (`MENU_NAME`, `MENU_ICON`, `PARAM_ITEM`, `menuValue`, `menuChange`), an icon in
   `rxs::menuIcon`;
4. a row in the transmitter's «Налаштування» (Settings) window (`txui.h`: `RXS_NAME`, `rxsText`, `rxsStep`);
5. a translation, a scene in the simulator, a check of `i<no.>=…` and `n`.

**…a receiver screen:** a drawing function in `rxscreens.h` (drawing only, no hardware — then it also builds on the
computer), an `SC_…` number and the selection in `ui_rx.h`, a scene in `tools/simrx/sim.cpp`.

**…a serial command:** a branch in `command()` (`hearlink.ino`) and a line in the header of the file.

**…a sound quality:** a row in `QDEF` (`proto.h`); the packet must be ≤ 250 bytes and must "fit" (`qFits`).

## Your own logo, your own sounds, your own language
- logo: `tools/gen_logo.py` → `logo.h`, `logo_oled.h`;
- fonts: `tools/make_fonts.py`;
- sounds: `assets-src/` → `tools/make_sounds.py` → `tools/flash_assets.sh`;
- a third language: at present there is one dictionary (`lang_en.h`), the choice is `uiLang` 0/1; a third language
  needs a second dictionary and an extension of `tr()` in `lang.h`.
