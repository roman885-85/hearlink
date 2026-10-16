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
#include "nsfx.h"
#include "subc.h"
#include "rsdot.h"   // подгонка темпа: свёртка одного отсчёта (с 2.97)
#include "driver/i2s_pdm.h"
#include "esp_cpu.h"   // счётчик тактов — для замера ступеней тракта (Y15)
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
// ---- ЗВУК ПРОПАЛ ДО ПЕРЕЗАГРУЗКИ (владелец 12.10: «сейчас не идет звук с приемников… устройства собраны, доступа к
// кнопке нет… исключить подобную проблему в будущем (она уже встречалась ранее, но не была озвучена)»).
// Что было на приборе (4C3B10, снято по радио до перезапуска): приём, запас, уровни до громкости — в порядке; ЦАП
// открыт, такты идут, а линия данных к ЦАП стоит в единице — все отсчёты 0xFFFF. Запрос n: «рівень до еквалайзера
// −4,5 дБ, після — nan», сжатие «не видит» звука. То есть в цепочке обработки застряло «не число» (NaN): одного
// такого отсчёта довольно — фильтры с обратной связью (эквалайзер, «чёткость») помнят прошлое, и «не число» остаётся
// в них навсегда; в отсчёт для ЦАП оно превращается как −1.
// Откуда отсчёт: подгонка темпа брала номер строки своей таблицы умножением дробным числом, и раз в ~17 минут
// (128 значений из 2^32) номер округлялся до 128 — тогда читалась строка 129, за краем таблицы. Множитель при ней
// в этот миг ноль, и обычно это безвредно, но если в чужой памяти за таблицей лежит «не число» или бесконечность —
// «ноль × бесконечность» даёт «не число». Что там лежит, зависит от запуска; поэтому беда случалась «иногда».
// (Источник найден по коду и проверен счётом на компьютере; саму память того запуска посмотреть было уже нельзя.)
// Сделано в 2.84:
//  1) номер строки таблицы считается целыми числами — за край не выходит;
//  2) на входе в обработку, после шумозаглушения и на самом выходе «не числа» и бесконечности заменяются нулём;
//  3) заметив такое на выходе, приёмник сам сбрасывает память всех фильтров (звук возвращается через доли секунды);
//  4) сторож выхода: в эфире громкий звук, громкость не на нуле, а на выход 1,5 с идёт одно и то же число — сброс
//     обработки; не помогло за 4 с — приёмник перезапускает себя сам, без заставки (не чаще раза в 10 минут);
//  5) о каждом таком случае приёмник сообщает передатчику (в порт и в журнал на карте);
//  6) в меню приёмника — «Перезапуск», в окне приёмника на передатчике — «Перезапустити».
static volatile uint32_t rxFxNan;         // сколько раз обработку сбрасывали из-за «не числа»
static volatile uint32_t rxFxStuck;       // и из-за того, что выход стоял при звуке в эфире
static volatile uint32_t rxBadIn;         // сколько «не чисел» поймано на входе в обработку (после подгонки темпа)
static volatile uint32_t rxFxHealMs;      // когда был последний сброс
static volatile bool rxSoundRestartAsk;   // сброс не помог — главный цикл перезапустит плату (rxSoundRestart)
static volatile uint8_t rxFakeNan;        // проверка (порт: n8=11…13): подложить «не число» на вход обработки (11), в память
                                          // эквалайзера (12); 13 — держать выход на одном числе, пока не снимут (n8=14)
static volatile uint32_t rxLimBusy;        // сколько отсчётов ограничитель убавлял звук (обнуляет проверка n8=8)
static volatile float rxLimLow = 1;       // наименьшее усиление ограничителя за это время
static volatile uint32_t rxInSamples, rxOutSamples;   // сколько отсчётов принято из эфира и отдано на выход (замер настоящих частот, n8=8)
static volatile uint8_t rxUiAsk;           // проверка с порта (n7=…): открыть экран — 1 меню, 2 «Зв'язок», 3 главный
// проверка пробного тона
static volatile uint32_t tZero, tBreaks;
static volatile float tSumSq;
static volatile uint32_t tCount;
// окно для спектра. С 3.03 — из кучи, только у приёмника (rxTask): постоянный массив занимал бы внутреннюю память и у
// передатчика, а у него она на пределе (правило «не меньше 40 КБ свободной»).
static int16_t *specBuf;
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
static volatile uint32_t rxDblCount;         // сколько принято «двойных» пакетов (два кадра в пакете, с 2.60)
static volatile float rxFrameMs = 2;          // сколько звука в пакете, мс — от этого зависит наименьший запас
static volatile float rxSeqMs = 2;            // длительность одного кадра (одного номера), мс: в двойном пакете их два
static volatile uint32_t rxGapMs;             // когда последний раз пакет пропал и кадры пришлось брать из запасных копий
static volatile float rxCopyMs = 8;           // на сколько миллисекунд назад достают запасные копии одного пакета
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

static volatile uint32_t rxScCount, rxMonoRec;   // кадров из копий: со стерео (копия разности) и в моно — для n8=8
static volatile uint32_t rxRecAll, rxExactRec;   // всего кадров из копий и из них без сжатия (пакет «с точными копиями», с 2.77) — для n8=8
static SubcDec rxSubDec;                         // сборка звука подполосного кодека (с 2.79); работает в задаче Wi-Fi
static volatile bool rxSubNow;                   // идёт пакетами подполосного кодека — для запаса (см. задачу звука) и отчёта
static volatile uint32_t rxPcmSFrames;           // кадров, пришедших с разностью каналов без сжатия (с 2.86) — для отчёта
static volatile uint32_t rxSubUsSum, rxSubUsMax, rxSubCalls;   // сколько времени задача Wi-Fi тратит на разбор пакета нового кодека — для n8=8
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
  bool dbl = h->q & Q_DOUBLE;   // два кадра в пакете: номер пакета — номер первого, второй кадр — следующий
  if (rxHopCh && (int32_t)(h->seq + (dbl ? 1 : 0) + 1 - rxHopSeq) >= 0) rxHopNow = true;   // последний пакет на прежнем канале — переходим
  {   // язык надписей — как у передатчика (если в меню приёмника не выбран свой: cfg.rxLang)
    uint8_t lg = (h->q & Q_LANG_EN) ? 1 : 0;
    if (lg != cfg.lang) {
      cfg.lang = lg;
      if (!cfg.rxLang) uiLang = lg;
      rxLangDirty = true;
    }
  }
  uint8_t hq = h->q & ~(Q_LANG_EN | Q_DOUBLE);   // вид пакета без признака языка и без «упаковки по два» (кадры те же)
  bool sub = rPktSub;   // подполосный кодек: признак Q_SUB входит в hq — смена кодека начинает приём заново
  int32_t gap = (int32_t)(h->seq - rxLastSeq);
  bool fresh = !rxHave || h->boot != rxBoot;   // начало приёма или новое включение передатчика
  if (!fresh && gap <= 0) return;              // повтор или пакет из прошлого (в том числе записанный и подсунутый снова)
  if (fresh || hq != rxQ || gap > 200) {     // начало, перезапуск передатчика, смена качества или моно/стерео, долгий перерыв
    rxHave = true;
    rxBoot = h->boot;
    rxQ = hq;
    rxAirStereo = st;
    rxUp.reset();
    rxUpS.reset();
    subcInit();
    rxSubDec.reset();
    rxNeedPrime = true;
    sResync = sResync + 1;
    gap = 1;
  }
  if (gap <= 0) return;   // повтор или старый пакет
  rxSubNow = sub;
  if (sub) {
    uint32_t subT0 = (uint32_t)esp_timer_get_time();
    // Подполосный кодек (proto.h, Q_SUB): новый кадр, прошлый тем же числом байт, позапрошлый попроще и ещё один
    // грубо. Кадры собираются строго по порядку — у сборки общее перекрытие окон. Пропавший совсем кадр — «пустой»
    // вызов: набор доигрывает хвост и плавно уходит в тишину, а следующий кадр так же плавно входит (щелчка нет).
    rxFlags = h->flags & 0x0F;
    rxSeqMs = d.n * 1000.0f / d.sr;
    rxFrameMs = rxSeqMs;
    rxCopyMs = SUB_COPIES * rxSeqMs;
    static const uint8_t LEN[1 + SUB_COPIES] = { SUB_F0, SUB_F0, SUB_F2, SUB_F3 };
    const uint8_t *fp[1 + SUB_COPIES];
    fp[0] = payload;
    for (int k = 1; k <= SUB_COPIES; k++) fp[k] = fp[k - 1] + LEN[k - 1];
    if (gap >= 2) {
      rxGapMs = millis() ? millis() : 1;
      int rec = gap - 1 < SUB_COPIES ? gap - 1 : SUB_COPIES;
      int32_t lost = gap - 1 - rec, fill = lost * outN;
      if (lost > 0) {
        sLostFrames = sLostFrames + lost;
        portENTER_CRITICAL(&rxMux);
        int32_t dd = rxDebt < fill ? rxDebt : fill;   // что выход уже сыграл впустую — не доигрываем
        rxDebt -= dd;
        portEXIT_CRITICAL(&rxMux);
        for (int32_t f = 0; f < lost; f++) {
          if (f < 2) rxSubDec.decode(NULL, 0, false, tmp);   // хвост прошлого звука и тишина
          else memset(tmp, 0, sizeof(int16_t) * outN);
          int32_t skip = dd < outN ? dd : outN;
          dd -= skip;
          if (skip < outN) rxPush(tmp + skip, NULL, outN - skip);
        }
        rxLastSample = 0;
      }
      for (int k = rec; k >= 1; k--) {   // от давнего кадра к недавнему
        rxSubDec.decode(fp[k], LEN[k], k == SUB_COPIES, tmp);
        rxPush(tmp, NULL, outN);
        rxRecAll = rxRecAll + 1;
        if (k == 1) rxExactRec = rxExactRec + 1;   // копия прошлого кадра — той же точности, что сам кадр
        if (h->flags & FLAG_THIN) sThinned = sThinned + 1;
        else sRecovered = sRecovered + 1;
      }
    }
    rxSubDec.decode(fp[0], LEN[0], false, tmp);
    rxLastSample = tmp[outN - 1];
    rxPush(tmp, NULL, outN);
    rxInSamples = rxInSamples + (uint32_t)gap * outN;
    rxLastSeq = h->seq;
    sFrames = sFrames + 1;
    {
      uint32_t du = (uint32_t)esp_timer_get_time() - subT0;
      rxSubUsSum = rxSubUsSum + du;
      rxSubCalls = rxSubCalls + 1;
      if (du > rxSubUsMax) rxSubUsMax = du;
    }
    return;
  }
  rxFlags = h->flags & 0x0F;
  rxSeqMs = d.n * 1000.0f / d.sr;
  rxFrameMs = (dbl ? 2 : 1) * rxSeqMs;   // звука в пакете (от этого зависит наименьший запас)
  bool fadeIn = false;
  int copies = dbl ? DBL_COPIES : qCopies(q, st);
  rxCopyMs = copies * rxSeqMs;
  // стерео с разностью без сжатия (proto.h — qPktLen4): разность в кадре — 128 байт, как середина; с 2.87 копии
  // разности кадров N−1 и N−2 — тоже без сжатия, у N−3 и N−4 копии разности нет (такой кадр выходит в моно)
  bool pcmS = dbl && st && rPktPcmS;
  int fl = qMainLen(q) + (st ? (pcmS ? d.n * 2 : qBlock(q)) : 0);   // один кадр в пакете: середина и, в стерео, разность
  const uint8_t *cp = payload + (dbl ? 2 : 1) * fl;   // запасные копии: от ближнего кадра к дальнему
  // «двойной с точными копиями» (с 2.77, proto.h): первые DUP_PCM копий — кадры без сжатия, за ними сжатые
  int dupN = dbl && rPktDup ? DUP_PCM : 0;
  const uint8_t *cpA = cp + dupN * qMainLen(q);       // сжатые копии: кадры дальше dupN
  const uint8_t *scp = cpA + (copies - dupN) * qBlock(q);   // копии разности (двойной пакет — всех тех же кадров; одиночный — одного)
  if (pcmS) rxPcmSFrames = rxPcmSFrames + 2;
  if (gap >= 2) {
    rxGapMs = millis() ? millis() : 1;   // для запаса: пока пакеты теряются, он держится глубже (см. задачу звука)
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
      rxRecAll = rxRecAll + 1;
      if (k <= dupN) {   // точная копия: кадр выйдет таким же, каким шёл в пропавшем пакете
        memcpy(tmp, cp + (k - 1) * qMainLen(q), d.n * 2);
        rxExactRec = rxExactRec + 1;
      } else adpcmDecodeBlock(cpA + (k - 1 - dupN) * qBlock(q), d.n, tmp);
      // у кадра есть и копия разности — он выйдет в стерео (в пакете с разностью без сжатия — только у N−1 и N−2)
      bool sc = st && q == Q_HI && (dbl || (rPktSc && k == 1)) && !(pcmS && k > dupN);
      if (sc) {
        if (pcmS) memcpy(tmpS, scp + (k - 1) * d.n * 2, d.n * 2);   // разность N−1, N−2 — без сжатия (с 2.87)
        else scDecode(scp + (dbl ? (k - 1) * SC_LEN : 0), tmpS);
        rxScCount = rxScCount + 1;
      } else if (pcmS) {
        // N−3 и N−4 (две потери подряд): копии разности нет — эти 4 мс звучат в моно. Резкий переход в моно — ступенька
        // на стыке, щелчок; поэтому разность за 1 мс плавно уходит к нулю от последнего сыгранного отсчёта и за 1 мс перед
        // кадром N−2 (у него разность точная, k = dupN) так же плавно приходит к его первому отсчёту. Счёт на компьютере
        // (tools/host/dbl.cpp, 5 % потерь): так сигнал/шум 32,1 / 30,0 дБ, «по прямой» от отсчёта к отсчёту — 29,8 / 27,8.
        int16_t to;
        memcpy(&to, scp + (dupN - 1) * d.n * 2, 2);
        static int16_t from;
        if (k == rec) from = rxRingS[(rxHead - 1) & (RING - 1)];   // перед первым из таких кадров
        const int F = 32, total = (rec - dupN) * d.n, at = (rec - k) * d.n;
        for (int i = 0; i < d.n; i++) {
          int p = at + i;
          float a = p < F ? 1.0f - (p + 1) / (float)(F + 1) : 0, b = total - p <= F ? 1.0f - (total - p) / (float)(F + 1) : 0;
          tmpS[i] = (int16_t)(from * a + to * b);
        }
        sc = true;
        rxMonoRec = rxMonoRec + 1;
      } else if (st) rxMonoRec = rxMonoRec + 1;
      rxEmit(tmp, sc ? tmpS : NULL, d, fadeIn);
      fadeIn = false;
      if (h->flags & FLAG_THIN) sThinned = sThinned + 1;
      else sRecovered = sRecovered + 1;
    }
  }
  for (int f = 0; f < (dbl ? 2 : 1); f++) {   // кадр пакета (в двойном — оба подряд)
    const uint8_t *fp = payload + f * fl;
    if (d.pcm) memcpy(tmp, fp, d.n * 2);
    else adpcmDecodeBlock(fp, d.n, tmp);
    if (st) {
      if (pcmS) memcpy(tmpS, fp + qMainLen(q), d.n * 2);   // разность без сжатия (с 2.86)
      else adpcmDecodeBlock(fp + qMainLen(q), d.n, tmpS);
    }
    rxEmit(tmp, st ? tmpS : NULL, d, fadeIn);
    fadeIn = false;
  }
  if (dbl) {
    gap++;   // второй кадр пакета — тоже принят
    rxDblCount = rxDblCount + 1;
  }
  rxInSamples = rxInSamples + (uint32_t)gap * outN;   // вместе с пропавшими: счёт идёт по номерам пакетов, то есть по часам передатчика
  rxLastSeq = h->seq + (dbl ? 1 : 0);
  sFrames = sFrames + (dbl ? 2 : 1);
}

// Вывод XSMT модуля PCM5102 («звук включён»). У модулей GY-PCM5102 он заводится перемычкой H3L на обороте, и у части
// модулей перемычки не запаяны — ЦАП молчит при исправных тактах и данных (владелец 08.10: «если замкнуть H3L так,
// чтобы соединить центральный контакт с контактом H — звук появляется»; «сделай, чтобы звук был без замыкания h3»).
// Приёмник сам управляет XSMT: провод от вывода XSMT модуля — на вывод 14. Если перемычка H3L запаяна на «H», вывод 14
// к XSMT НЕ подключать.
// С 2.59 (владелец 10.10: «при запуске приемников, чтобы не было хлопков в наушниках… pcm должен быть в mute, пока
// полностью не загрузится (вывод esp32 для xsmt выдает минус или землю пока не выполнена загрузка)»):
//  — вывод ставится в «землю» первой же строкой запуска платы (rxDacPinInit из setup), а не при запуске выхода звука;
//  — поднимается, только когда приёмник в работе и звук действительно пошёл (rxSoundOn), и опускается, как только
//    работа кончилась (ожидание, поиск передатчика, сообщения на экране) — до 2.59 он поднимался через 30 мс после
//    появления тактов, то есть на всё время заставки и «запуска»;
//  — перед любым перезапуском платы ЦАП сначала глушится, и «земля» на выводе удерживается сквозь перезапуск
//    (rxDacBeforeRestart), пока прошивка не запустится снова.
// Чего прошивка сделать не может: от подачи питания до её запуска (доли секунды) вывод процессора ничем не управляется.
// Если хлопок при ВКЛЮЧЕНИИ ПИТАНИЯ остаётся — резистор 10–47 кОм с XSMT на «землю» (см. описание, hardware).
static volatile bool rxDacOpen;   // XSMT высокий: ЦАП открыт
static void rxDacPinInit() {
  if (cfg.isTx || cfg.output != OUT_I2S) {
    gpio_hold_dis((gpio_num_t)PIN_DAC_XSMT);   // плата сменила роль или выход — вывод больше не держим
    return;
  }
  gpio_set_level((gpio_num_t)PIN_DAC_XSMT, 0);     // сначала уровень, потом «выход»: ни на миг не единица
  pinMode(PIN_DAC_XSMT, OUTPUT);
  digitalWrite(PIN_DAC_XSMT, LOW);
  gpio_hold_dis((gpio_num_t)PIN_DAC_XSMT);         // уровень уже задан — удержание после перезапуска можно снять
  gpio_input_enable((gpio_num_t)PIN_DAC_XSMT);     // чтобы уровень вывода можно было прочесть проверкой n8=7
}
static void rxDacSet(bool on) {
  if (cfg.output != OUT_I2S) return;
  digitalWrite(PIN_DAC_XSMT, on ? HIGH : LOW);
  rxDacOpen = on;
}
// Перед перезапуском платы (обработчик выключения, см. setup): заглушить ЦАП, дать ему плавно убрать звук и удержать
// «землю» на выводе до следующего запуска — иначе на время загрузки вывод остаётся «в воздухе».
static void rxDacBeforeRestart() {
  if (cfg.isTx || cfg.output != OUT_I2S) return;
  digitalWrite(PIN_DAC_XSMT, LOW);
  rxDacOpen = false;
  esp_rom_delay_us(30000);
  gpio_hold_en((gpio_num_t)PIN_DAC_XSMT);
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

// Частота выхода. У PCM5102 и однобитного выхода — те же 32 кГц, что в эфире. У CS4344 (с 2.85) — 31 250 Гц:
// этому ЦАП обязателен главный такт (MCLK), кратный частоте каналов (описание DS613F2, раздел 4.1 и таблица 1:
// «MCLK/LRCK must be an integer ratio»). Для 32 кГц это 8,192 МГц, а у ESP32-S3 его можно получить только дробным
// делителем из 160 МГц (19,53…) — периоды такта неровные; в 2.35–2.37 ровно такой такт на PCM5102 давал хрип.
// 160 МГц / 20 = 8 МГц делится без остатка, и 8 МГц / 256 = 31 250 Гц. Звук из эфира (32 кГц) приводит к этой частоте
// та же подгонка темпа, что держит запас: шаг по входу на отсчёт выхода не 1, а 32 000 / 31 250 = 1,024.
static uint32_t rxOutRate = SRATE;
static double rxRateBase = 1.0;   // отсчётов эфира на один отсчёт выхода (без подстройки под часы передатчика)
static bool rxMclkGrounded;       // выбран CS4344, а вывод 10 замкнут на «землю» — главный такт туда не подаётся (см. ниже)

static bool rxOutBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  // В очереди на выход — 4 мс (до 2.92 — 2). Владелец 14.10: «при включении шумодава появляются щелчки». Замер на
  // 884A94 (шумозаглушение 3, сжатие 2, паузы 3): выход оставался без данных раз в 30–60 с — с шумозаглушением, без
  // него — ни разу за минуту. Самый тяжёлый блок (шаг шумозаглушения раз в 4 мс; с 2.89 у разности каналов свой
  // спектр) — до 0,65–0,9 мс, и в редкие моменты, когда к нему добавляется задержка от общего кэша команд и радио, двух
  // миллисекунд запаса не хватало: 1 мс тишины — щелчок. +2 мс задержки на фоне запаса приёмника 14–34 мс не слышны.
  cc.dma_desc_num = 4;
  cc.dma_frame_num = OUT_BLOCK;
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &rxI2s, NULL) != ESP_OK) return false;
  rxOutRate = SRATE;
  rxRateBase = 1.0;
  if (cfg.output == OUT_CS4344) {
    // Внешний ЦАП CS4344 (Cirrus Logic; модули вроде Digilent Pmod I2S и его копий). По описанию DS613F2:
    //  — главный такт обязателен и должен быть целым кратным частоты каналов; 256 × 31 250 Гц = 8 МГц = 160 МГц / 20;
    //  — такт бит подаём свой (режим «External SCLK»: ЦАП переходит в него, увидев 16 перепадов за полупериод LRCK):
    //    64 такта на кадр, слово канала — 32 такта (ЦАП берёт 24 старших бита; с 2.86 их и шлём — до этого шло 16 бит
    //    и нули, см. «24 бита на ЦАП» в задаче звука);
    //  — LRCK обязан быть «ровным», 45–55 %: длина сигнала каналов — те же 32 такта, а не 16 (ошибка 2.38 с PCM5102);
    //  — входы: «единица» — от 60 % напряжения питания ЦАП; при питании 3,3 В это 2 В, выводы ESP32-S3 подходят с запасом.
    rxOutRate = 31250;
    rxRateBase = (double)SRATE / rxOutRate;
    // Вывод 10 до 2.85 был «землёй» для SCK модуля PCM5102, и SCK могли завести и проводом на вывод 10, и каплей
    // припоя на «землю» на самом модуле. Подать туда такт 8 МГц — значит гнать вывод в замыкание. Поэтому сначала
    // проверка: вывод — вход с подтяжкой к питанию; если он всё равно читается нулём, его держит «земля» — такт не
    // подаём (ЦАП CS4344 без него молчит, а PCM5102, выбранный здесь по ошибке, сыграет и так — от BCK).
    pinMode(PIN_I2S_MCLK, INPUT_PULLUP);
    delay(3);
    int ones = 0;
    for (int k = 0; k < 8; k++) {
      ones += digitalRead(PIN_I2S_MCLK);
      delayMicroseconds(200);
    }
    rxMclkGrounded = ones == 0;
    pinMode(PIN_I2S_MCLK, INPUT);
    i2s_std_config_t sc = {};
    sc.clk_cfg.sample_rate_hz = rxOutRate;
    sc.clk_cfg.clk_src = I2S_CLK_SRC_PLL_160M;
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    sc.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    sc.slot_cfg.ws_width = 32;
    sc.gpio_cfg.mclk = rxMclkGrounded ? I2S_GPIO_UNUSED : (gpio_num_t)PIN_I2S_MCLK;
    sc.gpio_cfg.bclk = (gpio_num_t)PIN_I2S_BCK;
    sc.gpio_cfg.ws = (gpio_num_t)PIN_I2S_WS;
    sc.gpio_cfg.dout = (gpio_num_t)PIN_I2S_DOUT;
    sc.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(rxI2s, &sc) != ESP_OK) return false;
  } else if (cfg.output == OUT_I2S) {
    // внешний ЦАП PCM5102
    i2s_std_config_t sc = {};
    sc.clk_cfg.sample_rate_hz = SRATE;
    sc.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    // С 2.86 — 32-битные слова (24 бита звука; ЦАП их принимает): BCK = 64 × 32 кГц = 2,048 МГц, сигнал каналов —
    // тоже 32 такта (ws_width; в 2.38 он остался 16, и играл один канал). По описанию PCM510x такт из BCK его PLL
    // берёт и при 32, и при 64 тактах на кадр; 64 такта с этим же модулем проверены в 2.85 (выход «CS4344» на 884A94).
    // Зачем: громкость приёмника — в цифре, и 16 бит после неё — это 16 бит минус громкость. На 2 из 20 (−36 дБ, так
    // стоял 4C3B10 14.10) от звука оставалось 10 бит, а на тихих местах и хвостах нот — 5–9: отсюда «зерно» (до 2.86 дробь
    // ещё и отбрасывалась). 24 бита дают запас на всю шкалу громкости.
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    sc.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    sc.slot_cfg.ws_width = 32;
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
    // До 2.86 слово канала было 16 тактов (BCK = 32 × 32 кГц = 1,024 МГц), как в радио. В 2.38 я поставил 32 такта на
    // слово, не поправив длину сигнала каналов (ws_width осталась 16) — у владельца «работал только один канал».
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
  return true;   // ЦАП пока заглушён (XSMT в «земле»): откроется, когда пойдёт звук, — см. rxDacSet в задаче звука
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

// Эквалайзер приёмника (с 2.45; владелец 08.10: «графический эквалайзер для приемников, который будет настраиваться с
// передатчика»; 09.10: «мне кажется, что много низов идет» — звук с пульта шёл в наушники без всякой обработки).
// Шесть звеньев второго порядка подряд: срез низов 100 Гц (12 дБ на октаву) и пять полос — «полка» 125 Гц, колокола
// 400 Гц, 1 и 2,5 кГц (добротность 1), «полка» 6 кГц; каждая −12…+12 дБ шагом 2. Стоит после подгонки темпа и перед
// «чёткостью», громкостью и ограничителем: подъём полос ограничитель удержит в шкале.
// обработка «для слуха» (nsfx.h) — для отчёта: на сколько сейчас убавляют шумозаглушение (в среднем, дБ), сжатие и
// приглушение в паузах (наибольшее с прошлого отчёта, дБ), где сейчас «дно шума», и сколько занимает шумозаглушение
static volatile float rxNsRedDb, rxDynRedDb, rxGateRedDb, rxGateFloorDb;
static volatile bool rxNsLearnAsk, rxNsAutoAsk, rxNsFrozen;   // образец шума: «запомнить сейчас» / «снова сам» / запомнен ли
static volatile uint32_t rxNsUsSum, rxNsUsMax, rxNsCalls, rxNsPauseHops;
// Ступени тракта приёмника (с 2.94; команда Y15): до подгонки темпа, подгонка темпа, шумозаглушение, обработка и выход,
// остальное до записи в I2S; с 3.00 — и части «до подгонки темпа»: запас, темп, настройки, громкость. (В пробных 2.97–3.00
// мерились и части «обработки и выхода» — по отсчёту; сами замеры брали ~25 мкс на блок, в 3.01 их нет. Итог 15.10:
// до эквалайзера 29, эквалайзер 64–72, «чёткость» и сжатие 75–86 (до 3.00 — 101–125), ограничитель 36, остальное 41.) Задача звука копит такты сама и по просьбе (rxStAsk) отдаёт итог в микросекундах с прошлой
// просьбы — одним куском, без разрывов.
static volatile bool rxStAsk, rxStReady;
static uint32_t rxStRes[9], rxStResMax, rxStResN;
static volatile float rxBoostNow = 1;   // во сколько раз усиление действует сейчас (оно меняется плавно) — для отчёта
static bool rxQuietToSave(uint32_t dueMs, uint32_t maxMs);   // можно ли сейчас писать настройки (пауза в звуке) — см. ниже
static volatile int rxEqPkIn, rxEqPkOut;   // наибольший отсчёт середины до и после эквалайзера с прошлого запроса n7=3
static volatile uint32_t rxEqGen = 1;   // растёт при каждой смене настройки — задача звука пересчитывает звенья
static bool rxEqSet(uint8_t band, uint8_t val) {
  if (band < 5) {
    if (val > 12 || cfg.rxEq[band] == val) return false;
    cfg.rxEq[band] = val;
  } else if (band == 5) {
    if (cfg.rxLowCut == (val ? 1 : 0)) return false;
    cfg.rxLowCut = val ? 1 : 0;
  } else return false;
  rxEqGen = rxEqGen + 1;
  return true;
}

// Подгонка темпа (с 2.44): таблица sinc с окном Кайзера, 24 отсчёта × 128 фаз, между фазами — по прямой.
// Прежняя (кубическая, по 4 отсчётам) ошибалась на верхах: в полосе 4–8 кГц −30 дБ к звуку, 8–14 кГц −14 дБ. С ЦАП
// темп держится +1560 ppm, и эта ошибка «дрожала» 50 раз в секунду — владелец 08.10: «цифровая грязь в виде фонового
// жужжания в некоторых гармониках». Новая: −92 дБ до 8 кГц, −41 дБ в 8–14 кГц (счёт на проверочной мелодии).
// С 2.86 — 48 отсчётов и срез на 0,94 половины частоты (15 кГц): 24 отсчётов не хватало на самый верх — синус 14 кГц
// выходил с искажениями −26 дБ, 15 кГц — −11 дБ, 15,5 кГц — −6 дБ (крутизны среза не хватало, и зеркальная частота
// 32 кГц − f проходила почти без ослабления; tools/host/rxchain.cpp, часть 2). Теперь до 15 кГц не хуже −77 дБ, а
// 15,5 кГц ослаблен на 13 дБ и чист на −33 дБ. Это верх, который поднимают «чёткость» и эквалайзер владельца (+6…+12
// дБ на 6 кГц и выше). Памяти — 24 КБ вместо 12; не нашлось — берётся прежняя таблица 24 × 128, затем кубическая.
enum { RXRS_MAX = 48, RXRS_PH = 128 };
static_assert((int)RXRS_MAX <= (int)RS_TAPS_MAX && RXRS_MAX % 4 == 0, "rsDot: отводов не больше RS_TAPS_MAX и кратно четырём");
static int rxRsTaps = RXRS_MAX;   // сколько отсчётов в таблице, которую удалось разместить
static float *rxRsTab;   // [RXRS_PH + 1][rxRsTaps], во внутренней памяти (24 КБ) — только у приёмника
static float rsI0(float x) {
  float s = 1, t = 1;
  for (int k = 1; k < 30; k++) {
    t *= (x / (2 * k)) * (x / (2 * k));
    s += t;
  }
  return s;
}
static bool rsBegin() {
  rxRsTaps = RXRS_MAX;
  rxRsTab = (float *)heap_caps_malloc((RXRS_PH + 1) * RXRS_MAX * sizeof(float), MALLOC_CAP_INTERNAL);
  if (!rxRsTab) {   // памяти мало — прежняя таблица (до 2.86)
    rxRsTaps = 24;
    rxRsTab = (float *)heap_caps_malloc((RXRS_PH + 1) * 24 * sizeof(float), MALLOC_CAP_INTERNAL);
  }
  if (!rxRsTab) return false;
  const int TAPS = rxRsTaps;
  const float beta = TAPS == RXRS_MAX ? 8.6f : 8.5f, cut = TAPS == RXRS_MAX ? 0.94f : 1.0f, i0b = rsI0(beta);
  const int h = TAPS / 2;
  for (int p = 0; p <= RXRS_PH; p++) {
    float sum = 0, *c = rxRsTab + p * TAPS;
    for (int j = 0; j < TAPS; j++) {
      float d = (j - (h - 1)) - p / (float)RXRS_PH, u = d / h, w = fabsf(u) < 1 ? rsI0(beta * sqrtf(1 - u * u)) / i0b : 0;
      float x = PI * d * cut;
      c[j] = (fabsf(x) < 1e-6f ? 1.0f : sinf(x) / x) * w;
      sum += c[j];
    }
    for (int j = 0; j < TAPS; j++) c[j] /= sum;
  }
  return true;
}

static void rxTask(void *) {
  static int16_t out[OUT_BLOCK * 2];     // 16 бит: однобитному выходу — сам звук, внешнему ЦАП — для сторожа выхода
  const bool wide = cfg.output != OUT_PDM;   // выход выбирается при запуске (смена — с перезапуском)
  static Eq6 eq;
  uint32_t eqGen = 0;
  // Усиление сверх «звука как есть» — настоящее, как «trim» на пульте (с 2.62): сколько выставлено, на столько звук и
  // усиливается, всегда. Владелец 10.10: «на приемнике перестала работать настройка буста… при изменении параметра
  // ничего не изменяется»; «нужен реальный цифровой буст усиления, на микшерном пульте данная функция называется trim».
  // В 2.48–2.61 усиление шло «с придержкой» — только в запас, оставшийся до полной шкалы (чтобы на музыке во всю шкалу
  // ограничитель не работал без остановки): на громкости 20 из 20 с проверочной музыкой из выставленных +14 дБ
  // действовало +0,2 дБ (замер на 4C3B10), на громкости 6 — все +14. Для слушателя это «настройка не работает».
  // Теперь всё, что после усиления не помещается в шкалу, придерживает ограничитель (он стоит дальше и заглядывает
  // на 1,5 мс вперёд): треска нет, но на сильном усилении с громкой музыкой звук становится плотнее и «дышит» —
  // расплата за громкость, и убавить её может сам слушатель той же настройкой.
  float bK = 1, boostWant = 1;
  // сжатие и паузы — вместо «множитель + ограничитель»; спектральное шумозаглушение (память под него берётся, когда
  // его впервые включат)
  static NsDyn dyn;
  static NsSpec *ns;
  static float *nsWork;   // с 3.03 — из кучи, вместе с NsSpec (см. specBuf)
  static float nsM[OUT_BLOCK], nsS[OUT_BLOCK];
  bool nsWas = false;
  float dynG = 1;
  uint8_t boostStep = 255;
  bool rsOk = rsBegin();   // памяти не нашлось — останется прежняя, кубическая
  specBuf = (int16_t *)heap_caps_calloc(SPEC_N, sizeof(int16_t), MALLOC_CAP_INTERNAL);   // нет памяти — спектра не будет, звук будет
  if (!rxOutBegin()) {
    Serial.println("вихід звуку не запустився");
    vTaskDelete(NULL);
  }
  uint32_t frac = 0;
  uint64_t step = (uint64_t)(rxRateBase * 4294967296.0);   // у CS4344 вход идёт чуть быстрее выхода (см. rxOutRate)
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
  // Теперь звук идёт через задержку LIM_LA отсчётов (1,5 мс; с 2.86 — 3 мс), а усиление считается по самому большому отсчёту в этом
  // окне: оно плавно опускается ДО прихода пика и медленно (четверть секунды) возвращается. Замер на проверочной
  // мелодии, громкость 75 % с «чёткостью»: скачок усиления за отсчёт был до 4,4 дБ — стал до 0,45; «гуляние» за
  // 20 мс было 1,7 дБ — стало 0,17. Когда ограничивать нечего, звук проходит без изменений.
  // С 2.40 порог — полная шкала: при громкости без усиления ограничитель звук не трогает вовсе (остался для
  // «чёткости», которая поднимает верх на 4–12 дБ, и для проверочного тона). Однобитному выходу полная шкала не
  // нужна — он, как и раньше, получает не больше 30 000 (множитель PDM_K на самом выходе).
  const float limit = 32767.0f, PDM_K = 30000.0f / 32767.0f;
  // С 2.86 окно — 3 мс (96 отсчётов), и усиление опускается не одним звеном (за 0,3 мс, как до 2.86), а средним за всё
  // окно: к приходу пика оно ровно там, где нужно, и начало спуска растянуто на 3 мс. Прежняя атака на большом
  // усилении без сжатия (884A94 14.10: громкость 100 %, +24 дБ, сжатие выключено — ограничитель убавлял 20–25 дБ почти
  // всё время) давала быстрые рывки усиления и не успевала: на проверочной мелодии 1426 отсчётов за 87 с выходили за
  // шкалу и срезались жёстко (треск), «гуляние» усиления в полосе 0,3–1 кГц было −27 дБ, интермодуляция −48 дБ. Теперь
  // 3 отсчёта за шкалу (доли единицы — округление), −40 дБ и −59 дБ (tools/host/rxchain.cpp, часть 5). Доказательство,
  // что за шкалу не выйдет: каждое из LIM_LA прошлых значений «отпускания» не больше нужного выходящему отсчёту (все их
  // окна его видели), значит, и их среднее не больше. Наименьшее по окну — очередью (dq…): значения в ней растут от
  // головы к хвосту, на отсчёт уходит постоянное время (до 2.86 при каждом уходе наименьшего окно пересматривалось целиком).
  enum { LIM_LA = 96, LIM_DQ = 128 };
  // Буферы ограничителя и 24-битного выхода (2.86) — из кучи, а не постоянные: прошивка общая, и постоянные массивы
  // задачи приёмника занимали бы внутреннюю память и у передатчика (у него её мало: правило «меньше 40 КБ свободной —
  // не выпускать», журнал 2.14).
  struct OutBufs {
    float limL[LIM_LA], limR[LIM_LA], boxBuf[LIM_LA], dqV[LIM_DQ];
    uint32_t dqT[LIM_DQ];
    int32_t out32[OUT_BLOCK * 2];   // внешнему ЦАП (PCM5102, CS4344) — 24 бита в 32-битном слове (с 2.86)
  };
  OutBufs *ob = (OutBufs *)heap_caps_calloc(1, sizeof(OutBufs), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!ob) {
    Serial.println("вихід звуку: немає пам'яті під обмежувач");
    vTaskDelete(NULL);
  }
  float *const limL = ob->limL, *const limR = ob->limR, *const boxBuf = ob->boxBuf, *const dqV = ob->dqV;
  uint32_t *const dqT = ob->dqT;
  int32_t *const out32 = ob->out32;
  for (int i = 0; i < LIM_LA; i++) boxBuf[i] = 1;
  int limPos = 0, boxPos = 0, boxN = 0;
  uint32_t limT = 0, dqH = 0, dqE = 0;   // время в отсчётах; голова и хвост очереди
  float limG = 1, limR1 = 1, boxSum = LIM_LA;
  // Со сжатием (с 2.68) ограничителю достаются только выбросы первых полутора миллисекунд, и возвращать усиление
  // четверть секунды незачем: после каждого такого выброса звук «проседал» бы на те же четверть секунды. Со
  // сжатием отпускание — 40 мс; без него (сжатие выключено) — как было.
  const float LIM_REL = 1 - expf(-1.0f / (SRATE * 0.25f)), LIM_REL_FAST = 1 - expf(-1.0f / (SRATE * 0.04f));
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
    // Обновление прошивки по радио (с 2.90): звук всё равно молчит, а весь тракт — подгонка темпа, шумозаглушение,
    // эквалайзер, ограничитель — крутился бы над тишиной. Владелец 14.10: с включённым шумозаглушением обновление
    // 2.88 → 2.89 шло в 6–12 раз медленнее обычного, и приём, и запись во флеш («приемники обновляются, но чего-то крайне
    // медленно», «процесс загрузки тоже был очень медленным»); с выключенным (2.86–2.88) — за две минуты. Код обработки
    // идёт из флеша и делит с записью шину памяти и общий кэш команд. Поэтому на время обновления — только тишина на
    // выход, без всякого счёта; после него (если оно сорвалось и плата не перезапустилась) приём звука начнётся заново.
    if (rxOtaMute && rxAudioOn) {
      memset(out, 0, sizeof(out));
      memset(out32, 0, sizeof(ob->out32));
      size_t w;
      if (wide) i2s_channel_write(rxI2s, out32, sizeof(ob->out32), &w, portMAX_DELAY);
      else i2s_channel_write(rxI2s, out, sizeof(out), &w, portMAX_DELAY);
      rxOutSamples = rxOutSamples + OUT_BLOCK;
      rxPeak = 0;
      rxPeakL = 0;
      rxPeakR = 0;
      rxNeedPrime = true;
      continue;
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
        rxDacSet(false);
        vTaskDelay(pdMS_TO_TICKS(30));   // ЦАП плавно глушит звук сам — подождать, потом можно снимать такты
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
      else i2s_channel_enable(rxI2s);   // ЦАП откроется, когда пойдёт звук (ниже, по rxSoundOn)
      rxAudioStopped = false;
      rxDacTestAsk = false;
      dcStart = -32768.0f;   // и так же плавно поднять
      gain = 0;
      rxNeedPrime = true;
      continue;
    }
    uint32_t cyc0 = esp_cpu_get_cycle_count();
    // кадр длиннее — запас больше: звук приходит порциями, и между порциями запас тает на целый кадр
    // Потолок запаса: у длинных кадров (8 и 12 мс — «мова» и «дальня», на них передатчик уходит ради слабого
    // приёмника) он выше — 58 и 74 мс вместо 42 и 50. На 1 Мбит/с пакет занимает эфир 2,3 мс и ждёт свободного
    // эфира дольше: замер 12.10 — до 35–49 мс, и при потолке 42 мс у обоих приёмников выходило 8–18 мс провалов
    // в минуту. Запас растёт до потолка, только когда столько понадобилось, и сам ужимается обратно.
    float frameMs = rxFrameMs, autoLo = 4 + frameMs / 2, autoHi = 26 + frameMs * (frameMs >= 7.5f ? 4 : 2);
    if (rxSubNow) autoHi = 46;   // подполосный кодек идёт и на 1 Мбит/с, где пакет ждёт эфира до 30–35 мс (кадр у него 4 мс)
    static float lastFrameMs = 2;
    if (frameMs != lastFrameMs) {   // качество сменили — подобрать запас заново
      // Кадр стал длиннее — значит, передатчик перешёл на нижнюю ступень из-за слабого приёмника (или так выбрал
      // оператор): начинаем с глубокого запаса и ужимаем по замерам, а не с мелкого, который тут же провалится.
      autoMs = autoLo + 3 + (frameMs > lastFrameMs ? 2 * frameMs : 0);
      if (autoMs > autoHi) autoMs = autoHi;
      lastFrameMs = frameMs;
      for (float &v : need) v = autoLo - 1;
    }
    // Пока пакеты теряются (за последнюю минуту кадры приходилось брать из запасных копий), запас не опускается ниже
    // того, что нужно, чтобы дождаться пакета с копиями: глубина копий + один пакет + 2 мс. До 2.73 запас за
    // минуту-другую ужимался до наименьшего, и первая же пара потерянных подряд пакетов давала провал звука, хотя
    // кадры были целы в копиях. Замер 12.10, приёмник на −77 дБм, потери 3 %: провалы в каждой пятой десятисекундке
    // при том, что пропавших кадров не было вовсе.
    {
      float lossLo = rxGapMs && msSince(rxGapMs) < 60000 ? rxCopyMs + frameMs + 2 : 0;
      if (lossLo > autoHi) lossLo = autoHi;
      if (autoMs < lossLo) autoMs = lossLo;
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
      if (run != rxDacOpen && !rxPattern) rxDacSet(run);   // ЦАП открыт, только пока идёт работа; звук при этом входит с нуля
    }
    if (rxHopCh && (rxHopNow || (int32_t)(millis() - rxHopDeadline) >= 0)) {   // переход на новый канал вместе с передатчиком
      // Свой пакет (сведения о себе) ещё у драйвера — подождать его, но не дольше 12 мс: смена канала под неушедшим
      // пакетом — подозреваемая причина «приёмник играет, а передатчик его не слышит» (см. rxSend в radio.h).
      static uint32_t hold;
      bool mine = rxTxBusy();
      if (mine && !hold) hold = millis() | 1;
      if (!mine || millis() - hold >= 12) {
        hold = 0;
        uint8_t ch = rxHopCh;
        rxHopCh = 0;
        rxHopNow = false;
        cfg.channel = ch;
        rLastChanMs = millis();
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        bbMark(BB_HOP, ch, mine ? 1 : 0);
        rxHops = rxHops + 1;
      }
    }
    uint32_t pc1 = esp_cpu_get_cycle_count();
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
      step = (uint64_t)(rxRateBase * (1.0 + (double)adj) * 4294967296.0);
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
    uint32_t pc2 = esp_cpu_get_cycle_count();
    {
      uint8_t bs = cfg.rxBoost > 12 ? 0 : cfg.rxBoost;
      if (bs != boostStep) {
        boostStep = bs;
        boostWant = powf(10.0f, bs * 0.1f);   // шаг 2 дБ
      }
      rxBoostNow = bK;
      dyn.boostDb = bs * 2.0f;
      dyn.comp = cfg.rxComp > 3 ? 1 : cfg.rxComp;
      dyn.gate = cfg.rxGate > 3 ? 0 : cfg.rxGate;
      dyn.gateThr = cfg.rxGateThr > 4 ? 1 : cfg.rxGateThr;
      dyn.gateRel = cfg.rxGateRel > 2 ? 1 : cfg.rxGateRel;
    }
    float limRel = dyn.comp && boostStep ? LIM_REL_FAST : LIM_REL;   // без усиления всё как до 2.68
    bool nsOn = cfg.rxNs >= 1 && cfg.rxNs <= 3;
    if (nsOn && !ns) {
      nsWork = (float *)heap_caps_aligned_alloc(16, 2 * NSX_N * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      ns = nsWork ? (NsSpec *)heap_caps_malloc(sizeof(NsSpec), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : nullptr;
      if (ns) ns->reset();
    }
    if (nsOn && ns && !nsWas) ns->reset();   // включили: оценка шума и очередь — с чистого листа
    nsWas = nsOn && ns;
    if (nsWas) {
      ns->level = cfg.rxNs;
      ns->sens = cfg.rxNsSens > 4 ? 2 : cfg.rxNsSens;
      if (rxNsLearnAsk) {
        rxNsLearnAsk = false;
        ns->learnStart();
      }
      if (rxNsAutoAsk) {
        rxNsAutoAsk = false;
        if (!ns->learnLeft) ns->frozen = false;
      }
      rxNsFrozen = ns->frozen;
    } else rxNsFrozen = false;
    if (eqGen != rxEqGen) {   // настройку эквалайзера сменили — пересчитать звенья (состояние остаётся: без щелчка)
      eqGen = rxEqGen;
      eq.build(cfg.rxEq, cfg.rxLowCut != 0, (float)rxOutRate);
    }
    uint32_t pc3 = esp_cpu_get_cycle_count();
    // звук — только в работе: во время заставки и сообщений тихо (выход уже включён, вход плавный)
    uint32_t nowMs = millis();
    bool ear = rxEarTest;
    if (ear && nowMs - rxEarAt >= (uint32_t)EAR_CYCLE_MS * EAR_CYCLES) {   // проверка наушников кончается сама
      rxEarTest = false;
      ear = false;
    }
    int earSide = ear ? rxEarSide(nowMs) : 0;
    float volStep = rxVolumeScaled(cfg.volume);   // шкала 0…100 % растянута на 0…предел громкости
    static float volStepWas = -1, volGainWas, earGainWas;   // powf — только когда громкость сменилась (с 3.00)
    if (volStep != volStepWas) {
      volStepWas = volStep;
      volGainWas = rxVolumeGain(volStep);
      earGainWas = rxVolumeGain(volStep < 12 ? 12 : volStep);
    }
    float want = rxMute || cfg.off || rxOtaMute || !rxSoundOn() || ear ? 0 : volGainWas;
    float earAmp = 12000.0f * earGainWas;   // тон слышен и при убранной громкости
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
    uint32_t cyc1 = esp_cpu_get_cycle_count();
    for (int i = 0; i < OUT_BLOCK; i++) {
      float y, ys = 0;   // середина и разность каналов
      uint32_t tail = rxTail;
      if (priming || (int32_t)(rxHead - tail) < (rsOk ? rxRsTaps / 2 + 2 : 4)) {
        last *= 0.9f;    // запас пуст — плавно к тишине
        y = last;
        if (!priming) under++;
        fade = 0;
      } else if (rsOk) {
        // Номер строки таблицы и доля до следующей — целыми числами (с 2.84). До этого было «frac × 128 / 2^32»
        // дробным числом: при frac от 0xFFFFFF80 оно округлялось до 128, и вторая строка бралась за краем таблицы.
        static_assert(RXRS_PH == 128, "номер строки — старшие 7 бит");
        uint32_t pi = frac >> 25;
        float a = (float)(frac & 0x1FFFFFFu) * (1.0f / 33554432.0f);
        const int TAPS = rxRsTaps;
        uint32_t base = (tail - (TAPS / 2 - 1)) & (RING - 1);
        const int16_t *pm = rxRing + base, *ps = rxRingS + base;
        int16_t tm[RXRS_MAX], ts[RXRS_MAX];
        if (base + TAPS > RING) {   // окно переходит через конец кольца (раз на 170 с лишним отсчётов) — собрать подряд
          for (int j = 0; j < TAPS; j++) {
            tm[j] = rxRing[(base + j) & (RING - 1)];
            ts[j] = rxRingS[(base + j) & (RING - 1)];
          }
          pm = tm;
          ps = ts;
        }
        rsDot(rxRsTab + pi * TAPS, a, TAPS, pm, stOut ? ps : nullptr, &y, &ys);   // rsdot.h (с 2.97)
        uint64_t acc = (uint64_t)frac + step;
        rxTail = tail + (uint32_t)(acc >> 32);
        frac = (uint32_t)acc;
        if (fade < 1) {
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
      if (rxFakeNan == 11) {   // проверка: одно «не число» на вход обработки
        rxFakeNan = 0;
        y = NAN;
      }
      // «не число» или бесконечность дальше не идут: в фильтрах с обратной связью они остались бы навсегда
      if (!(fabsf(y) < 1e6f)) {
        y = 0;
        last = 0;
        rxBadIn = rxBadIn + 1;
      }
      if (!(fabsf(ys) < 1e6f)) ys = 0;
      nsM[i] = y;
      nsS[i] = ys;
    }
    uint32_t cyc2 = esp_cpu_get_cycle_count();
    if (nsWas) {   // спектральное шумозаглушение — над целым блоком, до эквалайзера и усиления (задержка 7 мс)
      uint32_t t0 = (uint32_t)esp_timer_get_time();
      ns->runF(nsM, nsS, OUT_BLOCK, stOut, nsWork);
      uint32_t d = (uint32_t)esp_timer_get_time() - t0;
      rxNsUsSum = rxNsUsSum + d;
      rxNsCalls = rxNsCalls + 1;
      if (d > rxNsUsMax) rxNsUsMax = d;
      rxNsRedDb = ns->redDb;
      rxNsPauseHops = ns->pauseHops;
    }
    uint32_t cyc3 = esp_cpu_get_cycle_count();
    bool healAsk = false;   // в этом блоке на выходе обработки оказалось «не число» — сбросить её память
    for (int i = 0; i < OUT_BLOCK; i++) {
      float y = nsM[i], ys = nsS[i];
      if (!(fabsf(y) < 1e6f) || !(fabsf(ys) < 1e6f)) {   // шумозаглушение отдало «не число» — его память тоже под сброс
        y = ys = 0;
        healAsk = true;
      }
      // для спектра, индикатора и проверки тона — звук до громкости
      int yi = (int)y;
      if (rxSpecOn && specBuf) {
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
      {
        int a0 = (int)fabsf(y);
        if (a0 > rxEqPkIn) rxEqPkIn = a0;
      }
      if (eq.any) {   // эквалайзер и срез низов
        y = eq.run(0, y);
        if (stOut) ys = eq.run(1, ys);
      }
      {
        int a1 = (int)fabsf(y);
        if (a1 > rxEqPkOut) rxEqPkOut = a1;
      }
      clK += (clWant - clK) * 0.001f;
      float yc = y + clK * (shM.run(y) - y), ysc = stOut ? ys + clK * (shS.run(ys) - ys) : 0;
      // левый и правый; в моно оба равны середине. Ограничитель общий — по большему из двух, чтобы не сдвигать звук вбок
      float vl = (yc + ysc) * gain, vr = (yc - ysc) * gain, av = fabsf(vl) > fabsf(vr) ? fabsf(vl) : fabsf(vr);
      {   // усиление: меняется плавно — около 15 мс на шаг, без щелчка; у края шкалы и в паузах его убавляет
          // сжатие (nsfx.h, NsDyn) — оно смотрит на звук до усиления
        bK += (boostWant - bK) * 0.002f;
        dynG = dyn.run(av, (float)a, bK);
        vl *= bK;
        vr *= bK;
        av *= bK * dyn.tRed;   // каким этот отсчёт станет, когда сжатие дойдёт до своей цели
      }
      {
        float need = av > limit ? limit / av : 1.0f;   // во сколько раз надо убавить, чтобы этот отсчёт уложился
        float dl = limL[limPos], dr = limR[limPos];    // отсчёт, пришедший LIM_LA назад, — он сейчас пойдёт на выход
        limL[limPos] = vl;
        limR[limPos] = vr;
        limPos = limPos + 1 == LIM_LA ? 0 : limPos + 1;
        limT++;
        while (dqE != dqH && dqV[(dqE - 1) & (LIM_DQ - 1)] >= need) dqE--;   // наименьшее «надо убавить» по окну
        dqV[dqE & (LIM_DQ - 1)] = need;
        dqT[dqE & (LIM_DQ - 1)] = limT;
        dqE++;
        while (limT - dqT[dqH & (LIM_DQ - 1)] >= LIM_LA) dqH++;              // вышедшие из окна — из головы
        float limMin = dqV[dqH & (LIM_DQ - 1)];
        if (limMin < limR1) limR1 = limMin;            // «отпускание»: вниз — сразу, вверх — звеном
        else limR1 += (limMin - limR1) * limRel;
        limG = boxSum * (1.0f / (float)LIM_LA);               // среднее по прошлым LIM_LA значениям — до добавления нового
        boxSum += limR1 - boxBuf[boxPos];
        boxBuf[boxPos] = limR1;
        boxPos = boxPos + 1 == LIM_LA ? 0 : boxPos + 1;
        if (++boxN >= 4096) {                          // накопленную ошибку суммы — пересчётом
          boxN = 0;
          boxSum = 0;
          for (int k = 0; k < LIM_LA; k++) boxSum += boxBuf[k];
        }
        vl = dl * limG * dynG;
        vr = dr * limG * dynG;
        if (limG < 0.999f) limBusy++;
        if (limG < limLow) limLow = limG;
      }
      if (ear || earL > 1 || earR > 1) {   // проверка наушников: тон в одно ухо (при выходе «протифаза» канал один — в оба)
        earL += ((earSide == 1 || (earSide == 2 && !stOut) ? earAmp : 0) - earL) * 0.004f;
        earR += ((earSide == 2 ? earAmp : 0) - earR) * 0.004f;
        earPh += 2 * PI * 660.0f / rxOutRate;
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
      if (!(fabsf(ol) < 1e9f) || !(fabsf(orr) < 1e9f)) {   // «не число» прошло обработку: в ЦАП — ноль, память фильтров — под сброс
        ol = orr = 0;
        healAsk = true;
      }
      if (dcStart < 0) {   // первые 0,7 с после включения
        dcStart += 32768.0f / (SRATE * 0.7f);
        if (dcStart > 0) dcStart = 0;
        ol += dcStart;
        orr += dcStart;
        ol = ol < -32768 ? -32768 : ol;
        orr = orr < -32768 ? -32768 : orr;
      }
      // 24 бита на ЦАП (с 2.86): внешнему ЦАП — 32-битное слово, однобитному выходу — 16 бит С ОКРУГЛЕНИЕМ. До 2.86
      // везде шло 16 бит, и дробь отбрасывалась: на тихом месте это не шум, а искажения, идущие за звуком (синус на
      // −60 дБ получал гармоники −41 дБ к себе; с округлением −45, в 24 битах их нет вовсе — rxchain.cpp, часть 3).
      float cl = ol > 32767.0f ? 32767.0f : ol < -32768.0f ? -32768.0f : ol, cr = orr > 32767.0f ? 32767.0f : orr < -32768.0f ? -32768.0f : orr;
      out[2 * i] = (int16_t)(cl >= 0 ? cl + 0.5f : cl - 0.5f);
      out[2 * i + 1] = (int16_t)(cr >= 0 ? cr + 0.5f : cr - 0.5f);
      out32[2 * i] = (int32_t)(cl * 65536.0f);
      out32[2 * i + 1] = (int32_t)(cr * 65536.0f);
      if (rxPattern) {   // известные числа — чтобы на выводах было видно, какой бит где стоит
        out[2 * i] = (int16_t)0x5A01;
        out[2 * i + 1] = (int16_t)0xA5F3;
        out32[2 * i] = (int32_t)0x5A010000u;
        out32[2 * i + 1] = (int32_t)0xA5F30000u;
      }
    }
    uint32_t cyc4 = esp_cpu_get_cycle_count();
    {   // сторож выхода и сброс обработки (см. «ЗВУК ПРОПАЛ ДО ПЕРЕЗАГРУЗКИ» в начале файла)
      static uint16_t stuckMs;        // сколько блоков подряд (по 1 мс) выход стоял при громком звуке
      static int16_t stuckL, stuckR;
      static uint32_t healedAt;       // когда сторож сбрасывал обработку в прошлый раз
      if (rxFakeNan == 12) {          // проверка: «не число» прямо в память эквалайзера и «чёткости»
        rxFakeNan = 0;
        eq.z[0][0][0] = NAN;
        shM.z1 = NAN;
      }
      if (rxFakeNan == 13)            // проверка: выход держится на одном числе (как при «не числе» до 2.84)
        for (int i = 0; i < OUT_BLOCK * 2; i++) {
          out[i] = -1;
          out32[i] = -65536;
        }
      bool same = true;
      for (int i = 1; i < OUT_BLOCK && same; i++) same = out[2 * i] == out[0] && out[2 * i + 1] == out[1];
      // «должно звучать»: в эфире громко (−24 дБ и выше), громкость не убрана, звук уже вошёл, проверок нет
      bool loud = pk > 2000 && want > 0.01f && gain > 0.01f && !priming && fade >= 1 && dcStart >= 0 && !ear && !beep && !rxPattern;
      if (!same || (stuckMs && (out[0] != stuckL || out[1] != stuckR))) stuckMs = 0;
      else if (loud) {
        if (!stuckMs) {
          stuckL = out[0];
          stuckR = out[1];
        }
        if (stuckMs < 60000) stuckMs++;
      }
      bool stuck = stuckMs >= 1500;
      if (healAsk || stuck) {
        uint32_t nowH = millis();
        if (stuck) {
          stuckMs = 0;
          rxFxStuck = rxFxStuck + 1;
          // сброс уже был только что, а выход снова стоит — дело не в памяти фильтров: перезапуск (главный цикл)
          if (healedAt && nowH - healedAt < 4000) rxSoundRestartAsk = true;
          healedAt = nowH ? nowH : 1;
        } else rxFxNan = rxFxNan + 1;
        rxFxHealMs = nowH ? nowH : 1;
        // память всего, что помнит прошлое: фильтры, сжатие, ограничитель, плавные множители, шумозаглушение
        memset(eq.z, 0, sizeof(eq.z));
        shM.z1 = shM.z2 = shS.z1 = shS.z2 = 0;
        memset(limL, 0, sizeof(ob->limL));
        memset(limR, 0, sizeof(ob->limR));
        for (int i = 0; i < LIM_LA; i++) boxBuf[i] = 1;
        limPos = boxPos = boxN = 0;
        dqH = dqE = 0;
        limG = limR1 = 1;
        boxSum = LIM_LA;
        dyn.env = dyn.envIn = 0;
        dyn.gRed = dyn.gGate = dyn.tRed = dyn.tGate = 1;
        dyn.floorDb = -50;
        dyn.tick = dyn.hold = dyn.envHold = 0;
        dynG = 1;
        gain = 0;                 // громкость войдёт плавно
        bK = boostWant;
        clK = 0;
        balL = balR = 1;
        earL = earR = earPh = 0;
        last = prev1 = prev2 = 0;
        if (dcStart < 0 || !(dcStart == dcStart)) dcStart = 0;
        if (!(integ == integ) || !(fillSm == fillSm)) {   // и подгонка темпа, если «не число» добралось до неё
          integ = 0;
          fillSm = (float)(rxHead - rxTail);
          step = (uint64_t)(rxRateBase * 4294967296.0);
        }
        if (ns) ns->reset();
        for (int i = 0; i < OUT_BLOCK * 2; i++) {   // этот блок — тишина
          out[i] = 0;
          out32[i] = 0;
        }
      }
    }
    rxPeak = pk > 32767 ? 32767 : pk;
    if (pk > 300 && !(rxFlags & FLAG_MUTE)) rxSoundMs = millis() ? millis() : 1;   // −40 дБ и громче — «в эфире звук»
    rxPeakL = pkL > 32767 ? 32767 : pkL;
    rxPeakR = pkR > 32767 ? 32767 : pkR;
    if (dyn.redLowDb > rxDynRedDb) rxDynRedDb = dyn.redLowDb;   // читающий (команда ?) обнуляет
    if (dyn.gateLowDb > rxGateRedDb) rxGateRedDb = dyn.gateLowDb;
    dyn.redLowDb = 0;
    dyn.gateLowDb = 0;
    rxGateFloorDb = dyn.floorDb;
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
    {
      static uint64_t stC[9];
      static uint32_t stMax, stN;
      uint32_t cyc5 = esp_cpu_get_cycle_count();
      stC[0] += cyc1 - cyc0;
      stC[1] += cyc2 - cyc1;
      stC[2] += cyc3 - cyc2;
      stC[3] += cyc4 - cyc3;
      stC[4] += cyc5 - cyc4;
      stC[5] += pc1 - cyc0;
      stC[6] += pc2 - pc1;
      stC[7] += pc3 - pc2;
      stC[8] += cyc1 - pc3;
      if (cyc5 - cyc0 > stMax) stMax = cyc5 - cyc0;
      stN++;
      if (rxStAsk) {
        for (int k = 0; k < 9; k++) {
          rxStRes[k] = (uint32_t)(stC[k] / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
          stC[k] = 0;
        }
        rxStResMax = stMax / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
        rxStResN = stN;
        stMax = stN = 0;
        rxStAsk = false;
        rxStReady = true;
      }
    }
    size_t w;
    if (wide) i2s_channel_write(rxI2s, out32, sizeof(ob->out32), &w, portMAX_DELAY);
    else i2s_channel_write(rxI2s, out, sizeof(out), &w, portMAX_DELAY);
    rxOutSamples = rxOutSamples + OUT_BLOCK;
  }
}

// Запись настроек во флеш на 8–120 мс останавливает и приём, и выход звука (у выхода I2S запас 4 мс, до 2.92 — 2) — в наушниках
// щелчок или провал. Поэтому несрочную запись (ручку повернули, настройку сменили с передатчика) откладываем до паузы:
// 0,15 с принятый звук тише −40 дБ или звука нет вовсе. Дольше maxMs не ждём.
static bool rxQuietToSave(uint32_t dueMs, uint32_t maxMs) {
  uint32_t now = millis();
  if ((int32_t)(now - dueMs) < 0) return false;
  if (now - dueMs >= maxMs) return true;
  return !rxPlaying || !rxSoundMs || msSince(rxSoundMs, now) > 150;
}
