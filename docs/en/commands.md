# Serial commands and debugging

[← back to main page](../../README.md) · [Українська](../uk/commands.md)

USB port, **115200 baud**, a command is a line terminated by a newline. The first letter is the command, followed by
the argument with no space (`c7`, `V12`). Replies are in Ukrainian. A receiver is silent on the port until something
arrives from the port; after the first command it prints a report every second.

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
| `v<rate>` | 0.5 1 2 5.5 11 6 9 12 18 24 36 48 54 Mbit/s; `v0` — auto |
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
| `T0` / `T1` | packet thinning when the air is busy |
| `A1` / `A0` | count other parties' transmissions on our own channel |
| `O` | turn the transmitter off (a touch on the screen turns it on) |
| `S<name>` | sound source name; `S` — restore «З пульта» (Mixer) |
| `H` | 4848 module: check of the free pins IO1, IO2, IO40 (25 s) |
| `D` | memory card status |
| `Z1` / `Z0` / `Z` | recording to the card: start / stop / list; `Z2`, `Z3`, `Zv<file>` — a test pattern and its verification |
| `F…` | files: `Fl<folder>` list, `Fi<file>` info, `Fp<file>` on air, `Fs` stop, `Fm<folder>` create, `Fd<path>` delete, `Fc<file>\|<folder>` copy, `F` — what is on air |
| `Q` | the radio "black box"; `Q0…Q3` — up to which step to revive; `Q7 Q8 Q9 Q6` — perform a step on a healthy radio |
| `Y1`/`Y0`, `Y3`/`Y4`, `Y5…Y8` | experiments with display frame output |

### Receivers from the transmitter
| Command | Action |
|---|---|
| `P` | list of receivers with all readings and settings |
| `I` | to the first one online — "identify yourself" |
| `E0` / `E1` | turn it off / on |
| `V<0–20>` | volume |
| `W0` / `W1` | output «протифаза» (antiphase) / "two channels" |
| `N<name>` | give a name |
| `X` | output check by ear (five states of 6 s each) |
| `i<no.>=<value>[@number]` | settings: 0 clarity 0–3, 1 balance −5…5, 2 volume limit 1–20, 3 view 0–2, 4 LED 0–3, 5 language 0–2; `i9=1` / `i9=0` — headphone test. Example: `i0=2@884A94` |
| `J` / `J1` / `J0` | access requests / open the adding window for 3 min / close it |
| `j<no.>` | allow access to a receiver from the list of requests |
| `R<number>` | remove a receiver from the kit (`R4C3B10`) |
| `M` / `M1` / `M0` | over-the-air update of receivers: status / start / cancel |
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
| `o0` / `o1` | output: PDM / PCM5102 (restart) |
| `w0` / `w1` | pins: antiphase / two channels |
| `n` | the "for hearing" settings: clarity, balance, limit, view, LED, language |
| `n<no.>=<value>` | change one (numbers as in `i`) |
| `n9=1` / `n9=0` | headphone test |
| `n7=1` / `n7=2` / `n7=3` | open the menu / the «Зв'язок» (Link) screen / the main screen |

### Knob hands-free
The pin shorts itself to ground — exactly what the button or the encoder contact does.

| Command | Action |
|---|---|
| `n8=1` | button pin configuration (interrupt type, whether it is enabled, whether it wakes from sleep) and sleep counters |
| `n8=<ms>` (from 50) | press for this many milliseconds: 300 — a press, 1000 — a long one (opens the menu) |
| `n8=3` / `n8=4` | turn by one click in one direction and in the other |
| `n8=5` | press for a second without holding up the main loop |
| `n8=6` | the same after 30 s — a check of waking from sleep (in sleep the port is not listened to) |
| `n8=7` | what is on the audio output pins: share of ones and edges on 10 (SCK), 11 (BCK), 12 (LRCK), 13 (data), 17, 18 (PDM); the XSMT level (pin 14) |
| `n8=8` | true rates since the previous such call: audio from the air (transmitter clock) and the output (own clock), in samples per second and ppm |
| `n8=2` | leave the pin "dirty", as after sleep in old versions (a check of the cleanup at startup) |

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
| `qsweep.py` | comparison of qualities and rates: `qsweep.py ПОРТ СЕКУНД quality:rate[:power] …` |
| `sweep.py`, `hoptest.py` | comparison of channels, a check of the channel move |
| `fliptest.py`, `idletest.py` | smoothness of page flipping and steadiness of the transmitter screen |
| `sendfile.py` | put a file on the transmitter's card through the port |
| `langtest.py` | translation check |
| `linktest/` | a separate firmware for checking the radio link on "bare" boards |
