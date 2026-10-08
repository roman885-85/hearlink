// Проверка стерео на компьютере: два канала проходят «середина + разность → пакет → приёмник» для каждого
// качества. Печатает сигнал/шум по каналам и проникание левого канала в правый.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>
#define SRATE 32000
#define FRAME 64
enum { Q_HI = 0, Q_STD, Q_SPEECH, Q_FAR, Q_COUNT };
#include "proto_host.h"

static bool useBest = true;      // кодер с подбором начального шага (как в передатчике) или прежний
static double peakLeak;          // наибольший отсчёт правого выхода относительно наибольшего левого входа, дБ
static void enc(const int16_t *x, int n, uint8_t *out, int &idx) {
  if (useBest) adpcmEncodeBlockBest(x, n, out, idx);
  else adpcmEncodeBlock(x, n, out, idx);
}
static double run(int q, const std::vector<int16_t> &L, const std::vector<int16_t> &R, int lossPct, double &snrL, double &snrR, int &lost, int &mono) {
  const QDef &d = QDEF[q];
  const int N = L.size(), bl = qBlock(q), copies = qCopies(q, true);
  Decim2 dec, decS;
  Interp2 up, upS;
  int idx = 0, idxS = 0, lastSeq = -1;
  uint8_t hist[2][3 + Q_MAX_N / 2] = {}, blk[3 + Q_MAX_N / 2], blkS[3 + Q_MAX_N / 2], pkt[PKT_MAX];
  std::vector<int16_t> oL, oR;
  int16_t m[FRAME * 6], s[FRAME * 6], frm[Q_MAX_N], frmS[Q_MAX_N], tm[Q_MAX_N], ts[Q_MAX_N], um[Q_MAX_N * 2], us[Q_MAX_N * 2];
  lost = mono = 0;
  srand(11);
  auto emit = [&](int16_t *x, int16_t *sd) {
    int n = d.n;
    int16_t *pm = x, *ps = sd;
    if (d.sr != SRATE) {
      up.run(x, d.n, um);
      if (sd) upS.run(sd, d.n, us);
      pm = um; ps = sd ? us : nullptr; n = d.n * 2;
    }
    for (int i = 0; i < n; i++) {
      int a = pm[i] + (ps ? ps[i] : 0), b = pm[i] - (ps ? ps[i] : 0);
      oL.push_back(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
      oR.push_back(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
    }
  };
  for (int pos = 0, seq = 0; pos + d.chunks * FRAME <= N; pos += d.chunks * FRAME, seq++) {
    for (int i = 0; i < d.chunks * FRAME; i++) {
      m[i] = (L[pos + i] + R[pos + i]) / 2;
      s[i] = (L[pos + i] - R[pos + i]) / 2;
    }
    const int16_t *src = m, *srcS = s;
    if (d.sr != SRATE) { dec.run(m, d.n, frm); decS.run(s, d.n, frmS); src = frm; srcS = frmS; }
    enc(src, d.n, blk, idx);
    enc(srcS, d.n, blkS, idxS);
    uint8_t *w = pkt;
    if (d.pcm) { memcpy(w, src, d.n * 2); w += d.n * 2; } else { memcpy(w, blk, bl); w += bl; }
    memcpy(w, blkS, bl); w += bl;
    for (int k = 0; k < copies; k++, w += bl) memcpy(w, hist[k], bl);
    if ((int)(w - pkt) + (int)sizeof(Hdr) + SEC_TAG != qPktLen(q, true)) { printf("ДЛИНА ПАКЕТА НЕ СОШЛАСЬ\n"); exit(1); }
    memcpy(hist[1], hist[0], bl);
    memcpy(hist[0], blk, bl);
    if (rand() % 100 < lossPct) continue;
    int gap = seq - lastSeq;
    const uint8_t *sideBlk = pkt + qMainLen(q), *cp = sideBlk + bl;
    if (gap >= 2) {
      int rec = gap - 1 < copies ? gap - 1 : copies, ls = gap - 1 - rec;
      lost += ls;
      memset(tm, 0, sizeof(tm));
      for (int k = 0; k < ls; k++) emit(tm, nullptr);
      for (int k = rec; k >= 1; k--) { adpcmDecodeBlock(cp + (k - 1) * bl, d.n, tm); emit(tm, nullptr); mono++; }
    }
    if (d.pcm) memcpy(tm, pkt, d.n * 2); else adpcmDecodeBlock(pkt, d.n, tm);
    adpcmDecodeBlock(sideBlk, d.n, ts);
    emit(tm, ts);
    lastSeq = seq;
  }
  int delay = d.sr == SRATE ? 0 : 23;
  double eL = 0, eR = 0, sL = 0, sR = 0, xr = 0, pkIn = 1, pkOut = 0;
  for (size_t i = SRATE / 10; i + delay < oL.size() && i < (size_t)N; i++) {
    double a = oL[i + delay] - L[i], b = oR[i + delay] - R[i];
    eL += a * a; eR += b * b; sL += (double)L[i] * L[i]; sR += (double)R[i] * R[i];
    xr += (double)oR[i + delay] * oR[i + delay];
    if (fabs(L[i]) > pkIn) pkIn = fabs(L[i]);
    if (fabs(oR[i + delay]) > pkOut) pkOut = fabs(oR[i + delay]);
  }
  peakLeak = 20 * log10((pkOut + 1e-3) / pkIn);
  snrL = 10 * log10(sL / (eL + 1e-9));
  snrR = sR > 1 ? 10 * log10(sR / (eR + 1e-9)) : 999;
  return 10 * log10((xr + 1e-9) / (sL + 1e-9));   // уровень правого выхода относительно левого входа
}

int main() {
  const int N = SRATE * 4;
  std::vector<int16_t> L(N), R(N), Z(N, 0);
  for (int i = 0; i < N; i++) {   // слева — низ и середина, справа — середина и верх, с разными огибающими
    double t = i / (double)SRATE;
    L[i] = (int16_t)((0.6 + 0.4 * sin(2 * M_PI * 3 * t)) * (6000 * sin(2 * M_PI * 220 * t) + 3000 * sin(2 * M_PI * 1000 * t)));
    R[i] = (int16_t)((0.6 + 0.4 * cos(2 * M_PI * 2 * t)) * (4000 * sin(2 * M_PI * 1000 * t + 1) + 3500 * sin(2 * M_PI * 3100 * t)));
  }
  std::vector<int16_t> G(N);     // гудки 1 кГц с резким началом и концом — как в проверке каналов
  for (int i = 0; i < N; i++) G[i] = (i / (SRATE / 5)) % 2 ? 0 : (int16_t)(8192 * sin(2 * M_PI * 1000 * i / SRATE));
  for (int best = 0; best < 2; best++) {
    useBest = best;
    printf("--- кодер %s\n", best ? "с подбором начального шага (новый)" : "прежний");
    for (int q = 0; q < Q_COUNT; q++) {
      if (!qStereoOk(q)) continue;
      double a, b; int lost, mono;
      run(q, L, R, 0, a, b, lost, mono);
      printf("%-10s | сигнал/шум: левый %5.1f дБ, правый %5.1f дБ", Q_NAME[q], a, b);
      run(q, G, Z, 0, a, b, lost, mono);
      printf(" | гудки только слева: в правом вершина %6.1f дБ\n", peakLeak);
    }
  }
  useBest = true;
  for (int q = 0; q < Q_COUNT; q++) {
    if (!qStereoOk(q)) { printf("%-10s | стерео нет (только моно)\n", Q_NAME[q]); continue; }
    double a, b; int lost, mono;
    run(q, L, R, 0, a, b, lost, mono);
    printf("%-10s | пакет %d байт, копий %d | сигнал/шум: левый %5.1f дБ, правый %5.1f дБ", Q_NAME[q], qPktLen(q, true), qCopies(q, true), a, b);
    double x = run(q, L, Z, 0, a, b, lost, mono);
    printf(" | звук только слева: в правом %6.1f дБ", x);
    run(q, L, R, 5, a, b, lost, mono);
    printf(" | при 5 %% потерь: кадров в моно %d, пропало %d\n", mono, lost);
  }
}
