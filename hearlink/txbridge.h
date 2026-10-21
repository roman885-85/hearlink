// Передатчик 4.x («Next Edition»): связь с мостом на Raspberry Pi Pico (прошивка — bridge/, обмен — bridge_proto.h).
// Мост отдаёт звук по I2S (вывод 40) — аналоговый вход (PCM1808) или цифровой (AES3 с пульта) — и в битах 7…4 слов I2S
// несёт свой обратный канал: кадр стану каждые 50 мс. Передатчик командует мостом кадрами A7 5B… по своему порту
// (TXD — вывод 43): главного такта на этом выводе больше нет, такт PCM1808 даёт сам мост, поэтому порт передатчика
// теперь работает всегда. Строки журнала в том же порту мост пропускает (ищет начало кадра и сверяет CRC).
#pragma once
#include "bridge_proto.h"

// Звуковая задача (txReadI2s) кладёт сюда байт обратного канала на каждый кадр I2S: старший полубайт — биты 7…4 левого
// слова, младший — правого. Разбирает главный цикл (txBridgeTick); 4096 байт — 128 мс запаса, если он задержится.
#define BR_RING 4096
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

// Главный цикл: разобрать обратный канал и раз в секунду (или сразу после смены входа) напомнить мосту, какой вход играть.
static void txBridgeTick() {
  static uint8_t enc[64];
  static int en = -1;   // −1 — ждём нуля-разделителя (начало потока или потеря)
  uint32_t put = brPut, got = brGot;
  if (put - got > BR_RING) {   // главный цикл отстал больше чем на 128 мс — пропущенное не разобрать
    brLost = brLost + (put - got - BR_RING);
    got = put - BR_RING;
    en = -1;
  }
  while (got != put) {
    uint8_t b = brRing[got & (BR_RING - 1)];
    got++;
    if (!b) {
      if (en > 0) txBridgeFrame(enc, en);
      en = 0;
    } else if (en >= 0 && en < (int)sizeof(enc)) enc[en++] = b;
    else en = -1;
  }
  brGot = got;
  static uint32_t lastSet;
  uint32_t now = millis();
  if (txBr.kick || now - lastSet >= 1000) {
    txBr.kick = false;
    lastSet = now;
    uint8_t d[4] = { (uint8_t)(cfg.bridgeSrc == BP_SRC_AES ? BP_SRC_AES : BP_SRC_PCM), BP_FMT_LJ, 0, ++txBr.seq };
    txBridgeSend(BP_SET, d, sizeof(d));
  }
}

// Порт: C — стан моста; C1 — цифровой вход, C2 — аналоговый.
static void txBridgePrint() {
  static const char *const ACT[] = { "нічого", "цифровий вхід (AES)", "аналоговий вхід (PCM1808)", "тиша", "тон моста", "перемикання" };
  static const char *const AES[] = { "немає сигналу", "ловлю", "є", "частота, якої міст не бере" };
  uint32_t now = millis();
  Serial.printf("міст: обрано %s; кадрів стану %u, зіпсованих %u, байтів зворотного каналу втрачено %u, команд надіслано %u\n",
                cfg.bridgeSrc == BP_SRC_AES ? "цифровий вхід" : "аналоговий вхід", (unsigned)txBr.frames, (unsigned)txBr.bad, (unsigned)brLost,
                (unsigned)txBr.sent);
  if (!txBr.lastMs) {
    Serial.println("  міст МОВЧИТЬ: жодного кадру стану (не підключений, не прошитий або без живлення)");
    return;
  }
  const bp_status_t &s = txBr.st;
  Serial.printf("  %s; версія %u.%02u (завантажувач %u), працює %u с; звучить: %s; AES: %s %u Гц, помилок парності %u\n",
                txBridgeOk() ? "на зв'язку" : "НЕ НА ЗВ'ЯЗКУ", s.verMajor, s.verMinor, s.loaderVer, (unsigned)s.uptime,
                ACT[s.act <= BP_ACT_SWITCH ? s.act : 0], AES[s.aes <= BP_AES_RATE ? s.aes : 0], (unsigned)s.aesRate, (unsigned)s.aesParity);
  Serial.printf("  пік Л −%.1f П −%.1f дБ; запас %d, поправка %+.1f ppm, провали %u/%u; перескоки такту %u; ядро звуку %u‰; "
                "остання команда №%u (надіслано №%u); стан %u мс тому\n",
                s.peakL / 2.0, s.peakR / 2.0, s.level, s.ppm10 / 10.0, s.under, s.over, s.resync, s.cpu1, s.seq, txBr.seq,
                (unsigned)(now - txBr.lastMs));
}
