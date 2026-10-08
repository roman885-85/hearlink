// Приёмник: пакеты → запас → выход на наушники.
//
// Пакеты приходят неровно (эфир занят — передатчик ждёт), поэтому между приёмом и выходом лежит запас
// в несколько миллисекунд. Потерянный кадр берётся из запасной копии в следующем пакете; если пропало
// больше — вставляется тишина с плавным спадом, чтобы не щёлкало и не сбивалось время.
// Кварцы передатчика и приёмника идут чуть по-разному, поэтому приёмник играет на доли процента быстрее
// или медленнее, удерживая запас на заданной величине.
#pragma once
#include "radio.h"
#include "lang.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "rom/gpio.h"
#include "soc/gpio_sig_map.h"

#define RING 8192                 // отсчётов в кольце (256 мс)
#define OUT_BLOCK 32              // отсчётов за один проход выхода (1 мс)
#define SPEC_N 512                // окно для спектра

// Запас звука приёмника. Память под него берётся только на приёмнике (rxRingAlloc, из setup): прошивка у всех плат
// общая, и пока эти 32 КБ были постоянными, они зря занимали внутреннюю память передатчика — а её там в обрез:
// 07.10 при заторе в эфире очередь пакетов выросла, памяти не хватило, и радио передатчика встало до перезапуска.
static int16_t *rxRing;                       // середина (Л+П)/2 — в моно это и есть звук
static int16_t *rxRingS;                      // разность (Л−П)/2 — нули, когда в эфире моно
static bool rxRingAlloc() {
  if (!rxRing) rxRing = (int16_t *)heap_caps_calloc(RING, 2, MALLOC_CAP_INTERNAL);
  if (!rxRingS) rxRingS = (int16_t *)heap_caps_calloc(RING, 2, MALLOC_CAP_INTERNAL);
  return rxRing && rxRingS;
}
static volatile uint32_t rxHead, rxTail;      // пишет приём, читает выход; оба только растут
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
static volatile int32_t rxDebt;               // сколько отсчётов выход уже сыграл «впустую» при пустом запасе
static volatile bool rxNeedPrime = true;      // начать заново: набрать запас и только потом играть

// состояние приёма
static bool rxHave;
static uint32_t rxBoot, rxLastSeq;
static int16_t rxLastSample;
static volatile uint8_t rxFlags;
// счётчики за секунду
static volatile uint32_t sFrames, sRecovered, sLostFrames, sUnder, sDrops, sResync;
static volatile uint32_t sThinned;            // кадров взято из копий потому, что передатчик нарочно прореживал пакеты (эфир занят)
// состояние выхода (для отчёта и экрана)
static volatile float rxFillSm, rxRatioPpm;
static volatile float rxTargetMs;             // запас, который сейчас держится (при «авто» подбирается сам)
static volatile uint32_t rxFillMin = UINT32_MAX, rxFillMax;
static volatile bool rxPlaying;
static volatile int16_t rxPeak;               // наибольший отсчёт до громкости
static volatile uint32_t rxSoundMs;           // когда в принятом звуке последний раз было что-то громче тишины (для светодиода)
static volatile int16_t rxPeakL, rxPeakR;     // то же по каналам (в режиме стерео; для отчёта)
static volatile bool rxAirStereo;             // в эфире сейчас стерео
static volatile bool rxMute;
static volatile uint32_t rxIdentifyUntil;     // до какого времени «показывать себя»: экран мигает, в наушниках гудки
static volatile bool rxScreenFound;
static volatile bool rxScreenProbed;       // задача экрана уже проверила, есть ли экран (нашла или нет)
// ---- настройки «для слуха» (меню приёмника и окно приёмника на передатчике, с версии 2.32)
// Проверка наушников: тон 660 Гц по очереди в левое и правое ухо, передача на это время молчит. Экран показывает то
// же ухо, считая от той же отметки времени. Кончается сама через четыре круга.
#define EAR_CYCLE_MS 2600                  // круг: 1 с левое, 0,3 с пауза, 1 с правое, 0,3 с пауза
#define EAR_CYCLES 4
static volatile bool rxEarTest;
static volatile uint32_t rxEarAt;          // когда началась
static inline int rxEarSide(uint32_t now) {   // 0 — пауза, 1 — левое, 2 — правое
  uint32_t e = (now - rxEarAt) % EAR_CYCLE_MS;
  return e < 1000 ? 1 : e < 1300 ? 0 : e < 2300 ? 2 : 0;
}
static void rxEarStart(bool on) {
  if (on) rxEarAt = millis();
  rxEarTest = on;
}
static volatile uint16_t rxVuL, rxVuR;     // наибольший отсчёт левого и правого канала с прошлого чтения экраном (стрелки)
static volatile bool rxPattern;           // проверка n8=9: на выход идут постоянные числа (левый 0x5A01, правый 0xA5F3)
static volatile uint32_t rxLimBusy;        // сколько отсчётов ограничитель убавлял звук (обнуляет проверка n8=8)
static volatile float rxLimLow = 1;       // наименьшее усиление ограничителя за это время
static volatile uint32_t rxInSamples, rxOutSamples;   // сколько отсчётов принято из эфира и отдано на выход (замер настоящих частот, n8=8)
static volatile uint8_t rxUiAsk;           // проверка с порта (n7=…): открыть экран — 1 меню, 2 «Зв'язок», 3 главный
// проверка пробного тона
static volatile uint32_t tZero, tBreaks;
static volatile float tSumSq;
static volatile uint32_t tCount;
// окно для спектра
static int16_t specBuf[SPEC_N];
static volatile uint32_t specPos;
static volatile bool rxSpecOn;                // спектр сейчас на экране приёмника — иначе звук для него не копим

static i2s_chan_handle_t rxI2s;

// Состояния приёмника (владелец, 07.10: «дистанционное отключение»).
//   ПОИСК       — после включения: заставка 4 с и до 10 с ждём передатчик;
//   ЗАПУСК      — 1,5 с сообщение «запуск»;
//   РАБОТА      — звук идёт;
//   УХОД        — 3 с сообщение («сигнал відсутній, переходжу у режим очікування» или «вимкнено з передавача»);
//   ОЖИДАНИЕ    — спит всё: экран погашен, выход звука остановлен, радио выключено, процессор спит. Раз в полсекунды
//                 приёмник просыпается на ~0,1 с послушать эфир (rxSleepCycle в rxlink.h) — только чтобы поймать
//                 сигнал пробуждения. Владелец 07.10: «в спящем режиме отключается все… кроме ожидания сигнала»;
//   ПРОБУЖДЕНИЕ — экран включён, заставка 4 с, дальше «запуск» и работа.
// Из ожидания выводит сигнал передатчика, а если приёмник выключили с передатчика — только команда «включить»
// (или двойное нажатие ручки на самом приёмнике). Без экрана заставки и сообщения не ждём — звук идёт сразу.
enum { PW_SEARCH = 0, PW_STARTING, PW_RUN, PW_GOING, PW_STANDBY, PW_WAKING };
enum { WHY_NOSIGNAL = 0, WHY_OFF = 1, WHY_KEY = 2 };   // почему ушли в ожидание / чем разбужены
#define RX_SPLASH_MS 4000    // заставка (как у усилителя наушников)
#define RX_START_MS 4400     // сообщение «запуск»: полоска идёт ровно 4 с (владелец 07.10: «в приемниках полоска запуска
                             // должна идти 4 секуды»), 150 мс до неё и 250 мс полной — см. rxs::starting
#define RX_SOUND_MS 1500     // а звук входит, как и раньше, через полторы секунды после начала «запуска»: полоска —
                             // картинка, заставлять человека ждать звук лишние три секунды незачем
#define RX_GOING_MS 6000     // сообщение перед уходом в ожидание, и столько же мигает красным светодиод
                             // (владелец 07.10: «время на выключение приемника добавить до 6 секунд»; было 3, потом 4)
#define RX_WAIT_MS 10000     // сколько ждём передатчик
static volatile bool rxUiBusy;             // на экране открыто меню — в ожидание не уходить
// Переход на другой канал вместе с передатчиком, без паузы: передатчик заранее объявляет «с пакета номер N я на канале
// C» (команда CMD_HOP), приёмник переходит, как только принял пакет N−1 (или чуть позже срока, если тот потерялся).
// Сам переход делает задача звука — она просыпается каждую миллисекунду.
static volatile uint8_t rxHopCh;           // на какой канал перейти (0 — перехода не ждём)
static volatile uint32_t rxHopSeq;         // с какого пакета передатчик будет там
static volatile uint32_t rxHopDeadline;    // крайний срок перехода, мс
static volatile bool rxHopNow;             // пора (принят последний пакет на прежнем канале)
static volatile uint32_t rxHops;           // сколько таких переходов сделано (для отчёта)
static volatile bool rxLangDirty;          // язык сменился вслед за передатчиком — записать в память
static volatile bool rxUiAwake;            // в ожидании тронули ручку: экран показывает подсказку — пока не засыпать
static volatile uint8_t rxPower = PW_SEARCH;
static volatile uint8_t rxPowerWhy;
static volatile uint32_t rxPowerAt;        // когда вошли в нынешнее состояние
// Звук идёт: работа — или «запуск», в котором полторы секунды уже прошли (на экране ещё добегает полоска).
static inline bool rxSoundOn() {
  return rxPower == PW_RUN || (rxPower == PW_STARTING && millis() - rxPowerAt >= RX_SOUND_MS);
}
static volatile bool rxOtaMute;            // идёт обновление прошивки по радио (ota.h): звук молчит
static volatile bool rxAudioOn = true;     // выход звука должен работать
static volatile bool rxAudioStopped;       // задача звука подтвердила остановку

static inline void rxPush(const int16_t *x, const int16_t *sd, int n) {   // sd — разность каналов или NULL (моно)
  uint32_t h = rxHead;
  for (int i = 0; i < n; i++) {
    rxRing[(h + i) & (RING - 1)] = x[i];
    rxRingS[(h + i) & (RING - 1)] = sd ? sd[i] : 0;
  }
  rxHead = h + n;
}

static volatile uint8_t rxQ = 255;            // вид пакетов, в котором сейчас идёт передача (качество и признак стерео)
static volatile float rxFrameMs = 2;          // длительность кадра в пакете — от неё зависит наименьший запас
static Interp2 rxUp, rxUpS;

// Положить кадр в запас: 16 кГц перед этим переводятся в 32. sd — разность каналов или NULL.
static void rxEmit(int16_t *x, int16_t *sd, const QDef &d, bool fadeIn) {
  static int16_t up[Q_MAX_N * 2], upS[Q_MAX_N * 2];
  if (fadeIn)
    for (int i = 0; i < 64; i++) {   // плавный вход после тишины, 2–4 мс
      x[i] = x[i] * i / 64;
      if (sd) sd[i] = sd[i] * i / 64;
    }
  if (d.sr == SRATE) {
    rxLastSample = x[d.n - 1];
    rxPush(x, sd, d.n);
  } else {
    rxUp.run(x, d.n, up);
    if (sd) rxUpS.run(sd, d.n, upS);
    rxLastSample = up[d.n * 2 - 1];
    rxPush(up, sd ? upS : NULL, d.n * 2);
  }
}

// Вызывается из задачи Wi-Fi на каждый свой пакет.
static void rxOnPacket(const Hdr *h, const uint8_t *payload) {
  static int16_t tmp[Q_MAX_N], tmpS[Q_MAX_N];
  uint8_t txCh = h->flags >> 4;
  if (txCh >= 1 && txCh <= 13 && txCh != cfg.channel) {   // услышали передатчик с соседнего канала — перейти на его канал
    cfg.channel = txCh;
    rApply = true;
    return;
  }
  if (!rxAudioOn) {   // ожидание: играть некому — пакет не разбираем; после пробуждения начнём заново
    rxHave = false;
    return;
  }
  int q = h->q & Q_MASK;
  bool st = h->q & Q_STEREO;
  const QDef &d = QDEF[q];
  int outN = d.n * (SRATE / d.sr);   // отсчётов на выходе за кадр
  if (rxHopCh && (int32_t)(h->seq + 1 - rxHopSeq) >= 0) rxHopNow = true;   // последний пакет на прежнем канале — переходим
  {   // язык надписей — как у передатчика (если в меню приёмника не выбран свой: cfg.rxLang)
    uint8_t lg = (h->q & Q_LANG_EN) ? 1 : 0;
    if (lg != cfg.lang) {
      cfg.lang = lg;
      if (!cfg.rxLang) uiLang = lg;
      rxLangDirty = true;
    }
  }
  uint8_t hq = h->q & ~Q_LANG_EN;   // вид пакета без признака языка
  int32_t gap = (int32_t)(h->seq - rxLastSeq);
  bool fresh = !rxHave || h->boot != rxBoot;   // начало приёма или новое включение передатчика
  if (!fresh && gap <= 0) return;              // повтор или пакет из прошлого (в том числе записанный и подсунутый снова)
  if (fresh || hq != rxQ || gap > 200) {     // начало, перезапуск передатчика, смена качества или моно/стерео, долгий перерыв
    rxHave = true;
    rxBoot = h->boot;
    rxQ = hq;
    rxAirStereo = st;
    rxFrameMs = d.n * 1000.0f / d.sr;
    rxUp.reset();
    rxUpS.reset();
    rxNeedPrime = true;
    sResync = sResync + 1;
    gap = 1;
  }
  if (gap <= 0) return;   // повтор или старый пакет
  rxFlags = h->flags & 0x0F;
  bool fadeIn = false;
  int copies = qCopies(q, st);
  const uint8_t *sideBlk = st ? payload + qMainLen(q) : NULL;
  const uint8_t *cp = payload + qMainLen(q) + (st ? qBlock(q) : 0);
  if (gap >= 2) {
    // пропали кадры: последние есть в запасных копиях, остальные заменить тишиной
    // (за вычетом того, что выход уже сыграл впустую)
    int rec = gap - 1 < copies ? gap - 1 : copies;
    int32_t lost = gap - 1 - rec, fill = lost * outN;
    if (lost > 0) sLostFrames = sLostFrames + lost;
    if (fill > 0) {
      portENTER_CRITICAL(&rxMux);
      int32_t dd = rxDebt < fill ? rxDebt : fill;
      rxDebt -= dd;
      portEXIT_CRITICAL(&rxMux);
      fill -= dd;
      int v = rxLastSample;
      while (fill > 0) {
        int n = fill > FRAME ? FRAME : fill;
        for (int i = 0; i < n; i++) {
          v -= v / 8;          // плавный спад от последнего отсчёта к нулю
          tmp[i] = v;
        }
        rxPush(tmp, NULL, n);
        fill -= n;
      }
      fadeIn = true;
      rxUp.reset();
      rxUpS.reset();
    }
    for (int k = rec; k >= 1; k--) {   // от давнего кадра к недавнему; копии несут только середину — кадр выйдет в моно
      adpcmDecodeBlock(cp + (k - 1) * qBlock(q), d.n, tmp);
      rxEmit(tmp, NULL, d, fadeIn);
      fadeIn = false;
      if (h->flags & FLAG_THIN) sThinned = sThinned + 1;
      else sRecovered = sRecovered + 1;
    }
  }
  if (d.pcm) memcpy(tmp, payload, d.n * 2);
  else adpcmDecodeBlock(payload, d.n, tmp);
  if (st) adpcmDecodeBlock(sideBlk, d.n, tmpS);
  rxEmit(tmp, st ? tmpS : NULL, d, fadeIn);
  rxInSamples = rxInSamples + (uint32_t)gap * outN;   // вместе с пропавшими: счёт идёт по номерам пакетов, то есть по часам передатчика
  rxLastSeq = h->seq;
  sFrames = sFrames + 1;
}

// Вывод XSMT модуля PCM5102 («звук включён»). У модулей GY-PCM5102 он заводится перемычкой H3L на обороте, и у части
// модулей перемычки не запаяны — ЦАП молчит при исправных тактах и данных (владелец 08.10: «если замкнуть H3L так,
// чтобы соединить центральный контакт с контактом H — звук появляется»; «сделай, чтобы звук был без замыкания h3»).
// Приёмник сам держит XSMT высоким, пока выход работает: провод от вывода XSMT модуля — на вывод 14. Перед остановкой
// тактов (сон) сначала глушим ЦАП — без щелчка. Если перемычка H3L запаяна на «H», вывод 14 к XSMT НЕ подключать.
static void rxDacUnmute(bool on) {
  if (cfg.output != OUT_I2S) return;
  static bool ready;
  if (!ready) {
    pinMode(PIN_DAC_XSMT, OUTPUT);
    gpio_input_enable((gpio_num_t)PIN_DAC_XSMT);   // чтобы уровень вывода можно было прочесть проверкой n8=7
    ready = true;
  }
  if (on) vTaskDelay(pdMS_TO_TICKS(30));   // такты уже идут — дать ЦАП их поймать, потом открыть звук
  digitalWrite(PIN_DAC_XSMT, on ? HIGH : LOW);
  if (!on) vTaskDelay(pdMS_TO_TICKS(30));  // ЦАП плавно глушит звук сам — подождать, потом можно снимать такты
}

// Сколько раз выход остался без данных: порция в очереди выхода кончилась раньше, чем задача звука положила следующую
// (тогда в наушники уходит миллисекунда тишины — на басе это слышно как срыв). Считает сам драйвер выхода.
static volatile uint32_t rxOutStarved, rxOutSent;
static bool IRAM_ATTR rxOnStarve(i2s_chan_handle_t, i2s_event_data_t *, void *) {
  rxOutStarved = rxOutStarved + 1;
  return false;
}
static bool IRAM_ATTR rxOnSent(i2s_chan_handle_t, i2s_event_data_t *, void *) {
  rxOutSent = rxOutSent + 1;
  return false;
}

static bool rxOutBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 2;          // в очереди на выход не больше 2 мс
  cc.dma_frame_num = OUT_BLOCK;
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &rxI2s, NULL) != ESP_OK) return false;
  if (cfg.output == OUT_I2S) {
    // внешний ЦАП PCM5102
    i2s_std_config_t sc = {};
    sc.clk_cfg.sample_rate_hz = SRATE;
    sc.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    // SCK микросхемы — как в проекте радио владельца, где этот же модуль «работает напрямую с наушниками на полную и
    // все идеально»: на «земле». Тогда ЦАП сам восстанавливает главный такт из BCK и сам его «чистит». В 2.35–2.37 на
    // вывод 10 шёл главный такт с процессора (8,192 МГц с дробного делителя — его периоды неровные); это было главное
    // отличие от радио, а звук хрипел (владелец 08.10: «звук рыпит и хрипит, сравни как в РАДИО»). Теперь вывод 10
    // просто держится в нуле:
    // это «земля» для SCK, провод можно оставить на нём или переставить на GND — разницы нет.
    sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    pinMode(PIN_I2S_MCLK, OUTPUT);
    digitalWrite(PIN_I2S_MCLK, LOW);
    gpio_input_enable((gpio_num_t)PIN_I2S_MCLK);
    // Слово канала — 16 тактов (BCK = 32 × 32 кГц = 1,024 МГц), как в радио. В 2.38 я поставил 32 такта на слово, не
    // поправив длину сигнала каналов (ws_width осталась 16) — у владельца «работает только один канал». Возвращено.
    sc.gpio_cfg.bclk = (gpio_num_t)PIN_I2S_BCK;
    sc.gpio_cfg.ws = (gpio_num_t)PIN_I2S_WS;
    sc.gpio_cfg.dout = (gpio_num_t)PIN_I2S_DOUT;
    sc.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(rxI2s, &sc) != ESP_OK) return false;
  } else {
    // звук одним битом на двух выводах: левый канал — «+», правый — «−»
    i2s_pdm_tx_config_t pc = {};
    i2s_pdm_tx_clk_config_t clk = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(SRATE);
    i2s_pdm_tx_slot_config_t slot = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    slot.line_mode = I2S_PDM_TX_TWO_LINE_DAC;
    slot.hp_en = false;   // фильтр низа (35 Гц) съедал плавный подъём выводов при включении — см. dcStart ниже
    pc.clk_cfg = clk;
    pc.slot_cfg = slot;
    pc.gpio_cfg.clk = I2S_GPIO_UNUSED;
    pc.gpio_cfg.dout = (gpio_num_t)PIN_OUT_P;
    pc.gpio_cfg.dout2 = (gpio_num_t)PIN_OUT_N;
    if (i2s_channel_init_pdm_tx_mode(rxI2s, &pc) != ESP_OK) return false;
    // Выводы 17 и 18 у ESP32-S3 после сброса — самые слабые из всех (10 мА; остальные 20). Ставим наибольшую
    // силу: при наушниках, подключённых прямо к выводам, от неё зависит громкость (описание микросхемы, стр. 18
    // и 65: до 40 мА «вверх» и 28 мА «вниз», сопротивление открытого вывода около 17 Ом).
    gpio_set_drive_capability((gpio_num_t)PIN_OUT_P, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability((gpio_num_t)PIN_OUT_N, GPIO_DRIVE_CAP_3);
  }
  {
    i2s_event_callbacks_t cb = {};
    cb.on_send_q_ovf = rxOnStarve;
    cb.on_sent = rxOnSent;
    i2s_channel_register_event_callback(rxI2s, &cb, NULL);
  }
  if (i2s_channel_enable(rxI2s) != ESP_OK) return false;
  rxDacUnmute(true);
  return true;
}

// ---- Проверка выхода на слух: откуда шум в тишине (06.10: при нулях на входе преобразователя шум остаётся).
// Пять состояний по 6 секунд; перед каждым — столько коротких гудков, какой у него номер:
//   1 — однобитный выход остановлен, оба вывода на «земле»: шуметь нечему. Шумит — помеха идёт не через выводы
//       (по «земле», по USB, наводкой);
//   2 — выход остановлен, оба вывода на питании 3,3 В: в наушники идёт то, что дрожит на шине питания;
//   3 — выход работает, на входе нули, настройки как в работе;
//   4 — то же без подмешивания шума в преобразователе;
//   5 — то же с фильтром низа и обоими подмешиваниями.
// Тихо в 1, шумно в 2 и 3 — питание платы. Тихо в 1 и 2, шумно в 3 — сам преобразователь (тогда смотреть 4 и 5).
static volatile bool rxDacTestAsk;

static void rxQuiet(int ms, int toneAmp) {   // тишина или гудок 1 кГц, блоками по 1 мс
  static int16_t o[OUT_BLOCK * 2];
  static uint32_t ph;
  for (int b = 0; b < ms; b++) {
    for (int i = 0; i < OUT_BLOCK; i++) {
      int16_t v = toneAmp ? (int16_t)(toneAmp * sinf(2 * PI * (ph++ & 31) / 32.0f)) : 0;
      o[2 * i] = v;
      o[2 * i + 1] = cfg.rxStereo ? v : -v;
    }
    size_t w;
    i2s_channel_write(rxI2s, o, sizeof(o), &w, portMAX_DELAY);
  }
}

static void rxPdmSlot(bool hp, int dither, int dither2) {
  i2s_pdm_tx_slot_config_t slot = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  slot.line_mode = I2S_PDM_TX_TWO_LINE_DAC;
  slot.hp_en = hp;
  slot.sd_dither = dither;
  slot.sd_dither2 = dither2;
  i2s_channel_disable(rxI2s);
  i2s_channel_reconfig_pdm_tx_slot(rxI2s, &slot);
  i2s_channel_enable(rxI2s);
}

// Приёмник сам смотрит свои выводы выхода: доля единиц из 200 отсчётов, %. Идёт однобитный сигнал — около 50,
// вывод стоит на «земле» — 0, на питании — 100. Так после пробуждения видно числом, что выход действительно
// заработал, а в ожидании — что он остановлен (с передатчика это видно в списке приёмников: «вихід живий / стоїть»).
static volatile uint8_t rxOutDuty[2];
static void rxOutProbe() {
  // У внешнего ЦАП (PCM5102) смотрим такты: BCK и LRCK должны «дрожать». До 2.35 выход I2S не проверялся вовсе, и
  // передатчик показывал у такого приёмника «вихід СТОЇТЬ», хотя такты шли.
  int k = 0;
  const int pa = cfg.output == OUT_PDM ? PIN_OUT_P : PIN_I2S_BCK, pb = cfg.output == OUT_PDM ? PIN_OUT_N : PIN_I2S_WS;
  for (int pin : { pa, pb }) {
    gpio_input_enable((gpio_num_t)pin);   // только чтение уровня; выход остаётся за однобитным преобразователем
    int ones = 0;
    for (int i = 0; i < 200; i++) {
      ones += gpio_get_level((gpio_num_t)pin);
      for (int d = i % 7; d > 0; d--) __asm__ __volatile__("nop");   // неровный шаг — чтобы не попадать в такт с выходом
    }
    rxOutDuty[k++] = ones / 2;
  }
}
static inline bool rxOutLive() {
  return rxOutDuty[0] > 1 && rxOutDuty[0] < 99 && rxOutDuty[1] > 1 && rxOutDuty[1] < 99;
}

static void rxPinsStatic(int level) {   // остановить выход и поставить оба вывода на «землю» или на питание
  i2s_channel_disable(rxI2s);
  for (int pin : { PIN_OUT_P, PIN_OUT_N }) {
    gpio_set_level((gpio_num_t)pin, level);
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
  }
}

static void rxPinsPdm() {               // вернуть выводы однобитному выходу
  i2s_pdm_tx_gpio_config_t gc = {};
  gc.clk = I2S_GPIO_UNUSED;
  gc.dout = (gpio_num_t)PIN_OUT_P;
  gc.dout2 = (gpio_num_t)PIN_OUT_N;
  i2s_channel_reconfig_pdm_tx_gpio(rxI2s, &gc);
  gpio_set_drive_capability((gpio_num_t)PIN_OUT_P, GPIO_DRIVE_CAP_3);
  gpio_set_drive_capability((gpio_num_t)PIN_OUT_N, GPIO_DRIVE_CAP_3);
  i2s_channel_enable(rxI2s);
}

static void rxDacTest() {
  if (cfg.output != OUT_PDM) return;
  for (int step = 1; step <= 5; step++) {
    rxPdmSlot(false, 0, 1);
    rxQuiet(500, 0);
    for (int k = 0; k < step; k++) {
      rxQuiet(140, 1600);
      rxQuiet(200, 0);
    }
    rxQuiet(400, 0);
    switch (step) {
      case 1:
      case 2:
        rxPinsStatic(step == 2);
        vTaskDelay(pdMS_TO_TICKS(6000));
        rxPinsPdm();
        break;
      case 3: rxQuiet(6000, 0); break;
      case 4:
        rxPdmSlot(false, 0, 0);
        rxQuiet(6000, 0);
        break;
      case 5:
        rxPdmSlot(true, 1, 1);
        rxQuiet(6000, 0);
        break;
    }
  }
  rxPdmSlot(false, 0, 1);
  rxQuiet(300, 0);
  rxNeedPrime = true;
}

// Громкость: 21 шаг по 2 дБ. Ноль — тишина, 20 (100 %) — звук как есть, ниже — тише (шаг 1 — на 38 дБ).
// Усиления сверх «как есть» нет (с версии 2.40). Раньше «как есть» был шаг 12 (60 %), а выше шло усиление до +16 дБ,
// которое держал ограничитель: на музыке, записанной во всю шкалу, он на 85 % работал 84 % времени и сбивал усиление
// до 10,8 дБ — звук «заваливался» на каждом басе (владелец 08.10: «завалы и рыпения происходят только при громкости
// выше 50»; «в радио такой модуль работает напрямую с наушниками на полную и все идеально»). В радио полная громкость —
// это звук как есть; здесь теперь так же. Тихий источник усиливают на передатчике («Звук» → «Підсилення входу»).
static inline float rxVolumeGain(float step) {
  if (step <= 0) return 0;
  if (step >= 20) return 1;
  return powf(10.0f, (step - 20) * 2.0f / 20.0f);
}
// «Межа гучності» — потолок шкалы (с 2.41): ручка и передатчик по-прежнему ходят от 0 до 100 %, но 100 % звучит так,
// как звучал бы шаг предела. Владелец 08.10: «при выставлении 50 процентов ограничения максимальная шкала громкости
// остается до 100, но громкость будет равная 50». До 2.41 предел просто не давал поднять громкость выше себя.
static inline float rxVolumeScaled(int step) {
  int mx = cfg.rxVolMax < 1 || cfg.rxVolMax > 20 ? 20 : cfg.rxVolMax;
  return step * mx / 20.0f;
}

static void rxTask(void *) {
  static int16_t out[OUT_BLOCK * 2];
  if (!rxOutBegin()) {
    Serial.println("вихід звуку не запустився");
    vTaskDelete(NULL);
  }
  uint32_t frac = 0;
  uint64_t step = 1ULL << 32;
  float last = 0, integ = 0, fillSm = 0, env = 0, gain = 0, fade = 0;
  // Плавный старт: выводы поднимаются от нуля до середины (1,65 В) за 0,7 с. Иначе разделительные конденсаторы
  // заряжаются рывком через наушники или усилитель — хлопок при включении.
  float dcStart = -32768.0f;
  float prev1 = 0, prev2 = 0;
  bool priming = true, wasNeg = false;
  // Чёткость речи: подъём верха — там согласные, по которым слово отличают от слова; с возрастом слух теряет именно
  // их. Фильтр-«полка» поднимает всё выше 2 кГц на 12 дБ, а до 1 кГц звук не трогает (первая проба, 2.32 до выпуска:
  // простой подъём «звук минус его низ» задирал и середину — на 500 Гц +4 дБ: становилось громче, а не разборчивее).
  // В выход идёт смесь: звук и его «поднятая» копия в доле clK — 0,196 даёт +4 дБ, 0,507 — +8, единица — +12; доля
  // меняется плавно, переключение без щелчка. Посчитано: 1 кГц +0,9 дБ, 2 кГц +6, 3 кГц +9,8, 4 кГц +11,2 (ступень
  // «сильна»). За подъёмом стоит общий ограничитель, так что громче предела звук не станет.
  struct Shelf {
    float z1 = 0, z2 = 0;
    inline float run(float x) {
      float y = 3.287168f * x + z1;
      z1 = -5.286367f * x + 1.247790f * y + z2;
      z2 = 2.213098f * x - 0.461690f * y;
      return y;
    }
  } shM, shS;
  float clK = 0, balL = 1, balR = 1;   // баланс — ослабление одного из каналов
  float earL = 0, earR = 0, earPh = 0;   // проверка наушников: огибающие тона по каналам и его фаза
  // Появление звука. После короткого провала (миллисекунды) звук возвращается за 2 мс — просто без щелчка. А когда
  // он появляется заново — приёмник подключили к набору, он проснулся, вернулся пропавший сигнал — нарастает плавно,
  // за 0,4 с (владелец 07.10: «при подключении приемника звук в приемнике появляется плавно, без щелчков»).
  const float FADE_FAST = 1.0f / 64, FADE_SLOW = 1.0f / (SRATE * 0.4f);
  float fadeStep = FADE_SLOW;
  uint32_t primeBlocks = 1000;   // сколько миллисекунд подряд звука не было
  bool wasRun = false;
  uint32_t quiet = 0, sinceUnder = 0;
  // Ограничитель с заглядыванием вперёд (с версии 2.37).
  // Прежний (мгновенная атака по текущему отсчёту, отпускание 80 мс) на нарастающей половине каждого пика попросту
  // срезал верхушку, а усиление внутри одного периода баса «гуляло» на 1–2 дБ — искажения, как от перегрузки. С
  // наушниками на выводах платы их не было слышно (там нет баса), с ЦАП — слышно сразу: владелец 08.10 — «звук
  // заваливается по низам как от перегрузки», «рыпение начинается уже на 50 процентах… в радио такой модуль работает
  // напрямую с наушниками на полную и все идеально, так что это проблема в твоем коде». Особенно при «чёткости»: она
  // поднимает верх до ограничителя на 4–12 дБ.
  // Теперь звук идёт через задержку LIM_LA отсчётов (1,5 мс), а усиление считается по самому большому отсчёту в этом
  // окне: оно плавно опускается ДО прихода пика и медленно (четверть секунды) возвращается. Замер на проверочной
  // мелодии, громкость 75 % с «чёткостью»: скачок усиления за отсчёт был до 4,4 дБ — стал до 0,45; «гуляние» за
  // 20 мс было 1,7 дБ — стало 0,17. Когда ограничивать нечего, звук проходит без изменений.
  // С 2.40 порог — полная шкала: при громкости без усиления ограничитель звук не трогает вовсе (остался для
  // «чёткости», которая поднимает верх на 4–12 дБ, и для проверочного тона). Однобитному выходу полная шкала не
  // нужна — он, как и раньше, получает не больше 30 000 (множитель PDM_K на самом выходе).
  const float limit = 32767.0f, PDM_K = 30000.0f / 32767.0f;
  enum { LIM_LA = 48 };
  static float limL[LIM_LA], limR[LIM_LA], limNeed[LIM_LA];
  for (int i = 0; i < LIM_LA; i++) limNeed[i] = 1;
  int limPos = 0, limMinAge = 0;
  float limMin = 1, limG = 1;
  const float LIM_ATT = 1 - expf(-1.0f / 10.0f), LIM_REL = 1 - expf(-1.0f / (SRATE * 0.25f));
  // «Авто»: запас держится таким, какой был нужен за последнюю минуту, плюс 4 мс. Раз в пять секунд смотрим, как
  // глубоко запас проседал (пакеты приходят неровно: эфир занят — они ждут и приходят пачкой), и помним двенадцать
  // таких замеров. Провал звука — запас сразу вырастает на длину провала.
  // До версии 1.7 запас каждые 5 с ужимался, пока звук не провалится, — и в неровном эфире проваливался раз в
  // 10–20 с «по построению» (замер 07.10: «найвища» — 56 мс провалов за 45 с, «стандартна» — 8 мс).
  float autoMs = 8;
  uint32_t winMin = UINT32_MAX, winBlocks = 0;
  float need[12];   // сколько запаса потребовалось в каждом из последних окон по 5 с, мс
  int needPos = 0;
  for (float &v : need) v = 4;
  for (;;) {
    if (rxDacTestAsk) {
      rxDacTestAsk = false;
      rxDacTest();
    }
    if (!rxAudioOn) {   // ожидание: выход звука остановить
      if (cfg.output == OUT_PDM) {
        // сначала плавно опустить выводы к «земле» (0,5 с) — иначе конденсаторы разряжаются через наушники хлопком
        for (int b = 1; b <= 500; b++) {
          int16_t dc = (int16_t)(-32768.0f * b / 500);
          for (int i = 0; i < OUT_BLOCK * 2; i++) out[i] = dc;
          size_t w;
          i2s_channel_write(rxI2s, out, sizeof(out), &w, portMAX_DELAY);
        }
        rxPinsStatic(0);
        rxOutProbe();
      } else {
        rxDacUnmute(false);
        i2s_channel_disable(rxI2s);   // у PCM5102 без тактов выход глушится сам
        rxOutProbe();                 // такты встали — передатчик увидит «вихід СТОЇТЬ»
      }
      rxPlaying = false;
      rxPeak = 0;
      rxPeakL = 0;
      rxPeakR = 0;
      rxAudioStopped = true;
      while (!rxAudioOn) vTaskDelay(pdMS_TO_TICKS(20));
      if (cfg.output == OUT_PDM) rxPinsPdm();
      else {
        i2s_channel_enable(rxI2s);
        rxDacUnmute(true);
      }
      rxAudioStopped = false;
      rxDacTestAsk = false;
      dcStart = -32768.0f;   // и так же плавно поднять
      gain = 0;
      rxNeedPrime = true;
      continue;
    }
    // кадр длиннее — запас больше: звук приходит порциями, и между порциями запас тает на целый кадр
    float frameMs = rxFrameMs, autoLo = 4 + frameMs / 2, autoHi = 26 + frameMs * 2;
    static float lastFrameMs = 2;
    if (frameMs != lastFrameMs) {   // качество сменили — подобрать запас заново
      lastFrameMs = frameMs;
      autoMs = autoLo + 3;
      for (float &v : need) v = autoLo - 1;
    }
    float targetMs = cfg.depthMs ? (float)cfg.depthMs : autoMs;
    uint32_t target = (uint32_t)(targetMs * SRATE / 1000);
    rxTargetMs = targetMs;
    if (rxNeedPrime) {   // начать заново: выбросить накопленное, набрать запас
      rxNeedPrime = false;
      rxTail = rxHead;
      priming = true;
      frac = 0;   // integ не трогаем: разница хода часов двух плат от перезапуска приёма не меняется
      portENTER_CRITICAL(&rxMux);
      rxDebt = 0;
      portEXIT_CRITICAL(&rxMux);
    }
    uint32_t fill = rxHead - rxTail;
    if (priming) {
      if (primeBlocks < 100000) primeBlocks++;
    }
    if (priming && fill >= target + OUT_BLOCK) {
      priming = false;
      fillSm = fill;
      fade = 0;
      fadeStep = primeBlocks >= 250 ? FADE_SLOW : FADE_FAST;   // молчали дольше четверти секунды — звук появляется заново
      primeBlocks = 0;
      quiet = 0;
    }
    {   // заставка и «запуск» кончились, начинается работа — звук тоже входит плавно
      bool run = rxSoundOn();
      if (run && !wasRun) {
        fade = 0;
        fadeStep = FADE_SLOW;
      }
      wasRun = run;
    }
    if (rxHopCh && (rxHopNow || (int32_t)(millis() - rxHopDeadline) >= 0)) {   // переход на новый канал вместе с передатчиком
      uint8_t ch = rxHopCh;
      rxHopCh = 0;
      rxHopNow = false;
      cfg.channel = ch;
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      rxHops = rxHops + 1;
    }
    if (!priming) {
      if (fill > target + SRATE * 25 / 1000) {   // накопилось слишком много — вернуться к заданному запасу
        rxTail = rxHead - target;
        fill = target;
        fillSm = fill;
        sDrops = sDrops + 1;
      }
      // Подстройка темпа нужна только затем, чтобы идти в ногу с часами передатчика (сотни миллионных долей).
      // Раньше она была впятеро резче и на рывках запаса (пакеты пришли пачкой) меняла темп до ±1 % — на музыке это
      // слышно как «плавание» высоты (замер 07.10: от −6000 до +10000 ppm за несколько секунд). Теперь не больше ±0,3 %.
      fillSm += (fill - fillSm) * 0.004f;
      float err = fillSm - (float)target;
      integ += err * 3e-10f;
      // Пределы: замер 08.10 — звук из эфира идёт 32 050 отсчётов/с (+1560 ppm: АЦП передатчика даёт 64 100 в секунду
      // вместо 64 000), а выход I2S на внешний ЦАП — ровно 32 000. Однобитный выход (PDM) «спешит» так же, как
      // передатчик, поэтому там подстройка была около нуля, а с ЦАП держалась у прежнего края ±2000 ppm.
      if (integ > 0.0035f) integ = 0.0035f;
      if (integ < -0.0035f) integ = -0.0035f;
      float adj = err * 5e-6f + integ;
      if (adj > 0.0045f) adj = 0.0045f;
      if (adj < -0.0045f) adj = -0.0045f;
      step = (uint64_t)((1.0 + (double)adj) * 4294967296.0);
      rxRatioPpm = adj * 1e6f;
      rxFillSm = fillSm;
      if (fill < rxFillMin) rxFillMin = fill;
      if (fill > rxFillMax) rxFillMax = fill;
      if (fill < winMin) winMin = fill;
      if (++winBlocks >= 5000) {
        float minMs = winMin * 1000.0f / SRATE, used = targetMs - minMs, mx = 0;
        if (used > need[needPos]) need[needPos] = used;   // в этом окне уже мог быть записан провал
        needPos = (needPos + 1) % 12;
        need[needPos] = 0;
        for (float v : need) mx = v > mx ? v : mx;
        float want = mx + 4;
        autoMs = want > autoMs ? want : autoMs - 0.5f > want ? autoMs - 0.5f : want;   // вверх — сразу, вниз — по полмиллисекунды за окно
        autoMs = autoMs < autoLo ? autoLo : autoMs > autoHi ? autoHi : autoMs;
        winMin = UINT32_MAX;
        winBlocks = 0;
      }
    }
    // звук — только в работе: во время заставки и сообщений тихо (выход уже включён, вход плавный)
    uint32_t nowMs = millis();
    bool ear = rxEarTest;
    if (ear && nowMs - rxEarAt >= (uint32_t)EAR_CYCLE_MS * EAR_CYCLES) {   // проверка наушников кончается сама
      rxEarTest = false;
      ear = false;
    }
    int earSide = ear ? rxEarSide(nowMs) : 0;
    float volStep = rxVolumeScaled(cfg.volume);   // шкала 0…100 % растянута на 0…предел громкости
    float want = rxMute || cfg.off || rxOtaMute || !rxSoundOn() || ear ? 0 : rxVolumeGain(volStep);
    float earAmp = 12000.0f * rxVolumeGain(volStep < 12 ? 12 : volStep);   // тон слышен и при убранной громкости
    if (earAmp > 24000) earAmp = 24000;
    static const float CL_K[4] = { 0, 0.1962f, 0.5072f, 1.0f };           // +4, +8, +12 дБ выше 2 кГц
    static const float BAL_K[6] = { 1, 0.7079f, 0.5012f, 0.3548f, 0.2512f, 0.1778f };   // по 3 дБ на шаг
    float clWant = CL_K[cfg.rxClarity > 3 ? 0 : cfg.rxClarity];
    int bal = cfg.rxBalance < -5 ? -5 : cfg.rxBalance > 5 ? 5 : cfg.rxBalance;
    float blWant = cfg.rxStereo && bal > 0 ? BAL_K[bal] : 1, brWant = cfg.rxStereo && bal < 0 ? BAL_K[-bal] : 1;
    int vuL = 0, vuR = 0;
    uint32_t limBusy = 0;
    float limLow = 1;
    bool beep = nowMs < rxIdentifyUntil && (nowMs % 500) < 160;   // три коротких гудка в полторы секунды
    static uint32_t beepPh;
    int pk = rxPeak, pkL = rxPeakL, pkR = rxPeakR;
    uint32_t under = 0;
    // Выход «стерео» — левый и правый канал на двух выводах (усилитель подключён как стерео); «моно» — один звук
    // на двух выводах в противофазе (вдвое больший размах — полная громкость). Это свойство подключения, а не эфира:
    // при стерео-выходе и моно в эфире оба вывода несут один и тот же звук.
    bool stOut = cfg.rxStereo;
    bool tone = rxFlags & FLAG_TONE;
    for (int i = 0; i < OUT_BLOCK; i++) {
      float y, ys = 0;   // середина и разность каналов
      uint32_t tail = rxTail;
      if (priming || (int32_t)(rxHead - tail) < 4) {
        last *= 0.9f;    // запас пуст — плавно к тишине
        y = last;
        if (!priming) under++;
        fade = 0;
      } else {
        float xm = rxRing[(tail - 1) & (RING - 1)], x0 = rxRing[tail & (RING - 1)];
        float x1 = rxRing[(tail + 1) & (RING - 1)], x2 = rxRing[(tail + 2) & (RING - 1)];
        float f = frac * (1.0f / 4294967296.0f);
        float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2 * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        y = ((c3 * f + c2) * f + c1) * f + x0;
        if (stOut) {   // разность — тем же способом и из того же места
          float sm = rxRingS[(tail - 1) & (RING - 1)], s0 = rxRingS[tail & (RING - 1)];
          float s1 = rxRingS[(tail + 1) & (RING - 1)], s2 = rxRingS[(tail + 2) & (RING - 1)];
          float d1 = 0.5f * (s1 - sm), d2 = sm - 2.5f * s0 + 2 * s1 - 0.5f * s2, d3 = 0.5f * (s2 - sm) + 1.5f * (s0 - s1);
          ys = ((d3 * f + d2) * f + d1) * f + s0;
        }
        uint64_t acc = (uint64_t)frac + step;
        rxTail = tail + (uint32_t)(acc >> 32);
        frac = (uint32_t)acc;
        if (fade < 1) {      // плавный вход после тишины: быстрый — по прямой, медленный — по мягкой кривой
          fade += fadeStep;
          if (fade >= 1) {
            fade = 1;
            fadeStep = FADE_FAST;
          }
          float k = fadeStep == FADE_SLOW ? fade * fade : fade;
          y *= k;
          ys *= k;
        }
        last = y;
      }
      // для спектра, индикатора и проверки тона — звук до громкости
      int yi = (int)y;
      if (rxSpecOn) {
        specBuf[specPos & (SPEC_N - 1)] = yi > 32767 ? 32767 : yi < -32768 ? -32768 : yi;
        specPos = specPos + 1;
      }
      int a = yi < 0 ? -yi : yi;
      if (a > pk) pk = a;
      {
        int al = (int)fabsf(y + ys), ar = (int)fabsf(y - ys);   // в моно разности нет — оба равны середине
        if (stOut) {
          if (al > pkL) pkL = al;
          if (ar > pkR) pkR = ar;
        }
        if (al > vuL) vuL = al;
        if (ar > vuR) vuR = ar;
      }
      if (tone && !priming) {
        if (wasNeg && y >= 0) tZero = tZero + 1;
        wasNeg = y < 0;
        if (fabsf(y - 2 * prev1 + prev2) > 700) tBreaks = tBreaks + 1;   // у чистого тона 1 кГц тут не больше 320
        tSumSq = tSumSq + y * y;
        tCount = tCount + 1;
      }
      prev2 = prev1;
      prev1 = y;
      // громкость (без щелчков при смене) и ограничитель
      gain += (want - gain) * 0.002f;
      // чёткость речи: смесь звука и его копии с поднятым верхом (см. выше)
      clK += (clWant - clK) * 0.001f;
      float yc = y + clK * (shM.run(y) - y), ysc = stOut ? ys + clK * (shS.run(ys) - ys) : 0;
      // левый и правый; в моно оба равны середине. Ограничитель общий — по большему из двух, чтобы не сдвигать звук вбок
      float vl = (yc + ysc) * gain, vr = (yc - ysc) * gain, av = fabsf(vl) > fabsf(vr) ? fabsf(vl) : fabsf(vr);
      {
        float need = av > limit ? limit / av : 1.0f;   // во сколько раз надо убавить, чтобы этот отсчёт уложился
        float dl = limL[limPos], dr = limR[limPos];    // отсчёт, пришедший LIM_LA назад, — он сейчас пойдёт на выход
        limL[limPos] = vl;
        limR[limPos] = vr;
        limNeed[limPos] = need;
        limPos = limPos + 1 == LIM_LA ? 0 : limPos + 1;
        if (need <= limMin) {                          // наименьшее «надо убавить» по окну
          limMin = need;
          limMinAge = 0;
        } else if (++limMinAge >= LIM_LA) {            // прежний наименьший вышел из окна — найти новый
          limMin = 2;
          for (int k = 0; k < LIM_LA; k++) {           // от свежего к давнему: из равных остаётся самый свежий
            int idx = limPos - 1 - k;
            if (idx < 0) idx += LIM_LA;
            if (limNeed[idx] < limMin) {
              limMin = limNeed[idx];
              limMinAge = k;
            }
          }
        }
        limG += (limMin - limG) * (limMin < limG ? LIM_ATT : LIM_REL);
        vl = dl * limG;
        vr = dr * limG;
        if (limG < 0.999f) limBusy++;
        if (limG < limLow) limLow = limG;
      }
      if (ear || earL > 1 || earR > 1) {   // проверка наушников: тон в одно ухо (при выходе «протифаза» канал один — в оба)
        earL += ((earSide == 1 || (earSide == 2 && !stOut) ? earAmp : 0) - earL) * 0.004f;
        earR += ((earSide == 2 ? earAmp : 0) - earR) * 0.004f;
        earPh += 2 * PI * 660.0f / SRATE;
        if (earPh > 2 * PI) earPh -= 2 * PI;
        float sn = sinf(earPh);
        vl += earL * sn;
        vr += earR * sn;
      }
      balL += (blWant - balL) * 0.002f;    // баланс: один из каналов тише (только при выходе «2 канали»)
      balR += (brWant - balR) * 0.002f;
      vl *= balL;
      vr *= balR;
      if (beep) {   // гудок 1000 Гц, негромкий и не зависящий от ручки
        static const int16_t Q[8] = { 0, 1768, 2500, 1768, 0, -1768, -2500, -1768 };
        int16_t bq = Q[(beepPh++ >> 2) & 7];
        vl = vl * 0.3f + bq;
        vr = vr * 0.3f + bq;
      }
      if (cfg.output == OUT_PDM) {
        vl *= PDM_K;
        vr *= PDM_K;
      }
      float ol = vl, orr = stOut ? vr : cfg.output == OUT_PDM ? -vl : vl;
      if (dcStart < 0) {   // первые 0,7 с после включения
        dcStart += 32768.0f / (SRATE * 0.7f);
        if (dcStart > 0) dcStart = 0;
        ol += dcStart;
        orr += dcStart;
        ol = ol < -32768 ? -32768 : ol;
        orr = orr < -32768 ? -32768 : orr;
      }
      out[2 * i] = (int16_t)(ol > 32767 ? 32767 : ol < -32768 ? -32768 : ol);
      out[2 * i + 1] = (int16_t)(orr > 32767 ? 32767 : orr < -32768 ? -32768 : orr);
      if (rxPattern) {   // известные числа — чтобы на выводах было видно, какой бит где стоит
        out[2 * i] = (int16_t)0x5A01;
        out[2 * i + 1] = (int16_t)0xA5F3;
      }
    }
    rxPeak = pk > 32767 ? 32767 : pk;
    if (pk > 300 && !(rxFlags & FLAG_MUTE)) rxSoundMs = millis() ? millis() : 1;   // −40 дБ и громче — «в эфире звук»
    rxPeakL = pkL > 32767 ? 32767 : pkL;
    rxPeakR = pkR > 32767 ? 32767 : pkR;
    rxLimBusy = rxLimBusy + limBusy;                     // для проверки n8=8: сколько отсчётов ограничитель убавлял звук
    if (limLow < rxLimLow) rxLimLow = limLow;
    if (vuL > rxVuL) rxVuL = vuL > 65535 ? 65535 : vuL;   // экран забирает и обнуляет
    if (vuR > rxVuR) rxVuR = vuR > 65535 ? 65535 : vuR;
    static uint32_t underRun;   // сколько отсчётов длится нынешний провал
    if (!under && underRun && !priming) {
      // Провал кончился — запаса не хватило на всю его длину: прибавить сразу столько и ещё 2 мс. Раньше прибавлялось
      // по 2 мс за провал, и до нужного запаса приёмник добирался через пять–семь провалов подряд.
      float needed = autoMs + underRun * 1000.0f / SRATE;
      if (needed > need[needPos]) need[needPos] = needed > autoHi ? autoHi : needed;   // помнить минуту
      autoMs = needed + 2 > autoHi ? autoHi : needed + 2;
      winMin = UINT32_MAX;
    }
    if (!under || priming) underRun = 0;
    else underRun += under;
    if (under) {
      sUnder = sUnder + under;
      portENTER_CRITICAL(&rxMux);
      rxDebt += under;
      portEXIT_CRITICAL(&rxMux);
      sinceUnder = 0;
      if ((quiet += under) > SRATE / 4) priming = true;   // четверть секунды тишины — сигнала нет, начать заново
    } else {
      quiet = 0;
      if (++sinceUnder == 100) {     // 100 мс без провалов — долг забыт
        portENTER_CRITICAL(&rxMux);
        rxDebt = 0;
        portEXIT_CRITICAL(&rxMux);
      }
    }
    rxPlaying = !priming;
    static uint16_t probeN;
    if ((++probeN & 255) == 0) rxOutProbe();   // четыре раза в секунду
    size_t w;
    i2s_channel_write(rxI2s, out, sizeof(out), &w, portMAX_DELAY);
    rxOutSamples = rxOutSamples + OUT_BLOCK;
  }
}
