// Проверка обработки звука «для слуха» (hearlink/nsfx.h) на компьютере: шумозаглушение и сжатие.
//   build/host/nsfx [папка с assets] [папка для записей WAV] [файл шума s16le 32 кГц — вместо искусственного]
// Печатает цифры «было → стало» и кладёт записи для прослушивания. Код возврата 1 — если что-то вышло за рамки.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "nsfx.h"

typedef std::vector<float> Sig;
static const int SR = 32000;

static Sig loadPcm(const std::string &p) {
  Sig s;
  FILE *f = fopen(p.c_str(), "rb");
  if (!f) return s;
  int16_t b[4096];
  size_t n;
  while ((n = fread(b, 2, 4096, f)) > 0)
    for (size_t i = 0; i < n; i++) s.push_back(b[i]);
  fclose(f);
  return s;
}
static void saveWav(const std::string &p, const Sig &s) {
  FILE *f = fopen(p.c_str(), "wb");
  if (!f) return;
  uint32_t n = (uint32_t)s.size() * 2, sr = SR, br = SR * 2, riff = 36 + n, fmt = 16;
  uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
  fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f);
  for (float v : s) {
    int16_t x = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : lrintf(v));
    fwrite(&x, 2, 1, f);
  }
  fclose(f);
}
static double rmsDb(const Sig &s, size_t a, size_t b) {
  double e = 0;
  size_t n = 0;
  for (size_t i = a; i < b && i < s.size(); i++, n++) e += (double)s[i] * s[i];
  return n && e > 0 ? 10 * log10(e / n / (32768.0 * 32768.0)) : -120;
}
static float peakOf(const Sig &s) {
  float p = 1;
  for (float v : s) p = fabsf(v) > p ? fabsf(v) : p;
  return p;
}
static uint32_t rnd = 12345;
static float white() {   // примерно нормальный шум с единичным разбросом
  float a = 0;
  for (int i = 0; i < 6; i++) {
    rnd = rnd * 1664525u + 1013904223u;
    a += (rnd >> 8) * (1.0f / 16777216) - 0.5f;
  }
  return a * 1.41421f;
}
static Sig noiseLike(size_t n, float rmsDbfs, const Sig &real) {
  Sig s(n);
  float k = 32768 * powf(10, rmsDbfs / 20);
  if (real.size() > SR) {
    for (size_t i = 0; i < n; i++) s[i] = real[i % real.size()];
    return s;
  }
  for (size_t i = 0; i < n; i++) s[i] = white() * k;
  return s;
}
// шумозаглушение кадрами по 64, как в приборе
static Sig runNs(const Sig &in, int level, bool learnFirst, float *avgRed = nullptr) {
  static NsSpec ns;
  static float work[NSX_N];
  ns.reset();
  ns.frozen = false;
  ns.level = (uint8_t)level;
  if (learnFirst) ns.learnStart();
  Sig out(in.size());
  double red = 0;
  int nr = 0;
  for (size_t p = 0; p + 64 <= in.size(); p += 64) {
    int16_t m[64], s[64] = {};
    for (int i = 0; i < 64; i++) m[i] = (int16_t)lrintf(in[p + i]);
    ns.run(m, s, 64, false, work);
    for (int i = 0; i < 64; i++) out[p + i] = m[i];
    red += ns.redDb;
    nr++;
  }
  if (avgRed) *avgRed = nr ? (float)(red / nr) : 0;
  return out;
}
static const int NS_DELAY = 192;
// Разрывы звука, какие бывают у приёмника: короткий провал (запас кончился — спад к нулю, тишина, быстрый вход) и
// долгая пропажа с плавным входом за 0,4 с. До 2.76 после любого такого места «самое тихое за десять минут»
// оказывалось ниже настоящего шума (уровень проходил через него на спаде или на входе), и пауз шумозаглушение
// больше не находило. Возвращает, сколько шагов сочтено паузой и сколько в среднем убрано в паузах записи.
static void gapTest(const Sig &rec, const char *name, int everyS, int gapMs, bool slowIn) {
  static NsSpec ns;
  static float work[NSX_N];
  Sig x = rec;
  size_t n = x.size();
  if (everyS)
    for (size_t at = SR * 3; at + SR < n; at += (size_t)SR * everyS) {
      size_t g = (size_t)SR * gapMs / 1000, in = slowIn ? SR * 2 / 5 : 64;
      float last = x[at];
      for (size_t i = 0; i < g; i++) {
        last *= 0.9f;
        x[at + i] = last;
      }
      for (size_t i = 0; i < in && at + g + i < n; i++) {
        float f = (float)i / in;
        x[at + g + i] *= slowIn ? f * f : f;
      }
    }
  ns.reset();
  ns.level = 2;
  ns.sens = 2;
  double red = 0;
  int nr = 0;
  // запись короткая (десятки секунд) — прогнать её несколько раз подряд, как идёт эфир
  for (int rep = 0; rep < 4; rep++)
    for (size_t p = 0; p + 64 <= n; p += 64) {
      int16_t m[64], sd[64] = {};
      for (int i = 0; i < 64; i++) m[i] = (int16_t)lrintf(x[p + i]);
      ns.run(m, sd, 64, false, work);
      if (rep == 3 && rmsDb(rec, p, p + 64) < -60) {
        red += ns.redDb;
        nr++;
      }
    }
  printf("  %-46s шагов в паузах %6u; в паузах убирает в среднем %5.1f дБ\n", name, (unsigned)ns.pauseHops, nr ? red / nr : 0.0);
}
// мощность ошибки относительно чистого образца (со сдвигом на задержку), дБ: «сигнал/шум» вместе с искажениями
static double snrDb(const Sig &clean, const Sig &test, size_t a, size_t b, int delay) {
  double ps = 0, pe = 0;
  for (size_t i = a; i < b && i + delay < test.size(); i++) {
    double c = clean[i], e = test[i + delay] - c;
    ps += c * c;
    pe += e * e;
  }
  return pe > 0 ? 10 * log10(ps / pe) : 99;
}

// ограничитель приёмника — как в rxaudio.h (заглядывание 1,5 мс, отпускание 0,25 с)
struct Lim {
  enum { LA = 48 };
  float relS = 0.25f;   // отпускание, с: 0,25 — как до 2.68; со сжатием — 0,04
  float d[LA] = {}, need[LA], g = 1, mn = 1;
  int pos = 0, age = 0;
  long busy = 0;
  float low = 1;
  Lim() { for (float &v : need) v = 1; }
  // v — отсчёт с усилением; tRed — во сколько раз его ещё убавит сжатие (цель): ограничитель смотрит на итог
  float run(float v, float tRed = 1) {
    const float ATT = 1 - expf(-1.0f / 10), REL = 1 - expf(-1.0f / (SR * relS));
    float a = fabsf(v) * tRed, nd = a > 32767 ? 32767 / a : 1, o = d[pos];
    d[pos] = v;
    need[pos] = nd;
    pos = pos + 1 == LA ? 0 : pos + 1;
    if (nd <= mn) { mn = nd; age = 0; }
    else if (++age >= LA) {
      mn = 2;
      for (int k = 0; k < LA; k++) {
        int i = pos - 1 - k;
        if (i < 0) i += LA;
        if (need[i] < mn) { mn = need[i]; age = k; }
      }
    }
    g += (mn - g) * (mn < g ? ATT : REL);
    if (g < 0.999f) busy++;
    if (g < low) low = g;
    return o * g;
  }
};
struct ChainOut { Sig y; double limBusyPct; float limLowDb; };
// каскад усиления приёмника: comp < 0 — как до 2.68 (множитель и ограничитель)
static ChainOut chain(const Sig &x, float boostDb, int comp, int gate) {
  ChainOut o;
  o.y.resize(x.size());
  NsDyn dyn;
  dyn.boostDb = boostDb;
  dyn.comp = (uint8_t)(comp < 0 ? 0 : comp);
  dyn.gate = (uint8_t)gate;
  Lim lim;
  if (comp > 0) lim.relS = 0.04f;
  float bl = powf(10, boostDb / 20);
  for (size_t i = 0; i < x.size(); i++) {
    float g = comp >= 0 ? dyn.run(fabsf(x[i]), fabsf(x[i]), bl) : 1;
    o.y[i] = lim.run(x[i] * bl, comp >= 0 ? dyn.tRed : 1) * g;
  }
  o.limBusyPct = 100.0 * lim.busy / (x.size() ? x.size() : 1);
  o.limLowDb = 20 * log10f(lim.low);
  return o;
}

// Настоящая запись эфира (с карты передатчика: 32 кГц, моно, s16le) через цепочку приёмника, как она настроена у
// владельца 11.10: подъём верхов +24 дБ («чёткость» 3 и верх эквалайзера +12), усиление +20 дБ. Шум в паузах, уровень
// речи и работа ограничителя — «как было» и с новой обработкой; записи для прослушивания.
struct Shelf {   // «чёткость» приёмника на полную: выше 2 кГц +12 дБ (звено из rxaudio.h)
  float z1 = 0, z2 = 0;
  float run(float x) {
    float y = 3.287168f * x + z1;
    z1 = -5.286367f * x + 1.247790f * y + z2;
    z2 = 2.213098f * x - 0.461690f * y;
    return y;
  }
};
static void realDemo(const Sig &rec, const std::string &outDir) {
  size_t n = rec.size();
  // паузы и речь — по самой записи: отрезки 50 мс, тише −60 дБ — пауза, громче −30 — речь
  std::vector<char> kind(n, 0);
  for (size_t i = 0; i + 1600 <= n; i += 1600) {
    double l = rmsDb(rec, i, i + 1600);
    for (size_t k = i; k < i + 1600; k++) kind[k] = l < -60 ? 1 : l > -30 ? 2 : 0;
  }
  auto part = [&](const Sig &s, int delay, char want) {
    double e = 0;
    size_t c = 0;
    for (size_t i = SR * 2; i + delay < n; i++)
      if (kind[i] == want) {
        e += (double)s[i + delay] * s[i + delay];
        c++;
      }
    return c && e > 0 ? 10 * log10(e / c / (32768.0 * 32768.0)) : -120.0;
  };
  printf("nsfx: настоящая запись эфира, %.0f с: шум в паузах %.1f дБ, речь %.1f дБ\n", n / (double)SR, part(rec, 0, 1), part(rec, 0, 2));
  struct Var { const char *name, *file; int txNs, rxNs, comp, gate; };
  const Var V[] = {
    { "как было (усиление +20, ограничитель)", "yak-bulo", 0, 0, -1, 0 },
    { "сжатие лёгкое", "styskannya", 0, 0, 1, 0 },
    { "сжатие и «в паузах — снять усиление»", "styskannya-pauzy", 0, 0, 1, 1 },
    { "то же и шумозаглушение приёмника 2", "styskannya-pauzy-shum-pryimacha-2", 0, 2, 1, 1 },
    { "то же, но шумозаглушение передатчика 2", "styskannya-pauzy-shum-peredavacha-2", 2, 0, 1, 1 },
    { "шумозаглушение передатчика 3 и приёмника 2", "vse-razom", 3, 2, 1, 1 },
  };
  for (const Var &v : V) {
    Sig x = rec;
    int delay = 0;
    if (v.txNs) {
      x = runNs(x, v.txNs, false);
      delay += NS_DELAY;
    }
    if (v.rxNs) {
      x = runNs(x, v.rxNs, false);
      delay += NS_DELAY;
    }
    Shelf a, b;
    NsDyn dyn;
    dyn.boostDb = 20;
    dyn.comp = (uint8_t)(v.comp < 0 ? 0 : v.comp);
    dyn.gate = (uint8_t)v.gate;
    Lim lim;
    if (v.comp > 0) lim.relS = 0.04f;
    Sig y(n);
    long clipped = 0;
    for (size_t i = 0; i < n; i++) {
      float pre = x[i], e = b.run(a.run(pre)) * 0.5f;   // верх +24 дБ; громкость — половина шкалы (ручка на 70 %)
      float g = v.comp >= 0 ? dyn.run(fabsf(e), fabsf(pre), 10) : 1;
      y[i] = lim.run(e * 10, v.comp >= 0 ? dyn.tRed : 1) * g;
      if (y[i] > 32767 || y[i] < -32768) clipped++;
    }
    delay += Lim::LA;
    printf("  %-44s шум в паузах %6.1f дБ; речь %6.1f дБ; ограничитель: занят %5.1f %% времени, глубже всего %5.1f дБ; отсчётов за краем шкалы %ld\n", v.name, part(y, delay, 1),
           part(y, delay, 2), 100.0 * lim.busy / n, 20 * log10f(lim.low), clipped);
    saveWav(outDir + "/propovid-" + v.file + ".wav", y);
  }
  saveWav(outDir + "/propovid-efir.wav", rec);
}

int main(int argc, char **argv) {
  std::string assets = argc > 1 ? argv[1] : "assets", outDir = argc > 2 ? argv[2] : "build/host/ns";
  Sig realNoise;
  if (argc > 3) {   // третьим — настоящая запись эфира: только она
    Sig rec = loadPcm(argv[3]);
    if (rec.size() < (size_t)SR * 4) {
      printf("nsfx: запись %s не прочиталась\n", argv[3]);
      return 1;
    }
    realDemo(rec, outDir);
    printf("nsfx: шумозаглушение и разрывы звука (сила 2, запись прогнана четыре раза подряд)\n");
    gapTest(rec, "без разрывов", 0, 0, false);
    gapTest(rec, "провал 30 мс каждые 7 с", 7, 30, false);
    gapTest(rec, "пропажа 1,5 с каждые 11 с, вход за 0,4 с", 11, 1500, true);
    return 0;
  }
  int fail = 0;
  Sig voice = loadPcm(assets + "/golos.pcm"), music = loadPcm(assets + "/oda.pcm");
  if (voice.size() < SR || music.size() < SR * 5) {
    printf("nsfx: нет записей в %s (golos.pcm, oda.pcm) — проверка на речи и музыке пропущена\n", assets.c_str());
    voice.assign(SR * 2, 0);
    for (size_t i = 0; i < voice.size(); i++) voice[i] = 6000 * sinf(2 * M_PI * 300 * i / SR) * (0.5f + 0.5f * sinf(2 * M_PI * 3 * i / SR));
    music.assign(SR * 12, 0);
    for (size_t i = 0; i < music.size(); i++) music[i] = 5000 * sinf(2 * M_PI * 440 * i / SR) + 3000 * sinf(2 * M_PI * 1320 * i / SR);
  }
  // речь: фраза, пауза 0,9 с — и так 14 секунд; пики около −12 дБ
  float vk = 32768 * powf(10, -12 / 20.0f) / peakOf(voice);
  size_t per = voice.size() + (size_t)(SR * 0.9), total = (size_t)SR * 14;
  Sig clean(total, 0);
  std::vector<char> isSpeech(total, 0);
  for (size_t i = 0; i < total; i++) {
    size_t ph = i % per;
    if (ph < voice.size()) {
      clean[i] = voice[ph] * vk;
      isSpeech[i] = 1;
    }
  }
  const float NOISE_DB = -52;
  Sig nz = noiseLike(total, NOISE_DB, realNoise), noisy(total);
  for (size_t i = 0; i < total; i++) noisy[i] = clean[i] + nz[i];
  // участки для замера: после 4-й секунды (оценка шума уже установилась); пауза — середина промежутка между фразами
  auto pauseRms = [&](const Sig &s, int delay) {
    double e = 0;
    size_t n = 0;
    for (size_t i = SR * 4; i + delay < total; i++) {
      size_t ph = i % per;
      if (ph > voice.size() + SR * 0.25 && ph < per - SR * 0.1) {
        double v = s[i + delay];
        e += v * v;
        n++;
      }
    }
    return n && e > 0 ? 10 * log10(e / n / (32768.0 * 32768.0)) : -120.0;
  };
  auto speechSnr = [&](const Sig &s, int delay) {
    double ps = 0, pe = 0;
    for (size_t i = SR * 4; i + delay < total; i++)
      if (isSpeech[i]) {
        double c = clean[i], e = s[i + delay] - c;
        ps += c * c;
        pe += e * e;
      }
    return pe > 0 ? 10 * log10(ps / pe) : 99.0;
  };
  auto speechLevel = [&](const Sig &s, int delay) {
    double e = 0;
    size_t n = 0;
    for (size_t i = SR * 4; i + delay < total; i++)
      if (isSpeech[i]) {
        e += (double)s[i + delay] * s[i + delay];
        n++;
      }
    return n && e > 0 ? 10 * log10(e / n / (32768.0 * 32768.0)) : -120.0;
  };
  printf("nsfx: шумозаглушение — речь с паузами, шум %s\n", realNoise.size() > SR ? "записан с прибора" : "искусственный (белый)");
  printf("  без обработки:                 шум в паузах %6.1f дБ; речь к шуму %5.1f дБ; уровень речи %6.1f дБ\n", pauseRms(noisy, 0), speechSnr(noisy, 0), speechLevel(noisy, 0));
  saveWav(outDir + "/mova-0-yak-ye.wav", noisy);
  for (int learn = 0; learn < 2; learn++)
    for (int lv = 1; lv <= 3; lv++) {
      Sig in = noisy;
      if (learn) {   // «запомнить шум»: первая секунда — только шум
        for (size_t i = 0; i < (size_t)SR; i++) in[i] = nz[i];
      }
      Sig o = runNs(in, lv, learn);
      double pn = pauseRms(o, NS_DELAY), sn = speechSnr(o, NS_DELAY), sl = speechLevel(o, NS_DELAY);
      printf("  сила %d, шум %-22s шум в паузах %6.1f дБ (%+5.1f); речь к шуму %5.1f дБ (%+5.1f); уровень речи %6.1f дБ (%+4.1f)\n", lv,
             learn ? "запомнен:" : "оценивается сам:", pn, pn - pauseRms(noisy, 0), sn, sn - speechSnr(noisy, 0), sl, sl - speechLevel(noisy, 0));
      if (pn - pauseRms(noisy, 0) > -5) fail = 1;            // шум в паузах должен упасть хотя бы на 5 дБ
      if (sl - speechLevel(noisy, 0) < -1.5) fail = 1;       // речь не должна стать заметно тише
      if (sn < speechSnr(noisy, 0) - 0.5) fail = 1;          // и чище, а не грязнее
      if (!learn) saveWav(outDir + "/mova-" + std::to_string(lv) + ".wav", o);
    }
  {   // музыка: с шумом и без
    size_t n = music.size() < (size_t)SR * 14 ? music.size() : (size_t)SR * 14;
    float mk = 32768 * powf(10, -9 / 20.0f) / peakOf(music);
    Sig mc(n), mn(n);
    for (size_t i = 0; i < n; i++) {
      mc[i] = music[i] * mk;
      mn[i] = mc[i] + nz[i];
    }
    printf("nsfx: шумозаглушение — музыка\n");
    for (int learn = 0; learn < 2; learn++)
      for (int lv = 1; lv <= 3; lv++) {
        Sig inC = mc, inN = mn;
        if (learn)   // «запомнить шум»: первая секунда — только шум (для «чистой» музыки шум всё равно запоминается тот же)
          for (size_t i = 0; i < (size_t)SR; i++) inC[i] = inN[i] = nz[i];
        Sig a = runNs(inC, lv, learn), b = runNs(inN, lv, learn);
        double dist = -snrDb(mc, a, SR * 4, n - 1000, NS_DELAY), lvl = rmsDb(a, SR * 4 + NS_DELAY, n) - rmsDb(mc, SR * 4, n - NS_DELAY);
        double s0 = snrDb(mc, mn, SR * 4, n - 1000, 0), s1 = snrDb(mc, b, SR * 4, n - 1000, NS_DELAY);
        printf("  сила %d, шум %-17s чистая музыка — уровень %+.2f дБ, искажения %5.1f дБ; музыка с шумом — к шуму %5.1f → %5.1f дБ\n", lv, learn ? "запомнен:" : "оценивается сам:", lvl, dist, s0, s1);
        if (lvl < -0.3 || dist > -35) fail = 1;   // чистую музыку обработка портить не должна
        if (s1 < s0 - 0.3) fail = 1;              // и музыка с шумом не должна стать грязнее
        if (lv == 2 && !learn) {
          saveWav(outDir + "/muzyka-0-yak-ye.wav", mn);
          saveWav(outDir + "/muzyka-2.wav", b);
        }
      }
  }
  {   // как в жизни: шесть секунд речи с паузами (шум запоминается в них), затем сплошная музыка с тем же шумом
    size_t n1 = (size_t)SR * 6, n2 = (size_t)SR * 12, n = n1 + n2;
    float mk = 32768 * powf(10, -9 / 20.0f) / peakOf(music);
    Sig cl(n), ny(n);
    for (size_t i = 0; i < n; i++) {
      cl[i] = i < n1 ? clean[i] : music[i - n1] * mk;
      ny[i] = cl[i] + nz[i % nz.size()];
    }
    printf("nsfx: шумозаглушение — речь с паузами, затем музыка без пауз (шум оценивается сам)\n");
    for (int lv = 1; lv <= 3; lv++) {
      Sig o = runNs(ny, lv, false);
      double s0 = snrDb(cl, ny, n1 + SR, n - 1000, 0), s1 = snrDb(cl, o, n1 + SR, n - 1000, NS_DELAY);
      double lvl = rmsDb(o, n1 + SR + NS_DELAY, n) - rmsDb(ny, n1 + SR, n - NS_DELAY);
      printf("  сила %d: музыка к шуму %5.1f → %5.1f дБ (%+.1f); уровень музыки %+.2f дБ\n", lv, s0, s1, s1 - s0, lvl);
      if (s1 < s0 + 3 || lvl < -0.5) fail = 1;   // шум, запомненный в паузах речи, должен убираться и из музыки
      if (lv == 2) {
        saveWav(outDir + "/mova-potim-muzyka-0-yak-ye.wav", ny);
        saveWav(outDir + "/mova-potim-muzyka-2.wav", o);
      }
    }
  }
  {   // сжатие: «завал» после громкого и занятость ограничителя
    printf("nsfx: усиление +20 дБ — прежняя связка «множитель + ограничитель» и сжатие\n");
    // 1) громкий тон 0,3 с (−6 дБ), затем тихий (−40 дБ): каким усиление тихого будет через 20, 60, 120, 250 мс
    Sig t((size_t)(SR * 1.5));
    size_t onEnd = (size_t)(SR * 0.6);
    for (size_t i = 0; i < t.size(); i++) {
      float a = i < (size_t)(SR * 0.3) ? 0 : i < onEnd ? 32768 * powf(10, -6 / 20.0f) : 32768 * powf(10, -40 / 20.0f);
      t[i] = a * sinf(2 * (float)M_PI * 1000 * i / SR);
    }
    for (int c = -1; c <= 3; c++) {
      if (c == 0) continue;
      ChainOut o = chain(t, 20, c, 0);
      printf("  %-26s усиление тихого после громкого: ", c < 0 ? "как было (ограничитель):" : c == 1 ? "сжатие лёгкое:" : c == 2 ? "сжатие среднее:" : "сжатие сильное:");
      double gAt[4];
      int at[4] = { 20, 60, 120, 250 };
      for (int k = 0; k < 4; k++) {
        size_t a = onEnd + (size_t)(SR * at[k] / 1000.0) + Lim::LA, b = a + SR / 100;   // окно 10 мс
        gAt[k] = rmsDb(o.y, a, b) - rmsDb(t, a - Lim::LA, b - Lim::LA);
        printf("через %d мс %+5.1f дБ%s", at[k], gAt[k], k < 3 ? ", " : "");
      }
      printf("; громкий на выходе %5.1f дБ\n", rmsDb(o.y, onEnd - SR / 10, onEnd - SR / 50) + 3.0);
      if (c > 0 && (gAt[1] < 14 || gAt[2] < 18.5)) fail = 1;   // через 60 мс — уже больше прежнего, через 120 мс — почти всё усиление
    }
    // 2) речь с пиками −12 дБ: сколько времени работает ограничитель и насколько глубоко
    for (int c = -1; c <= 3; c++) {
      if (c == 0) continue;
      ChainOut o = chain(clean, 20, c, 0);
      printf("  речь, %-24s ограничитель занят %5.1f %% времени, глубже всего %5.1f дБ; уровень на выходе %5.1f дБ\n", c < 0 ? "как было:" : c == 1 ? "сжатие лёгкое:" : c == 2 ? "сжатие среднее:" : "сжатие сильное:",
             o.limBusyPct, o.limLowDb, rmsDb(o.y, SR * 4, total));
      if (c > 0 && o.limLowDb < -3) fail = 1;   // ограничителю остаются только короткие выбросы
      saveWav(outDir + (c < 0 ? "/pidsylennya-yak-bulo.wav" : "/pidsylennya-styskannya-" + std::to_string(c) + ".wav"), o.y);
    }
    // 3) паузы: шум с речью, усиление +20 дБ
    printf("nsfx: усиление +20 дБ и шум в паузах\n");
    for (int g = 0; g <= 3; g++) {
      ChainOut o = chain(noisy, 20, 1, g);
      double pn = pauseRms(o.y, Lim::LA), sl = speechLevel(o.y, Lim::LA);
      if (g && pn > -32.0 - 12) fail = 1;   // в паузах шум должен опуститься хотя бы на 12 дБ (в среднем по паузе: закрывается она плавно)
      printf("  в паузах: %-28s шум в паузах %6.1f дБ; уровень речи %6.1f дБ\n", g == 0 ? "ничего" : g == 1 ? "усиление снять" : g == 2 ? "снять и ещё −6 дБ" : "снять и ещё −12 дБ", pn, sl);
      if (g == 1) saveWav(outDir + "/pauzy-1.wav", o.y);
      if (g == 0) saveWav(outDir + "/pauzy-0.wav", o.y);
    }
  }
  printf("nsfx: %s; записи для прослушивания — в %s\n", fail ? "ЕСТЬ ВЫХОД ЗА РАМКИ" : "в рамках", outDir.c_str());
  return fail;
}
