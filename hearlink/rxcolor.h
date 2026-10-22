// Цветной экран приёмника (с 3.03): IPS 1,9″ на ST7789, 320×170, горизонтально. Владелец 15.10: «можно ли его
// подключить к нашим приемникам опционально вместо текущего, естественно полностью перерисовав все меню, значки,
// заставки, эффекты, логотипы под его разрешение и цветной режим дисплея, без потери в скорости и стабильности?» —
// «делай, горизонтально 320×170».
//
// Здесь только рисование — графикой m2 (m2gfx.h: сглаженные фигуры и шрифты с кириллицей, та же, что в меню
// передатчика, и те же цвета) и никакого железа, поэтому файл собирается и на компьютере: tools/simrxc/run.sh
// (картинки и анимация — в tools/simrxc/out). Экраны те же, что у OLED (rxscreens.h), и с тем же движением.
// Кадр целиком в памяти не держится (внутренней памяти у приёмника ~56 КБ, а кадр — 106 КБ): rxstrip.h рисует его
// полосами по нескольку строк и шлёт на экран только изменившееся. Поэтому каждая функция здесь «чистая»: по одним и
// тем же данным рисует одно и то же, сколько раз её ни позови (по разу на каждую полосу), и сама ничего не меняет.
//
// Размеры. Точка этого экрана — 0,13 мм (у OLED 1,3″ — 0,23): буквы основного текста — «H» 16 точек (2,1 мм, как
// 6×13 у OLED), заголовки — 22, громкость крупно — 72 (9,6 мм). Шрифты — tools/make_fonts.py, эмблема —
// tools/gen_logo.py --rx.
#pragma once
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "m2gfx.h"
#include "lang.h"
#include "m2Big.h"
#include "m2Mid.h"
#include "fTxt.h"
#include "fTxtB.h"
#include "fNum.h"
#include "rxTxt.h"
#include "rxTxtB.h"
#include "rxNum.h"
#include "logo_rx.h"

#ifdef ARDUINO
#pragma GCC push_options
#pragma GCC optimize("O2")   // с 3.05: рисуется по разу на каждую полосу — см. m2gfx.cpp
#endif
namespace rxc {
using m2::AL_C;
using m2::AL_L;
using m2::AL_R;
typedef m2::Gfx G;

constexpr int W = 320, H = 170;
constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
// цвета — как у передатчика (txui.h: тёмная тема панелей); плитки (карточки экранов) — чёрные: владелец 16.10 «в цветном
// экране приемника фон плиток полностью черный для улучшения контрастности изображения» (до 3.14 — RGB(29, 24, 21),
// как у передатчика). Плашка выбора в меню — прежнего цвета (C_SEL): чёрная на тёмном фоне выбор не выделяла бы.
const uint16_t C_BG = RGB(16, 12, 10), C_FG = RGB(245, 241, 236), C_CARD = RGB(0, 0, 0), C_SEL = RGB(29, 24, 21);
const uint16_t C_PRIMARY = RGB(202, 153, 113), C_PRIMARY_FG = RGB(25, 15, 10), C_SECONDARY = RGB(47, 39, 34);
const uint16_t C_MUTED = RGB(41, 35, 30), C_MUTED_FG = RGB(173, 163, 151), C_BORDER = RGB(62, 54, 48);
const uint16_t C_DANGER = RGB(197, 55, 47), C_OK = RGB(52, 143, 79), C_WARN = RGB(215, 150, 40);
const uint16_t C_GLOW = RGB(255, 222, 176);   // светлое золото: огоньки, блики
const uint16_t C_SKY = RGB(120, 178, 232);    // радиоволны
const uint16_t C_BG_TOP = RGB(26, 20, 17), C_BG_BOT = RGB(12, 9, 7);   // фон: чуть светлее вверху
const GFXfont *const RF_CAP = &fTxt;     // подписи, «H» 13
const GFXfont *const RF_CAPB = &fTxtB;
const GFXfont *const RF_TXT = &rxTxt;    // основной текст, «H» 16
const GFXfont *const RF_TXTB = &rxTxtB;
const GFXfont *const RF_MID = &m2Mid;    // подзаголовки, «H» 15, Montserrat Bold
const GFXfont *const RF_BIG = &m2Big;    // заголовки, «H» 22
const GFXfont *const RF_NUM = &fNum;     // числа, «H» 44 (только цифры и знаки)
const GFXfont *const RF_HUGE = &rxNum;   // громкость крупно, «H» 72 (только цифры и знаки)

// ---------------------------------------------------------------- помощники
static inline float clamp01(float k) {
  return k < 0 ? 0 : k > 1 ? 1 : k;
}
static inline float easeOut(float k) {   // быстро начать, мягко остановиться
  k = 1 - clamp01(k);
  return 1 - k * k * k;
}
static inline float easeInOut(float k) {
  k = clamp01(k);
  return k * k * (3 - 2 * k);
}
static inline float easeOutBack(float k) {   // с лёгким перелётом
  k = clamp01(k) - 1;
  return 1 + k * k * (2.70158f * k + 1.70158f);
}
static inline float phase(uint32_t t, uint32_t from, uint32_t len) {   // какая доля отрезка [from, from + len] прошла
  if (t <= from || !len) return t > from ? 1 : 0;
  return clamp01((float)(t - from) / len);
}
static inline float fract(float x) {
  return x - floorf(x);
}
static inline uint16_t mix(uint16_t a, uint16_t b, float k) {
  return G::blend(a, b, (uint8_t)(clamp01(k) * 255 + 0.5f));
}
static inline int16_t tw(const char *s, const GFXfont *f) {
  return G::textW(s, f);
}
static inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}
static inline int utf8Len(const char *s) {   // длина первой буквы в байтах
  int n = 1;
  while ((s[n] & 0xC0) == 0x80) n++;
  return n;
}
static int charCount(const char *s) {
  int n = 0;
  for (; *s; s++)
    if ((*s & 0xC0) != 0x80) n++;
  return n;
}

// Видны ли строки y0…y0+h в нынешней полосе (кадр рисуется полосами: всё, что не в ней, можно не считать вовсе —
// ни ширин надписей, ни перевода в CP1251; с 3.05).
static inline bool vis(G &g, int y0, int h) {
  return g.visible(0, y0, W, h);
}
static inline bool textVis(G &g, int y, const GFXfont *f, int drop = 0) {
  return vis(g, y - f->yAdvance, f->yAdvance + f->yAdvance / 2 + drop);
}
static inline int16_t txt(G &g, int x, int y, const char *s, const GFXfont *f, uint16_t c, uint8_t al = AL_L, int16_t maxw = 0) {
  if (!textVis(g, y, f)) return 0;   // строка не в этой полосе: даже не переводим в CP1251
  return g.text(x, y, s, f, c, al, maxw);
}
static void bg(G &g) {
  g.vgrad(0, 0, W, H, C_BG_TOP, C_BG_BOT);
}
// Заголовок: крупный шрифт, если помещается, иначе поменьше (длинные заголовки и английский перевод).
static const GFXfont *fitTitle(const char *s, int maxw = W - 16) {
  return tw(s, RF_BIG) <= maxw ? RF_BIG : RF_MID;
}

// Строка, которая не помещается между x0 и x1, плавно ездит туда-обратно (как у OLED: полторы секунды стоит, едет,
// стоит, едет назад); помещается — стоит по выравниванию.
static void ticker(G &g, int x0, int x1, int y, const char *s, const GFXfont *f, uint16_t c, uint32_t t, uint8_t al = AL_L) {
  if (!textVis(g, y, f)) return;
  int w = tw(s, f), span = x1 - x0;
  if (w <= span) {
    txt(g, al == AL_C ? (x0 + x1) / 2 : al == AL_R ? x1 : x0, y, s, f, c, al);
    return;
  }
  int over = w - span + 6, off;
  uint32_t run = (uint32_t)over * 28, cyc = 3000 + 2 * run, k = t % cyc;
  off = k < 1500 ? 0 : k < 1500 + run ? (int)((k - 1500) / 28) : k < 3000 + run ? over : over - (int)((k - 3000 - run) / 28);
  m2::Rect old = g.narrow(x0, y - f->yAdvance, span, f->yAdvance + f->yAdvance / 2);
  txt(g, x0 + 3 - off, y, s, f, c);
  g.restore(old);
}
// Строка «печатается» по буквам: k — какая часть уже напечатана; буквы стоят там же, где в готовой строке.
static void typed(G &g, int x, int y, const char *s, const GFXfont *f, uint16_t c, float k, uint8_t al = AL_C) {
  if (!textVis(g, y, f)) return;
  int show = (int)(clamp01(k) * charCount(s) + 0.5f);
  if (show <= 0) return;
  char b[100];
  size_t i = 0;
  while (s[i] && show > 0 && i + 5 < sizeof(b)) {
    int n = utf8Len(s + i);
    memcpy(b + i, s + i, n);
    i += n;
    show--;
  }
  b[i] = 0;
  int w = tw(s, f);
  txt(g, al == AL_C ? x - w / 2 : al == AL_R ? x - w : x, y, b, f, c);
}
// Буквы встают снизу по одной, каждая со своим запозданием, и садятся с отскоком.
static void bounceIn(G &g, int cx, int y, const char *s, const GFXfont *f, uint16_t c, uint32_t t, uint32_t t0, uint32_t step, uint32_t dur, int drop) {
  if (!textVis(g, y, f, drop + 8)) return;
  int x = cx - tw(s, f) / 2, li = 0;
  for (const char *p = s; *p; li++) {
    char one[6] = { 0 };
    int n = utf8Len(p);
    memcpy(one, p, n);
    float a = phase(t, t0 + li * step, dur);
    if (a > 0) txt(g, x, y + (int)lroundf((1 - easeOutBack(a)) * drop), one, f, mix(C_BG, c, a * 1.8f));
    x += tw(one, f);
    p += n;
  }
}
// Мерцающие искры: n точек в прямоугольнике, у каждой своё место и свой период.
static void stars(G &g, uint32_t t, int x, int y, int w, int h, int n, uint32_t seed) {
  for (int i = 0; i < n; i++) {
    uint32_t r = hash32(seed + i * 977u);
    float sx = x + (r % 1000) / 1000.0f * w, sy = y + ((r >> 10) % 1000) / 1000.0f * h;
    float per = 1400 + (r >> 20) % 1600, ph = fract(t / per + (r & 255) / 255.0f);
    float a = ph < 0.5f ? ph * 2 : (1 - ph) * 2;
    a *= a;
    if (a < 0.06f || !g.visible((int)sx - 4, (int)sy - 4, 9, 9)) continue;
    g.discFast(sx, sy, 0.7f + 0.9f * a, mix(C_BG, C_GLOW, 0.25f + 0.75f * a));
    if (a > 0.6f) {   // лучики у самой яркой
      uint8_t al = (uint8_t)(110 * (a - 0.6f) / 0.4f);
      g.fillA((int)sx - 4, (int)sy, 9, 1, C_GLOW, al);
      g.fillA((int)sx, (int)sy - 4, 1, 9, C_GLOW, al);
    }
  }
}
// Полоса с бегущим бликом (ход запуска, обновления): трек, заполненная часть и светлый блик по ней.
static void progress(G &g, int x, int y, int w, int h, float k, uint32_t t, bool glint = true) {
  g.box(x, y, w, h, h / 2, C_MUTED);
  int fw = (int)(w * clamp01(k) + 0.5f);
  if (fw < h) fw = k > 0 ? h : 0;
  if (fw <= 0) return;
  g.box(x, y, fw, h, h / 2, C_PRIMARY);
  g.fillA(x + h / 2, y + 2, fw - h, 2, C_GLOW, 70);   // верхний отсвет
  if (glint && fw > 3 * h && k < 1) {
    float p = fract(t / 1100.0f);
    int bx = x + (int)(p * (fw + 30)) - 30;
    m2::Rect old = g.narrow(x, y, fw, h);
    for (int i = 0; i < 30; i++) {   // мягкий блик: ярче в середине
      float a = 1 - fabsf(i - 15.0f) / 15.0f;
      g.fillA(bx + i, y, 1, h, C_GLOW, (uint8_t)(150 * a * a));
    }
    g.restore(old);
  }
}
// Полоса оставшегося времени: тает к середине; под ней пунктир — где она была.
static void timeBar(G &g, int y, float left, int w0 = 300) {
  for (int x = (W - w0) / 2; x < (W + w0) / 2; x += 6) g.fill(x, y + 1, 2, 1, C_BORDER);
  int w = (int)(w0 * clamp01(left) + 0.5f);
  if (w > 2) g.box((W - w) / 2, y, w, 3, 1, mix(C_DANGER, C_PRIMARY, left * 2));
}
// Динамик: корпус и раструб; (x, y) — левый верхний угол, s — высота.
static void speaker(G &g, float x, float y, float s, uint16_t c) {
  float bw = s * 0.26f, bh = s * 0.40f, cy = y + s / 2;
  g.box((int)lroundf(x), (int)lroundf(cy - bh / 2), (int)lroundf(bw), (int)lroundf(bh), 2, c);
  const float p[8] = { x + bw - 1, cy - bh / 2, x + bw + s * 0.34f, y, x + bw + s * 0.34f, y + s, x + bw - 1, cy + bh / 2 };
  g.poly(p, 4, c);
}
// Волны вправо от точки (cx, cy): n дуг, раствор ±span градусов.
static void wavesR(G &g, float cx, float cy, int n, float r0, float dr, float wd, float span, uint16_t c) {
  for (int i = 0; i < n; i++) g.ringFast(cx, cy, r0 + i * dr, wd, c, 90 - span, 90 + span);
}
// Уровень сигнала: четыре столбика, растут и опадают плавно (bars — дробное 0…4); цвет — по качеству.
static uint16_t qualityColor(float bars) {
  return bars >= 2.5f ? C_OK : bars >= 1.5f ? C_WARN : C_DANGER;
}
static void sigBars(G &g, int x, int base, float bars, int bw, int gap, int h0, int dh) {
  uint16_t on = qualityColor(bars);
  for (int i = 0; i < 4; i++) {
    int hmax = h0 + i * dh, bx = x + i * (bw + gap);
    g.box(bx, base - hmax, bw, hmax, 2, C_MUTED);
    float h = hmax * clamp01(bars - i);
    int hi = (int)h;
    if (hi >= 2) g.box(bx, base - hi, bw, hi, 2, on);
    if (h - hi > 0.05f && hi < hmax) g.fillA(bx + 1, base - hi - 1, bw - 2, 1, on, (uint8_t)((h - hi) * 255));
  }
}

// ---------------------------------------------------------------- заставка
// Эмблема собирается, как у передатчика (txui.h, logoAnimFrame), только по времени, а не по кадрам: глобус
// раскрывается по кругу от верха и чуть подрастает, книга раскрывается вширь с перелётом и поднимается, крест падает
// сверху с отскоком, по готовой эмблеме расходится кольцо света и проходит косой блик. Название встаёт по букве.
// (16.10 пробовалась заставка по ролику логотипа — tools/gen_logo_video.py; на плате «карежится и тормозит», владелец:
// «чтобы не переделывать все, используй заставку из передатчика — там все работает хорошо» — вернул эту.)
struct LogoFx {
  float gS = 1, gWipe = 1, bSx = 1;
  float bDy = 0, cDy = 0;
  uint8_t gA = 255, bA = 255, cA = 255;
  float hR = 0;
  uint8_t hA = 0;
  float sPos = -9;
};
static LogoFx logoFx(uint32_t t) {
  LogoFx fx;
  float tg = t / 700.0f, tb = (t - 550.0f) / 650.0f, tc = (t - 1150.0f) / 600.0f, th = (t - 1750.0f) / 450.0f, ts = (t - 2150.0f) / 550.0f;
  fx.gWipe = tg >= 1 ? 1 : (tg <= 0 ? 0 : tg * tg * (3 - 2 * tg));
  fx.gS = 0.82f + 0.18f * easeOut(tg);
  fx.bA = tb <= 0 ? 0 : 255;
  fx.bSx = tb <= 0 ? 0 : easeOutBack(tb);
  if (fx.bSx > 1.06f) fx.bSx = 1.06f;
  fx.bDy = 10 * (1 - easeOut(tb));
  fx.cA = tc <= 0 ? 0 : (uint8_t)(255 * clamp01(tc * 2.5f));
  fx.cDy = -36 * (1 - easeOutBack(tc));
  if (th > 0 && th < 1) {
    fx.hR = 5 + 80 * easeOut(th);
    fx.hA = (uint8_t)(190 * (1 - th));
  }
  if (ts > 0 && ts < 1) fx.sPos = -0.1f + 1.2f * ts;
  return fx;
}
// быстрый угол от верха по часовой стрелке, 0…1 (ошибка ~0,3°: для края раскрытия хватает)
static inline float angle01(float dx, float dy) {   // dy — вниз
  float ax = fabsf(dx), ay = fabsf(dy), mn = ax < ay ? ax : ay, mxv = ax > ay ? ax : ay;
  if (mxv < 1e-6f) return 0;
  float a = mn / mxv, s = a * a, r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;   // atan(a), 0…π/4
  if (ay > ax) r = 1.57079637f - r;   // теперь угол от оси x, 0…π/2
  // перевод в «от верха по часовой»: (dx, −dy) — привычные оси
  float ang;   // 0…2π
  if (dx >= 0 && dy <= 0) ang = 1.57079637f - r;          // правый верх
  else if (dx >= 0) ang = 1.57079637f + r;                 // правый низ
  else if (dy > 0) ang = 4.71238898f - r;                  // левый низ
  else ang = 4.71238898f + r;                              // левый верх
  return ang * 0.159154943f;
}
static void logo(G &g, int lx, int ly, uint32_t t) {
  const int LW = RX_LOGO_W, LH = RX_LOGO_H;
  m2::Rect cr = g.clipRect();
  int y0 = cr.y - (ly + g.oy()), y1 = cr.y + cr.h - (ly + g.oy()), x0 = cr.x - (lx + g.ox()), x1 = cr.x + cr.w - (lx + g.ox());
  if (y0 < 0) y0 = 0;
  if (x0 < 0) x0 = 0;
  if (y1 > LH) y1 = LH;
  if (x1 > LW) x1 = LW;
  if (y1 <= y0 || x1 <= x0) return;
  LogoFx fx = logoFx(t);
  const bool flat = t >= 1800;   // эмблема уже собрана — готовая картинка, поверх только свет и блик
  const float cxg = LW * 0.5f, cyg = LH * 0.40f, invG = 1.0f / fx.gS, invB = fx.bSx > 0.02f ? 1.0f / fx.bSx : 0;
  const float hx = LW * 0.5f, hy = LH * 0.36f, r0 = fx.hR - 10, r1 = fx.hR + 10;
  uint16_t row[RX_LOGO_W];   // на стеке: постоянный массив занял бы память и у передатчика
  for (int y = y0; y < y1; y++) {
    int gy = (int)(cyg + (y - cyg) * invG + 0.5f), by = y - (int)lroundf(fx.bDy), cy = y - (int)lroundf(fx.cDy);
    float dy2 = (y - hy) * (y - hy);
    for (int x = x0; x < x1; x++) {
      uint16_t c = C_BG;
      if (flat) c = RX_LOGO[y * LW + x];
      else {
        if (gy >= 0 && gy < LH) {   // глобус
          int gx = (int)(cxg + (x - cxg) * invG + 0.5f);
          if (gx >= 0 && gx < LW) {
            int a = RX_GLOBE_A[gy * LW + gx];
            if (a && fx.gWipe < 1) {
              float d = (fx.gWipe * 1.03f - angle01(gx - cxg, gy - cyg)) * 32;   // мягкий край раскрытия
              a = d <= 0 ? 0 : d < 1 ? (int)(a * d) : a;
            }
            if (a) c = G::blend(c, RX_GLOBE[gy * LW + gx], (uint8_t)a);
          }
        }
        if (fx.bA && invB && by >= 0 && by < LH) {   // книга: раскрывается вширь от середины
          int bx = (int)(LW * 0.5f + (x - LW * 0.5f) * invB + 0.5f);
          if (bx >= 0 && bx < LW) {
            int a = RX_BOOK_A[by * LW + bx];
            if (a) c = G::blend(c, RX_BOOK[by * LW + bx], (uint8_t)a);
          }
        }
        if (fx.cA && cy >= 0 && cy < LH) {   // крест
          int a = RX_CROSS_A[cy * LW + x] * fx.cA / 255;
          if (a) c = G::blend(c, RX_CROSS[cy * LW + x], (uint8_t)a);
        }
      }
      if (fx.hA && c != C_BG) {   // кольцо света — по самой эмблеме
        float dx = x - hx, d2 = dx * dx + dy2;
        if (d2 > r0 * r0 && d2 < r1 * r1) {
          float d = sqrtf(d2), k = 1 - fabsf(d - fx.hR) / 10;
          if (k > 0) c = G::blend(c, C_GLOW, (uint8_t)(fx.hA * k));
        }
      }
      if (fx.sPos > -1 && c != C_BG) {   // блик: косая светлая полоса
        float u = (x + (LH - y) * 0.5f) / (LW + LH * 0.5f) - fx.sPos;
        if (u > -0.05f && u < 0.05f) c = G::blend(c, 0xFFFF, (uint8_t)(150 * (1 - fabsf(u) / 0.05f)));
      }
      row[x] = c;
    }
    g.blit(lx + x0, ly + y, x1 - x0, 1, row + x0);
  }
}
static void splash(G &g, uint32_t t) {
  g.fill(0, 0, W, H, C_BG);   // ровный фон: эмблема запечена на C_BG, на переходе цвета за ней был бы виден прямоугольник
  const int lx = (W - RX_LOGO_W) / 2, ly = 4;
  if (t > 450) {   // искры по бокам
    int n = t < 1450 ? (int)((t - 450) / 110) : 9;
    stars(g, t, 24, 14, 70, 96, n, 0x51A7u);
    stars(g, t + 400, W - 94, 14, 70, 96, n, 0xC0DEu);
  }
  logo(g, lx, ly, t);
  bounceIn(g, W / 2, 158, tr("ВІДРОДЖЕННЯ"), RF_BIG, C_FG, t, 1300, 55, 420, 30);
}

// ---------------------------------------------------------------- вышка, волны, «сигнала нет»
// Вышка: шар-огонёк, шпиль, две расходящиеся ноги и перекладины-ступени, опоры (как у OLED). lamp: 0 — не горит,
// 1 — горит, 2 — вспышка.
static void tower(G &g, float cx, float top, float base, int lamp) {
  const float apex = top + 9, h = base - apex, half = h * 0.4f + 2;
  const uint16_t c = C_PRIMARY;
  g.line(cx, top + 4, cx, apex + 1, 2.2f, c);                      // шпиль
  g.line(cx - 1, apex, cx - half, base, 3.0f, c);                  // ноги
  g.line(cx + 1, apex, cx + half, base, 3.0f, c);
  for (int lv = 1; lv <= 3; lv++) {                                // перекладины
    float y = apex + h * lv / 4.0f, w = half * lv / 4.0f;
    g.line(cx - w, y, cx + w, y, 2.0f, mix(C_BG, c, 0.85f));
  }
  g.line(cx - half - 5, base, cx - half + 5, base, 2.4f, c);       // опоры
  g.line(cx + half - 5, base, cx + half + 5, base, 2.4f, c);
  if (lamp == 0) g.ringFast(cx, top, 4.2f, 1.6f, C_MUTED_FG);
  else {
    if (lamp == 2) g.discFast(cx, top, 10, mix(C_BG, C_GLOW, 0.22f));   // ореол
    g.discFast(cx, top, lamp == 2 ? 5.2f : 4.4f, lamp == 2 ? C_GLOW : C_WARN);
  }
}
// Волны от огонька: по n дуг в каждую сторону; дуги стоят на местах, по ним от мачты наружу бежит «свет».
// Возвращает, пора ли огоньку вспыхнуть.
static bool waves(G &g, uint32_t t, float cx, float cy, int n, float r0, float dr, float maxUp, uint32_t period = 1800) {
  for (int i = 0; i < n; i++) {
    float r = r0 + i * dr, span = 42;
    if (r * 0.669f > maxUp) span = asinf(maxUp / r) * 57.29578f;
    float u = fract(t / (float)period - i * 0.16f);
    float k = u < 0.06f ? 1 : u < 0.32f ? 0.95f : u < 0.52f ? 0.7f : u < 0.7f ? 0.45f : 0.28f;
    float wd = u < 0.06f ? 3.4f : u < 0.32f ? 3.0f : 2.2f;
    float rr = r + (u < 0.06f ? 1.5f : 0);
    uint16_t c = mix(C_BG, C_SKY, k);
    g.ringFast(cx, cy, rr, wd, c, 90 - span, 90 + span);
    g.ringFast(cx, cy, rr, wd, c, 270 - span, 270 + span);
  }
  return fract(t / (float)period + 0.07f) < 0.13f;
}
// Знак «сигнала нет»: кружок запрета, в нём столбики уровня, поверх косая черта; раз в три секунды столбики пробуют
// подняться, черта бьёт по ним, от знака расходится кольцо. Возвращает true в миг удара.
static bool noSignal(G &g, uint32_t t, float cx, float cy) {
  const uint32_t T = 3000;
  uint32_t c = t % T;
  bool first = t < T;
  if (c >= 1040 && c < 1280) cx += (((c - 1040) / 40) & 1) ? 1.5f : -1.5f;   // дрожь от удара
  float sweep = first ? easeInOut(phase(t, 0, 420)) : 1;
  if (sweep > 0.01f) g.ringFast(cx, cy, 26, 4, C_DANGER, 0, 360 * sweep);
  for (int i = 0; i < 4; i++) {   // столбики
    float full = 6 + i * 6, x = cx - 14 + i * 8, base = cy + 12;
    float up = easeOut(phase(c, 150 + i * 170, 280)), down = easeInOut(phase(c, 1300 + (3 - i) * 120, 260));
    float h = 3 + (full - 3) * up * (1 - down);
    if (first && t < 300) h = 0;
    if (h >= 1) g.box((int)x, (int)(base - h), 5, (int)h, 1, C_MUTED_FG);
  }
  float strike = c < 1000 ? 1 : easeOutBack(phase(c, 1000, 240));
  bool bold = c >= 1000;
  if (first && t < 1000) strike = easeOut(phase(t, 380, 220));
  float len = 36 * clamp01(strike > 1.1f ? 1.1f : strike);
  if (len > 1) {
    float x0 = cx - 18, y0 = cy - 18;
    g.line(x0, y0, x0 + len, y0 + len, bold ? 8.5f : 6.5f, C_BG);   // тёмная кайма
    g.line(x0, y0, x0 + len, y0 + len, bold ? 4.5f : 3.0f, C_DANGER);
    float gl = phase(c, 2050, 520);
    if (bold && gl > 0 && gl < 1) {   // блик по черте
      float p = gl * 34;
      g.line(x0 + p, y0 + p, x0 + p + 3, y0 + p + 3, 3.0f, C_GLOW);
    }
  }
  float ping = phase(c, 1020, 560);
  if (ping > 0 && ping < 1) g.ringFast(cx, cy, 29 + 14 * easeOut(ping), 2.0f, mix(C_BG, C_DANGER, 1 - ping));
  return c >= 1000 && c < 1240;
}

// ---------------------------------------------------------------- поиск передатчика и «немає сигналу»
struct ScanInfo {
  const char *title = "";   // «ПОШУК ПЕРЕДАВАЧА» / «НЕМАЄ СИГНАЛУ»
  const char *label = "";   // «шукаю: канал» / «канал»
  int ch = 1, prevCh = 1;   // канал сейчас и прежний
  float roll = 1;           // 0…1 — насколько цифра канала уже «прокрутилась»
  float pos = 1;            // плавное положение отметки на шкале каналов, 1…13
  float left = 1;           // сколько осталось до ухода в ожидание, 1…0
  bool lost = false;        // сигнал был и пропал
};
static void titleBar(G &g, uint32_t t, int y, const char *s, uint16_t c = C_FG) {   // заголовок и черта под ним с бегущим бликом
  if (!textVis(g, y, RF_BIG, 12)) return;
  const GFXfont *f = fitTitle(s);
  int w = tw(s, f), x0 = (W - w) / 2;
  if (w > W - 16) w = W - 16, x0 = 8;
  txt(g, W / 2, y, s, f, c, AL_C, W - 16);
  g.fill(x0, y + 7, w, 2, C_BORDER);
  float p = fract(t / 1700.0f);
  int bx = x0 - 30 + (int)(p * (w + 60));
  m2::Rect old = g.narrow(x0, y + 7, w, 2);
  for (int i = 0; i < 40; i++) {
    float a = 1 - fabsf(i - 20.0f) / 20.0f;
    g.fillA(bx + i, y + 7, 1, 2, C_GLOW, (uint8_t)(220 * a));
  }
  g.restore(old);
}
static void channelScale(G &g, const ScanInfo &s, int y) {   // тринадцать делений, нынешний канал — плашка, едет плавно
  for (int i = 0; i < 13; i++) g.discFast(64 + i * 16, y, 1.6f, C_BORDER);
  float px = 64 + (s.pos - 1) * 16;
  g.box((int)lroundf(px) - 7, y - 4, 14, 8, 4, C_PRIMARY);
}
static void channelLabel(G &g, const ScanInfo &s, int y) {   // подпись, цифра канала прокручивается снизу вверх
  if (!textVis(g, y, RF_TXT, 8)) return;
  char num[8], prev[8];
  snprintf(num, sizeof(num), "%d", s.ch);
  snprintf(prev, sizeof(prev), "%d", s.prevCh);
  int lw = tw(s.label, RF_TXT), nw = tw("13", RF_TXTB), x0 = (W - lw - 8 - nw) / 2;
  txt(g, x0, y, s.label, RF_TXT, C_FG);
  m2::Rect old = g.narrow(x0 + lw + 6, y - 20, nw + 6, 26);
  float rk = easeOut(s.roll);
  if (rk < 1) txt(g, x0 + lw + 8, y - (int)lroundf(rk * 22), prev, RF_TXTB, C_FG);
  txt(g, x0 + lw + 8, y + (int)lroundf((1 - rk) * 22), num, RF_TXTB, C_FG);
  g.restore(old);
}
static void search(G &g, uint32_t t, const ScanInfo &s) {
  bg(g);
  titleBar(g, t, 26, s.title, s.lost ? C_WARN : C_FG);
  stars(g, t, 10, 44, 70, 64, 4, 0xBEEF1u);
  stars(g, t + 700, W - 80, 44, 70, 64, 4, 0xC0FFEu);
  float tcx = s.lost ? 116 : W / 2;
  if (s.lost) {
    bool hit = noSignal(g, t, 210, 82);
    tower(g, tcx, 50, 114, hit ? 1 : 0);
  } else {
    m2::Rect old = g.narrow(0, 36, W, 82);
    bool flash = waves(g, t, tcx, 52, 3, 15, 13, 14);
    g.restore(old);
    tower(g, tcx, 52, 114, flash ? 2 : 1);
  }
  for (int x = 90; x < 230; x += 6) g.fill(x, 117, 3, 1, C_BORDER);   // земля
  g.fill((int)tcx - 30, 116, 61, 2, C_BORDER);
  channelScale(g, s, 132);
  channelLabel(g, s, 156);
  timeBar(g, 165, s.left);
}

// ---------------------------------------------------------------- запуск, обновление, уход в ожидание, сон
static void starting(G &g, uint32_t t, uint32_t total, const char *name, const char *info) {
  bg(g);
  typed(g, W / 2, 46, tr("ЗАПУСК"), RF_BIG, C_FG, phase(t, 0, 300));
  progress(g, 30, 62, 260, 14, easeInOut(phase(t, 150, total > 400 ? total - 400 : 1)), t);
  int dy = (int)((1 - easeOutBack(phase(t, 200, 500))) * 60 + 0.5f);   // подписи выезжают снизу и садятся с отскоком
  m2::Rect old = g.narrow(0, 84, W, H - 84);
  ticker(g, 10, W - 10, 118 + dy, name, RF_TXTB, C_FG, t, AL_C);
  txt(g, W / 2, 148 + dy, info, RF_CAP, C_FG, AL_C);
  g.restore(old);
}
static void update(G &g, uint32_t t, const char *line, int percent, const char *foot) {
  bg(g);
  txt(g, W / 2, 34, tr("ОНОВЛЕННЯ"), fitTitle(tr("ОНОВЛЕННЯ")), C_FG, AL_C, W - 16);
  txt(g, W / 2, 66, line, RF_TXT, C_FG, AL_C, W - 20);
  if (percent >= 0) progress(g, 20, 82, 280, 18, percent / 100.0f, t, percent < 100);
  else g.discFast(W / 2, 92, 9, C_DANGER);
  txt(g, W / 2, 146, foot, percent >= 0 ? RF_MID : RF_TXT, percent >= 0 ? C_PRIMARY : C_FG, AL_C, W - 20);
}
static void going(G &g, uint32_t t, uint32_t total, bool off) {
  bg(g);
  const uint32_t fold = 380;
  if (t + fold >= total) {   // в конце картинка схлопывается в черту и точку, как гаснущий кинескоп
    float k = phase(t, total > fold ? total - fold : 0, fold);
    if (k < 0.65f) {
      int w = (int)(W * (1 - k / 0.65f));
      if (w < 6) w = 6;
      g.box((W - w) / 2, H / 2 - 2, w, 4, 2, C_GLOW);
      g.fillA((W - w) / 2, H / 2 - 6, w, 12, C_GLOW, 40);
    } else if (k < 0.92f) {
      float r = 4 * (1 - (k - 0.65f) / 0.27f) + 1.5f;
      g.discFast(W / 2, H / 2, r + 4, mix(C_BG, C_GLOW, 0.25f));
      g.discFast(W / 2, H / 2, r, C_GLOW);
    }
    return;
  }
  int drop = (int)((1 - easeOut(phase(t, 0, 320))) * 40 + 0.5f);   // заголовок опускается сверху
  if (off) {
    txt(g, W / 2, 40 - drop, tr("ВИМКНЕНО"), fitTitle(tr("ВИМКНЕНО")), C_FG, AL_C, W - 16);
    typed(g, W / 2, 80, tr("з передавача"), RF_TXT, C_FG, phase(t, 320, 450));
    typed(g, W / 2, 110, tr("переходжу в сон"), RF_TXT, C_FG, phase(t, 800, 550));
  } else {
    txt(g, W / 2, 36 - drop, tr("СИГНАЛ ВІДСУТНІЙ"), fitTitle(tr("СИГНАЛ ВІДСУТНІЙ")), C_WARN, AL_C, W - 16);
    int lw = (int)(280 * easeOut(phase(t, 200, 400)));
    if (lw > 0) g.fill((W - lw) / 2, 44, lw, 2, C_BORDER);
    typed(g, W / 2, 82, tr("переходжу у режим"), RF_TXT, C_FG, phase(t, 400, 550));
    typed(g, W / 2, 110, tr("очікування"), RF_TXT, C_FG, phase(t, 950, 400));
  }
  timeBar(g, 148, 1 - phase(t, 0, total - fold), 280);
}
static void sleeping(G &g, uint32_t t) {   // выключен с передатчика, а на нём нажали ручку
  bg(g);
  txt(g, 18, 52, tr("ВИМКНЕНО"), fitTitle(tr("ВИМКНЕНО"), 200), C_FG, AL_L, 200);
  txt(g, 18, 84, tr("з передавача"), RF_TXT, C_FG);
  txt(g, W / 2, 156, tr("вмикає лише передавач"), RF_TXT, C_FG, AL_C);
  float mx = 250, my = 64 + 3 * sinf(t / 900.0f);   // месяц: диск, из которого вынут сдвинутый диск
  stars(g, t, 200, 22, 110, 90, 6, 0x700Du);
  g.discFast(mx, my, 30, mix(C_BG, C_GLOW, 0.12f));
  g.discFast(mx, my, 22, C_GLOW);
  g.discFast(mx + 11, my - 6, 19, C_BG_TOP);
  for (int i = 0; i < 3; i++) {   // «z Z» всплывают и тают
    float k = fract(t / 2600.0f + i / 3.0f);
    float x = 272 + k * 22 + 5 * sinf(k * 6.28f), y = 40 - k * 30;
    if (k < 0.8f) txt(g, (int)x, (int)y, k < 0.35f ? "z" : "Z", k < 0.35f ? RF_TXT : RF_MID, mix(C_BG, C_MUTED_FG, 1 - k / 0.8f));
  }
}
// Не подключён к набору: просит доступ; код — тот же, что покажет передатчик в окне «Додати приймач».
static void unpaired(G &g, uint32_t t, unsigned code, bool heard) {
  bg(g);
  txt(g, W / 2, 30, tr("НЕ ПІДКЛЮЧЕНО"), fitTitle(tr("НЕ ПІДКЛЮЧЕНО")), C_FG, AL_C, W - 16);
  char b[64];
  int dots = (t / 400) % 4;
  const char *base = heard ? tr("прошу доступ") : tr("шукаю передавач");
  snprintf(b, sizeof(b), "%s%.*s", base, dots, "...");
  char full[64];
  snprintf(full, sizeof(full), "%s...", base);
  txt(g, (W - tw(full, RF_TXT)) / 2, 60, b, RF_TXT, C_FG);
  if (heard) {   // код — крупно, в рамке с бегущими штрихами («обратите внимание»)
    snprintf(b, sizeof(b), "%04u", code % 10000);
    int cw = tw(b, RF_NUM), kw = tw(tr("КОД"), RF_MID), fw = kw + 12 + cw + 36, fx = (W - fw) / 2, fy = 72, fh = 58;
    g.box(fx, fy, fw, fh, 12, C_CARD);
    int per = 2 * (fw + fh), ph = (t / 40) % 12;
    for (int k = 0; k < per; k += 2) {   // штрихи по периметру
      if ((k / 2 + 12 - ph) % 12 >= 6) continue;
      int x, y;
      if (k < fw) x = fx + k, y = fy;
      else if (k < fw + fh) x = fx + fw - 2, y = fy + (k - fw);
      else if (k < 2 * fw + fh) x = fx + fw - 1 - (k - fw - fh), y = fy + fh - 2;
      else x = fx, y = fy + fh - 1 - (k - 2 * fw - fh);
      g.fill(x, y, 2, 2, C_PRIMARY);
    }
    txt(g, fx + 18, fy + 40, tr("КОД"), RF_MID, C_FG);
    txt(g, fx + 18 + kw + 12, fy + 50, b, RF_NUM, C_GLOW);
    txt(g, W / 2, 158, tr("Приймачі - Додати"), RF_CAP, C_FG, AL_C);
  } else {
    m2::Rect old = g.narrow(0, 66, W, 66);
    bool flash = waves(g, t, W / 2, 80, 2, 13, 11, 12);
    g.restore(old);
    tower(g, W / 2, 80, 130, flash ? 2 : 1);
    txt(g, W / 2, 158, tr("увімкніть передавач"), RF_TXT, C_FG, AL_C);
  }
}

// ---------------------------------------------------------------- тишина, громкость, «це я»
// С 3.09 рамка «дышит» ступенями — 10 на полвдоха: экран целиком перерисовывается ~5 раз в секунду, а не 60 (рамка идёт
// по всему краю, и каждый её кадр — ~21 000 точек на экран; владелец 15.10: «при отключении звука анимация тормозит»);
// вступление (1,4 с: волны гаснут, крест штрих за штрихом, слово по букве) рисуется только в своих областях
// (sceneDyn: MUTE_FX — волны и крест, muteWordRect — слово). sceneKey знает те же ступени.
constexpr uint32_t MUTE_INTRO = 1400;
const m2::Rect MUTE_FX(96, 50, 44, 70);
static float muteBreath(uint32_t t) {
  return roundf((0.5f - 0.5f * cosf(t / 620.0f)) * 10) / 10;
}
static m2::Rect muteWordRect() {
  int w = tw(tr("ТИША"), RF_BIG);
  return m2::Rect(228 - w / 2 - 6, 66, w + 12, 78);   // буквы выезжают снизу (с 40 точек ниже), с перелётом вверх
}
static bool muteJit(uint32_t t) {   // крест изредка вздрагивает
  return t > 1200 && (t % 2300) < 90;
}
static void muted(G &g, uint32_t t) {
  bg(g);
  float br = muteBreath(t);   // рамка «дышит»
  g.frame(14, 22, W - 28, H - 44, 16, mix(C_BORDER, C_PRIMARY, 0.25f + 0.35f * br), 2);
  g.frame(10, 18, W - 20, H - 36, 20, mix(C_BG, C_PRIMARY, 0.10f + 0.18f * br), 2);
  speaker(g, 52, 55, 60, C_FG);
  for (int i = 0; i < 3; i++) {   // волны гаснут одна за другой
    float k = phase(t, i * 110, 240);
    if (k < 1) g.ringFast(88, 85, 18 + i * 12, 3, mix(C_BG, C_SKY, 1 - k), 50, 130);
  }
  float a = easeOut(phase(t, 330, 170)), c = easeOut(phase(t, 480, 170));   // крест — штрих за штрихом
  float jit = muteJit(t) ? 1.5f : 0;
  if (a > 0) g.line(104 + jit, 68, 104 + jit + 26 * a, 68 + 34 * a, 5, C_DANGER);
  if (c > 0) g.line(130 + jit, 68, 130 + jit - 26 * c, 68 + 34 * c, 5, C_DANGER);
  bounceIn(g, 228, 98, tr("ТИША"), RF_BIG, C_FG, t, 380, 70, 320, 40);
}
// Громкость: карточка поверх пригашенного главного экрана. k — насколько раскрылась, vol — деления 0…20 (дробное).
static void volumeCard(G &g, uint32_t t, float vol, float k, int limit = 20) {
  if (k <= 0) return;
  g.fillA(0, 0, W, H, 0, (uint8_t)(150 * clamp01(k * 1.5f)));
  float e = easeOutBack(k);
  int hw = (int)lroundf(122 * e), hh = (int)lroundf(62 * e);
  if (hw < 10 || hh < 8) return;
  const int cx = W / 2, cy = H / 2 + 6;
  m2::Rect old = g.narrow(cx - hw, cy - hh, 2 * hw, 2 * hh);
  g.box(cx - hw, cy - hh, 2 * hw, 2 * hh, 14, C_CARD);
  g.frame(cx - hw, cy - hh, 2 * hw, 2 * hh, 14, C_BORDER, 2);
  speaker(g, cx - 104, cy - 38, 44, C_FG);
  int wv = vol < 0.5f ? 0 : vol < 7 ? 1 : vol < 14 ? 2 : 3;
  for (int i = 0; i < wv; i++) g.ringFast(cx - 74, cy - 16, 12 + i * 9 + sinf(t / 180.0f + i) * 0.8f, 2.6f, C_SKY, 50, 130);
  if (!wv) {
    g.line(cx - 66, cy - 26, cx - 50, cy - 6, 3.5f, C_DANGER);
    g.line(cx - 50, cy - 26, cx - 66, cy - 6, 3.5f, C_DANGER);
  }
  char b[8];
  snprintf(b, sizeof(b), "%d", (int)lroundf(vol * 5));
  txt(g, cx + 64, cy + 8, b, RF_NUM, C_FG, AL_R);
  txt(g, cx + 68, cy + 8, "%", RF_MID, C_FG);
  for (int i = 0; i < 20; i++) {   // двадцать делений
    int x = cx - 104 + i * 10 + 2;
    float f = vol - i;
    if (f >= 1) g.box(x, cy + 26, 8, 14, 2, C_PRIMARY);
    else if (f > 0) {
      g.box(x, cy + 26, 8, 14, 2, C_MUTED);
      int h = (int)lroundf(14 * f);
      if (h > 1) g.box(x, cy + 40 - h, 8, h, 2, C_PRIMARY);
    } else if (i < limit) g.box(x, cy + 26, 8, 14, 2, C_MUTED);
  }
  if (limit < 20) {   // предел громкости: «стопор» за последним доступным делением
    int x = cx - 104 + limit * 10;
    g.box(x - 1, cy + 22, 3, 22, 1, C_WARN);
  }
  g.restore(old);
}
static void identify(G &g, uint32_t t, const char *name) {   // «Це я»: кольца, слово и имя, вспышки
  bg(g);
  for (int i = 0; i < 4; i++) {
    float p = fract(t / 1400.0f + i / 4.0f), r = 14 + 170 * p;
    g.ringFast(W / 2, H / 2, r, p < 0.3f ? 4.0f : 2.5f, mix(C_BG, C_PRIMARY, 1 - p));
  }
  g.box(64, 36, 192, 98, 16, C_CARD);
  g.frame(64, 36, 192, 98, 16, C_PRIMARY, 2);
  txt(g, W / 2, 80, tr("ЦЕ Я"), RF_BIG, C_GLOW, AL_C);
  ticker(g, 74, 246, 116, name, tw(name, RF_TXTB) <= 172 ? RF_TXTB : RF_CAPB, C_FG, t, AL_C);   // длинное имя — мельче, а не бегом
  if ((t % 700) < 90) g.fillA(0, 0, W, H, C_GLOW, 120);   // вспышка
}

// ---------------------------------------------------------------- главный экран
// Верхняя строка: уровень сигнала, имя приёмника (длинное — бегущей строкой), громкость; под строкой черта — когда в
// эфире звук, по ней бежит светлая засечка, в тишине черта пунктирная.
static void statusBar(G &g, uint32_t t, float bars, const char *name, int volPct, bool sound) {
  if (!vis(g, 0, 30)) return;
  sigBars(g, 10, 21, bars, 5, 2, 6, 3);
  char b[8];
  int vw = 0;
  if (volPct >= 0) {
    snprintf(b, sizeof(b), "%d", volPct);
    vw = tw(b, RF_TXTB) + tw("%", RF_CAP) + 22;
    int x = W - 8 - tw("%", RF_CAP);
    txt(g, x, 20, "%", RF_CAP, C_FG);
    txt(g, x - 1, 20, b, RF_TXTB, C_PRIMARY, AL_R);
    speaker(g, x - tw(b, RF_TXTB) - 19, 7, 14, C_MUTED_FG);
  }
  ticker(g, 44, W - 10 - vw, 20, name, RF_TXT, C_FG, t);
  if (sound) {
    g.fill(0, 27, W, 1, C_BORDER);
    float p = fract(t / 2200.0f);
    int bx = (int)(p * (W + 90)) - 90;
    m2::Rect old = g.narrow(0, 26, W, 3);
    for (int i = 0; i < 90; i += 2) {
      float a = i < 70 ? i / 70.0f : (90 - i) / 20.0f;
      g.fillA(bx + i, 26, 2, 3, C_GLOW, (uint8_t)(200 * a * a));
    }
    g.restore(old);
  } else
    for (int x = 2; x < W; x += 6) g.fill(x, 27, 2, 1, C_BORDER);
}
// Цвет рисок спектра по высоте: тёплая растяжка от тёмного золота внизу к светлому, у самого верха — к красному.
static uint16_t specColor(float k) {
  static const uint16_t K[5] = { RGB(118, 78, 48), RGB(202, 153, 113), RGB(250, 208, 148), RGB(242, 146, 62), RGB(214, 62, 46) };
  static const float Z[5] = { 0, 0.55f, 0.78f, 0.92f, 1.0f };
  k = clamp01(k);
  for (int i = 1; i < 5; i++)
    if (k <= Z[i]) return mix(K[i - 1], K[i], (k - Z[i - 1]) / (Z[i] - Z[i - 1]));
  return K[4];
}
// Спектр: 14 рисок, над каждой — отметка пика (держится и плавно опускается). lv, pk — 0…1 (уже сглаженные).
static void spectrum(G &g, const float *lv, const float *pk, int n, uint32_t t) {
  const int base = 166, top = 38, HH = base - top, bw = 16, gap = 6, x0 = (W - (n * bw + (n - 1) * gap)) / 2;
  bool quiet = true;
  for (int b = 0; b < n; b++)
    if (lv[b] * HH >= 2 || pk[b] * HH >= 3) quiet = false;
  if (quiet) {   // тишина: по низу идёт пологая волна точек — экран «дышит»
    for (int x = 8; x < W - 4; x += 8) {
      float y = 154 + 3.2f * sinf(x / 22.0f + t / 300.0f), a = 0.55f + 0.45f * sinf(x / 40.0f - t / 500.0f);
      g.discFast(x, y, 1.6f, mix(C_BG, C_MUTED_FG, a));
    }
    return;
  }
  static const float Z[5] = { 0, 0.55f, 0.78f, 0.92f, 1.0f };
  for (int b = 0; b < n; b++) {
    int x = x0 + b * (bw + gap);
    float h = clamp01(lv[b]) * HH, hp = clamp01(pk[b]) * HH;
    if (!g.visible(x, top - 8, bw, HH + 8)) continue;
    if (h < 2) g.box(x + 3, base - 3, bw - 6, 3, 1, C_BORDER);   // тихая полоса — чёрточка
    else {
      int hi = (int)h;
      for (int z = 1; z < 5; z++) {   // зоны растяжки снизу вверх
        float za = Z[z - 1] * HH, zb = Z[z] * HH;
        if (hi <= za) break;
        float tp = hi < zb ? hi : zb;
        g.vgrad(x, base - (int)tp, bw, (int)tp - (int)za, specColor(tp / HH), specColor(za / HH));
      }
      if (h - hi > 0.05f) g.fillA(x, base - hi - 1, bw, 1, specColor(h / HH), (uint8_t)((h - hi) * 255));
      g.fillA(x, base - hi, bw, 1, 0xFFFF, 60);   // светлая кромка
    }
    if (hp >= 3 && hp > h + 2) {   // отметка пика
      int py = base - (int)lroundf(hp) - 4;
      g.box(x, py, bw, 3, 1, C_FG);
    }
  }
}
// ---------------------------------------------------------------- главный экран, вид «спектр» (с 3.07; раскладка — с 3.08)
// Владелец 15.10: «анализатор спектра сделать как в радио, но с 14 столбиками…» (3.07: анализатор на 2/3 экрана), затем
// «анализатор слева по ширине половина экрана и по высоте. Справа от анализатора информация и сверху от анализатора.
// Так думаю будет распределение информации лучше, обновлений экрана меньше и оптимизация лучше», «плюс добавить в
// анализаторе значки пиков», «столбцов оставить 10» (3.08). Слева вверху — громкость крупно, под ней анализатор (всё,
// что о звуке), справа — связь, стерео/шумодав, имя приёмника. Риски — как в радио (m2player.cpp радио; txui.h,
// drawSpectrum): ровные скруглённые столбики одного цвета, тем ярче, чем громче (8 ступеней от C_SECONDARY к
// C_PRIMARY); над каждой — отметка пика (держится 0,45 с и плавно опускается: ui_rx.h, specAnimate). На экран идёт
// только изменившийся кусок столбца (analyzerStep); дрожь в одну точку не рисуется.
constexpr int VC_X = 4, VC_Y = 4, VC_W = 154, VC_H = 78;      // громкость
constexpr int AN_X = 4, AN_Y = 86, AN_W = 154, AN_H = 80;     // анализатор
constexpr int IP_X = 162, IP_Y = 4, IP_W = 154, IP_H = 162;   // сведения
constexpr int AN_N = 10, AN_BW = 10, AN_STEP = 14;
constexpr int AN_X0 = AN_X + (AN_W - (AN_N * AN_STEP - (AN_STEP - AN_BW))) / 2;   // первая риска
constexpr int AN_PH = 3, AN_PGAP = 2;                                              // отметка пика: высота и зазор над риской
constexpr int AN_BASE = AN_Y + AN_H - 8;                                           // низ рисок
constexpr int AN_HMAX = AN_BASE - (AN_Y + 7) - AN_PGAP - AN_PH;                     // наибольшая высота: отметка — не выше AN_Y + 7
const uint16_t C_PEAK = C_FG;
static inline int anTop(float lv) {
  int h = lv > 0.004f ? (int)(4 + clamp01(lv) * (AN_HMAX - 4)) : 3;
  return AN_BASE - h;
}
static inline int anPeakY(float lv, float pk) {   // верх отметки пика; −1 — отметки нет (тишина)
  if (pk <= 0.004f) return -1;
  return anTop(pk > lv ? pk : lv) - AN_PGAP - AN_PH;
}
// Цвет столбца — по высоте (3.23–3.24). Владелец 16.10: «столбцы, в зависимости от своей высоты, плавно меняют цвет от
// зеленого, при уровне до 1/3 высоты столбца и далее чем выше, тем ближе к красному, выше 1/3 высоты стремится к
// желтоватому, а при максимуме уже ярко красный»; к 3.23 (столбец целиком одного цвета, 12 ступеней): «градация цветов в
// столбиках должна меняться не резко, а плавно оттенками». С 3.24 у каждой строки точек столбика — свой оттенок по её
// высоте на шкале: до 1/3 — зелёный, до 2/3 — плавно к жёлтому, выше — через оранжевый к ярко-красному у самого верха.
// Невысокий столбец — зелёный, средний — с желтоватой верхушкой, полный — зелёный, жёлтый и красный кончик. Цвет точки
// зависит только от высоты на шкале, поэтому при росте и спаде дорисовываются только сдвинувшиеся строки (как прежде) —
// шина экрана не загружается больше (её работа слышна в звуке, см. 3.11). Тишина — тёмная чёрточка, как прежде.
const uint16_t C_AN_GREEN = RGB(48, 206, 72), C_AN_YELLOW = RGB(250, 212, 44), C_AN_RED = RGB(255, 34, 26);
static inline uint8_t anCk(float lv) {   // 0 — тишина (тёмная чёрточка), 1 — звук (столбик с оттенками)
  return lv > 0.004f ? 1 : 0;
}
static uint16_t anRowColor(int y) {   // оттенок строки y столбика: по её высоте над низом рисок
  static uint16_t tab[AN_HMAX + 2];
  static bool ready;
  if (!ready) {
    for (int k = 0; k <= AN_HMAX + 1; k++) {
      float h = (k + 0.5f) / AN_HMAX;   // 0 — низ, 1 — верх шкалы
      tab[k] = h <= 1.0f / 3 ? C_AN_GREEN : h <= 2.0f / 3 ? mix(C_AN_GREEN, C_AN_YELLOW, (h - 1.0f / 3) * 3)
                                                     : mix(C_AN_YELLOW, C_AN_RED, h >= 1 ? 1.0f : (h - 2.0f / 3) * 3);
    }
    ready = true;
  }
  int k = AN_BASE - 1 - y;
  return tab[k < 0 ? 0 : k > AN_HMAX + 1 ? AN_HMAX + 1 : k];
}
static void anBar(G &g, int i, int top, int pk, uint8_t ck) {   // риска i и её отметка пика
  int bx = AN_X0 + i * AN_STEP;
  if (!ck) g.box(bx, top, AN_BW, AN_BASE - top, 3, C_SECONDARY);
  else {
    const int h = AN_BASE - top, r = h < 6 ? h / 2 : 3;
    for (int y = top; y < AN_BASE; y++) {
      uint16_t c = anRowColor(y);
      if (y - top >= r && AN_BASE - 1 - y >= r) g.fill(bx, y, AN_BW, 1, c);   // середина: строка целиком
      else {   // скруглённые углы сверху и снизу — тем же box, обрезанным до этой строки
        m2::Rect was = g.narrow(bx, y, AN_BW, 1);
        g.box(bx, top, AN_BW, h, 3, c);
        g.restore(was);
      }
    }
  }
  if (pk >= 0) g.box(bx, pk, AN_BW, AN_PH, 1, C_PEAK);
}
// Целиком (вход на экран, смена «ключа», переход). В тишине эфира (передатчик молчит по кнопке) риски лежат, отметок
// нет, поверх — «ТИША В ЕФІРІ».
static void analyzer(G &g, const float *lv, const float *pk, bool airMute) {
  if (!vis(g, AN_Y, AN_H)) return;
  g.box(AN_X, AN_Y, AN_W, AN_H, 12, C_CARD);
  if (airMute) lv = pk = nullptr;
  for (int i = 0; i < AN_N; i++) {
    float l = lv ? lv[i] : 0, p = pk ? pk[i] : 0;
    anBar(g, i, anTop(l), anPeakY(l, p), anCk(l));
  }
  if (airMute) txt(g, AN_X + AN_W / 2, AN_Y + 40, tr("ТИША В ЕФІРІ"), RF_CAPB, C_FG, AL_C);
}
// Что нарисовано на экране по каждой риске — чтобы следующий кадр дорисовал только разницу.
struct AnState {
  int16_t top[AN_N], pk[AN_N];
  uint8_t ck[AN_N];
};
static void anRemember(AnState &st, const float *lv, const float *pk, bool airMute) {
  if (airMute) lv = pk = nullptr;
  for (int i = 0; i < AN_N; i++) {
    float l = lv ? lv[i] : 0, p = pk ? pk[i] : 0;
    st.top[i] = (int16_t)anTop(l);
    st.pk[i] = (int16_t)anPeakY(l, p);
    st.ck[i] = anCk(l);
  }
}
// Дорисовать в кадр только изменившиеся риски: кусок столбца — от верхней из прежних и новых отметок до низа сдвига
// (сменился цвет — до низа риски) — стирается и рисуется заново с обрезкой, то есть ровно так, как при рисовании
// целиком. rects — что послать на экран; возвращает, сколько.
static int analyzerStep(G &g, AnState &st, const float *lv, const float *pk, bool airMute, m2::Rect *rects) {
  if (airMute) lv = pk = nullptr;
  int n = 0;
  for (int i = 0; i < AN_N; i++) {
    float l = lv ? lv[i] : 0, p = pk ? pk[i] : 0;
    int top = anTop(l), py = anPeakY(l, p), old = st.top[i], opy = st.pk[i];
    uint8_t ck = anCk(l);
    bool same = ck == st.ck[i] && py == opy;
#ifndef AN_NO_JITTER   // проверка на компьютере (tools/simrxc) — без этого правила кадр должен совпасть с нарисованным целиком
    if (same && top >= old - 1 && top <= old + 1) continue;   // дрожь в одну точку не рисуем
#else
    if (same && top == old) continue;
#endif
    int y0 = top < old ? top : old, y1 = ck != st.ck[i] ? AN_BASE : (top > old ? top : old) + 4;   // 4 — скругление верха
    if (py >= 0) {
      if (py < y0) y0 = py;
      if (py + AN_PH > y1) y1 = py + AN_PH;
    }
    if (opy >= 0) {
      if (opy < y0) y0 = opy;
      if (opy + AN_PH > y1) y1 = opy + AN_PH;
    }
    y0 -= 1;
    y1 += 1;
    if (y1 > AN_BASE) y1 = AN_BASE;
    m2::Rect r(AN_X0 + i * AN_STEP - 1, y0, AN_BW + 2, y1 - y0);
    m2::Rect was = g.narrow(r.x, r.y, r.w, r.h);
    g.fill(r.x, r.y, r.w, r.h, C_CARD);
    anBar(g, i, top, py, ck);
    g.restore(was);
    st.top[i] = (int16_t)top;
    st.pk[i] = (int16_t)py;
    st.ck[i] = ck;
    rects[n++] = r;
  }
  return n;
}
// Соседние куски — одним, если так дешевле: каждая посылка на экран стоит ~0,2 мс подготовки (замер 15.10: 2 500 точек
// за кадр уходили за 4 мс вместо 1), это ≈ ovh точек шины. Кадр в памяти всегда равен экрану — лишние точки в общем
// куске верны.
static int mergeRects(m2::Rect *r, int n, int ovh) {
  int m = 0;
  for (int i = 0; i < n; i++) {
    if (m) {
      m2::Rect &a = r[m - 1];
      int x0 = a.x < r[i].x ? a.x : r[i].x, y0 = a.y < r[i].y ? a.y : r[i].y;
      int x1 = a.x + a.w > r[i].x + r[i].w ? a.x + a.w : r[i].x + r[i].w, y1 = a.y + a.h > r[i].y + r[i].h ? a.y + a.h : r[i].y + r[i].h;
      if ((x1 - x0) * (y1 - y0) <= a.w * a.h + r[i].w * r[i].h + ovh) {
        a = m2::Rect(x0, y0, x1 - x0, y1 - y0);
        continue;
      }
    }
    r[m++] = r[i];
  }
  return m;
}
// Громкость (слева вверху): крупное число и двадцать делений; пока крутят ручку — рамка подсвечена. Всплывающая
// карточка громкости на этом виде не показывается (с 3.08): громкость и так крупно, а карточка поверх экрана — это кадр
// целиком 60 раз в секунду, пока крутят ручку. С 3.09 число «перелистывается», как на OLED (владелец 15.10: «при
// регулировании громкости нет анимации как в олед (прелистывание цифр)»): идёт за плавной громкостью vol (ui_rx.h,
// volF), деление заполняется постепенно; пока громкость догоняет, на экран уходит только эта карточка (sceneDyn).
static inline bool volMoving(int volPct, float vol) {
  return fabsf(vol * 5 - volPct) >= 0.25f;
}
static void volumePanel(G &g, int volPct, float vol, bool active) {
  if (!vis(g, VC_Y, VC_H)) return;
  g.box(VC_X, VC_Y, VC_W, VC_H, 12, C_CARD);
  if (active) g.frame(VC_X, VC_Y, VC_W, VC_H, 12, C_PRIMARY, 2);
  float v5 = volMoving(volPct, vol) ? vol * 5 : volPct;   // догнало — ровно целевое
  v5 = v5 < 0 ? 0 : v5 > 100 ? 100 : v5;
  int v = (int)lroundf(v5);
  char b[8];
  snprintf(b, sizeof(b), "%d", v);
  int nw = tw(b, RF_NUM), pw = tw("%", RF_MID), x = VC_X + (VC_W - (nw + 4 + pw)) / 2;
  txt(g, x, VC_Y + 52, b, RF_NUM, C_FG);
  txt(g, x + nw + 4, VC_Y + 52, "%", RF_MID, C_FG);
  speaker(g, VC_X + 10, VC_Y + 58, 16, v ? C_MUTED_FG : C_DANGER);
  float vs = v5 / 5;   // деления 0…20, дробное
  for (int i = 0; i < 20; i++) {
    int sx = VC_X + 30 + i * 6, sy = VC_Y + 61;
    float f = vs - i;
    if (f >= 1) g.box(sx, sy, 4, 10, 2, C_PRIMARY);
    else {
      g.box(sx, sy, 4, 10, 2, C_MUTED);
      int h = f > 0 ? (int)lroundf(10 * f) : 0;
      if (h > 1) g.box(sx, sy + 10 - h, 4, h, 2, C_PRIMARY);
    }
  }
}
static int qualityIdx(bool signal, int rssi) {   // 0 немає, 1 слабо, 2 помірно, 3 добре, 4 відмінно
  return !signal ? 0 : rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : 1;
}
// Плашка: скруглённая, высота 22, текст шрифтом подписей; возвращает ширину.
static int pill(G &g, int x, int y, const char *s, uint16_t bg) {
  int w = tw(s, RF_CAP) + 18;
  g.box(x, y, w, 22, 11, bg);
  txt(g, x + 9, y + 16, s, RF_CAP, C_FG);
  return w;
}
// Имя — в одну строку, а не помещается — в две, по тому пробелу, при котором строки ровнее всего («Зал, 2 ряд.» /
// «4 місце»); не помещается и так — 0 (тогда бежит одной строкой).
static int nameLines(const char *s, char *a, char *b, size_t n, int maxw) {
  if (tw(s, RF_CAP) <= maxw) {
    strlcpy(a, s, n);
    return 1;
  }
  int best = -1, bestW = 1 << 30;
  for (int i = 0; s[i] && i + 1 < (int)n; i++)
    if (s[i] == ' ') {
      strlcpy(a, s, (size_t)i + 1);
      int w1 = tw(a, RF_CAP), w2 = tw(s + i + 1, RF_CAP), wm = w1 > w2 ? w1 : w2;
      if (w1 <= maxw && w2 <= maxw && wm < bestW) {
        best = i;
        bestW = wm;
      }
    }
  if (best < 0) return 0;
  strlcpy(a, s, (size_t)best + 1);
  strlcpy(b, s + best + 1, n);
  return 2;
}
// Сведения (справа): связь — столбики и слово, плашки «СТЕРЕО»/«МОНО» и «ШУМОДАВ», под чертой — имя приёмника.
static void infoPanel(G &g, uint32_t t, float bars, int quality, bool stereo, bool ns, const char *name) {
  if (!vis(g, IP_Y, IP_H)) return;
  const int L = IP_X + 10, R = IP_X + IP_W - 10;
  g.box(IP_X, IP_Y, IP_W, IP_H, 12, C_CARD);
  // «Зв'язок», имя приёмника и «%» — белые (с 3.23). Владелец 16.10: «все белого цвета, для лучшей читаемости (на данном
  // дисплее из-за не качественного черного цвета, подобные цвета как сейчас сливаются)».
  txt(g, L, IP_Y + 20, tr("Зв'язок"), RF_CAP, C_FG);
  static const char *const QW[5] = { "немає", "слабо", "помірно", "добре", "відмінно" };
  static const uint16_t QC[5] = { C_DANGER, C_DANGER, C_WARN, C_OK, C_OK };
  sigBars(g, L, IP_Y + 54, quality ? bars : 0, 6, 3, 9, 6);
  txt(g, L + 41, IP_Y + 54, tr(QW[quality]), RF_TXTB, QC[quality], AL_L, R - L - 41);
  pill(g, L, IP_Y + 66, stereo ? tr("СТЕРЕО") : tr("МОНО"), C_SECONDARY);
  if (ns) pill(g, L, IP_Y + 94, tr("ШУМОДАВ"), mix(C_SECONDARY, C_PRIMARY, 0.35f));
  g.fill(L, IP_Y + 122, R - L, 1, C_BORDER);
  char a[72], b[72];
  int nl = nameLines(name, a, b, sizeof(a), R - L);
  if (nl == 2) {
    txt(g, L, IP_Y + 138, a, RF_CAP, C_FG);
    txt(g, L, IP_Y + 156, b, RF_CAP, C_FG);
  } else if (nl == 1) txt(g, L, IP_Y + 156, a, RF_CAP, C_FG);
  else ticker(g, L, R, IP_Y + 156, name, RF_CAP, C_FG, t);
}

// Стрелочный прибор (вид «стрілки»): светлая шкала, как у настоящего, правая четверть — красная, стрелка качается
// вокруг оси под нижним краем, отметка пика ползёт вдоль шкалы, в углу — лампочка перегруза.
static void vuMeter(G &g, int x, int y, int w, int h, float lv, float pk, const char *label, uint32_t t) {
  lv = clamp01(lv);
  pk = clamp01(pk);
  const uint16_t FACE = RGB(236, 224, 196), INK = RGB(58, 44, 34);
  g.box(x, y, w, h, 10, C_CARD);
  g.box(x + 3, y + 3, w - 6, h - 26, 8, FACE);
  m2::Rect old = g.narrow(x + 3, y + 3, w - 6, h - 26);
  g.vgrad(x + 3, y + 3, w - 6, 18, RGB(250, 242, 220), FACE);
  const float D = 0.017453293f, cx = x + w / 2.0f;
  float amax = w > 200 ? 42 : 40;
  float R = (w / 2.0f - 14) / sinf(amax * D), py = y + 22 + R;
  g.ringFast(cx, py, R, 1.6f, INK, -amax, amax);
  g.ringFast(cx, py, R - 2, 4, C_DANGER, amax * 0.5f, amax);   // перегруз
  for (int i = 0; i <= 12; i++) {   // деления: каждое третье длиннее
    float a = (-amax + 2 * amax * i / 12.0f) * D, sn = sinf(a), cs = cosf(a);
    float l = i % 3 == 0 ? 9 : 5;
    g.line(cx + (R + 2) * sn, py - (R + 2) * cs, cx + (R + 2 + l) * sn, py - (R + 2 + l) * cs, i % 3 == 0 ? 2 : 1.3f, i >= 9 ? C_DANGER : INK);
  }
  if (pk > 0.03f) {   // отметка пика
    float a = (-amax + 2 * amax * pk) * D;
    g.discFast(cx + (R - 9) * sinf(a), py - (R - 9) * cosf(a), 2.4f, C_DANGER);
  }
  {   // стрелка: выходит из-за нижней панели и идёт до шкалы
    float a = (-amax + 2 * amax * lv) * D, sn = sinf(a), cs = cosf(a);
    float r0 = (py - (y + h - 23)) / cs, r1 = R + 4;
    if (r0 < 0) r0 = 0;
    g.lineFast(cx + r0 * sn + 1.2f, py - r0 * cs + 1.5f, cx + r1 * sn + 1.2f, py - r1 * cs + 1.5f, 2.6f, RGB(196, 182, 150));   // тень
    g.lineFast(cx + r0 * sn, py - r0 * cs, cx + r1 * sn, py - r1 * cs, 2.2f, INK);
  }
  g.restore(old);
  // нижняя панель: подпись и «глазок» перегруза
  txt(g, x + 12, y + h - 6, label, RF_CAPB, C_FG);
  bool hot = pk > 0.9f && (t % 240) < 160;
  if (hot) g.discFast(x + w - 16, y + h - 12, 9, mix(C_CARD, C_DANGER, 0.3f));
  g.discFast(x + w - 16, y + h - 12, 5, hot ? C_DANGER : C_MUTED);
}
static void needles(G &g, uint32_t t, float l, float r, float pl, float pr, bool stereo) {
  if (stereo) {
    vuMeter(g, 6, 34, 151, 132, l, pl, tr("Л"), t);
    vuMeter(g, 163, 34, 151, 132, r, pr, tr("П"), t);
  } else vuMeter(g, 6, 34, 308, 132, l > r ? l : r, pl > pr ? pl : pr, tr("РІВЕНЬ"), t);
}
// Вид «гучність»: громкость крупным числом — её видно издалека; слева динамик (волн столько, сколько громкости, они
// «дышат» вместе со звуком), под числом двадцать делений со «стопором» предела, у низа — полоска уровня звука.
static void bigVolume(G &g, uint32_t t, float vol, int limit, float lv) {
  speaker(g, 12, 52, 62, C_FG);
  int wv = vol < 0.5f ? 0 : vol < 7 ? 1 : vol < 14 ? 2 : 3;
  for (int i = 0; i < wv; i++) g.ringFast(50, 83, 18 + i * 11 + lv * 2.5f * sinf(t / 150.0f + i * 0.9f), i == 0 ? 3.5f : 2.6f, C_SKY, 50, 130);
  if (!wv) {
    g.line(58, 70, 80, 96, 4.5f, C_DANGER);
    g.line(80, 70, 58, 96, 4.5f, C_DANGER);
  }
  char b[8];
  snprintf(b, sizeof(b), "%d", (int)lroundf(vol * 5));
  txt(g, 268, 120, b, RF_HUGE, C_FG, AL_R);
  txt(g, 272, 120, "%", RF_BIG, C_FG);
  for (int i = 0; i < 20; i++) {
    int x = 12 + i * 15;
    float f = vol - i;
    if (f >= 1) g.box(x, 134, 12, 12, 3, C_PRIMARY);
    else {
      g.box(x, 134, 12, 12, 3, i < limit ? C_MUTED : C_BG);
      if (f > 0) {
        int w = (int)lroundf(12 * f);
        if (w > 1) g.box(x, 134, w, 12, 3, C_PRIMARY);
      }
    }
  }
  if (limit < 20) g.box(12 + limit * 15 - 3, 130, 3, 20, 1, C_WARN);
  for (int x = 12; x < 308; x += 6) g.fill(x, 162, 2, 1, C_BORDER);
  int lw = (int)lroundf(clamp01(lv) * 296);
  if (lw > 2) g.box(12, 160, lw, 5, 2, mix(C_PRIMARY, C_GLOW, lv));
}

// ---------------------------------------------------------------- экран «Зв'язок»
struct LinkInfo {
  bool signal = false;
  int rssi = 0;                 // дБм
  float bars = 0;               // столбики 0…4 (плавные)
  float lossPct = 0, reserveMs = 0;
  const int8_t *hr = nullptr;   // уровень по секундам (−128 — передатчика не было), кольцо из n
  const uint8_t *hl = nullptr;  // потери, десятые доли процента
  uint32_t pos = 0;             // сколько замеров записано всего
  int n = 0;
  float frac = 0;               // доля секунды с последнего замера: график едет плавно
};
static void linkScreen(G &g, uint32_t t, const LinkInfo &li) {
  bg(g);
  char b[32];
  txt(g, 10, 30, tr("ЗВ'ЯЗОК"), RF_BIG, C_FG);
  const char *word = !li.signal ? tr("немає") : li.rssi > -55 ? tr("відмінно") : li.rssi > -67 ? tr("добре") : li.rssi > -78 ? tr("помірно") : tr("слабо");
  uint16_t wc = !li.signal ? C_DANGER : li.rssi > -67 ? C_OK : li.rssi > -78 ? C_WARN : C_DANGER;
  txt(g, W - 10, 30, word, RF_TXTB, wc, AL_R);
  sigBars(g, 12, 86, li.signal ? li.bars : 0, 8, 4, 12, 9);
  if (li.signal) snprintf(b, sizeof(b), "%d", li.rssi);
  else snprintf(b, sizeof(b), "--");
  int nw = txt(g, 64, 86, b, RF_NUM, C_FG);
  txt(g, 68 + nw, 86, tr("дБм"), RF_CAP, C_FG);
  snprintf(b, sizeof(b), tr("втрати %.1f %%"), li.lossPct);
  txt(g, W - 10, 54, b, RF_CAP, li.lossPct >= 2 ? C_WARN : C_FG, AL_R);
  snprintf(b, sizeof(b), tr("запас %.0f мс"), li.reserveMs);
  txt(g, W - 10, 74, b, RF_CAP, C_FG, AL_R);
  // график за минуту: новое справа, под кривой — заливка, внизу — потери
  const int gx0 = 8, gx1 = 310, gy0 = 98, gy1 = 156;
  for (int x = gx0; x <= gx1; x += 6) g.fill(x, gy1 + 1, 2, 1, C_BORDER);
  for (int lvl : { -55, -67, -78 }) {   // границы «відмінно / добре / помірно»
    int y = gy1 - (int)lroundf(clamp01((lvl + 95) / 60.0f) * (gy1 - gy0));
    for (int x = gx0; x <= gx1; x += 10) g.fill(x, y, 4, 1, C_MUTED);
  }
  m2::Rect old = g.narrow(gx0 - 2, gy0 - 6, gx1 - gx0 + 6, H - gy0 + 6);
  int shown = li.pos < (uint32_t)li.n ? (int)li.pos : li.n;
  float px = -1, py = 0, lastX = -1, lastY = 0;
  const float step = (gx1 - gx0) / (float)(li.n > 1 ? li.n - 1 : 1);
  for (int k = shown - 1; k >= 0; k--) {   // k — сколько секунд назад
    uint32_t idx = (li.pos - 1 - k) % li.n;
    float x = gx1 - (k + li.frac) * step;
    if (x < gx0 - step) continue;
    int v = li.hr[idx];
    int ls = li.hl[idx];
    if (ls) {
      int lh = ls < 10 ? 3 : ls < 50 ? 6 : 10;
      g.box((int)x - 2, H - lh, 4, lh, 1, C_DANGER);
    }
    if (v == -128) {   // передатчика не было слышно — разрыв
      g.discFast(x, gy1 + 6, 1.5f, C_DANGER);
      px = -1;
      continue;
    }
    float y = gy1 - clamp01((v + 95) / 60.0f) * (gy1 - gy0);
    if (px >= 0) {
      for (int xx = (int)ceilf(px); xx <= (int)x; xx++) {   // заливка под кривой
        float yy = py + (y - py) * (xx - px) / (x - px);
        g.vgrad(xx, (int)yy, 1, gy1 - (int)yy, mix(C_BG, C_PRIMARY, 0.45f), mix(C_BG, C_PRIMARY, 0.06f));
      }
      g.lineFast(px, py, x, y, 2.2f, C_PRIMARY);
    }
    px = x;
    py = y;
    lastX = x;
    lastY = y;
  }
  if (lastX >= 0 && px == lastX) {   // свежая точка: «маячок»
    float p = fract(t / 1000.0f);
    g.discFast(lastX, lastY, 3.2f, C_GLOW);
    if (p < 0.6f) g.ringFast(lastX, lastY, 4 + p * 12, 1.6f, mix(C_BG, C_GLOW, 1 - p / 0.6f));
  }
  g.restore(old);
}

// ---------------------------------------------------------------- проверка наушников
static void earTest(G &g, uint32_t t, int side, bool stereo) {
  bg(g);
  txt(g, W / 2, 24, tr("ПЕРЕВІРКА НАВУШНИКІВ"), RF_MID, C_FG, AL_C, W - 16);
  const float cx = W / 2, cy = 104;
  bool l = side && (!stereo || side == 1), r = side && (!stereo || side == 2);
  g.ringFast(cx, cy + 6, 58, 7, C_SECONDARY, -72, 72);   // оголовье
  g.ringFast(cx, cy + 6, 58, 3, C_BORDER, -72, 72);
  for (int k = 0; k < 2; k++) {
    bool on = k ? r : l;
    float bx = k ? cx + 46 : cx - 70;
    float grow = on && (t % 300) < 150 ? 1.5f : 0;
    if (on) {
      g.box((int)(bx - grow), (int)(cy - 6 - grow), (int)(24 + 2 * grow), (int)(50 + 2 * grow), 10, C_PRIMARY);
      for (int i = 0; i < 3; i++) {   // дуги от чашки наружу
        float u = fract(t / 700.0f - i * 0.22f);
        uint16_t c = mix(C_BG, C_SKY, u < 0.45f ? 1 : u < 0.72f ? 0.6f : 0.3f);
        if (k) g.ringFast(cx + 70, cy + 19, 14 + i * 12, u < 0.25f ? 3.5f : 2.5f, c, 50, 130);
        else g.ringFast(cx - 70, cy + 19, 14 + i * 12, u < 0.25f ? 3.5f : 2.5f, c, 230, 310);
      }
    } else g.frame((int)bx, (int)(cy - 6), 24, 50, 10, C_BORDER, 3);
  }
  for (int k = 0; k < 2; k++) {   // буквы каналов по краям
    const char *ch = k ? tr("П") : tr("Л");
    bool on = k ? r : l;
    int x = k ? W - 34 : 14;
    if (on) g.box(x - 4, cy - 2, 28, 34, 8, C_PRIMARY);
    txt(g, x + 10, cy + 25, ch, RF_BIG, on ? C_PRIMARY_FG : C_FG, AL_C);
  }
  txt(g, W / 2, 162, !side ? ". . ." : !stereo ? tr("обидва вуха") : side == 1 ? tr("ліве вухо") : tr("праве вухо"), RF_TXT, C_FG, AL_C);
}

// ---------------------------------------------------------------- «Про пристрій»
struct AboutInfo {
  char name[70] = "";
  char version[12] = "";
  int channel = 0, kit = 0;
  char id[8] = "";
  float reserveMs = 0;
  int rssi = 0;
  bool signal = false;
};
static void about(G &g, uint32_t t, const AboutInfo &a) {
  bg(g);
  ticker(g, 12, W - 12, 28, a.name, RF_TXTB, C_FG, t);
  g.fill(12, 37, W - 24, 1, C_BORDER);
  char b[32];
  const char *lab[5] = { tr("Версія"), tr("Канал / набір"), tr("Номер"), tr("Запас"), tr("Сигнал") };
  for (int i = 0; i < 5; i++) {
    int y = 62 + i * 25;
    switch (i) {
      case 0: snprintf(b, sizeof(b), "%s", a.version); break;
      case 1: snprintf(b, sizeof(b), "%d / %d", a.channel, a.kit); break;
      case 2: snprintf(b, sizeof(b), "%s", a.id); break;
      case 3: snprintf(b, sizeof(b), tr("%.1f мс"), a.reserveMs); break;
      default:
        if (a.signal) snprintf(b, sizeof(b), tr("%d дБм"), a.rssi);
        else snprintf(b, sizeof(b), "--");
    }
    txt(g, 12, y, lab[i], RF_TXT, C_FG);
    txt(g, W - 12, y, b, RF_TXTB, C_FG, AL_R);
  }
}

// ---------------------------------------------------------------- меню
// Значки пунктов (20×20): тонкие линии цвета c, (x, y) — левый верхний угол. Номера — как у OLED (rxs::menuIcon) и
// ещё 22 — вид экрана, 23 — яркость.
// Полупрозрачный скруглённый прямоугольник (тень): по строке — отрезок между скруглениями, без сглаживания краёв.
static void boxA(G &g, int x, int y, int w, int h, int r, uint16_t c, uint8_t a) {
  for (int j = 0; j < h; j++) {
    float dy = j < r ? r - j - 0.5f : j >= h - r ? j - (h - r) + 0.5f : 0;
    int in = dy > 0 ? (int)lroundf(r - sqrtf(fmaxf(0.0f, (float)(r * r) - dy * dy))) : 0;
    g.fillA(x + in, y + j, w - 2 * in, 1, c, a);
  }
}
// ---------------------------------------------------------------- значки меню
// Значок рисуется в квадрате 20×20 (x, y — левый верхний угол), цветом c на фоне bg. С 3.14 (владелец 15.10: «добавь в
// меню анимированные значки, максимум красоты мультимедиа, но не в ущерб производительности») значки стоят на цветных
// плитках (iconTile), а у выбранного пункта значок движется: ph — фаза 0…1 по кругу (меньше нуля — стоит). Движется
// только он: его плитка — своя область (sceneDyn), остальной экран при этом не перерисовывается.
static inline float rotX(float cx, float px, float py, float cy, float c, float s) {
  return cx + (px - cx) * c - (py - cy) * s;
}
static inline float rotY(float cy, float px, float py, float cx, float c, float s) {
  return cy + (px - cx) * s + (py - cy) * c;
}
static void icon(G &g, int id, float x, float y, uint16_t c, float ph = -1, uint16_t bgc = C_CARD) {
  const float s = 1.6f;   // толщина линии
  const bool mv = ph >= 0;
  const float a = mv ? ph * 6.2831853f : 0, sn = mv ? sinf(a) : 0;
  switch (id) {
    case 0: {   // набор: два звена — сходятся и расходятся
      int d = mv ? (int)lroundf(1.2f + 1.2f * sn) : 0;
      g.frame((int)x - d, (int)y + 6, 12, 9, 4, c, 2);
      g.frame((int)x + 8 + d, (int)y + 6, 12, 9, 4, c, 2);
      break;
    }
    case 1: {   // канал: волна — бежит
      float px = x, py = y + 10 - 5 * sinf(-a);
      for (int i = 1; i <= 10; i++) {
        float nx = x + i * 2, ny = y + 10 - 5 * sinf(i * 0.63f - a);
        g.line(px, py, nx, ny, s, c);
        px = nx;
        py = ny;
      }
      break;
    }
    case 2: {   // запас: стакан, уровень то поднимается, то опускается
      int lv = mv ? 3 + (int)lroundf(4 * (0.5f + 0.5f * sn)) : 7;
      g.frame((int)x + 3, (int)y + 1, 14, 18, 3, c, 2);
      g.box((int)x + 6, (int)y + 16 - lv, 8, lv, 1, c);
      break;
    }
    case 3: {   // выход звука: наушники — чашки покачиваются по очереди
      float d1 = mv ? 1.2f * sn : 0, d2 = mv ? -1.2f * sn : 0;
      g.ringFast(x + 10, y + 11, 8, s, c, -90, 90);
      g.box((int)x + 1, (int)lroundf(y + 11 + d1), 4, 8, 2, c);
      g.box((int)x + 15, (int)lroundf(y + 11 + d2), 4, 8, 2, c);
      break;
    }
    case 4: {   // выводы: две точки, между ними бежит третья
      g.discFast(x + 3, y + 10, 2.5f, c);
      g.discFast(x + 17, y + 10, 2.5f, c);
      g.line(x + 7, y + 10, x + 13, y + 10, s, mv ? mix(bgc, c, 0.55f) : c);
      if (mv) g.discFast(x + 6 + 8 * ph, y + 10, 1.6f, c);
      break;
    }
    case 5: {   // экран: по стеклу проходит блик
      g.frame((int)x, (int)y + 2, 20, 13, 2, c, 2);
      g.line(x + 6, y + 18.5f, x + 14, y + 18.5f, 2, c);
      if (mv) {
        m2::Rect was = g.narrow((int)x + 2, (int)y + 4, 16, 9);
        float bx = x - 4 + 26 * ph;
        g.line(bx, y + 13, bx + 5, y + 4, 2.4f, mix(bgc, c, 0.55f));
        g.restore(was);
      }
      break;
    }
    case 6: {   // забыть набор: крест покачивается
      float r = mv ? 0.26f * sn : 0, co = cosf(r), si = sinf(r), cx = x + 10, cy = y + 10;
      g.line(rotX(cx, x + 4, y + 4, cy, co, si), rotY(cy, x + 4, y + 4, cx, co, si), rotX(cx, x + 16, y + 16, cy, co, si),
             rotY(cy, x + 16, y + 16, cx, co, si), 2.2f, c);
      g.line(rotX(cx, x + 16, y + 4, cy, co, si), rotY(cy, x + 16, y + 4, cx, co, si), rotX(cx, x + 4, y + 16, cy, co, si),
             rotY(cy, x + 4, y + 16, cx, co, si), 2.2f, c);
      break;
    }
    case 7: {   // о приборе: «i» в круге — точка подпрыгивает
      float d = mv ? 1.6f * fabsf(sn) : 0;
      g.ringFast(x + 10, y + 10, 8.5f, s, c);
      g.discFast(x + 10, y + 6 - d, 1.4f, c);
      g.line(x + 10, y + 9, x + 10, y + 15, 2, c);
      break;
    }
    case 9: {   // вид главного экрана: три столбика — пляшут
      int h1 = mv ? (int)lroundf(10 + 5 * sinf(a)) : 9, h2 = mv ? (int)lroundf(13 + 4 * sinf(a + 2.1f)) : 16,
          h3 = mv ? (int)lroundf(11 + 5 * sinf(a + 4.2f)) : 12;
      g.box((int)x + 1, (int)y + 19 - h1, 4, h1, 1, c);
      g.box((int)x + 8, (int)y + 19 - h2, 4, h2, 1, c);
      g.box((int)x + 15, (int)y + 19 - h3, 4, h3, 1, c);
      break;
    }
    case 10: {   // светодиод: огонёк с лучами — мерцает
      float r = mv ? 3.6f + 0.7f * sn : 3.6f, l = mv ? 9 + 1.5f * sn : 9;
      if (mv) g.discFast(x + 10, y + 10, 6.5f, mix(bgc, c, 0.18f + 0.12f * sn));
      g.discFast(x + 10, y + 10, r, c);
      for (int i = 0; i < 8; i++) {
        float an = i * 0.785398f;
        g.line(x + 10 + 6 * cosf(an), y + 10 + 6 * sinf(an), x + 10 + l * cosf(an), y + 10 + l * sinf(an), 1.4f, c);
      }
      break;
    }
    case 11: {   // язык: глобус — меридиан поворачивается
      g.ringFast(x + 10, y + 10, 8.5f, s, c);
      g.line(x + 1.5f, y + 10, x + 18.5f, y + 10, 1.3f, c);
      float rx = mv ? 8.0f * fabsf(cosf(a * 0.5f)) : 4.0f;
      float px = x + 10, py = y + 1.5f;
      for (int i = 1; i <= 16; i++) {   // меридиан — эллипс, ломаной
        float t2 = i * 0.392699f, nx = x + 10 + rx * sinf(t2), ny = y + 10 - 8.5f * cosf(t2);
        g.line(px, py, nx, ny, 1.3f, c);
        px = nx;
        py = ny;
      }
      break;
    }
    case 12: {   // чёткость речи: «звёздочка» — поворачивается и вспыхивает
      float r = mv ? a / 8 : 0, k = mv ? 1 + 0.12f * sn : 1, co = cosf(r), si = sinf(r);
      static const float B[16] = { 0, -9, 2.2f, -2.2f, 9, 0, 2.2f, 2.2f, 0, 9, -2.2f, 2.2f, -9, 0, -2.2f, -2.2f };
      float p[16];
      for (int i = 0; i < 8; i++) {
        float bx = B[2 * i] * k, by = B[2 * i + 1] * k;
        p[2 * i] = x + 10 + bx * co - by * si;
        p[2 * i + 1] = y + 10 + bx * si + by * co;
      }
      g.poly(p, 8, c);
      break;
    }
    case 13: {   // баланс: коромысло качается на опоре
      float r = mv ? 0.21f * sn : 0, co = cosf(r), si = sinf(r), cx = x + 10, cy = y + 6;
      float lx = rotX(cx, x + 1, y + 6, cy, co, si), ly = rotY(cy, x + 1, y + 6, cx, co, si);
      float rx2 = rotX(cx, x + 19, y + 6, cy, co, si), ry2 = rotY(cy, x + 19, y + 6, cx, co, si);
      g.line(lx, ly, rx2, ry2, 2, c);
      const float p[6] = { x + 10, y + 7, x + 5, y + 18, x + 15, y + 18 };
      g.poly(p, 3, c);
      g.box((int)lroundf(lx), (int)lroundf(ly) - 4, 4, 4, 1, c);
      g.box((int)lroundf(rx2) - 4, (int)lroundf(ry2) - 4, 4, 4, 1, c);
      break;
    }
    case 14: {   // предел громкости: столбики тянутся к «потолку»
      g.line(x, y + 2, x + 20, y + 2, 2, c);
      static const int H0[3] = { 6, 10, 13 };
      for (int i = 0; i < 3; i++) {
        int h = mv ? (int)lroundf(H0[i] + (15 - H0[i]) * (0.5f + 0.5f * sinf(a + i * 0.9f))) : H0[i];
        g.box((int)x + 1 + i * 7, (int)y + 19 - h, 4, h, 1, c);
      }
      break;
    }
    case 15: {   // проверка наушников: звучит то одна чашка, то другая
      bool left = !mv || ph < 0.5f;
      g.ringFast(x + 10, y + 11, 8, s, c, -90, 90);
      if (left) {
        g.box((int)x + 1, (int)y + 11, 5, 8, 2, c);
        g.frame((int)x + 14, (int)y + 11, 5, 8, 2, c, 1);
      } else {
        g.frame((int)x + 1, (int)y + 11, 5, 8, 2, c, 1);
        g.box((int)x + 14, (int)y + 11, 5, 8, 2, c);
      }
      break;
    }
    case 16: {   // связь: график — бежит
      float px = x + 1, py = 0;
      for (int i = 0; i <= 9; i++) {
        float u = i / 9.0f + (mv ? ph : 0);
        float ny = y + 10 - 4.5f * sinf(u * 9.4f) - 2.5f * sinf(u * 21.0f + 1);
        float nx = x + 1 + i * 2;
        if (i) g.line(px, py, nx, ny, s, c);
        px = nx;
        py = ny;
      }
      break;
    }
    case 17: {   // усиление: динамик и «плюс» — плюс пульсирует
      float k = mv ? 2.5f + 0.9f * sn : 2.5f;
      speaker(g, x, y + 3, 14, c);
      g.line(x + 17.5f - k, y + 10, x + 17.5f + k, y + 10, 2, c);
      g.line(x + 17.5f, y + 10 - k, x + 17.5f, y + 10 + k, 2, c);
      break;
    }
    case 18: {   // шумозаглушение: «крупа» мерцает и гаснет, черта наискось
      for (int i = 0; i < 9; i++) {
        uint32_t r = hash32(i * 31u + 7);
        float k = mv ? 0.5f + 0.5f * sinf(a * 2 + i * 1.7f) : 1;
        g.discFast(x + 2 + r % 16, y + 2 + (r >> 8) % 16, 1.1f, mix(bgc, c, k));
      }
      g.line(x + 2, y + 18, x + 18, y + 2, 2, c);
      break;
    }
    case 19: {   // сжатие: две черты и стрелки навстречу — сжимают
      float d = mv ? 1.6f * (0.5f + 0.5f * sn) : 0;
      g.line(x, y + 2 + d, x + 20, y + 2 + d, 2, c);
      g.line(x, y + 18 - d, x + 20, y + 18 - d, 2, c);
      g.line(x + 10, y + 5 + d, x + 10, y + 9, 2, c);
      g.line(x + 7, y + 7 + d * 0.5f, x + 10, y + 9.5f, 1.5f, c);
      g.line(x + 13, y + 7 + d * 0.5f, x + 10, y + 9.5f, 1.5f, c);
      g.line(x + 10, y + 15 - d, x + 10, y + 11, 2, c);
      g.line(x + 7, y + 13 - d * 0.5f, x + 10, y + 10.5f, 1.5f, c);
      g.line(x + 13, y + 13 - d * 0.5f, x + 10, y + 10.5f, 1.5f, c);
      break;
    }
    case 20: {   // в паузах: знак паузы — половинки мигают по очереди
      float k = mv ? 0.5f + 0.5f * sn : 1;
      g.box((int)x + 4, (int)y + 3, 4, 14, 1, mv ? mix(bgc, c, 0.35f + 0.65f * k) : c);
      g.box((int)x + 12, (int)y + 3, 4, 14, 1, mv ? mix(bgc, c, 1 - 0.65f * k) : c);
      break;
    }
    case 21: {   // перезапуск: кольцо с разрывом и стрелкой — вращается
      float r = mv ? ph * 360 : 0, rr = r * 0.0174533f, co = cosf(rr), si = sinf(rr), cx = x + 10, cy = y + 11;
      g.ringFast(cx, cy, 7, 2, c, 40 + r, 330 + r);
      float p[6] = { x + 11, y + 1, x + 11, y + 9, x + 17, y + 5 };
      for (int i = 0; i < 3; i++) {
        float px = p[2 * i], py = p[2 * i + 1];
        p[2 * i] = rotX(cx, px, py, cy, co, si);
        p[2 * i + 1] = rotY(cy, px, py, cx, co, si);
      }
      g.poly(p, 3, c);
      break;
    }
    case 22:   // вид экрана: экран с цветными точками (с 3.13 в меню нет)
      g.frame((int)x, (int)y + 2, 20, 13, 2, c, 2);
      g.discFast(x + 6, y + 8.5f, 1.8f, C_DANGER);
      g.discFast(x + 10, y + 8.5f, 1.8f, C_OK);
      g.discFast(x + 14, y + 8.5f, 1.8f, C_SKY);
      g.line(x + 6, y + 18.5f, x + 14, y + 18.5f, 2, c);
      break;
    case 24: {   // эквалайзер (с 4.00): три движка — ручки ходят вверх-вниз, каждая в свою сторону
      static const float Y0[3] = { 7, 13, 9 };
      for (int k = 0; k < 3; k++) {
        float cx = x + 4 + k * 6, cy = y + Y0[k] + (mv ? 2.2f * sinf(a + k * 2.1f) : 0);
        g.line(cx, y + 3, cx, y + 17, 1.6f, c);
        g.discFast(cx, cy, 2.6f, c);
      }
      break;
    }
    case 23: {   // яркость: солнце — лучи вращаются
      float r0 = 0.39f + (mv ? ph * 0.785398f : 0), rad = mv ? 4 + 0.5f * sn : 4;
      g.discFast(x + 10, y + 10, rad, c);
      for (int i = 0; i < 8; i++) {
        float an = i * 0.785398f + r0;
        g.line(x + 10 + 6.5f * cosf(an), y + 10 + 6.5f * sinf(an), x + 10 + 9.2f * cosf(an), y + 10 + 9.2f * sinf(an), 1.6f, c);
      }
      break;
    }
    default: {   // назад: стрелка — уходит влево
      float d = mv ? -2.5f * (0.5f + 0.5f * sn) : 0;
      g.line(x + 3 + d, y + 10, x + 18 + d, y + 10, 2, c);
      g.line(x + 3 + d, y + 10, x + 9 + d, y + 4, 2, c);
      g.line(x + 3 + d, y + 10, x + 9 + d, y + 16, 2, c);
    }
  }
}
// Цвет плитки значка — по смыслу пункта: звук и его обработка — золото, связь — бирюза, экран и вид — синий, выход звука
// и наушники — сирень, «забыть набор» — красный, перезапуск — оранжевый, о приборе — сланец, назад — серый.
static uint16_t iconTint(int id) {
  switch (id) {
    case 0: case 1: case 2: case 16: return RGB(36, 124, 108);
    case 5: case 9: case 10: case 11: case 22: case 23: return RGB(56, 100, 168);
    case 3: case 4: case 15: return RGB(126, 80, 150);
    case 6: return RGB(176, 56, 46);
    case 21: return RGB(194, 110, 38);
    case 7: return RGB(88, 96, 114);
    case 8: return RGB(82, 74, 68);
    default: return RGB(170, 118, 66);
  }
}
// Блик: верхние rows строк скруглённого прямоугольника светлеют к верху (как стекло), a0 — прозрачность первой строки.
static void sheen(G &g, int x, int y, int w, int r, int rows, uint8_t a0) {
  for (int j = 0; j < rows; j++) {
    float dy = j < r ? r - j - 0.5f : 0;
    int in = dy > 0 ? (int)lroundf(r - sqrtf(fmaxf(0.0f, (float)(r * r) - dy * dy))) : 0;
    g.fillA(x + in, y + j, w - 2 * in, 1, C_FG, (uint8_t)(a0 * (rows - j) / rows));
  }
}
// Плитка значка 26×26 (x, y — левый верхний угол); у выбранного пункта — ярче, со свечением вокруг, значок движется.
constexpr int TILE = 26;
constexpr uint32_t ICON_PERIOD = 1600;   // мс на круг движения значка
static void iconTile(G &g, int id, int x, int y, bool cur, uint32_t t) {
  uint16_t tc = iconTint(id);
  if (!cur) tc = mix(tc, C_BG, 0.38f);
  if (cur) boxA(g, x - 3, y - 3, TILE + 6, TILE + 6, 10, tc, 60);   // свечение
  g.box(x, y, TILE, TILE, 7, tc);
  sheen(g, x, y, TILE, 7, 12, cur ? 46 : 26);
  icon(g, id, x + 3, y + 3, cur ? C_FG : mix(tc, C_FG, 0.72f), cur ? fract(t / (float)ICON_PERIOD) : -1, tc);
}
struct MenuRow {
  const char *name;   // надпись (постоянная строка)
  char value[24];     // значение — копией: сцена копируется целиком (переход рисует прежнюю)
  uint8_t icon;
};
constexpr int MENU_ROW_H = 34;   // строка меню; на экране — пять строк
// sel — выбранная строка (дробная: плашка едет к цели), scroll — сдвиг списка в точках (тоже едет), edit — правка.
// С 3.14: значки — на цветных плитках, у выбранного движется; плашка выбора — с бликом и мягкой тенью; в правке новое
// значение «въезжает» с той стороны, куда повернули ручку, прежнее уходит в другую (editOld, editK 0…1, editDir ±1).
static void menuList(G &g, uint32_t t, const MenuRow *rows, int n, float sel, float scroll, bool edit, const char *editOld = "",
                     float editK = 1, int editDir = 1) {
  g.fill(0, 0, W, H, 0);   // фон меню — чёрный (владелец 16.10: «фон меню также полностью черный»; до 3.16 — тёмный перелив)
  int hy = (int)lroundf(sel * MENU_ROW_H - scroll) + 1;   // плашка выбора
  boxA(g, 5, hy + 2, W - 16, MENU_ROW_H - 2, 9, 0, 45);   // тень
  if (edit) {
    g.box(4, hy, W - 16, MENU_ROW_H - 2, 9, C_SEL);
    sheen(g, 4, hy, W - 16, 9, 14, 22);
    g.frame(4, hy, W - 16, MENU_ROW_H - 2, 9, C_PRIMARY, 2);
  } else {
    g.box(4, hy, W - 16, MENU_ROW_H - 2, 9, C_SECONDARY);
    sheen(g, 4, hy, W - 16, 9, 14, 30);
    g.box(4, hy + 7, 4, MENU_ROW_H - 16, 2, C_PRIMARY);
  }
  for (int i = 0; i < n; i++) {
    int y = i * MENU_ROW_H - (int)lroundf(scroll);
    if (y < -MENU_ROW_H || y > H) continue;
    if (!g.visible(0, y, W, MENU_ROW_H)) continue;
    bool cur = fabsf(sel - i) < 0.5f;
    iconTile(g, rows[i].icon, 12, y + 4, cur, t);
    int vw = rows[i].value[0] ? tw(rows[i].value, edit && cur ? RF_TXTB : RF_TXT) : 0;
    // Все буквы меню — белые (3.27). Владелец 16.10: «буквы в меню цветного дисплея сделай все белыми» (до того
    // невыбранные пункты и значения — серые, выбранное значение — золотое: на этом экране серое сливается с чёрным).
    txt(g, 46, y + 23, rows[i].name, RF_TXT, C_FG, AL_L, W - 70 - vw - 12);
    if (!vw) continue;
    if (edit && cur) {   // правка: значение «въезжает», стрелки по сторонам ходят туда-сюда
      float a = 2.5f * sinf(t / 170.0f);
      int vx = W - 36;
      if (editK < 1 && editOld && editOld[0]) {   // «перекат», как у счётчика: больше — новое снизу, меньше — сверху
        float e = easeOut(editK);
        int sh = (int)lroundf(e * 20) * editDir;
        m2::Rect was = g.narrow(vx - 116, y + 3, 118, MENU_ROW_H - 6);
        txt(g, vx, y + 23 - sh, editOld, RF_TXTB, mix(C_SEL, C_FG, 1 - e), AL_R);
        txt(g, vx, y + 23 + editDir * 20 - sh, rows[i].value, RF_TXTB, mix(C_SEL, C_FG, e), AL_R);
        g.restore(was);
      } else txt(g, vx, y + 23, rows[i].value, RF_TXTB, C_FG, AL_R);
      const float lp[6] = { vx - vw - 14 - a, y + 17.0f, vx - vw - 7 - a, y + 11.0f, vx - vw - 7 - a, y + 23.0f };
      const float rp[6] = { W - 18 + a, y + 17.0f, W - 25 + a, y + 11.0f, W - 25 + a, y + 23.0f };
      g.poly(lp, 3, C_PRIMARY);
      g.poly(rp, 3, C_PRIMARY);
    } else txt(g, W - 20, y + 23, rows[i].value, RF_TXT, C_FG, AL_R);
  }
  // полоса прокрутки
  int th = H * 5 / n, ty = (int)lroundf(scroll * H / (n * MENU_ROW_H));
  g.box(W - 6, 2, 4, H - 4, 2, C_MUTED);
  g.box(W - 6, ty + 2, 4, th - 4 > 8 ? th - 4 : 8, 2, C_PRIMARY);
}
// Где плитка значка выбранной строки (для sceneDyn): вместе со свечением.
static m2::Rect menuIconRect(float sel, float scroll) {
  int y = (int)lroundf(sel) * MENU_ROW_H - (int)lroundf(scroll);
  return m2::Rect(9, y + 1, TILE + 6, TILE + 6);
}

// ---------------------------------------------------------------- оповещение
// Плашка сверху: выезжает за четверть секунды, держится полторы, уезжает (k — 0…1…0).
static void toast(G &g, const char *s, float k) {
  if (k <= 0) return;
  int w = tw(s, RF_TXT) + 36;
  if (w > W - 16) w = W - 16;
  int x = (W - w) / 2, y = -40 + (int)lroundf(easeOut(k) * 46);
  boxA(g, x + 2, y + 4, w, 34, 17, 0, 120);   // тень
  g.box(x, y, w, 34, 17, C_CARD);
  g.frame(x, y, w, 34, 17, C_PRIMARY, 2);
  txt(g, W / 2, y + 24, s, RF_TXT, C_FG, AL_C, w - 24);
}

// ---------------------------------------------------------------- сцена
// Всё, по чему рисуется любой экран, — одним куском: ui_rx.h заполняет её раз за кадр, rxstrip.h рисует по ней каждую
// полосу, а для перехода хранится копия прежней. Виды экрана — те же, что в ui_rx.h (SC_…).
enum { SC_NONE, SC_UPDATE, SC_IDENT, SC_SLEEP, SC_SPLASH, SC_UNPAIRED, SC_GOING, SC_STARTING, SC_SEARCH, SC_MUTE, SC_MAIN, SC_LOST, SC_MENU, SC_ABOUT,
       SC_LINK, SC_EAR };
constexpr int MENU_MAX = 36;   // с 4.00 пунктов 33 (+ эквалайзер)
struct Scene {
  uint8_t sid = SC_NONE;
  uint32_t t = 0;              // время, по которому идёт движение этого экрана, мс
  // строка состояния и главный экран
  char name[70] = "";
  float bars = 0;
  int volPct = -1;
  bool sound = false, stereo = false;
  uint8_t view = 0;            // 0 — спектр, 1 — стрелки, 2 — громкость крупно
  const float *spec = nullptr, *peak = nullptr;
  int nb = 0;
  float vuL = 0, vuR = 0, vuLP = 0, vuRP = 0;
  float vol = 0, volK = 0;
  int limit = 20;
  bool ns = false, airMute = false;   // шумозаглушение включено; передатчик дал «тишу в эфире» (с 3.07)
  int rssi = 0;
  bool signal = false;
  // меню
  MenuRow rows[MENU_MAX];
  int nrows = 0;
  float sel = 0, scroll = 0;
  bool edit = false;
  char editOld[24] = "";   // правка: прежнее значение (уходит), новое въезжает (с 3.14)
  float editK = 1;         // 0…1 — насколько въехало
  int8_t editDir = 1;      // куда повернули ручку
  // поиск и «немає сигналу», запуск, обновление, уход, «не підключено», «про пристрій», «зв'язок», навушники
  ScanInfo si;
  char siTitle[40] = "", siLabel[40] = "";
  uint32_t total = 0;
  char info[64] = "", line[64] = "", foot[64] = "";
  int percent = 0;
  bool off = false;
  unsigned code = 0;
  bool heard = false;
  AboutInfo about;
  LinkInfo li;
  int earSide = 0;
  // оповещение поверх
  char toast[48] = "";
  float toastK = 0;
};
static void drawScene(G &g, const Scene &s) {
  switch (s.sid) {
    case SC_UPDATE: update(g, s.t, s.line, s.percent, s.foot); break;
    case SC_IDENT: identify(g, s.t, s.name); break;
    case SC_SLEEP: sleeping(g, s.t); break;
    case SC_SPLASH: splash(g, s.t); break;
    case SC_UNPAIRED: unpaired(g, s.t, s.code, s.heard); break;
    case SC_GOING: going(g, s.t, s.total, s.off); break;
    case SC_STARTING: starting(g, s.t, s.total, s.name, s.info); break;
    case SC_SEARCH:
    case SC_LOST: {
      ScanInfo si = s.si;
      si.title = s.siTitle;
      si.label = s.siLabel;
      search(g, s.t, si);
      break;
    }
    case SC_MUTE: muted(g, s.t); break;
    case SC_MAIN:
      if (s.view == 0) {   // с 3.08: громкость и анализатор слева, сведения справа
        g.fill(0, 0, W, H, C_BG);
        volumePanel(g, s.volPct, s.vol, s.volK > 0);
        analyzer(g, s.spec, s.peak, s.airMute);
        // уровень связи — четвертями деления: ровно с той точностью, с какой его учитывает ключ сцены (sceneKey),
        // иначе дорисовка «по изменениям» отстала бы от кадра, нарисованного целиком
        infoPanel(g, s.t, lroundf(s.bars * 4) / 4.0f, qualityIdx(s.signal, s.rssi), s.stereo, s.ns, s.name);
        break;
      }
      bg(g);
      statusBar(g, s.t, s.bars, s.name, s.view == 2 ? -1 : s.volPct, s.sound);
      if (s.view == 1) needles(g, s.t, s.vuL, s.vuR, s.vuLP, s.vuRP, s.stereo);
      else bigVolume(g, s.t, s.vol, s.limit, s.vuL > s.vuR ? s.vuL : s.vuR);
      if (s.view != 2) volumeCard(g, s.t, s.vol, s.volK, s.limit);
      break;
    case SC_MENU: menuList(g, s.t, s.rows, s.nrows, s.sel, s.scroll, s.edit, s.editOld, s.editK, s.editDir); break;
    case SC_ABOUT: about(g, s.t, s.about); break;
    case SC_LINK: linkScreen(g, s.t, s.li); break;
    case SC_EAR: earTest(g, s.t, s.earSide, s.stereo); break;
    default: bg(g);
  }
  if (s.toastK > 0) toast(g, s.toast, s.toastK);
}

// Что из сцены рисуется «по событию», а что движется само (с 3.07). sceneKey — отпечаток всего, от чего зависит
// неподвижная часть экрана: пока он тот же, экран целиком не перерисовывается (rxstrip.h). 0 — экран движется весь
// (поиск, запуск, «це я»…): рисовать каждый кадр целиком. sceneDyn — области, которые движутся сами (бегущая строка,
// стрелки правки); они рисуются каждый кадр, риски анализатора — отдельно (analyzerStep).
static inline void keyAdd(uint32_t &h, const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
}
static inline void keyStr(uint32_t &h, const char *s) {
  for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  h = (h ^ 0xFF) * 16777619u;
}
static uint32_t sceneKey(const Scene &s) {
  uint32_t h = 2166136261u;
  keyAdd(h, &s.sid, 1);
  if (s.toastK > 0) keyAdd(h, &s.t, 4);   // оповещение едет — всё целиком
  switch (s.sid) {
    case SC_MAIN: {
      if (s.view != 0) return 0;   // стрелки и громкость крупно движутся
      int q = qualityIdx(s.signal, s.rssi), bb = (int)lroundf(s.bars * 4);
      keyAdd(h, &s.volPct, 4);
      keyAdd(h, &q, 4);
      keyAdd(h, &bb, 4);
      uint8_t fl = (s.stereo ? 1 : 0) | (s.ns ? 2 : 0) | (s.airMute ? 4 : 0) | (s.volK > 0 ? 8 : 0) | (volMoving(s.volPct, s.vol) ? 16 : 0);
      keyAdd(h, &fl, 1);
      keyStr(h, s.name);
      break;
    }
    case SC_MUTE: {   // ступени «дыхания» рамки и вздрагивание креста; вступление — в своих областях (sceneDyn)
      uint8_t st = (uint8_t)lroundf(muteBreath(s.t) * 10) | (muteJit(s.t) ? 0x80 : 0) | (s.t < MUTE_INTRO ? 0x40 : 0);
      keyAdd(h, &st, 1);
      break;
    }
    case SC_MENU: {
      for (int i = 0; i < s.nrows; i++) {
        keyStr(h, s.rows[i].name);
        keyStr(h, s.rows[i].value);
      }
      int hy = (int)lroundf(s.sel * MENU_ROW_H - s.scroll), sc = (int)lroundf(s.scroll), cur = (int)lroundf(s.sel);   // то, что рисуется
      keyAdd(h, &hy, 4);
      keyAdd(h, &sc, 4);
      keyAdd(h, &cur, 4);
      keyAdd(h, &s.edit, 1);
      break;
    }
    case SC_ABOUT: {
      AboutInfo a = s.about;
      a.name[0] = 0;   // имя — бегущей строкой, его движение — в sceneDyn
      keyStr(h, s.about.name);
      keyAdd(h, &a.channel, 4);
      keyAdd(h, &a.kit, 4);
      keyStr(h, a.version);
      keyStr(h, a.id);
      int r = (int)lroundf(a.reserveMs * 10);
      keyAdd(h, &r, 4);
      keyAdd(h, &a.rssi, 4);
      keyAdd(h, &a.signal, 1);
      break;
    }
    default:
      return 0;
  }
  return h ? h : 1;
}
static int sceneDyn(const Scene &s, m2::Rect *r) {
  int n = 0;
  if (s.sid == SC_MAIN && s.view == 0) {
    char a[72], b[72];
    if (!nameLines(s.name, a, b, sizeof(a), IP_W - 20)) r[n++] = m2::Rect(IP_X + 6, IP_Y + 138, IP_W - 12, 24);   // имя едет
    if (volMoving(s.volPct, s.vol)) r[n++] = m2::Rect(VC_X, VC_Y, VC_W, VC_H);   // громкость «перелистывается»
  } else if (s.sid == SC_MENU && s.edit) {
    int y = (int)lroundf(s.sel * MENU_ROW_H - s.scroll);
    r[n++] = m2::Rect(0, y, W, MENU_ROW_H);   // стрелки правки «ходят», значение въезжает
  } else if (s.sid == SC_MENU) {
    r[n++] = menuIconRect(s.sel, s.scroll);   // значок выбранного пункта движется (с 3.14)
  } else if (s.sid == SC_ABOUT) {
    if (tw(s.about.name, RF_TXTB) > W - 24) r[n++] = m2::Rect(0, 8, W, 28);
  } else if (s.sid == SC_MUTE && s.t < MUTE_INTRO) {
    r[n++] = MUTE_FX;
    r[n++] = muteWordRect();
  }
  return n;
}

}   // namespace rxc
#ifdef ARDUINO
#pragma GCC pop_options
#endif
