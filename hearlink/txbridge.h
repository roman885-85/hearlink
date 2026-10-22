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
  bp_status_t st;                 // последний стан моста
  volatile uint8_t gen;           // проверочный сигнал моста вместо входа (BP_GEN_*; не запоминается — с порта C3…C6)
  volatile uint32_t lastMs;       // когда он пришёл (0 — ни разу)
  uint32_t frames, bad, sent;     // кадров стану принято / испорчено; команд послано
  uint8_t seq;                    // номер последней команды (мост повторяет его в стане)
  volatile bool kick;             // вход сменили — послать команду сейчас, не дожидаясь секунды
};
static TxBridge txBr;
// мост на связи: годный стан был за последние полторы секунды
static bool txBridgeOk() {
  uint32_t t = txBr.lastMs;
  return t && millis() - t < 1500;
}

static void txBridgeSend(uint8_t cmd, const uint8_t *d, size_t n) {
  uint8_t f[24];
  size_t m = bp_frame(cmd, d, n, f);
  hwSerial.write(f, m);   // мимо копии в «налагодження ефіром»: двоичному кадру там не место
  txBr.sent++;
}

static void txBridgeFrame(const uint8_t *enc, int n) {
  uint8_t raw[64];
  size_t m = n <= (int)sizeof(raw) ? bp_cobs_dec(enc, n, raw) : 0;
  if (m != sizeof(bp_status_t) + 2 || raw[0] != BP_ST_STATUS || bp_crc16(raw, sizeof(bp_status_t), 0xFFFF) != (uint16_t)(raw[m - 2] | (raw[m - 1] << 8))) {
    txBr.bad++;
    return;
  }
  memcpy(&txBr.st, raw, sizeof(bp_status_t));
  txBr.lastMs = millis() | 1;
  txBr.frames++;
}

// Идёт обновление моста (txbrupd.h): на выводе 40 ответы его загрузчика, а не звук — звуковая задача глушит вход.
static volatile bool txBrMute;   // звук со входа — тишиной (на выводе 40 сейчас ответы загрузчика, а не звук)
