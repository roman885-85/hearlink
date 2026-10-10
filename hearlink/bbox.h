// «Чёрный ящик» радио передатчика.
// 07.10 радио передатчика трижды вставало намертво: драйвер перестаёт отвечать на отправки, все 32 его буфера
// остаются занятыми, каждая новая отправка отклоняется (ESP_ERR_ESPNOW_NO_MEM), и прежний перезапуск ESP-NOW его
// не оживлял — помогал только сброс платы. Чтобы видеть, что было перед этим, передатчик помнит последние события
// (смена канала, запись настроек, обращение к карте, сообщения приёмников, попытки оживить радио) и в миг первой
// отказанной отправки снимает с них слепок вместе с состоянием памяти. Слепок печатается в порт (команда «u» —
// последний) и дописывается в журнал на карте.
#pragma once
#include <Arduino.h>
#include <esp_heap_caps.h>

enum {
  BB_NONE = 0,
  BB_APPLY,      // канал/скорость/мощность применены (a — канал)
  BB_HOP,        // переход без паузы (a — канал)
  BB_SCAN,       // замер каналов: a = 1 начат, 0 закончен
  BB_MON,        // наблюдение за чужими передачами: a = 1/0
  BB_NVS,        // запись настроек во флеш (b — сколько заняла, мс)
  BB_CARD,       // обращение к карте дольше 20 мс (a: 1 запись, 2 чтение; b — мс)
  BB_PKT,        // пришло сообщение не звуком (a — младший байт вида, b — длина)
  BB_REVIVE,     // попытка оживить радио (a — ступень 1…3, b — сколько заняла, мс)
  BB_ALIVE,      // радио снова отвечает (a — после какой ступени, b — сколько молчало, мс / 10)
  BB_CMD,        // команда с порта (a — буква)
  BB_PAGE,       // на экране открыта другая страница (a — номер)
  BB_REFUSE,     // первая отказанная отправка (b — код ошибки)
  BB_WAIT,       // пакет ждал эфира дольше 60 мс (b — мс)
  BB_LCDFLIP,    // драйвер экрана сбился с очереди буферов (a — который раз; panel4848.h) — до 2.49 это был срыв картинки
};
struct BbEv {
  uint32_t ms;
  uint8_t what, a;
  uint16_t b;
};
#define BB_N 40
static BbEv bbRing[BB_N];
static volatile uint32_t bbPos;
static inline void bbMark(uint8_t what, uint8_t a = 0, uint32_t b = 0) {
  uint32_t i = bbPos;
  bbPos = i + 1;   // пишут несколько задач; редкая потеря отметки при гонке не страшна
  BbEv &e = bbRing[i % BB_N];
  e.ms = millis();
  e.what = what;
  e.a = a;
  e.b = b > 65535 ? 65535 : (uint16_t)b;
}

// Показания закрытой библиотеки радиочасти — только чтение (те же, что печатает команда Qn). В слепок они идут, чтобы
// проверить подозрение: радио «встаёт», потому что считает эфир занятым и ждёт тишины (с 2.63).
extern "C" {
int phy_get_noise_floor(void);
int read_hw_noisefloor(void);
uint8_t phy_get_cca(void);
}
struct BbSnap {                  // без начальных значений: живёт в памяти часов и должен пережить перезапуск платы
  uint32_t ms, pos;
  int16_t nf, nfHw;              // уровень шума, каким его видит радиочасть (четверти дБм): расчётный и «с железа»
  uint8_t cca;                   // её порог/состояние «эфир занят»
  int32_t pend;                  // сколько пакетов было у драйвера
  uint32_t sinceDoneMs;          // сколько драйвер уже молчал
  uint32_t heap, heapBig, heapMin, psram;
  int err;
  uint8_t ch;
  BbEv ev[BB_N];
};
#define BB_MAGIC 0x42425831u
RTC_NOINIT_ATTR static BbSnap bbSnap;
RTC_NOINIT_ATTR static uint32_t bbSnapMagic;   // BB_MAGIC — в bbSnap лежит слепок (этого запуска или прежнего)
static volatile uint32_t bbSnapSeq;   // растёт с каждым слепком этого запуска
static volatile uint32_t bbDeaths;    // сколько раз радио вставало с запуска

// Снять слепок (из задачи передачи, в миг первой отказанной отправки).
static void bbFreeze(int32_t pend, uint32_t sinceDoneMs, int err, uint8_t ch) {
  BbSnap &s = bbSnap;
  s.ms = millis();
  s.pos = bbPos;
  s.pend = pend;
  s.sinceDoneMs = sinceDoneMs;
  s.err = err;
  s.ch = ch;
  s.heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  s.heapBig = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  s.heapMin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  s.psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  s.nf = (int16_t)phy_get_noise_floor();
  s.nfHw = (int16_t)read_hw_noisefloor();
  s.cca = phy_get_cca();
  memcpy(s.ev, bbRing, sizeof(s.ev));
  bbSnapMagic = BB_MAGIC;
  bbDeaths = bbDeaths + 1;
  bbSnapSeq = bbSnapSeq + 1;
}

static const char *bbName(uint8_t w) {
  static const char *const N[] = { "?", "канал", "перехід", "вимір каналів", "спостереження", "запис налаштувань", "картка", "повідомлення",
                                   "оживлення", "радіо відповіло", "команда", "сторінка", "відмова", "довге чекання", "збій черги буферів екрана" };
  return w < sizeof(N) / sizeof(N[0]) ? N[w] : "?";
}

// Дополнить слепок тем, что было после отказа (попытки оживить радио): перед печатью и перед перезапуском платы.
static void bbRefresh() {
  if (bbSnapMagic != BB_MAGIC || !bbSnapSeq) return;
  bbSnap.pos = bbPos;
  memcpy(bbSnap.ev, bbRing, sizeof(bbSnap.ev));
}

// Слепок словами: события от старых к новым, время — за сколько мс до отказа (после отказа — со знаком «+»).
static int bbFormat(const BbSnap &s, char *out, int cap, bool live) {
  int o = 0;
  const BbEv *ev = live ? bbRing : s.ev;
  uint32_t pos = live ? bbPos : s.pos, ref = live ? millis() : s.ms;
  if (!live)
    o += snprintf(out + o, cap - o, "# РАДІО СТАЛО на %u-й секунді, канал %u: у драйвера %d пакетів, мовчав %u мс, помилка 0x%X; пам'ять %u КБ "
                                    "(найбільший шматок %u КБ, найменше було %u КБ), PSRAM %u КБ; радіо чуло шум %.1f / %.1f дБм, поріг-стан %u. Перед цим:",
                  (unsigned)(s.ms / 1000), s.ch, (int)s.pend, (unsigned)s.sinceDoneMs, (unsigned)s.err, (unsigned)(s.heap / 1024),
                  (unsigned)(s.heapBig / 1024), (unsigned)(s.heapMin / 1024), (unsigned)(s.psram / 1024), s.nf / 4.0f, s.nfHw / 4.0f, s.cca);
  int n = pos < BB_N ? (int)pos : BB_N;
  for (int k = n; k >= 1 && o < cap - 48; k--) {
    const BbEv &e = ev[(pos - k) % BB_N];
    if (!e.what) continue;
    o += snprintf(out + o, cap - o, " [%s%d мс: %s %u/%u]", (int32_t)(e.ms - ref) > 0 ? "+" : "-", (int)abs((int32_t)(ref - e.ms)), bbName(e.what), e.a, e.b);
  }
  if (o > cap - 2) o = cap - 2;
  out[o++] = '\n';
  out[o] = 0;
  return o;
}
