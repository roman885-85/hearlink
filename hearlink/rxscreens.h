// Экраны приёмника с движением: заставка, поиск передатчика, запуск, уход в ожидание, сообщения.
//
// Здесь только рисование (сишные вызовы библиотеки U8g2) и никакого железа — поэтому этот же файл собирается
// на компьютере, и кадры можно посмотреть без платы: tools/simrx/run.sh (картинки и анимация — в tools/simrx/out).
// Каждая функция получает время t в миллисекундах от начала своего экрана и рисует один кадр в очищенный лист.
#pragma once
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "logo_oled.h"
#include "lang.h"
#include "rxfx.h"   // растр, переходы, дуги с затуханием, искры, оповещения

namespace rxs {

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
// какая доля отрезка [from, from + len] уже прошла, 0…1
static inline float phase(uint32_t t, uint32_t from, uint32_t len) {
  if (t <= from || !len) return t > from ? 1 : 0;
  return clamp01((float)(t - from) / len);
}

static void centered(u8g2_t *g, int y, const char *s) {
  if (y < 1 || y > 90) return;
  u8g2_DrawUTF8(g, (128 - u8g2_GetUTF8Width(g, s)) / 2, y, s);
}

static int charCount(const char *s) {
  int n = 0;
  for (; *s; s++)
    if ((*s & 0xC0) != 0x80) n++;
  return n;
}

// Строка «печатается» по буквам: k — какая часть уже напечатана. Буквы стоят там же, где будут в готовой строке.
static void typed(u8g2_t *g, int y, const char *s, float k) {
  char b[72];
  int show = (int)(clamp01(k) * charCount(s) + 0.5f);
  if (show <= 0) return;
  size_t i = 0;
  while (s[i] && show > 0 && i + 5 < sizeof(b)) {
    b[i] = s[i];
    i++;
    while ((s[i] & 0xC0) == 0x80) {
      b[i] = s[i];
      i++;
    }
    show--;
  }
  b[i] = 0;
  u8g2_DrawUTF8(g, (128 - u8g2_GetUTF8Width(g, s)) / 2, y, b);
}

// дуга окружности от угла a0 до a1 (градусы, 0 — вправо, против часовой стрелки)
static void arc(u8g2_t *g, int cx, int cy, int r, int a0, int a1) {
  float step = 40.0f / r;   // градусов на точку: соседние точки ближе одного пикселя
  for (float a = a0; a <= a1; a += step) {
    float rad = a * 0.017453293f;
    int x = cx + (int)lroundf(r * cosf(rad)), y = cy - (int)lroundf(r * sinf(rad));
    if (x >= 0 && x < 128 && y >= 0 && y < 64) u8g2_DrawPixel(g, x, y);
  }
}

// полоса времени: тает к середине; left — сколько осталось, 1…0
static void timeBar(u8g2_t *g, int y, float left) {
  int w = (int)(108 * clamp01(left) + 0.5f);
  for (int x = 10; x < 118; x += 4) u8g2_DrawPixel(g, x, y + 1);   // след — где полоса была
  if (w > 0) u8g2_DrawBox(g, (128 - w) / 2, y, w, 3);
}

// ---- Заставка. Итог — та же картинка, что на усилителе наушников: эмблема и «ВІДРОДЖЕННЯ».
// Эмблема проявляется сверху вниз за светлой чертой (перед чертой — растровая «дымка»), по бокам загораются искры,
// название встаёт по букве — каждая подпрыгивает снизу и садится на место, — потом по эмблеме проходит блик.
static void splash(u8g2_t *g, uint32_t t) {
  const int lx = (128 - OLED_LOGO_W) / 2;
  float k = easeInOut(phase(t, 100, 900));
  int edge = (int)(k * OLED_LOGO_H + 0.5f);
  if (edge > 0) {
    u8g2_SetClipWindow(g, 0, 0, 128, edge + 6 > OLED_LOGO_H ? OLED_LOGO_H : edge + 6);
    u8g2_DrawXBMP(g, lx, 0, OLED_LOGO_W, OLED_LOGO_H, OLED_LOGO);
    u8g2_SetMaxClipWindow(g);
    if (k < 1) {   // шесть строк под чертой — эмблема ещё «в дымке»: проступает растром
      dim(g, lx - 2, edge, OLED_LOGO_W + 4, 3, 8);
      dim(g, lx - 2, edge + 3, OLED_LOGO_W + 4, 3, 3);
    }
  }
  if (k > 0 && k < 1) {
    u8g2_DrawHLine(g, lx - 12, edge, OLED_LOGO_W + 24);
    u8g2_DrawPixel(g, lx - 14, edge);   // черта с «искрами» на концах
    u8g2_DrawPixel(g, lx + OLED_LOGO_W + 13, edge);
  }
  if (t > 500) {   // искры по бокам от эмблемы
    int n = t < 1500 ? (int)((t - 500) / 125) : 8;
    stars(g, t, 3, 2, lx - 10, 38, n, 0x51A7u);
    stars(g, t + 400, lx + OLED_LOGO_W + 7, 2, lx - 10, 38, n, 0xC0DEu);
  }
  // название — по букве, каждая со своим запозданием
  const char *nm = tr("ВІДРОДЖЕННЯ");
  u8g2_SetFont(g, u8g2_font_10x20_t_cyrillic);
  int total = u8g2_GetUTF8Width(g, nm), x = (128 - total) / 2, li = 0;
  for (const char *c = nm; *c; li++) {
    char one[5] = { 0 };
    int n = 1;
    while ((c[n] & 0xC0) == 0x80) n++;
    memcpy(one, c, n);
    float a = phase(t, 900 + li * 55, 420);
    if (a > 0) {
      int y = 63 + (int)lroundf((1 - easeOutBack(a)) * 22);
      if (y < 84) u8g2_DrawUTF8(g, x, y, one);
    }
    x += u8g2_GetUTF8Width(g, one);
    c += n;
  }
  float sh = phase(t, 2100, 700);
  if (sh > 0 && sh < 1) {   // блик: тёмная косая полоска пробегает по светлому рисунку
    int pos = lx - 6 + (int)(sh * (OLED_LOGO_W + OLED_LOGO_H / 2 + 12));
    u8g2_SetDrawColor(g, 0);
    for (int y = 0; y < OLED_LOGO_H; y++) {
      int x0 = pos - y / 2, x1 = x0 + 4;
      if (x0 < lx) x0 = lx;
      if (x1 > lx + OLED_LOGO_W) x1 = lx + OLED_LOGO_W;
      if (x1 > x0) u8g2_DrawHLine(g, x0, y, x1 - x0);
    }
    u8g2_SetDrawColor(g, 1);
  }
}

// ---- Вышка — общая для «поиска», «нет сигнала» и «не подключено».
// Владелец 07.10 о прежней (тонкая решётка в одну точку, от неё далеко разлетались редкие скобки): «вышку более
// понятно нарисовать и волны более понятно отобразить, основной вопрос был именно в этом». Теперь это знак, который
// узнаётся с первого взгляда: шар-огонёк, шпиль, две толстые расходящиеся ноги на опорах и перекладины между ними,
// как ступени. Раскосов нет нарочно: на такой высоте (два десятка точек) кресты и зигзаги сливаются в кашу — пять
// вариантов сравнивал картинкой. lamp: 0 — огонёк не горит (пустое колечко), 1 — горит, 2 — вспышка.
static void tower(u8g2_t *g, int cx, int top, int base, int lamp) {
  const int apex = top + 5, h = base - apex, half = h * 2 / 5 + 1;
  u8g2_DrawVLine(g, cx, top + 2, apex - top - 2);            // шпиль
  u8g2_DrawLine(g, cx - 1, apex, cx - half, base);           // ноги — в две точки толщиной
  u8g2_DrawLine(g, cx, apex, cx - half + 1, base);
  u8g2_DrawLine(g, cx + 1, apex, cx + half, base);
  u8g2_DrawLine(g, cx, apex, cx + half - 1, base);
  for (int lv = 1; lv <= 3; lv++) {                          // перекладины
    int y = apex + h * lv / 4, w = half * lv / 4;
    u8g2_DrawHLine(g, cx - w, y, 2 * w + 1);
  }
  u8g2_DrawHLine(g, cx - half - 2, base, 5);                 // опоры
  u8g2_DrawHLine(g, cx + half - 2, base, 5);
  if (lamp == 0) u8g2_DrawCircle(g, cx, top, 2, U8G2_DRAW_ALL);
  else {
    u8g2_DrawDisc(g, cx, top, 2, U8G2_DRAW_ALL);
    if (lamp == 2) {                                         // вспышка — лучики
      u8g2_DrawPixel(g, cx, top - 4);
      u8g2_DrawPixel(g, cx - 3, top - 3);
      u8g2_DrawPixel(g, cx + 3, top - 3);
    }
  }
}

// пара дуг — справа и слева от точки (cx, cy), зеркально: радиус r, раствор ±span градусов; dens 1 — сплошная,
// 2 — через точку, 3 — через две; thick — толщина в точках (внутрь от r)
static void wavePair(u8g2_t *g, int cx, int cy, float r, float span, int dens, int thick) {
  float step = 40.0f / r;
  int i = 0;
  for (float a = -span; a <= span + 0.01f; a += step, i++) {
    if (dens > 1 && (i % dens)) continue;
    float rad = a * 0.017453293f, cs = cosf(rad), sn = sinf(rad);
    for (int k = 0; k < thick; k++) {
      int dx = (int)lroundf((r - k) * cs), y = cy - (int)lroundf((r - k) * sn);
      if (y < 0 || y > 63) continue;
      if (cx + dx < 128) u8g2_DrawPixel(g, cx + dx, y);
      if (cx - dx >= 0) u8g2_DrawPixel(g, cx - dx, y);
    }
  }
}

// ---- Волны от вершины вышки: по n дуг в каждую сторону, как на привычном знаке «((( • )))». Дуги стоят на своих
// местах — знак читается в любом кадре, — а от мачты наружу по ним бежит «свет»: дуга вспыхивает жирной, на миг
// подавшись наружу, потом становится тонкой, потом пунктиром, и к этому времени уже вспыхнула следующая.
// maxUp — на сколько точек дуге можно подняться над огоньком (выше — заголовок): дальним дугам раствор убавляется.
// Возвращает, пора ли огоньку вспыхнуть (он вспыхивает чуть раньше первой дуги).
static bool waves(u8g2_t *g, uint32_t t, int cx, int cy, int n, int r0, int dr, float maxUp, uint32_t period = 1800) {
  for (int i = 0; i < n; i++) {
    int r = r0 + i * dr;
    float span = 42;
    if (r * 0.669f > maxUp) span = asinf(maxUp / r) * 57.29578f;
    float u = fract(t / (float)period - i * 0.16f);
    if (u < 0.06f) wavePair(g, cx, cy, r + 1, span, 1, 2);
    else if (u < 0.32f) wavePair(g, cx, cy, r, span, 1, 2);
    else if (u < 0.52f) wavePair(g, cx, cy, r, span, 1, 1);
    else if (u < 0.70f) wavePair(g, cx, cy, r, span, 2, 1);
    else wavePair(g, cx, cy, r, span, 3, 1);
  }
  return fract(t / (float)period + 0.07f) < 0.13f;
}

// ---- Знак «сигнала нет»: кружок запрета, в нём «лесенка» уровня сигнала (та же, что в строке состояния при работе),
// поверх — косая черта. Знак живой (владелец 07.10: «перечеркнутый знак можно было тоже сделать анимированным»):
// кружок при появлении обегает по кругу; дальше раз в три секунды столбики один за другим пробуют подняться
// черта бьёт по ним наотмашь — знак вздрагивает, от него расходится тающее кольцо, — и столбики
// по очереди оседают. Черта видна всегда (иначе на миг читалось бы «сигнал есть»): пока столбики тянутся — тонкая,
// после удара — жирная, и по ней пробегает блик. Возвращает true в миг удара (вышке — мигнуть огоньком).
static bool noSignal(u8g2_t *g, uint32_t t, int cx, int cy) {
  const uint32_t T = 3000;
  uint32_t c = t % T;
  bool first = t < T;
  int sx = c >= 1040 && c < 1280 ? (((c - 1040) / 40) & 1 ? 1 : -1) : 0;   // дрожь от удара
  cx += sx;
  // кружок
  float sweep = first ? easeInOut(phase(t, 0, 420)) : 1;
  for (float a = 0; a < 360 * sweep; a += 2.0f) {
    float rad = (90 - a) * 0.017453293f, cs = cosf(rad), sn = sinf(rad);
    for (float r = 12; r <= 13.01f; r += 0.5f) u8g2_DrawPixel(g, cx + (int)lroundf(r * cs), cy - (int)lroundf(r * sn));
  }
  // столбики: тянутся вверх и оседают
  for (int i = 0; i < 4; i++) {
    int full = 3 + i * 3, x = cx - 7 + i * 4, base = cy + 6;
    float up = easeOut(phase(c, 150 + i * 170, 280)), down = easeInOut(phase(c, 1300 + (3 - i) * 120, 260));
    int h = 2 + (int)lroundf((full - 2) * up * (1 - down));
    if (first && t < 300) h = 0;
    if (h <= 0) continue;
    u8g2_DrawBox(g, x, base - h, 3, h);
  }
  // черта
  float strike = c < 1000 ? 1 : easeOutBack(phase(c, 1000, 240));
  bool bold = c >= 1000;
  if (first && t < 1000) strike = easeOut(phase(t, 380, 220));
  int len = (int)lroundf(18 * strike);
  if (len > 20) len = 20;
  if (len > 0) {
    int x0 = cx - 9, y0 = cy - 9;
    u8g2_SetDrawColor(g, 0);   // тёмная кайма — чтобы черта не слипалась со столбиками
    for (int w = (bold ? 2 : 1); w <= (bold ? 3 : 2); w++) {
      u8g2_DrawLine(g, x0 + w, y0, x0 + w + len, y0 + len);
      u8g2_DrawLine(g, x0 - w, y0, x0 - w + len, y0 + len);
    }
    u8g2_SetDrawColor(g, 1);
    for (int w = (bold ? -1 : 0); w <= (bold ? 1 : 0); w++) u8g2_DrawLine(g, x0 + w, y0, x0 + w + len, y0 + len);
    float gl = phase(c, 2050, 520);
    if (bold && gl > 0 && gl < 1) {   // блик: тёмный разрыв бежит по черте
      int p = (int)(gl * 18);
      u8g2_SetDrawColor(g, 0);
      for (int w = -1; w <= 1; w++) u8g2_DrawLine(g, x0 + w + p, y0 + p, x0 + w + p + 2, y0 + p + 2);
      u8g2_SetDrawColor(g, 1);
    }
  }
  // кольцо от удара
  float ping = phase(c, 1020, 560);
  if (ping > 0 && ping < 1) ringArc(g, cx, cy, 14.5f + 7 * easeOut(ping), 0, 359, ping < 0.35f ? 1 : ping < 0.7f ? 2 : 3, 1);
  return c >= 1000 && c < 1240;
}

// ---- Поиск передатчика и «немає сигналу»: что показать
struct ScanInfo {
  const char *title;      // «ПОШУК ПЕРЕДАВАЧА» / «НЕМАЄ СИГНАЛУ»
  const char *label;      // «шукаю: канал» / «канал»
  int ch = 1, prevCh = 1; // канал сейчас и прежний
  float roll = 1;         // 0…1 — насколько цифра канала уже «прокрутилась» от прежней к нынешней
  float pos = 1;          // плавное положение отметки на шкале каналов, 1…13
  float left = 1;         // сколько осталось до ухода в ожидание, 1…0
  bool lost = false;      // сигнал был и пропал: вышка без волн и знак «сигнала нет»; t — от появления экрана
};

// ---- Поиск передатчика (и «немає сигналу» во время работы): вышка с волнами под звёздами, под ней шкала тринадцати
// каналов с бегущей отметкой, подпись с «прокручивающейся» цифрой канала и полоса оставшегося времени.
static void search(u8g2_t *g, uint32_t t, const ScanInfo &s) {
  u8g2_SetFont(g, u8g2_font_7x13_t_cyrillic);   // жирный шрифт 6×13 на этом экране сливается — проверено картинкой
  centered(g, 10, s.title);
  {   // под заголовком пробегает блик — тёмный разрыв в тонкой черте
    int tw = u8g2_GetUTF8Width(g, s.title), x0 = (128 - tw) / 2;
    u8g2_DrawHLine(g, x0, 12, tw);
    int bx = x0 - 8 + (int)(fract(t / 1700.0f) * (tw + 16));
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawHLine(g, bx < x0 ? x0 : bx, 12, 7);
    u8g2_SetDrawColor(g, 1);
  }
  u8g2_SetClipWindow(g, 0, 13, 128, 45);
  stars(g, t, 2, 15, 22, 24, 3, 0xBEEF1u);        // звёзды — только по краям: рядом с волнами их точки путали бы рисунок
  stars(g, t + 700, 104, 15, 22, 24, 3, 0xC0FFEu);
  int tcx = s.lost ? 40 : 64;
  if (s.lost) {
    // Сигнала нет: вышка стоит без волн, огонёк не горит, рядом — живой знак «сигнала нет». В миг, когда черта бьёт
    // по столбикам, огонёк на вышке коротко вспыхивает и гаснет — «пробовал и не вышло».
    bool hit = noSignal(g, t, 88, 29);
    tower(g, tcx, 20, 43, hit ? 1 : 0);
  } else {
    bool flash = waves(g, t, tcx, 22, 3, 8, 6, 7.2f);
    tower(g, tcx, 22, 43, flash ? 2 : 1);
  }
  u8g2_SetMaxClipWindow(g);
  for (int x = 30; x < 98; x += 3) u8g2_DrawPixel(g, x, 44);   // земля
  u8g2_DrawHLine(g, tcx - 13, 44, 27);
  // шкала каналов: тринадцать делений, нынешний канал — столбик, отметка едет плавно
  for (int i = 0; i < 13; i++) u8g2_DrawPixel(g, 16 + i * 8, 48);
  {
    float px = 16 + (s.pos - 1) * 8;
    int x = (int)lroundf(px);
    u8g2_DrawBox(g, x - 2, 46, 5, 3);
    u8g2_DrawPixel(g, x, 49);
  }
  // подпись: цифра канала прокручивается
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  char num[8], prev[8];
  snprintf(num, sizeof(num), "%d", s.ch);
  snprintf(prev, sizeof(prev), "%d", s.prevCh);
  int lw = u8g2_GetUTF8Width(g, s.label), nw = u8g2_GetStrWidth(g, "13"), x0 = (128 - lw - 4 - nw) / 2;
  u8g2_DrawUTF8(g, x0, 60, s.label);
  u8g2_SetClipWindow(g, x0 + lw + 3, 50, 128, 61);
  float rk = easeOut(s.roll);
  if (rk < 1) u8g2_DrawStr(g, x0 + lw + 4, 60 - (int)lroundf(rk * 11), prev);
  u8g2_DrawStr(g, x0 + lw + 4, 60 + (int)lroundf((1 - rk) * 11), num);
  u8g2_SetMaxClipWindow(g);
  // оставшееся время — тонкая полоса у нижнего края, тает к середине
  int w = (int)(120 * clamp01(s.left) + 0.5f);
  if (w > 0) u8g2_DrawHLine(g, (128 - w) / 2, 63, w);
}

// ---- Запуск: слово печатается по буквам, полоса наполняется.
// Строка, которая не помещается между x0 и x1, плавно ездит туда-обратно (полторы секунды стоит, едет 25 точек в
// секунду, полторы стоит, едет назад); помещается — стоит на месте с x0. Имя приёмника с версии 2.19 бывает до 32 букв,
// а на экране их помещается 14–21. clipY0/clipY1 — границы по высоте, за которые рисовать нельзя.
static void ticker(u8g2_t *g, int x0, int x1, int y, const char *s, uint32_t t, int clipY0 = -1, int clipY1 = -1) {
  int w = u8g2_GetUTF8Width(g, s), span = x1 - x0, off = 0;
  if (w > span) {
    int over = w - span + 2;
    uint32_t run = (uint32_t)over * 40, cyc = 3000 + 2 * run, k = t % cyc;
    off = k < 1500 ? 0 : k < 1500 + run ? (int)((k - 1500) / 40) : k < 3000 + run ? over : over - (int)((k - 3000 - run) / 40);
  }
  u8g2_SetClipWindow(g, x0, clipY0 < 0 ? y - 12 : clipY0, x1, clipY1 < 0 ? y + 4 : clipY1);
  u8g2_DrawUTF8(g, x0 - off, y, s);
  u8g2_SetMaxClipWindow(g);
}

static void starting(u8g2_t *g, uint32_t t, uint32_t total, const char *name, const char *info) {
  u8g2_SetFont(g, u8g2_font_10x20_t_cyrillic);
  typed(g, 17, tr("ЗАПУСК"), phase(t, 0, 300));
  u8g2_DrawRFrame(g, 14, 23, 100, 9, 3);
  int w = (int)(easeInOut(phase(t, 150, total > 400 ? total - 400 : 1)) * 94 + 0.5f);
  if (w > 0) {
    u8g2_DrawBox(g, 17, 26, w, 3);
    if (w > 12 && w < 94) {   // по наполненной части бежит тёмный блик, перед её краем — растровый «хвост»
      int bx = 17 + (int)(fract(t / 900.0f) * (w + 6)) - 6;
      u8g2_SetDrawColor(g, 0);
      for (int k = 0; k < 4; k++)
        if (bx + k >= 17 && bx + k < 17 + w) u8g2_DrawVLine(g, bx + k, 26, 3);
      u8g2_SetDrawColor(g, 1);
      shade(g, 17 + w, 26, 6, 3, 6);
    }
  }
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  u8g2_SetClipWindow(g, 0, 34, 128, 64);
  int dy = (int)((1 - easeOutBack(phase(t, 200, 500))) * 30 + 0.5f);   // подписи выезжают снизу и садятся с отскоком
  if (u8g2_GetUTF8Width(g, name) <= 126) centered(g, 47 + dy, name);
  else {   // длинное имя — бегущей строкой
    ticker(g, 1, 127, 47 + dy, name, t, 34, 64);
    u8g2_SetClipWindow(g, 0, 34, 128, 64);
  }
  centered(g, 61 + dy, info);
  u8g2_SetMaxClipWindow(g);
}

// ---- Обновление прошивки по радио (владелец 07.10: «во время обновлений приемников по воздуху, на экране
// соответствующее сообщение с ходом выполнения обновления»): что делается, полоска и проценты. По закрашенной части
// полоски пробегает светлая засечка — видно, что работа идёт, даже когда проценты стоят.
static void update(u8g2_t *g, uint32_t t, const char *line, int percent, const char *foot) {
  u8g2_SetFont(g, u8g2_font_7x13_t_cyrillic);
  centered(g, 11, tr("ОНОВЛЕННЯ"));
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  centered(g, 26, line);
  if (percent >= 0) {
    u8g2_DrawRFrame(g, 8, 31, 112, 11, 3);
    int w = percent > 100 ? 106 : percent * 106 / 100;
    if (w > 0) {
      u8g2_DrawBox(g, 11, 34, w, 5);
      if (w > 10 && percent < 100) {
        int x = 11 + (int)((t % 1100) * (w + 8) / 1100) - 8;
        u8g2_SetDrawColor(g, 0);
        for (int i = 0; i < 5; i++) {
          int xx = x + i - 2;
          if (xx >= 11 && xx < 11 + w) u8g2_DrawVLine(g, xx, 34 + (i == 0 || i == 4 ? 1 : 0), i == 0 || i == 4 ? 3 : 5);
        }
        u8g2_SetDrawColor(g, 1);
      }
    }
  }
  centered(g, 58, foot);
}

// ---- Уход в ожидание: сообщение, полоса обратного отсчёта, в конце картинка схлопывается в черту и точку.
// off — приёмник выключили с передатчика; иначе — пропал сигнал.
static void going(u8g2_t *g, uint32_t t, uint32_t total, bool off) {
  const uint32_t fold = 380;
  if (t + fold >= total) {
    float k = phase(t, total > fold ? total - fold : 0, fold);
    if (k < 0.65f) {
      int w = (int)(128 * (1 - k / 0.65f));
      if (w < 3) w = 3;
      u8g2_DrawHLine(g, (128 - w) / 2, 32, w);
    } else if (k < 0.92f) u8g2_DrawBox(g, 63, 31, 2, 2);
    return;
  }
  int drop = (int)((1 - easeOut(phase(t, 0, 320))) * 22 + 0.5f);   // заголовок опускается сверху
  if (off) {
    u8g2_SetFont(g, u8g2_font_10x20_t_cyrillic);
    centered(g, 16 - drop, tr("ВИМКНЕНО"));
    u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
    typed(g, 31, tr("з передавача"), phase(t, 320, 450));
    typed(g, 46, tr("переходжу в сон"), phase(t, 800, 550));
  } else {
    u8g2_SetFont(g, u8g2_font_7x13_t_cyrillic);
    centered(g, 13 - drop, tr("СИГНАЛ ВІДСУТНІЙ"));
    u8g2_DrawHLine(g, 8, 17, (int)(112 * easeOut(phase(t, 200, 400))));
    u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
    typed(g, 32, tr("переходжу у режим"), phase(t, 400, 550));
    typed(g, 47, tr("очікування"), phase(t, 950, 400));
  }
  timeBar(g, 56, 1 - phase(t, 0, total - fold));
}

// ---- Приёмник спит по команде с передатчика, а на нём нажали ручку: подсказать, как включить.
static void sleeping(u8g2_t *g, uint32_t t) {
  u8g2_SetFont(g, u8g2_font_10x20_t_cyrillic);
  u8g2_DrawUTF8(g, 4, 18, tr("ВИМКНЕНО"));
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  u8g2_DrawUTF8(g, 4, 34, tr("з передавача"));
  centered(g, 60, tr("ручку двічі - увімк."));
  {   // месяц: диск, из которого вынут сдвинутый диск; вокруг — редкие звёзды
    int mx = 108, my = 30 + (int)lroundf(1.5f * sinf(t / 900.0f));
    u8g2_DrawDisc(g, mx, my, 8, U8G2_DRAW_ALL);
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawDisc(g, mx + 4, my - 2, 7, U8G2_DRAW_ALL);
    u8g2_SetDrawColor(g, 1);
    stars(g, t, 88, 20, 38, 26, 4, 0x700Du);
  }
  for (int i = 0; i < 3; i++) {   // «z z z» всплывают от месяца и тают
    float k = fract(t / 2600.0f + i / 3.0f);
    int x = 112 + (int)(k * 12 + 3 * sinf(k * 6.28f)), y = 20 - (int)(k * 18);
    if (k < 0.8f && y > 6) u8g2_DrawStr(g, x, y, k < 0.35f ? "z" : "Z");
  }
}

// ---- Приёмник не подключён к набору: просит доступ у передатчика. Код — тот же, что покажет передатчик в окне
// «Додати приймач»: оператор сверяет их и нажимает «Дозволити». heard — передатчик слышен.
static void unpaired(u8g2_t *g, uint32_t t, unsigned code, bool heard) {
  char b[64];   // кириллица — по два байта на букву
  u8g2_SetFont(g, u8g2_font_7x13_t_cyrillic);
  centered(g, 11, tr("НЕ ПІДКЛЮЧЕНО"));
  u8g2_DrawHLine(g, 14, 15, 100);
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  int dots = (t / 400) % 4;   // точки бегут
  snprintf(b, sizeof(b), "%s%.*s", heard ? tr("прошу доступ") : tr("шукаю передавач"), dots, "...");
  u8g2_DrawUTF8(g, (128 - u8g2_GetUTF8Width(g, heard ? tr("прошу доступ...") : tr("шукаю передавач..."))) / 2, 28, b);
  if (heard) {   // код — крупно, в рамке, по которой бегут штрихи («обратите внимание»)
    u8g2_SetFont(g, u8g2_font_10x20_t_cyrillic);
    snprintf(b, sizeof(b), tr("КОД %04u"), code % 10000);
    int fw = u8g2_GetUTF8Width(g, b) + 12, fx = (128 - fw) / 2, fy = 32, fh = 20;   // рамка — по ширине надписи
    int per = 2 * (fw + fh), ph = (t / 45) % 6;
    for (int k = 0; k < per; k++) {
      if ((k + 6 - ph) % 6 >= 3) continue;
      int x, y;
      if (k < fw) x = fx + k, y = fy;
      else if (k < fw + fh) x = fx + fw - 1, y = fy + (k - fw);
      else if (k < 2 * fw + fh) x = fx + fw - 1 - (k - fw - fh), y = fy + fh - 1;
      else x = fx, y = fy + fh - 1 - (k - 2 * fw - fh);
      u8g2_DrawPixel(g, x, y);
    }
    centered(g, 49, b);
    u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
    centered(g, 63, tr("Приймачі - Додати"));
  } else {
    u8g2_SetClipWindow(g, 0, 31, 128, 53);
    bool flash = waves(g, t, 64, 36, 2, 6, 5, 4.6f);   // места по высоте мало: две дуги (третья вышла бы прямой чертой)
    tower(g, 64, 36, 52, flash ? 2 : 1);
    u8g2_SetMaxClipWindow(g);
    centered(g, 63, tr("увімкніть передавач"));
  }
}

// ---- Тишина. При входе: волны у динамика гаснут одна за другой (густая — редкая — нет), крест прочерчивается двумя
// штрихами, слово встаёт по букве. Дальше рамка «дышит» растром, крест изредка вздрагивает.
static void muted(u8g2_t *g, uint32_t t) {
  // рамка: постоянная тонкая и вокруг неё растровое «дыхание»
  u8g2_DrawRFrame(g, 6, 12, 116, 40, 6);
  int glow = 3 + (int)lroundf(3 * (0.5f - 0.5f * cosf(t / 620.0f)));
  shade(g, 3, 9, 122, 2, glow);
  shade(g, 3, 53, 122, 2, glow);
  shade(g, 3, 11, 2, 42, glow);
  shade(g, 123, 11, 2, 42, glow);
  // динамик
  u8g2_DrawBox(g, 20, 27, 6, 10);
  u8g2_DrawTriangle(g, 26, 27, 36, 18, 36, 46);
  u8g2_DrawTriangle(g, 26, 27, 36, 46, 26, 37);
  // волны гаснут
  for (int i = 0; i < 3; i++) {
    float k = phase(t, i * 110, 240);
    if (k >= 1) continue;
    ringArc(g, 36, 32, 6 + i * 5, -40, 40, k < 0.4f ? 1 : k < 0.7f ? 2 : 3, 1);
  }
  // крест: штрих за штрихом
  float a = easeOut(phase(t, 330, 170)), c = easeOut(phase(t, 480, 170));
  int jit = t > 1200 && (t % 2300) < 90 ? 1 : 0;
  if (a > 0) {
    int x1 = 42 + (int)lroundf(10 * a), y1 = 26 + (int)lroundf(12 * a);
    u8g2_DrawLine(g, 42 + jit, 26, x1 + jit, y1);
    u8g2_DrawLine(g, 43 + jit, 26, x1 + 1 + jit, y1);
  }
  if (c > 0) {
    int x1 = 52 - (int)lroundf(10 * c), y1 = 26 + (int)lroundf(12 * c);
    u8g2_DrawLine(g, 52 + jit, 26, x1 + jit, y1);
    u8g2_DrawLine(g, 53 + jit, 26, x1 + 1 + jit, y1);
  }
  // слово — по букве, с отскоком
  const char *w = tr("ТИША");
  u8g2_SetFont(g, u8g2_font_10x20_t_cyrillic);
  int x = 64, li = 0;
  u8g2_SetClipWindow(g, 58, 14, 120, 50);
  for (const char *ch = w; *ch; li++) {
    char one[5] = { 0 };
    int n = 1;
    while ((ch[n] & 0xC0) == 0x80) n++;
    memcpy(one, ch, n);
    float k = phase(t, 380 + li * 70, 320);
    if (k > 0) u8g2_DrawUTF8(g, x, 39 + (int)lroundf((1 - easeOutBack(k)) * 26), one);
    x += u8g2_GetUTF8Width(g, one);
    ch += n;
  }
  u8g2_SetMaxClipWindow(g);
}

// ---- Громкость: карточка поверх пригашенного главного экрана. k — насколько она раскрылась (0…1), vol — число
// делений (0…20, дробное: едет к новой громкости плавно), t — время. Слева динамик, волн у него столько, сколько
// громкости; крупное число «набегает»; внизу двадцать делений.
static void volumeCard(u8g2_t *g, uint32_t t, float vol, float k, int limit = 20) {
  if (k <= 0) return;
  dim(g, 0, 0, 128, 64, k > 0.5f ? 4 : 9);
  float e = easeOutBack(k);
  int hw = (int)lroundf(52 * e), hh = (int)lroundf(25 * e);
  if (hw < 4 || hh < 3) return;
  u8g2_SetClipWindow(g, 64 - hw - 1, 33 - hh - 1, 64 + hw + 1, 33 + hh + 1);
  u8g2_SetDrawColor(g, 0);
  u8g2_DrawBox(g, 64 - hw - 1, 33 - hh - 1, 2 * hw + 2, 2 * hh + 2);
  u8g2_SetDrawColor(g, 1);
  u8g2_DrawRFrame(g, 64 - hw, 33 - hh, 2 * hw, 2 * hh, 6);
  // динамик и волны
  u8g2_DrawBox(g, 19, 25, 5, 8);
  u8g2_DrawTriangle(g, 24, 25, 32, 18, 32, 40);
  u8g2_DrawTriangle(g, 24, 25, 32, 40, 24, 33);
  int waves = vol < 0.5f ? 0 : vol < 7 ? 1 : vol < 14 ? 2 : 3;
  for (int i = 0; i < waves; i++) ringArc(g, 32, 29, 5 + i * 4 + sinf(t / 180.0f + i) * 0.6f, -42, 42, 1, 1);
  if (!waves) {   // ноль — крестик
    u8g2_DrawLine(g, 36, 25, 42, 33);
    u8g2_DrawLine(g, 36, 33, 42, 25);
  }
  // число
  char b[8];
  snprintf(b, sizeof(b), "%d", (int)lroundf(vol * 5));
  u8g2_SetFont(g, u8g2_font_logisoso24_tn);
  int nw = u8g2_GetStrWidth(g, b);
  u8g2_DrawStr(g, 98 - nw, 41, b);
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  u8g2_DrawStr(g, 101, 41, "%");
  // деления
  for (int i = 0; i < 20; i++) {
    int x = 24 + i * 4;   // двадцать делений подряд, без разрыва (владелец 07.10: «в шкале громкости посредине черный провал»)
    float f = vol - i;
    if (f >= 1) u8g2_DrawBox(g, x, 47, 3, 6);
    else if (f > 0) u8g2_DrawBox(g, x, 53 - (int)lroundf(6 * f), 3, (int)lroundf(6 * f) + 0);
    else if (i < limit) u8g2_DrawPixel(g, x + 1, 52);
  }
  if (limit < 20) {   // предел громкости: «стопор» за последним доступным делением, дальше шкалы нет
    int x = 24 + limit * 4;
    u8g2_DrawVLine(g, x, 45, 9);
    u8g2_DrawHLine(g, x - 1, 45, 3);
  }
  u8g2_SetMaxClipWindow(g);
}

// ---- «Це я» (с передатчика нажали «Показати себе»): от середины расходятся кольца, слово и имя — в «окне»,
// раз в 0,7 с весь экран коротко вспыхивает негативом. Найти приёмник глазами среди других должно быть легко.
static void identify(u8g2_t *g, uint32_t t, const char *name) {
  for (int i = 0; i < 4; i++) {
    float p = fract(t / 1400.0f + i / 4.0f), r = 6 + 70 * p;
    ringArc(g, 64, 32, r, 0, 359, p < 0.45f ? 1 : p < 0.75f ? 2 : 3, p < 0.25f ? 2 : 1);
  }
  u8g2_SetDrawColor(g, 0);
  u8g2_DrawRBox(g, 24, 6, 80, 22, 5);
  u8g2_SetDrawColor(g, 1);
  u8g2_DrawRFrame(g, 24, 6, 80, 22, 5);
  u8g2_SetFont(g, u8g2_font_10x20_t_cyrillic);
  centered(g, 23, tr("ЦЕ Я"));
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  int nw = u8g2_GetUTF8Width(g, name);
  if (nw > 122) nw = 122;
  u8g2_SetDrawColor(g, 0);
  u8g2_DrawBox(g, (128 - nw) / 2 - 3, 38, nw + 6, 14);
  u8g2_SetDrawColor(g, 1);
  if (u8g2_GetUTF8Width(g, name) <= 122) centered(g, 49, name);
  else ticker(g, 3, 125, 49, name, t);
  if ((t % 700) < 90) {   // вспышка: негатив
    u8g2_SetDrawColor(g, 2);
    u8g2_DrawBox(g, 0, 0, 128, 64);
    u8g2_SetDrawColor(g, 1);
  }
}

// ---- Верхняя строка главного экрана: уровень сигнала (четыре столбика, растут и опадают плавно — bars дробное
// 0…4), имя приёмника (длинное — бегущей строкой), громкость. Под строкой черта: когда в эфире звук, по ней бежит
// яркая засечка, в тишине черта пунктирная.
static void statusBar(u8g2_t *g, uint32_t t, float bars, const char *name, int volPct, bool sound) {
  for (int i = 0; i < 4; i++) {
    int hmax = 3 + i * 2;
    float f = bars - i;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    int h = (int)lroundf(hmax * f);
    if (h > 0) u8g2_DrawBox(g, i * 4, 10 - h, 3, h);
    else u8g2_DrawHLine(g, i * 4, 9, 3);
  }
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  char b[8];
  snprintf(b, sizeof(b), "%d", volPct);
  int vw = volPct < 0 ? -4 : u8g2_GetStrWidth(g, b);   // volPct < 0 — громкость на экране и так крупно, в строке её нет
  ticker(g, 20, 124 - vw, 10, name, t, 0, 12);
  if (volPct >= 0) u8g2_DrawStr(g, 128 - vw, 10, b);
  if (sound) {
    for (int x = 0; x < 128; x += 2) u8g2_DrawPixel(g, x, 12);
    int bx = (int)(fract(t / 2200.0f) * 148) - 20;
    for (int x = bx; x < bx + 18; x++)
      if (x >= 0 && x < 128) u8g2_DrawPixel(g, x, 12);
  } else
    for (int x = 1; x < 128; x += 4) u8g2_DrawPixel(g, x, 12);
}

// ---- Спектр на главном экране: 14 рисок по 7 точек и над каждой — отметка пика, которая держится и плавно опускается
// (владелец 07.10: «добавить показания пиков в анализаторе, наподобие того, что есть на главной странице в передатчике
// на пик-метре»; «не вижу плавности анализатора спектра на приемнике и отсутствуют плавные значки пиков»).
// lv и pk — высоты 0…1 (уже сглаженные), область — нижние 48 точек экрана.
static void spectrum(u8g2_t *g, const float *lv, const float *pk, int n, uint32_t t = 0) {
  bool quiet = true;
  for (int b = 0; b < n; b++)
    if (lv[b] * 46 >= 2 || pk[b] * 46 >= 3) quiet = false;
  if (quiet) {   // тишина: по низу идёт пологая волна точек — экран «дышит», а не замирает
    for (int x = 2; x < 127; x += 3) u8g2_DrawPixel(g, x, 60 + (int)lroundf(1.6f * sinf(x / 11.0f + t / 300.0f)));
    return;
  }
  for (int b = 0; b < n; b++) {
    int x = b * 9 + 1, h = (int)(lv[b] * 46 + 0.5f), hp = (int)(pk[b] * 46 + 0.5f);
    if (h < 2) u8g2_DrawBox(g, x + 2, 61, 2, 2);        // тихая полоса — точка
    else u8g2_DrawRBox(g, x, 64 - h, 7, h, h >= 6 ? 2 : 0);
    if (hp >= 3 && hp > h + 1) u8g2_DrawBox(g, x, 64 - hp - 2, 7, 2);   // отметка пика — чёрточка над риской
  }
}

// ---- Стрелочный индикатор уровня (вид главного экрана «стрілки»). Прибор в рамке: шкала-дуга с делениями, её правый
// конец («перегруз») жирный, стрелка в две точки толщиной качается вокруг оси под нижним краем, вдоль шкалы ползёт
// отметка пика, в углу — лампочка перегруза. lv, pk — уровень и пик 0…1 (уже сглаженные: ход стрелки задаёт экран).
static void vuMeter(u8g2_t *g, int x, int y, int w, int h, float lv, float pk, const char *label, uint32_t t) {
  lv = clamp01(lv);
  pk = clamp01(pk);
  u8g2_DrawRFrame(g, x, y, w, h, 4);
  u8g2_SetClipWindow(g, x + 1, y + 1, x + w - 1, y + h - 1);
  const float D = 0.017453293f, cx = x + (w - 1) / 2.0f;
  float amax = w > 80 ? 40 : 36;                       // половина раствора шкалы (у широкого прибора шкала положе, и при большем
                                                       // растворе стрелка у краёв почти вся пряталась за нижней панелью)
  float R = (w / 2.0f - 7) / sinf(amax * D), py = y + 8 + R;   // ось — под нижним краем
  ringArc(g, cx, py, R, 90 - amax, 90 + amax, 1, 1);
  ringArc(g, cx, py, R - 1, 90 - amax, 90 - amax * 0.5f, 1, 2);   // перегруз: последняя четверть шкалы — в три точки
  for (int i = 0; i <= 12; i++) {                      // деления: каждое третье длиннее
    float a = (90 + amax - 2 * amax * i / 12.0f) * D, cs = cosf(a), sn = sinf(a);
    int len = i % 3 == 0 ? 5 : 2;
    u8g2_DrawLine(g, (int)lroundf(cx + (R + 2) * cs), (int)lroundf(py - (R + 2) * sn), (int)lroundf(cx + (R + 1 + len) * cs), (int)lroundf(py - (R + 1 + len) * sn));
  }
  {   // отметка пика — точка под шкалой
    float a = (90 + amax - 2 * amax * pk) * D;
    if (pk > 0.03f) u8g2_DrawDisc(g, (int)lroundf(cx + (R - 5) * cosf(a)), (int)lroundf(py - (R - 5) * sinf(a)), 1, U8G2_DRAW_ALL);
  }
  {   // стрелка: выходит из-за нижней панели прибора (ось спрятана под ней, как у настоящего) и идёт до шкалы
    float a = (90 + amax - 2 * amax * lv) * D, cs = cosf(a), sn = sinf(a);
    float r0 = (py - (y + h - 10)) / sn, r1 = R - 2;
    if (r0 < 0) r0 = 0;
    int x0 = (int)lroundf(cx + r0 * cs), y0 = (int)lroundf(py - r0 * sn), x1 = (int)lroundf(cx + r1 * cs), y1 = (int)lroundf(py - r1 * sn);
    u8g2_DrawLine(g, x0, y0, x1, y1);
    u8g2_DrawLine(g, x0 + 1, y0, x1 + 1, y1);
  }
  // нижняя панель: светлая полоса с подписью, справа в ней «глазок» перегруза — загорается точкой
  u8g2_DrawBox(g, x + 2, y + h - 10, w - 4, 8);
  u8g2_SetDrawColor(g, 0);
  u8g2_SetFont(g, u8g2_font_5x8_t_cyrillic);
  if (label && *label) u8g2_DrawUTF8(g, x + 5, y + h - 3, label);
  u8g2_DrawDisc(g, x + w - 8, y + h - 6, 2, U8G2_DRAW_ALL);
  u8g2_SetDrawColor(g, 1);
  if (pk > 0.9f && (t % 240) < 160) u8g2_DrawBox(g, x + w - 9, y + h - 7, 3, 3);
  u8g2_SetMaxClipWindow(g);
}
// один прибор во всю ширину (звук один) или два — левый и правый канал
static void needles(u8g2_t *g, uint32_t t, float l, float r, float pl, float pr, bool stereo) {
  if (stereo) {
    vuMeter(g, 0, 15, 63, 49, l, pl, tr("Л"), t);
    vuMeter(g, 65, 15, 63, 49, r, pr, tr("П"), t);
  } else vuMeter(g, 0, 15, 128, 49, l > r ? l : r, pl > pr ? pl : pr, tr("РІВЕНЬ"), t);
}

// ---- Вид главного экрана «гучність»: громкость крупным числом — её видно издалека и без очков. Слева динамик (волн
// столько, сколько громкости; они «дышат» вместе со звуком), под числом шкала из двадцати делений со «стопором»
// предела, у самого низа — полоска уровня звука в эфире (экран не замирает). vol — деления 0…20 (дробное: число
// «набегает»), limit — предел громкости, lv — уровень звука 0…1.
static void bigVolume(u8g2_t *g, uint32_t t, float vol, int limit, float lv) {
  const int sx = 5, sy = 24;
  u8g2_DrawBox(g, sx, sy + 6, 6, 10);
  u8g2_DrawTriangle(g, sx + 6, sy + 6, sx + 16, sy - 3, sx + 16, sy + 25);
  u8g2_DrawTriangle(g, sx + 6, sy + 6, sx + 16, sy + 25, sx + 6, sy + 16);
  int wv = vol < 0.5f ? 0 : vol < 7 ? 1 : vol < 14 ? 2 : 3;
  for (int i = 0; i < wv; i++) ringArc(g, sx + 16, sy + 11, 6 + i * 5 + lv * 1.6f * sinf(t / 150.0f + i * 0.9f), -40, 40, 1, i == 0 ? 2 : 1);
  if (!wv) {
    for (int w = 0; w < 2; w++) {
      u8g2_DrawLine(g, sx + 21 + w, sy + 5, sx + 31 + w, sy + 17);
      u8g2_DrawLine(g, sx + 31 + w, sy + 5, sx + 21 + w, sy + 17);
    }
  }
  char b[8];
  snprintf(b, sizeof(b), "%d", (int)lroundf(vol * 5));
  u8g2_SetFont(g, u8g2_font_logisoso32_tn);
  int nw = u8g2_GetStrWidth(g, b);
  u8g2_DrawStr(g, 108 - nw, 50, b);
  u8g2_SetFont(g, u8g2_font_10x20_t_cyrillic);
  u8g2_DrawStr(g, 112, 50, "%");
  for (int i = 0; i < 20; i++) {   // шкала
    int x = 4 + i * 6;
    float f = vol - i;
    if (f >= 1) u8g2_DrawBox(g, x, 54, 5, 5);
    else if (f > 0) {
      int w = (int)lroundf(5 * f);
      if (w > 0) u8g2_DrawBox(g, x, 54, w, 5);
    } else if (i < limit) u8g2_DrawBox(g, x + 2, 56, 1, 1);
  }
  if (limit < 20) {
    int x = 3 + limit * 6;
    u8g2_DrawVLine(g, x, 52, 8);
    u8g2_DrawHLine(g, x - 1, 52, 3);
  }
  int lw = (int)lroundf(clamp01(lv) * 120);
  for (int x = 4; x < 124; x += 4) u8g2_DrawPixel(g, x, 63);
  if (lw > 0) u8g2_DrawBox(g, 4, 62, lw, 2);
}

// ---- Экран «Зв'язок»: как приёмник слышит передатчик сейчас и как слышал последнюю минуту.
struct LinkInfo {
  bool signal = false;
  int rssi = 0;                 // дБм
  float bars = 0;               // столбики уровня 0…4 (плавные)
  float lossPct = 0, reserveMs = 0;
  const int8_t *hr = nullptr;   // история уровня сигнала по секундам (−128 — передатчика не было), кольцо из n
  const uint8_t *hl = nullptr;  // история потерь, десятые доли процента
  uint32_t pos = 0;             // сколько замеров записано всего
  int n = 0;
  float frac = 0;               // сколько прошло с последнего замера, 0…1 с: график едет плавно
};
static void linkScreen(u8g2_t *g, uint32_t t, const LinkInfo &li) {
  char b[24];
  u8g2_SetFont(g, u8g2_font_7x13_t_cyrillic);
  u8g2_DrawUTF8(g, 1, 10, tr("ЗВ'ЯЗОК"));
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  const char *word = !li.signal ? tr("немає") : li.rssi > -55 ? tr("відмінно") : li.rssi > -67 ? tr("добре") : li.rssi > -78 ? tr("помірно") : tr("слабо");
  u8g2_DrawUTF8(g, 127 - u8g2_GetUTF8Width(g, word), 10, word);
  for (int i = 0; i < 4; i++) {   // крупные столбики уровня
    int hmax = 6 + i * 5, x = 1 + i * 7;
    float f = clamp01(li.bars - i);
    int h = (int)lroundf(hmax * f);
    if (h > 0) u8g2_DrawBox(g, x, 35 - h, 5, h);
    else u8g2_DrawHLine(g, x, 34, 5);
  }
  u8g2_SetFont(g, u8g2_font_10x20_t_cyrillic);
  if (li.signal) snprintf(b, sizeof(b), "%d", li.rssi);
  else snprintf(b, sizeof(b), "--");
  u8g2_DrawStr(g, 32, 32, b);
  int nx = 32 + u8g2_GetStrWidth(g, b) + 2;
  u8g2_SetFont(g, u8g2_font_5x8_t_cyrillic);
  u8g2_DrawUTF8(g, nx, 32, tr("дБм"));
  snprintf(b, sizeof(b), tr("втр %.1f%%"), li.lossPct);
  u8g2_DrawUTF8(g, 127 - u8g2_GetUTF8Width(g, b), 23, b);
  snprintf(b, sizeof(b), tr("зап %.0fмс"), li.reserveMs);
  u8g2_DrawUTF8(g, 127 - u8g2_GetUTF8Width(g, b), 33, b);
  // график за минуту: новое справа; под кривой — растровая заливка, внизу полоска потерь
  const int gx1 = 124, gy0 = 39, gy1 = 58;
  u8g2_SetClipWindow(g, 3, gy0 - 1, 126, 64);
  for (int x = 4; x <= gx1; x += 4) u8g2_DrawPixel(g, x, 60);   // «ноль» графика
  int px = -1, py = 0, lastX = -1, lastY = 0;
  int shown = li.pos < (uint32_t)li.n ? (int)li.pos : li.n;
  for (int k = shown - 1; k >= 0; k--) {   // k — сколько секунд назад
    uint32_t idx = (li.pos - 1 - k) % li.n;
    int x = gx1 - (int)lroundf((k + li.frac) * 2.0f);
    if (x < 2) continue;
    int v = li.hr[idx];
    if (v == -128) {   // передатчика не было слышно — разрыв
      u8g2_DrawPixel(g, x, 62);
      px = -1;
      continue;
    }
    float kk = (v + 95) / 60.0f;
    int y = gy1 - (int)lroundf(clamp01(kk) * (gy1 - gy0));
    if (px >= 0) u8g2_DrawLine(g, px, py, x, y);
    else u8g2_DrawPixel(g, x, y);
    for (int xx = (px >= 0 ? px : x); xx <= x; xx++) {   // заливка под кривой
      int yy = px >= 0 && x > px ? py + (y - py) * (xx - px) / (x - px) : y;
      if (gy1 + 1 > yy + 1) shade(g, xx, yy + 1, 1, gy1 + 1 - yy, 4);
    }
    int ls = li.hl[idx];
    if (ls) u8g2_DrawBox(g, x - 1, ls < 10 ? 63 : ls < 50 ? 62 : 61, 2, ls < 10 ? 1 : ls < 50 ? 2 : 3);
    px = x;
    py = y;
    lastX = x;
    lastY = y;
  }
  if (lastX >= 0 && px == lastX) {   // свежая точка: «маячок»
    float p = fract(t / 1000.0f);
    u8g2_DrawDisc(g, lastX, lastY, 1, U8G2_DRAW_ALL);
    if (p < 0.6f) ringArc(g, lastX, lastY, 2 + p * 6, 0, 359, p < 0.3f ? 1 : 2, 1);
  }
  u8g2_SetMaxClipWindow(g);
}

// ---- Проверка наушников: тон по очереди в левое и правое ухо. Наушники нарисованы крупно; чашка, в которой сейчас
// тон, закрашена и «звучит» — от неё расходятся дуги, её буква стоит в светлой плашке, внизу подпись. side: 0 —
// пауза, 1 — левое, 2 — правое; stereo — выход «2 канали» (при «протифазі» канал один, тон идёт в оба уха сразу).
static void earTest(u8g2_t *g, uint32_t t, int side, bool stereo) {
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  centered(g, 10, tr("ПЕРЕВІРКА НАВУШНИКІВ"));
  const int cx = 64, cy = 33;
  bool l = side && (!stereo || side == 1), r = side && (!stereo || side == 2);
  ringArc(g, cx, cy, 21, 10, 170, 1, 2);   // оголовье
  for (int k = 0; k < 2; k++) {
    bool on = k ? r : l;
    int bx = k ? 81 : 38, grow = on && (t % 300) < 150 ? 1 : 0;
    if (on) u8g2_DrawRBox(g, bx - grow, cy - 4 - grow, 9 + 2 * grow, 18 + 2 * grow, 3);
    else u8g2_DrawRFrame(g, bx, cy - 4, 9, 18, 3);
    if (on)
      for (int i = 0; i < 3; i++) {   // дуги от чашки наружу, свет бежит от ближней к дальней
        float u = fract(t / 700.0f - i * 0.22f);
        int dens = u < 0.45f ? 1 : u < 0.72f ? 2 : 3, thick = u < 0.25f ? 2 : 1;
        if (k) ringArc(g, 88, cy + 5, 7 + i * 5, -38, 38, dens, thick);
        else ringArc(g, 39, cy + 5, 7 + i * 5, 142, 218, dens, thick);
      }
  }
  u8g2_SetFont(g, u8g2_font_10x20_t_cyrillic);
  for (int k = 0; k < 2; k++) {   // буквы каналов по краям
    const char *ch = k ? tr("П") : tr("Л");
    bool on = k ? r : l;
    int x = k ? 113 : 5;
    if (on) {
      u8g2_DrawRBox(g, x - 3, cy - 6, 16, 20, 3);
      u8g2_SetDrawColor(g, 0);
    }
    u8g2_DrawUTF8(g, x, cy + 10, ch);
    u8g2_SetDrawColor(g, 1);
  }
  u8g2_SetFont(g, u8g2_font_7x13_t_cyrillic);
  centered(g, 62, !side ? ". . ." : !stereo ? tr("обидва вуха") : side == 1 ? tr("ліве вухо") : tr("праве вухо"));
}

// ---- Меню: строки с значками, выбранная — под «плашкой», которая плавно переезжает от строки к строке, список
// плавно прокручивается. sel — выбранная строка (дробная: едет к цели), scroll — сдвиг списка в точках (тоже едет),
// edit — правка значения: плашка становится рамкой, у значения «дышат» стрелки.
struct MenuRow {
  const char *name;
  const char *value;
  uint8_t icon;
};
static void menuIcon(u8g2_t *g, int id, int x, int y) {   // значок 9×9, (x, y) — левый верхний угол
  switch (id) {
    case 0:   // набор: два звена
      u8g2_DrawRFrame(g, x, y + 2, 6, 5, 2);
      u8g2_DrawRFrame(g, x + 3, y + 2, 6, 5, 2);
      break;
    case 1:   // канал: волна
      for (int i = 0; i < 9; i++) u8g2_DrawPixel(g, x + i, y + 4 + (int)lroundf(3 * sinf(i * 0.8f)));
      break;
    case 2:   // запас: стакан с уровнем
      u8g2_DrawFrame(g, x + 1, y, 7, 9);
      u8g2_DrawBox(g, x + 3, y + 4, 3, 3);
      break;
    case 3:   // выход звука: наушники
      ringArc(g, x + 4, y + 5, 4, 0, 180, 1, 1);
      u8g2_DrawBox(g, x, y + 5, 2, 4);
      u8g2_DrawBox(g, x + 7, y + 5, 2, 4);
      break;
    case 4:   // выводы: две точки и стрелки
      u8g2_DrawDisc(g, x + 1, y + 4, 1, U8G2_DRAW_ALL);
      u8g2_DrawDisc(g, x + 7, y + 4, 1, U8G2_DRAW_ALL);
      u8g2_DrawHLine(g, x + 3, y + 4, 3);
      break;
    case 5:   // экран
      u8g2_DrawRFrame(g, x, y + 1, 9, 6, 1);
      u8g2_DrawHLine(g, x + 2, y + 8, 5);
      break;
    case 6:   // забыть набор: крест
      u8g2_DrawLine(g, x + 1, y + 1, x + 7, y + 7);
      u8g2_DrawLine(g, x + 7, y + 1, x + 1, y + 7);
      break;
    case 7:   // о приборе: «i» в круге
      u8g2_DrawCircle(g, x + 4, y + 4, 4, U8G2_DRAW_ALL);
      u8g2_DrawPixel(g, x + 4, y + 2);
      u8g2_DrawVLine(g, x + 4, y + 4, 3);
      break;
    case 9:   // вид главного экрана: три столбика
      u8g2_DrawBox(g, x, y + 5, 2, 4);
      u8g2_DrawBox(g, x + 3, y + 1, 2, 8);
      u8g2_DrawBox(g, x + 6, y + 3, 2, 6);
      break;
    case 10:  // светодиод: огонёк с лучами
      u8g2_DrawDisc(g, x + 4, y + 4, 2, U8G2_DRAW_ALL);
      u8g2_DrawPixel(g, x + 4, y);
      u8g2_DrawPixel(g, x + 4, y + 8);
      u8g2_DrawPixel(g, x, y + 4);
      u8g2_DrawPixel(g, x + 8, y + 4);
      u8g2_DrawPixel(g, x + 1, y + 1);
      u8g2_DrawPixel(g, x + 7, y + 1);
      u8g2_DrawPixel(g, x + 1, y + 7);
      u8g2_DrawPixel(g, x + 7, y + 7);
      break;
    case 11:  // язык: глобус
      u8g2_DrawCircle(g, x + 4, y + 4, 4, U8G2_DRAW_ALL);
      u8g2_DrawVLine(g, x + 4, y, 9);
      u8g2_DrawHLine(g, x, y + 4, 9);
      break;
    case 12:  // чёткость речи: «звёздочка» — ясный звук
      u8g2_DrawVLine(g, x + 4, y, 9);
      u8g2_DrawHLine(g, x, y + 4, 9);
      u8g2_DrawBox(g, x + 3, y + 3, 3, 3);
      break;
    case 13:  // баланс: коромысло на опоре
      u8g2_DrawHLine(g, x, y + 3, 9);
      u8g2_DrawTriangle(g, x + 4, y + 4, x + 1, y + 9, x + 8, y + 9);
      u8g2_DrawBox(g, x, y + 1, 2, 2);
      u8g2_DrawBox(g, x + 7, y + 1, 2, 2);
      break;
    case 14:  // предел громкости: столбики под «потолком»
      u8g2_DrawHLine(g, x, y + 1, 9);
      u8g2_DrawBox(g, x, y + 6, 2, 3);
      u8g2_DrawBox(g, x + 3, y + 4, 2, 5);
      u8g2_DrawBox(g, x + 6, y + 3, 2, 6);
      break;
    case 15:  // проверка наушников: одна чашка звучит, другая нет
      ringArc(g, x + 4, y + 5, 4, 0, 180, 1, 1);
      u8g2_DrawBox(g, x, y + 5, 3, 4);
      u8g2_DrawFrame(g, x + 6, y + 5, 3, 4);
      break;
    case 16:  // связь: ломаная графика
      u8g2_DrawLine(g, x, y + 7, x + 2, y + 4);
      u8g2_DrawLine(g, x + 2, y + 4, x + 5, y + 6);
      u8g2_DrawLine(g, x + 5, y + 6, x + 8, y + 1);
      break;
    default:  // назад: стрелка
      u8g2_DrawHLine(g, x + 1, y + 4, 7);
      u8g2_DrawLine(g, x + 1, y + 4, x + 4, y + 1);
      u8g2_DrawLine(g, x + 1, y + 4, x + 4, y + 7);
  }
}
static void menuList(u8g2_t *g, uint32_t t, const MenuRow *rows, int n, float sel, float scroll, bool edit) {
  u8g2_SetFont(g, u8g2_font_6x13_t_cyrillic);
  for (int i = 0; i < n; i++) {
    int y = 13 + i * 16 - (int)lroundf(scroll);
    if (y < -4 || y > 78) continue;
    menuIcon(g, rows[i].icon, 3, y - 10);
    u8g2_DrawUTF8(g, 16, y, rows[i].name);
    int vw = u8g2_GetUTF8Width(g, rows[i].value);
    bool cur = fabsf(sel - i) < 0.5f;
    if (edit && cur && vw) {   // правка: стрелки по сторонам значения ходят туда-сюда
      int a = (int)lroundf(1.5f * sinf(t / 170.0f));
      u8g2_DrawUTF8(g, 114 - vw, y, rows[i].value);
      u8g2_DrawTriangle(g, 108 - vw - a, y - 4, 111 - vw - a, y - 8, 111 - vw - a, y);
      u8g2_DrawTriangle(g, 120 + a, y - 4, 117 + a, y - 8, 117 + a, y);
    } else u8g2_DrawUTF8(g, 121 - vw, y, rows[i].value);
  }
  // плашка выбора: негатив поверх строки (в правке — рамка)
  int hy = 1 + (int)lroundf(sel * 16 - scroll);
  if (edit) {
    u8g2_DrawRFrame(g, 0, hy, 124, 16, 3);
  } else {
    u8g2_SetDrawColor(g, 2);
    u8g2_DrawRBox(g, 0, hy, 124, 16, 3);
    u8g2_SetDrawColor(g, 1);
  }
  // полоса прокрутки
  int th = 64 * 4 / n, ty = (int)lroundf(scroll * 64 / (n * 16));
  for (int y = 0; y < 64; y += 3) u8g2_DrawPixel(g, 126, y);
  u8g2_DrawBox(g, 125, ty, 3, th);
}

}   // namespace rxs
