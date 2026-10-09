# Changelog / Історія версій

## 2.47 — 2026-10-09
- EN: transmitter screen: opening a settings window is cheaper — only the part of the page that stays visible around
  the window is dimmed (the picture is the same pixel for pixel; the full-page dim took 53 ms idle and up to 0.45 s
  while a file was playing). While a file from the card or the test tune plays, the decoder takes the processor away
  from the screen for shorter periods (the menu used to freeze for about a second at a time). The `u` report shows
  how long windows take to draw.
- UK: екран передавача: вікно налаштування відкривається дешевше — притемнюється лише та частина сторінки, яку видно
  довкола вікна (картинка та сама, точка в точку; притемнення всієї сторінки тривало 53 мс у спокої й до 0,45 с, коли
  грав файл). Поки грає файл із картки чи перевірочна мелодія, розбір звуку забирає процесор в екрана на коротший
  час (раніше меню завмирало приблизно на секунду). Звіт `u` показує, скільки триває малювання вікон.

## 2.46 — 2026-10-09
- EN: receiver knob lock set from the transmitter (receiver window → “Settings” → “Knob lock”: none / menu / volume /
  all); the receiver shows “Locked”. The “Auto-update: on / off” switch in the “Update” window.
- UK: блокування ручки приймача з передавача (вікно приймача → «Налаштування» → «Блокування ручки»: немає / меню /
  гучність / усе); приймач показує «Заблоковано». Перемикач «Автооновлення: увімкнено / вимкнено» у вікні «Оновлення».

## 2.45 — 2026-10-09
- EN: equalizers. **Receiver equalizer** set from the transmitter (receiver window → “Settings” → “Equalizer”):
  five bands (125 Hz, 400 Hz, 1 kHz, 2.5 kHz, 6 kHz; −12…+12 dB in 2 dB steps) and a 100 Hz low cut, on by default.
  **Input equalizer** of the transmitter (the “Equalizer” button on the spectrum tile of the “Sound” page): the same
  bands and low cut applied once to the input for all receivers. Port: `i20…i25`, `e…`. Fixed: a restart asked from
  the port or the screen could hang for a second and leave a false “radio stopped” record (the radio watchdog
  interfered with the shutdown).
- UK: еквалайзери. **Еквалайзер приймача** з передавача (вікно приймача → «Налаштування» → «Еквалайзер»): п'ять смуг
  (125 Гц, 400 Гц, 1 кГц, 2,5 кГц, 6 кГц; −12…+12 дБ кроком 2) і зріз низів нижче 100 Гц, з коробки ввімкнений.
  **Еквалайзер входу** передавача (кнопка «Еквалайзер» на плитці спектра сторінки «Звук»): ті самі смуги й зріз —
  один раз для всіх приймачів. Порт: `i20…i25`, `e…`. Виправлено: перезапуск із порту чи з екрана міг зависнути на
  секунду й лишити хибний запис «радіо стало» (сторож радіо втручався у вимкнення).

## 2.44 — 2026-10-08
- EN: receiver: the playback-rate adjustment now uses a windowed-sinc interpolator (24 taps × 128 phases) instead of
  the cubic one, whose error in the treble was modulated 50 times a second with the PCM5102 DAC (a background buzz).
- UK: приймач: підгонка темпу тепер через таблицю sinc (24 відліки × 128 фаз) замість кубічної, похибка якої на
  верхах із ЦАП PCM5102 «тремтіла» 50 разів на секунду (фонове дзижчання).

## 2.43 — 2026-10-08
- EN: “highest” quality in stereo: the packet also carries a spare copy of the channel difference of the previous
  frame (16 kHz ADPCM, 19 bytes; packet 239 bytes), so a frame restored from the copy stays stereo and the
  transmitter no longer folds stereo into mono for half a second after every short jam. Sent only when all receivers
  on the air understand it (2.43+); every 64th packet has the old length so that an older receiver is still heard
  and updated.
- UK: «найвища» в стерео: у пакеті йде ще й запасна копія різниці каналів попереднього кадру (ADPCM 16 кГц, 19 байт;
  пакет 239 байт) — кадр, відновлений із копії, лишається стерео, і передавач більше не зводить стерео в моно на
  пів секунди після кожного короткого затору. Надсилається, лише коли всі приймачі на зв'язку її розуміють (2.43+);
  кожен 64-й пакет — старої довжини, щоб старіший приймач було чути й оновлено.

## 2.42 — 2026-10-08
- EN: automatic update: when a receiver with firmware older than the transmitter's comes on the air, the transmitter
  starts the over-the-air distribution by itself (after ~10 s; at most two attempts per receiver per power-on;
  `M7` / `M6` switch it off / on). Receivers now report their exact version. `n7=3` also prints the effective volume
  in dB.
- UK: автооновлення: коли на зв'язок виходить приймач із прошивкою, старішою за прошивку передавача, передавач сам
  починає роздачу по радіо (приблизно за 10 с; не більше двох спроб на приймач за одне ввімкнення; `M7` / `M6` —
  вимкнути / увімкнути). Приймачі тепер повідомляють точну версію. `n7=3` показує також дійсну гучність у дБ.

## 2.41 — 2026-10-08
- EN: receiver: “Volume limit” is now a ceiling of the scale — the knob (and the transmitter) still go 0…100 %, but
  100 % sounds like the chosen limit (with a 50 % limit, 100 % on the knob is as loud as 50 % used to be).
- UK: приймач: «Межа гучності» тепер стеля шкали — ручка (і передавач) так само ходять 0…100 %, але 100 % звучить
  як вибрана межа (з межею 50 % на 100 % ручки гучність така, як раніше на 50 %).

## 2.40 — 2026-10-08
- EN: receiver: 100 % volume is now the audio as it is, with no gain above full scale — as in an ordinary player.
  Before, “as it is” was 60 % and everything above was amplified by up to +16 dB and squeezed by the limiter: on
  music mastered to full scale the sound “sagged” and rasped on every bass note above 50–60 % (very audible with
  the PCM5102 DAC). The limiter threshold is now full scale, so without “Clarity” it does nothing. A stored volume
  and volume limit are converted once on update (+8 steps). For a quiet source use «Підсилення входу» (Input gain)
  on the transmitter.
- UK: приймач: 100 % гучності — тепер звук як є, без підсилення понад повну шкалу, як у звичайному програвачі.
  Раніше «як є» було 60 %, а все вище підсилювалося до +16 дБ і стискалося обмежувачем: на музиці, записаній на
  всю шкалу, вище 50–60 % звук «завалювався» й хрипів на кожному басі (особливо чути з ЦАП PCM5102). Поріг
  обмежувача тепер — повна шкала, без «Чіткості» він не працює. Збережена гучність і її межа один раз
  перераховуються при оновленні (+8 кроків). Для тихого джерела — «Підсилення входу» на передавачі.

## 2.39 — 2026-10-08
- EN: PCM5102 output is now exactly the classic 3-wire connection: 16-bit I2S, BCK 1.024 MHz, no master clock (pin 10
  is held low and can serve as ground for SCK). 2.38 had a bug — only one channel played (32-bit slots with a 16-bit
  word-clock width) — fixed. New check `n8=9`: the receiver samples its own I2S pins and prints the bits.
  Docs: the module's control pins FLT, DEMP, FMT must not float.
- UK: вихід на PCM5102 тепер точно класичне трипровідне під'єднання: I2S 16 біт, BCK 1,024 МГц, без головного такту
  (вивід 10 тримається в нулі й може бути «землею» для SCK). У 2.38 була помилка — грав один канал — виправлено.
  Нова перевірка `n8=9`: приймач сам знімає свої сигнали I2S і друкує біти. Опис: керівні виводи модуля FLT, DEMP,
  FMT не можна лишати «в повітрі».

## 2.37 — 2026-10-08
- EN: receiver: a look-ahead limiter (1.5 ms) replaces the instant one — no more clipped peaks and “overloaded”
  bass at higher volume or with “Clarity” on (clearly audible with the PCM5102 DAC). `n8=8` also reports how
  much the limiter worked and whether the output ever ran out of data.
- UK: приймач: обмежувач із заглядуванням уперед (1,5 мс) замість миттєвого — без зрізаних піків і «перевантажених»
  низів на більшій гучності чи з увімкненою «Чіткістю» (добре чути з ЦАП PCM5102). `n8=8` показує також, скільки
  працював обмежувач і чи лишався вихід без даних.

## 2.36 — 2026-10-08
- EN: PCM5102: the receiver drives the DAC's XSMT (“sound on”) from pin 14, so modules with open solder jumpers
  work without soldering; the DAC is muted before the clocks stop (no click on sleep). Wider margin of the playback
  rate adjustment (the transmitter's audio clock runs +0.16 % fast; the I2S output is exact). `n8=8` measures the
  true rates.
- UK: PCM5102: приймач сам подає на XSMT ЦАП («звук увімкнено») рівень із виводу 14 — модулі з незапаяними
  перемичками працюють без паяння; перед зупинкою тактів ЦАП приглушується (без клацання). Більший запас
  підстроювання темпу (звук передавача йде на 0,16 % швидше номіналу; вихід I2S — точний). `n8=8` міряє справжні
  частоти.

## 2.35 — 2026-10-08
- EN: receiver with an external PCM5102 DAC: the master clock (SCK, 8.192 MHz) is now output on pin 10; the
  “output alive” status and the `n8=7` pin check work for the I2S output too.
- UK: приймач із зовнішнім ЦАП PCM5102: головний такт (SCK, 8,192 МГц) тепер виходить на вивід 10; стан «вихід
  живий» і перевірка виводів `n8=7` працюють і для виходу I2S.

## 2.34 — 2026-10-07
**EN**
- Receiver menu: main-screen views (spectrum / needle meters / large volume), LED brightness, language, speech
  clarity, balance, volume limit, headphone test, “Link” screen with a one-minute graph.
- The same receiver settings are available from the transmitter (receiver window → “Налаштування / Settings”).
- Fixed: a knob press could hang and reboot a receiver after “sleep → over-the-air update” (a level interrupt left
  on the button pin across a software restart). Present since 2.19.
- Fixed: receivers often crashed at boot after a software restart (IPC task stack overflow in `attachInterrupt`).
- Settings written by a newer firmware are now readable after a downgrade.
- Hands-free knob test commands (`n8=…`).

**UK**
- Меню приймача: вигляд головного екрана (спектр / стрілки / гучність), світлодіод, мова, чіткість мови, баланс,
  межа гучності, перевірка навушників, екран «Зв'язок».
- Ті самі налаштування — з передавача (вікно приймача → «Налаштування»).
- Виправлено зависання й перезапуск приймача від натискання ручки після «сон → оновлення по радіо» (було з 2.19).
- Виправлено падіння приймача при запуску після програмного перезапуску.
- Налаштування, записані новішою прошивкою, читаються після повернення на старішу.
- Команди перевірки ручки без рук (`n8=…`).

## 2.29 – 2.31 — 2026-10-07
- EN: receiver screens rewritten: 60 fps, transitions, notifications, new tower / waves / “no signal” pictograms.
- UK: екрани приймача переписано: 60 кадрів/с, переходи, сповіщення, нові вежа, хвилі та знак «немає сигналу».
- Known issue / відома вада: knob hang after OTA (fixed in 2.34) / зависання від ручки після оновлення.

## 2.28 — 2026-10-07
- EN: transmitter screen no longer tears when switching tabs. UK: екран передавача не смикається при перегортанні.

## 2.19 – 2.27 — 2026-10-07
- EN: over-the-air update of receivers; update of the whole kit from a file on the memory card; recovery kit;
  receiver names up to 32 letters; smooth LED breathing; keyboard fixes.
- UK: оновлення приймачів по радіо; оновлення всього набору файлом із картки; набір відновлення; імена до 32 літер.

## 2.18 — 2026-10-07
- EN: radio “black box” and self-revival of the transmitter radio. UK: «чорна скринька» та самооживлення радіо.

## 2.0 – 2.17 — 2026-10-07
- EN: memory card (recording, file manager, files on air), own frame output for the RGB panel, automatic channel
  hop, knob-only receiver UI. UK: картка пам'яті, файловий менеджер, власне виведення кадру, відхід із каналу.

## 1.8 — 2026-10-06
- EN: protocol 4 — hardware AES-128 encryption and signing, pairing with a four-digit code, key rotation.
- UK: протокол 4 — шифрування та підпис апаратним AES-128, додавання приймача з кодом, зміна ключа.

## 0.1 – 1.7 — 2026-10-05…06
- EN: first audio on air, transmitter touch UI, quality levels, stereo, feedback channel, test sounds, sleep mode.
- UK: перший звук в ефірі, сенсорне меню, якості звуку, стерео, зворотний зв'язок, перевірочні звуки, сон.
