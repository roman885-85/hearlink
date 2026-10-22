// Обмін передавача з мостом (Raspberry Pi Pico, RP2040; прошивка — bridge/). Спільний для обох прошивок, чистий C.
//
// Дроти — ті самі, що були до PCM1808: вивід 1 (BCK) і 2 (LRCK) передавача — такти, вивід 40 — дані I2S від моста,
// вивід 43 — тепер не MCLK, а TXD порту передавача: його журнал (як і раніше, іде на комп'ютер) і команди мосту.
//
// Передавач → міст: кадри в потоці порту (115 200 8N1). Решту байтів (журнал) міст пропускає.
//   A7 5B LEN_LO LEN_HI CMD DATA… CRC_LO CRC_HI;  LEN = 1 + довжина DATA;  CRC-16/CCITT-FALSE по LEN…DATA.
// Міст → передавач: молодші біти слів I2S (звуку вистачає 24 старших): на кадр (лівий + правий) — один байт, старший
// напівбайт у бітах 7…4 лівого слова, молодший — у бітах 7…4 правого; біти 3…0 — завжди нулі (передавач 3.29 інакше
// «впізнає зсув даних PCM1808» за бітом 0 і псує звук). Байти — кадри COBS, розділені нулем; кадр = тип, дані,
// CRC-16 (як вище). Порожньо — нулі.
// Завантажувач моста відповідає тими самими кадрами A7 5B… звичайним UART з виводу даних моста (передавач на час
// оновлення бере вивід 40 як RX).
#pragma once
#include <stdint.h>
#include <stddef.h>

#define BP_SYNC0 0xA7
#define BP_SYNC1 0x5B
#define BP_MAX_DATA 4100   // найбільший DATA (запис 4096 байт + зміщення)

// Команди
enum {
  BP_PING = 0x01,          // DATA: seq(1)
  BP_SET = 0x02,           // DATA: src(1) fmt(1) tone(1) seq(1) — див. BP_SRC_*, BP_FMT_*
  BP_ENTER_LOADER = 0x03,  // DATA: 'L' 'O' 'A' 'D' — перезапуститися в завантажувач і чекати
  // завантажувач:
  BP_HELLO = 0x10,         // → BP_R_HELLO
  BP_WRITE = 0x11,         // DATA: off(4, кратне 256) байти(кратно 256, ≤ 4096) → BP_R_ACK
  BP_FINISH = 0x12,        // DATA: size(4) crc32(4) ver(2) → BP_R_ACK (перевірка всієї програми і запис відомостей)
  BP_BOOT = 0x13,          // запустити програму
  BP_BAUD = 0x14,          // DATA: baud(4) → BP_R_ACK на старій швидкості, далі — на новій
};
enum { BP_R_HELLO = 0x90, BP_R_ACK = 0x91 };
enum { BP_ACK_OK = 0, BP_ACK_BAD = 1, BP_ACK_FLASH = 2, BP_ACK_CRC = 3 };

// Джерело звуку моста
enum { BP_SRC_AUTO = 0, BP_SRC_AES = 1, BP_SRC_PCM = 2, BP_SRC_MUTE = 3 };
// Формат PCM1808 (вивід FMT): 0 — «лівовирівняний» (FMT на 3,3 В, як зараз), 1 — I2S (FMT на землі)
enum { BP_FMT_LJ = 0, BP_FMT_I2S = 1 };
// Перевірочний сигнал моста (BP_SET d[2]; з 1.02 — не лише тон): іде замість входу через увесь шлях передавача
enum { BP_GEN_OFF = 0, BP_GEN_TONE = 1 /* 1 кГц, −12 дБ */, BP_GEN_PINK = 2 /* рожевий шум, −20 дБ */, BP_GEN_SWEEP = 3 /* 20 Гц → 16 кГц за 10 с */ };
// Що звучить насправді
enum { BP_ACT_NONE = 0, BP_ACT_AES = 1, BP_ACT_PCM = 2, BP_ACT_MUTE = 3, BP_ACT_TONE = 4, BP_ACT_SWITCH = 5 };
// Стан прийому AES
enum { BP_AES_NONE = 0, BP_AES_WAIT = 1, BP_AES_OK = 2, BP_AES_RATE = 3 /* частота, якої міст не бере */ };

#define BP_ST_STATUS 1   // тип кадру стану

typedef struct __attribute__((packed)) {
  uint8_t type;          // BP_ST_STATUS
  uint8_t verMajor, verMinor;
  uint8_t seq;           // останній seq команди, яку міст прийняв
  uint8_t src, fmt, tone;// що просили
  uint8_t act;           // що звучить (BP_ACT_*)
  uint8_t aes;           // BP_AES_*
  uint8_t peakL, peakR;  // пік за 50 мс у −дБ повної шкали ×2 (0 = 0 дБ, 255 — тиша)
  uint8_t loaderVer;
  uint32_t aesRate;      // Гц (0 — немає)
  uint32_t aesParity;    // помилок парності AES усього
  int16_t ppm10;         // поправка перерахунку частоти, ppm ×10
  int16_t level;         // запас вхідних кадрів
  uint16_t under, over;  // провали / переповнення перерахунку
  uint16_t resync;       // скільки разів вихід ловив такт передавача наново
  uint16_t cpu1;         // зайнятість звукового ядра, ‰
  uint32_t uptime;       // секунд від запуску моста
} bp_status_t;

// CRC-16/CCITT-FALSE (поліном 0x1021, початок 0xFFFF)
static inline uint16_t bp_crc16(const uint8_t *p, size_t n, uint16_t c) {
  while (n--) {
    c ^= (uint16_t)(*p++) << 8;
    for (int i = 0; i < 8; i++) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1);
  }
  return c;
}

// CRC-32 (як у zip): для перевірки образу програми моста
static inline uint32_t bp_crc32(const uint8_t *p, size_t n, uint32_t c) {
  c = ~c;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

// COBS: кодує n байтів src у dst (dst ≥ n + n/254 + 1), повертає довжину; нуль-роздільник додає викликач.
static inline size_t bp_cobs_enc(const uint8_t *src, size_t n, uint8_t *dst) {
  size_t o = 1, c = 0;
  uint8_t code = 1;
  for (size_t i = 0; i < n; i++) {
    if (src[i] == 0) {
      dst[c] = code;
      c = o++;
      code = 1;
    } else {
      dst[o++] = src[i];
      if (++code == 0xFF) {
        dst[c] = code;
        c = o++;
        code = 1;
      }
    }
  }
  dst[c] = code;
  return o;
}

// COBS: розкодувати n байтів (без нуля-роздільника) у dst (≥ n); повертає довжину або 0 — зіпсований кадр.
static inline size_t bp_cobs_dec(const uint8_t *src, size_t n, uint8_t *dst) {
  size_t i = 0, o = 0;
  while (i < n) {
    uint8_t code = src[i++];
    if (code == 0) return 0;
    for (uint8_t k = 1; k < code; k++) {
      if (i >= n) return 0;
      dst[o++] = src[i++];
    }
    if (code != 0xFF && i < n) dst[o++] = 0;
  }
  return o;
}

// Зібрати кадр команди в out (≥ len + 7); повертає довжину.
static inline size_t bp_frame(uint8_t cmd, const uint8_t *data, size_t len, uint8_t *out) {
  size_t L = len + 1;
  out[0] = BP_SYNC0;
  out[1] = BP_SYNC1;
  out[2] = (uint8_t)L;
  out[3] = (uint8_t)(L >> 8);
  out[4] = cmd;
  for (size_t i = 0; i < len; i++) out[5 + i] = data[i];
  uint16_t c = bp_crc16(out + 2, L + 2, 0xFFFF);
  out[5 + len] = (uint8_t)c;
  out[6 + len] = (uint8_t)(c >> 8);
  return len + 7;
}

// Розбір кадрів A7 5B… з потоку байтів (по одному). bp_rx_feed повертає 1, коли кадр готовий: cmd, data, len.
typedef struct {
  uint8_t st;
  uint16_t len, got;
  uint8_t buf[BP_MAX_DATA + 4];
} bp_rx_t;

static inline int bp_rx_feed(bp_rx_t *r, uint8_t b) {
  switch (r->st) {
    case 0: r->st = b == BP_SYNC0 ? 1 : 0; return 0;
    case 1: r->st = b == BP_SYNC1 ? 2 : (b == BP_SYNC0 ? 1 : 0); return 0;
    case 2: r->buf[0] = b; r->st = 3; return 0;
    case 3:
      r->buf[1] = b;
      r->len = (uint16_t)(r->buf[0] | (b << 8));
      if (r->len < 1 || r->len > BP_MAX_DATA + 1) { r->st = 0; return 0; }
      r->got = 0;
      r->st = 4;
      return 0;
    default:
      r->buf[2 + r->got++] = b;
      if (r->got < r->len + 2) return 0;
      r->st = 0;
      {
        uint16_t c = bp_crc16(r->buf, (size_t)r->len + 2, 0xFFFF);
        uint16_t g = (uint16_t)(r->buf[2 + r->len] | (r->buf[3 + r->len] << 8));
        return c == g;
      }
  }
}
// Після bp_rx_feed() == 1: команда і дані
static inline uint8_t bp_rx_cmd(const bp_rx_t *r) { return r->buf[2]; }
static inline const uint8_t *bp_rx_data(const bp_rx_t *r) { return r->buf + 3; }
static inline uint16_t bp_rx_len(const bp_rx_t *r) { return (uint16_t)(r->len - 1); }
