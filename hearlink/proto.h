// Пакет набора, качество звука и сжатие.
//
// В каждом пакете — текущий кадр звука и один-два предыдущих, сжатых IMA ADPCM (4 бита на отсчёт).
// Потерялись один или два пакета подряд — их кадры достаются из следующего.
// (Сначала копия была одна; днём при занятом эфире терялось по два пакета подряд — до 6 кадров в секунду.)
//
// Качество звука выбирается на передатчике. Чем оно ниже, тем меньше поток: пакеты реже, копии перекрывают
// более долгую помеху, и поток помещается в медленные, дальнобойные скорости радио. Плата — задержка:
// пакет нельзя отправить, пока не набран весь его кадр.
//   найвища    — 32 кГц без сжатия,  кадр 2 мс,  две копии: 212 байт × 500 в секунду
//   стандартна — 32 кГц ADPCM,       кадр 4 мс,  две копии: 215 байт × 250
//   мова       — 16 кГц ADPCM,       кадр 8 мс,  две копии: 215 байт × 125
//   дальня     — 16 кГц ADPCM,       кадр 12 мс, одна копия: 212 байт × 83
// Приёмник узнаёт качество из заголовка пакета и на выходе всегда играет 32 кГц.
//
// Стерео. В эфир идёт «середина» (Л+П)/2 — тот же звук, что в моно, — и следом за ней «разность» (Л−П)/2,
// сжатая ADPCM. Приёмник собирает Л = середина + разность, П = середина − разность; приёмник в режиме моно
// разность просто не трогает. Запасные копии несут только середину: восстановленный кадр звучит в моно.
// В стерео (длины — без подписи, см. ниже):
//   найвища    — середина без сжатия + разность + одна копия: 212 байт (до версии 1.8 копий было две, 247 байт)
//   стандартна — середина + разность + одна копия (в моно две): 215 байт
//   мова       — то же, 215 байт
//   дальня     — только моно
// Признак стерео — старший бит байта q; моно-пакеты от этого не изменились, их понимает и прошивка 1.0.
//
// Защита (протокол 4, см. sec.h): всё после заголовка зашифровано ключом набора, в конце пакета — подпись SEC_TAG
// байт; заголовок идёт открыто, но тоже подписан.
// Пакет ESP-NOW не длиннее 250 байт. «Найвища» в стерео с двумя копиями и подписью вышла бы 255 — ESP-NOW версии 2
// такие пакеты формально умеет, но на плате (07.10, ядро 3.3.3) передатчик слал их в 2,5 раза медленнее, через
// несколько секунд драйвер замолчал, а после перезапуска радио отвергал всё подряд. Поэтому в «найвищій» стерео
// копия одна: 14 + 128 + 35 + 35 + 8 = 220 байт (в моно по-прежнему две: 220 байт).
#pragma once
#include <stddef.h>
#include <string.h>
#include "config.h"
#include "utf8.h"

#define SEC_TAG 8   // байт подписи в конце каждого пакета

struct __attribute__((packed)) Hdr {
  uint16_t magic;
  uint8_t ver;
  uint8_t kit;      // номер набора
  uint32_t boot;    // «эпоха»: номер включения передатчика, растёт с каждым запуском (хранится в плате) — см. sec.h
  uint32_t seq;     // номер пакета
  uint8_t flags;    // см. ниже
  uint8_t q;        // качество звука, Q_… (младшие биты) и признак стерео Q_STEREO
};
#define Q_STEREO 0x80
#define Q_LANG_EN 0x40   // язык надписей передатчика — английский: приёмники берут язык отсюда (к звуку не относится)
#define Q_MASK 0x0F
#define FLAG_TONE 0x01   // в пакете пробный тон
#define FLAG_CLIP 0x02   // вход передатчика перегружен
#define FLAG_MUTE 0x04   // на передатчике включена тишина: пакеты идут, звука в них нет
#define FLAG_THIN 0x08   // эфир занят, и передатчик нарочно пропустил пакеты перед этим: их кадры — в запасных копиях, это не потери
// старшие четыре бита flags — номер канала, на котором работает передатчик: соседние каналы Wi-Fi
// перекрываются, и приёмник может услышать пакет, стоя на соседнем канале. По этому числу он встаёт точно.

struct QDef {
  uint16_t sr;       // отсчётов в секунду в эфире
  uint16_t n;        // отсчётов в пакете
  uint8_t pcm;       // текущий кадр идёт без сжатия
  uint8_t copies;    // сколько прежних кадров повторяется
  uint8_t chunks;    // из скольких кусков по 2 мс набирается пакет
  uint8_t copiesSt;  // сколько копий в стерео; 255 — стерео в этом качестве нет
};
static const QDef QDEF[Q_COUNT] = {
  { 32000, 64, 1, 2, 1, 1 },   // в стерео одна копия: с подписью пакет должен уместиться в 250 байт (см. выше)
  { 32000, 128, 0, 2, 2, 1 },
  { 16000, 128, 0, 2, 4, 1 },
  { 16000, 192, 0, 1, 6, 255 },
};
static const char *const Q_NAME[Q_COUNT] = { "найвища", "стандартна", "мова", "дальня" };
#define Q_MAX_N 192                         // самый длинный кадр, отсчётов
#define Q_MAX_COPIES 2
#define PKT_MAX 250                         // самый длинный пакет, байт (это и предел ESP-NOW)
// сжатый кадр: первый отсчёт (2 байта), шаг (1 байт) и по 4 бита на отсчёт
static inline int qBlock(int q) { return 3 + QDEF[q].n / 2; }
static inline int qMainLen(int q) { return QDEF[q].pcm ? QDEF[q].n * 2 : qBlock(q); }
static inline bool qStereoOk(int q) { return QDEF[q].copiesSt != 255; }
static inline int qCopies(int q, bool st) { return st ? QDEF[q].copiesSt : QDEF[q].copies; }
static inline int qPktLen(int q, bool st = false) { return sizeof(Hdr) + qMainLen(q) + (st ? qBlock(q) : 0) + qCopies(q, st) * qBlock(q) + SEC_TAG; }
static inline uint32_t qFrameUs(int q) { return (uint32_t)QDEF[q].n * 1000000u / QDEF[q].sr; }

// ---- обратная связь: приёмник раз в две секунды коротко сообщает о себе, передатчик может дать ему команду.
// Пакеты крошечные и редкие — эфир они не занимают.
#define MAGIC_STATUS 0x5348    // «HS»
#define MAGIC_COMMAND 0x4348   // «HC»
#define NAME_LEN 28            // байтов под имя приёмника в сообщениях прежнего вида (UTF-8, с нулём): 13 кириллических букв
// С версии 2.19 имя длиннее (владелец 07.10: «при вводе имени приемника присутствует сильное ограничение по длине
// имени»): до 32 букв. Чтобы приёмники с прошивкой до 2.19 не «оглохли» к командам, прежние сообщения не тронуты, а
// длинное имя ходит в сообщениях того же вида, но с длинным полем (RxStatus2, TxCommand2) — их отличает длина пакета:
//  — приёмник 2.19+ сообщает о себе всегда «длинным» сообщением; передатчик понимает оба и по длине узнаёт, какому
//    приёмнику можно слать длинное имя;
//  — команды передатчик шлёт прежние; «длинная» — только смена имени приёмнику, который её поймёт.
#define NAME_LONG 66           // байтов под длинное имя: 32 кириллические буквы и ноль
#define NAME_LETTERS 32


struct __attribute__((packed)) RxStatus {     // приёмник → передатчик
  uint16_t magic;
  uint8_t ver, kit;
  uint8_t id[3];        // три последних байта адреса приёмника — его постоянный номер
  // последний услышанный пакет звука. По ним передатчик видит, что сведения свежие (а не записаны и повторены),
  // и они же — одноразовое число шифра
  uint32_t echoEpoch, echoSeq;
  // ---- отсюда зашифровано
  uint8_t volume;       // 0–20
  uint8_t flags;        // ST_…
  int8_t rssi;          // как приёмник слышит передатчик, дБм
  uint8_t depthMs;      // запас, мс
  uint8_t lossPm;       // потеряно пакетов за 2 с, десятые доли процента (до 25,5 %)
  uint8_t lostFrames;   // кадров, которые не удалось восстановить, за 2 с
  uint8_t underMs;      // провалов звука за 2 с, мс
  uint16_t uptimeMin;
  uint8_t fw;           // версия ×10
  char name[NAME_LEN];
  uint8_t tag[SEC_TAG];
};
#define ST_CLEAR offsetof(RxStatus, volume)
#define ST_BODY (offsetof(RxStatus, tag) - offsetof(RxStatus, volume))
struct __attribute__((packed)) RxStatus2 {    // то же с длинным именем (приёмники с версии 2.19)
  uint16_t magic;
  uint8_t ver, kit;
  uint8_t id[3];
  uint32_t echoEpoch, echoSeq;
  uint8_t volume, flags;
  int8_t rssi;
  uint8_t depthMs, lossPm, lostFrames, underMs;
  uint16_t uptimeMin;
  uint8_t fw;
  char name[NAME_LONG];
  uint8_t tag[SEC_TAG];
};
#define ST2_BODY (offsetof(RxStatus2, tag) - offsetof(RxStatus2, volume))
#define ST_MUTE 0x01      // тишина включена ручкой
#define ST_OFF 0x02       // приёмник выключен с передатчика
#define ST_SCREEN 0x04    // экран найден
#define ST_PLAYING 0x08   // звук идёт
#define ST_SLEEP 0x20     // приёмник спит (выключен с передатчика) или уходит в сон
#define ST_OUTLIVE 0x40   // на выводах выхода идёт однобитный сигнал — приёмник сам смотрит свои выводы (с версии 1.6)
#define ST_STEREO 0x10    // выход приёмника — стерео (левый и правый), а не моно в противофазе

struct __attribute__((packed)) TxCommand {    // передатчик → приёмник
  uint16_t magic;
  uint8_t ver, kit;
  uint8_t id[3];        // кому
  uint32_t epoch, ctr;  // включение передатчика и номер команды: приёмник выполняет команду один раз и только свежую
  // ---- отсюда зашифровано
  uint8_t cmd;          // CMD_…
  uint8_t arg;
  char name[NAME_LEN];  // для CMD_NAME
  uint8_t tag[SEC_TAG];
};
#define CMD_CLEAR offsetof(TxCommand, cmd)
#define CMD_BODY (offsetof(TxCommand, tag) - offsetof(TxCommand, cmd))
struct __attribute__((packed)) TxCommand2 {   // то же с длинным именем (смена имени приёмнику с версии 2.19)
  uint16_t magic;
  uint8_t ver, kit;
  uint8_t id[3];
  uint32_t epoch, ctr;
  uint8_t cmd, arg;
  char name[NAME_LONG];
  uint8_t tag[SEC_TAG];
};
#define CMD2_BODY (offsetof(TxCommand2, tag) - offsetof(TxCommand2, cmd))

// ---- подключение приёмника к набору (см. sec.h). Просьба идёт открыто: в ней нет тайн, только открытый ключ.
#define MAGIC_PAIRREQ 0x5048   // «HP»
#define MAGIC_PAIRRSP 0x5148   // «HQ»
#define PAIR_PUB 32
struct __attribute__((packed)) PairReq {      // приёмник без ключа → передатчик
  uint16_t magic;
  uint8_t ver;
  uint8_t id[3];
  uint8_t pub[PAIR_PUB];
  char name[NAME_LEN];
};
struct __attribute__((packed)) PairRsp {      // передатчик → приёмник, которого оператор допустил
  uint16_t magic;
  uint8_t ver;
  uint8_t id[3];
  uint8_t pub[PAIR_PUB];
  uint8_t box[32 + 1 + 4 + 4];   // зашифровано общим секретом: ключ набора, номер набора, эпоха, поколение ключа
  uint8_t tag[SEC_TAG];
};

// ---- смена ключа набора (приёмник удалили из набора — см. sec.h). У каждого приёмника есть личный ключ, выведенный
// при подключении; новый ключ набора передатчик шлёт каждому оставшемуся приёмнику под его личным ключом.
#define MAGIC_KEYMSG 0x4B48    // «HK»
#define MAGIC_KEYREQ 0x5248    // «HR»
struct __attribute__((packed)) KeyMsg {       // передатчик → приёмник: новый ключ набора
  uint16_t magic;
  uint8_t ver;
  uint8_t id[3];
  uint32_t gen;              // поколение ключа: приёмник принимает только более новое
  uint8_t box[32 + 1 + 4];   // зашифровано личным ключом приёмника: ключ набора, номер набора, эпоха
  uint8_t tag[SEC_TAG];
};
#define KEYMSG_CLEAR offsetof(KeyMsg, box)
struct __attribute__((packed)) KeyReq {       // приёмник → передатчик: «слышу тебя, но ключ набора у меня старый»
  uint16_t magic;
  uint8_t ver;
  uint8_t id[3];
  uint32_t gen, rnd;         // какое поколение у приёмника; случайное число
  uint8_t tag[SEC_TAG];      // подпись личным ключом приёмника
};
#define KEYREQ_CLEAR offsetof(KeyReq, tag)
enum { CMD_IDENTIFY = 1, CMD_NAME = 2, CMD_ENABLE = 3, CMD_VOLUME = 4, CMD_MUTE = 5, CMD_STEREO = 6, CMD_DACTEST = 7,
       CMD_FORGET = 8,     // приёмник удалён из набора: стереть ключи (он снова станет «не підключено»)
       CMD_HOP = 9,        // всем приёмникам сразу (номер FF FF FF): передатчик уходит на канал arg с пакета номер name[0..3]
       CMD_SET = 10,       // настройка «для слуха и удобства» (с 2.32): arg = номер настройки RXP_… × 32 + значение 0…31
       CMD_EARTEST = 11 }; // проверка наушников: arg 1 — начать, 0 — прекратить (с 2.32)

// Настройки приёмника, которые можно менять и его ручкой, и с передатчика (с версии 2.32). По радио значение идёт
// числом 0…31; баланс (−5…+5) — со сдвигом на 5.
enum { RXP_CLARITY = 0, RXP_BALANCE, RXP_VOLMAX, RXP_VIEW, RXP_LED, RXP_LANG, RXP_COUNT };
// О них приёмник сообщает передатчику отдельным коротким сообщением вслед за сведениями о себе (тот же признак
// MAGIC_STATUS, другая длина). Передатчик с прошивкой до 2.32 такую длину не знает и молча пропускает — прежние
// сообщения не тронуты, поэтому старый передатчик видит новый приёмник как раньше.
struct __attribute__((packed)) RxInfo {
  uint16_t magic;
  uint8_t ver, kit;
  uint8_t id[3];
  uint32_t echoEpoch, echoSeq;
  // ---- отсюда зашифровано
  uint8_t p[RXP_COUNT];   // значения настроек в том же виде, что и в команде CMD_SET
  uint8_t earTest;        // идёт проверка наушников
  uint8_t spare[5];
  uint8_t tag[SEC_TAG];
};
#define INFO_BODY (offsetof(RxInfo, tag) - offsetof(RxInfo, p))

static const int8_t IMA_IDX[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static const int16_t IMA_STEP[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
  130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
  1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
  7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

struct Adpcm {
  int pred = 0, idx = 0;
};

static inline int adpcmStep(Adpcm &s, uint8_t code) {
  int step = IMA_STEP[s.idx], d = step >> 3;
  if (code & 4) d += step;
  if (code & 2) d += step >> 1;
  if (code & 1) d += step >> 2;
  s.pred += (code & 8) ? -d : d;
  if (s.pred > 32767) s.pred = 32767;
  if (s.pred < -32768) s.pred = -32768;
  s.idx += IMA_IDX[code & 7];
  if (s.idx < 0) s.idx = 0;
  if (s.idx > 88) s.idx = 88;
  return s.pred;
}

static inline uint8_t adpcmCode(Adpcm &s, int x) {
  int step = IMA_STEP[s.idx], diff = x - s.pred;
  uint8_t code = 0;
  if (diff < 0) {
    code = 8;
    diff = -diff;
  }
  if (diff >= step) {
    code |= 4;
    diff -= step;
  }
  if (diff >= step >> 1) {
    code |= 2;
    diff -= step >> 1;
  }
  if (diff >= step >> 2) code |= 1;
  adpcmStep(s, code);
  return code;
}

// Сжать кадр из n отсчётов. Начальное состояние (первый отсчёт и шаг) пишется в начало, шаг переходит
// на следующий кадр.
static inline void adpcmEncodeBlock(const int16_t *x, int n, uint8_t *out, int &idxCarry) {
  Adpcm s;
  s.pred = x[0];
  s.idx = idxCarry;
  out[0] = (uint8_t)x[0];
  out[1] = (uint8_t)((uint16_t)x[0] >> 8);
  out[2] = (uint8_t)idxCarry;
  for (int i = 0; i < n; i += 2) out[3 + i / 2] = adpcmCode(s, x[i]) | (adpcmCode(s, x[i + 1]) << 4);
  idxCarry = s.idx;
}

// То же, но с подбором начального шага. ADPCM после тишины держит шаг мелким и на резком начале звука
// несколько отсчётов «не успевает» — в стерео это слышно как щелчок в соседнем канале (замер 06.10: −28 дБ).
// Шаг каждого кадра всё равно передаётся в его начале, поэтому передатчик волен взять не тот, что остался
// от прошлого кадра, а тот из нескольких, с которым кадр выходит точнее. Приёмнику разницы нет.
static inline void adpcmEncodeBlockBest(const int16_t *x, int n, uint8_t *out, int &idxCarry) {
  static const int8_t TRY[] = { 0, 8, 16, 24, 36, 48, -8 };   // набор подобран на гудках с резким началом: просачивание −9 → −38 дБ
  uint8_t tmp[3 + Q_MAX_N / 2];
  int64_t bestErr = -1, energy = 0;
  int bestEnd = idxCarry;
  for (int i = 0; i < n; i++) energy += (int32_t)x[i] * x[i];
  for (int t = 0; t < (int)sizeof(TRY); t++) {
    int i0 = idxCarry + TRY[t];
    if (i0 < 0 || i0 > 88) continue;
    Adpcm s;
    s.pred = x[0];
    s.idx = i0;
    int64_t err = 0;
    for (int i = 0; i < n; i += 2) {
      uint8_t a = adpcmCode(s, x[i]);
      int e = x[i] - s.pred;
      err += (int64_t)e * e;
      uint8_t b = adpcmCode(s, x[i + 1]);
      e = x[i + 1] - s.pred;
      err += (int64_t)e * e;
      tmp[3 + i / 2] = a | (b << 4);
    }
    if (bestErr < 0 || err < bestErr) {
      bestErr = err;
      bestEnd = s.idx;
      tmp[2] = (uint8_t)i0;
      memcpy(out, tmp, 3 + n / 2);
    }
    // обычный кадр выходит хорошо с первого раза (шум ниже звука на 30 дБ или сам звук очень тихий) — дальше не ищем:
    // перебор нужен только на резких началах, и в среднем кодер почти не дорожает
    if (!t && (err * 1000 < energy || err < (int64_t)n * 64)) break;
  }
  out[0] = (uint8_t)x[0];
  out[1] = (uint8_t)((uint16_t)x[0] >> 8);
  idxCarry = bestEnd;
}

static inline void adpcmDecodeBlock(const uint8_t *in, int n, int16_t *x) {
  Adpcm s;
  s.pred = (int16_t)(in[0] | (in[1] << 8));
  s.idx = in[2] > 88 ? 88 : in[2];
  for (int i = 0; i < n; i += 2) {
    x[i] = adpcmStep(s, in[3 + i / 2] & 15);
    x[i + 1] = adpcmStep(s, in[3 + i / 2] >> 4);
  }
}

// ---- переход 32 кГц ↔ 16 кГц для качества «мова» и «дальня».
// Один полуполосный фильтр на обе стороны: ровно до 6 кГц, −3 дБ на 7,5 кГц, −35 дБ на 10 кГц и глубже дальше.
static const float HB[6] = { 0.624812f, -0.178455f, 0.077611f, -0.032923f, 0.011577f, -0.002621f };

struct Decim2 {          // передатчик: 2n отсчётов 32 кГц → n отсчётов 16 кГц
  int16_t hist[22] = {};
  void run(const int16_t *x, int n, int16_t *y) {
    static int16_t z[22 + Q_MAX_N * 2];
    memcpy(z, hist, sizeof(hist));
    memcpy(z + 22, x, n * 2 * sizeof(int16_t));
    for (int m = 0; m < n; m++) {
      const int16_t *c = z + 2 * m + 11;
      float v = c[0];
      for (int k = 0; k < 6; k++) v += HB[k] * (c[-(2 * k + 1)] + c[2 * k + 1]);
      v *= 0.5f;
      y[m] = v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)lrintf(v);
    }
    memcpy(hist, z + 2 * n, sizeof(hist));
  }
};

struct Interp2 {         // приёмник: n отсчётов 16 кГц → 2n отсчётов 32 кГц
  int16_t h[12] = {};
  void reset() { memset(h, 0, sizeof(h)); }
  void run(const int16_t *x, int n, int16_t *y) {
    for (int i = 0; i < n; i++) {
      memmove(h, h + 1, 11 * sizeof(int16_t));
      h[11] = x[i];
      float v = 0;
      for (int k = 0; k < 6; k++) v += HB[k] * (h[5 - k] + h[6 + k]);
      y[2 * i] = h[5];
      y[2 * i + 1] = v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)lrintf(v);
    }
  }
};
