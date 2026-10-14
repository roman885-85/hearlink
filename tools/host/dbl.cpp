// Проверка «двух кадров в пакете» на компьютере (качество «найвища», с 2.60): звук проходит «передатчик → пакет →
// приёмник» одиночными пакетами и двойными при потерях 0, 2, 5, 10 и 20 % пакетов. Сборка и разбор пакета повторяют
// txaudio.h и rxaudio.h строка в строку (сами они без платы не собираются); функции сжатия и длины — настоящие,
// из proto.h. Печатает: сколько кадров восстановлено из копий, сколько пропало, сигнал/шум по каналам.
// Что ловит: перепутанный порядок копий (ближний/дальний кадр), копии разности не от того кадра, неверную длину.
// С 2.86 — и стерео с разностью без сжатия (proto.h, qPktLen4; с 2.87 — и в копиях N−1, N−2): разность в кадрах и
// одиночная потеря восстанавливаются бит в бит, у N−3 и N−4 разности нет (моно).
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
  int exact;        // из восстановленных кадров — без сжатия (пакет «с точными копиями», с 2.77)
  long diffRec;     // сколько отсчётов восстановленных кадров не совпало с исходным
  long diffSide;    // 2.86: сколько отсчётов разности в кадрах пакета не совпало с переданной (должно быть 0)
};

static Res run(int mode, bool st, const std::vector<int16_t> &L, const std::vector<int16_t> &R, int lossPct) {
  const bool dbl = mode >= 1, dup = mode >= 2, pcmS = mode == 3 && st;
  const int q = Q_HI;
  const QDef &d = QDEF[q];
  const int N = L.size(), bl = qBlock(q), fl = qMainLen(q) + (st ? (pcmS ? d.n * 2 : bl) : 0);
  int idx = 0, idxS = 0, idxSc = 0;
  uint8_t hist[1 + DBL_COPIES][3 + Q_MAX_N / 2] = {}, scHist[1 + DBL_COPIES][SC_LEN] = {}, blk[3 + Q_MAX_N / 2], blkS[3 + Q_MAX_N / 2], scNow[SC_LEN];
  uint8_t pairMain[Q_MAX_N * 2], pairSide[3 + Q_MAX_N / 2], pkt[PKT_MAX];
  uint8_t pcmHist[1 + DUP_PCM][FRAME * 2] = {};
  uint8_t pcmHistS[1 + DUP_PCM][FRAME * 2] = {};
  int16_t pairSideP[FRAME];
  bool pairHave = false;
  uint32_t pairSeq = 0;
  long lastSeq = -1;
  std::vector<int16_t> oL, oR;
  int16_t m[FRAME], s[FRAME], tm[Q_MAX_N], ts[Q_MAX_N];
  Res r = {};
  srand(11);
  int16_t lastS = 0;   // последний отсчёт разности, ушедший на выход (для плавного перехода в 2.87)
  auto emit = [&](const int16_t *x, const int16_t *sd) {
    lastS = sd ? sd[d.n - 1] : 0;
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
      if (st) {
        memcpy(pairSide, blkS, bl);
        memcpy(pairSideP, s, d.n * 2);
      }
      pairHave = true;
    }
    uint8_t *w = pkt;
    if (dbl && !stash) {
      memcpy(w, pairMain, d.n * 2); w += d.n * 2;
      if (st && pcmS) { memcpy(w, pairSideP, d.n * 2); w += d.n * 2; }
      else if (st) { memcpy(w, pairSide, bl); w += bl; }
    }
    memcpy(w, m, d.n * 2); w += d.n * 2;
    if (st && pcmS) { memcpy(w, s, d.n * 2); w += d.n * 2; }
    else if (st) { memcpy(w, blkS, bl); w += bl; }
    bool withSc = false;
    if (dbl) {
      int k = 1;
      if (dup)
        for (; k <= DUP_PCM; k++, w += d.n * 2) memcpy(w, pcmHist[k], d.n * 2);
      for (; k <= DBL_COPIES; k++, w += bl) memcpy(w, hist[k], bl);
      if (st && pcmS)
        for (int k2 = 1; k2 <= DUP_PCM; k2++, w += d.n * 2) memcpy(w, pcmHistS[k2], d.n * 2);
      else if (st)
        for (int k2 = 1; k2 <= DBL_COPIES; k2++, w += SC_LEN) memcpy(w, scHist[k2], SC_LEN);
    } else {
      for (int k = 0; k < qCopies(q, st); k++, w += bl) memcpy(w, hist[k], bl);
      if (st) { memcpy(w, scHist[0], SC_LEN); w += SC_LEN; withSc = true; }   // копия разности прошлого кадра (с 2.43)
    }
    int plen = (int)(w - pkt) + (int)sizeof(Hdr) + SEC_TAG;
    bool send = !stash;
    if (send) {
      int want = pcmS ? qPktLen4(q) : dup ? qPktLen3(q, st) : dbl ? qPktLen2(q, st) : qPktLen(q, st) + (withSc ? SC_LEN : 0);
      if (plen != want) { printf("ДЛИНА ПАКЕТА НЕ СОШЛАСЬ: %d вместо %d\n", plen, want); exit(1); }
      if (plen > PKT_MAX) { printf("ПАКЕТ ДЛИННЕЕ PKT_MAX\n"); exit(1); }
    }
    uint32_t hseq = dbl ? pairSeq : (uint32_t)seq;
    if (!stash) pairHave = false;
    for (int k = DBL_COPIES; k >= 1; k--) { memcpy(hist[k], hist[k - 1], bl); memcpy(scHist[k], scHist[k - 1], SC_LEN); }
    memcpy(hist[0], blk, bl); memcpy(scHist[0], scNow, SC_LEN);
    for (int k = DUP_PCM; k >= 1; k--) memcpy(pcmHist[k], pcmHist[k - 1], FRAME * 2);
    memcpy(pcmHist[0], m, d.n * 2);
    if (st) {
      for (int k = DUP_PCM; k >= 1; k--) memcpy(pcmHistS[k], pcmHistS[k - 1], FRAME * 2);
      memcpy(pcmHistS[0], s, d.n * 2);
    }
    if (!send) continue;
    r.pkts++;
    if (rand() % 100 < lossPct) { r.dropped++; continue; }
    // ---- приёмник (rxaudio.h, rxOnPacket)
    long gap = (long)hseq - lastSeq;
    int copies = dbl ? DBL_COPIES : qCopies(q, st);
    int dupN = dup ? DUP_PCM : 0;
    const uint8_t *cp = pkt + (dbl ? 2 : 1) * fl, *cpA = cp + dupN * qMainLen(q), *scp = cpA + (copies - dupN) * bl;
    if (gap >= 2) {
      int rec = gap - 1 < copies ? gap - 1 : copies, ls = gap - 1 - rec;
      r.lost += ls;
      memset(tm, 0, sizeof(tm));
      for (int k = 0; k < ls; k++) emit(tm, nullptr);
      for (int k = rec; k >= 1; k--) {
        if (k <= dupN) {
          memcpy(tm, cp + (k - 1) * qMainLen(q), d.n * 2);
          r.exact++;
        } else adpcmDecodeBlock(cpA + (k - 1 - dupN) * bl, d.n, tm);
        {   // сверка восстановленного кадра середины с исходным (кадр номер hseq − k)
          long f0 = ((long)hseq - k) * FRAME;
          for (int i = 0; i < d.n; i++) {
            int want = st ? (L[f0 + i] + R[f0 + i]) / 2 : L[f0 + i];
            if (tm[i] != want) r.diffRec++;
          }
        }
        bool sc = st && (dbl || (withSc && k == 1)) && !(pcmS && k > dupN);
        if (sc) {
          if (pcmS) {
            memcpy(ts, scp + (k - 1) * d.n * 2, d.n * 2);
            long f0 = ((long)hseq - k) * FRAME;   // разность восстановленного кадра — тоже бит в бит
            for (int i = 0; i < d.n; i++)
              if (ts[i] != (L[f0 + i] - R[f0 + i]) / 2) r.diffSide++;
          } else scDecode(scp + (dbl ? (k - 1) * SC_LEN : 0), ts);
        } else if (pcmS) {   // как rxaudio.h: разность по прямой от последнего сыгранного к точной разности N−2
          int16_t to;
          memcpy(&to, scp + (dupN - 1) * d.n * 2, 2);
          static int16_t from;
          if (k == rec) from = lastS;
          int total = (rec - dupN) * d.n, at = (rec - k) * d.n;
          // разность плавно уходит к нулю за 1 мс от последнего сыгранного отсчёта и так же плавно приходит к точной
          for (int i = 0; i < d.n; i++) {
            int p = at + i, F = 32;
            float a = p < F ? 1.0f - (p + 1) / (float)(F + 1) : 0, b = total - p <= F ? 1.0f - (total - p) / (float)(F + 1) : 0;
            ts[i] = (int16_t)(from * a + to * b);
          }
          sc = true;
        }
        emit(tm, sc ? ts : nullptr);
        r.rec++;
      }
    }
    for (int f = 0; f < (dbl ? 2 : 1); f++) {
      const uint8_t *fp = pkt + f * fl;
      memcpy(tm, fp, d.n * 2);
      if (st && pcmS) {
        memcpy(ts, fp + qMainLen(q), d.n * 2);
        long f0 = ((long)hseq + f) * FRAME;   // разность этого кадра на передатчике
        for (int i = 0; i < d.n; i++)
          if (ts[i] != (L[f0 + i] - R[f0 + i]) / 2) r.diffSide++;
      } else if (st) adpcmDecodeBlock(fp + qMainLen(q), d.n, ts);
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
  int bad = 0, badDup = 0;
  double lostMs[2][3][5] = {};
  const int LOSS[5] = { 0, 2, 5, 10, 20 };
  static const char *const PACK[4] = { "одиночные", "двойные", "двойные+точные", "+разн.16 бит" };
  double snr3[5] = {}, snr2[5] = {};
  long exact3 = 0;
  for (int st = 1; st >= 0; st--)
    for (int dbl = 0; dbl <= (st ? 3 : 2); dbl++)
      for (int li = 0; li < 5; li++) {
        int loss = LOSS[li];
        Res r = run(dbl, st, L, R, loss);
        lostMs[st][dbl][li] = r.lost * 2.0;
        printf("%-6s %-9s пакет %3d байт | потери %2d %%: пакетов %4d, пропало %3d | кадров из копий %4d, без звука %4d (%5.1f мс) | сигнал/шум: левый %5.1f дБ, правый %5.1f дБ\n",
               st ? "стерео" : "моно", PACK[dbl], dbl == 3 ? qPktLen4(Q_HI) : dbl == 2 ? qPktLen3(Q_HI, st) : dbl ? qPktLen2(Q_HI, st) : qPktLen(Q_HI, st) + (st ? SC_LEN : 0), loss, r.pkts, r.dropped, r.rec, r.lost,
               r.lost * 2.0, r.snrL, r.snrR);
        if (st && dbl == 3) {
          snr3[li] = fmin(r.snrL, r.snrR);
          exact3 += r.diffSide;
          if (r.diffRec > (long)(r.rec - r.exact) * FRAME) exact3 += r.diffRec;   // точные копии середины — тоже бит в бит
        }
        if (st && dbl == 2) snr2[li] = fmin(r.snrL, r.snrR);
        if (dbl == 3) continue;
        if (loss == 0 && (r.rec || r.lost || r.snrL < 30 || r.snrR < 30)) bad++;   // без потерь: середина идёт без сжатия, разность — сжатая
        if (dbl == 2) {
          printf("         из копий без сжатия (точных) %4d; отсчётов восстановленных кадров, не совпавших с исходным: %ld\n", r.exact, r.diffRec);
          // точные копии обязаны совпасть бит в бит: не совпадать вправе только кадры из сжатых копий
          if (r.diffRec > (long)(r.rec - r.exact) * FRAME) badDup++;
          if (loss && loss <= 5 && r.exact * 10 < r.rec * 8) badDup++;   // при малых потерях почти всё восстановленное — точное
        }
      }
  if (bad) { printf("ОШИБКА: без потерь звук не сошёлся с исходным\n"); return 1; }
  if (badDup) { printf("ОШИБКА: кадры из точных копий не совпали с исходными или точных слишком мало\n"); return 1; }
  for (int st = 0; st < 2; st++)
    for (int li = 1; li <= 4; li++)
      if (lostMs[st][2][li] > lostMs[st][1][li]) { printf("ОШИБКА: при потерях %d %% пакеты с точными копиями теряют больше звука, чем прежние двойные\n", LOSS[li]); return 1; }
  printf("двойные пакеты с точными копиями: кадры из них совпали с исходными бит в бит; звука пропадает не больше, чем с прежними двойными — сошлось\n");
  // 2.86: разность в кадрах пакета обязана дойти бит в бит; Л и П — с точностью до младшего бита (середина и разность
  // — (Л+П)/2 и (Л−П)/2 целыми: при нечётной сумме младший бит теряется, −96 дБ; так было всегда, и в моно тоже нет
  // потерь только потому, что там нет деления). С потерями — не хуже прежнего вида (копии разности N−1, N−2 полные).
  if (exact3) { printf("ОШИБКА: разность без сжатия или точные копии середины дошли не бит в бит (%ld отсчётов)\n", exact3); return 1; }
  if (snr3[0] < 70) { printf("ОШИБКА: стерео с разностью без сжатия без потерь: сигнал/шум %.1f дБ — ниже младшего бита\n", snr3[0]); return 1; }
  // при 2 % потерь (почти все одиночные) новый вид обязан быть почти без ошибки; при 5–20 % часть кадров N−3, N−4
  // идёт с разностью «по прямой» — по сигнал/шум это бывает и хуже 4-битной копии, но без шума и щелчков: для сведения
  if (snr3[1] < 60) { printf("ОШИБКА: при 2 %% потерь стерео с разностью без сжатия восстановилось не точно (%.1f дБ)\n", snr3[1]); return 1; }
  printf("стерео с разностью без сжатия: разность и точные копии — бит в бит; без потерь сигнал/шум %.1f дБ (младший бит; прежний вид — %.1f); при потерях 2/5/10/20 %%: %.1f / %.1f / %.1f / %.1f дБ (прежний вид — %.1f / %.1f / %.1f / %.1f) — сошлось\n",
         snr3[0], snr2[0], snr3[1], snr3[2], snr3[3], snr3[4], snr2[1], snr2[2], snr2[3], snr2[4]);
  // двойные пакеты не должны терять больше звука, чем одиночные, при потерях до 10 %
  for (int st = 0; st < 2; st++)
    for (int li = 1; li <= 3; li++)
      if (lostMs[st][1][li] > lostMs[st][0][li]) { printf("ОШИБКА: при потерях %d %% двойные пакеты (%s) теряют больше звука, чем одиночные\n", LOSS[li], st ? "стерео" : "моно"); return 1; }
  printf("двойные пакеты: при потерях до 10 %% звука пропадает не больше, чем с одиночными — сошлось\n");
  return 0;
}
