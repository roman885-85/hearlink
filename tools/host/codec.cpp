// Проверка на компьютере: звук проходит «передатчик → пакет → приёмник» для каждого качества, с потерями пакетов.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>
#define SRATE 32000
#define FRAME 64
enum { Q_HI = 0, Q_STD, Q_SPEECH, Q_FAR, Q_COUNT };
#define PROTO_HOST
#include "proto_host.h"

int main() {
  const int N = SRATE * 4;
  std::vector<int16_t> in(N);
  for (int i = 0; i < N; i++) {   // голосоподобная смесь: 220, 1000, 3100 Гц с медленной огибающей
    double t = i / (double)SRATE, e = 0.6 + 0.4 * sin(2 * M_PI * 3 * t);
    in[i] = (int16_t)(e * (5000 * sin(2 * M_PI * 220 * t) + 3000 * sin(2 * M_PI * 1000 * t) + 1500 * sin(2 * M_PI * 3100 * t)));
  }
  for (int lossPct : { 0, 5, 20 })
    for (int q = 0; q < Q_COUNT; q++) {
      const QDef &d = QDEF[q];
      Decim2 dec;
      Interp2 up;
      srand(7);
      int idxCarry = 0, bl = qBlock(q), outN = d.n * (SRATE / d.sr);
      uint8_t hist[2][3 + Q_MAX_N / 2] = {}, blk[3 + Q_MAX_N / 2], pkt[PKT_MAX];
      std::vector<int16_t> out;
      int16_t frm[Q_MAX_N], tmp[Q_MAX_N], u[Q_MAX_N * 2];
      int lastSeq = -1, lost = 0, recovered = 0, pkts = 0, dropped = 0;
      auto emit = [&](int16_t *x) {
        if (d.sr == SRATE) out.insert(out.end(), x, x + d.n);
        else { up.run(x, d.n, u); out.insert(out.end(), u, u + d.n * 2); }
      };
      for (int pos = 0, seq = 0; pos + d.chunks * FRAME <= N; pos += d.chunks * FRAME, seq++) {
        const int16_t *src = &in[pos];
        if (d.sr != SRATE) { dec.run(&in[pos], d.n, frm); src = frm; }
        adpcmEncodeBlock(src, d.n, blk, idxCarry);
        uint8_t *w = pkt;
        if (d.pcm) { memcpy(w, src, d.n * 2); w += d.n * 2; } else { memcpy(w, blk, bl); w += bl; }
        for (int k = 0; k < d.copies; k++, w += bl) memcpy(w, hist[k], bl);
        memcpy(hist[1], hist[0], bl);
        memcpy(hist[0], blk, bl);
        pkts++;
        if (rand() % 100 < lossPct) { dropped++; continue; }
        // приём
        int gap = seq - lastSeq;
        if (gap >= 2) {
          int rec = gap - 1 < d.copies ? gap - 1 : d.copies, ls = gap - 1 - rec;
          lost += ls;
          for (int i = 0; i < ls * outN; i++) out.push_back(0);
          const uint8_t *cp = pkt + qMainLen(q);
          for (int k = rec; k >= 1; k--) { adpcmDecodeBlock(cp + (k - 1) * bl, d.n, tmp); emit(tmp); recovered++; }
        }
        if (d.pcm) memcpy(tmp, pkt, d.n * 2); else adpcmDecodeBlock(pkt, d.n, tmp);
        emit(tmp);
        lastSeq = seq;
      }
      // сравнение: у 16 кГц задержка фильтров 11 + 11 отсчётов 32 кГц (вниз) и 6 отсчётов 16 кГц (вверх)
      double best = -99; int bestD = 0;
      for (int delay = 0; delay < 40; delay++) {
        double se = 0, ss = 0;
        for (size_t i = SRATE / 10; i + delay < out.size() && i < (size_t)N; i++) {
          double e = out[i + delay] - in[i];
          se += e * e; ss += (double)in[i] * in[i];
        }
        double snr = 10 * log10(ss / (se + 1e-9));
        if (snr > best) { best = snr; bestD = delay; }
      }
      printf("потери %2d %% | %-10s | пакет %3d байт, %4.0f/с, поток %3.0f кбит/с | пропало пакетов %3d из %d, достали из копий %3d, не восстановлено %3d | сигнал/шум %5.1f дБ\n",
             lossPct, Q_NAME[q], (int)(14 + qMainLen(q) + d.copies * bl), 1e6 / qFrameUs(q), (14 + qMainLen(q) + d.copies * bl) * 8 * 1e3 / qFrameUs(q), dropped, pkts, recovered, lost,
             best);
      if (!lossPct) printf("      задержка фильтров %d отсчётов (%.2f мс)\n", bestD, bestD * 1000.0 / SRATE);
    }
}
