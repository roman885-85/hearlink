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
#include <stdio.h>

#define BP_SYNC0 0xA7
#define BP_SYNC1 0x5B
#define BP_MAX_DATA 4100   // найбільший DATA (запис 4096 байт + зміщення)

// Команди
enum {
  BP_PING = 0x01,          // DATA: seq(1)
  BP_SET = 0x02,           // DATA: src(1) fmt(1) tone(1) seq(1) — див. BP_SRC_*, BP_FMT_*
  BP_ENTER_LOADER = 0x03,  // DATA: 'L' 'O' 'A' 'D' — перезапуститися в завантажувач і чекати
  BP_LOG = 0x04,           // (з 22.10) DATA: from(4) [count(2)] — записи журналу моста від номера from (кадри BP_ST_LOG)
  // завантажувач:
  BP_HELLO = 0x10,         // → BP_R_HELLO
  BP_WRITE = 0x11,         // DATA: off(4, кратне 256) байти(кратно 256, ≤ 4096) → BP_R_ACK
  BP_FINISH = 0x12,        // DATA: size(4) crc32(4) ver(2) → BP_R_ACK (перевірка всієї програми і запис відомостей)
  BP_BOOT = 0x13,          // запустити програму
  BP_BAUD = 0x14,          // DATA: baud(4) → BP_R_ACK на старій швидкості, далі — на новій
};
enum { BP_R_HELLO = 0x90, BP_R_ACK = 0x91 };
enum { BP_ACK_OK = 0, BP_ACK_BAD = 1, BP_ACK_FLASH = 2, BP_ACK_CRC = 3 };

// Джерело звуку моста (BP_SRC_USB — з 22.10: міст як звукова карта комп'ютера, bridge/src/usb_audio.h)
enum { BP_SRC_AUTO = 0, BP_SRC_AES = 1, BP_SRC_PCM = 2, BP_SRC_MUTE = 3, BP_SRC_USB = 4 };
// Формат PCM1808 (вивід FMT): 0 — «лівовирівняний» (FMT на 3,3 В), 1 — I2S (FMT на землі — так у наборі, власник
// 23.10). Міст біт не читає: формат розпізнає сам за даними (pcmShift у bridge/src/main.c).
enum { BP_FMT_LJ = 0, BP_FMT_I2S = 1 };
// BP_SET d[1], біт 1 (з 22.10): аналоговий вхід — СИМЕТРИЧНИЙ: міст бере різницю каналів PCM1808 (Л − П) / 2 і шле
// її в обидва канали (моно). Гул «землі» однаковий в обох проводах і віднімається (на мосту 22.10: −34 дБ на 250 Гц);
// так підключають симетричний вихід пульта (XLR: 2 → Л, 3 → П, 1 → земля) — і так само виправляється стерео, у якого
// один канал прийшов перевернутим. Старий міст біт не читає.
#define BP_PCM_DIFF 0x02
// BP_SET d[1], біт 2 (з 23.10): вихід до передавача — 48 кГц (режим «Монітор»: передавач тоді дає такти 48 кГц);
// без біта — 32 кГц, як було. Старий міст біт не читає (і дав би 32 кГц — передавач це бачить у стані: outRate).
#define BP_OUT48 0x04
// BP_SET d[1], біт 3 (з 23.10): прибирати гул радіо передавача з аналогового входу — міст вчить його форму в тиші й
// віднімає (bridge/src/hum.h: лінія 250 Гц −81 → −100…−117 дБ). Без біта (і в старого моста) — вхід як є.
#define BP_PCM_HUM 0x08
// Перевірочний сигнал моста (BP_SET d[2]; з 1.02 — не лише тон): іде замість входу через увесь шлях передавача
enum { BP_GEN_OFF = 0, BP_GEN_TONE = 1 /* 1 кГц, −12 дБ */, BP_GEN_PINK = 2 /* рожевий шум, −20 дБ */, BP_GEN_SWEEP = 3 /* 20 Гц → 16 кГц за 10 с */ };
// Що звучить насправді
enum { BP_ACT_NONE = 0, BP_ACT_AES = 1, BP_ACT_PCM = 2, BP_ACT_MUTE = 3, BP_ACT_TONE = 4, BP_ACT_SWITCH = 5, BP_ACT_USB = 6 };
// USB моста: комп'ютера немає / підключено / комп'ютер грає в міст (потік відкрито)
enum { BP_USB_NONE = 0, BP_USB_HOST = 1, BP_USB_PLAY = 2 };
// Стан прийому AES
enum { BP_AES_NONE = 0, BP_AES_WAIT = 1, BP_AES_OK = 2, BP_AES_RATE = 3 /* частота, якої міст не бере */ };

#define BP_ST_STATUS 1   // тип кадру стану
#define BP_ST_LOG 2      // (з 22.10) тип кадру журналу: bp_log_hdr_t і до BP_LOG_PER записів bp_log_rec_t

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
  // з 22.10 (кадр попередніх збірок коротший — без цих полів, BP_STATUS_V1 байт; передавач бере обидва):
  uint32_t appCrc;       // CRC-32 програми моста (з відомостей завантажувача): поки 4.x доробляється, номер версії не
                         // міняється (владелець 22.10), і нову збірку передатчик упізнає за ним (BRIDGE_FW_CRC32)
  uint32_t logHead;      // скільки записів зроблено в журнал моста за весь час (номер наступного)
  uint16_t boot;         // номер запуску моста від увімкнення живлення (1 — перший)
  uint8_t btnSeq;        // натискань кнопки USR моста (лічильник: передавач дивиться на зміну)
  uint8_t btnSrc;        // вхід, який попросила кнопка: BP_SRC_AES чи BP_SRC_PCM
  // з 22.10 (USB): кадр стає довшим — передавач бере й довші, ніж знає (зайве відкидає), і коротші
  uint8_t usb;           // BP_USB_*
  uint8_t usbRec;        // 1 — комп'ютер записує «Що йде в ефір» (потік запису USB відкрито)
  uint8_t usbRsv[2];
} bp_status_t;
#define BP_STATUS_V1 offsetof(bp_status_t, appCrc)

// Журнал моста (з 22.10): події, які міст пам'ятає між своїми перезапусками (але не між вимкненнями живлення).
typedef struct __attribute__((packed)) {
  uint32_t ms;     // від запуску моста
  uint16_t boot;   // номер запуску (як у стані)
  uint8_t kind;    // BL_*
  uint8_t a;
  uint32_t b;
} bp_log_rec_t;
typedef struct __attribute__((packed)) {
  uint8_t type;    // BP_ST_LOG
  uint8_t n;       // записів у кадрі
  uint32_t first;  // номер першого з них
  uint32_t head;   // скільки всього записано (як logHead у стані)
} bp_log_hdr_t;
#define BP_LOG_PER 8     // записів у кадрі: 10 + 96 + CRC 2 = 108 байт, після COBS з нулем — 110
#define BP_LOG_KEEP 512  // скільки останніх записів міст тримає в пам'яті
enum {
  BL_BOOT = 1,     // запуск: a — чому (BL_WHY_*), b — версія (старший байт — головна)
  BL_FAULT = 2,    // минулий запуск упав: a — 0 panic, 1 апаратна помилка (+0x80 — на ядрі 1); b — адреса
  BL_AES = 3,      // прийом AES: a — стан BP_AES_*, b — частота, Гц
  BL_ACT = 4,      // що звучить: a — BP_ACT_*
  BL_SET = 5,      // передавач змінив запит: a — вхід BP_SRC_*, b — перевірочний сигнал BP_GEN_*
  BL_CLK = 6,      // такти передавача: a — 1 з'явились, 0 зникли (вимкнули, перезапустився, обрив), 2 — після запуску немає
  BL_RESYNC = 7,   // вихід ловив такт передавача наново: b — разів усього
  BL_UNDER = 8,    // провали перерахунку (входу забракло): b — усього
  BL_OVER = 9,     // переповнення перерахунку: b — усього
  BL_LOADER = 10,  // у завантажувач: a — 0 команда передавача, 1 «розрив» лінії, 2 з USB; b = 1 — у режим запису USB
  BL_BTN = 11,     // кнопка USR: a — вхід, який попросила (BP_SRC_*)
  BL_CLIP = 12,    // перевантаження: b — скільки проміжків по 50 мс за хвилину пік був у межах 0,5 дБ від повної шкали
  BL_DROP = 13,    // пропущено записів (надто часті події — не більше 30 на хвилину): b — скільки
  BL_USB = 14,     // USB: a — новий стан BP_USB_* (комп'ютер підключили, почав чи перестав грати)
  BL_USBREC = 15,  // запис на комп'ютер: a — 1 почав записувати «Що йде в ефір», 0 перестав
};
enum { BL_WHY_POWER = 0, BL_WHY_WATCHDOG = 1, BL_WHY_ASKED = 2 /* оновлення, команда, USB */, BL_WHY_PIN = 3 /* кнопка RST */ };

// Запис журналу словами — однаково для порту USB моста і для передавача (його порт і журнал на карті). Повертає довжину.
static inline int bp_log_text(const bp_log_rec_t *r, char *o, size_t cap) {
  static const char *const WHY[] = { "увімкнули живлення", "сторож (міст зависав)", "на прохання (оновлення, команда)", "кнопка RST" };
  static const char *const ACT[] = { "нічого", "цифровий вхід (AES)", "аналоговий вхід (PCM1808)", "тиша", "перевірочний сигнал", "перемикання",
                                     "вхід USB (комп'ютер)" };
  static const char *const AES[] = { "немає сигналу", "ловлю", "є", "частота, якої міст не бере" };
  static const char *const SRC[] = { "авто", "цифровий вхід", "аналоговий вхід", "тиша", "вхід USB" };
  static const char *const USB[] = { "комп'ютер відключили", "комп'ютер підключено", "комп'ютер грає в міст" };
  static const char *const GEN[] = { "", ", перевірочний тон", ", рожевий шум", ", розгортка" };
  static const char *const LDR[] = { "команда передавача", "«розрив» лінії команд", "з USB" };
  unsigned long b = (unsigned long)r->b;
  switch (r->kind) {
    case BL_BOOT: return snprintf(o, cap, "запуск, версія %lu.%02lu: %s", b >> 8, b & 0xFF, WHY[r->a & 3]);
    case BL_FAULT: return snprintf(o, cap, "минулий запуск УПАВ: %s на ядрі %u, адреса 0x%08lx", (r->a & 0x7F) ? "апаратна помилка" : "panic", (unsigned)(r->a >> 7), b);
    case BL_AES:
      if (r->a == BP_AES_OK || r->a == BP_AES_RATE) return snprintf(o, cap, "AES: %s, %lu Гц", AES[r->a], b);
      return snprintf(o, cap, "AES: %s", AES[r->a & 3]);
    case BL_ACT: return snprintf(o, cap, "звучить: %s", ACT[r->a <= BP_ACT_USB ? r->a : 0]);
    case BL_SET: return snprintf(o, cap, "передавач обрав: %s%s", SRC[r->a <= BP_SRC_USB ? r->a : 0], GEN[b & 3]);
    case BL_CLK: return snprintf(o, cap, r->a == 1 ? "такти передавача є" : r->a == 2 ? "тактів передавача немає" : "такти передавача ЗНИКЛИ");
    case BL_RESYNC: return snprintf(o, cap, "вихід ловив такт передавача наново (усього %lu)", b);
    case BL_UNDER: return snprintf(o, cap, "провал перерахунку — входу забракло (усього %lu)", b);
    case BL_OVER: return snprintf(o, cap, "переповнення перерахунку (усього %lu)", b);
    case BL_LOADER:
      if (b) return snprintf(o, cap, "у режим запису USB (з порту USB)");
      return snprintf(o, cap, "у завантажувач: %s", LDR[r->a <= 2 ? r->a : 0]);
    case BL_BTN: return snprintf(o, cap, "кнопка USR: попросила %s", SRC[r->a <= BP_SRC_USB ? r->a : 0]);
    case BL_USB: return snprintf(o, cap, "USB: %s", USB[r->a <= BP_USB_PLAY ? r->a : 0]);
    case BL_USBREC: return snprintf(o, cap, r->a ? "USB: комп'ютер записує, що йде в ефір" : "USB: запис на комп'ютер зупинено");
    case BL_CLIP: return snprintf(o, cap, "перевантаження: %lu разів за хвилину (пік до 0,5 дБ від повної шкали)", b);
    case BL_DROP: return snprintf(o, cap, "пропущено %lu записів (надто часті події)", b);
  }
  return snprintf(o, cap, "запис невідомого виду %u (%u, %lu)", (unsigned)r->kind, (unsigned)r->a, b);
}

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
