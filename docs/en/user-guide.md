# User guide

[← back to main page](../../README.md) · [Українська](../uk/user-guide.md)

## Transmitter

After power-on — the splash screen (about 4 s; sound goes on air from the very first second), then the home page.
At the bottom there are six tabs, at the top — the page title, the power-off button and the status "pill"
(«В ЕФІРІ» (ON AIR), «ТИХО НА ВХОДІ» (INPUT SILENT), «ПЕРЕВАНТАЖЕННЯ» (OVERLOAD), «ТИША» (MUTE)).

![Home](../img/tx-01-golovna.png) ![Sound](../img/tx-03-zvuk.png)

### «Головна» (Home)
- **«З пульта» (Mixer) / «Перевірка» (Test) / «Тиша» (Mute)** — what goes on air: the sound from the input, the
  test sound, or silence (packets are sent, there is no sound in them — the receivers do not fall asleep).
- **«Рівень входу» (Input level)** — in dB, with a peak mark and a hint («Тихо» (Quiet), «Гучно, але ще чисто»
  (Loud but still clean), «Перевантаження — зменшіть рівень на пульті» (Overload — reduce the level on the mixer)).
- Tiles **«Канал» (Channel)**, **«Набір» (Kit)**, **«Приймачі» (Receivers)** (how many are online out of all),
  **«Запис на картку» (Recording to card)**.

### «Звук» (Sound)
The input spectrum; **«Підсилення входу» (Input gain)** (−12…+24 dB); **«Вхід» (Input)** (built-in ADC or PCM1808);
**«Який звук» (Which sound)** in test mode (tone, voice, voice and music, music, music with an announcement every
N seconds, left/right channel check) with a choice of tune and volumes; **«Якість звуку» (Sound quality)**;
**«Ефір» (Air)** mono/stereo.

**«Еквалайзер» (Equalizer)** (a button on the spectrum tile, since 2.45) — the input equalizer: it shapes the sound
from the mixer once for all receivers. Five bands (bass 125 Hz, 400 Hz, 1 kHz, 2.5 kHz, treble 6 kHz), each
−12…+12 dB in 2 dB steps, and a low cut below 100 Hz. It acts on the input (built-in or PCM1808) and does not touch
the test sound or files from the card. The spectrum on this page shows the sound after the equalizer.

![Input equalizer](../img/tx-03b-ekvalaizer-vkhodu.png)

**Input noise** (since 2.71; in the equalizer window — the **«Шумозаглушення» (Noise reduction)** button, back —
**«Еквалайзер» (Equalizer)**) removes steady noise (hiss, hum) from the mixer sound before it goes on air:
**«Шумозаглушення» (Noise reduction)** off / light / medium / strong; **«Чутливість до шуму» (Noise sensitivity)** from
very low to very high (higher removes noise more fully, but quiet word endings get duller); **«Зразок шуму» (Noise
sample)** — “auto, in pauses” or “remember now” (for a few seconds only silence with noise must come from the mixer).
It acts on the input sound; the test sound and card files are not touched.

| Quality | On air | Added delay | Lowest radio rate | When to choose |
|---|---|---|---|---|
| «найвища» (highest) | 32 kHz uncompressed; two frames per packet, the previous packet repeated uncompressed (since 2.77); in stereo since 2.86 the channel difference is uncompressed too — the receiver plays the very L and R of the input | 2 ms | 5.5 Mbit/s | hall up to ~15 m, clean air, music |
| «стандартна» (standard) | 32 kHz ADPCM; at 1 Mbit/s — the new “eight bands” codec (since 2.79) | 4 ms | 1 Mbit/s | the usual choice |
| «мова» (speech) | 16 kHz ADPCM (up to 7 kHz) | 8 ms | 1 Mbit/s | sermon, busy air |
| «дальня» (far) | 16 kHz ADPCM | 12 ms | 0.5 Mbit/s | the longest range, mono only |

If the selected quality "does not fit" into the current radio rate, the nearest lower one goes on air — the menu
shows this («мова — за швидкістю» (speech — due to rate)).

**Far mode (since 2.79).** When the transmitter drops to 1 Mbit/s for a weak receiver (see «Ефір»), the «стандартна»
(standard) quality uses a new codec: eight 2 kHz bands, a 4 ms frame, the packet carries the frame and three spare
copies — three lost packets in a row cause no dropout. On a sermon recording it is cleaner than the former «мова»
(speech) (signal-to-noise “as heard through a receiver” 41.6 dB against 30.5) and even than the ADPCM «стандартна»
(39.2 dB). Mono only: stereo goes as mono on this step.

### «Ефір» (Air)

![Air](../img/tx-07-efir.png)

- **«Зайнятість каналів» (Channel occupancy)** and the button **«Знайти вільний канал» (Find a free channel)** (the
  sound is interrupted for two seconds). Since 2.82 the measurement accounts for strong neighbouring networks: an
  access point nearby disturbs with its energy even several channels away (three channels away it still does, nine
  away it does not), so such channels show higher on the chart.
- If on the current channel packets often wait 15 ms or more for the air, a yellow line appears at the top:
  **«канал N: пакети чекають ефіру до X мс»** (channel N: packets wait for air up to X ms) — the receivers' sound is
  delayed by the same amount. The cure is a channel farther from strong networks.
- **Channel** 1–13; **Kit** — the number of your kit (receivers hear only their own).
- **«Канал при заторі: сам» (On congestion: auto)** — when packets wait long for the air, the transmitter moves to a
  freer channel by itself without a pause (the move is announced to the receivers in advance and they follow). Since
  2.83 — also when packets merely wait 15–35 ms often (the sound is delayed): this is a **trial** — after a minute on
  the new channel the transmitter compares and goes back if the waits are no smaller there or the receivers lose
  sound more often. Since 2.88 a trial also starts when **a receiver with a good signal** (−62 dBm or stronger) loses
  1.5 % of packets a minute or more: that is interference near it (clicks are heard) which the transmitter neither
  hears nor sees in its channel scan. The transmitter then tries the best of the other channels by its scan and stays
  there if the losses dropped by at least a third. A message on the screen tells about the move and its reason. A
  channel left because of congestion is not chosen for ten minutes; a failed trial channel — for half an hour.
- **«Швидкість радіо» (Radio rate)**. «Сама» (auto): 5.5 Mbit/s, and for a weak receiver (−75 dBm or weaker, or losing
  sound) — 1 Mbit/s, which reaches farther (since 2.73). Back up when everybody hears well: after 30 s if the weakest
  is −66 dBm or better, after 2 min when closer to the limit (since 2.83). A rate chosen by hand is the upper step:
  the transmitter steps down from it the same way; the row then shows «5.5 (зараз 1)» (5.5, now 1) and a message
  names the receiver that hears weakly.
- **«Слабкий сигнал» (Weak signal)** (since 2.83): *lowering* / *not lowering*. “Not lowering” — the rate and quality
  always stay as chosen, but a distant receiver may lose sound. If you choose a rate or quality while the transmitter
  is held at 1 Mbit/s by a weak receiver, it steps up at once when the receivers already hear, otherwise it says who
  hears weakly and where to turn this off.
- **«Потужність» (Power)** 2–20 dBm.

### «Приймачі» (Receivers)

![Receivers](../img/tx-17-pryimachi.png) ![Receiver window](../img/tx-18c-pryimach-vikno.png)

A receiver row: name, signal, volume; the switch on the right turns the receiver off or on remotely (a receiver
that is turned off falls asleep and is woken only by the "turn on" command from the transmitter: since version 2.65
it cannot be turned on with its own knob — not after a power cycle and not while the transmitter is off either; the
transmitter itself brings the receiver back to the state it has set).
Touching the row opens the receiver window: signal, volume, loss, buffer, output, uptime and version; the buttons
**«Показати себе» (Identify)** (the receiver blinks white, shows «ЦЕ Я» (IT'S ME) on its screen, beeps in the
headphones), **«Перейменувати» (Rename)** (up to 32 letters), **«Тихіше / Гучніше» (Quieter / Louder)**,
**«Налаштування» (Settings)**, **«Вимкнути» (Turn off)**, **«Перезапустити» (Restart)** (receivers with 2.84+ — a
restart over the radio when the sound is gone and the reset button cannot be reached; the sound returns in 1–2 s),
**«Видалити з набору» (Remove from kit)**.

![Receiver settings](../img/tx-18d-pryimach-nalashtuvannya.png) ![Receiver equalizer](../img/tx-18g-ekvalaizer.png)

**Settings** of a receiver from the transmitter: audio output, speech clarity, balance, volume limit, **volume
boost** («Підсилення гучності», 0…+24 dB; in this list since 2.63 — it used to be on the equalizer sheet), screen
view, LED, knob lock, the button **«Перевірка навушників» (Headphone test)**. The **«Еквалайзер» (Equalizer)**
button to the right of the name (receivers with 2.45+) opens the second sheet of the same window — the equalizer of
this receiver: the same five bands, a low cut below 100 Hz (on by default in a receiver) and the receiver's
interface language; the same button («Налаштування», Settings) goes back. This is how the sound is fitted to one
person's hearing, while the input equalizer works for everybody at once.

The third sheet is **«Обробка звуку» (Sound processing)** (receivers with 2.68+; the same button cycles on):
**«Шумозаглушення» (Noise reduction)** off / light / medium / strong; **«Чутливість до шуму» (Noise sensitivity)**;
**«Зразок шуму» (Noise sample)** — “auto, in pauses” or “remember now”; **«Стискання» (Compression)** off / light /
medium / strong — quiet sound is raised, loud sound is held down (together with the boost); **«У паузах» (In pauses)**
off / no boost / another −6 dB / another −12 dB — between words the receiver removes the boost so as not to amplify
noise; **«Поріг пауз» (Pause threshold)** +3…+15 dB above the noise; **«Закриття пауз» (Pause closing)** fast /
medium / slow. The same items are in the receiver menu.

The last row is **«Блокування ручки» (Knob lock)** (receivers with 2.46+): *none* / *menu* (a long press does not open
the menu) / *volume* (turning and mute-by-press do nothing) / *all*. The receiver shows «Заблоковано» (Locked) when
someone tries. Everything can still be changed from the transmitter.

![Knob lock](../img/tx-18d2-blokuvannya-ruchky.png)

#### Adding a receiver

![Add](../img/tx-17b-dodaty-zapyty.png)

1. Switch the new receiver on next to the transmitter. Its screen shows «НЕ ПІДКЛЮЧЕНО» (NOT CONNECTED), then
   «прошу доступ» (asking for access) and a four-digit **«КОД» (CODE)**.
2. On the transmitter: «Приймачі» (Receivers) → **«+ Додати» (+ Add)**. The window stays open for three minutes; a
   request with the same code appears in it.
3. Compare the code and press **«Дозволити» (Allow)**. The receiver gets the kit key and starts playing.

A receiver that already has a key does not listen to other transmitters. It can be moved to another kit only on the
receiver itself: menu → «Забути набір» (Forget kit).

#### Removing a receiver
"Remove from kit" — the receiver loses access, the kit key changes and is distributed to the remaining receivers by
itself (to those that were switched off — when they are switched on). A lost receiver will hear nothing after this.

### Firmware update

![Update](../img/tx-17e-onovlennya-nadsylayu.png) ![Warning](../img/tx-17i0-onovlennya-poperedzhennya.png)

**Via the memory card (no cable needed):**
1. Put the file `hearlink-<version>.bin` from the [releases](../../../releases/latest) on the card (FAT32) into the
   **`UPDATE`** folder in the card root: `UPDATE/hearlink-2.34.bin`. The transmitter creates the folder by itself.
   The file name can be anything, only the `.bin` extension is required: the transmitter reads the version from
   inside the file and takes the newest of several files. Do not put the cable-flashing parts here
   (`bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `assets.bin`).
2. Insert the card — the transmitter finds the file and opens the «Оновлення» (Update) window.
3. **«Оновити» (Update)**: first the receivers over the radio (about 15 s of sending, the sound on air is stopped
   for this time; writing in the receivers takes another 15–30 s), then the transmitter writes itself (about a
   minute, the screen blinks — it shows in large letters «ОНОВЛЕННЯ ПРОШИВКИ — НЕ ВИМИКАЙТЕ ЖИВЛЕННЯ!!!»
   (UPDATING FIRMWARE — DO NOT SWITCH POWER OFF!!!)) and restarts.

**Automatically** (since 2.42): when a receiver with firmware older than the transmitter's comes on the air (it was
switched off or asleep during the update, or it is a new one), the transmitter starts the distribution by itself
after about 10 s. The sound on air stops for ~20 s for everyone; at most two attempts per receiver per power-on of
the transmitter. Switch off / on: the **«Автооновлення: увімкнено / вимкнено» (Auto-update: on / off)** button in the
“Update” window (since 2.46) or the port commands `M7` / `M6`.
Since 2.56 the transmitter screen says so by itself: the “Update” window with the progress opens without a touch and
closes 15 s after the end (the result stays as a message); if another window is open at that moment, a short message
is shown instead.

**Over the cable from a computer** (the `build.sh` script): while the computer writes the firmware, the transmitter's
processor sits in its loader and cannot draw — the screen is black for about a minute. So before the writing starts
(since 2.50) the screen shows the warning “FIRMWARE UPDATE — THE SCREEN GOES DARK FOR A MINUTE — DO NOT SWITCH THE
POWER OFF!!!”, and after the first start with the new version — “Transmitter updated: version …”. If you need the
progress on the screen, update through the card.

![Cable update](../img/tx-17k-onovlennya-kabelem.png)

**Receivers only** (the transmitter already has the new version): "Receivers" → "Update" → **«Почати» (Start)**.
**«Пробне» (Trial)** — shows what it looks like: the receivers receive and verify everything but write nothing.

A sleeping receiver will not be updated — update it another time. An interrupted update breaks nothing: until the
new firmware has been written and verified in full, the receiver starts from the old one.

### Memory card

![Files](../img/tx-fm-01-spysok.png) ![File on air](../img/tx-fm-04-fayl-v-efiri.png)

- **Recording the broadcast** — the "Recording to card" tile on the home page: WAV 32 kHz mono 16 bit, about 230 MB
  per hour, folder `ZAPYS`. The file header is updated every five seconds — on a sudden power-off no more than that
  is lost.
- **«Файли» (Files)** — a file manager: folders, viewing text files, copying, deleting, new folder, formatting.
- **File on air** — touch an MP3 or WAV file → «В ефір» (On air): the file plays instead of the input (background
  music before the service).
- The `LOG` folder — the log of the radio "black box"; the `UPDATE` folder — updates.

### «Налаштування» (Settings)
Screen brightness, screen dimming (never / 1 / 5 / 15 min), the sound source name (the caption on the button instead
of «З пульта» (Mixer)), language (Українська / English — the receivers pick it up by themselves), «Про пристрій»
(About), «Налагодження ефіром» (Debug over air), **«Перезапустити» (Restart)**, **«Скинути все» (Reset all)**.

The power-off button in the header: the air goes silent, the screen goes dark; a touch on the screen turns it on.
After 10 s without a signal the receivers fall asleep by themselves.

## Receiver

![Receiver screens](../img/rx-menu.png)

### Knob
| Action | On the main screen | In the menu |
|---|---|---|
| turn | volume 0…100 % (step 5 %) | select an item / change a value |
| press | mute / sound | enter an item / confirm |
| long press (0.7 s) | menu | back |
| any movement while the screen is off | turn the screen on | — |
| anything without a signal (searching, NO SIGNAL, asleep) or when turned off from the transmitter | nothing: the knob does not act at all — no volume, no mute, no menu (since 3.14; the receiver wakes by itself when it hears the transmitter) | — |

### What it shows

![Search and "no signal"](../img/rx-search.png)

| Screen | When |
|---|---|
| splash screen (4 s) | power-on, wake-up |
| «ПОШУК ПЕРЕДАВАЧА» (SEARCHING) — a tower with waves, a channel scale | searches for 10 s, then falls asleep |
| «ЗАПУСК» (STARTING) — a 4 s bar | the transmitter has been found; the sound comes in after 1.5 s, smoothly |
| main screen | operation |
| «НЕМАЄ СИГНАЛУ» (NO SIGNAL) — a tower without waves and a crossed-out sign | the signal is gone; sleep after 10 s |
| «ТИША» (MUTE) | the knob was pressed |
| «НЕ ПІДКЛЮЧЕНО» (NOT CONNECTED) + code | the receiver is not in a kit yet |
| «ОНОВЛЕННЯ» (UPDATING) with progress | an over-the-air update is in progress |
| «ЦЕ Я» (IT'S ME) | "Identify" was pressed on the transmitter |

At the top of the main screen — the signal level (four bars), the name, the volume. Notifications slide in from the
top: «Сигнал відновлено» (Signal is back), «Слабкий сигнал» (Weak signal), «Гучно на вході!» (Input too loud!),
«Тиша в ефірі» (Silence on air), «Межа гучності» (Volume limit), «Чіткість: середня» (Clarity: medium) (a setting
was changed from the transmitter) and so on.

### Colour display (since 3.03)

Instead of the OLED a receiver can have a colour IPS 1.9″ display (how to connect and select it — see
[Hardware](hardware.md#colour-ips-19-display-on-the-receiver-firmware-303-and-later)). The screens are the same as
on the OLED, only larger and in colour.

![Colour receiver display](../img/rx-color-screens.png)

Main screen ("spectrum" view; layout by the owner since 3.08):

- **top left — the volume** in large digits with twenty steps; while the knob is turned, the number "flips" to the
  new value and the card frame lights up;
- **bottom left — the spectrum analyser:** 10 bars with a white peak mark above each, held for 0.45 s and then
  falling smoothly; when the transmitter mutes the air — «ТИША В ЕФІРІ» (silence on air);
- **right — «Зв'язок»** (link: bars and a word — none / weak / fair / good / excellent), «СТЕРЕО» (stereo) or «МОНО»
  (mono) and «ШУМОДАВ» (noise reduction, when it is on) tags, and below the line the receiver name in one or two lines.

The other views ("needles", "volume"), the menu, About, Link, Mute, searching, NO SIGNAL, updating and sleep are the
same as on the OLED, in colour. Only what changed is sent to the display, so everything moves smoothly (60 frames per
second).

### Menu
| Item | Values |
|---|---|
| **«Вигляд» (View)** | spectrum / needles / volume (a large number) |
| **«Світлодіод» (LED)** | off / dim / normal / bright |
| **«Мова» (Language)** | as transmitter / українська / English |
| **«Чіткість» (Clarity)** | off / light (+4 dB) / medium (+8) / strong (+12) — a boost above 2 kHz, where the consonants are |
| **«Баланс» (Balance)** | left +1…+5 / center / right +1…+5 (3 dB each); only with the «2 канали» (2 channels) output |
| **«Підсилення» (Boost)** | off / +2…+24 dB — digital gain above “audio as it is”, like the trim on a mixing console: the sound is amplified by exactly what is set (since 2.63; in 2.48–2.56 it acted only within the headroom below full scale and was barely noticeable on loud music). If the amplified sound does not fit the DAC's scale — a loud source at a high volume — the limiter holds the peaks: no crackle, but music becomes denser. Nothing digital can make it louder than full scale: headroom above it comes only from the headphone amplifier |
| **«Межа гучн.» (Volume limit)** | 20…100 % — the loudness ceiling: the knob scale stays 0…100 %, but 100 % sounds like this limit (since 2.41) |
| **«Стискання» (Compression)** | off / light / medium / strong — quiet sound is raised, loud sound held down (since 2.68) |
| **«У паузах» (In pauses)** | off / no boost / another −6 dB / another −12 dB — the boost is removed in speech pauses (since 2.68) |
| **«Поріг пауз» (Pause threshold)** | +3…+15 dB above the noise — what counts as a pause (since 2.72) |
| **«Закриття» (Closing)** | fast / medium / slow — how quickly a pause “closes” (since 2.72) |
| **«Шумозагл.» (Noise red.)** | off / light / medium / strong — spectral noise reduction (since 2.68) |
| **«Чутл. шуму» (Noise sens.)** | 1…5 of 5 — higher removes noise more fully, quiet word endings get duller (since 2.72) |
| **«Зразок шуму» (Noise sample)** | auto (learns in pauses) / taken (remember now: a few seconds of silence with noise) (since 2.72) |
| **«Навушники → тест» (Headphones → test)** | a tone alternately in the left and the right ear |
| **«Зв'язок» (Link)** | signal in dBm, loss, buffer, a one-minute graph |
| «Набір» (Kit) | kit number (display only) |
| «Канал» (Channel) | auto or 1–13 |
| «Запас» (Buffer) | «сам» (auto) or 4–40 ms — a buffer against uneven air (it is also the delay) |
| «Вихід звуку» (Audio output) | PDM / PCM5102 / CS4344 (restart; CS4344 since 2.85) |
| «Виводи» (Pins) | 2 channels / «протифаза» (antiphase) |
| «Екран» (Screen) | turn off (any knob movement turns it on) |
| «Яскравість» (Brightness) | 1…5 — colour display backlight or OLED contrast (since 3.03) |
| «Забути набір» (Forget kit) | erase the key — the receiver will ask for access again |
| «Перезапуск» (Restart) | “ТАК” (YES) and a press — restarts the receiver (since 2.84; when the sound is gone and the reset button cannot be reached) |
| «Про пристрій» (About) | version, channel, kit, number, buffer, signal |

### LED
| Color | State |
|---|---|
| not lit | asleep, turned off from the transmitter |
| blinks blue | startup, search, signal gone |
| slowly "breathes" green | working, silence on air |
| "breathes" yellow | working, sound on air |
| blinks red for 6 s | transmitter not found, falling asleep |
| "breathes" purple | firmware update |
| blinks white rapidly | "Identify" (even when the LED is turned off) |

### Sleep
10 s without a signal → 6 s of a message → sleep: the screen, the audio output and the radio are off, the processor
sleeps. Once every half second the receiver turns the radio on for 0.07 s and listens to its channel (every fourth
cycle — the remaining channels). Once it hears its transmitter, it wakes up by itself: splash screen, "STARTING",
sound.

### Sound self-recovery (since 2.84)
If the sound is gone while the receiver is connected and there is sound on air (the sound processing got stuck), the
receiver resets its processing within 1.5 s, and if that does not help it restarts without the splash screen (no more
than once in 10 minutes). The transmitter reports every such case to the serial port and to the log on the card. By
hand — «Перезапуск» (Restart) in the receiver menu or «Перезапустити» (Restart) in its window on the transmitter.
