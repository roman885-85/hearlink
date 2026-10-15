# Changelog / Історія версій

## 3.02 — 2026-10-15 (includes 2.89–3.01, which were not released / містить 2.89–3.01, які не випускались)
- EN: **the receiver's menu and spectrum analyzer are smooth again, also with noise suppression on.** The owner: “with
  the noise suppressor on the menu and the analyzer on the main page are very sluggish”. Measured: the receiver's sound
  chain had grown heavier since 2.86 and with noise suppression in stereo took 830–910 µs of every millisecond — 83–93 %
  of the core it shared with the display, which got 17–30 frames/s instead of 60. Now the display runs on the other
  core (radio core, 40–45 % free) and sends a frame above the sound's priority; the chain itself is faster without
  changing the sound (host checks `tools/host/rsdot.cpp`, `tools/host/nsfx.cpp` give the same figures): the
  pace-conversion convolution 270–290 → 125 µs per millisecond, block preparation 130–150 → 80 (the volume power no
  longer computed twice per block), compression 100–125 → 75–85 (own exact logarithm and power instead of the library
  ones). On the
  devices: chain 630 µs per millisecond with noise suppression 2 and compression 3 (was 830–910), 485 without noise
  suppression (was 710–755); the display 62–63 frames/s on the main screen and in the menu; the output ran dry 0 times in
  3 minutes on both receivers. The version is 3.02: version numbers are compared as “major.minor” numbers, so 3.02 is
  newer than 2.99 for radio updates and update files on the card.
- UK: **меню й аналізатор спектра приймача знову плавні, і з шумозаглушенням.** Власник: «при включенном шумодаве
  очень тормозит меню и анализатор на главной странице». Замір: тракт звуку приймача з 2.86 потяжчав і з
  шумозаглушенням у стерео брав 830–910 мкс із кожної мілісекунди — 83–93 % ядра, яке він ділив з екраном, а екран
  отримував 17–30 кадрів/с замість 60. Тепер екран працює на іншому ядрі (ядрі радіо, там вільно 40–45 %) і віддає кадр
  із пріоритетом вище звуку; сам тракт швидший без зміни звуку (перевірки на комп'ютері `tools/host/rsdot.cpp`,
  `tools/host/nsfx.cpp` дають ті самі цифри): згортка підгонки темпу 270–290 → 125 мкс на мілісекунду, підготовка
  блока 130–150 → 80 (степінь для гучності більше не рахується двічі на блок), стискання 100–125 → 75–85 (власні точні
  логарифм і степінь замість бібліотечних). На приладах: тракт 630 мкс на мілісекунду з шумозаглушенням 2 і
  стисканням 3 (було 830–910), 485 без шумозаглушення (було 710–755); екран 62–63 кадри/с на головній і в меню; вихід
  без даних — 0 разів за 3 хвилини на обох приймачах. Номер — 3.02: версії порівнюються як числа «старша.молодша», тож
  для оновлення по радіо й файлів на картці 3.02 новіша за 2.99.
- EN: **noise suppression in stereo: no clicks, no lost channel.** The channel difference used to be only delayed and
  attenuated by one overall number while the mid was attenuated per band — L and R stopped being left and right (quiet
  music panned to L leaked into R at −16…−20 dB). Since 2.89 the difference goes through the same window and transform
  with the same band gains: leakage −27…−31 dB, noise in R next to loud music in L −56…−60 dB instead of −52 (host test
  `tools/host/nsstereo.cpp`). The DAC output queue is 4 ms instead of 2 (2.92): with suppression on the output ran dry
  once in 30–60 s (a 1 ms click), now not once in 3 minutes on either receiver.
- EN: **radio updates in under a minute, also with noise suppression on.** During an update the sound chain kept
  processing silence and slowed both receiving and writing 6–12 times (2.88 → 2.89 took over 10 minutes); since 2.90
  plain silence goes to the output during an update — two receivers update in 45 s. The transmitter waits while
  receivers write (up to 10 minutes) instead of calling them “vanished” after 2 (2.91).
- UK: **шумозаглушення в стерео: без клацань і без «зниклого» каналу.** Різниця каналів лише чекала й ослаблялася
  одним загальним числом, а середина — по смугах: Л і П переставали бути лівим і правим (тиха музика, зведена в Л,
  протікала в П на −16…−20 дБ). З 2.89 різниця йде тим самим вікном і перетворенням із тими самими посиленнями смуг:
  протікання −27…−31 дБ, шум у П поруч із гучною музикою в Л — −56…−60 дБ замість −52 (перевірка на комп'ютері —
  `tools/host/nsstereo.cpp`). Черга виходу на ЦАП — 4 мс замість 2 (2.92): з шумозаглушенням вихід лишався без даних
  раз на 30–60 с (клацання 1 мс), тепер — жодного разу за 3 хвилини на обох приймачах.
- UK: **оновлення по радіо — менше хвилини, і з шумозаглушенням.** Під час оновлення звуковий тракт рахував тишу й
  уповільнював і прийом, і запис у 6–12 разів (2.88 → 2.89 — понад 10 хвилин); з 2.90 на час оновлення на вихід іде
  готова тиша — два приймачі оновлюються за 45 с. Передавач чекає, поки приймачі пишуть (до 10 хвилин), а не називає
  їх «зниклими» через 2 (2.91).

## 2.88 — 2026-10-14 (includes 2.86–2.87, which were not released / містить 2.86–2.87, які не випускались)
- EN: **light clicks “like a scratched CD” — the channel is now chosen by the receivers' packet losses too.** After the
  exact copies (2.87) only two losses in a row and gaps could still be heard. Measured: on channel 1 the near receiver
  (−47 dBm) lost 1–6 % of packets with two losses in a row every 3–50 s, on a trial channel 5 — not a single packet in a
  minute; the transmitter hears none of this interference (it is at the receiver's place), and its channel scan rated
  channel 1 the best. The soft channel move («Канал при заторі: сам») now also starts a trial when a receiver with a good
  signal (−62 dBm or stronger) loses 1.5 % of packets a minute or more, tries the best of the other channels and stays if
  the losses dropped by at least a third; the screen says “a receiver lost N % of packets (interference)”. On the devices
  it moved 1 → 2 → 5 by itself and stayed: over the next 5 minutes the near receiver lost 0.0 % of packets, the far one
  1.6–2.4 % (all single losses, restored exactly), lost sound and buffer underruns 0 ms for both.
- UK: **легкі клацання «як у подряпаного CD» — канал тепер обирається й за втратами пакетів у приймачів.** Після точних
  копій (2.87) чутно лишалися тільки дві втрати поспіль і провали. Вимір: на каналі 1 ближній приймач (−47 дБм) губив
  1–6 % пакетів, дві втрати поспіль — раз на 3–50 с, на пробному каналі 5 — жодного пакета за хвилину; передавач цих
  завад не чує (вони біля приймача), а за своїм заміром вважав канал 1 найкращим. М'який перехід («Канал при заторі:
  сам») тепер починає пробу й тоді, коли приймач із добрим сигналом (−62 дБм і сильніше) губить за хвилину 1,5 % пакетів
  і більше, пробує найкращий за заміром з інших каналів і лишається, якщо втрат стало хоча б на третину менше; на екрані —
  «приймач губив N % пакетів (завади)». На приладах сам перейшов 1 → 2 → 5 і лишився: наступні 5 хвилин ближній приймач
  губив 0,0 % пакетів, дальній 1,6–2,4 % (усі одиночні, відновлені точно), пропалого звуку й провалів запасу — 0 мс в обох.

- EN: **no more “graininess” in the sound.** The owner heard an unpleasant graininess with an ideal signal, on both
  DACs, on the test sound and on a good recording, with all noise suppression off. The receiver chain was reproduced on
  a computer (`tools/host/rxchain.cpp`) and the error of every stage measured per frequency band; four causes found:
  1. **the stereo channel difference went as 4-bit ADPCM.** Its noise is flat in frequency while the top of music is
     quiet: in the 2–16 kHz bands it was only 27…2 dB below the music, at 15–16 kHz louder than it, and the owner's
     equalisers (+6…+12 dB on the top) raised it further. Now «найвища» in stereo carries the difference uncompressed,
     like the mid: the receiver gets the very L and R that left the transmitter. In 2.86 the spare copies of the
     difference were still 4-bit — and on the now clean sound every lost packet came out as a short rustle (owner:
     “the sound became perfectly clean, but periodic rustles are heard, as with packet loss”; the near receiver lost
     ~6 packets a second on channel 1). Since 2.87 the copies of the previous packet carry the difference uncompressed
     too: a single lost packet is restored bit for bit in stereo. The two oldest copies (needed only when two packets
     in a row are lost) carry no difference — those 4 ms play in mono, the difference fading out and back in within
     1 ms (no step, no click). The double packet grows from 750 to 1116 bytes (47 % of the air at 5.5 Mbit/s instead of
     34 %). Used when every receiver on the list has 2.87+; `Qr0` brings back the 4-bit difference for comparison.
     Measured on the devices, 30 s windows on channel 1 (losses there vary a lot from window to window): near receiver
     ~10 frames a second from copies, 97 % of them exact; two losses in a row about once every 3–4 s; lost sound 0 ms;
  2. **the receiver output was 16 bits with truncation after the digital volume** — at volume 2 of 20 (−36 dB) only
     5–10 bits of the sound were left, and the distortion followed the sound. External DACs (PCM5102, CS4344) now get
     24 bits in a 32-bit word (PCM5102: 64 bit clocks per frame); the one-bit output keeps 16 bits, rounded;
  3. **the limiter** with a large boost and no compression (+24 dB at full volume) could not keep up with its 0.3 ms
     attack: 1426 samples in 87 s went over full scale and were clipped hard. The window is now 3 ms and the gain goes
     down as the average over the window — provably never over full scale; intermodulation −48 → −59 dB, fast gain
     wobble (0.3–1 kHz) −27 → −40 dB. +1.5 ms of delay;
  4. **the 24-tap pace conversion** let image frequencies through at 14–16 kHz (−26 dB at 14 kHz, −11 dB at 15 kHz).
     Now 48 taps with the cutoff at 15 kHz: no worse than −77 dB up to 15 kHz; the top above 14 kHz rolls off
     (−5.6 dB at 15 kHz).
  The receiver's `?` shows the output chain; the line of `P`/stat says “різниця без стиснення”.
- EN: `Qp<bits>` (radio protocol experiment) works again on the display module — the `Qp<setting>=<value>` parser no
  longer swallows it. Debug-over-radio buffers shrank from 1000 to 283 bytes and the receiver-only limiter buffers moved
  to the heap: despite the larger packet the transmitter has 0.9 KB more free internal memory than with 2.85 (static
  data 148 451 bytes instead of 149 403).
- EN: known issues: with a large boost and compression off the limiter still holds 20–25 dB most of the time — that
  is the cost of loudness (switch on compression 1–3); a receiver turned off from the transmitter can stay off if the
  transmitter loses power within 3–11 s after it was turned on again (the change is saved in a pause of the sound).
- UK: **більше немає «зернистості» звуку.** Власник почув неприємну зернистість на ідеальному сигналі, на обох ЦАП,
  на перевірочному звуці й на гарному записі, з вимкненим шумозаглушенням. Тракт приймача повторено на комп'ютері
  (`tools/host/rxchain.cpp`) і поміряно похибку кожного ступеня за смугами частот; чотири причини:
  1. **різниця каналів у стерео йшла 4-бітним ADPCM.** Її шум рівний по частоті, а верх у музики тихий: у смугах
     2–16 кГц він був лише на 27…2 дБ тихіший за музику, на 15–16 кГц — гучніший, а еквалайзери власника (+6…+12 дБ на
     верх) піднімали його ще. Тепер «найвища» в стерео несе різницю без стиснення, як середину: приймач отримує ті самі
     Л і П, що пішли з передавача. У 2.86 запасні копії різниці лишалися 4-бітними — і на вже чистому звуці кожен
     загублений пакет звучав коротким шелестом (власник: «звук став гранично чистий, але прослуховуються періодичні
     шелести, як при втраті пакетів»; ближній приймач на каналі 1 губив ~6 пакетів за секунду). З 2.87 копії минулого
     пакета теж несуть різницю без стиснення: одиночна втрата відновлюється біт у біт і в стерео. Дві найдавніші копії
     (потрібні лише при двох втратах поспіль) різниці не несуть — ці 4 мс звучать у моно, різниця за 1 мс плавно йде до
     нуля й повертається (без сходинки й клацання). Подвійний пакет — 1116 байтів замість 750 (47 % ефіру на 5,5 Мбіт/с
     замість 34 %). Іде, коли всі приймачі списку мають 2.87+; `Qr0` повертає 4-бітну різницю для порівняння. На
     приладах, вікна по 30 с, канал 1 (втрати там дуже гуляють від вікна до вікна): ближній приймач — ~10 кадрів за
     секунду з копій, 97 % із них точні; дві втрати поспіль — приблизно раз на 3–4 с; пропало звуку 0 мс;
  2. **вихід приймача був 16-бітним із відкиданням дробу після цифрової гучності** — на гучності 2 з 20 (−36 дБ) від
     звуку лишалося 5–10 розрядів, спотворення йшли за звуком. Зовнішні ЦАП (PCM5102, CS4344) тепер отримують 24 біти в
     32-бітному слові (PCM5102: 64 такти на кадр); однобітний вихід — 16 біт з округленням;
  3. **обмежувач** при великому підсиленні без стискання (+24 дБ на повній гучності) не встигав атакою за 0,3 мс: 1426
     відліків за 87 с виходили за шкалу й зрізалися жорстко. Тепер вікно 3 мс, і підсилення опускається середнім за
     вікно — доведено, що за шкалу не виходить; інтермодуляція −48 → −59 дБ, швидке «гуляння» підсилення (0,3–1 кГц)
     −27 → −40 дБ. +1,5 мс затримки;
  4. **підгонка темпу на 24 відліки** пропускала дзеркальні частоти на 14–16 кГц (−26 дБ на 14 кГц, −11 дБ на 15 кГц).
     Тепер 48 відліків і зріз на 15 кГц: не гірше −77 дБ до 15 кГц; верх вище 14 кГц спадає (−5,6 дБ на 15 кГц).
  `?` приймача показує тракт виходу; рядок стану — «різниця без стиснення».
- UK: `Qp<біти>` (дослід із режимами радіо) знову працює на модулі з екраном — розбір `Qp<налаштування>=<значення>` його
  більше не забирає. Буфери налагодження по радіо — 283 байти замість 1000, буфери обмежувача приймача — у купі:
  попри більший пакет у передавача на 0,9 КБ більше вільної внутрішньої пам'яті, ніж у 2.85 (постійні дані 148 451
  байт замість 149 403).
- UK: відоме: при великому підсиленні без стискання обмежувач і далі тримає 20–25 дБ більшу частину часу — це ціна
  гучності (увімкніть стискання 1–3); приймач, вимкнений із передавача, може лишитися вимкненим, якщо передавач втратить
  живлення за 3–11 с після того, як його ввімкнули знову (зміна записується в паузі звуку).

## 2.85 — 2026-10-14 (includes 2.64–2.84, which were not released / містить 2.64–2.84, які не випускались)
- EN: **receivers no longer drop out of the list with a weak signal (2.64).** A receiver reports about itself to the
  transmitter’s address with acknowledgement and retries (it used to send “to everybody”, without delivery checks), and
  raises its own radio power to 20 dBm when the signal is weak. The transmitter counts delivered and lost reports of
  each receiver (`P`).
- UK: **приймачі більше не випадають зі списку при слабкому сигналі (2.64).** Приймач повідомляє про себе на адресу
  передавача з підтвердженням і повторами (раніше — «всім», без перевірки доставки) і сам піднімає потужність радіо до
  20 дБм при слабкому сигналі. Передавач рахує доставлені й загублені повідомлення кожного приймача (`P`).
- EN: **“the transmitter is in charge” (2.65–2.67).** A receiver turned off from the transmitter cannot be turned on with
  its own knob; the transmitter returns it to the state it set. Radio updates: the receiver does not fall asleep while
  receiving, the hand-out no longer waits for laggards longer than 3 minutes, the “freezes at 99 %” bug is fixed.
- UK: **«передавач головний» (2.65–2.67).** Приймач, вимкнений із передавача, своєю ручкою не вмикається; передавач сам
  повертає його в заданий стан. Оновлення по радіо: приймач не засинає під час прийому, роздача не чекає відсталих
  довше 3 хвилин, виправлено «завмирає на 99 %».
- EN: **sound processing “for hearing” (2.68–2.72, 2.75–2.76).** In the receiver: compression (quiet sound raised, loud
  held down), the boost removed in speech pauses, spectral noise reduction; in the transmitter: input noise reduction.
  Settings on the transmitter screen (receiver window → third sheet «Обробка звуку»; the input equalizer → «Шумозаглушення»),
  in the receiver menu and from the port. Measured on the devices with a sermon from the card: the noise in pauses is
  removed by 12.8–13.6 dB. 2.75–2.76 fix two bugs that made the receivers’ noise reduction almost inactive.
- UK: **обробка звуку «для слуху» (2.68–2.72, 2.75–2.76).** У приймачі: стискання (тихе підсилюється, гучне
  притискається), зняття підсилення в паузах мови, спектральне шумозаглушення; у передавачі — шумозаглушення входу.
  Налаштування — на екрані передавача (вікно приймача → третій аркуш «Обробка звуку»; еквалайзер входу →
  «Шумозаглушення»), у меню приймача й з порту. Виміряно на приладах на проповіді з картки: шум у паузах прибирається
  на 12,8–13,6 дБ. 2.75–2.76 виправляють дві помилки, через які шумозаглушення приймачів майже не працювало.
- EN: **continuous sound with a weak signal (2.73–2.78, 2.83).** Measured: at 5.5 Mbit/s the sound breaks off between
  −77 and −80 dBm, at 1 Mbit/s it holds to about −86. Now the transmitter steps down to 1 Mbit/s by itself for a weak
  receiver (−75 dBm or weaker, or losing sound) and returns when everybody hears well — also when the rate was chosen by
  hand (it is the upper step then); a message on the screen names the receiver. Result at −83 dBm: 6.5 % loss and
  160 ms of lost sound in 75 s where there used to be no sound at all. The receiver keeps a deeper buffer while packets
  are lost and searches for the channel only after 2.5 s of silence. **New «Слабкий сигнал» (Weak signal) setting** on
  «Ефір»: lower the rate or always keep the chosen rate and quality; a manual choice of rate or quality takes effect at
  once when possible, otherwise the screen explains why; the automatic return takes 30 s with a clear margin.
- UK: **безперервний звук при слабкому сигналі (2.73–2.78, 2.83).** Виміряно: на 5,5 Мбіт/с звук обривається між −77 і
  −80 дБм, на 1 Мбіт/с тримається приблизно до −86. Тепер передавач сам сходить на 1 Мбіт/с заради слабкого приймача
  (−75 дБм і слабше або в нього пропадає звук) і повертається, коли всі чують добре, — і тоді, коли швидкість обрано
  вручну (тоді вона — верхня сходинка); повідомлення на екрані називає приймач. Результат на −83 дБм: 6,5 % втрат і
  160 мс пропалого звуку за 75 с там, де звуку не було зовсім. Приймач тримає глибший запас, поки губляться пакети, і
  шукає канал лише після 2,5 с тиші. **Нове налаштування «Слабкий сигнал»** на «Ефірі»: знижувати швидкість чи завжди
  тримати обрані швидкість і якість; ручний вибір швидкості чи якості діє одразу, коли можна, інакше екран пояснює чому;
  самостійне повернення — 30 с при доброму запасі.
- EN: **no rustle on «найвища» with a weak signal (2.77):** the double packet repeats the previous packet uncompressed —
  a single loss is recovered bit for bit (on the far receiver 870 of 911 recovered frames were exact).
- UK: **без «шелесту» на «найвищій» при слабкому сигналі (2.77):** подвійний пакет повторює минулий пакет без стиснення —
  одна втрата відновлюється біт у біт (на далекому приймачі 870 із 911 відновлених кадрів — точні).
- EN: **a new far-mode codec (2.79–2.82).** At 1 Mbit/s «стандартна» goes with an eight-band codec (4 ms frame, the
  packet carries the frame and three spare copies — three lost packets in a row cause no dropout). On a sermon:
  signal-to-noise “as heard through a receiver” 41.6 dB against 30.5 for the former «мова»; on the far receiver at
  −77…−80 dBm 332 ms of lost sound in 84 s against 4.5 s with «мова». Mono only. 2.82 runs it from fast memory: four
  times cheaper for the processor.
- UK: **новий кодек дальнього режиму (2.79–2.82).** На 1 Мбіт/с «стандартна» йде восьмисмуговим кодеком (кадр 4 мс, у
  пакеті кадр і три запасні копії — три втрачені пакети поспіль не дають провалу). На проповіді: сигнал/шум «на слух
  приймача» 41,6 дБ проти 30,5 у колишньої «мови»; на далекому приймачі (−77…−80 дБм) 332 мс пропалого звуку за 84 с
  проти 4,5 с із «мовою». Лише моно. 2.82 виконує його зі швидкої пам'яті — учетверо дешевше для процесора.
- EN: **delay and channel choice (2.82–2.83).** The “2.5 s interference” is explained: a strong access point nearby
  (−39 dBm, channel 10) made packets wait 22–35 ms for the air on channel 13 every 2.5 s, and the receivers kept a
  33–46 ms buffer because of it; on channel 1 there were no such waits. The channel scan now accounts for strong
  neighbouring networks; «Ефір» shows «канал N: пакети чекають ефіру до X мс»; «Канал при заторі: сам» also performs a
  soft move when packets often wait 15–35 ms — a trial with a return after a minute if it is not better there.
- UK: **затримка і вибір каналу (2.82–2.83).** «Завада 2,5 с» пояснена: сильна точка доступу поруч (−39 дБм, канал 10)
  змушувала пакети на каналі 13 раз на 2,5 с чекати ефіру 22–35 мс, і приймачі через це тримали запас 33–46 мс; на
  каналі 1 такого чекання немає. Замір каналів тепер враховує сильні сусідні мережі; «Ефір» показує «канал N: пакети
  чекають ефіру до X мс»; «Канал при заторі: сам» робить і м'який перехід, коли пакети часто чекають по 15–35 мс, —
  пробу з поверненням за хвилину, якщо там не краще.
- EN: **sound that vanished until a restart — found and fixed (2.84).** On a live receiver the data line to the DAC was
  stuck at one: a “not-a-number” had got into the processing filters. Source: the tempo adjustment read a row past the
  end of its table about once in 17 minutes. Fixed; the processing also replaces not-a-numbers with zero and resets
  itself, an output watchdog resets the processing and then restarts the receiver quietly if the output is stuck while
  there is sound on air. New: «Перезапуск» (Restart) in the receiver menu, «Перезапустити» (Restart) in the receiver
  window on the transmitter, `E2 <number>` from the port.
- UK: **звук, що пропадав до перезапуску, — знайдено й виправлено (2.84).** На живому приймачі лінія даних до ЦАП стояла
  в одиниці: у фільтри обробки потрапило «не число». Джерело: підгонка темпу приблизно раз на 17 хвилин читала рядок за
  краєм своєї таблиці. Виправлено; крім того, обробка замінює «не числа» нулем і сама скидається, а сторож виходу
  скидає обробку й далі тихо перезапускає приймач, якщо вихід стоїть, хоча в ефірі звук. Нове: «Перезапуск» у меню
  приймача, «Перезапустити» у вікні приймача на передавачі, `E2 <номер>` з порту.
- EN: **the CS4344 DAC on receivers (2.85).** A new output type: an 8 MHz master clock on pin 10 by integer division
  (160 MHz / 20), output at 31,250 Hz (the 32 kHz air is converted by the tempo adjustment), the pin 10 is checked for a
  short to ground before the clock is driven. Schematic `docs/skhema-priemnik-s3-cs4344.png`, documentation in both
  languages, documentation in PDF (`tools/docs_pdf.mjs`). Verified in this mode on a receiver with a PCM5102; not on an
  actual CS4344 module.
- UK: **ЦАП CS4344 на приймачах (2.85).** Новий вид виходу: головний такт 8 МГц на виводі 10 цілим діленням
  (160 МГц / 20), вихід 31 250 Гц (32 кГц ефіру перераховує підгонка темпу), вивід 10 перед подачею такту перевіряється
  на замикання на «землю». Схема `docs/skhema-priemnik-s3-cs4344.png`, опис обома мовами, документація в PDF
  (`tools/docs_pdf.mjs`). Перевірено в цьому режимі на приймачі з PCM5102; на самому модулі CS4344 — ні.
- EN: **known issues:** a “graininess” of the sound reported by the owner on 14.10 with an ideal signal, two DACs, medium
  volume, the test sound and good recordings (noise reduction off) — under investigation; the port commands
  `i20…i22` are taken by the processing, so three receiver EQ bands cannot be set from the port (they can from the
  screen); the far-mode codec is mono only.
- UK: **відомі проблеми:** «зернистість» звуку, яку власник почув 14.10 при ідеальному сигналі, на двох ЦАП, середній
  гучності, на перевірочному звуці й добрих записах (шумозаглушення вимкнене), — досліджується; команди порту
  `i20…i22` зайняті обробкою, тож три смуги еквалайзера приймача з порту не задаються (з екрана — задаються); кодек
  дальнього режиму — лише моно.

## 2.63 — 2026-10-10 (includes 2.57–2.62, which were not released / містить 2.57–2.62, які не випускались)
- EN: **two frames per packet on «найвища» (highest) quality.** One call of the radio driver costs the transmitter
  0.7–0.78 ms whatever the packet length, and 500 calls a second kept the radio core 70–93 % busy. Now two adjacent
  2 ms frames go in one packet (564 bytes in stereo, 418 in mono; ESP-NOW version 2 carries up to 1470), 250 packets
  a second. Measured on the transmitter with the test music on air: frame processing 1.85 → 1.1–1.2 ms out of 2 ms,
  the “radio” stage 0.78 → 0.34 ms per frame, frames that took longer than 2 ms 45–51 % → 4 %, air time 19 % → 15 %.
  The price is +2 ms of delay.
- UK: **два кадри в пакеті на якості «найвища».** Один виклик драйвера радіо коштує передавачеві 0,7–0,78 мс за
  будь-якої довжини пакета, і 500 викликів за секунду займали ядро радіо на 70–93 %. Тепер два сусідні кадри по
  2 мс ідуть одним пакетом (564 байти в стерео, 418 у моно; ESP-NOW версії 2 несе до 1470), 250 пакетів за секунду.
  Виміряно на передавачі з перевірочною музикою в ефірі: обробка кадру 1,85 → 1,1–1,2 мс із 2 мс, етап «радіо»
  0,78 → 0,34 мс на кадр, кадрів, довших за 2 мс, 45–51 % → 4 %, ефір 19 % → 15 %. Ціна — +2 мс затримки.
- EN: service packets of the transmitter (the single packet for old receivers, the “whom I hear” list, commands,
  debug lines) go in the free frame of a pair, when the driver has already put the audio packet on air — never right
  behind an audio packet. With the built-in ADC, radio stalls went hand in hand with the share of packets handed to
  the driver “on top” of an unsent one.
- UK: службові пакети передавача (одиночний пакет для старих приймачів, список «кого чую», команди, рядки
  налагодження) йдуть у вільному кадрі пари, коли драйвер уже віддав в ефір пакет звуку, — ніколи не впритул за
  пакетом звуку. Із вбудованим АЦП зупинки радіо йшли в ногу з часткою пакетів, покладених драйверу «поверх» ще
  не надісланого.
- EN: **the sound survives more packet loss than before.** A double packet carries compressed copies (mid and, in
  stereo, the channel difference) of the four previous frames, that is of the two previous packets: two lost
  packets in a row (8 ms) are recovered completely and in stereo. A calculation with the real packing code on a
  computer (8 s of sound, random packet loss; `tools/host/dbl.cpp`): at 5 % loss the old stereo packets lose 18 ms
  of sound and the new ones nothing; at 10 % — 68 ms against nothing; at 20 % — 302 ms against 76 ms. Mono keeps
  what it had (nothing lost up to 10 %).
- UK: **звук витримує більші втрати пакетів, ніж раніше.** Подвійний пакет несе стиснуті копії (середину і, у
  стерео, різницю каналів) чотирьох попередніх кадрів, тобто двох минулих пакетів: два загублені пакети поспіль
  (8 мс) відновлюються повністю й у стерео. Розрахунок справжнім кодом пакування на комп'ютері (8 с звуку,
  випадкові втрати пакетів; `tools/host/dbl.cpp`): при втратах 5 % старі стереопакети гублять 18 мс звуку, нові —
  нічого; при 10 % — 68 мс проти нуля; при 20 % — 302 мс проти 76 мс. Моно тримає, що й тримало (до 10 % без втрат).
- EN: **old receivers are not left without sound.** A receiver with older firmware does not understand a double
  packet, and one that is asleep or has just been switched on cannot find the transmitter by rare single packets
  either (found on our own kit: two sleeping receivers never woke up). So the transmitter now remembers the firmware
  version of every receiver of its list in flash and uses double packets only when all of them — in touch or not —
  have this firmware; while the “add a receiver” window is open it uses single packets too. Three times a second it
  also sends an ordinary single packet. `?` names the receiver that keeps the transmitter on single packets; a
  receiver that no longer exists must be deleted from the list. Checked with a receiver that pretends to be old
  (`n11`): the transmitter went to single packets within a few seconds and back again.
- UK: **старі приймачі без звуку не лишаються.** Приймач зі старішою прошивкою подвійного пакета не розуміє, а
  сплячий чи щойно ввімкнений не знаходить передавач і за рідкими одиночними пакетами (знайдено на власному
  наборі: два сплячі приймачі так і не прокинулись). Тому передавач тепер пам'ятає у флеш-пам'яті версію прошивки
  кожного приймача зі списку й іде подвійними пакетами, лише коли всі вони — на зв'язку чи ні — мають цю прошивку;
  поки відкрите вікно додавання приймачів — теж одиночні. Тричі на секунду він шле ще й звичайний одиночний пакет.
  `?` називає приймач, через який передавач лишається на одиночних пакетах; приймач, якого вже немає, треба
  видалити зі списку. Перевірено приймачем, що вдає старого (`n11`): передавач перейшов на одиночні пакети за
  кілька секунд і повернувся назад.
- EN: **the built-in ADC input: far fewer radio stalls.** On one and the same build, the built-in ADC as the input:
  single packets — 7 stalls in 3 minutes (the driver ran out of memory and the board restarted itself), double
  packets — none in 14 minutes. The PCM1808 input remains the recommended one, but the built-in ADC is usable again.
- UK: **вхід «вбудований АЦП»: зупинок радіо значно менше.** На одній і тій самій збірці, вхід — вбудований АЦП:
  одиночні пакети — 7 зупинок за 3 хвилини (драйверу забракло пам'яті, і плата сама перезапустилась), подвійні —
  жодної за 14 хвилин. Радимо, як і раніше, вхід PCM1808, але вбудованим АЦП знову можна користуватися.
- EN: fixed: **a receiver plays, but the transmitter shows it as “not in touch”.** What happened: a receiver kept
  receiving sound and obeying commands (it even followed a channel change), but its own reports stopped reaching
  the transmitter — for 18 minutes, until the receiver was restarted; the logs show the same with the other receiver
  a night earlier. The transmitting part of the receiver's Wi-Fi driver stalls while reception works; the cause
  inside the driver is not known. Until now the receiver did not watch its own transmissions at all. Now:
  (1) every transmission is counted and the receiver waits for the driver's “sent” reply — a packet without a reply
  for 0.5 s reloads the radio (20–30 ms, 24 ms of sound lost once); (2) the receiver never changes the channel while
  its own packet is still in the driver and no longer sends two packets back to back; (3) every 2 s the transmitter
  announces whom it hears, and a receiver that hears the transmitter but does not find itself in that list for 12 s
  reloads its radio, and if that does not help restarts once without the splash (about a second of silence) — never
  in a loop; (4) the receiver reports every such case to the transmitter, which writes it to its port and to the
  log on the card. Bench results with the `n12` test command: “driver silent” — healed within 0.7–2.6 s; “packets
  go nowhere” — back in the list 25 s later; “radio reload does not help” — one restart, sound back in about a
  second; “even a restart does not help” — one restart and then only rare radio reloads.
- UK: виправлено: **приймач грає, а передавач показує його «не на зв'язку».** Що сталося: приймач приймав звук і
  виконував команди (навіть перейшов на оголошений канал), але його власні повідомлення перестали доходити до
  передавача — 18 хвилин, поки приймач не перезапустили; у журналах те саме з другим приймачем напередодні вночі.
  Передавальна частина драйвера Wi-Fi приймача стає, тоді як приймання працює; причину всередині драйвера не
  знайдено. Досі приймач за своїми надсиланнями не стежив зовсім. Тепер: (1) кожне надсилання рахується, приймач
  чекає відповіді драйвера «пішло» — пакет без відповіді 0,5 с перезавантажує радіо (20–30 мс, раз губиться 24 мс
  звуку); (2) приймач не міняє канал, поки його власний пакет ще в драйвера, і більше не шле два пакети поспіль;
  (3) раз на 2 с передавач оголошує, кого він чує, і приймач, який чує передавач, але 12 с не знаходить себе в
  цьому списку, перезавантажує радіо, а якщо не допомогло — один раз перезапускається без заставки (близько
  секунди тиші) — ніколи не по колу; (4) про кожен такий випадок приймач повідомляє передавачеві, а той пише в порт
  і в журнал на картці. Досліди на столі командою `n12`: «драйвер мовчить» — вилікувано за 0,7–2,6 с; «пакети йдуть
  у нікуди» — знову в списку за 25 с; «перезавантаження радіо не допомагає» — один перезапуск, звук за секунду;
  «не допомагає й перезапуск» — один перезапуск і далі лише рідкі перезавантаження радіо.
- EN: fixed: **the transmitter restarted itself while handing out an update to receivers** (“start-up cause: hang
  (task watchdog)”; on the kit — five restarts in a row before the update finally went through; the card logs show
  the same with several earlier versions). With single packets the transmit task keeps the radio core 93–100 %
  busy; the preparation of the hand-out (a fingerprint of the 2.2 MB firmware) ran on the same core, did not finish
  even in 40 s, the idle task of that core got no time for 5 s and the task watchdog restarted the board. Now the
  preparation runs in a separate task on the display core (6 s instead of “more than 40 s”), and the task watchdog
  watches the transmit task itself instead of the idle task of the radio core — a real hang is still caught, a busy
  core is no longer taken for one. Checked: single packets plus a trial update — done in 26 s with no restart; then
  a real update of both receivers over the air with single packets — 1.5 minutes, no restart.
- UK: виправлено: **передавач сам перезапускався під час роздачі оновлення приймачам** («запуск через: ЗАВИСАННЯ
  (сторож задач)»; на наборі — п'ять перезапусків поспіль, перш ніж оновлення нарешті пройшло; у журналах на
  картці те саме з кількома попередніми версіями). За одиночних пакетів задача передачі займає ядро радіо на
  93–100 %; підготовка роздачі (відбиток 2,2 МБ прошивки) йшла на тому самому ядрі, не закінчувалась і за 40 с,
  бездіяльна задача ядра 5 с не отримувала часу, і сторож задач перезапускав плату. Тепер підготовка йде окремою
  задачею на ядрі екрана (6 с замість «понад 40 с»), а сторож задач стежить за самою задачею передачі, а не за
  бездіяльною задачею ядра радіо, — справжнє зависання він ловить, як і раніше, а зайняте ядро за зависання більше
  не вважає. Перевірено: одиночні пакети й пробне оновлення — готово за 26 с без перезапуску; далі справжнє
  оновлення обох приймачів по радіо за одиночних пакетів — 1,5 хвилини, без перезапуску.
- EN: **no pops in the headphones when a receiver starts** (PCM5102 with XSMT on pin 14). The pin used to be
  configured only when the audio output started and went high 30 ms after the clocks appeared — long before the
  receiver had finished booting. Now it is driven to ground in the first line of start-up, raised only when the
  receiver is running and sound is actually playing (the splash and the beginning of “start-up” pass with the DAC
  muted), lowered as soon as work ends, and before any restart the DAC is muted first and the pin is held at ground
  across the restart. Measured: 0 at 1.1, 2.7 and 4.3 s after a restart, 1 from 5.9 s (sound on). Between power-up
  and the firmware's start the pin is not driven by anything — if a pop at power-up remains, add a 10–47 kΩ resistor
  from XSMT to ground (see the hardware page).
- UK: **без хлопків у навушниках при запуску приймача** (PCM5102 з XSMT на виводі 14). Вивід налаштовувався лише
  під час запуску виходу звуку й ставав високим через 30 мс після появи тактів — задовго до кінця завантаження.
  Тепер він ставиться в «землю» першим рядком запуску, піднімається, лише коли приймач у роботі й звук справді
  пішов (заставка й початок «запуску» минають із заглушеним ЦАП), опускається, щойно робота скінчилась, а перед
  будь-яким перезапуском ЦАП спершу глушиться й «земля» на виводі втримується крізь перезапуск. Виміряно: 0 на
  1,1, 2,7 і 4,3 с після перезапуску, 1 — з 5,9 с (звук пішов). Від подачі живлення до запуску прошивки вивід
  нічим не керується — якщо хлопок при вмиканні живлення лишається, додайте резистор 10–47 кОм між XSMT і
  «землею» (див. сторінку про обладнання).
- EN: **the receiver's volume boost is now a real gain, like the trim on a mixing console** («Підсилення»,
  0…+24 dB). In 2.48–2.56 the boost was applied “with a hold”: only within the headroom left below full scale, so
  that the limiter would not work all the time on loud music. Measured on a receiver with the test music and +14 dB
  set: at volume 6 of 20 all +14 dB acted, at volume 12 — +12 dB, at volume 20 — +0.2 dB. To the listener that is
  “the setting does not work”. Now the sound is amplified by exactly what is set, always; whatever does not fit the
  DAC's scale after that is held by the limiter (no crackle, but loud music becomes denser at a strong boost).
  Nothing digital can be louder than the DAC's full scale — headroom above it comes only from the headphone
  amplifier.
- UK: **підсилення гучності приймача тепер справжнє, як «trim» на пульті** («Підсилення», 0…+24 дБ). У 2.48–2.56
  підсилення йшло «з притримкою»: лише в запас, що лишався до повної шкали, — щоб на гучній музиці обмежувач не
  працював без упину. Виміряно на приймачі з перевірочною музикою та виставленими +14 дБ: на гучності 6 із 20 діяли
  всі +14 дБ, на гучності 12 — +12 дБ, на гучності 20 — +0,2 дБ. Для слухача це «настройка не працює». Тепер звук
  підсилюється рівно на виставлену величину, завжди; усе, що після цього не вміщується в шкалу ЦАП, притримує
  обмежувач (тріску немає, але гучна музика на сильному підсиленні стає щільнішою). Гучніше за повну шкалу ЦАП
  цифрою не зробити — запас понад неї дає лише підсилювач навушників.
- EN: **the boost is where you look for it on the transmitter**: «Приймачі» → a receiver → «Налаштування» now has
  «Підсилення гучності» in the main list, right under «Межа гучності» (it used to be the seventh row of the
  «Еквалайзер» sheet); the receiver's interface language moved to the «Еквалайзер» sheet.
- UK: **підсилення на передавачі — там, де його шукають**: «Приймачі» → приймач → «Налаштування» тепер має
  «Підсилення гучності» в основному списку, одразу під «Межа гучності» (раніше це був сьомий рядок аркуша
  «Еквалайзер»); мова написів приймача переїхала на аркуш «Еквалайзер».
- EN: **built with Arduino core esp32 3.3.12 (ESP-IDF 5.5.5)** instead of 3.3.3 (5.5.1). In the worst case for the
  radio (built-in ADC, single packets) the transmitter stalled about five times less often on the new core (5 stalls
  in 12 minutes against 7 in 3); frame timing is the same. The display driver bug that is worked around since 2.49
  is still present in this ESP-IDF (checked with the `Y9` experiment) — the workaround stays and the picture stays
  whole. `?` now prints which core the firmware was built with. An update over the air puts the new firmware on the
  old bootloader — checked on both receivers, they start normally.
- UK: **зібрано ядром Arduino esp32 3.3.12 (ESP-IDF 5.5.5)** замість 3.3.3 (5.5.1). У найгіршому для радіо режимі
  (вбудований АЦП, одиночні пакети) передавач на новому ядрі ставав разів у п'ять рідше (5 зупинок за 12 хвилин
  проти 7 за 3); час обробки кадру той самий. Помилка драйвера екрана, яку обійдено з 2.49, у цьому ESP-IDF
  лишилась (перевірено дослідом `Y9`) — обхід зберігається, картинка ціла. `?` тепер друкує, яким ядром зібрано
  прошивку. Оновлення по радіо кладе нову прошивку на старий завантажувач — перевірено на обох приймачах,
  запускаються як звичайно.
- EN: the receiver window on the transmitter screen and the `P` report show the **exact receiver version**
  (“2.63”, not “2.6” rounded to tenths).
- UK: вікно приймача на екрані передавача і звіт `P` показують **точну версію приймача** («2.63», а не «2.6» з
  округленням до десятих).
- EN: tools and test commands: `Qd0/Qd1/Qd2` (double packets never / automatic / always), `n11` (a receiver
  pretends to be old), `n12` (test faults of the receiver radio), `tools/stallrun.sh` (a “does the radio stall” run,
  one line a minute; run it under `caffeinate -i` so that the computer does not fall asleep mid-run),
  `tools/host/dbl.cpp` (double packets under loss, part of `tools/host/run.sh`).
- UK: засоби й перевірочні команди: `Qd0/Qd1/Qd2` (подвійні пакети ніколи / сам / завжди), `n11` (приймач вдає
  старого), `n12` (перевірочні несправності радіо приймача), `tools/stallrun.sh` (прогін «чи стає радіо», рядок на
  хвилину; запускати під `caffeinate -i`, щоб комп'ютер не заснув посеред прогону), `tools/host/dbl.cpp` (подвійні
  пакети при втратах, входить у `tools/host/run.sh`).
- EN: **how to update:** put the new firmware on the transmitter (over the cable — `2 Прошить передатчик.command`
  from the kit — or from the card: the file into `UPDATE`, then «Приймачі» → «Оновлення»); receivers that are
  switched on update themselves over the air within a couple of minutes. Until every receiver of the list has
  reported the new version the transmitter stays on single packets, exactly as before the update.
- UK: **як оновити:** покласти нову прошивку в передавач (кабелем — `2 Прошить передатчик.command` із набору — або
  з картки: файл у `UPDATE`, далі «Приймачі» → «Оновлення»); увімкнені приймачі оновляться самі по радіо за пару
  хвилин. Доки кожен приймач зі списку не повідомить нову версію, передавач лишається на одиночних пакетах — так
  само, як до оновлення.
- EN: known limits: why the receiver's (and the transmitter's) Wi-Fi driver stops transmitting is still not known —
  the firmware heals it (a 0.2 s gap) and now leaves a record of every case together with the noise level the radio
  was hearing. On the PCM1808 input with double packets there was one such stall in about three hours of bench
  runs (none in the nine hours observed before, with single packets). Double packets were measured on the bench and
  calculated under loss, but not tried in really crowded air; whether the pops are gone and how the boost sounds at
  the ceiling must be judged by ear.
- UK: відомі межі: чому драйвер Wi-Fi приймача (і передавача) перестає передавати, досі не відомо — прошивка це
  лікує (провал 0,2 с) і тепер лишає запис про кожен випадок разом із рівнем шуму, який чуло радіо. На вході
  PCM1808 з подвійними пакетами за приблизно три години прогонів на столі була одна така зупинка (за дев'ять годин
  спостережень до того, з одиночними пакетами, — жодної). Подвійні пакети виміряно на столі й пораховано при
  втратах, але не випробувано в справді забитому ефірі; чи зникли хлопки і як звучить підсилення під стелею —
  судити на слух.

## 2.56 — 2026-10-10 (includes 2.50–2.55, which were not released / містить 2.50–2.55, які не випускались)
- EN: **why the sound dropped for fractions of a second, and what is done about it.** The transmitter's radio driver
  sometimes stops transmitting (reception keeps working) and only a full driver reload revives it. A controlled test
  on one and the same firmware build: 4–7 stalls in 8 minutes with the **built-in ADC** as the input, none with the
  I2S input; the card logs say the same (13 stalls in 4.6 h with the built-in ADC, 0 in 2 h with the PCM1808). In the
  ESP32-S3 the built-in ADC in continuous mode and the Wi-Fi radio share hardware. So: **use the PCM1808 input for
  real work**; with the built-in ADC a stall is now closed in 0.22 s instead of 0.62 s (revival starts with the full
  driver reload — the lighter step never helped; a stall is noticed after 0.2 s; the driver is no longer stuffed with
  packets while silent, so a stall costs 4 KB of memory instead of 13 KB; when memory runs low the board restarts
  cleanly; two stalls on one channel within five minutes — the transmitter moves to a freer channel).
- UK: **чому звук пропадав на частки секунди і що з цим зроблено.** Драйвер радіо передавача часом перестає
  передавати (приймання при цьому працює), і оживляє його лише повне перезавантаження драйвера. Дослід на одній і
  тій самій збірці: 4–7 зупинок за 8 хвилин, коли вхід — **вбудований АЦП**, і жодної зі входом I2S; те саме в
  журналах на картці (13 зупинок за 4,6 год із вбудованим АЦП, 0 за 2 год із PCM1808). В ESP32-S3 вбудований АЦП у
  безперервному режимі та радіо Wi-Fi ділять один вузол мікросхеми. Тому: **для роботи — вхід PCM1808**; із
  вбудованим АЦП зупинка тепер закривається за 0,22 с замість 0,62 с (оживлення одразу з повного перезавантаження
  драйвера — легший ступінь не допоміг жодного разу; зупинку видно за 0,2 с; мовчазний драйвер більше не
  закидається пакетами, тож зупинка коштує 4 КБ пам'яті замість 13 КБ; коли пам'яті мало — плата чисто
  перезапускається; дві зупинки на одному каналі за п'ять хвилин — передавач іде на вільніший канал).
- EN: **the port works with the PCM1808.** The ADC master clock shares pin 43 with the port's TXD. It is now put on
  the pin only while the PCM1808 input is actually on air; with the test sound or a card file on air the pin belongs
  to the port and the transmitter prints as usual. No more switching to the built-in ADC just to talk to the port.
- UK: **порт працює і з PCM1808.** Головний такт АЦП ділить вивід 43 із TXD порту. Тепер такт подається на вивід
  лише тоді, коли вхід PCM1808 справді йде в ефір; коли в ефірі перевірочний звук чи файл із картки — вивід
  належить порту, і передавач друкує як звичайно. Перемикатися на вбудований АЦП заради порту більше не треба.
- EN: fixed: **false “busy channel” moves.** The send time of a packet was recorded after the driver call, while the
  driver could answer earlier; the answer then picked a 64-packets-old record, and “the packet waited 129 ms” went
  into the reports, the log and the congestion count. A stalled radio is not counted as congestion either (the
  transmitter used to announce channel changes into air that did not exist).
- UK: виправлено: **хибні переходи «з зайнятого каналу».** Час надсилання пакета записувався після виклику драйвера,
  а драйвер встигав відповісти раніше; відповідь брала запис 64-пакетної давності, і «пакет чекав 129 мс» потрапляло
  у звіти, журнал і лік затору. Радіо, що стало, теж більше не рахується затором (передавач оголошував переходи в
  ефір, якого не було).
- EN: **every firmware update is announced on the transmitter screen.** While the transmitter writes itself, the big
  “FIRMWARE UPDATE — DO NOT SWITCH THE POWER OFF” screen is always shown (before, only an open “Update” window drew
  it); an automatic update of a receiver opens the “Update” window by itself and closes it 15 s after the end;
  before a cable update the computer sends `M8` and the screen warns “THE SCREEN GOES DARK FOR A MINUTE”; after the
  first start with a new version — “Transmitter updated: version …”.
- UK: **про кожне оновлення прошивки екран передавача повідомляє.** Поки передавач записує себе, великий екран
  «ОНОВЛЕННЯ ПРОШИВКИ — НЕ ВИМИКАЙТЕ ЖИВЛЕННЯ» показується завжди (раніше його малювало лише відкрите вікно
  «Оновлення»); автооновлення приймача саме відкриває вікно «Оновлення» і закриває його через 15 с після кінця;
  перед оновленням кабелем комп'ютер надсилає `M8`, і екран попереджає «ЕКРАН ЗГАСНЕ НА ХВИЛИНУ»; після першого
  запуску з новою версією — «Передавач оновлено: версія …».
- EN: fixed: **restarts of the transmitter by the task watchdog** (“the screen goes dark for a moment”). The transmit
  task spends 1.4 ms of the 2 ms frame with the live input and 1.8 ms with the test music; with the voice
  announcement it did not keep up, never slept, and the watchdog restarted the board. The three loudness powers are
  no longer recomputed on every frame, and under overload the task yields the core by itself. A debug limit on radio
  revival now expires by itself (a forgotten `Q1` cost nine minutes of silence on 2026-10-09). `Q` prints where the
  frame time goes; `tools/kicktest.py`, `Q10`, `Qn`, `Qp` are the test tools used.
- UK: виправлено: **перезапуски передавача сторожем задач** («екран на мить гасне»). Задача передачі витрачає 1,4 мс
  із 2 мс кадру зі звуком із входу й 1,8 мс із перевірочною музикою; з голосовим оголошенням вона не встигала, не
  спала, і сторож перезапускав плату. Три степеневі гучності більше не рахуються на кожному кадрі, а за
  перевантаження задача сама віддає ядро. Налагоджувальне обмеження оживлення радіо тепер знімається саме (забуте
  `Q1` 09.10.2026 коштувало дев'яти хвилин тиші). `Q` друкує, куди йде час кадру; `tools/kicktest.py`, `Q10`, `Qn`,
  `Qp` — використані засоби перевірки.
- EN: known limits: with the built-in ADC the radio still stalls a few times an hour (0.2 s gaps); the radio core is
  about 90 % busy with the test music on air — halving the packet rate (two frames per packet) is the planned cure.
- UK: відомі межі: із вбудованим АЦП радіо все ще стає кілька разів на годину (провали 0,2 с); ядро радіо зайняте
  приблизно на 90 %, коли в ефірі перевірочна музика, — заплановані ліки: удвічі рідші пакети (два кадри в пакеті).

## 2.49 — 2026-10-09
- EN: fixed: **the transmitter picture breaking up “until the power is switched off”**. The display driver (ESP-IDF
  5.5.1 inside Arduino core 3.3.3) chooses which of the two frame-output buffers to refill by the parity of an
  interrupt counter, and with frame output restarted in every frame it never resets that counter. As soon as two
  “portion sent” interrupts merge into one (the screen core took no interrupts for half a millisecond: switching the
  test sound, a settings write…), the driver keeps refilling the buffer that is being sent at that very moment: the
  picture shifts by 10 lines and tears in every portion, all counters stay normal, and only a restart helped.
  ESP-IDF fixed this later; the Arduino libraries do not have the fix. The firmware now chooses the buffer itself —
  by the position of the portion in the frame. Measured on the device: after a forced fault the driver stays out of
  order for good (2027 portions a second “into the wrong buffer”); with the old behaviour every portion (2029 a
  second) lands in the buffer being transmitted, with the fix — none. A real switch of the test sound knocked the
  driver over once in six tries. `u` shows how many times the driver lost its order; `Y9` forces the fault, `Y10` /
  `Y11` switch the old / the new behaviour for comparison.
- UK: виправлено: **зрив картинки передавача «до вимкнення живлення»**. Драйвер екрана (ESP-IDF 5.5.1 у складі ядра
  Arduino 3.3.3) вибирає, який із двох буферів видачі кадру наповнювати, за парністю лічильника переривань, і за
  перезапуску видачі в кожному кадрі цей лічильник не скидає ніколи. Щойно два переривання «порція пішла» зіллються
  в одне (ядро екрана пів мілісекунди не приймало переривань: перемикання перевірочного звуку, запис налаштувань…),
  драйвер відтоді наповнює той буфер, який саме передається: картинка з'їжджає на 10 рядків і рветься в кожній
  порції, усі лічильники при цьому в нормі, а допомагав лише перезапуск. В ESP-IDF це виправили пізніше; у
  бібліотеках Arduino виправлення немає. Тепер прошивка вибирає буфер сама — за місцем порції в кадрі. Виміряно на
  приладі: після навмисного збою драйвер лишається збитим назавжди (2027 порцій за секунду «не в той буфер»); за
  старої поведінки кожна порція (2029 за секунду) лягає в буфер, що саме передається, з виправленням — жодна.
  Справжнє перемикання перевірочного звуку збило драйвер один раз із шести спроб. `u` показує, скільки разів драйвер
  збивався; `Y9` — збити навмисно, `Y10` / `Y11` — стара / нова поведінка для порівняння.

## 2.48 — 2026-10-09
- EN: fixed: the “Equalizer” button on the “Sound” page lost its two bottom pixel rows when the spectrum bars redrew.
- UK: виправлено: кнопка «Еквалайзер» на сторінці «Звук» втрачала два нижні рядки точок, коли перемальовувався спектр.
- EN: **receiver volume boost** (menu “Boost”, or from the transmitter: receiver window → “Settings” → “Equalizer” →
  “Volume boost”): 0…+24 dB of digital gain above “audio as it is”, for a weak headphone amplifier. The boost only
  uses the room left below full scale: quiet audio gets all of it, loud audio as much as fits, so it does not bring
  back the “sagging” of the old volume scale. Port: `i7=<dB>[@id]`, on the receiver `n10=<dB>`.
- UK: **підсилення гучності приймача** (меню «Підсилення» або з передавача: вікно приймача → «Налаштування» →
  «Еквалайзер» → «Підсилення гучності»): 0…+24 дБ цифрового підсилення понад «звук як є» — для слабкого підсилювача
  навушників. Підсилення займає лише місце, що лишилося до повної шкали: тихий звук отримує його все, гучний —
  скільки вміщається, тож «завалів» старої шкали гучності воно не повертає. Порт: `i7=<дБ>[@номер]`, на приймачі `n10=<дБ>`.
- EN: settings are written to flash in a pause of the sound (transmitter: 0.15 s quieter than −46 dB on air;
  receiver: the received sound quieter than −40 dB), waiting no longer than 8 s. A flash write stops the audio input
  for 8–120 ms — measured: 438 packets instead of 500 in the second of the write. The receiver list (`P`) also shows
  each receiver's equalizer, knob lock and exact firmware version. Faster sample-rate conversion of card files at
  48/24/16/8 kHz (same samples, less work).
- UK: налаштування записуються у флеш у паузі звуку (передавач: 0,15 с в ефірі тихіше −46 дБ; приймач: прийнятий звук
  тихіше −40 дБ), але чекаємо не довше 8 с. Запис у флеш зупиняє приймання звуку на 8–120 мс — виміряно: 438 пакетів
  замість 500 за секунду запису. Список приймачів (`P`) показує ще еквалайзер, блокування ручки й точну версію. Швидший перерахунок частоти
  файлів із картки на 48/24/16/8 кГц (ті самі відліки, менше роботи).

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
