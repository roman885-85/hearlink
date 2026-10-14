// Подполосный кодек дальнего режима (с 2.79). Владелец 12.10: «делай кодек с хорошим звуком для дальнего режима но
// с минимальной задержкой» — прежний звук нижней ступени («мова»: 16 кГц, 4 бита на отсчёт, кадр 8 мс) был груб, и
// от него уходили на постоянную 5,5 Мбит/с, где слабый приёмник теряет звук совсем.
//
// Устройство — как у SBC и MPEG-1, но под нашу задачу:
//  • набор из восьми косинусно-модулированных фильтров (полосы по 2 кГц при 32 кГц), окно 80 отсчётов (subc_tab.h,
//    считает tools/subc_design.py): разбор и сборка без квантования возвращают звук с ошибкой −62 дБ, задержка
//    набора 72 отсчёта (2,25 мс);
//  • кадр — 16 блоков по 8 отсчётов = 128 отсчётов = 4 мс (вдвое короче кадра «мова»): чем короче кадр, тем меньше
//    запас у приёмника и тем короче пакет на 1 Мбит/с — а короткий пакет реже теряется;
//  • у каждой полосы в кадре свой масштаб (наибольший отсчёт, шаг 1,5 дБ) и своё число бит на отсчёт; биты делятся
//    по полосам заново в каждом кадре — туда, где шум квантования был бы слышнее всего (subcAlloc). Шум сжатия в
//    каждой полосе идёт вслед за её звуком и прячется под ним; у прежнего сжатия он был ровным по частоте и после
//    подъёма верха в приёмнике (чёткость, эквалайзер) вылезал шипением;
//  • полоса, которой бит не досталось, не глохнет: приёмник заполняет её шумом того же уровня (масштаб передан) —
//    иначе тихий верх то появлялся бы, то пропадал от кадра к кадру;
//  • каждый кадр разбирается сам по себе (ни предсказания, ни подстройки по прошлому): пропавший кадр ничего не
//    портит в следующих, а сборка сама плавно сводит звук к тишине и обратно (окна перекрываются).
// Один и тот же разобранный кадр можно уложить в любое число байт — чем меньше, тем грубее (бит хватает только
// громким полосам, остальные — шумом). Этим пользуется пакет (proto.h, Q_SUB): новый кадр и копия прошлого — в полную
// силу, копия позапрошлого — попроще, а кадр трёхпакетной давности — совсем грубо («последняя надежда» при трёх
// потерях подряд; у грубого вида масштабы по 4 бита, шаг 6 дБ).
// Замер на компьютере (tools/host/subc.cpp, запись проповеди; сигнал/шум «на слух приёмника» — верх поднят на 12 дБ,
// до 8 кГц): 46 байт на кадр (92 кбит/с) — 41,6 дБ; 32 байта — 33,1; прежние «стандартна» (134 кбит/с) — 39,2,
// «мова» (67 кбит/с) — 30,5.
// Файл общий для прошивки и для проверки на компьютере (tools/host/subc.cpp).
//
// Счёт (с 2.82). Первый вариант (2.79–2.81) считал «в лоб»: 16 сумм по окну и матрица 8×16 на блок, сдвиг памяти на
// каждый блок. На плате он стоил 0,58 мс на кадр у передатчика и 0,6 мс на пакет у приёмника — вчетверо больше, чем
// выходило по числу действий. Причина не в арифметике: код исполнялся из флеш-памяти, а кэш команд у ESP32-S3 всего
// 16 КБ на оба ядра — к следующему кадру (через 4 мс) его вытесняли радио, экран и обработка звука, и ~4 КБ кода
// кодека каждый раз читались из флеша заново. Поэтому:
//  • разбор, укладка в биты и сборка лежат в быстрой памяти (SUBC_HOT → IRAM) и собраны компактно (O2, без разворота
//    циклов: развёрнутый O3 занимал 7,5 КБ);
//  • матрица 8×16 свёрнута в 8×8: косинусы набора повторяются со сдвигом на 8 отсчётов с точностью до знака, поэтому
//    16 сумм по окну сначала складываются попарно в 8 (см. subcAnalyse), а при сборке 8 сумм разворачиваются в 16
//    (subcSynth) — действий на блок 152 вместо 208;
//  • вход и перекрытие сборки лежат одним куском на весь кадр (хвост прошлого кадра + новый) — память сдвигается раз
//    за кадр, а не 16 раз;
//  • делений нет (у ESP32-S3 их делает подпрограмма): обратные масштабы и степени двойки — из таблиц.
// Вид кадра в битах не менялся: прошивки 2.79–2.81 и 2.82 понимают друг друга. Сверка с прежним счётом —
// tools/host/subc.cpp (эталон — tools/host/subc_ref.h).
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "subc_tab.h"

#define SUBC_NB 16                  // блоков в кадре
#define SUBC_N (SUBC_NB * SUBC_M)   // отсчётов в кадре: 128 (4 мс при 32 кГц)
#define SUBC_DELAY (SUBC_L - SUBC_M)   // на сколько отсчётов звук после сборки отстаёт от поданного кадрами: 72

// Горячие функции — в быстрой памяти (на компьютере — обычные). Обычные static-функции, а не методы в теле
// структуры: у «встроенных» методов компилятор кладёт константы отдельно, и указание раздела для них ненадёжно.
#if defined(ARDUINO) && !defined(SUBC_NO_IRAM)
#include "esp_attr.h"
#define SUBC_HOT IRAM_ATTR __attribute__((optimize("O2")))
#else
#define SUBC_HOT
#endif
#define SUBC_INL inline __attribute__((always_inline))

// «Веса слуха» полос при дележе бит (см. subcAlloc): середина (2–8 кГц) важнее — там согласные, и там же приёмник
// поднимает звук (чёткость, эквалайзер); самый верх (12–16 кГц) — в последнюю очередь.
#ifndef SUBC_W_TEST
static const int8_t subcW[SUBC_M] = { 0, 3, 3, 2, 0, -2, -5, -9 };
#else
static int8_t subcW[SUBC_M] = { 0, 3, 3, 2, 0, -2, -5, -9 };   // проверка на компьютере подбирает веса
#endif

// Таблицы: окна со знаками и множителями, косинусы, масштабы. Заполняются один раз.
struct SubcTab {
  float war[SUBC_L];              // окно разбора задом наперёд (вход лежит по порядку времени), знак (−1)^j по кускам в 2M
  float ws[SUBC_L];               // окно сборки, знаки те же
  float c4[SUBC_M][SUBC_M];       // разбор: полоса k из свёрнутого блока — 2·σk·cos((2k+1)(2j+1)π/32), σ = + − − + + − − +
  float c4t[SUBC_M][SUBC_M];      // сборка: та же таблица «боком» (отсчёт j из восьми полос)
  float sf[64], isf[64];          // масштаб по номеру: 2^(1 + s/4), шаг 1,5 дБ (грубый кадр берёт каждый четвёртый), и обратный
  float ip2[16];                  // 2^−(b−1): шаг ступени при b битах — масштаб на это число
  int8_t wt[SUBC_M];              // веса полос при дележе бит (subcW) — копия в быстрой памяти: сама таблица лежит во флеше
  bool ok = false;
};
static SubcTab subcTab;
static inline void subcInit() {
  SubcTab &t = subcTab;
  if (t.ok) return;
  for (int n = 0; n < SUBC_L; n++) {
    float sg = ((n / (2 * SUBC_M)) & 1) ? -1.0f : 1.0f;
    t.war[SUBC_L - 1 - n] = sg * SUBC_P[n] * SUBC_KA;
    t.ws[n] = sg * SUBC_P[n] * SUBC_KS;
  }
  for (int k = 0; k < SUBC_M; k++)
    for (int j = 0; j < SUBC_M; j++) {
      double v = 2 * cos((2 * k + 1) * (2 * j + 1) * M_PI / (4 * SUBC_M));
      if (((k + 1) >> 1) & 1) v = -v;
      t.c4[k][j] = (float)v;
      t.c4t[j][k] = (float)v;
    }
  for (int s = 0; s < 64; s++) {
    t.sf[s] = powf(2.0f, 1.0f + s * 0.25f);
    t.isf[s] = 1.0f / t.sf[s];
  }
  t.ip2[0] = 0;
  for (int b = 1; b < 16; b++) t.ip2[b] = 1.0f / (float)(1 << (b - 1));
  for (int k = 0; k < SUBC_M; k++) t.wt[k] = subcW[k];
  t.ok = true;
}

// Сколько бит на отсчёт дать каждой полосе. lev[k] — уровень полосы в шагах по 1,5 дБ (номер масштаба), budget —
// сколько бит на отсчёты во всём кадре. Считают одинаково передатчик и приёмник — только целыми числами.
// Каждый бит убавляет шум полосы на 6 дБ (четыре шага); очередной бит получает полоса, где шум сейчас выше всех
// над «весом слуха» (subcW).
static SUBC_INL void subcAlloc(const uint8_t *lev, int budget, uint8_t *b) {
#ifndef SUBC_W_TEST
  const int8_t *W = subcTab.wt;   // до первого subcInit() — нули: кодек без subcInit() не зовут
#else
  const int8_t *W = subcW;
#endif
  int need[SUBC_M];
  for (int k = 0; k < SUBC_M; k++) {
    b[k] = 0;
    need[k] = lev[k] ? (int)lev[k] + W[k] : -1000;   // полоса с нулевым масштабом — тишина: бит не просит
  }
  while (budget >= SUBC_NB) {
    int best = -1;
    for (int k = 0; k < SUBC_M; k++)
      if (b[k] < 15 && need[k] > -900 && (best < 0 || need[k] > need[best])) best = k;
    if (best < 0) break;
    b[best]++;
    need[best] -= 4;
    budget -= SUBC_NB;
  }
}

struct SubcBits {   // запись и чтение бит, старшим вперёд
  uint8_t *p;
  uint32_t acc = 0;
  int n = 0, pos = 0;
  SUBC_INL void put(uint32_t v, int bits) {
    acc = (acc << bits) | v;
    n += bits;
    while (n >= 8) {
      n -= 8;
      p[pos++] = (uint8_t)(acc >> n);
    }
  }
  SUBC_INL void flush(int total) {
    if (n) p[pos++] = (uint8_t)(acc << (8 - n));
    while (pos < total) p[pos++] = 0;
  }
  SUBC_INL uint32_t get(int bits) {
    while (n < bits) {
      acc = (acc << 8) | p[pos++];
      n += 8;
    }
    n -= bits;
    return (acc >> n) & ((1u << bits) - 1);
  }
};

struct SubcEnc {
  float B[SUBC_DELAY + SUBC_N];   // вход по порядку времени: хвост прошлого кадра (72 отсчёта) и новый кадр
  float s[SUBC_NB][SUBC_M];       // отсчёты полос разобранного кадра
  uint8_t lev[SUBC_M];            // масштабы полос (шаг 1,5 дБ)
  void reset() {
    memset(B, 0, sizeof(B));
    memset(s, 0, sizeof(s));
    memset(lev, 0, sizeof(lev));
  }
  // 128 отсчётов → разобранный кадр (лежит в кодере); затем pack() — сколько угодно раз, с разным числом байт
  inline void analyse(const int16_t *x);
  // Разобранный кадр → bytes байт. coarse — грубый вид: масштабы по 4 бита (шаг 6 дБ) вместо 6.
  inline void pack(uint8_t *out, int bytes, bool coarse) const;
};

// Разбор. Блок m (восемь новых отсчётов) смотрит на 80 последних отсчётов входа: b[0…79], новейший — последним.
//  1) 16 сумм по окну: yr[r] = Σ war[r + 16j]·b[r + 16j], j = 0…4;
//  2) свёртка в 8: косинус полосы k на сдвиге в 8 отсчётов повторяется с точностью до знака, поэтому
//     u[j] = yr[11 − j] + yr[12 + j] (j = 0…3), u[j] = yr[11 − j] − yr[j − 4] (j = 4…7);
//  3) полоса k = Σ c4[k][j]·u[j].
static SUBC_HOT void subcAnalyse(SubcEnc *e, const int16_t *x) {
  const SubcTab &t = subcTab;
  float *B = e->B;
  float pk[SUBC_M];
  for (int k = 0; k < SUBC_M; k++) pk[k] = 0;
  memcpy(B, B + SUBC_N, sizeof(float) * SUBC_DELAY);
  for (int i = 0; i < SUBC_N; i++) B[SUBC_DELAY + i] = x[i];
  for (int m = 0; m < SUBC_NB; m++) {
    const float *b = B + m * SUBC_M;
    float yr[2 * SUBC_M], u[SUBC_M];
    for (int r = 0; r < 2 * SUBC_M; r++)
      yr[r] = t.war[r] * b[r] + t.war[r + 16] * b[r + 16] + t.war[r + 32] * b[r + 32] + t.war[r + 48] * b[r + 48] + t.war[r + 64] * b[r + 64];
    for (int j = 0; j < 4; j++) {
      u[j] = yr[11 - j] + yr[12 + j];
      u[4 + j] = yr[7 - j] - yr[j];
    }
    float *sm = e->s[m];
    for (int k = 0; k < SUBC_M; k++) {
      const float *c = t.c4[k];
      float a = c[0] * u[0] + c[1] * u[1] + c[2] * u[2] + c[3] * u[3] + c[4] * u[4] + c[5] * u[5] + c[6] * u[6] + c[7] * u[7];
      sm[k] = a;
      float f = fabsf(a);
      if (f > pk[k]) pk[k] = f;
    }
  }
  for (int k = 0; k < SUBC_M; k++) {   // наименьший масштаб, который не меньше наибольшего отсчёта полосы
    int lo = 0, hi = 63;
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if (t.sf[mid] >= pk[k]) hi = mid;
      else lo = mid + 1;
    }
    e->lev[k] = (uint8_t)lo;
  }
}
static SUBC_HOT void subcPack(const SubcEnc *e, uint8_t *out, int bytes, bool coarse) {
  const SubcTab &t = subcTab;
  uint8_t lv[SUBC_M], b[SUBC_M];
  SubcBits w;
  w.p = out;
  for (int k = 0; k < SUBC_M; k++) {
    if (coarse) {
      int c = (e->lev[k] + 3) / 4;   // вверх: масштаб не меньше наибольшего отсчёта
      if (c > 15) c = 15;
      w.put((uint32_t)c, 4);
      lv[k] = (uint8_t)(c * 4);
    } else {
      w.put(e->lev[k], 6);
      lv[k] = e->lev[k];
    }
  }
  subcAlloc(lv, bytes * 8 - SUBC_M * (coarse ? 4 : 6), b);
  // полосы, которым достались биты: множитель, полшкалы, верх шкалы
  int na = 0, ak[SUBC_M], ab[SUBC_M], half[SUBC_M];
  float inv[SUBC_M];
  for (int k = 0; k < SUBC_M; k++)
    if (b[k]) {
      ak[na] = k;
      ab[na] = b[k];
      half[na] = 1 << (b[k] - 1);
      inv[na] = (float)half[na] * t.isf[lv[k] > 63 ? 63 : lv[k]];
      na++;
    }
  for (int m = 0; m < SUBC_NB; m++) {
    const float *sm = e->s[m];
    for (int i = 0; i < na; i++) {   // равномерно, без нулевой ступени: номер ступени от нижнего края шкалы
      // (округление вниз — сдвигом в положительные числа и отбрасыванием дроби: floorf — вызов подпрограммы)
      int top = 2 * half[i] - 1, q = (int)(sm[ak[i]] * inv[i] + 32768.0f) - 32768 + half[i];
      w.put((uint32_t)(q < 0 ? 0 : q > top ? top : q), ab[i]);
    }
  }
  w.flush(bytes);
}
inline void SubcEnc::analyse(const int16_t *x) { subcAnalyse(this, x); }
inline void SubcEnc::pack(uint8_t *out, int bytes, bool coarse) const { subcPack(this, out, bytes, coarse); }

struct SubcDec {
  float A[SUBC_DELAY + SUBC_N];   // сборка с перекрытием: хвосты прошлых кадров (72 отсчёта) и место под кадр
  float s[SUBC_NB][SUBC_M];       // отсчёты полос кадра перед сборкой
  uint32_t rnd = 0x1234567u;
  void reset() {
    memset(A, 0, sizeof(A));
    memset(s, 0, sizeof(s));
  }
  // Кадр (bytes байт; coarse — грубого вида) → 128 отсчётов. in == NULL — кадра нет: набор «доигрывает» хвост
  // прошлых блоков и уходит в тишину.
  inline void decode(const uint8_t *in, int bytes, bool coarse, int16_t *out);
  inline void synth(int16_t *out);   // сборка кадра из s (decode зовёт сама; отдельно — для проверки без квантования)
};

static SUBC_INL int16_t subcClip(float y) {   // округление без lrintf (вызов подпрограммы)
  return (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : (int)(y + 32768.5f) - 32768);
}
// конец кадра: хвосты — в начало, место под следующий кадр — обнулить
static SUBC_INL void subcShift(float *A) {
  memcpy(A, A + SUBC_N, sizeof(float) * SUBC_DELAY);
  memset(A + SUBC_DELAY, 0, sizeof(float) * SUBC_N);
}
// Сборка кадра из отсчётов полос d->s. Блок: восемь сумм w[j] = Σ c4t[j][k]·s[k] разворачиваются в 16
// (v[n] = −w[4 + n] для n = 0…3, w[11 − n] для n = 4…11, w[n − 12] для n = 12…15), ложатся через окно на 80 отсчётов
// вперёд, и первые восемь из них готовы.
static SUBC_HOT void subcSynth(SubcDec *d, int16_t *out) {
  const SubcTab &t = subcTab;
  float *A = d->A;
  for (int m = 0; m < SUBC_NB; m++) {
    const float *s = d->s[m];
    float w[SUBC_M], v[2 * SUBC_M];
    for (int j = 0; j < SUBC_M; j++) {
      const float *c = t.c4t[j];
      w[j] = c[0] * s[0] + c[1] * s[1] + c[2] * s[2] + c[3] * s[3] + c[4] * s[4] + c[5] * s[5] + c[6] * s[6] + c[7] * s[7];
    }
    for (int n = 0; n < 4; n++) {
      v[n] = -w[4 + n];
      v[12 + n] = w[n];
    }
    for (int n = 4; n < 12; n++) v[n] = w[11 - n];
    float *a = A + m * SUBC_M;
    for (int r = 0; r < 2 * SUBC_M; r++) {
      float x = v[r];
      a[r] += t.ws[r] * x;
      a[r + 16] += t.ws[r + 16] * x;
      a[r + 32] += t.ws[r + 32] * x;
      a[r + 48] += t.ws[r + 48] * x;
      a[r + 64] += t.ws[r + 64] * x;
    }
    for (int i = 0; i < SUBC_M; i++) out[m * SUBC_M + i] = subcClip(a[i]);
  }
  subcShift(A);
}
static SUBC_HOT void subcDecode(SubcDec *d, const uint8_t *in, int bytes, bool coarse, int16_t *out) {
  if (!in) {   // кадра нет: в полосах нули — остаётся выдать хвосты прошлых блоков
    for (int i = 0; i < SUBC_N; i++) out[i] = subcClip(d->A[i]);
    subcShift(d->A);
    return;
  }
  const SubcTab &t = subcTab;
  uint8_t lv[SUBC_M], b[SUBC_M];
  float step[SUBC_M], sf[SUBC_M], nz[SUBC_M];
  SubcBits r;
  r.p = (uint8_t *)in;
  int sfBits = coarse ? 4 : 6;
  for (int k = 0; k < SUBC_M; k++) {
    uint32_t v = r.get(sfBits);
    lv[k] = (uint8_t)(coarse ? v * 4 : v);
    if (lv[k] > 63) lv[k] = 63;
  }
  subcAlloc(lv, bytes * 8 - SUBC_M * sfBits, b);
  for (int k = 0; k < SUBC_M; k++) {
    sf[k] = t.sf[lv[k]];
    step[k] = sf[k] * t.ip2[b[k]];
    nz[k] = sf[k] * (0.5f / 8388608.0f);
  }
  uint32_t rnd = d->rnd;
  for (int m = 0; m < SUBC_NB; m++) {
    float *s = d->s[m];
    for (int k = 0; k < SUBC_M; k++) {
      if (b[k]) s[k] = ((float)r.get(b[k]) + 0.5f) * step[k] - sf[k];   // середина ступени
      else if (lv[k] > 8) {   // бит не досталось: шум того же уровня (вполовину масштаба), чтобы полоса не «мигала»
        rnd = rnd * 1664525u + 1013904223u;
        s[k] = (float)((int32_t)rnd >> 8) * nz[k];
      } else s[k] = 0;
    }
  }
  d->rnd = rnd;
  subcSynth(d, out);
}
inline void SubcDec::decode(const uint8_t *in, int bytes, bool coarse, int16_t *out) { subcDecode(this, in, bytes, coarse, out); }
inline void SubcDec::synth(int16_t *out) { subcSynth(this, out); }
