// Проверка пересчёта частоты из hearlink/txmp3.h (та же арифметика) на компьютере.
//   c++ -O2 -o build/rs_test tools/host/rs_test.cpp && ./build/rs_test
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#define SRATE 32000
#define PI 3.14159265358979f
#define RS_TAPS 16
#define RS_PH 32
static float rsTab[RS_PH + 1][RS_TAPS];
static float rsHist[2][RS_TAPS * 2];
static int rsW;
static uint32_t rsRate, rsIn, rsNextI, rsNextF, rsStepI, rsStepF;
static std::vector<int16_t> out;
static void mpPut(int16_t l, int16_t) { out.push_back(l); }
static void rsSetup(uint32_t rate) {
  rsRate = rate == SRATE ? 0 : rate;
  rsIn = 0; rsNextI = 0; rsNextF = 0; rsW = 0;
  memset(rsHist, 0, sizeof(rsHist));
  if (!rsRate) return;
  rsStepI = rate / SRATE;
  rsStepF = (uint32_t)(((uint64_t)(rate % SRATE) << 32) / SRATE);
  float fc = (rate > SRATE ? (float)SRATE / rate : 1.0f) * 0.92f;
  for (int p = 0; p <= RS_PH; p++) {
    float sum = 0;
    for (int j = 0; j < RS_TAPS; j++) {
      float d = (j - (RS_TAPS / 2 - 1)) - (float)p / RS_PH, x = PI * fc * d;
      float s = fabsf(x) < 1e-6f ? 1.0f : sinf(x) / x;
      float u = d / (RS_TAPS / 2);
      float w = fabsf(u) >= 1 ? 0.0f : 0.42f + 0.5f * cosf(PI * u) + 0.08f * cosf(2 * PI * u);
      rsTab[p][j] = fc * s * w;
      sum += rsTab[p][j];
    }
    for (int j = 0; j < RS_TAPS; j++) rsTab[p][j] /= sum;
  }
}
static void rsPush(int16_t l, int16_t r) {
  if (!rsRate) { mpPut(l, r); return; }
  rsHist[0][rsW] = rsHist[0][rsW + RS_TAPS] = l;
  rsHist[1][rsW] = rsHist[1][rsW + RS_TAPS] = r;
  rsW = (rsW + 1) & (RS_TAPS - 1);
  uint32_t i = rsIn++;
  while ((int32_t)(i - rsNextI) >= RS_TAPS / 2) {
    int p = rsNextF >> 27;
    float a = (rsNextF & 0x07FFFFFF) * (1.0f / 134217728.0f), sl = 0, sr = 0;
    const float *c0 = rsTab[p], *c1 = rsTab[p + 1], *hl = rsHist[0] + rsW, *hr = rsHist[1] + rsW;
    for (int j = 0; j < RS_TAPS; j++) {
      float c = c0[j] + (c1[j] - c0[j]) * a;
      sl += hl[j] * c; sr += hr[j] * c;
    }
    int il = (int)lroundf(sl), ir = (int)lroundf(sr);
    mpPut(il > 32767 ? 32767 : il < -32768 ? -32768 : il, ir > 32767 ? 32767 : ir < -32768 ? -32768 : ir);
    uint32_t f = rsNextF + rsStepF;
    rsNextI += rsStepI + (f < rsNextF ? 1 : 0);
    rsNextF = f;
  }
}
int main() {
  static const uint32_t RATES[] = { 8000, 11025, 16000, 22050, 24000, 44100, 48000, 96000 };
  static const float TONES[] = { 300, 1000, 3000, 6000, 12000 };
  int bad = 0;
  for (uint32_t rate : RATES) {
    for (float f : TONES) {
      if (f > rate * 0.4f) continue;   // выше возможного для этой входной частоты
      rsSetup(rate);
      out.clear();
      int n = rate * 2;
      for (int i = 0; i < n; i++) rsPush((int16_t)lroundf(10000 * sinf(2 * PI * f * i / rate)), 0);
      // число отсчётов и сравнение с идеальным синусом (задержка фильтра: выход k соответствует времени k/32000)
      double want = n * 32000.0 / rate, err2 = 0, sig2 = 0;
      int cnt = 0;
      for (size_t k = 200; k + 200 < out.size(); k++) {
        double ideal = 10000 * sin(2 * 3.14159265358979 * f * k / 32000.0);
        err2 += (out[k] - ideal) * (out[k] - ideal);
        sig2 += ideal * ideal;
        cnt++;
      }
      double snr = 10 * log10(sig2 / (err2 + 1e-9));
      // Строго проверяются тоны в полосе, где фильтр обязан быть ровным (до четверти входной частоты и до 4 кГц): там ошибка
      // должна быть ниже −50 дБ. Выше — фильтр полого спадает (16 отводов), это спад громкости верхов, а не искажение:
      // такие строки только печатаются. Отсчётов меньше на «хвост» фильтра (8 входных отсчётов).
      bool strict = f <= rate * 0.25f && f <= 4000;
      bool ok = fabs((double)out.size() - want) < 8.0 * 32000 / rate + 4 && (!strict || snr > 50);
      if (!ok) bad++;
      printf("%6u Гц → 32000: тон %5.0f Гц: отсчётов %zu (ждали %.0f), сигнал/ошибка %.1f дБ %s\n", rate, f, out.size(), want, snr, ok ? (strict ? "" : " (спад верхов)") : "  ПЛОХО");
    }
  }
  printf(bad ? "ЕСТЬ ОШИБКИ: %d\n" : "всё сходится\n", bad);
  return bad != 0;
}
