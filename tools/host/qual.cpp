// Проверка на компьютере: насколько каждое качество звука искажает настоящую запись (без потерь пакетов).
//   c++ -std=c++17 -O1 -DFW_VERSION=\"0.0\" -Ibuild/host -Ihearlink tools/host/qual.cpp -o build/host/qual
//   ./build/host/qual <запись 32 кГц моно s16 без заголовка> <папка для записей на прослушивание>
// Печатает отношение «сигнал / искажения» для каждого качества: по всей полосе и после подъёма верха, каким его
// делает «чёткость 3» приёмника (+12 дБ выше 2 кГц) — с ним шум сжатия слышнее. (build/host/proto_host.h делает
// tools/host/run.sh.)
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

struct Shelf {   // как в hearlink/rxaudio.h: +12 дБ выше 2 кГц
  double z1 = 0, z2 = 0;
  double run(double x) {
    double y = 3.287168 * x + z1;
    z1 = -5.286367 * x + 1.247790 * y + z2;
    z2 = 2.213098 * x - 0.461690 * y;
    return y;
  }
};

static void wav(const std::string &path, const std::vector<int16_t> &x) {
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

int main(int argc, char **argv) {
  if (argc < 3) return 1;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 1;
  std::vector<int16_t> in;
  int16_t b[4096];
  size_t r;
  while ((r = fread(b, 2, 4096, f)) > 0) in.insert(in.end(), b, b + r);
  fclose(f);
  const int N = (int)in.size();
  // Опыт (третий довод, 0…0.95): «предыскажение» сжатого звука — перед сжатием верх поднят (y = x − a·x₋₁), после
  // разжатия опущен обратно. Шум сжатия при этом уходит из верха в низ, где его закрывает сам звук.
  double pre = argc > 3 ? atof(argv[3]) : 0;
  long clipped = 0;
  static const char *const FN[Q_COUNT] = { "naivyshcha", "standartna", "mova", "dalnia" };
  for (int q = 0; q < Q_COUNT; q++) {
    const QDef &d = QDEF[q];
    Decim2 dec;
    Interp2 up;
    int idxCarry = 0;
    uint8_t blk[3 + Q_MAX_N / 2];
    std::vector<int16_t> out, ref;   // ref — тот же путь без сжатия (у 16 кГц — только перевод частоты туда и обратно)
    Interp2 upR;
    int16_t frm[Q_MAX_N], tmp[Q_MAX_N], u[Q_MAX_N * 2];
    for (int pos = 0; pos + d.chunks * FRAME <= N; pos += d.chunks * FRAME) {
      const int16_t *src = &in[pos];
      if (d.sr != SRATE) { dec.run(&in[pos], d.n, frm); src = frm; }
      if (d.sr == SRATE) ref.insert(ref.end(), src, src + d.n);
      else { upR.run((int16_t *)src, d.n, u); ref.insert(ref.end(), u, u + d.n * 2); }
      if (d.pcm) memcpy(tmp, src, d.n * 2);
      else {
        static double px[Q_COUNT], dz[Q_COUNT];
        int16_t em[Q_MAX_N];
        for (int i = 0; i < d.n; i++) {
          double y = src[i] - pre * px[q];
          px[q] = src[i];
          if (y > 32767 || y < -32768) clipped++;
          em[i] = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : lrint(y));
        }
        adpcmEncodeBlockBest(em, d.n, blk, idxCarry);
        adpcmDecodeBlock(blk, d.n, tmp);
        for (int i = 0; i < d.n; i++) {
          dz[q] = tmp[i] + pre * dz[q];
          tmp[i] = (int16_t)(dz[q] > 32767 ? 32767 : dz[q] < -32768 ? -32768 : lrint(dz[q]));
        }
      }
      if (d.sr == SRATE) out.insert(out.end(), tmp, tmp + d.n);
      else { up.run(tmp, d.n, u); out.insert(out.end(), u, u + d.n * 2); }
    }
    double best = -99, bestHi = -99;
    int bestD = 0;
    for (int delay = 0; delay < 40; delay++) {
      double se = 0, ss = 0;
      for (size_t i = SRATE / 10; i + delay < out.size() && i < (size_t)N; i++) {
        double e = out[i + delay] - in[i];
        se += e * e; ss += (double)in[i] * in[i];
      }
      double snr = 10 * log10(ss / (se + 1e-9));
      if (snr > best) { best = snr; bestD = delay; }
    }
    {
      Shelf a, c;
      double se = 0, ss = 0;
      for (size_t i = 0; i + bestD < out.size() && i < (size_t)N; i++) {
        double x = a.run(in[i]), e = c.run(out[i + bestD] - in[i]);
        if (i < SRATE / 10) continue;
        se += e * e; ss += x * x;
      }
      bestHi = 10 * log10(ss / (se + 1e-9));
    }
    double nz = 0, nzHi = 0;   // один шум сжатия: против того же звука без сжатия (потеря полосы сюда не входит)
    {
      Shelf a, c;
      double se = 0, ss = 0, seH = 0, ssH = 0;
      for (size_t i = 0; i < out.size() && i < ref.size(); i++) {
        double x = ref[i], e = (double)out[i] - ref[i], xh = a.run(x), eh = c.run(e);
        if (i < SRATE / 10) continue;
        se += e * e; ss += x * x; seH += eh * eh; ssH += xh * xh;
      }
      nz = 10 * log10(ss / (se + 1e-9));
      nzHi = 10 * log10(ssH / (seH + 1e-9));
    }
    printf("%-10s | %5u Гц, кадр %2u мс | сигнал / искажения: %5.1f дБ по всей полосе, %5.1f дБ после подъёма верха «чёткость 3»; один шум сжатия: %5.1f и %5.1f дБ\n", Q_NAME[q], d.sr, (unsigned)(qFrameUs(q) / 1000), best, bestHi, nz, nzHi);
    wav(std::string(argv[2]) + "/" + FN[q] + (pre > 0 ? "-pre" : "") + ".wav", out);
  }
  if (pre > 0) printf("предыскажение %.2f; отсчётов, упёршихся в край шкалы: %ld\n", pre, clipped);
  return 0;
}
