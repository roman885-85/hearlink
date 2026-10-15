// Подгонка темпа приёмника (2.97): новая свёртка rsDot (hearlink/rsdot.h) против прежней — отвод за отводом прямо в
// rxTask (до 2.96). Та же таблица (rsBegin из rxaudio.h: 48 отводов × 128 строк, окно Кайзера β 8,6, срез 0,94),
// то же кольцо на 8192 отсчёта и шаг темпа, что у CS4344 (31 250 из 32 000) и у PCM5102 (32 000 ± 300 ppm). Мерится
// расхождение выходов в единицах младшего разряда 16-битного звука — должно быть порядка ошибок округления float, а
// окна, переходящие через конец кольца, — совпадать с прочими.
//   c++ -std=c++17 -O2 -Ihearlink tools/host/rsdot.cpp -o build/host/rsdot && ./build/host/rsdot [музыка s16 стерео 32 кГц]
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include "rsdot.h"

enum { RING = 8192, PH = 128, TAPS = 48 };
static float tab[(PH + 1) * TAPS];

static float rsI0(float x) {   // как в rxaudio.h
  float s = 1, t = 1;
  for (int k = 1; k < 30; k++) {
    t *= (x / (2 * k)) * (x / (2 * k));
    s += t;
  }
  return s;
}
static void rsBegin() {   // как в rxaudio.h (PI там — число double из Arduino)
  const double PI = 3.1415926535897932384626433832795;
  const float beta = 8.6f, cut = 0.94f, i0b = rsI0(beta);
  const int h = TAPS / 2;
  for (int p = 0; p <= PH; p++) {
    float sum = 0, *c = tab + p * TAPS;
    for (int j = 0; j < TAPS; j++) {
      float d = (j - (h - 1)) - p / (float)PH, u = d / h, w = fabsf(u) < 1 ? rsI0(beta * sqrtf(1 - u * u)) / i0b : 0;
      float x = PI * d * cut;
      c[j] = (fabsf(x) < 1e-6f ? 1.0f : sinf(x) / x) * w;
      sum += c[j];
    }
    for (int j = 0; j < TAPS; j++) c[j] /= sum;
  }
}

int main(int argc, char **argv) {
  rsBegin();
  std::vector<int16_t> m, s;
  if (argc > 1) {
    FILE *f = fopen(argv[1], "rb");
    int16_t b[2];
    while (f && fread(b, 2, 2, f) == 2) {
      m.push_back((int16_t)((b[0] + b[1]) / 2));
      s.push_back((int16_t)((b[0] - b[1]) / 2));
    }
    if (f) fclose(f);
  }
  uint32_t r = 12345;
  if (m.size() < 200000) {   // музыки нет — шум во всю шкалу
    m.resize(400000);
    s.resize(400000);
    for (size_t i = 0; i < m.size(); i++) {
      r = r * 1664525u + 1013904223u;
      m[i] = (int16_t)(r >> 16);
      r = r * 1664525u + 1013904223u;
      s[i] = (int16_t)(r >> 16);
    }
  }
  static int16_t ringM[RING], ringS[RING];
  int bad = 0;
  const double ratios[3] = { 31250.0 / 32000.0, 1.0003, 0.9997 };   // CS4344; PCM5102 с подстройкой в обе стороны
  for (double ratio : ratios) {
    uint64_t step = (uint64_t)(1.0 / ratio * 4294967296.0);   // столько входа на отсчёт выхода (rxRateBase: вход/выход)
    uint32_t head = 0, tail = 0, frac = 0;
    double maxD = 0, sumD = 0, maxWrapD = 0;
    size_t n = 0, wraps = 0, src = 0;
    while (src + 64 < m.size() && n < 1500000) {
      while ((int32_t)(head - tail) < 200 && src < m.size()) {   // запас в кольце — как в приборе (десяток мс)
        ringM[head & (RING - 1)] = m[src];
        ringS[head & (RING - 1)] = s[src];
        head++;
        src++;
      }
      uint32_t pi = frac >> 25;
      float a = (float)(frac & 0x1FFFFFFu) * (1.0f / 33554432.0f);
      // прежний счёт (rxaudio.h до 2.96)
      float accM = 0, accS = 0;
      {
        const float *c0 = tab + pi * TAPS, *c1 = c0 + TAPS;
        uint32_t base = tail - (TAPS / 2 - 1);
        for (int j = 0; j < TAPS; j++) {
          float c = c0[j] + (c1[j] - c0[j]) * a;
          accM += c * ringM[(base + j) & (RING - 1)];
          accS += c * ringS[(base + j) & (RING - 1)];
        }
      }
      // новый (как в rxaudio.h с 2.97)
      float y = 0, ys = 0;
      {
        uint32_t base = (tail - (TAPS / 2 - 1)) & (RING - 1);
        const int16_t *pm = ringM + base, *ps = ringS + base;
        int16_t tm[TAPS], ts[TAPS];
        bool wrap = base + TAPS > RING;
        if (wrap) {
          for (int j = 0; j < TAPS; j++) {
            tm[j] = ringM[(base + j) & (RING - 1)];
            ts[j] = ringS[(base + j) & (RING - 1)];
          }
          pm = tm;
          ps = ts;
          wraps++;
        }
        rsDot(tab + pi * TAPS, a, TAPS, pm, ps, &y, &ys);
        double d = fmax(fabs((double)y - accM), fabs((double)ys - accS));
        if (wrap && d > maxWrapD) maxWrapD = d;
      }
      double d = fmax(fabs((double)y - accM), fabs((double)ys - accS));
      if (d > maxD) maxD = d;
      sumD += d * d;
      n++;
      uint64_t acc = (uint64_t)frac + step;
      tail += (uint32_t)(acc >> 32);
      frac = (uint32_t)acc;
    }
    printf("темп %.5f: отсчётов %zu (через конец кольца %zu): расхождение с прежним счётом наиб. %.5f, ср.кв. %.6f единицы 16 бит; на окнах через конец кольца наиб. %.5f\n",
           ratio, n, wraps, maxD, sqrt(sumD / (n ? n : 1)), maxWrapD);
    // float хранит 24 значащих бита: у суммы около полной шкалы (32 768) его собственный шаг — 0,002–0,004 единицы, и
    // 48 слагаемых в другом порядке расходятся на несколько таких шагов (на шуме во всю шкалу — до 0,03). Порог 0,05
    // единицы 16 бит — это −150 дБ к полной шкале, на порядки ниже младшего бита ЦАП.
    if (maxD > 0.05 || !wraps) bad++;
  }
  printf("%s\n", bad ? "НЕ В РАМКАХ" : "в рамках (порядок ошибок округления float)");
  return bad ? 1 : 0;
}
