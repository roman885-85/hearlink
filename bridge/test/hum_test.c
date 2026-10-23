// Перевірка віднімання гулу радіо (bridge/src/hum.h) на Mac — той самий код, що йде в RP2040.
// Вхід — записи входу передавача 22.10–23.10 (слова I2S, 24 біти у старших бітах, 32 кГц):
//   гул — тиша аналогового входу з гулом радіо (logs/sil-pcm-e19.bin), музика — цифровий вхід (logs/dig-2310-e19.bin).
// Сигнал: 1,5 с гулу, 2 с музики (+12 дБ — аналог відносно цифри) з гулом, 1,5 с гулу. Міряється помилка (вихід −
// музика) — лінії 250 / 750 / 1250 Гц і СКВ — у тиші до музики, під час музики, у тиші після; окремо — музика без гулу
// (віднімання не має нічого додати).
//   cc -O2 -I../src hum_test.c -lm -o /tmp/hum_test && /tmp/hum_test ../../logs/sil-pcm-e19.bin ../../logs/dig-2310-e19.bin
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "hum.h"

static hum_t H;

static int32_t *load(const char *p, size_t *n) {
  FILE *f = fopen(p, "rb");
  if (!f) { perror(p); exit(1); }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint32_t *w = malloc(sz);
  if (fread(w, 1, sz, f) != (size_t)sz) { perror(p); exit(1); }
  fclose(f);
  *n = sz / 8;   // кадрів (Л, П)
  int32_t *x = malloc(sz);
  for (size_t i = 0; i < 2 * *n; i++) x[i] = (int32_t)w[i] >> 8;
  free(w);
  return x;
}

// рівень лінії (Герцель по середині каналів, вікно Блекмана), дБ від повної шкали 24 біт
static double line(const double *e, size_t n, double f) {
  double c = 2 * cos(2 * M_PI * f / 32000.0), s1 = 0, s2 = 0, ws = 0;
  for (size_t i = 0; i < n; i++) {
    double w = 0.42 - 0.5 * cos(2 * M_PI * i / (n - 1)) + 0.08 * cos(4 * M_PI * i / (n - 1));
    double s = e[i] * w + c * s1 - s2;
    s2 = s1;
    s1 = s;
    ws += w;
  }
  double re = s1 - s2 * cos(2 * M_PI * f / 32000.0), im = s2 * sin(2 * M_PI * f / 32000.0);
  return 20 * log10(sqrt(re * re + im * im) / ws * 2 / 8388608.0 + 1e-15);
}
static double rms(const double *e, size_t n) {
  double s = 0;
  for (size_t i = 0; i < n; i++) s += e[i] * e[i];
  return 10 * log10(s / n / (8388608.0 * 8388608.0) + 1e-30);
}

// Без файлів (так у build.sh: записів у сховищі немає) — свої: гул — меандр 250 Гц −84 дБ з білим шумом −93 дБ (як
// у тиші аналогового входу), музика — п'ять синусів з частотами не в такт циклу і шум, −30 дБ.
static uint32_t rnd = 12345;
static int32_t noise(int32_t a) {
  rnd = rnd * 1664525u + 1013904223u;
  return (int32_t)((int64_t)(int32_t)rnd * a >> 31);
}
static int32_t *synthHum(size_t *n) {
  *n = 48000;
  int32_t *x = malloc(*n * 8);
  for (size_t i = 0; i < *n; i++)
    for (int c = 0; c < 2; c++) x[2 * i + c] = ((i % 128) < 64 ? 530 : -530) + noise(320);
  return x;
}
static int32_t *synthMusic(size_t *n) {
  *n = 32000;
  int32_t *x = malloc(*n * 8);
  const double f[5] = { 97.3, 233.1, 441.7, 1013.9, 3307.3 };
  for (size_t i = 0; i < *n; i++)
    for (int c = 0; c < 2; c++) {
      double s = 0;
      for (int k = 0; k < 5; k++) s += sin(2 * M_PI * f[k] * i / 32000.0 + k + c);
      x[2 * i + c] = (int32_t)(s * 0.2 * 8388608.0 * 0.0316 / 4) + noise(26000);
    }
  return x;
}

int main(int argc, char **argv) {
  size_t nh, nm;
  int32_t *hum = argc >= 3 ? load(argv[1], &nh) : synthHum(&nh), *mus = argc >= 3 ? load(argv[2], &nm) : synthMusic(&nm);
  const size_t S0 = 48000, SM = 64000, S1 = 48000, N = S0 + SM + S1;
  int fails = 0;
  for (int withHum = 1; withHum >= 0; withHum--) {
    hum_reset(&H, 32000);
    double *err = malloc(N * sizeof(double));
    for (size_t i = 0; i < N; i++) {
      int32_t hl = withHum ? hum[2 * (i % nh)] : 0, hr = withHum ? hum[2 * (i % nh) + 1] : 0;
      int32_t ml = 0, mr = 0;
      if (i >= S0 && i < S0 + SM) {
        size_t j = (i - S0) % nm;
        ml = mus[2 * j] * 4;
        mr = mus[2 * j + 1] * 4;
      }
      int32_t l = hl + ml, r = hr + mr;
      hum_step(&H, &l, &r);
      err[i] = ((double)(l - ml) + (double)(r - mr)) / 2;   // що лишилось від гулу (і що додало віднімання)
    }
    printf("%s\n", withHum ? "гул + музика:" : "музика без гулу (віднімання не має нічого додати):");
    const char *nm3[3] = { "тиша до музики, остання 1 с", "музика, остання 1 с", "тиша після, остання 1 с" };
    size_t a[3] = { S0 - 32000, S0 + SM - 32000, N - 32000 };
    for (int s = 0; s < 3; s++) {
      double *e = err + a[s];
      double l250 = line(e, 32000, 250), l750 = line(e, 32000, 750), l1250 = line(e, 32000, 1250), r = rms(e, 32000);
      printf("  %-28s 250 Гц %6.1f  750 Гц %6.1f  1250 Гц %6.1f  СКВ %6.1f дБ\n", nm3[s], l250, l750, l1250, r);
      if (withHum && (l250 > -98 || l750 > -110)) fails++;
      if (!withHum && r > -110) fails++;
    }
    free(err);
  }
  // сам гул без віднімання — для порівняння
  double *e0 = malloc(32000 * sizeof(double));
  for (size_t i = 0; i < 32000; i++) e0[i] = ((double)hum[2 * (i % nh)] + hum[2 * (i % nh) + 1]) / 2;
  printf("без віднімання: 250 Гц %6.1f  750 Гц %6.1f  1250 Гц %6.1f  СКВ %6.1f дБ\n", line(e0, 32000, 250),
         line(e0, 32000, 750), line(e0, 32000, 1250), rms(e0, 32000));
  printf("%s\n", fails ? "НЕ ПРОЙДЕНО" : "пройдено");
  return fails != 0;
}
