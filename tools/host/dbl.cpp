// Проверка «двух кадров в пакете» на компьютере (качество «найвища», с 2.60): звук проходит «передатчик → пакет →
// приёмник» одиночными пакетами и двойными при потерях 0, 2, 5, 10 и 20 % пакетов. Сборка и разбор пакета повторяют
// txaudio.h и rxaudio.h строка в строку (сами они без платы не собираются); функции сжатия и длины — настоящие,
// из proto.h. Печатает: сколько кадров восстановлено из копий, сколько пропало, сигнал/шум по каналам.
// Что ловит: перепутанный порядок копий (ближний/дальний кадр), копии разности не от того кадра, неверную длину.
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

struct Res {
  int rec, lost, pkts, dropped;
  double snrL, snrR;
};

static Res run(bool dbl, bool st, const std::vector<int16_t> &L, const std::vector<int16_t> &R, int lossPct) {
  const int q = Q_HI;
  const QDef &d = QDEF[q];
  const int N = L.size(), bl = qBlock(q), fl = qMainLen(q) + (st ? bl : 0);
  int idx = 0, idxS = 0, idxSc = 0;
  uint8_t hist[1 + DBL_COPIES][3 + Q_MAX_N / 2] = {}, scHist[1 + DBL_COPIES][SC_LEN] = {}, blk[3 + Q_MAX_N / 2], blkS[3 + Q_MAX_N / 2], scNow[SC_LEN];
  uint8_t pairMain[Q_MAX_N * 2], pairSide[3 + Q_MAX_N / 2], pkt[PKT_MAX];
  bool pairHave = false;
  uint32_t pairSeq = 0;
  long lastSeq = -1;
  std::vector<int16_t> oL, oR;
  int16_t m[FRAME], s[FRAME], tm[Q_MAX_N], ts[Q_MAX_N];
  Res r = {};
  srand(11);
  auto emit = [&](const int16_t *x, const int16_t *sd) {
    for (int i = 0; i < d.n; i++) {
      int a = x[i] + (sd ? sd[i] : 0), b = x[i] - (sd ? sd[i] : 0);
      oL.push_back(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
      oR.push_back(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
    }
  };
  for (int pos = 0, seq = 0; pos + FRAME <= N; pos += FRAME, seq++) {
    for (int i = 0; i < FRAME; i++) {
      m[i] = st ? (L[pos + i] + R[pos + i]) / 2 : L[pos + i];
      s[i] = st ? (L[pos + i] - R[pos + i]) / 2 : 0;
    }
    // ---- передатчик (txaudio.h)
    adpcmEncodeBlockBest(m, d.n, blk, idx);
    if (st) {
      adpcmEncodeBlockBest(s, d.n, blkS, idxS);
      scEncode(s, scNow, idxSc);
    } else memset(scNow, 0, SC_LEN);
    bool stash = dbl && !pairHave;
    if (stash) {
      pairSeq = seq;
      memcpy(pairMain, m, d.n * 2);
      if (st) memcpy(pairSide, blkS, bl);
      pairHave = true;
    }
    uint8_t *w = pkt;
    if (dbl && !stash) {
      memcpy(w, pairMain, d.n * 2); w += d.n * 2;
      if (st) { memcpy(w, pairSide, bl); w += bl; }
    }
    memcpy(w, m, d.n * 2); w += d.n * 2;
    if (st) { memcpy(w, blkS, bl); w += bl; }
    bool withSc = false;
    if (dbl) {
      for (int k = 1; k <= DBL_COPIES; k++, w += bl) memcpy(w, hist[k], bl);
      if (st)
        for (int k = 1; k <= DBL_COPIES; k++, w += SC_LEN) memcpy(w, scHist[k], SC_LEN);
    } else {
      for (int k = 0; k < qCopies(q, st); k++, w += bl) memcpy(w, hist[k], bl);
      if (st) { memcpy(w, scHist[0], SC_LEN); w += SC_LEN; withSc = true; }   // копия разности прошлого кадра (с 2.43)
    }
    int plen = (int)(w - pkt) + (int)sizeof(Hdr) + SEC_TAG;
    bool send = !stash;
    if (send) {
      int want = dbl ? qPktLen2(q, st) : qPktLen(q, st) + (withSc ? SC_LEN : 0);
      if (plen != want) { printf("ДЛИНА ПАКЕТА НЕ СОШЛАСЬ: %d вместо %d\n", plen, want); exit(1); }
      if (plen > PKT_MAX) { printf("ПАКЕТ ДЛИННЕЕ PKT_MAX\n"); exit(1); }
    }
    uint32_t hseq = dbl ? pairSeq : (uint32_t)seq;
    if (!stash) pairHave = false;
    for (int k = DBL_COPIES; k >= 1; k--) { memcpy(hist[k], hist[k - 1], bl); memcpy(scHist[k], scHist[k - 1], SC_LEN); }
    memcpy(hist[0], blk, bl); memcpy(scHist[0], scNow, SC_LEN);
    if (!send) continue;
    r.pkts++;
    if (rand() % 100 < lossPct) { r.dropped++; continue; }
    // ---- приёмник (rxaudio.h, rxOnPacket)
    long gap = (long)hseq - lastSeq;
    int copies = dbl ? DBL_COPIES : qCopies(q, st);
    const uint8_t *cp = pkt + (dbl ? 2 : 1) * fl, *scp = cp + copies * bl;
    if (gap >= 2) {
      int rec = gap - 1 < copies ? gap - 1 : copies, ls = gap - 1 - rec;
      r.lost += ls;
      memset(tm, 0, sizeof(tm));
      for (int k = 0; k < ls; k++) emit(tm, nullptr);
      for (int k = rec; k >= 1; k--) {
        adpcmDecodeBlock(cp + (k - 1) * bl, d.n, tm);
        bool sc = st && (dbl || (withSc && k == 1));
        if (sc) scDecode(scp + (dbl ? (k - 1) * SC_LEN : 0), ts);
        emit(tm, sc ? ts : nullptr);
        r.rec++;
      }
    }
    for (int f = 0; f < (dbl ? 2 : 1); f++) {
      const uint8_t *fp = pkt + f * fl;
      memcpy(tm, fp, d.n * 2);
      if (st) adpcmDecodeBlock(fp + qMainLen(q), d.n, ts);
      emit(tm, st ? ts : nullptr);
    }
    lastSeq = (long)hseq + (dbl ? 1 : 0);
  }
  double eL = 0, eR = 0, sL = 0, sR = 0;
  for (size_t i = SRATE / 10; i < oL.size() && i < (size_t)N; i++) {
    double a = oL[i] - L[i], b = oR[i] - (st ? R[i] : L[i]);
    eL += a * a; eR += b * b; sL += (double)L[i] * L[i]; sR += (double)(st ? R[i] : L[i]) * (st ? R[i] : L[i]);
  }
  r.snrL = 10 * log10(sL / (eL + 1e-9));
  r.snrR = 10 * log10(sR / (eR + 1e-9));
  return r;
}

int main() {
  const int N = SRATE * 8;
  std::vector<int16_t> L(N), R(N);
  for (int i = 0; i < N; i++) {   // слева — низ и середина, справа — середина и верх, с разными огибающими (как в stereo.cpp)
    double t = i / (double)SRATE;
    L[i] = (int16_t)((0.6 + 0.4 * sin(2 * M_PI * 3 * t)) * (6000 * sin(2 * M_PI * 220 * t) + 3000 * sin(2 * M_PI * 1000 * t)));
    R[i] = (int16_t)((0.6 + 0.4 * cos(2 * M_PI * 2 * t)) * (4000 * sin(2 * M_PI * 1000 * t + 1) + 3500 * sin(2 * M_PI * 3100 * t)));
  }
  printf("«найвища», %d с звука; потери — случайные, в пакетах (двойной пакет несёт 4 мс звука и %d копии, одиночный — 2 мс)\n", N / SRATE, DBL_COPIES);
  int bad = 0;
  double lostMs[2][2][5] = {};
  const int LOSS[5] = { 0, 2, 5, 10, 20 };
  for (int st = 1; st >= 0; st--)
    for (int dbl = 0; dbl <= 1; dbl++)
      for (int li = 0; li < 5; li++) {
        int loss = LOSS[li];
        Res r = run(dbl, st, L, R, loss);
        lostMs[st][dbl][li] = r.lost * 2.0;
        printf("%-6s %-9s пакет %3d байт | потери %2d %%: пакетов %4d, пропало %3d | кадров из копий %4d, без звука %4d (%5.1f мс) | сигнал/шум: левый %5.1f дБ, правый %5.1f дБ\n",
               st ? "стерео" : "моно", dbl ? "двойные" : "одиночные", dbl ? qPktLen2(Q_HI, st) : qPktLen(Q_HI, st) + (st ? SC_LEN : 0), loss, r.pkts, r.dropped, r.rec, r.lost,
               r.lost * 2.0, r.snrL, r.snrR);
        if (loss == 0 && (r.rec || r.lost || r.snrL < 30 || r.snrR < 30)) bad++;   // без потерь: середина идёт без сжатия, разность — сжатая
      }
  if (bad) { printf("ОШИБКА: без потерь звук не сошёлся с исходным\n"); return 1; }
  // двойные пакеты не должны терять больше звука, чем одиночные, при потерях до 10 %
  for (int st = 0; st < 2; st++)
    for (int li = 1; li <= 3; li++)
      if (lostMs[st][1][li] > lostMs[st][0][li]) { printf("ОШИБКА: при потерях %d %% двойные пакеты (%s) теряют больше звука, чем одиночные\n", LOSS[li], st ? "стерео" : "моно"); return 1; }
  printf("двойные пакеты: при потерях до 10 %% звука пропадает не больше, чем с одиночными — сошлось\n");
  return 0;
}
