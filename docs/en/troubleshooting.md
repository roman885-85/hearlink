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
| the transmitter port is silent or pours out “garbage”, no replies to commands | mixer audio from the PCM1808 input is on air: its master clock occupies TXD, the pin the transmitter prints with | switch the test sound on («Перевірка» (Test) or `g5` blind) — the port comes back (since 2.56); or switch the input to the built-in ADC (`a0`) — for debugging only |
| `?` says «два кадри в пакеті поки не можна: приймач … зі списку не повідомляв версію 2.60 або новішу» (two frames per packet not possible yet) | the list of receivers contains a receiver with old firmware, or one the transmitter has not heard since its update | switch that receiver on nearby — it updates by itself; if it no longer exists — delete it from the list («Приймачі» → the receiver → «Видалити з набору»). Until then the transmitter works with single packets, as before |
| the sound in the receivers drops for fractions of a second, “no signal” flashes | the transmitter input is the built-in ADC: with it the transmitter radio stalls now and then and is revived in 0.2 s (the port prints “# РАДІО СТАЛО…”) | use the PCM1808 input («Звук» (Sound) → «Вхід» (Input)); with the built-in ADC this is unavoidable |
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
| the receiver plays, but in the transmitter's list it is «не на зв'язку» (not in touch) | the transmitting part of the receiver's radio has stalled (reception keeps working); before 2.63 only a restart of the receiver cured it | since 2.63 nothing: the receiver notices it by itself within 0.5–25 s and reloads its radio (a 20 ms break in the sound), as a last resort it restarts once (a second of silence). A record of the case is in the log on the transmitter's card (`# [number] приймач: …`); the counters are in the receiver's `?` report |
| a pop in the headphones when the receiver is switched on or restarts | the DAC opened before the receiver had finished booting | firmware 2.63 (XSMT wired to pin 14); if a pop at power-up remains — a 10–47 kΩ resistor between XSMT and ground ([hardware](hardware.md)) |
| «НЕ ПІДКЛЮЧЕНО» (NOT CONNECTED), no code, «увімкніть передавач» (switch transmitter on) | the transmitter is not heard | switch the transmitter on, bring the receiver closer |
| there is a code, but there is no request on the transmitter | the adding window is closed | «Приймачі» (Receivers) → «+ Додати» (+ Add) |
| «ПОШУК ПЕРЕДАВАЧА» (SEARCHING), then sleep | the transmitter is off or on another channel («Тиша» (Mute) on the transmitter has nothing to do with it: with mute on, packets are still sent) | switch the transmitter on; in the receiver menu set «Канал» (Channel) to "auto" |
| «НЕМАЄ СИГНАЛУ» (NO SIGNAL) from time to time | far away; a human body is in the way; busy air | closer to the transmitter; the transmitter antenna above head height; quality "speech" or «дальня» (far); rate "auto" |
| there is sound, but with short dropouts | packet loss is greater than the copies cover; the buffer is too small | leave «Запас» (Buffer) on «сам» (auto); a lower quality; another channel |
| the sound "drifts" in pitch | very uneven air | increase "Buffer" manually (15–25 ms) |
| quiet even at 100 % | directly connected headphones have low output; «Межа гучн.» (Volume limit) is turned on | check "Volume limit"; a headphone amplifier; headphones with a higher sensitivity |
| sound in one ear only / "inside out" | «Виводи» (Pins) does not match the wiring; balance | headphones directly — «2 канали» (2 channels); «Баланс» (Balance) — «рівно» (center); «Навушники → тест» (Headphones → test) shows which is left and which is right |
| the sound is gone while the receiver is connected and there is sound on air | the sound processing got stuck (before 2.84 — a “not-a-number” in the filters, cured only by a restart) | since 2.84 the receiver recovers by itself within 1.5–6 s; by hand — «Перезапуск» (Restart) in the menu or «Перезапустити» (Restart) in the receiver window on the transmitter (`E2 <number>` from the port) |
| choosing a rate or quality on the transmitter changes nothing | a distant receiver hears weakly, and the transmitter holds 1 Mbit/s for everybody (the row shows «5.5 (зараз 1)») | by design; the screen names the receiver that hears weakly. To always keep the choice — «Ефір» → «Слабкий сигнал» → “not lowering” (the distant receiver may then lose sound) |
| the sound is late, an “echo” with the live voice in the hall | packets wait for the air (a strong Wi-Fi network nearby) — the receivers keep a larger buffer | the «Ефір» line «пакети чекають ефіру до X мс»; another channel — away from strong networks («Знайти вільний канал» accounts for this since 2.82) or «Канал при заторі: сам» (the soft move since 2.83) |
| PCM5102 is silent | **the module's XSMT is floating** (the H3L jumper is open): a wire from XSMT to receiver pin 14 or to 3V3; the PCM5102 output is not selected; SCK is floating; BCK/LCK/DIN are swapped | menu «Вихід звуку: PCM5102»; SCK to pin 10 or to ground; jumper 3 to “H”; `n8=7` shows whether the clocks are running — see [Hardware](hardware.md#external-pcm5102-dac-on-the-receiver) |
| PCM5102 rasps, crackles, sounds “overloaded” on loud parts; digital “dirt”, a background buzz in the sound | the module's control pins FLT, DEMP, FMT are floating (solder jumpers on the back are open) | bridge H1L, H2L, H4L (verified on the device: H1L → H, H2L → H, H4L → L — the dirt was gone; per the chip datasheet all three to “L” is valid as well) or connect FLT, DEMP, FMT to GND (the G pin of the module header); `n8=9` shows that the signal to the DAC itself is correct |
| The sound is “grainy”, “sandy” in the treble — even with an ideal signal and no noise suppression; more noticeable in stereo and with an equaliser that raises the top | before 2.86: the stereo channel difference was 4-bit (its noise in the treble was almost as loud as the music); the DAC got 16 bits with truncation (at a low receiver volume — 5–10 bits); the limiter with a large «Підсилення» (Boost) and no «Стискання» (Compression) clipped peaks | update the transmitter and receivers to 2.87+; the transmitter's `?` should say «різниця каналів БЕЗ стиснення» (difference uncompressed); with Boost above +6 dB at full volume switch Compression to 1–3; to compare by ear — `Qr0` (as before 2.86) / `Qr1` |
| The sound is clean, but a short rustle is heard now and then, as if packets were lost | 2.86: the spare copies of the channel difference were 4-bit — every lost packet brought the “grain” back for 4 ms | update to 2.87+ (a single loss is restored bit for bit); receiver `n8=8`: “frames from spare copies … of them exact” — inexact ones only when two packets in a row are lost. If there are many losses even with a strong signal (−40…−55 dBm) — interference on the channel: another channel |
| The receiver's menu and the spectrum analyzer on its main screen are “sluggish”, especially with noise suppression | before 3.02 the display and the sound shared one core, and the sound chain (heavier since 2.86, 83–93 % of the core with stereo noise suppression) left the display no time: 17–30 frames/s instead of 60 | update to 3.02+; `G1` → «екран: 62–63 кадрів за секунду» (display: 62–63 frames a second); receiver `Y15` — the chain takes 480–630 µs per 1 ms on average |
| Clicks with noise suppression on, “one channel disappears” | before 2.89 the channel difference was processed differently from the mid (one overall number) — stereo fell apart; before 2.92 the 2 ms DAC output queue could not hold the heaviest block | update to 2.92+; receiver `n8=8`: “вихід лишався без даних 0 раз” (output ran dry 0 times) |
| A radio update of receivers takes minutes, “receiving” and “writing” crawl | before 2.90 the sound chain (especially with noise suppression) kept processing silence during the update | after updating to 2.90+ the next ones take about a minute; while receivers run an older version, switch noise suppression off for the update (`i16=0@id`) |
| CS4344 is silent | the CS4344 output is not selected («Вихід звуку» → CS4344, or `o2`); no master clock: MCLK is not on pin 10, or pin 10 is held on ground (then `n8=7` prints «ВИВІД 10 НА «ЗЕМЛІ»» — remove the short, e.g. an old SCK wire from a PCM5102); SCLK/LRCK/SDIN swapped (11/12/13); the module is powered from 5 V without its own regulator — then a logic “1” needs 3 V | `n8=7`: pins 10–13 must toggle, the line «головний такт … ділення 20 ціле → 8.000 МГц» (integer divider 20, 8 MHz); supply VCC from 3V3 |
| CS4344 is quiet and distorts on loud parts | headphones straight in the module jack: the output is line level, load from 3 kΩ | the MAX97220 headphone amplifier (hardware.md, CS4344 section); its gain may be set to 2 |
| with the CS4344 the sound appears half a second later after power-on or wake-up | this is the chip's own anti-click protection (Popguard: the output rises smoothly in 0.25–0.42 s) | nothing to do |
| noise in silence | a one-bit output without a filter; power | an RC filter as in the schematic; another supply/power bank; `X` from the transmitter — a check of where the noise comes from |
| clicks at power-on / when falling asleep | no coupling capacitors | build the output as in the schematic |
| the knob turns "the wrong way" | pins A and B are swapped | swap 5 and 6 |
| the receiver does not wake up from the transmitter | turned off from the transmitter | on the transmitter turn on the switch in the receiver's row (the receiver's own knob does not turn it on; if the transmitter no longer exists — cable and the `K0` command, the receiver will ask for access again) |
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
