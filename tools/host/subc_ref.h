// ЭТАЛОН для сверки (tools/host/subc.cpp): счёт кодека «в лоб», каким он был в прошивках 2.79–2.81. В прошивку не входит.
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
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "subc_tab.h"

#define SUBC_NB 16                  // блоков в кадре
#define SUBC_N (SUBC_NB * SUBC_M)   // отсчётов в кадре: 128 (4 мс при 32 кГц)
#define SUBC_DELAY (SUBC_L - SUBC_M)   // на сколько отсчётов звук после сборки отстаёт от поданного кадрами: 72

// Таблицы: окно со знаками и множителями, косинусы разбора и сборки, масштабы. Заполняются один раз.
struct SubcTab {
  float wa[SUBC_L], ws[SUBC_L];           // окно разбора и сборки: знак (−1)^j по кускам в 2M отсчётов и множители
  float ca[SUBC_M][2 * SUBC_M], cs[SUBC_M][2 * SUBC_M];
  float sf[64];                           // масштаб по номеру: 2^(1 + s/4), шаг 1,5 дБ; грубый кадр берёт каждый четвёртый
  bool ok = false;
};
static SubcTab subcTab;
static inline void subcInit() {
  SubcTab &t = subcTab;
  if (t.ok) return;
  for (int n = 0; n < SUBC_L; n++) {
    float sg = ((n / (2 * SUBC_M)) & 1) ? -1.0f : 1.0f;
    t.wa[n] = sg * SUBC_P[n] * SUBC_KA;
    t.ws[n] = sg * SUBC_P[n] * SUBC_KS;
  }
  for (int k = 0; k < SUBC_M; k++)
    for (int n = 0; n < 2 * SUBC_M; n++) {
      double ph = (2 * k + 1) * M_PI / (2 * SUBC_M) * (n - (SUBC_L - 1) / 2.0), th = (k & 1) ? -M_PI / 4 : M_PI / 4;
      t.ca[k][n] = (float)(2 * cos(ph + th));
      t.cs[k][n] = (float)(2 * cos(ph - th));
    }
  for (int s = 0; s < 64; s++) t.sf[s] = powf(2.0f, 1.0f + s * 0.25f);
  t.ok = true;
}

// Сколько бит на отсчёт дать каждой полосе. lev[k] — уровень полосы в шагах по 1,5 дБ (номер масштаба), budget —
// сколько бит на отсчёты во всём кадре. Считают одинаково передатчик и приёмник — только целыми числами.
// Каждый бит убавляет шум полосы на 6 дБ (четыре шага); очередной бит получает полоса, где шум сейчас выше всех
// над «весом слуха». Веса: середина (2–8 кГц) важнее — там согласные, и там же приёмник поднимает звук (чёткость,
// эквалайзер); самый верх (12–16 кГц) — в последнюю очередь.
#ifndef SUBC_W_TEST
static const int8_t subcW[SUBC_M] = { 0, 3, 3, 2, 0, -2, -5, -9 };
#else
static int8_t subcW[SUBC_M] = { 0, 3, 3, 2, 0, -2, -5, -9 };   // проверка на компьютере подбирает веса
#endif
static inline void subcAlloc(const uint8_t *lev, int budget, uint8_t *b) {
  const int8_t *W = subcW;
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
  inline void put(uint32_t v, int bits) {
    acc = (acc << bits) | v;
    n += bits;
    while (n >= 8) {
      n -= 8;
      p[pos++] = (uint8_t)(acc >> n);
    }
  }
  inline void flush(int total) {
    if (n) p[pos++] = (uint8_t)(acc << (8 - n));
    while (pos < total) p[pos++] = 0;
  }
  inline uint32_t get(int bits) {
    while (n < bits) {
      acc = (acc << 8) | p[pos++];
      n += 8;
    }
    n -= bits;
    return (acc >> n) & ((1u << bits) - 1);
  }
};

struct SubcEnc {
  float h[SUBC_L];              // последние отсчёты входа, новейший — первым
  float s[SUBC_NB][SUBC_M];     // отсчёты полос разобранного кадра
  uint8_t lev[SUBC_M];          // масштабы полос (шаг 1,5 дБ)
  void reset() {
    memset(h, 0, sizeof(h));
    memset(s, 0, sizeof(s));
    memset(lev, 0, sizeof(lev));
  }
  // 128 отсчётов → разобранный кадр (лежит в кодере); затем pack() — сколько угодно раз, с разным числом байт
  void analyse(const int16_t *x) {
    const SubcTab &t = subcTab;
    float pk[SUBC_M];
    for (int k = 0; k < SUBC_M; k++) pk[k] = 0;
    for (int m = 0; m < SUBC_NB; m++) {
      memmove(h + SUBC_M, h, sizeof(float) * (SUBC_L - SUBC_M));
      for (int i = 0; i < SUBC_M; i++) h[i] = x[m * SUBC_M + SUBC_M - 1 - i];
      float y[2 * SUBC_M];
      for (int n = 0; n < 2 * SUBC_M; n++) {
        float a = 0;
        for (int j = 0; j < SUBC_L / (2 * SUBC_M); j++) a += t.wa[n + 2 * SUBC_M * j] * h[n + 2 * SUBC_M * j];
        y[n] = a;
      }
      for (int k = 0; k < SUBC_M; k++) {
        float a = 0;
        for (int n = 0; n < 2 * SUBC_M; n++) a += t.ca[k][n] * y[n];
        s[m][k] = a;
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
      lev[k] = (uint8_t)lo;
    }
  }
  // Разобранный кадр → bytes байт. coarse — грубый вид: масштабы по 4 бита (шаг 6 дБ) вместо 6.
  void pack(uint8_t *out, int bytes, bool coarse) const {
    const SubcTab &t = subcTab;
    uint8_t lv[SUBC_M], b[SUBC_M];
    float inv[SUBC_M];
    SubcBits w;
    w.p = out;
    for (int k = 0; k < SUBC_M; k++) {
      if (coarse) {
        int c = (lev[k] + 3) / 4;   // вверх: масштаб не меньше наибольшего отсчёта
        if (c > 15) c = 15;
        w.put((uint32_t)c, 4);
        lv[k] = (uint8_t)(c * 4);
      } else {
        w.put(lev[k], 6);
        lv[k] = lev[k];
      }
    }
    subcAlloc(lv, bytes * 8 - SUBC_M * (coarse ? 4 : 6), b);
    for (int k = 0; k < SUBC_M; k++) inv[k] = b[k] ? (float)(1 << (b[k] - 1)) / t.sf[lv[k] > 63 ? 63 : lv[k]] : 0;
    for (int m = 0; m < SUBC_NB; m++)
      for (int k = 0; k < SUBC_M; k++)
        if (b[k]) {   // равномерно, без нулевой ступени: номер ступени от нижнего края шкалы
          // (округление вниз — сдвигом в положительные числа и отбрасыванием дроби: floorf на плате стоил 0,15 мс на кадр)
          int top = (1 << b[k]) - 1, q = (int)(s[m][k] * inv[k] + 32768.0f) - 32768 + (1 << (b[k] - 1));
          w.put((uint32_t)(q < 0 ? 0 : q > top ? top : q), b[k]);
        }
    w.flush(bytes);
  }
};

struct SubcDec {
  float acc[SUBC_L];   // сборка с перекрытием: вклады прошлых блоков в ещё не выданные отсчёты
  uint32_t rnd = 0x1234567u;
  void reset() { memset(acc, 0, sizeof(acc)); }
  // Кадр (bytes байт; coarse — грубого вида) → 128 отсчётов. in == NULL — кадра нет: набор «доигрывает» хвост
  // прошлых блоков и уходит в тишину.
  void decode(const uint8_t *in, int bytes, bool coarse, int16_t *out) {
    const SubcTab &t = subcTab;
    uint8_t lv[SUBC_M], b[SUBC_M];
    float step[SUBC_M], sf[SUBC_M];
    SubcBits r;
    r.p = (uint8_t *)in;
    if (in) {
      int sfBits = coarse ? 4 : 6;
      for (int k = 0; k < SUBC_M; k++) {
        uint32_t v = r.get(sfBits);
        lv[k] = (uint8_t)(coarse ? v * 4 : v);
        if (lv[k] > 63) lv[k] = 63;
      }
      subcAlloc(lv, bytes * 8 - SUBC_M * sfBits, b);
      for (int k = 0; k < SUBC_M; k++) {
        sf[k] = t.sf[lv[k]];
        step[k] = b[k] ? sf[k] / (float)(1 << (b[k] - 1)) : 0;
      }
    }
    for (int m = 0; m < SUBC_NB; m++) {
      float s[SUBC_M];
      for (int k = 0; k < SUBC_M; k++) {
        if (!in) s[k] = 0;
        else if (b[k]) s[k] = ((float)r.get(b[k]) + 0.5f) * step[k] - sf[k];   // середина ступени
        else if (lv[k] > 8) {   // бит не досталось: шум того же уровня (вполовину масштаба), чтобы полоса не «мигала»
          rnd = rnd * 1664525u + 1013904223u;
          s[k] = ((int32_t)rnd >> 8) * (1.0f / 8388608.0f) * 0.5f * sf[k];
        } else s[k] = 0;
      }
      block(s, out + m * SUBC_M);
    }
  }
  // сборка одного блока: восемь отсчётов полос → восемь отсчётов звука
  void block(const float *s, int16_t *out) {
    const SubcTab &t = subcTab;
    float v[2 * SUBC_M];
    for (int n = 0; n < 2 * SUBC_M; n++) {
      float a = 0;
      for (int k = 0; k < SUBC_M; k++) a += t.cs[k][n] * s[k];
      v[n] = a;
    }
    for (int n = 0; n < SUBC_L; n++) acc[n] += t.ws[n] * v[n & (2 * SUBC_M - 1)];
    for (int i = 0; i < SUBC_M; i++) {
      float y = acc[i];
      out[i] = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : (int)(y + 32768.5f) - 32768);   // округление без lrintf
    }
    memmove(acc, acc + SUBC_M, sizeof(float) * (SUBC_L - SUBC_M));
    memset(acc + SUBC_L - SUBC_M, 0, sizeof(float) * SUBC_M);
  }
};
