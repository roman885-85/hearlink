// Передатчик 4.x («Next Edition»): связь с мостом на Raspberry Pi Pico (прошивка — bridge/, обмен — bridge_proto.h).
// Мост отдаёт звук по I2S (вывод 40) — аналоговый вход (PCM1808) или цифровой (AES3 с пульта) — и в битах 7…4 слов I2S
// несёт свой обратный канал: кадр стану каждые 50 мс. Передатчик командует мостом кадрами A7 5B… по своему порту
// (TXD — вывод 43): главного такта на этом выводе больше нет, такт PCM1808 даёт сам мост, поэтому порт передатчика
// теперь работает всегда. Строки журнала в том же порту мост пропускает (ищет начало кадра и сверяет CRC).
#pragma once
#include "bridge_proto.h"

// Звуковая задача (txReadI2s) кладёт сюда байт обратного канала на каждый кадр I2S: старший полубайт — биты 7…4 левого
// слова, младший — правого. Разбирает главный цикл (txBridgeTick); 16 КБ — полсекунды запаса: главный цикл стоит, пока
// в порт уходит длинный ответ (P — 2 КБ, это 180 мс на 115 200), и при 4 КБ (128 мс) кольцо переполнялось.
#define BR_RING 16384
static uint8_t brRing[BR_RING];
static volatile uint32_t brPut, brGot, brLost;
static inline void IRAM_ATTR txBridgeFeed(const int32_t *raw, int frames) {
  uint32_t p = brPut;
  for (int i = 0; i < frames; i++)
    brRing[(p + i) & (BR_RING - 1)] = (uint8_t)(((uint32_t)raw[2 * i] & 0xF0u) | (((uint32_t)raw[2 * i + 1] >> 4) & 0x0Fu));
  brPut = p + frames;
}

struct TxBridge {
  bp_status_t st;                 // последний стан моста (от сборок до 22.10 — без новых полей: там нули)
  volatile uint8_t gen;           // проверочный сигнал моста вместо входа (BP_GEN_*; не запоминается — с порта C3…C6)
  volatile uint32_t lastMs;       // когда он пришёл (0 — ни разу)
  uint32_t frames, bad, sent;     // кадров стану принято / испорчено; команд послано
  uint8_t seq;                    // номер последней команды (мост повторяет его в стане)
  volatile bool kick;             // вход сменили — послать команду сейчас, не дожидаясь секунды
  bool newer;                     // мост сборки 22.10+: в стане есть CRC программы, журнал и кнопка
  // журнал моста: записи с номера logNext ещё не забраны (в журнал на карту); C8 — печать в порт до prnTo
  uint32_t logNext, prnNext, prnTo, logAskMs, logGot;
  bool logKnown;
  // кнопка USR моста: её счётчик из стана; btnWant — какой вход она попросила (выполняет экран, как свою кнопку входа)
  uint8_t btnSeq;
  bool btnKnown;
  volatile uint8_t btnWant;
};
static TxBridge txBr;

// Записи журнала моста для журнала на карте (hearlink.ino, logEmit): очередь главного цикла (он же их и забирает).
#define BRLOG_Q 32
static bp_log_rec_t brLogQ[BRLOG_Q];
static uint32_t brLogQn[BRLOG_Q];   // номера записей
static uint32_t brLogPut, brLogGot, brLogDrop;

// Запись журнала моста строкой: «міст #N [запуск B, ЧЧ:ММ:СС.мс] текст»
static int txBridgeLogLine(uint32_t num, const bp_log_rec_t &r, char *o, int cap) {
  char t[120];
  bp_log_text(&r, t, sizeof(t));
  unsigned long sec = r.ms / 1000u;
  return snprintf(o, cap, "міст #%lu [запуск %u, %02lu:%02lu:%02lu.%03lu] %s", (unsigned long)num, (unsigned)r.boot, sec / 3600, sec / 60 % 60,
                  sec % 60, (unsigned long)(r.ms % 1000u), t);
}

// Записи берутся строго по порядку номеров. Мост шлёт их потоком (после BP_LOG — и новые, по мере появления), и
// пропавший кадр (испорченный или не вместившийся в кольцо) иначе оставлял дыру: 22.10 в журнале карты не было записи
// #23 (в C8 она была). Номер дальше ожидаемого — разрыв: такие кадры пропустить и вскоре спросить заново с нужного.
static void txBridgeLogGap() {
  uint32_t now = millis();
  if (now - txBr.logAskMs > 300) txBr.logAskMs = now - 1700;   // повтор через 0,3 с (кадры «из старого потока» ещё идут)
}
static void txBridgeLogRec(uint32_t num, const bp_log_rec_t &r) {
  txBr.logGot++;
  if (txBr.prnTo && num > txBr.prnNext) txBridgeLogGap();
  if (txBr.prnTo && num == txBr.prnNext) {   // C8: в порт
    char line[180];
    txBridgeLogLine(num, r, line, sizeof(line));
    Serial.println(line);
    txBr.prnNext = num + 1;
    if (txBr.prnNext >= txBr.prnTo) {
      Serial.printf("міст: кінець журналу (%u записів)\n", (unsigned)(txBr.prnTo > BP_LOG_KEEP ? BP_LOG_KEEP : txBr.prnTo));
      txBr.prnTo = 0;
    }
    txBr.logAskMs = 0;   // следующую порцию — сразу
  }
  if (num > txBr.logNext) txBridgeLogGap();
  if (num == txBr.logNext) {   // новое — в журнал на карту
    if (brLogPut - brLogGot >= BRLOG_Q) {
      brLogGot++;
      brLogDrop++;
    }
    brLogQ[brLogPut % BRLOG_Q] = r;
    brLogQn[brLogPut % BRLOG_Q] = num;
    brLogPut++;
    txBr.logNext = num + 1;
  }
}
// мост на связи: годный стан был за последние полторы секунды
static bool txBridgeOk() {
  uint32_t t = txBr.lastMs;
  return t && msSince(t) < 1500;   // msSince: метка «из будущего» (millis() | 1 в ту же мс) — это «только что», а не «давно»
}

static void txBridgeSend(uint8_t cmd, const uint8_t *d, size_t n) {
  uint8_t f[24];
  size_t m = bp_frame(cmd, d, n, f);
  hwSerial.write(f, m);   // мимо копии в «налагодження ефіром»: двоичному кадру там не место
  txBr.sent++;
}

// Кадр обратного канала: стан (50 мс; у сборок до 22.10 — короче, без новых полей) или записи журнала.
static void txBridgeFrame(const uint8_t *enc, int n) {
  uint8_t raw[160];
  size_t m = n <= (int)sizeof(raw) ? bp_cobs_dec(enc, n, raw) : 0;
  if (m < 3 || bp_crc16(raw, m - 2, 0xFFFF) != (uint16_t)(raw[m - 2] | (raw[m - 1] << 8))) {
    txBr.bad++;
    return;
  }
  m -= 2;
  if (raw[0] == BP_ST_STATUS && m >= BP_STATUS_V1 && m <= sizeof(bp_status_t)) {
    bp_status_t s;
    memset(&s, 0, sizeof(s));
    memcpy(&s, raw, m);
    bool newer = m == sizeof(bp_status_t);
    if (newer) {
      // кнопка USR: после запуска моста (и первого знакомства) счётчик только запомнить
      if (!txBr.btnKnown || !txBr.newer || s.boot != txBr.st.boot) {
        txBr.btnSeq = s.btnSeq;
        txBr.btnKnown = true;
      } else if (s.btnSeq != txBr.btnSeq) {
        txBr.btnSeq = s.btnSeq;
        if (s.btnSrc == BP_SRC_AES || s.btnSrc == BP_SRC_PCM) txBr.btnWant = s.btnSrc;
      }
      // журнал: при первом знакомстве — последние 16 записей (что было с мостом перед запуском передатчика);
      // журнал моста начался заново (у него пропадало питание) — с начала
      if (!txBr.logKnown || s.logHead < txBr.logNext) {
        txBr.logNext = txBr.logKnown ? 0 : (s.logHead > 16 ? s.logHead - 16 : 0);
        txBr.logKnown = true;
      }
    }
    txBr.newer = newer;
    txBr.st = s;
    uint32_t t = millis();
    txBr.lastMs = t ? t : 1;   // не «| 1»: метка на 1 мс впереди часов при беззнаковом вычитании — «давно» (см. txBridgeOk)
    txBr.frames++;
    return;
  }
  if (raw[0] == BP_ST_LOG && m >= sizeof(bp_log_hdr_t)) {
    bp_log_hdr_t h;
    memcpy(&h, raw, sizeof(h));
    if (h.n > BP_LOG_PER || m != sizeof(h) + h.n * sizeof(bp_log_rec_t)) {
      txBr.bad++;
      return;
    }
    for (int i = 0; i < h.n; i++) {
      bp_log_rec_t r;
      memcpy(&r, raw + sizeof(h) + i * sizeof(r), sizeof(r));
      txBridgeLogRec(h.first + i, r);
    }
    return;
  }
  txBr.bad++;
}

// Идёт обновление моста (txbrupd.h): на выводе 40 ответы его загрузчика, а не звук — звуковая задача глушит вход.
static volatile bool txBrMute;   // звук со входа — тишиной (на выводе 40 сейчас ответы загрузчика, а не звук)
