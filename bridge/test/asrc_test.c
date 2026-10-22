// Перевірка перерахунку частоти (bridge/src/asrc.c) на Mac — той самий код, що піде в RP2040.
// Імітація: вхід іде пачками (AES — по 192 кадри, PCM1808 — по 16) зі своєю «справжньою» частотою, вихід забирає
// передавач своєю; обидві відхилені від номіналу на задані ppm. Регулятор має сам знайти співвідношення.
// Звук: синус; після встоювання до кривої підганяється синус відомої частоти, решта — шум і спотворення (THD+N).
// Пороги — для робочого варіанта (ваги Q16 у множенні, ~88 дБ; 98 дБ давав варіант з двома множеннями — на RP2040 задорогий).
// Вхід — 24 біти (з 22.10): два останні випадки — тихий звук, де видно розрядність входу.
//   cc -O2 -I../src asrc_test.c ../src/asrc.c -lm -o /tmp/asrc_test && /tmp/asrc_test
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asrc.h"

static asrc_t A;

typedef struct {
  uint32_t fsIn;      // номінал входу
  int taps, block;    // відводи фільтра; кадрів у пачці входу
  double ppmIn, ppmOut;
  double f;           // частота синуса, Гц
  double amp;         // амплітуда, частка повної шкали
  double secs, settle;
  double fitF;        // на якій частоті міряти вихід (0 — на f); для перевірки «перекидання» вище 16 кГц
  int target, lead;   // запас «за часом» і скільки кадрів вихід виробляється наперед (0 — як для пачок)
} sc_t;

typedef struct {
  double snr;         // сигнал / (решта), дБ
  double level;       // рівень підігнаного синуса відносно amp, дБ
  double ppm;         // поправка кроку наприкінці
  double ppmWant;     // яка потрібна насправді
  int lvMin, lvMax;   // запас після встоювання
  unsigned under, over;
} res_t;

// Підігнати a·cos(wn) + b·sin(wn) + c до y[0..n) — найменші квадрати; повертає амплітуду, rest — СКВ залишку.
static double fitSine(const double *y, long n, double w, double *rest) {
  double s[3][4] = {{0}};
  for (long i = 0; i < n; i++) {
    double v[3] = {cos(w * i), sin(w * i), 1.0};
    for (int r = 0; r < 3; r++) {
      for (int c = 0; c < 3; c++) s[r][c] += v[r] * v[c];
      s[r][3] += v[r] * y[i];
    }
  }
  for (int k = 0; k < 3; k++) {   // Гаусс
    for (int r = k + 1; r < 3; r++) {
      double m = s[r][k] / s[k][k];
      for (int c = k; c < 4; c++) s[r][c] -= m * s[k][c];
    }
  }
  double x[3];
  for (int r = 2; r >= 0; r--) {
    double t = s[r][3];
    for (int c = r + 1; c < 3; c++) t -= s[r][c] * x[c];
    x[r] = t / s[r][r];
  }
  double e = 0;
  for (long i = 0; i < n; i++) {
    double d = y[i] - (x[0] * cos(w * i) + x[1] * sin(w * i) + x[2]);
    e += d * d;
  }
  *rest = sqrt(e / (double)n);
  return hypot(x[0], x[1]);
}

// Найкраща частота поблизу w0 (золотий перетин у межах ±span відносно) — найменший залишок.
static double fitBest(const double *y, long n, double w0, double span, double *rest, double *wBest) {
  double lo = w0 * (1 - span), hi = w0 * (1 + span), g = (sqrt(5.0) - 1) / 2;
  double x1 = hi - g * (hi - lo), x2 = lo + g * (hi - lo), r1, r2;
  fitSine(y, n, x1, &r1);
  fitSine(y, n, x2, &r2);
  for (int it = 0; it < 60; it++) {
    if (r1 < r2) {
      hi = x2; x2 = x1; r2 = r1; x1 = hi - g * (hi - lo); fitSine(y, n, x1, &r1);
    } else {
      lo = x1; x1 = x2; r1 = r2; x2 = lo + g * (hi - lo); fitSine(y, n, x2, &r2);
    }
  }
  *wBest = (lo + hi) / 2;
  return fitSine(y, n, *wBest, rest);
}

static bool measure = true;   // чи міряє міст частоти (інакше — лише регулятор)

static res_t run(const sc_t *c) {
  const uint32_t fsOut = 32000;
  const double fsInT = c->fsIn * (1 + c->ppmIn * 1e-6), fsOutT = fsOut * (1 + c->ppmOut * 1e-6);
  const int lead = c->lead ? c->lead : 32;   // скільки кадрів вихід виробляється наперед
  const int32_t target = c->target ? c->target : c->block + 48;   // запас «за часом»: пачка + поле
  asrc_init(&A, c->fsIn, fsOut, c->taps, target, c->block);
  long N = (long)(c->secs * fsOut), n0 = (long)(c->settle * fsOut);
  double *y = malloc(sizeof(double) * (size_t)(N - n0));
  int32_t *blk = malloc(sizeof(int32_t) * 2 * (size_t)c->block);
  long K = 0;                                // вхідних кадрів уже записано
  res_t r = {0};
  r.lvMin = 1 << 30;
  r.lvMax = -(1 << 30);
  bool started = false;
  for (long n = 0; n < N;) {
    double tp = (double)(n - lead) / fsOutT;  // коли виробляється вихідний кадр n
    long avail = (long)floor(tp * fsInT / c->block) * c->block;
    while (K < avail) {
      for (int k = 0; k < c->block; k++) {
        double t = (double)(K + k) / fsInT;
        double v = c->amp * sin(2 * M_PI * c->f * t) * 8388607.0;   // 24 біти, як з пульта (з 22.10)
        int32_t q = (int32_t)lrint(v);
        blk[2 * k] = q;
        blk[2 * k + 1] = -q;
      }
      asrc_write(&A, blk, (uint32_t)c->block);
      K += c->block;
    }
    if (!started) {
      if (K < 2 * c->block) {   // як у мосту: вихід вмикається, коли прийшли дві пачки
        n++;
        continue;
      }
      started = true;
      asrc_restart(&A);
    }
    int32_t l, rr;
    asrc_read(&A, &l, &rr);
    if (n >= n0) {
      y[n - n0] = (double)l / 8388608.0;
      int lv = asrc_level(&A);
      if (lv < r.lvMin) r.lvMin = lv;
      if (lv > r.lvMax) r.lvMax = lv;
    }
    if (n % 32 == 0) asrc_steer(&A, 32, (float)(tp * fsInT - (double)K));   // кадри «в дорозі» від останньої пачки
    // Міст міряє обидві частоти за мітками часу пачок (≈ 1 с) і задає крок одразу; тут — з похибкою +2 ppm.
    if (measure && n == fsOut) asrc_set_ratio(&A, fsInT * (1 + 2e-6), fsOutT);
    n++;
  }
  // Вікнами по 16384 (0,5 с): у кожному — своя найкраща частота; береться найгірше вікно.
  double ff = c->fitF > 0 ? c->fitF : c->f, w0 = 2 * M_PI * ff / fsOutT, worst = 1e9, lvl = 0;
  long W = 16384, nw = 0;
  for (long s = 0; s + W <= N - n0; s += W) {
    double rest, wb, a = fitBest(y + s, W, w0, 300e-6, &rest, &wb);
    double snr = 20 * log10((a / sqrt(2.0)) / rest);
    if (snr < worst) worst = snr;
    lvl += 20 * log10(a / c->amp);
    nw++;
  }
  r.snr = worst;
  r.level = lvl / (double)nw;
  r.ppm = A.ppm;
  r.ppmWant = measure ? -2.0 : ((fsInT / fsOutT) / ((double)c->fsIn / fsOut) - 1) * 1e6;
  r.under = A.under;
  r.over = A.over;
  free(y);
  free(blk);
  return r;
}

int main(void) {
  int bad = 0;
  printf("Перерахунок частоти: вхід → 32 кГц (вихід передавача). Кварци: вхід +100 ppm, вихід −50 ppm.\n\n");
  printf("%-34s %8s %9s %9s %9s %11s %6s\n", "випадок", "THD+N", "рівень", "поправка", "потрібна", "запас", "провали");
  struct {
    const char *name;
    sc_t c;
    double minSnr;   // нижче — погано
  } t[] = {
      {"AES 48 кГц, 1 кГц −1 дБ", {48000, 64, 192, 100, -50, 1000, 0.89, 30, 22, 0}, 87},
      {"AES 48 кГц, 10 кГц −1 дБ", {48000, 64, 192, 100, -50, 10000, 0.89, 30, 22, 0}, 85},
      {"AES 48 кГц, 100 Гц −20 дБ", {48000, 64, 192, 100, -50, 100, 0.1, 30, 22, 0}, 70},
      {"AES 44,1 кГц, 1 кГц −1 дБ", {44100, 64, 192, 100, -50, 1000, 0.89, 30, 22, 0}, 87},
      {"PCM1808 32,552 кГц, 1 кГц −1 дБ", {32552, 48, 16, 100, -50, 1000, 0.89, 30, 22, 0}, 87},
      {"PCM1808 32,552 кГц, 12 кГц −1 дБ", {32552, 48, 16, 100, -50, 12000, 0.89, 30, 22, 0}, 84},
      {"AES 48: кварци −300/+300 ppm", {48000, 64, 192, -300, 300, 1000, 0.89, 40, 32, 0}, 87},
      // як у мосту зараз: читання по кадру (пачки по ~8), малий запас — мінімальна затримка
      {"AES 48, запас 24, вихід 8 наперед", {48000, 64, 8, 100, -50, 1000, 0.89, 30, 22, 0, 24, 8}, 87},
      {"AES 48, 10 кГц, запас 24", {48000, 64, 8, 100, -50, 10000, 0.89, 30, 22, 0, 24, 8}, 85},
      {"AES 44,1, запас 24", {44100, 64, 8, 100, -50, 1000, 0.89, 30, 22, 0, 24, 8}, 87},
      {"PCM, 48 відв., запас 16", {32552, 48, 8, 100, -50, 1000, 0.89, 30, 22, 0, 16, 8}, 87},
      {"PCM, 32 відв., запас 16", {32552, 32, 8, 100, -50, 1000, 0.89, 30, 22, 0, 16, 8}, 87},
      {"PCM, 32 відв., 12 кГц", {32552, 32, 8, 100, -50, 12000, 0.89, 30, 22, 0, 16, 8}, 80},
      // Тихий звук (22.10, пульт −87 дБ): вхід 24 біти — шум перерахунку має бути на рівні 24-бітного входу (−146 дБ),
      // а не 16-бітного (−101 дБ): при −90 дБ це ~55 дБ сигнал/шум замість ~11.
      {"AES 48, 1 кГц −90 дБ (тихий звук)", {48000, 64, 8, 100, -50, 1000, 3.1623e-5, 30, 22, 0, 24, 8}, 50},
      {"AES 48, 5 кГц −100 дБ", {48000, 64, 8, 100, -50, 5000, 1e-5, 30, 22, 0, 24, 8}, 40},
  };
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
    res_t r = run(&t[i].c);
    int ok = r.snr >= t[i].minSnr && r.lvMin >= 0 && r.under == 0 && r.over == 0 && fabs(r.ppm + 2.0) < 0.5;
    if (!ok) bad++;
    printf("%-34s %6.1f дБ %+6.2f дБ %+7.1f ppm %+7.1f ppm %4d…%-4d %3u/%u %s\n", t[i].name, r.snr, r.level, r.ppm,
           r.ppmWant, r.lvMin, r.lvMax, r.under, r.over, ok ? "" : "  ← ПОГАНО");
  }
  measure = false;
  {
    sc_t c0 = {48000, 64, 192, 100, -50, 1000, 0.89, 40, 32, 0};
    res_t r = run(&c0);
    int ok = r.snr >= 87 && r.lvMin >= 0 && r.under == 0 && fabs(r.ppm - r.ppmWant) < 3;
    if (!ok) bad++;
    printf("%-34s %6.1f дБ %+6.2f дБ %+7.1f ppm %+7.1f ppm %4d…%-4d %3u/%u %s\n", "AES 48, без виміру частот (32 с)",
           r.snr, r.level, r.ppm, r.ppmWant, r.lvMin, r.lvMax, r.under, r.over, ok ? "" : "  ← ПОГАНО");
  }
  measure = true;
  printf("\nАЧХ (AES 48 кГц): рівень виходу відносно входу\n");
  double fr[] = {50, 1000, 5000, 10000, 12000, 13000, 14000, 15000};
  for (unsigned i = 0; i < sizeof fr / sizeof fr[0]; i++) {
    sc_t c = {48000, 64, 192, 100, -50, fr[i], 0.5, 12, 8, 0};
    res_t r = run(&c);
    printf("  %6.0f Гц: %+6.2f дБ\n", fr[i], r.level);
    if (fr[i] <= 12000 && fabs(r.level) > 0.1) bad++;
  }
  printf("\nПерекидання вище 16 кГц (AES 48 кГц, синус −1 дБ): що з'являється на 32 000 − f\n");
  double fa[] = {17500, 19000, 21000, 23000};
  for (unsigned i = 0; i < sizeof fa / sizeof fa[0]; i++) {
    sc_t c = {48000, 64, 192, 100, -50, fa[i], 0.89, 12, 8, 32000 * (1 - 50e-6) - fa[i]};
    res_t r = run(&c);
    printf("  %6.0f Гц → %6.0f Гц: %+7.1f дБ\n", fa[i], 32000 - fa[i], r.level);
    if (fa[i] >= 18000 && r.level > -85) bad++;
  }
  printf("\n%s\n", bad ? "Є ВІДХИЛЕННЯ (див. «ПОГАНО»)." : "Усе в межах.");
  return bad ? 1 : 0;
}
