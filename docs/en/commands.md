# Serial commands and debugging

[← back to main page](../../README.md) · [Українська](../uk/commands.md)

USB port, **115200 baud**, a command is a line terminated by a newline. The first letter is the command, followed by
the argument with no space (`c7`, `V12`). Replies are in Ukrainian. A receiver is silent on the port until something
arrives from the port; after the first command it prints a report every second.

> **The transmitter port and the PCM1808.** The PCM1808 master clock goes out through TXD (IO43) — the very pin the
> transmitter prints to the port with. Since 2.56 the clock is put on the pin only while the PCM1808 input is actually
> on air. So:
> — with **the test sound or a card file** on air the port works fully, the input may stay PCM1808;
> — with **mixer audio through the PCM1808** on air the transmitter prints nothing (it accepts commands “blind”). To
> get replies and reports meanwhile, switch the test sound on (main page, «Перевірка» (Test), or `g5`) or switch the
> input to the built-in ADC (tab «Звук» (Sound) → «Вхід» (Input), or `a0`). With the built-in ADC the transmitter's
> radio stalls for 0.2 s now and then — it is for debugging only, not for real work.
> Before 2.56 the port was silent whenever the input was the PCM1808. Flashing over the cable works with either input.

From a computer: `python3 tools/port.py ПОРТ СЕКУНД [command …]` — send commands and show the replies;
`python3 tools/txlog.py ПОРТ СЕКУНД [second:command …]` — record all output with timestamps and send commands at
the specified seconds. (`ПОРТ` and `СЕКУНД` are placeholders: the port name and the number of seconds.)

## General

| Command | Action |
|---|---|
| `?` | all settings, version, reason for the last start |
| `t` / `r` | make the board a transmitter / a receiver (restart) |
| `b0` / `b1` | board: plain ESP32-S3 / ESP32-4848S040 module (restart) |
| `k<0–255>` | kit number |
| `c<1–13>` | channel; `c0` — the receiver searches for the channel by itself |
| `L0` / `L1` | interface language: Ukrainian / English |
| `K` | security: whether there is a kit key, counters of rejected packets; `K0` — forget the key |
| `B` | cipher self-test and measurement of its time |
| `u` | check of the display, knob, touch sensor, card |
| `x<0–3>` | debugging: an "idle" hardware AES run in addition to the working one |

## Transmitter

| Command | Action |
|---|---|
| `v<rate>` | 0.5 1 2 5.5 11 6 9 12 18 24 36 48 54 Mbit/s; `v0` — auto. Since 2.78 a rate chosen by hand is the upper step: for a weak receiver the transmitter steps down to 1 Mbit/s by itself (if «Слабкий сигнал» (Weak signal) is “lowering”) |
| `Ql2` / `Ql3` | the «Слабкий сигнал» (Weak signal) setting: do not lower / lower the rate for a weak receiver (stored, as from the screen; since 2.83) |
| `Ql0` / `Ql1` | forbid / allow the lower rate step temporarily — for measurements at a fixed rate (not stored) |
| `Qc0` / `Qc1` | forbid / allow the new far-mode codec — to compare by ear with the former compression (not stored; since 2.79) |
| `Qr0` / `Qr1` / `Qr2` | the channel difference of «найвища» in stereo: 4-bit as before 2.86 (to compare by ear) / uncompressed when every receiver on the list has 2.87+ (the default) / always (an experiment). Not stored; `?` tells which one is on air |
| `p<2–20>` | power, dBm |
| `q<0–3>` | quality: 0 «найвища» (highest), 1 «стандартна» (standard), 2 «мова» (speech), 3 «дальня» (far) |
| `g<0–6>` | source: 0 input, 1 tone, 2 voice, 3 voice and music, 4 music, 5 music with an announcement, 6 left/right channel check |
| `y<no.> <value>` | test sound: 1 tune (0 — in turn), 2 announcement once every N s, 3 music under the voice, dB, 4 pause between voice repeats, 5 music volume, 6 voice volume |
| `w0` / `w1` | stereo on air |
| `a0` / `a1` | input: built-in ADC / PCM1808 (restart) |
| `m` | silence on air (packets are sent, there is no sound) |
| `z1` / `z0` | go completely silent on air / come back — a check of receiver sleep |
| `s` | find a free channel |
| `h0` / `h1` / `h2` / `h3` | leaving a busy channel: off / on / move now / treat 6 s as "bad" |
| `h` / `h4` | state of the soft channel move (since 2.83): whether a trial is running, seconds with air waits of 15 ms or more, the experience of channels, scores from the scan / a trial of the best channel by the scan now, with a return after a minute (a check of the procedure) |
| `T0` / `T1` | packet thinning when the air is busy |
| `A1` / `A0` | count other parties' transmissions on our own channel |
| `O` | turn the transmitter off (a touch on the screen turns it on) |
| `S<name>` | sound source name; `S` — restore «З пульта» (Mixer) |
| `H` | 4848 module: check of the free pins IO1, IO2, IO40 (25 s) |
| `D` | memory card status |
| `Z1` / `Z0` / `Z` | recording to the card: start / stop / list; `Z2`, `Z3`, `Zv<file>` — a test pattern and its verification |
| `F…` | files: `Fl<folder>` list, `Fi<file>` info, `Fp<file>` on air, `Fs` stop, `Fm<folder>` create, `Fd<path>` delete, `Fc<file>\|<folder>` copy, `F` — what is on air |
| `Q` | the radio "black box"; `Q0…Q3` — up to which step to revive (an experiment only: the limit expires by itself after 60 s, or after 5 s without air); `Q7 Q8 Q9 Q6` — perform a step on a healthy radio; `Q10` — an imitated stalled driver; `Qn` — what the radio hears (noise level); `Qp<bits>` — radio protocol experiment (1 b, 2 g, 4 n, 8 LR; `Qp15` — as usual). `Q` also prints measurements: gaps between sends, the path of a lone packet, frame processing time by stages |
| `Y1`/`Y0`, `Y3`/`Y4`, `Y5…Y8` | experiments with display frame output |
| `M8` | full-screen warning “cable update — the screen goes dark for a minute” (25 s or until the board is reset) |
| `Qp<setting>=<value>` | (the display module, since 2.83) a choice “as from the screen” without a touch — a check of the handling; the numbers are `enum Param` in `txui.h` (2 — rate, 9 — quality, 20 — «Слабкий сигнал»). Since 2.86 only with the “=” sign: before that this parser also took `Qp<bits>`, and on the display module the radio protocol experiment did nothing |
| `e6=<0…3>`, `e8=<0…4>`, `e7=1` / `e7=0` | input noise reduction (since 2.71–2.72): strength / sensitivity / noise sample — remember now / auto in pauses |
| `Qd0` / `Qd1` / `Qd2` / `Qd3` | two frames per packet on «найвища»: never / automatically, when all receivers in touch can take it (the default) / always (an experiment); since 2.77 the double packet repeats the previous packet uncompressed, `Qd3` — the former compressed copies. Lasts until a restart. `?` says «ДВА кадри в пакеті» (two frames) or «один кадр у пакеті» (one frame) |
| `Y9`, `Y10`/`Y11` | knock the display driver out of its buffer order on purpose; put frame portions where the driver says (as before 2.49) / the firmware's own way |

### Receivers from the transmitter
| Command | Action |
|---|---|
| `P` | list of receivers with all readings and settings; since 2.73 — the line «звук від команди P0» (sound since P0): how much sound each one lost |
| `P0` | reset these counters |
| `I` | to the first one online — "identify yourself" |
| `E0` / `E1` | turn it off / on; `E0 <number>` / `E1 <number>` — that receiver |
| `E2 <number>` | restart that receiver (as the «Перезапустити» button; receivers with 2.84+) |
| `V<0–20>` | volume |
| `W0` / `W1` | output «протифаза» (antiphase) / "two channels" |
| `N<name>` | give a name |
| `X` | output check by ear (five states of 6 s each) |
| `i<no.>=<value>[@number]` | settings: 0 clarity 0–3, 1 balance −5…5, 2 volume limit 1–20, 3 view 0–2, 4 LED 0–3, 5 language 0–2; `i9=1` / `i9=0` — headphone test. Example: `i0=2@884A94` |
| `i7=<dB 0…24>[@number]` | digital boost of a receiver, a “trim” (on the receiver itself — `n10=<dB>`); the `n7=3` reply shows “boost +N dB (now …)” |
| `i6=<0…3>[@number]` | receiver knob lock: 0 none, 1 menu, 2 volume, 3 all (on the receiver itself — `n6=<0…3>`) |
| `i16…i22=<value>[@number]` | receiver sound processing (since 2.68 / 2.72): 16 noise reduction 0–3, 17 compression 0–3, 18 in pauses 0–3, 19 noise sensitivity 0–4, 20 pause threshold 0–4 (+3…+15 dB), 21 pause closing 0–2, 22 noise sample (2 — remember now, 0 — auto) |
| `i23`, `i24=<dB>[@number]`, `i25=<0/1>[@number]` | receiver equalizer: the 2.5 kHz and 6 kHz bands (−12…12 dB) and the low cut; the state and the level before/after are in the `n7=3` reply. **Known bug in 2.72–2.85:** the numbers `i20…i22` are taken by the processing, so the 125 Hz, 400 Hz and 1 kHz bands cannot be set from the port (from the transmitter screen they can) |
| `e`, `e<0…4>=<dB>`, `e5=<0/1>` | transmitter input equalizer: show / band / low cut; `e9=1` — probe: apply it to the 1 kHz tone too (for measuring) |
| `a0` / `a1` / `a2` | input: built-in / PCM1808 / probe — PCM1808 for one boot without the master clock (the port works; normally with PCM1808 the transmitter prints nothing: the clock goes out on the TXD pin) |
| `J` / `J1` / `J0` | access requests / open the adding window for 3 min / close it |
| `j<no.>` | allow access to a receiver from the list of requests |
| `R<number>` | remove a receiver from the kit (`R4C3B10`) |
| `M` / `M1` / `M0` | over-the-air update of receivers: status / start / cancel |
| `M6` / `M7` | automatic update of receivers with older firmware: on (the default) / off |
| `M2` / `M3` / `M4` / `M5` / `M9` | update everything from the card / look through the `UPDATE` folder / trial / allow an older version / forced pass |

### Transmitter screen hands-free
| Command | Action |
|---|---|
| `U<0–5>`, `U9` | open a tab (0 home … 5 help), 9 — the card |
| `Ut<no.>` | "press" the button with this number |
| `Ux<x>,<y>,<ms>` | a touch at a point; a series: `Ux<pause>:<x>,<y>,<ms>;<x>,<y>,<ms>;…` |
| `Uk`, `Uq` | what has been typed on the keyboard / what is on the card screen |
| `ud` | print every touch of the sensor |

## Receiver

| Command | Action |
|---|---|
| `d<0\|4–40>` | buffer, ms (0 — auto) |
| `l<0–20>` | volume |
| `m` | mute |
| `o0` / `o1` / `o2` | output: PDM / PCM5102 / CS4344 (restart; `o2` since 2.85) |
| `w0` / `w1` | pins: antiphase / two channels |
| `n` | the "for hearing" settings: clarity, balance, limit, view, LED, language |
| `n<no.>=<value>` | change one (numbers as in `i`) |
| `n9=1` / `n9=0` | headphone test |
| `n7=1` / `n7=2` / `n7=3` | open the menu / the «Зв'язок» (Link) screen / the main screen |
| `n11=1` / `n11=2` / `n11=0` | compatibility check without an old firmware at hand: do not understand double packets / also call itself version 2.56 (the transmitter goes back to single packets) / as is. Until a restart |
| `n12=1…4` / `n12=0` | check of the receiver radio self-repair without a real fault: 1 — the driver “took the packet and stays silent”; 2 — packets “go nowhere” (until the radio is reloaded); 3 — the same until the board restarts; 4 — and after the restart too; 0 — off |
| `Y2` | (since 2.93) free time of both cores over a second, arbitrary units: more means freer. The receiver's second core runs the sound and the display |
| `Y12` / `Y13` | (since 2.93) a frame goes to the display at the display's priority, as before 2.93 (to compare) / above the sound (the default). Until a restart; the frame rate is in the report with `G1` — «екран: N кадрів за секунду, малювання …, передача кадру …» (display: N frames a second, drawing, frame transfer) |
| `Y15` | (since 2.94) stages of the sound chain, µs per 1 ms block since the previous such call: the total and the longest block (since 3.12 at the start of the line — a long line is cut over the radio), before the pace conversion (buffer, pace, settings, volume), pace conversion, noise suppression, processing and output, the rest. Above ~900 µs on average the sound core is short of time |
| `Y16` | (since 2.98) the pace-conversion convolution alone: cycles per call (3.02 — 648) |
| `n14` | (since 3.03; since 3.13 information only) which display is in use: a jumper selects it — pin 21 to GND — colour, open — OLED |
| `n15=1…5` | (since 3.03) brightness: colour display backlight or OLED contrast (as the "Brightness" menu item) |
| `n27=<sum of bits>` | (since 3.03) colour module options, restart: 1 — rotated by 180°, 2 — BGR colours, 4 — 80 MHz bus, 8 — 40 MHz (as before 3.11; without 4 and 8 — 20 MHz), 16 — strong bus outputs (as before 3.11; without it — the weakest) |
| `Y17` / `Y18` | (since 3.12) OLED: the main-screen status line redrawn every frame (as before 3.12) / taken from a cache and redrawn only when something in it changed (the default). Compare "drawing" in the display report |
| `Y19` / `Y20` / `Y21` | (since 3.12) spectrum analysis with the own FFT (as before 3.12) / with the esp-dsp library (the default; 5.4 times faster) / both on one copy of the audio window: band levels, the largest difference in dB and the time of each |
| `Y22` | (since 3.10) colour display: interference check from its bus — 7 phases of 5 s (display still / bus busy non-stop with different output drive); in each — radio noise, "air busy", packet losses; the summary as a table in the report |
| `Y23[=0…3]` | (since 3.11) colour display: 8 s of full frames non-stop — listen for pick-up in the headphones; bus output drive for that time (without a number — as configured) |
| `Y14` | core time per task over 2 s — only in a build with `#define HL_TASK_LOAD` (the FreeRTOS functions take 600 bytes of fast memory, which the transmitter cannot spare) |

The receiver's `?` report (since 2.63) has two lines about its radio: how many packets were sent and how many the
driver answered, the longest wait for an answer, how many times the radio was reloaded (did not answer + the
transmitter did not hear) and whether the transmitter hears the receiver: «передавач мене чує: ТАК / НІ / невідомо»
(yes / no / unknown).

### Knob hands-free
The pin shorts itself to ground — exactly what the button or the encoder contact does.

| Command | Action |
|---|---|
| `n8=1` | button pin configuration (interrupt type, whether it is enabled, whether it wakes from sleep) and sleep counters |
| `n8=<ms>` (from 50) | press for this many milliseconds: 300 — a press, 1000 — a long one (opens the menu) |
| `n8=3` / `n8=4` | turn by one click in one direction and in the other |
| `n8=5` | press for a second without holding up the main loop |
| `n8=6` | the same after 30 s — a check of waking from sleep (in sleep the port is not listened to) |
| `n8=7` | what is on the audio output pins: share of ones and edges on 10 (SCK), 11 (BCK), 12 (LRCK), 13 (data), 17, 18 (PDM); the XSMT level (pin 14); with the CS4344 (2.85) also the master clock divider (integer or fractional), the output rate and whether pin 10 is held on ground |
| `n8=9` | “logic analyzer”: samples BCK, LRCK and data on the pins (the DAC is muted meanwhile, known numbers 0x5A01 / 0xA5F3 go to the output) and prints the bits of one word — the format and bit shift become visible |
| `n8=8` | true rates since the previous such call: audio from the air (transmitter clock) and the output (own clock), in samples per second and ppm |
| `n8=2` | leave the pin "dirty", as after sleep in old versions (a check of the cleanup at startup) |
| `n8=11` / `n8=12` / `n8=13` / `n8=14` | a check of the sound self-recovery (since 2.84): a “not-a-number” into the processing input / into the equalizer memory / hold the output on one value (the watchdog must reset the processing, then restart the board) / clear |

## Debugging over the radio
On the transmitter: «Налашт.» (Settings) → «Налагодження ефіром» (Debug over air) or `G1` (turn off — `G0`). After
that, the serial lines of the receivers are visible on the transmitter's port with a number:
`[884A94] ПРИЙМАЧ к=7 …` (`ПРИЙМАЧ` = RECEIVER, `к` = channel). A command to another device of the kit:

```
@884A94 ?        to the receiver with this number
@tx u            to the transmitter (from a receiver's port)
@* ?             to everyone
```

Debug packets are protected with the kit key; a command is valid only while fresh.

## The once-a-second report

Transmitter:
```
ПЕРЕДАВАЧ к=7 шв=5.5 як=найвища набір 1 | пакетів 500, відмов 0, не пішло в ефір 0, перезапусків радіо 0,
до ефіру сер. 1600 макс. 6300 мкс | …
```
In English: TRANSMITTER ch=7 rate=5.5 q=highest kit 1 | packets 500, refusals 0, not sent on air 0, radio
restarts 0, wait for air avg 1600 max 6300 µs | …
- *відмов* (refusals) — the driver did not take the packet (the queue is full); *до ефіру* (wait for air) — how
  long a packet waited for free air: an average above ~10 ms or a maximum above ~60 ms means the air is busy.

Receiver:
```
ПРИЙМАЧ к=7 набір 1 | кадрів 500, відновлено 0, втрачено 0, порожньо 0.0 мс, зсувів 0, перезапусків 0, з копій 0 |
запас 10.6 мс (9.0…12.1, ціль 10.6 сам), темп +68 ppm | сигнал -37 дБм | рівень -11.5 дБ …
екран: 62 кадрів за секунду, передача кадру сер. 2.7 мс, найдовша 6.7 мс, обмін 800 кГц
```
In English: RECEIVER ch=7 kit 1 | frames 500, recovered 0, lost 0, empty 0.0 ms, slips 0, restarts 0, from
copies 0 | buffer 10.6 ms (9.0…12.1, target 10.6 auto), pace +68 ppm | signal -37 dBm | level -11.5 dB …
screen: 62 frames per second, frame transfer avg 2.7 ms, longest 6.7 ms, bus 800 kHz
- *відновлено* (recovered) — frames taken from the spare copies; *втрачено* (lost) — not recovered (heard as a
  short silence); *порожньо* (empty) — for how many milliseconds the buffer was empty; *зсувів* (slips) — the buffer
  overflowed and was reset; *перезапусків* (restarts) — reception started over; *з копій* (from copies) —
  deliberate thinning by the transmitter (not loss); *темп* (pace) — by how much the receiver adjusts its pace to
  the transmitter.

## Measurement tools (`tools/`)

| Tool | What it does |
|---|---|
| `port.py` | port: send commands, show replies |
| `txlog.py` | all board output to a file with time + commands on a schedule |
| `watch.py`, `watch2.py` | transmitter (and receiver) side by side: a summary over N seconds — waiting for air, refusals, dropouts |
| `duo.py`, `trio.py`, `kwatch.py` | measurements from two or three sides, a check of the key change |
| `sleeptest.py` | sleep and wake-up of receivers on a schedule of commands: `sleeptest.py ПОРТ 4:z1 26:z0 конец:42` (`конец` is Russian for "end") |
| `stallrun.sh` | a “does the transmitter radio stall” run: one line a minute — uptime, input, one or two frames per packet, radio stalls, frame processing time: `caffeinate -i tools/stallrun.sh PORT <a0\|a1\|-> <Qd0\|Qd1\|-> MINUTES` |
| `getlogs.py`, `logstat.py` | pull the `/LOG/` files from the card through the port and count radio stalls and receiver links |
| `qsweep.py` | comparison of qualities and rates: `qsweep.py ПОРТ СЕКУНД quality:rate[:power] …` |
| `sweep.py`, `hoptest.py` | comparison of channels, a check of the channel move |
| `fliptest.py`, `idletest.py` | smoothness of page flipping and steadiness of the transmitter screen |
| `sendfile.py` | put a file on the transmitter's card through the port |
| `langtest.py` | translation check |
| `linktest/` | a separate firmware for checking the radio link on "bare" boards |
