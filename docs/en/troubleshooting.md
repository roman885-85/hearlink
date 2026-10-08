# Troubleshooting

[← back to main page](../../README.md) · [Українська](../uk/troubleshooting.md)

## Flashing

| What you see | Cause | What to do |
|---|---|---|
| there is no port | a cable without data lines; no CH34x driver | another cable; the driver from the WCH website |
| "Failed to connect", "No serial data received" | the board did not enter flashing mode | hold BOOT, briefly press RESET, release BOOT, repeat |
| errors in the middle of writing on the transmitter module | the CH340 bridge cannot keep up with the speed; not enough current | `--baud 230400` or `115200`; 5 V ≥ 1 A power |
| after flashing the transmitter screen is dark | the board is still a "receiver without a display" | to the port: `t`, then `b1` |
| the port "disappears" while the module is running | powered from a laptop port | a separate 5 V supply |

## Transmitter

| What you see | Cause | What to do |
|---|---|---|
| «ТИХО НА ВХОДІ» (INPUT SILENT), the level is at zero | no signal from the mixer; the wrong output | check the cable and the level; «Перевірка» (Test) → if the receivers hear the test sound, the problem is in the input |
| «ПЕРЕВАНТАЖЕННЯ» (OVERLOAD), on the receivers «Гучно на вході!» (Input too loud!) | the level from the mixer is too high | reduce it on the mixer or set «Підсилення входу» (Input gain) to a negative value |
| 50 Hz hum | a ground loop between the mixer and the power supply | a 3.5 mm isolator; power from the same extension cord as the mixer |
| the receivers hear in fragments, «до ефіру» (wait for air) is large | the channel is busy | «Знайти вільний канал» (Find a free channel); «Канал при заторі: сам» (On congestion: auto); a lower quality («мова» (speech)) |
| weak signal on all receivers | no external antenna on the "1U" module | connect a 2.4 GHz antenna; power 20 dBm |
| quality «мова — за швидкістю» (speech — due to rate) | the selected quality does not fit into the rate | raise the radio rate or leave it as it is |
| the card «не відповідає» (does not respond) | not FAT32; poor contact | format it («Файли» (Files) → «Інше» (More) → «Форматувати» (Format)) — **erases everything**; the `D` command shows the cause |
| the picture blinks during the update | this is by design: writing to flash stops the frame output | wait, do not switch the power off |
| «# РАДІО СТАЛО…» ("# RADIO STOPPED…") on the port | the Wi-Fi driver stopped responding | nothing: the transmitter revives itself within seconds; send the line and the file in `/LOG/` to the developer |
| the transmitter restarted by itself | look at `?` → «запуск через…» (started because of…) | «ПРОСІДАННЯ ЖИВЛЕННЯ» (POWER SAG) — the power supply; «ЗАВИСАННЯ» (HANG), «ЗБІЙ» (CRASH) — take a crash dump (see [Development](development.md#crash-dump)) |
| the «Оновлення» (Update) window says «файл не годиться» (file is not usable) | the file was damaged while copying or it is not the kit's firmware | copy it again; check the SHA-256 against `ВЕРСИЯ.txt` (Russian for "VERSION") |
| it offers an "older" firmware | an old file is left on the card | delete it from `UPDATE` |

## Receiver

| What you see | Cause | What to do |
|---|---|---|
| the screen is dark, there is sound | the display was not found (power, SDA/SCL); the screen was turned off from the menu | VCC on 3V3, SDA=8, SCL=9; move the knob |
| «НЕ ПІДКЛЮЧЕНО» (NOT CONNECTED), no code, «увімкніть передавач» (switch transmitter on) | the transmitter is not heard | switch the transmitter on, bring the receiver closer |
| there is a code, but there is no request on the transmitter | the adding window is closed | «Приймачі» (Receivers) → «+ Додати» (+ Add) |
| «ПОШУК ПЕРЕДАВАЧА» (SEARCHING), then sleep | the transmitter is off or on another channel («Тиша» (Mute) on the transmitter has nothing to do with it: with mute on, packets are still sent) | switch the transmitter on; in the receiver menu set «Канал» (Channel) to "auto" |
| «НЕМАЄ СИГНАЛУ» (NO SIGNAL) from time to time | far away; a human body is in the way; busy air | closer to the transmitter; the transmitter antenna above head height; quality "speech" or «дальня» (far); rate "auto" |
| there is sound, but with short dropouts | packet loss is greater than the copies cover; the buffer is too small | leave «Запас» (Buffer) on «сам» (auto); a lower quality; another channel |
| the sound "drifts" in pitch | very uneven air | increase "Buffer" manually (15–25 ms) |
| quiet even at 100 % | directly connected headphones have low output; «Межа гучн.» (Volume limit) is turned on | check "Volume limit"; a headphone amplifier; headphones with a higher sensitivity |
| sound in one ear only / "inside out" | «Виводи» (Pins) does not match the wiring; balance | headphones directly — «2 канали» (2 channels); «Баланс» (Balance) — «рівно» (center); «Навушники → тест» (Headphones → test) shows which is left and which is right |
| PCM5102 is silent | **the solder jumpers on the back of the module are open (H3L must be bridged to “H”)**; the PCM5102 output is not selected; SCK is floating; BCK/LCK/DIN are swapped | menu «Вихід звуку: PCM5102»; SCK to pin 10 or to ground; jumper 3 to “H”; `n8=7` shows whether the clocks are running — see [Hardware](hardware.md#external-pcm5102-dac-on-the-receiver) |
| noise in silence | a one-bit output without a filter; power | an RC filter as in the schematic; another supply/power bank; `X` from the transmitter — a check of where the noise comes from |
| clicks at power-on / when falling asleep | no coupling capacitors | build the output as in the schematic |
| the knob turns "the wrong way" | pins A and B are swapped | swap 5 and 6 |
| the receiver does not wake up from the transmitter | turned off from the transmitter | on the transmitter turn on the switch in its row, or press the knob twice |
| the power bank switches off when the receiver sleeps | the power bank cuts off a low current | a power bank with a low-load mode or a battery with a power board |
| «потрібен USB (раз)» (needs USB (once)) during the update | an old partition table without a second firmware partition | flash it once by cable |
| «забракло пам'яті» (out of memory) during the update | a board without PSRAM | an N16R8 board is required |
| a hang and a restart on pressing the knob | versions before 2.33: a button interrupt left over after sleep | update to 2.34 |

## How to collect information for the developer

1. `?` to the port of the transmitter and of the receiver — version, settings, start reason.
2. `P` on the transmitter — the list of receivers.
3. `python3 tools/txlog.py ПОРТ 120 > log.txt` — two minutes of output (`ПОРТ` is a placeholder for the port name).
4. If there was a crash — the crash dump ([Development](development.md#crash-dump)) and the files from `/LOG/` on
   the card.

## Returning to the factory state
- Transmitter: «Налашт.» (Settings) → «Скинути все» (Reset all) (the role and the board type are kept). The
  receivers will have to be added again if you also erase the key: `K0`.
- Receiver: menu → «Забути набір» (Forget kit).
- Completely: `esptool --chip esp32s3 --port ПОРТ erase-flash` and flash again.
