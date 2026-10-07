// Приёмы рисования для экранов приёмника (128×64 точки, один бит на точку, 60 кадров в секунду).
//
// Владелец 07.10: «приемники имеют огромный запас по мощности и возможностям. Глупо оставлять это без использования.
// Перепиши меню приемников под красивый мультимедийный вид с анимациями и плавной красивой работой, оповещениями…
// все должно быть максимально мультимедийно наполнено».
//
// Экран одноцветный, поэтому «полутона» делаются растром (точки в шахматном порядке разной густоты), плавность —
// частыми кадрами и движением «с разгоном и мягкой остановкой». Здесь: растр и затемнение, переходы между экранами
// (растворение, сдвиг, раскрытие кругом, шторка с мягким краем), дуги с затуханием, мерцающие искры.
// Всё работает прямо с листом экрана в памяти (байт = восемь точек столбца, младший бит сверху) и собирается и в
// плате, и на компьютере (tools/simrx).
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace rxs {

static inline float easeOutBack(float k) {   // вылететь чуть дальше цели и вернуться
  k = k < 0 ? 0 : k > 1 ? 1 : k;
  const float c1 = 1.70158f, c3 = c1 + 1;
  float u = k - 1;
  return 1 + c3 * u * u * u + c1 * u * u;
}
static inline float fract(float v) {
  return v - floorf(v);
}

// ---- растр 4×4: level точек из 16 горят
static const uint8_t BAYER4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static inline uint8_t ditherByte(int x, int level) {   // какие из восьми точек столбца x горят при густоте level/16
  uint8_t m = 0;
  for (int y = 0; y < 4; y++)
    if (BAYER4[y * 4 + (x & 3)] < level) m |= (uint8_t)(0x11 << y);
  return m;
}
static inline uint8_t rowBits(int page, int y0, int y1) {   // какие точки страницы page лежат в строках y0…y1−1
  int a = y0 - page * 8, b = y1 - page * 8;
  if (a < 0) a = 0;
  if (b > 8) b = 8;
  if (b <= a) return 0;
  return (uint8_t)(((1u << (b - a)) - 1) << a);
}
// закрасить прямоугольник растром густоты level (0…16)
static void shade(u8g2_t *g, int x, int y, int w, int h, int level) {
  uint8_t *b = u8g2_GetBufferPtr(g);
  int x1 = x + w > 128 ? 128 : x + w, y1 = y + h > 64 ? 64 : y + h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  for (int p = y / 8; p <= (y1 - 1) / 8 && y1 > y; p++) {
    uint8_t rb = rowBits(p, y, y1);
    for (int xx = x; xx < x1; xx++) b[p * 128 + xx] |= ditherByte(xx, level) & rb;
  }
}
// пригасить нарисованное в прямоугольнике: оставить keep точек из 16
static void dim(u8g2_t *g, int x, int y, int w, int h, int keep) {
  uint8_t *b = u8g2_GetBufferPtr(g);
  int x1 = x + w > 128 ? 128 : x + w, y1 = y + h > 64 ? 64 : y + h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  for (int p = y / 8; p <= (y1 - 1) / 8 && y1 > y; p++) {
    uint8_t rb = rowBits(p, y, y1);
    for (int xx = x; xx < x1; xx++) b[p * 128 + xx] &= (uint8_t)(ditherByte(xx, keep) | ~rb);
  }
}

// ---- переход между экранами: в листе уже нарисован новый экран, from — прежний кадр; k — 0…1
enum { TR_DISSOLVE = 0, TR_LEFT, TR_RIGHT, TR_UP, TR_DOWN, TR_IRIS, TR_WIPE };
static void blend(u8g2_t *g, const uint8_t *from, float k, int kind) {
  uint8_t *b = u8g2_GetBufferPtr(g);
  if (k >= 1) return;
  if (k <= 0) {
    memcpy(b, from, 1024);
    return;
  }
  float e = k * k * (3 - 2 * k);
  switch (kind) {
    case TR_LEFT:    // новый въезжает справа
    case TR_RIGHT: { // …слева
      int s = (int)(e * 128 + 0.5f);
      uint8_t row[128];
      for (int p = 0; p < 8; p++) {
        uint8_t *c = b + p * 128;
        const uint8_t *f = from + p * 128;
        for (int x = 0; x < 128; x++) {
          if (kind == TR_LEFT) row[x] = x < 128 - s ? f[x + s] : c[x - (128 - s)];
          else row[x] = x >= s ? f[x - s] : c[x + (128 - s)];
        }
        memcpy(c, row, 128);
      }
      break;
    }
    case TR_UP:      // новый поднимается снизу
    case TR_DOWN: {  // …опускается сверху
      int s = (int)(e * 64 + 0.5f);
      if (s <= 0) {
        memcpy(b, from, 1024);
        break;
      }
      if (s >= 64) break;
      for (int x = 0; x < 128; x++) {
        uint64_t cf = 0, cc = 0;
        for (int p = 0; p < 8; p++) {
          cf |= (uint64_t)from[p * 128 + x] << (p * 8);
          cc |= (uint64_t)b[p * 128 + x] << (p * 8);
        }
        uint64_t o = kind == TR_UP ? (cf >> s) | (cc << (64 - s)) : (cf << s) | (cc >> (64 - s));
        for (int p = 0; p < 8; p++) b[p * 128 + x] = (uint8_t)(o >> (p * 8));
      }
      break;
    }
    case TR_IRIS: {  // новый раскрывается кругом из середины
      float r = e * 74, r2 = r * r;
      for (int p = 0; p < 8; p++)
        for (int x = 0; x < 128; x++) {
          uint8_t m = 0;
          for (int i = 0; i < 8; i++) {
            float dx = x - 63.5f, dy = (p * 8 + i) - 31.5f;
            if (dx * dx + dy * dy < r2) m |= 1 << i;
          }
          b[p * 128 + x] = (uint8_t)((b[p * 128 + x] & m) | (from[p * 128 + x] & ~m));
        }
      break;
    }
    case TR_WIPE: {  // шторка слева направо с мягким (растровым) краем
      float front = e * (128 + 28) - 28;
      for (int x = 0; x < 128; x++) {
        float l = (front + 28 - x) * 16 / 28;
        int lv = l < 0 ? 0 : l > 16 ? 16 : (int)l;
        uint8_t m = ditherByte(x, lv);
        for (int p = 0; p < 8; p++) b[p * 128 + x] = (uint8_t)((b[p * 128 + x] & m) | (from[p * 128 + x] & ~m));
      }
      break;
    }
    default: {       // растворение
      int lv = (int)(e * 16 + 0.5f);
      for (int x = 0; x < 128; x++) {
        uint8_t m = ditherByte(x, lv);
        for (int p = 0; p < 8; p++) b[p * 128 + x] = (uint8_t)((b[p * 128 + x] & m) | (from[p * 128 + x] & ~m));
      }
    }
  }
}

// ---- дуга с «затуханием»: dens 1 — сплошная, 2 — через точку, 3 — через две; thick 2 — в две точки толщиной.
// Углы в градусах, 0 — вправо, против часовой стрелки. Радиус дробный — дуга растёт плавно.
static void ringArc(u8g2_t *g, float cx, float cy, float r, float a0, float a1, int dens, int thick) {
  if (r < 1) return;
  float step = 46.0f / r;
  int i = 0;
  for (float a = a0; a <= a1; a += step, i++) {
    if (dens > 1 && (i % dens)) continue;
    float rad = a * 0.017453293f, cs = cosf(rad), sn = sinf(rad);
    int x = (int)lroundf(cx + r * cs), y = (int)lroundf(cy - r * sn);
    if (x >= 0 && x < 128 && y >= 0 && y < 64) u8g2_DrawPixel(g, x, y);
    if (thick > 1) {
      x = (int)lroundf(cx + (r - 1) * cs);
      y = (int)lroundf(cy - (r - 1) * sn);
      if (x >= 0 && x < 128 && y >= 0 && y < 64) u8g2_DrawPixel(g, x, y);
    }
  }
}

// ---- искры: n точек в прямоугольнике, каждая мерцает в своём темпе (вспыхивает крестиком и гаснет)
static void stars(u8g2_t *g, uint32_t t, int x0, int y0, int w, int h, int n, uint32_t seed) {
  for (int i = 0; i < n; i++) {
    seed = seed * 1664525u + 1013904223u;
    int x = x0 + (int)((seed >> 8) % (uint32_t)w), y = y0 + (int)((seed >> 20) % (uint32_t)h);
    float ph = fract(t / (1300.0f + (float)((seed >> 27) & 15) * 110.0f) + (float)(seed & 255) / 255.0f);
    if (ph < 0.42f) u8g2_DrawPixel(g, x, y);
    if (ph > 0.08f && ph < 0.17f) {   // вспышка
      u8g2_DrawPixel(g, x - 1, y);
      u8g2_DrawPixel(g, x + 1, y);
      u8g2_DrawPixel(g, x, y - 1);
      u8g2_DrawPixel(g, x, y + 1);
    }
  }
}

// ---- оповещение: плашка выезжает сверху (k — 0…1: насколько она выехала), текст по середине
static void toast(u8g2_t *g, const char *text, float k) {
  if (k <= 0) return;
  int y = (int)lroundf(-17 + 18 * easeOutBack(k));
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  int tw = u8g2_GetUTF8Width(g, text), w = tw + 14;
  if (w > 126) w = 126;
  int x = (128 - w) / 2;
  u8g2_SetDrawColor(g, 0);
  u8g2_DrawBox(g, x - 1, y - 1, w + 2, 17);
  u8g2_SetDrawColor(g, 1);
  u8g2_DrawRBox(g, x, y, w, 15, 4);
  u8g2_SetDrawColor(g, 0);
  u8g2_SetClipWindow(g, x + 3, y < 0 ? 0 : y, x + w - 3, y + 15 < 0 ? 0 : y + 15);
  u8g2_DrawUTF8(g, x + (w - tw) / 2 < x + 4 ? x + 4 : x + (w - tw) / 2, y + 11, text);
  u8g2_SetMaxClipWindow(g);
  u8g2_SetDrawColor(g, 1);
}

}   // namespace rxs
