// Шумозаглушение приёмника в стерео (hearlink/nsfx.h, NsSpec::runF) — на компьютере, тем же кодом и теми же блоками по
// 32 отсчёта, что в приборе. Владелец 14.10 (2.88): «при включении шумодава появляются щелчки при работе звука» и
// «также пропадает один канал при шумодаве». Проверяется:
//  1) уровень 0 (обработка выключена, звук только задерживается): Л и П на выходе — те же, что на входе (середина и
//     разность задержаны одинаково);
//  2) музыка, сведённая в одну сторону (Л — музыка, П — только шум входа): сколько музыки протекает в П и сколько
//     теряет Л;
//  3) стерео-музыка с шумом: как меняются уровни Л и П и сколько в выходе «щелчков» (скачков, каких нет во входе).
//   c++ -std=c++17 -O2 -Ihearlink tools/host/nsstereo.cpp -o build/host/nsstereo && ./build/host/nsstereo <музыка s16 стерео 32 кГц>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include "nsfx.h"

typedef std::vector<float> Sig;
static const int SR = 32000, BLK = 32, LAG = NSX_N - BLK;   // задержка середины в обработке (как lag в runF)

static void loadStereo(const char *p, Sig &L, Sig &R) {
  FILE *f = fopen(p, "rb");
  if (!f) return;
  int16_t b[4096];
  size_t n;
  while ((n = fread(b, 2, 4096, f)) > 0)
    for (size_t i = 0; i + 1 < n; i += 2) {
      L.push_back(b[i]);
      R.push_back(b[i + 1]);
    }
  fclose(f);
}
static uint32_t rnd = 777;
static float white() {
  float a = 0;
  for (int i = 0; i < 6; i++) {
    rnd = rnd * 1664525u + 1013904223u;
    a += (rnd >> 8) * (1.0f / 16777216) - 0.5f;
  }
  return a * 1.41421f;
}
static double db(double e, size_t n) { return n && e > 0 ? 10 * log10(e / n / (32768.0 * 32768.0)) : -150; }

// прогон через NsSpec: на входе Л и П, на выходе Л и П (задержанные на LAG)
static void runNs(int level, const Sig &L, const Sig &R, Sig &oL, Sig &oR, size_t learnFrom, size_t learnTo) {
  static NsSpec ns;
  static float work[2 * NSX_N] __attribute__((aligned(16)));
  ns.reset();
  ns.level = (uint8_t)level;
  ns.sens = 2;
  size_t n = L.size() / BLK * BLK;
  oL.assign(n, 0);
  oR.assign(n, 0);
  float m[BLK], s[BLK];
  for (size_t p = 0; p < n; p += BLK) {
    if (p == learnFrom) ns.learnStart();   // как «запам'ятати шум зараз»: секунда, где только шум
    for (int i = 0; i < BLK; i++) {
      m[i] = (L[p + i] + R[p + i]) * 0.5f;
      s[i] = (L[p + i] - R[p + i]) * 0.5f;
    }
    ns.runF(m, s, BLK, true, work);
    for (int i = 0; i < BLK; i++) {
      oL[p + i] = m[i] + s[i];
      oR[p + i] = m[i] - s[i];
    }
  }
  (void)learnTo;
}

// «щелчок»: отсчёт, где вторая разность выхода больше 8 средних по соседним 2 мс и больше 300 единиц, а у входа
// (задержанного) в этом месте — нет
static int clicks(const Sig &out, const Sig &in, size_t from, size_t to, std::vector<size_t> *where = nullptr) {
  int c = 0;
  for (size_t i = from + 64; i + 64 < to; i++) {
    auto d2 = [&](const Sig &x, size_t k) { return fabsf(x[k + 1] - 2 * x[k] + x[k - 1]); };
    float a = d2(out, i), loc = 0, li = 0;
    if (a < 300) continue;
    for (int k = -32; k <= 32; k++) if (k) { loc += d2(out, i + k); }
    loc /= 64;
    size_t j = i - LAG;
    if (j < 2) continue;
    li = d2(in, j);
    if (a > 8 * loc && a > 3 * li + 300) {
      c++;
      if (where && where->size() < 10) where->push_back(i);
      i += 32;
    }
  }
  return c;
}

int main(int argc, char **argv) {
  if (argc < 2) { printf("нужна музыка s16 стерео 32 кГц\n"); return 1; }
  Sig mL, mR;
  loadStereo(argv[1], mL, mR);
  size_t mus = mL.size() > (size_t)SR * 60 ? SR * 60 : mL.size();
  const size_t intro = SR * 4;   // 4 с только шума — чтобы шумозаглушение его выучило
  const float noise = 32768 * powf(10, -50 / 20.0f);   // шум входа −50 дБ, в каждом канале свой
  int bad = 0;

  // 1) уровень 0: выход = вход, задержанный на LAG
  {
    Sig L(intro + mus), R(intro + mus), oL, oR;
    for (size_t i = 0; i < L.size(); i++) {
      L[i] = (i >= intro ? mL[i - intro] : 0) + noise * white();
      R[i] = (i >= intro ? mR[i - intro] : 0) + noise * white();
    }
    runNs(0, L, R, oL, oR, 0, 0);
    double eL = 0, eR = 0, sL = 0;
    size_t cnt = 0;
    for (size_t i = SR; i < oL.size(); i++, cnt++) {
      eL += (oL[i] - L[i - LAG]) * (oL[i] - L[i - LAG]);
      eR += (oR[i] - R[i - LAG]) * (oR[i] - R[i - LAG]);
      sL += L[i - LAG] * L[i - LAG];
    }
    printf("1) обработка выключена (уровень 0): ошибка Л %.1f дБ, П %.1f дБ к сигналу (должно быть ниже −90)\n", 10 * log10(eL / sL + 1e-30), 10 * log10(eR / sL + 1e-30));
    if (10 * log10(eL / sL + 1e-30) > -90 || 10 * log10(eR / sL + 1e-30) > -90) bad++;
  }

  // 2) музыка только в Л (П — только шум входа): протекание в П и потеря Л — при каждом уровне
  for (int level = 1; level <= 3; level++) {
    Sig L(intro + mus), R(intro + mus), oL, oR, mono(intro + mus);
    for (size_t i = 0; i < L.size(); i++) {
      float x = i >= intro ? (mL[i - intro] + mR[i - intro]) * 0.5f : 0;
      L[i] = x + noise * white();
      R[i] = noise * white();
      mono[i] = x;
    }
    runNs(level, L, R, oL, oR, SR, SR * 2);
    double inL = 0, outL = 0, outR = 0, inR = 0;
    size_t cnt = 0;
    for (size_t i = intro + SR; i < oL.size(); i++, cnt++) {
      inL += L[i - LAG] * L[i - LAG];
      inR += R[i - LAG] * R[i - LAG];
      outL += oL[i] * oL[i];
      outR += oR[i] * oR[i];
    }
    printf("2) музыка только в Л, уровень %d: Л %+.1f дБ к входу; П на выходе %.1f дБ (на входе там шум %.1f дБ) — протекание музыки в П\n",
           level, 10 * log10(outL / inL), db(outR, cnt), db(inR, cnt));
    if (db(outR, cnt) > db(inR, cnt) + 6) bad++;
  }

  // 4) тихая музыка только в Л, вровень с шумом (−40 дБ при шуме −50): здесь шумозаглушение вправду режет полосы.
  // Протекание в П — разность выходов «с музыкой» и «без музыки» при одном и том же шуме (её дала музыка), к музыке в Л;
  // и сколько музыки осталось в Л (то же — разностью выходов)
  for (int level = 1; level <= 3; level++) {
    Sig L(intro + mus), R(intro + mus), L0(intro + mus), R0(intro + mus), oL, oR, oL0, oR0;
    double k = 0, e = 0;
    for (size_t i = intro; i < intro + mus; i++) {
      float x = (mL[i - intro] + mR[i - intro]) * 0.5f;
      e += (double)x * x;
    }
    k = 32768 * pow(10, -40 / 20.0) / sqrt(e / mus);   // музыка — на −40 дБ
    rnd = 4242;
    for (size_t i = 0; i < L.size(); i++) {
      float x = i >= intro ? (float)((mL[i - intro] + mR[i - intro]) * 0.5f * k) : 0;
      float nl = noise * white(), nr = noise * white();
      L[i] = x + nl; R[i] = nr; L0[i] = nl; R0[i] = nr;
    }
    runNs(level, L, R, oL, oR, SR, SR * 2);
    runNs(level, L0, R0, oL0, oR0, SR, SR * 2);
    double mus2 = 0, leak = 0, keep = 0;
    for (size_t i = intro + SR; i < oL.size(); i++) {
      double x = L[i - LAG] - L0[i - LAG];
      mus2 += x * x;
      leak += (oR[i] - oR0[i]) * (oR[i] - oR0[i]);
      keep += (oL[i] - oL0[i]) * (oL[i] - oL0[i]);
    }
    printf("4) тихая музыка только в Л (−40 дБ, шум −50), уровень %d: в Л осталось %+.1f дБ музыки; протекло в П %.1f дБ к ней\n",
           level, 10 * log10(keep / mus2), 10 * log10(leak / mus2 + 1e-30));
  }

  // 3) стерео-музыка с шумом: уровни каналов и щелчки
  for (int level = 0; level <= 3; level++) {
    Sig L(intro + mus), R(intro + mus), oL, oR;
    for (size_t i = 0; i < L.size(); i++) {
      L[i] = (i >= intro ? mL[i - intro] : 0) + noise * white();
      R[i] = (i >= intro ? mR[i - intro] : 0) + noise * white();
    }
    runNs(level, L, R, oL, oR, SR, SR * 2);
    double iL = 0, iR = 0, eL = 0, eR = 0;
    for (size_t i = intro + SR; i < oL.size(); i++) {
      iL += L[i - LAG] * L[i - LAG];
      iR += R[i - LAG] * R[i - LAG];
      eL += oL[i] * oL[i];
      eR += oR[i] * oR[i];
    }
    std::vector<size_t> wl;
    int cl = clicks(oL, L, intro + SR, oL.size(), &wl), cr = clicks(oR, R, intro + SR, oR.size());
    int ci = clicks(L, L, intro + SR + LAG, L.size());   // сам вход — для сравнения (задержка не та, поэтому почти всё — «чужое»)
    (void)ci;
    printf("3) стерео-музыка, уровень %d: Л %+.1f дБ, П %+.1f дБ к входу; «щелчков» Л %d, П %d за %.0f с", level, 10 * log10(eL / iL), 10 * log10(eR / iR), cl, cr,
           (oL.size() - intro - SR) / (double)SR);
    for (size_t k = 0; k < wl.size() && k < 4; k++) printf("%s%.3f с", k ? ", " : " (Л: ", (wl[k] - intro) / (double)SR);
    printf("%s\n", wl.empty() ? "" : ")");
  }
  return bad ? 1 : 0;
}
