// Проверка подполосного кодека дальнего режима (hearlink/subc.h) на компьютере.
//   c++ -std=c++17 -O2 -DFW_VERSION=\"0.0\" -Ibuild/host -Ihearlink -Itools/host tools/host/subc.cpp -o build/host/subc
//   ./build/host/subc <папка с записями 32 кГц моно s16 без заголовка> <папка для записей на прослушивание> [ещё записи…]
// 1) разбор и сборка без квантования: ошибка восстановления и задержка;
// 1а) быстрый счёт (с 2.82) против прежнего «в лоб» (subc_ref.h): отсчёты полос, биты кадра, звук, пропавшие кадры;
// 2) звук через кодек без потерь: сигнал/шум по всей полосе, до 8 кГц и «на слух приёмника» (подъём верха «чёткость 3»,
//    только до 8 кГц), по полосам, и в какой доле кадров полосе не досталось бит — рядом то же для прежних сжатий
//    («стандартна» 32 кГц и «мова» 16 кГц);
// 3) пакет 1 Мбит/с «кадр + прошлый кадр + грубая копия позапрошлого» при потерях 5, 15 и 25 %: сколько кадров взято
//    точной копией, сколько грубой, сколько пропало.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#define SRATE 32000
#define FRAME 64
enum { Q_HI = 0, Q_STD, Q_SPEECH, Q_FAR, Q_COUNT };
#define PROTO_HOST
#include "proto_host.h"
#include "subc.h"
namespace ref {   // прежний счёт «в лоб» (2.79–2.81): эталон для сверки быстрого
#include "subc_ref.h"
}

typedef std::vector<int16_t> Sig;
static Sig load(const std::string &p) {
  Sig s;
  FILE *f = fopen(p.c_str(), "rb");
  if (!f) return s;
  int16_t b[4096];
  size_t r;
  while ((r = fread(b, 2, 4096, f)) > 0) s.insert(s.end(), b, b + r);
  fclose(f);
  return s;
}
static void wav(const std::string &path, const Sig &x) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return;
  uint32_t n = x.size() * 2, v;
  uint16_t s;
  fwrite("RIFF", 1, 4, f); v = 36 + n; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
  v = SRATE; fwrite(&v, 4, 1, f); v = SRATE * 2; fwrite(&v, 4, 1, f); s = 2; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(x.data(), 2, x.size(), f);
  fclose(f);
}
struct Shelf {   // «чёткость 3» приёмника: +12 дБ выше 2 кГц (rxaudio.h)
  double z1 = 0, z2 = 0;
  double run(double x) {
    double y = 3.287168 * x + z1;
    z1 = -5.286367 * x + 1.247790 * y + z2;
    z2 = 2.213098 * x - 0.461690 * y;
    return y;
  }
};
struct Lp8 {   // срез выше 8 кГц (четыре звена подряд, грубо): то, чего слабослышащий всё равно не слышит
  double z[4] = { 0, 0, 0, 0 };
  double run(double x) {
    for (double &v : z) {
      v += 0.75 * (x - v);
      x = v;
    }
    return x;
  }
};
// сигнал/шум: по всей полосе; после среза 8 кГц; после подъёма верха и среза (то, что слышит человек с приёмником)
struct Snr { double all, lo, ear; };
static Snr snr(const Sig &ref, const Sig &out, int delay) {
  double se = 0, ss = 0, seL = 0, ssL = 0, seE = 0, ssE = 0;
  Lp8 l1, l2, l3, l4;
  Shelf s1, s2;
  for (size_t i = 0; i + delay < out.size() && i < ref.size(); i++) {
    double x = ref[i], e = (double)out[i + delay] - ref[i];
    double xl = l1.run(x), el = l2.run(e), xe = l3.run(s1.run(x)), ee = l4.run(s2.run(e));
    if (i < SRATE / 10) continue;
    se += e * e; ss += x * x; seL += el * el; ssL += xl * xl; seE += ee * ee; ssE += xe * xe;
  }
  return { 10 * log10(ss / (se + 1e-9)), 10 * log10(ssL / (seL + 1e-9)), 10 * log10(ssE / (seE + 1e-9)) };
}
static int bestDelay(const Sig &ref, const Sig &out, int maxD) {
  double best = -1e9;
  int bd = 0;
  for (int d = 0; d <= maxD; d++) {
    double se = 0, ss = 0;
    for (size_t i = SRATE / 10; i + d < out.size() && i < ref.size() && i < (size_t)SRATE * 4; i++) {
      double e = (double)out[i + d] - ref[i];
      se += e * e; ss += (double)ref[i] * ref[i];
    }
    double v = 10 * log10(ss / (se + 1e-9));
    if (v > best) { best = v; bd = d; }
  }
  return bd;
}
// прежние сжатия — как в tools/host/qual.cpp
static Sig adpcmPath(const Sig &in, int q) {
  const QDef &d = QDEF[q];
  Decim2 dec;
  Interp2 up;
  int idx = 0;
  uint8_t blk[3 + Q_MAX_N / 2];
  int16_t frm[Q_MAX_N], tmp[Q_MAX_N], u[Q_MAX_N * 2];
  Sig out;
  for (size_t pos = 0; pos + d.chunks * FRAME <= in.size(); pos += d.chunks * FRAME) {
    const int16_t *src = &in[pos];
    if (d.sr != SRATE) { dec.run((int16_t *)&in[pos], d.n, frm); src = frm; }
    adpcmEncodeBlockBest(src, d.n, blk, idx);
    adpcmDecodeBlock(blk, d.n, tmp);
    if (d.sr == SRATE) out.insert(out.end(), tmp, tmp + d.n);
    else { up.run(tmp, d.n, u); out.insert(out.end(), u, u + d.n * 2); }
  }
  return out;
}

// весь звук через кодек одним видом кадра (bytes байт, coarse — грубый)
static Sig codeAll(const Sig &x, int bytes, bool coarse, long *zero = nullptr, double *bitsSum = nullptr, long *frames = nullptr) {
  SubcEnc e;
  SubcDec d;
  e.reset();
  d.reset();
  Sig y;
  uint8_t buf[256];
  for (size_t p = 0; p + SUBC_N <= x.size(); p += SUBC_N) {
    int16_t o[SUBC_N];
    e.analyse(&x[p]);
    e.pack(buf, bytes, coarse);
    if (zero) {   // статистика распределения бит — тем же способом, что кодек
      SubcBits r;
      r.p = buf;
      uint8_t lv[SUBC_M], b[SUBC_M];
      for (int k = 0; k < SUBC_M; k++) lv[k] = (uint8_t)r.get(6);
      subcAlloc(lv, bytes * 8 - 48, b);
      for (int k = 0; k < SUBC_M; k++) { bitsSum[k] += b[k]; zero[k] += !b[k] && lv[k] > 8; }
      (*frames)++;
    }
    d.decode(buf, bytes, coarse, o);
    y.insert(y.end(), o, o + SUBC_N);
  }
  return y;
}
// Пакет при потерях. lay — виды кадров в пакете: [0] — новый кадр, [1] — прошлый, [2] — позапрошлый…
struct Lay { int bytes; bool coarse; };
struct LossRes { long pk, drop, good[4], lost; Snr q; Sig y; };
static LossRes lossRun(const Sig &x, const std::vector<Lay> &lay, int loss) {
  SubcEnc e;
  SubcDec d;
  e.reset();
  d.reset();
  srand(5);
  LossRes r = {};
  int nc = (int)lay.size() - 1;   // копий
  std::vector<std::vector<std::vector<uint8_t>>> hist(nc + 1, std::vector<std::vector<uint8_t>>(nc + 1, std::vector<uint8_t>(256, 0)));   // hist[возраст][вид]
  long last = -1;
  for (size_t p = 0, seq = 0; p + SUBC_N <= x.size(); p += SUBC_N, seq++) {
    e.analyse(&x[p]);
    for (int a = nc; a >= 1; a--) hist[a] = hist[a - 1];
    for (int v = 0; v <= nc; v++) e.pack(hist[0][v].data(), lay[v].bytes, lay[v].coarse);
    r.pk++;
    if (rand() % 100 < loss) { r.drop++; continue; }
    long gap = (long)seq - last;
    int16_t o[SUBC_N];
    if (gap >= 2) {
      int rec = gap - 1 < nc ? (int)gap - 1 : nc;
      for (long k = 0; k < gap - 1 - rec; k++) { d.decode(NULL, 0, false, o); r.y.insert(r.y.end(), o, o + SUBC_N); r.lost++; }
      for (int k = rec; k >= 1; k--) {   // кадр возраста k лежит в пакете в виде k
        d.decode(hist[k][k].data(), lay[k].bytes, lay[k].coarse, o);
        r.y.insert(r.y.end(), o, o + SUBC_N);
        r.good[k]++;
      }
    }
    d.decode(hist[0][0].data(), lay[0].bytes, lay[0].coarse, o);
    r.y.insert(r.y.end(), o, o + SUBC_N);
    last = (long)seq;
  }
  r.q = snr(x, r.y, SUBC_DELAY);
  return r;
}

int main(int argc, char **argv) {
  if (argc < 3) return 1;
  subcInit();
  std::string dir = argv[1], outDir = argv[2];
  int bad = 0;
  {   // 1) без квантования
    Sig x(SRATE * 2), y(SRATE * 2);
    uint32_t r = 7;
    for (size_t i = 0; i < x.size(); i++) {
      r = r * 1664525u + 1013904223u;
      double t = i / (double)SRATE;
      x[i] = (int16_t)(9000 * sin(2 * M_PI * 440 * t) + 5000 * sin(2 * M_PI * 3300 * t) + 3000 * sin(2 * M_PI * 9100 * t) + ((int32_t)r >> 20));
    }
    SubcEnc e;
    SubcDec d;
    e.reset();
    d.reset();
    for (size_t p = 0; p + SUBC_N <= x.size(); p += SUBC_N) {
      e.analyse(&x[p]);
      memcpy(d.s, e.s, sizeof(d.s));
      d.synth(&y[p]);
    }
    int dl = bestDelay(x, y, 120);
    Snr q = snr(x, y, dl);
    printf("subc: разбор и сборка без квантования — задержка %d отсчётов (%.2f мс), ошибка восстановления %.1f дБ\n", dl, dl * 1000.0 / SRATE, -q.all);
    if (dl != SUBC_DELAY || q.all < 55) { printf("ОШИБКА: набор фильтров не восстанавливает звук\n"); bad++; }
  }
  {   // 1а) быстрый счёт против прежнего: отсчёты полос, биты кадра, звук после сборки — и чужой кадр в обе стороны
    ref::subcInit();
    Sig x(SRATE * 4);
    uint32_t r = 99;
    for (size_t i = 0; i < x.size(); i++) {
      r = r * 1664525u + 1013904223u;
      double t = i / (double)SRATE, env = 0.15 + 0.85 * fabs(sin(2 * M_PI * 1.7 * t));
      x[i] = (int16_t)(env * (8000 * sin(2 * M_PI * 310 * t) + 6000 * sin(2 * M_PI * 2750 * t + 1) + 2500 * sin(2 * M_PI * 6100 * t) + 900 * sin(2 * M_PI * 11500 * t)) + ((int32_t)r >> 21));
    }
    SubcEnc e;
    SubcDec d, dX;
    ref::SubcEnc re;
    ref::SubcDec rd, rdX;
    e.reset(); d.reset(); dX.reset(); re.reset(); rd.reset(); rdX.reset();
    double maxRel = 0;
    long bitDiff = 0, bytesAll = 0, frames = 0, levDiff = 0;
    int maxOut = 0, maxCross1 = 0, maxCross2 = 0, maxNull = 0;
    static const struct { int bytes; bool coarse; } V[3] = { { SUB_F0, false }, { SUB_F2, false }, { SUB_F3, true } };
    for (size_t p = 0; p + SUBC_N <= x.size(); p += SUBC_N, frames++) {
      e.analyse(&x[p]);
      re.analyse(&x[p]);
      for (int m = 0; m < SUBC_NB; m++)
        for (int k = 0; k < SUBC_M; k++) {
          double df = fabs((double)e.s[m][k] - re.s[m][k]);   // в единицах отсчёта (шкала ±32768)
          if (df > maxRel) maxRel = df;
        }
      for (int k = 0; k < SUBC_M; k++) levDiff += e.lev[k] != re.lev[k];
      for (int v = 0; v < 3; v++) {
        uint8_t a[64], b[64];
        int16_t oa[SUBC_N], ob[SUBC_N];
        e.pack(a, V[v].bytes, V[v].coarse);
        re.pack(b, V[v].bytes, V[v].coarse);
        for (int i = 0; i < V[v].bytes; i++) bitDiff += a[i] != b[i];
        bytesAll += V[v].bytes;
        if (v) continue;
        bool drop = frames % 37 == 20 || frames % 37 == 21;   // пропавшие кадры — тоже сверить
        d.decode(drop ? NULL : b, V[v].bytes, V[v].coarse, oa);   // один и тот же кадр (эталонного кодера) — двумя приёмниками
        rd.decode(drop ? NULL : b, V[v].bytes, V[v].coarse, ob);
        for (int i = 0; i < SUBC_N; i++) {
          int df = abs(oa[i] - ob[i]);
          if (df > (drop ? maxNull : maxOut)) (drop ? maxNull : maxOut) = df;
        }
        dX.decode(b, V[v].bytes, V[v].coarse, oa);    // кадр прежнего передатчика — новым приёмником
        rdX.decode(a, V[v].bytes, V[v].coarse, ob);   // кадр нового передатчика — прежним приёмником
        for (int i = 0; i < SUBC_N; i++) {
          int ref0 = x[p + i >= (size_t)SUBC_DELAY ? p + i - SUBC_DELAY : 0];
          if (p + i < (size_t)SUBC_DELAY) continue;
          if (abs(oa[i] - ref0) > maxCross1) maxCross1 = abs(oa[i] - ref0);
          if (abs(ob[i] - ref0) > maxCross2) maxCross2 = abs(ob[i] - ref0);
        }
      }
    }
    printf("subc: быстрый счёт против прежнего, %ld кадров — отсчёты полос расходятся не больше чем на %.3f единицы отсчёта; масштабов разных %ld; "
           "байт кадра разных %ld из %ld; звук одного кадра двумя приёмниками расходится не больше чем на %d ед. (пропавший кадр — %d ед.)\n",
           frames, maxRel, levDiff, bitDiff, bytesAll, maxOut, maxNull);
    printf("subc: вперекрёст — кадр прежнего кодера новым приёмником: наибольшая ошибка к входу %d ед.; кадр нового кодера прежним приёмником: %d ед.\n", maxCross1, maxCross2);
    if (maxRel > 0.1 || levDiff || maxOut > 1 || maxNull > 1 || bitDiff * 200 > bytesAll || abs(maxCross1 - maxCross2) > maxCross1 / 4 + 64) {
      printf("ОШИБКА: быстрый счёт расходится с прежним\n");
      bad++;
    }
  }
  std::vector<std::string> files;
  for (const char *n : { "golos.pcm", "oda.pcm", "blagodat.pcm" }) files.push_back(dir + "/" + n);
  for (int i = 3; i < argc; i++) files.push_back(argv[i]);
  for (const std::string &fn : files) {
    Sig x = load(fn);
    if (x.size() < (size_t)SRATE) continue;
    std::string base = fn.substr(fn.find_last_of('/') + 1);
    base = base.substr(0, base.find('.'));
    Sig s1 = adpcmPath(x, Q_STD), s2 = adpcmPath(x, Q_SPEECH);
    Snr b1 = snr(x, s1, bestDelay(x, s1, 60)), b2 = snr(x, s2, bestDelay(x, s2, 60));
    printf("subc: %s, %.0f с — сигнал/шум: вся полоса / до 8 кГц / «на слух приёмника» (верх поднят, до 8 кГц)\n", base.c_str(), x.size() / (double)SRATE);
    printf("  «стандартна», 134 кбит/с, кадр 4 мс: %5.1f / %5.1f / %5.1f дБ\n", b1.all, b1.lo, b1.ear);
    printf("  «мова», 67 кбит/с, кадр 8 мс:        %5.1f / %5.1f / %5.1f дБ\n", b2.all, b2.lo, b2.ear);
    wav(outDir + "/" + base + "-standartna.wav", s1);
    wav(outDir + "/" + base + "-mova.wav", s2);
    for (int bytes : { 56, 46, 40, 32 }) {   // 2) без потерь, кадры разного размера
      long zero[SUBC_M] = {}, frames = 0;
      double bitsSum[SUBC_M] = {};
      Sig y = codeAll(x, bytes, false, zero, bitsSum, &frames);
      Snr a = snr(x, y, SUBC_DELAY);
      printf("  новый кодек, кадр %2d байт (%3d кбит/с): %5.1f / %5.1f / %5.1f дБ; бит по полосам:", bytes, bytes * 2, a.all, a.lo, a.ear);
      for (int k = 0; k < SUBC_M; k++) printf(" %.1f", bitsSum[k] / frames);
      printf("; без бит, %% кадров:");
      for (int k = 0; k < SUBC_M; k++) printf(" %ld", zero[k] * 100 / frames);
      printf("\n");
      if (bytes == SUB_F0) wav(outDir + "/" + base + "-novyi.wav", y);
      if (bytes == SUB_F0 && a.ear < b2.ear + 6) { printf("ОШИБКА: новый кодек не лучше «мова» хотя бы на 6 дБ\n"); bad++; }
    }
    for (int bytes : { 24, 16, 12 }) {
      Sig yc = codeAll(x, bytes, true);
      Snr c = snr(x, yc, SUBC_DELAY);
      printf("  грубая копия %d байт (если бы шла всё время): %5.1f / %5.1f / %5.1f дБ\n", bytes, c.all, c.lo, c.ear);
      if (bytes == SUB_F3) wav(outDir + "/" + base + "-hruba.wav", yc);
    }
    // 3) потери: разные раскладки пакета
    struct { const char *name; std::vector<Lay> lay; } LAY[] = {
      { "А: 56 + 56 + грубая 24 (158 байт)", { { 56, false }, { 56, false }, { 24, true } } },
      { "Б: 40 + 40 + 40 + грубая 16 (158 байт)", { { 40, false }, { 40, false }, { 40, false }, { 16, true } } },
      { "Г (в прошивке): 46 + 46 + 32 + грубая 12", { { SUB_F0, false }, { SUB_F0, false }, { SUB_F2, false }, { SUB_F3, true } } },
    };
    for (auto &L : LAY)
      for (int loss : { 5, 15, 25 }) {
        LossRes r = lossRun(x, L.lay, loss);
        printf("  %-40s потери %2d %%: из копий", L.name, loss);
        for (size_t k = 1; k < L.lay.size(); k++) printf(" %ld", r.good[k]);
        printf(", без звука %ld кадров (%.0f мс за %.0f с); сигнал/шум %5.1f / %5.1f / %5.1f дБ\n", r.lost, r.lost * 4.0, x.size() / (double)SRATE, r.q.all, r.q.lo, r.q.ear);
        if (L.lay.size() == 4 && L.lay[0].bytes == SUB_F0 && loss == 15) wav(outDir + "/" + base + "-novyi-vtraty15.wav", r.y);
      }
  }
  printf("subc: %s; записи для прослушивания — в %s\n", bad ? "ЕСТЬ ОШИБКИ" : "в рамках", outDir.c_str());
  return bad ? 1 : 0;
}
