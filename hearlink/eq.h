// Эквалайзер: срез низов и пять полос. Один и тот же у приёмника (rxaudio.h, настраивается с передатчика — с 2.45)
// и на входе передатчика (txaudio.h — общий для всех приёмников; владелец 09.10: «добавь общий эквалайзер для входа
// на передатчик»).
// Шесть звеньев второго порядка подряд: срез низов 100 Гц (Баттерворт, 12 дБ на октаву) и пять полос — «полка» 125 Гц,
// колокола 400 Гц, 1 и 2,5 кГц (добротность 1), «полка» 6 кГц. Значение полосы 0…12 — это −12…+12 дБ шагом 2 (6 — ровно).
// Выключенное звено (полоса на нуле, срез не включён) не считается вовсе; когда выключено всё, any == false.
#pragma once
struct Eq6 {
  float c[6][5];        // b0 b1 b2 a1 a2 (a0 = 1)
  float z[2][6][2];     // состояние: два канала (середина и разность)
  bool on[6], any;
  void set(int k, float b0, float b1, float b2, float a0, float a1, float a2) {
    c[k][0] = b0 / a0; c[k][1] = b1 / a0; c[k][2] = b2 / a0; c[k][3] = a1 / a0; c[k][4] = a2 / a0;
  }
  void build(const uint8_t *bands, bool lowCut) {
    static const float F[5] = { 125, 400, 1000, 2500, 6000 };
    any = false;
    {   // срез низов: фильтр Баттерворта 100 Гц
      float w = 2 * PI * 100.0f / SRATE, cs = cosf(w), al = sinf(w) / (2 * 0.7071f);
      set(0, (1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
      on[0] = lowCut;
      any |= on[0];
    }
    for (int b = 0; b < 5; b++) {
      int v = bands[b] > 12 ? 6 : bands[b];
      float A = powf(10.0f, (v - 6) * 2.0f / 40.0f), w = 2 * PI * F[b] / SRATE, cs = cosf(w), sn = sinf(w);
      on[b + 1] = v != 6;
      any |= on[b + 1];
      if (b == 0 || b == 4) {   // «полки»: низ и верх
        float al = sn / 2 * 1.4142f, q = 2 * sqrtf(A) * al, s = b == 0 ? 1.0f : -1.0f;   // s: знак у косинуса
        set(b + 1, A * ((A + 1) - s * (A - 1) * cs + q), s * 2 * A * ((A - 1) - s * (A + 1) * cs), A * ((A + 1) - s * (A - 1) * cs - q),
            (A + 1) + s * (A - 1) * cs + q, -s * 2 * ((A - 1) + s * (A + 1) * cs), (A + 1) + s * (A - 1) * cs - q);
      } else {
        float al = sn / 2;   // добротность 1
        set(b + 1, 1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
      }
    }
  }
  inline float run(int ch, float x) {
    for (int k = 0; k < 6; k++) {
      if (!on[k]) continue;
      float *s = z[ch][k], *q = c[k], y = q[0] * x + s[0];
      s[0] = q[1] * x - q[3] * y + s[1];
      s[1] = q[2] * x - q[4] * y;
      x = y;
    }
    return x;
  }
};
