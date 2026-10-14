// Откуда «зернистость» звука приёмника. Владелец 14.10: «Проверил на идеальном уровне сигнала на двух цапах на
// средней громкости… на тестовом звуке… и на записанной музыке хорошего качества — присутствует неприятная
// зернистость звука (все шумодавы отключил)».
// Ступени тракта повторены здесь один в один: на передатчике — середина и разность каналов (txaudio.h), разность
// в «найвищій» стерео — ADPCM (proto.h); на приёмнике (rxaudio.h) — подгонка темпа по таблице sinc, эквалайзер (eq.h),
// «чёткость», громкость, усиление, сжатие и паузы (nsfx.h), ограничитель, перевод в 16 бит. Каждая мерится отдельно:
//  1) интермодуляция SMPTE (60 Гц + 7 кГц, 4 : 1) — «грязь» от усиления, которое меняется в такт басу;
//  2) подгонка темпа: синусы 1…15,5 кГц при бегущей фазе (часы плат разнятся на ~100 ppm; у CS4344 шаг 1,024);
//  3) перевод в 16 бит на тихом звуке;
//  4) музыка: ошибка каждой ступени по полосам частот против того же тракта без неё, с настройками приёмников.
//   c++ -std=c++17 -O2 -DFW_VERSION=\"0.0\" -Ibuild/host -Ihearlink tools/host/rxchain.cpp -o build/host/rxchain
//   ./build/host/rxchain [музыка: s16le стерео 32 кГц] [папка для записей]
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <complex>
#include <vector>
#include <string>
#define SRATE 32000
#define FRAME 64
#ifndef PI
#define PI 3.14159265358979f
#endif
enum { Q_HI = 0, Q_STD, Q_SPEECH, Q_FAR, Q_COUNT };
#define PROTO_HOST
#include "proto_host.h"
#include "eq.h"
#include "nsfx.h"

static const double TAU = 6.283185307179586;
typedef std::vector<double> Sig;
static double db(double v) { return 20 * log10(v + 1e-30); }
static double dbp(double p) { return 10 * log10(p + 1e-30); }

// ---- амплитуда тона на окне (Блэкман–Харрис), прямым счётом
static double toneAmp(const Sig &x, size_t from, size_t n, double f) {
  double re = 0, im = 0, wsum = 0;
  for (size_t i = 0; i < n; i++) {
    double t = (double)i / (n - 1), w = 0.35875 - 0.48829 * cos(TAU * t) + 0.14128 * cos(2 * TAU * t) - 0.01168 * cos(3 * TAU * t);
    double ph = TAU * f * (from + i) / SRATE;
    re += x[from + i] * w * cos(ph);
    im += x[from + i] * w * sin(ph);
    wsum += w;
  }
  return 2 * sqrt(re * re + im * im) / wsum;
}

// ---- спектр мощности (Уэлч: окно Ханна 4096, шаг 2048) и мощность по полосам
static void fft(std::vector<std::complex<double>> &a) {
  size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; i++) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    std::complex<double> wl(cos(-TAU / len), sin(-TAU / len));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1);
      for (size_t k = 0; k < len / 2; k++) {
        std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}
enum { NF = 4096 };
static std::vector<double> psd(const Sig &x, size_t from, size_t to) {
  std::vector<double> p(NF / 2 + 1, 0.0);
  std::vector<std::complex<double>> a(NF);
  int cnt = 0;
  for (size_t s = from; s + NF <= to; s += NF / 2, cnt++) {
    for (int i = 0; i < NF; i++) a[i] = x[s + i] * (0.5 - 0.5 * cos(TAU * i / NF));
    fft(a);
    for (int k = 0; k <= NF / 2; k++) p[k] += std::norm(a[k]);
  }
  for (double &v : p) v /= cnt ? cnt : 1;
  return p;
}
static const double BAND[] = { 20, 250, 500, 1000, 2000, 4000, 6000, 8000, 10000, 12000, 13000, 14000, 15000, 16000 };
enum { NB = sizeof(BAND) / sizeof(BAND[0]) - 1 };
static void bands(const std::vector<double> &p, double fs, double *out) {
  for (int b = 0; b < NB; b++) {
    out[b] = 0;
    for (int k = 0; k <= NF / 2; k++) {
      double f = (double)k * fs / NF;
      if (f >= BAND[b] && (f < BAND[b + 1] || (b == NB - 1 && f <= BAND[b + 1]))) out[b] += p[k];
    }
  }
}
static const double FS_POW = (double)NF * NF * 0.09375 * 32767.0 * 32767.0;   // синус полной шкалы в этих единицах

// ---- подгонка темпа: таблица sinc (rsBegin в rxaudio.h: 24 отсчёта × 128 строк, окно Кайзера β 8,5, срез на
// половине частоты входа) и чтение по фазе, как в 2.84. Образец для сравнения — та же схема с длинной таблицей.
struct Rs {
  int taps, ph;
  std::vector<float> tab;
  static double i0(double x) {
    double s = 1, t = 1;
    for (int k = 1; k < 60; k++) {
      t *= (x / (2 * k)) * (x / (2 * k));
      s += t;
    }
    return s;
  }
  Rs(int taps_, int ph_, double beta, double cut = 1.0) : taps(taps_), ph(ph_), tab((ph_ + 1) * taps_) {
    const double i0b = i0(beta);
    const int h = taps / 2;
    for (int p = 0; p <= ph; p++) {
      double sum = 0;
      float *c = &tab[p * taps];
      for (int j = 0; j < taps; j++) {
        double d = (j - (h - 1)) - p / (double)ph, u = d / h, w = fabs(u) < 1 ? i0(beta * sqrt(1 - u * u)) / i0b : 0;
        double x = PI * d * cut;
        c[j] = (float)((fabs(x) < 1e-9 ? 1.0 : sin(x) / x) * w);
        sum += c[j];
      }
      for (int j = 0; j < taps; j++) c[j] = (float)(c[j] / sum);
    }
  }
  // ratio — входных отсчётов на выходной; выход начинается с одного и того же места при любой таблице
  Sig run(const Sig &in, double ratio, size_t nOut) const {
    Sig out(nOut, 0.0);
    uint64_t step = (uint64_t)(ratio * 4294967296.0);
    uint32_t frac = 0;
    size_t tail = 300;
    int shift = 32 - (int)lround(log2((double)ph));   // номер строки — старшие биты доли
    for (size_t o = 0; o < nOut && tail + taps < in.size(); o++) {
      uint32_t pi = frac >> shift;
      float a = (float)(frac & ((1u << shift) - 1)) / (float)(1u << shift), acc = 0;
      const float *c0 = &tab[pi * taps], *c1 = c0 + taps;
      size_t base = tail - (taps / 2 - 1);
      for (int j = 0; j < taps; j++) acc += (c0[j] + (c1[j] - c0[j]) * a) * (float)in[base + j];
      out[o] = acc;
      uint64_t s = (uint64_t)frac + step;
      tail += (uint32_t)(s >> 32);
      frac = (uint32_t)s;
    }
    return out;
  }
};

// ---- выходной тракт приёмника (rxaudio.h, цикл по отсчётам блока)
struct Chain {
  int volume = 20, boostDb = 0, comp = 0, gate = 0, gateThr = 1, gateRel = 1, clarity = 0;
  uint8_t eqBands[5] = { 6, 6, 6, 6, 6 };
  bool lowCut = false, stereoOut = true;
  int limBox = 0;   // 0 — ограничитель как в прошивке до 2.86 (окно 48, атака одним звеном); N — окно N, атака «скользящим средним» за всё окно
  bool limDq = false;   // наименьшее по окну — очередью, как в прошивке 2.86 (иначе — пересмотром окна; должно совпасть)
};
static float volGain(int step) { return step <= 0 ? 0 : step >= 20 ? 1 : powf(10.0f, (step - 20) * 2.0f / 20.0f); }
struct Out {
  Sig L, R;        // на выходе, до перевода в 16 бит
  Sig preL, preR;  // то же до множителя ограничителя и сжатия (уже задержанное на окно ограничителя)
  Sig g;           // этот множитель
  double limLowDb = 0, limBusy = 0;
};
static Out runChain(const Chain &c, const Sig &mid, const Sig &side, double fs = SRATE) {
  Eq6 eq;
  memset(eq.z, 0, sizeof(eq.z));
  eq.build(c.eqBands, c.lowCut, (float)fs);
  NsDyn dyn;
  dyn.boostDb = (float)c.boostDb;
  dyn.comp = c.comp;
  dyn.gate = c.gate;
  dyn.gateThr = c.gateThr;
  dyn.gateRel = c.gateRel;
  struct Shelf {
    float z1 = 0, z2 = 0;
    inline float run(float x) {
      float y = 3.287168f * x + z1;
      z1 = -5.286367f * x + 1.247790f * y + z2;
      z2 = 2.213098f * x - 0.461690f * y;
      return y;
    }
  } shM, shS;
  enum { LIM_MAX = 256 };
  const int LIM_LA = c.limBox ? c.limBox : 48;
  float limL[LIM_MAX] = {}, limR[LIM_MAX] = {}, limNeed[LIM_MAX], boxBuf[LIM_MAX];
  for (float &v : limNeed) v = 1;
  for (float &v : boxBuf) v = 1;
  int limPos = 0, limMinAge = 0, boxPos = 0, boxN = 0;
  float limMin = 1, limG = 1, limR1 = 1, boxSum = (float)LIM_LA;
  enum { LIM_DQ = 256 };
  float dqV[LIM_DQ];
  uint32_t dqT[LIM_DQ], limT = 0, dqH = 0, dqE = 0;
  static const float CL_K[4] = { 0, 0.1962f, 0.5072f, 1.0f };
  const float want = volGain(c.volume), clWant = CL_K[c.clarity & 3], boostWant = powf(10.0f, c.boostDb * 0.05f);
  const float limit = 32767.0f, LIM_ATT = 1 - expf(-1.0f / 10.0f), LIM_REL = 1 - expf(-1.0f / (SRATE * 0.25f)), LIM_REL_FAST = 1 - expf(-1.0f / (SRATE * 0.04f));
  const float limRel = c.comp && c.boostDb ? LIM_REL_FAST : LIM_REL;
  float gain = want, clK = clWant, bK = boostWant;   // всё уже установилось
  Out o;
  size_t n = mid.size();
  o.L.assign(n, 0); o.R.assign(n, 0); o.preL.assign(n, 0); o.preR.assign(n, 0); o.g.assign(n, 1);
  for (size_t i = 0; i < n; i++) {
    float y = (float)mid[i], ys = c.stereoOut ? (float)side[i] : 0;
    int a = (int)fabsf(y);
    gain += (want - gain) * 0.002f;
    if (eq.any) {
      y = eq.run(0, y);
      if (c.stereoOut) ys = eq.run(1, ys);
    }
    clK += (clWant - clK) * 0.001f;
    float yc = y + clK * (shM.run(y) - y), ysc = c.stereoOut ? ys + clK * (shS.run(ys) - ys) : 0;
    float vl = (yc + ysc) * gain, vr = (yc - ysc) * gain, av = fabsf(vl) > fabsf(vr) ? fabsf(vl) : fabsf(vr);
    bK += (boostWant - bK) * 0.002f;
    float dynG = dyn.run(av, (float)a, bK);
    vl *= bK;
    vr *= bK;
    av *= bK * dyn.tRed;
    float need = av > limit ? limit / av : 1.0f;
    float dl = limL[limPos], dr = limR[limPos];
    limL[limPos] = vl;
    limR[limPos] = vr;
    limNeed[limPos] = need;
    limPos = limPos + 1 == LIM_LA ? 0 : limPos + 1;
    if (c.limDq) {   // как в прошивке 2.86
      limT++;
      while (dqE != dqH && dqV[(dqE - 1) & (LIM_DQ - 1)] >= need) dqE--;
      dqV[dqE & (LIM_DQ - 1)] = need;
      dqT[dqE & (LIM_DQ - 1)] = limT;
      dqE++;
      while (limT - dqT[dqH & (LIM_DQ - 1)] >= (uint32_t)LIM_LA) dqH++;
      limMin = dqV[dqH & (LIM_DQ - 1)];
    } else if (need <= limMin) {
      limMin = need;
      limMinAge = 0;
    } else if (++limMinAge >= LIM_LA) {
      limMin = 2;
      for (int k = 0; k < LIM_LA; k++) {
        int idx = limPos - 1 - k;
        if (idx < 0) idx += LIM_LA;
        if (limNeed[idx] < limMin) {
          limMin = limNeed[idx];
          limMinAge = k;
        }
      }
    }
    if (!c.limBox) limG += (limMin - limG) * (limMin < limG ? LIM_ATT : limRel);
    else {
      // плавная атака: сначала «отпускание» (вниз — сразу, вверх — звеном), потом среднее за окно по ПРОШЛЫМ
      // значениям — каждое из них не больше нужного выходящему отсчёту, поэтому и среднее не больше: перегрузки нет
      if (limMin < limR1) limR1 = limMin;
      else limR1 += (limMin - limR1) * limRel;
      limG = boxSum * (1.0f / LIM_LA);
      boxSum += limR1 - boxBuf[boxPos];
      boxBuf[boxPos] = limR1;
      boxPos = boxPos + 1 == LIM_LA ? 0 : boxPos + 1;
      if (++boxN >= 4096) {   // накопленную ошибку суммы — пересчётом
        boxN = 0;
        boxSum = 0;
        for (int k = 0; k < LIM_LA; k++) boxSum += boxBuf[k];
      }
    }
    o.preL[i] = dl;
    o.preR[i] = dr;
    o.g[i] = limG * dynG;
    o.L[i] = dl * limG * dynG;
    o.R[i] = dr * limG * dynG;
    if (limG < 0.999f) o.limBusy++;
    if (db(limG) < o.limLowDb) o.limLowDb = db(limG);
  }
  o.limBusy /= n;
  return o;
}

static Sig loadStereo(const char *path, Sig &R) {
  Sig L;
  FILE *f = fopen(path, "rb");
  if (!f) return L;
  int16_t b[4096];
  size_t n;
  while ((n = fread(b, 2, 4096, f)) > 0)
    for (size_t i = 0; i + 1 < n; i += 2) {
      L.push_back(b[i]);
      R.push_back(b[i + 1]);
    }
  fclose(f);
  return L;
}
static void saveWav(const std::string &path, const Sig &L, const Sig &R, double k, uint32_t sr) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return;
  uint32_t n = (uint32_t)L.size(), br = sr * 4, ds = n * 4, rs = 36 + ds, fl = 16;
  uint16_t fmt = 1, ch = 2, ba = 4, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fl, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
  fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
  for (uint32_t i = 0; i < n; i++) {
    double v[2] = { L[i] * k, R[i] * k };
    for (double x : v) {
      int16_t s = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : lround(x));
      fwrite(&s, 2, 1, f);
    }
  }
  fclose(f);
}

struct Prof { const char *tag, *name; Chain c; double ratio, fsOut; };
// настройки приёмников владельца, прочитанные 14.10 около 13:10 (P и «@… ?» через передатчик)
static std::vector<Prof> owner() {
  std::vector<Prof> v;
  { Chain c; c.volume = 20; c.boostDb = 24; uint8_t e[5] = { 0, 0, 4, 6, 9 }; memcpy(c.eqBands, e, 5); c.lowCut = true;
    v.push_back({ "884A94", "884A94: CS4344, гучність 20/20, підсилення +24 дБ, стискання 0, у паузах 0, еквалайзер −12 −12 −4 0 +6, зріз низів", c, 1.024 * (1 + 60e-6), 31250 }); }
  { Chain c; c.volume = 2; c.boostDb = 20; c.gate = 2; c.gateThr = 2; c.gateRel = 2; uint8_t e[5] = { 0, 0, 4, 4, 12 }; memcpy(c.eqBands, e, 5); c.lowCut = true;
    v.push_back({ "4C3B10", "4C3B10: PCM5102, гучність 2/20, підсилення +20 дБ, стискання 0, у паузах 2, еквалайзер −12 −12 −4 −4 +12, зріз низів", c, 1 - 60e-6, 32000 }); }
  return v;
}

int main(int argc, char **argv) {
  const Rs fw(24, 128, 8.5), ref(512, 1024, 12.0, 0.985);
  const Rs fw2(48, 128, 8.6, 0.94), ref2(512, 1024, 12.0, 0.94);   // 2.86 и её образец с тем же срезом
  const size_t N = SRATE * 2, W = SRATE;
  std::vector<Prof> prof = owner();

  printf("=== 1. Інтермодуляція SMPTE: 60 Гц + 7 кГц (4 : 1), пік −1 дБ; продукти 7 кГц ± 60·k, k = 1…4 ===\n");
  {
    Sig mid(N), side(N, 0.0);
    for (size_t i = 0; i < N; i++) mid[i] = 0.8 * 32767 * 0.891 * sin(TAU * 60 * i / SRATE) + 0.2 * 32767 * 0.891 * sin(TAU * 7000 * i / SRATE);
    struct Case { std::string name; Chain c; };
    std::vector<Case> cases;
    { Chain c; cases.push_back({ "гучність 100 %, без підсилення (звук як є)", c }); }
    for (int b : { 6, 12, 20, 24 }) { Chain c; c.boostDb = b; cases.push_back({ "гучність 100 %, підсилення +" + std::to_string(b) + " дБ, стискання 0 (лише обмежувач)", c }); }
    for (int cp = 1; cp <= 3; cp++) { Chain c; c.boostDb = 24; c.comp = cp; cases.push_back({ "гучність 100 %, підсилення +24 дБ, стискання " + std::to_string(cp), c }); }
    for (auto &p : prof) cases.push_back({ p.name, p.c });
    for (auto &cs : cases) {
      Chain c = cs.c;
      memset(c.eqBands, 6, 5);   // эквалайзер здесь мешал бы мерить (срезал бы 60 Гц) — только громкость и динамика
      c.lowCut = false;
      Out o = runChain(c, mid, side);
      size_t from = N - W - 2000;
      double a7 = toneAmp(o.L, from, W, 7000), im = 0;
      for (int k = 1; k <= 4; k++) {
        double lo = toneAmp(o.L, from, W, 7000 - 60 * k), hi = toneAmp(o.L, from, W, 7000 + 60 * k);
        im += lo * lo + hi * hi;
      }
      printf("  IMD %6.2f %% (%6.1f дБ), обмежувач до %5.1f дБ — %s\n", 100 * sqrt(im) / a7, db(sqrt(im) / a7), o.limLowDb, cs.name.c_str());
    }
  }

  printf("\n=== 2. Підгонка темпу: синус −6 дБ; рівень після неї і все, крім нього (дзеркальні частоти, спотворення), дБ ===\n");
  printf("  %-9s %-30s%-30s%-30s%-30s\n", "", "до 2.86, PCM5102 (1 − 60 ppm)", "до 2.86, CS4344 (крок 1,024)", "2.86, PCM5102", "2.86, CS4344");
  for (double f : { 1000.0, 5000.0, 10000.0, 12000.0, 13000.0, 14000.0, 14500.0, 15000.0, 15500.0 }) {
    Sig x(N + 4000);
    for (size_t i = 0; i < x.size(); i++) x[i] = 0.5 * 32767 * sin(TAU * f * i / SRATE);
    printf("  %5.0f Гц ", f);
    for (int k = 0; k < 4; k++) {
      double ratio = k & 1 ? 1.024 : 1 - 60e-6;
      const Rs &r = k >= 2 ? fw2 : fw;
      Sig y = r.run(x, ratio, N);
      double fo = f * ratio, re = 0, im = 0;
      size_t from = 3000, n = W;
      for (size_t i = 0; i < n; i++) { re += y[from + i] * cos(TAU * fo * i / SRATE); im += y[from + i] * sin(TAU * fo * i / SRATE); }
      re *= 2.0 / n; im *= 2.0 / n;
      double e = 0, s = 0;
      for (size_t i = 0; i < n; i++) {
        double rf = re * cos(TAU * fo * i / SRATE) + im * sin(TAU * fo * i / SRATE);
        e += (y[from + i] - rf) * (y[from + i] - rf);
        s += rf * rf;
      }
      char b[64];
      snprintf(b, sizeof b, "рівень %+5.1f, решта %6.1f", db(sqrt(re * re + im * im) / (0.5 * 32767)), dbp(e / s));
      printf("%-30s", b);
    }
    printf("\n");
  }

  printf("\n=== 3. Переведення в 16 біт на тихому місці (синус 1 кГц на −60 дБ після гучності) ===\n");
  {
    Sig x(N);
    for (size_t i = 0; i < N; i++) x[i] = 32767 * 0.001 * sin(TAU * 1000 * i / SRATE);
    for (int mode = 0; mode < 3; mode++) {
      Sig q(N);
      uint32_t r = 1;
      for (size_t i = 0; i < N; i++) {
        double v = x[i];
        if (mode == 0) q[i] = (double)(int16_t)v;
        else if (mode == 1) q[i] = floor(v + 0.5);
        else {
          r = r * 1664525u + 1013904223u; double d1 = (r >> 8) / 16777216.0;
          r = r * 1664525u + 1013904223u; double d2 = (r >> 8) / 16777216.0;
          q[i] = floor(v + d1 - d2 + 0.5);
        }
      }
      double a1 = toneAmp(q, 8000, W, 1000), h = 0;
      for (int k = 2; k <= 9; k++) { double ak = toneAmp(q, 8000, W, 1000.0 * k); h += ak * ak; }
      printf("  %-44s гармоніки %6.1f дБ до сигналу\n", mode == 0 ? "відкидання дробу (зараз у прошивці)" : mode == 1 ? "округлення" : "округлення з підмішуванням шуму", db(sqrt(h) / a1));
    }
  }

  if (argc < 2) return 0;
  Sig R0, L0 = loadStereo(argv[1], R0);
  if (L0.size() < (size_t)SRATE * 20) { printf("мало звуку в %s\n", argv[1]); return 1; }
  std::string dir = argc > 2 ? argv[2] : "";
  // на передатчике: середина и разность (txTestDuck, без голоса), разность — ADPCM кадрами по 64 (как blkS)
  size_t n = L0.size() / FRAME * FRAME;
  if (n > (size_t)SRATE * 90) n = SRATE * 90;   // полторы минуты
  Sig M(n), S(n), Sa(n);
  for (size_t i = 0; i < n; i++) {
    M[i] = (double)(int16_t)((L0[i] + R0[i]) * 0.5f);
    S[i] = (double)(int16_t)((L0[i] - R0[i]) * 0.5f);
  }
  {
    int idx = 0;
    for (size_t p = 0; p + FRAME <= n; p += FRAME) {
      int16_t in[FRAME], out[FRAME];
      uint8_t blk[3 + FRAME / 2];
      for (int i = 0; i < FRAME; i++) in[i] = (int16_t)S[p + i];
      adpcmEncodeBlockBest(in, FRAME, blk, idx);
      adpcmDecodeBlock(blk, FRAME, out);
      for (int i = 0; i < FRAME; i++) Sa[p + i] = out[i];
    }
  }
  // одна таблица: ошибка каждой ступени против того же тракта без неё (как до 2.86 и как в 2.86)
  auto table = [&](const Prof &p, bool v286) {
    Chain c = p.c;
    if (v286) {
      c.limBox = 96;
      c.limDq = true;
    }
    const Rs &K = v286 ? fw2 : fw, &R = v286 ? ref2 : ref;
    size_t nOut = (size_t)((n - 1200) / p.ratio);
    Sig mR = R.run(M, p.ratio, nOut), sR = R.run(S, p.ratio, nOut), saR = R.run(Sa, p.ratio, nOut);
    Sig mF = K.run(M, p.ratio, nOut), sF = K.run(S, p.ratio, nOut);
    Out base = runChain(c, mR, sR, p.fsOut), side = runChain(c, mR, saR, p.fsOut), rs = runChain(c, mF, sF, p.fsOut);
    // быстрые изменения множителя: опорный — тот же, сглаженный вперёд-назад (около 30 Гц)
    Sig gs = base.g;
    const double k = 1 - exp(-TAU * 30 / p.fsOut);
    for (size_t i = 1; i < gs.size(); i++) gs[i] = gs[i - 1] + (gs[i] - gs[i - 1]) * k;
    for (size_t i = gs.size() - 1; i-- > 0;) gs[i] = gs[i + 1] + (gs[i] - gs[i + 1]) * k;
    Sig eSide(nOut), eRs(nOut), eLim(nOut), eQ(nOut), eSideR(nOut), eRsR(nOut);
    size_t over = 0;
    for (size_t i = 0; i < nOut; i++) {
      eSide[i] = v286 ? 0 : side.L[i] - base.L[i];   // в 2.86 разность приходит без сжатия
      eSideR[i] = v286 ? 0 : side.R[i] - base.R[i];
      eRs[i] = rs.L[i] - base.L[i];
      eRsR[i] = rs.R[i] - base.R[i];
      eLim[i] = base.L[i] - base.preL[i] * gs[i];
      double v = base.L[i] > 32767 ? 32767 : base.L[i] < -32768 ? -32768 : base.L[i];
      if (i >= (size_t)SRATE && (fabs(base.L[i]) > 32767.5 || fabs(base.R[i]) > 32767.5)) over++;
      eQ[i] = v286 ? (double)(int32_t)(float)(v * 65536.0) / 65536.0 - v : (double)(int16_t)v - v;   // 24 бита (с 2.86) / 16 с отбрасыванием дроби
    }
    if (v286) {   // ограничитель на очереди должен совпасть с ним же на пересмотре окна
      Chain c2 = c;
      c2.limDq = false;
      Out chk = runChain(c2, mR, sR, p.fsOut);
      double d = 0;
      for (size_t i = 0; i < nOut; i++) d = fmax(d, fabs(chk.L[i] - base.L[i]));
      printf("  (наименьше за вікно чергою і переглядом вікна: найбільша розбіжність %.3g)\n", d);
    }
    size_t from = SRATE * 3, to = nOut - 100;
    double bs[NB], b1[NB], b2[NB], b3[NB], b4[NB];
    bands(psd(base.L, from, to), p.fsOut, bs);
    bands(psd(eSide, from, to), p.fsOut, b1);
    bands(psd(eRs, from, to), p.fsOut, b2);
    bands(psd(eLim, from, to), p.fsOut, b3);
    bands(psd(eQ, from, to), p.fsOut, b4);
    printf("\n  %s — %s\n  обмежувач: найглибше %.1f дБ, працює %.0f %% часу; відліків за шкалою (зріз) %zu\n", p.name,
           v286 ? "2.86" : "як до 2.86", base.limLowDb, 100 * base.limBusy, over);
    printf("  %-13s %8s %9s %8s %8s %8s\n", "смуга, Гц", "сигнал", "різниця", "темп", "обмеж.", v286 ? "24 біти" : "16 біт");
    double tot[5] = {};
    for (int b = 0; b < NB; b++) {
      char nm[32];
      snprintf(nm, sizeof nm, "%.0f–%.0f", BAND[b], BAND[b + 1]);
      if (v286 && BAND[b] >= 13000)   // образец с тем же срезом круче таблицы: выше 13 кГц разница — спад верха, не искажения (см. часть 2)
        printf("  %-13s %8.1f %9s %8s %8.1f %8.1f\n", nm, dbp(bs[b] / FS_POW), "—", "спад", dbp(b3[b] / bs[b]), dbp(b4[b] / bs[b]));
      else if (v286) printf("  %-13s %8.1f %9s %8.1f %8.1f %8.1f\n", nm, dbp(bs[b] / FS_POW), "—", dbp(b2[b] / bs[b]), dbp(b3[b] / bs[b]), dbp(b4[b] / bs[b]));
      else printf("  %-13s %8.1f %9.1f %8.1f %8.1f %8.1f\n", nm, dbp(bs[b] / FS_POW), dbp(b1[b] / bs[b]), dbp(b2[b] / bs[b]), dbp(b3[b] / bs[b]), dbp(b4[b] / bs[b]));
      tot[0] += bs[b]; tot[1] += b1[b]; tot[2] += b2[b]; tot[3] += b3[b]; tot[4] += b4[b];
    }
    if (v286) printf("  %-13s %8.1f %9s %8.1f %8.1f %8.1f\n", "усього", dbp(tot[0] / FS_POW), "—", dbp(tot[2] / tot[0]), dbp(tot[3] / tot[0]), dbp(tot[4] / tot[0]));
    else printf("  %-13s %8.1f %9.1f %8.1f %8.1f %8.1f\n", "усього", dbp(tot[0] / FS_POW), dbp(tot[1] / tot[0]), dbp(tot[2] / tot[0]), dbp(tot[3] / tot[0]), dbp(tot[4] / tot[0]));
    if (!dir.empty()) {   // для прослуховування: звук як у приладі (до 2.86 — з 4-бітною різницею) і похибки, підняті на 20 дБ
      uint32_t sr = (uint32_t)lround(p.fsOut);
      std::string tag = std::string(p.tag) + (v286 ? "-2.86" : "-2.85");
      Out all = v286 ? runChain(c, mF, sF, p.fsOut) : runChain(c, mF, K.run(Sa, p.ratio, nOut), p.fsOut);
      saveWav(dir + "/" + tag + "-yak-u-pryladi.wav", all.L, all.R, 1, sr);
      if (!v286) saveWav(dir + "/" + tag + "-pohybka-riznytsi-plus20db.wav", eSide, eSideR, 10, sr);
      saveWav(dir + "/" + tag + "-pohybka-tempu-plus20db.wav", eRs, eRsR, 10, sr);
    }
  };
  printf("\n=== 4. Музика %s: похибка кожного ступеня за смугами, дБ до сигналу лівого каналу в тій самій смузі ===\n", argv[1]);
  printf("  сигнал — рівень смуги на виході, дБ від синуса повної шкали; «різниця» — 4-бітна різниця каналів «найвищої»\n"
         "  стерео; «темп» — таблиця підгонки темпу проти зразкової з тим самим зрізом; «обмеж.» — швидкі (понад 30 Гц)\n"
         "  зміни множника обмежувача, стискання і пауз; «16 біт» / «24 біти» — переведення в ціле на виході\n");
  for (auto &p : prof) {
    table(p, false);
    table(p, true);
  }

  // 5. Ограничитель: нынешний (атака одним звеном, окно 1,5 мс) против атаки «скользящим средним» за всё окно.
  // На музыке с настройками 884A94 (громкость 100 %, +24 дБ, сжатие выключено — ограничитель убавляет 20–25 дБ):
  // насколько быстро «гуляет» множитель (полосы частот самих изменений усиления, дБ к среднему множителю), сколько
  // отсчётов вышло за шкалу, и средняя громкость — чтобы сравнивать равное с равным.
  printf("\n=== 5. Обмежувач на музиці з налаштуваннями 884A94 і SMPTE (+24 дБ, стискання 0) ===\n");
  printf("  %-26s %8s %8s %8s %8s %8s %8s %9s %8s\n", "варіант", "<30 Гц", "30–100", "100–300", "0,3–1к", "1–4 кГц", ">4 кГц", "за шкалу", "IMD, дБ");
  {
    const Prof &p = prof[0];
    size_t nOut = (size_t)((n - 1200) / p.ratio);
    Sig mR = ref.run(M, p.ratio, nOut), sR = ref.run(S, p.ratio, nOut);
    Sig mid(N), side0(N, 0.0);
    for (size_t i = 0; i < N; i++) mid[i] = 0.8 * 32767 * 0.891 * sin(TAU * 60 * i / SRATE) + 0.2 * 32767 * 0.891 * sin(TAU * 7000 * i / SRATE);
    for (int lb : { 0, 48, 64, 96, 128 }) {
      Chain c = p.c;
      c.limBox = lb;
      Out o = runChain(c, mR, sR, p.fsOut);
      // множитель в децибелах, без среднего; его спектр — по полосам частот изменения
      Sig gdb(nOut);
      double mean = 0;
      for (size_t i = 0; i < nOut; i++) mean += gdb[i] = 20 * log10(o.g[i] + 1e-9);
      mean /= nOut;
      for (double &v : gdb) v -= mean;
      std::vector<double> ps = psd(gdb, SRATE * 3, nOut - 100);
      static const double MB[] = { 0.5, 30, 100, 300, 1000, 4000, 16000 };
      double mb[6] = {};
      for (int k = 1; k <= NF / 2; k++) {
        double f = (double)k * p.fsOut / NF;
        for (int b = 0; b < 6; b++)
          if (f >= MB[b] && f < MB[b + 1]) mb[b] += ps[k];
      }
      // в дБ «гуляния» множителя: корень из мощности (окно Ханна: 0,375·NF² на единицу дисперсии)
      size_t over = 0;
      for (size_t i = SRATE; i < nOut; i++) over += fabs(o.L[i]) > 32767.5 || fabs(o.R[i]) > 32767.5;
      Chain ci = c;
      memset(ci.eqBands, 6, 5);
      ci.lowCut = false;
      Out oi = runChain(ci, mid, side0);
      size_t from = N - W - 2000;
      double a7 = toneAmp(oi.L, from, W, 7000), im = 0;
      for (int k = 1; k <= 4; k++) {
        double lo = toneAmp(oi.L, from, W, 7000 - 60 * k), hi = toneAmp(oi.L, from, W, 7000 + 60 * k);
        im += lo * lo + hi * hi;
      }
      char nm[48];
      snprintf(nm, sizeof nm, lb ? "середнє за вікно %d (%.1f мс)" : "як до 2.86 (вікно 48)", lb, lb * 1000.0 / SRATE);
      printf("  %-26s", nm);
      for (int b = 0; b < 6; b++) printf(" %8.1f", 10 * log10(mb[b] / (0.375 * NF * NF / 2) + 1e-30));
      printf(" %9zu %8.1f   (середній множник %.1f дБ)\n", over, db(sqrt(im) / a7), mean);
    }
  }
  return 0;
}
