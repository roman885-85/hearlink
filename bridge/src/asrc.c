// Перерахунок частоти відліків — див. asrc.h.
#include "asrc.h"
#include <math.h>
#include <string.h>

// Зріз фільтра — 0,48 вихідної частоти (15,36 кГц при 32 кГц): рівно до ~13 кГц, а все, що вище ~17,5 кГц
// (і перекинулося б униз), придушено на 90 дБ. Кайзер β = 8,96 — саме для 90 дБ.
#define ASRC_CUT 0.48f
#define ASRC_BETA 8.96f
#define ASRC_PI 3.14159265358979f

static float besselI0(float x) {
  float s = 1, t = 1, h = x * 0.5f;
  for (int k = 1; k < 40; k++) {
    t *= h / (float)k;
    float u = t * t;
    s += u;
    if (u < s * 1e-9f) break;
  }
  return s;
}

void asrc_begin(asrc_t *a, uint32_t fsIn, uint32_t fsOut, int taps, int32_t target, int32_t burst) {
  if (taps > ASRC_MAX_TAPS) taps = ASRC_MAX_TAPS;
  taps &= ~1;
  a->taps = taps;
  a->fsIn = fsIn;
  a->fsOut = fsOut;
  a->target = target;
  a->burst = burst;
  a->built = 0;
}

// Ваги. Відлік x[i − T/2 + 1 + j] для дробової позиції μ (між i та i+1) має вагу k(μ + T/2 − 1 − j).
bool asrc_build(asrc_t *a, int rows) {
  const int taps = a->taps;
  const float fc = 2.0f * ASRC_CUT * (float)a->fsOut / (float)a->fsIn;   // смуга відносно вхідної частоти
  const float i0b = besselI0(ASRC_BETA), half = (float)taps * 0.5f;
  for (; rows > 0 && a->built <= ASRC_PHASES; rows--) {
    int p = a->built++;
    float mu = (float)p / (float)ASRC_PHASES, w[ASRC_MAX_TAPS], sum = 0;
    for (int j = 0; j < taps; j++) {
      float t = mu + half - 1.0f - (float)j, r = t / half, v = 0;
      if (r > -1.0f && r < 1.0f) {
        float x = fc * t, sn = fabsf(x) < 1e-6f ? 1.0f : sinf(ASRC_PI * x) / (ASRC_PI * x);
        v = fc * sn * besselI0(ASRC_BETA * sqrtf(1.0f - r * r)) / i0b;
      }
      w[j] = v;
      sum += v;
    }
    // Кожна фаза — рівно одиниця на постійному струмі (інакше постійна складова «дзвенить» з кроком фаз).
    for (int j = 0; j < taps; j++) a->coef[p * taps + j] = (int32_t)lround((double)w[j] / (double)sum * 1073741824.0);
  }
  return a->built > ASRC_PHASES;
}

void asrc_finish(asrc_t *a) {
  a->step0 = ((uint64_t)a->fsIn << 32) / a->fsOut;
  a->step = a->step0;
  memset(a->in, 0, sizeof(a->in));
  a->wr = 0;
  a->lastWr = 0;
  a->frac = 0;
  a->under = a->over = 0;
  asrc_restart(a);
}

void asrc_init(asrc_t *a, uint32_t fsIn, uint32_t fsOut, int taps, int32_t target, int32_t burst) {
  asrc_begin(a, fsIn, fsOut, taps, target, burst);
  asrc_build(a, ASRC_PHASES + 1);
  asrc_finish(a);
}

void asrc_restart(asrc_t *a) {
  uint32_t wr = __atomic_load_n(&a->wr, __ATOMIC_ACQUIRE);
  // запас «за часом» = wr + frac − позиція − T/2 − 1 = target; рахуємо по модулю 2^32 кадрів (лічильник іде по колу)
  double back = (double)a->target + (double)(a->taps / 2) + 1.0 - (double)a->frac;
  a->pos = ((uint64_t)wr << 32) - (uint64_t)(int64_t)llround(back * 4294967296.0);
  a->lvl = (float)a->target;
  a->integ = 0;
  a->ppm = 0;
  a->step = a->step0;
}

void asrc_set_ratio(asrc_t *a, double fsInReal, double fsOutReal) {
  a->step0 = (uint64_t)(fsInReal / fsOutReal * 4294967296.0 + 0.5);
  a->step = a->step0;
  a->integ = 0;
}

void ASRC_FAST asrc_write(asrc_t *a, const int16_t *lr, uint32_t n) {
  uint32_t wr = a->wr;
  for (uint32_t k = 0; k < n; k++) {
    uint32_t i = (wr + k) & ASRC_IN_MASK;
    int16_t l = lr[2 * k], r = lr[2 * k + 1];
    a->in[i][0] = l;
    a->in[i][1] = r;
    a->in[i + ASRC_IN_SIZE][0] = l;
    a->in[i + ASRC_IN_SIZE][1] = r;
  }
  __atomic_store_n(&a->wr, wr + n, __ATOMIC_RELEASE);
}

static inline int32_t sat24(int32_t v) {
  return v > 8388607 ? 8388607 : v < -8388608 ? -8388608 : v;
}

bool ASRC_FAST asrc_read(asrc_t *a, int32_t *l, int32_t *r) {
  const int T = a->taps;
  uint32_t wr = __atomic_load_n(&a->wr, __ATOMIC_ACQUIRE);
  uint32_t i = (uint32_t)(a->pos >> 32);
  int32_t lev = (int32_t)(wr - i) - T / 2 - 1;
  if (lev < 0) {   // вхідних кадрів забракло — тиша, позиція стоїть (asrc_steer пересуне, коли вхід піде)
    a->under++;
    *l = *r = 0;
    return false;
  }
  if (lev > (int32_t)(ASRC_IN_SIZE - (uint32_t)T - 64)) {   // запис наздогнав читання — стрибок до цілі
    a->over++;
    asrc_restart(a);
    i = (uint32_t)(a->pos >> 32);
  }
  uint32_t frac = (uint32_t)a->pos;
  uint32_t p = frac >> (32 - ASRC_PHASES_BITS);
  int32_t mu = (int32_t)((frac >> (32 - ASRC_PHASES_BITS - 14)) & 0x3FFF);
  const int32_t *c0 = &a->coef[p * (uint32_t)T], *c1 = c0 + T;
  const int16_t(*x)[2] = &a->in[(i - (uint32_t)T / 2 + 1) & ASRC_IN_MASK];
  // Вага між фазами — у Q30 (точно), множиться на відлік уже як Q16: одне множення на канал. Так удвічі швидше, ніж
  // ділити добуток на дві частини (на RP2040 множення лише 32×32→32), а якість — у межах 16-бітного тракту передавача
  // (перевірка на Mac: bridge/test/asrc_test.c). |x·c16| < 2^31, бо |c| < 1.
  int32_t al = 0, ar = 0;
  for (int j = 0; j < T; j++) {
    int32_t c = c0[j] + ((((c1[j] - c0[j]) >> 8) * mu) >> 6);
    int32_t c16 = c >> 14;
    al += (x[j][0] * c16) >> 2;
    ar += (x[j][1] * c16) >> 2;
  }
  a->pos += a->step;
  // al = Σ x·c16 / 4 → 16 біт: >> 14; 24 біти: >> 6
  *l = sat24((al + 32) >> 6);
  *r = sat24((ar + 32) >> 6);
  return true;
}

// Регулятор кроку: запас «за часом» (див. asrc.h), трохи згладжений (0,2 с), і ПІ-регулятор зі смугою ≈ 0,05 Гц:
// розбіжність кварців у ±200 ppm він вибирає за кілька секунд, а запас за цей час відхиляється на кілька кадрів.
void ASRC_FAST asrc_steer(asrc_t *a, uint32_t n, float frac) {
  a->frac = frac;
  uint32_t wr = __atomic_load_n(&a->wr, __ATOMIC_ACQUIRE);
  int32_t lev = asrc_level(a);
  if (lev < 0 && wr != a->lastWr) {   // відстали, а вхід іде — наново від цілі (без цього тиша не закінчиться)
    a->lastWr = wr;
    asrc_restart(a);
    return;
  }
  // Запасу раптом набагато більше, ніж треба (вхід приніс пачку з минулого, вихід стояв): регулятор зганяв би це
  // секундами на межі ±1000 ppm — краще одразу стати на ціль (один короткий перескок замість довгої зайвої затримки).
  if (lev > 4 * a->target + 2 * a->burst + 64) {
    a->lastWr = wr;
    asrc_restart(a);
    return;
  }
  a->lastWr = wr;
  const float dt = (float)n / (float)a->fsOut;           // секунд між викликами
  const float klp = dt / 0.2f;
  // запас «за часом»: з дробом позиції і кадрами, що вже «звучать», але ще в дорозі
  float fine = (float)lev + frac - (float)(uint32_t)a->pos * (1.0f / 4294967296.0f);
  a->lvl += (fine - a->lvl) * klp;
  const float wn = 2.0f * ASRC_PI * 0.05f;           // власна частота петлі, рад/с
  const float fsIn = (float)a->fsIn;
  const float kp = 2.0f * wn / fsIn, ki = wn * wn / fsIn;  // демпфування 1
  float e = a->lvl - (float)a->target;
  a->integ += e * dt;
  float corr = kp * e + ki * a->integ;
  if (corr > 1e-3f) corr = 1e-3f;                         // більше ±1000 ppm кварци не розходяться
  if (corr < -1e-3f) corr = -1e-3f;
  a->ppm = corr * 1e6f;
  a->step = a->step0 + (uint64_t)(int64_t)((double)a->step0 * (double)corr);
}
