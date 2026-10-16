// Кадр цветного экрана приёмника (с 3.03; с 3.06 — целиком во внешней памяти). Общий для прибора (rxlcd.h) и проверки
// на компьютере (tools/simrxc).
//
// Как было в 3.03–3.05 и почему переделано. Кадр 320×170 в 16-битном цвете — 106 КБ; во внутренней памяти приёмника
// его не уместить (свободно ~56 КБ), поэтому сцена рисовалась полосами по 10 строк в маленькие полосы во внутренней
// памяти — и весь код экрана прогонялся заново для каждой из 17 полос. Код лежит во флеше, а кэш команд (16 КБ) общий
// со звуком на соседнем ядре: замер на приборе 15.10 — неподвижное меню ~22 мс на кадр, главный экран со спектром
// 30–40 мс (20–30 кадров/с), тракт звука на втором ядре замедлился на ~10 %, а на 884A94 во время обновления по радио
// задача экрана не отдавала первое ядро, и сторож задач перезапускал плату (владелец 15.10: «отрисовка на цветном
// дисплее очень тормозит», «во время обновления посыпалось изображение на цветном дисплее и перезагрузился»).
//
// Теперь сцена рисуется ОДИН раз за кадр — в кадр во внешней памяти (PSRAM, 106 КБ; её у платы 8 МБ). Потом кадр
// сравнивается с тем, что уже на экране, по отпечаткам (32 бита) плиток TW×SH, и изменившийся кусок каждой полосы
// копируется в одну из двух маленьких полос во внутренней памяти и уходит на экран по DMA (пока одна уходит, в другую
// копируется следующий кусок). Неподвижный экран не шлёт ничего.
//
// Переход между экранами: прежний экран рисуется ещё раз (он «чистый» — см. rxcolor.h) — со сдвигом в тот же кадр
// (сдвиги) или во второй кадр (растворение, круг, шторка), и кадры смешиваются по точкам — как rxs::blend у OLED.
#pragma once
#include <stdint.h>
#include <string.h>
#include "rxcolor.h"
#ifdef ARDUINO
#include "esp_cpu.h"
#define RXC_CYC() esp_cpu_get_cycle_count()
#else
#define RXC_CYC() 0u
#endif

#ifdef ARDUINO
#pragma GCC push_options
#pragma GCC optimize("O2")   // с 3.05: см. rxcolor.h
#endif
namespace rxc {
enum { TR_DISSOLVE = 0, TR_LEFT, TR_RIGHT, TR_UP, TR_DOWN, TR_IRIS, TR_WIPE };   // как в rxfx.h
constexpr int SH = 10;                          // строк в полосе (сравнение и отправка)
constexpr int STRIPS = (H + SH - 1) / SH;       // 17
constexpr int TW = 32, TILES = W / TW;          // плитки для сравнения: 32×SH

// Куда идут изменившиеся куски кадра.
struct Out {
  virtual void wait(int buf) = 0;   // дождаться, пока кусок из полосы buf ушёл на экран (у прибора — DMA)
  // px — w×h точек подряд, обычный порядок байтов; полосу buf до wait(buf) не трогать
  virtual void send(int buf, int x, int y, int w, int h, uint16_t *px) = 0;
  virtual ~Out() {}
};

struct Strips {
  uint16_t *fb = nullptr;                  // кадр целиком (внешняя память)
  uint16_t *fb2 = nullptr;                 // прежний экран для перехода (нет памяти — переход просто сменяет экран)
  uint16_t *a[2] = { nullptr, nullptr };   // полосы во внутренней памяти: через них кусок кадра уходит по DMA
  uint32_t hash[STRIPS][TILES];
  bool valid = false;                      // отпечатки соответствуют экрану
  uint32_t sentPx = 0, spans = 0;          // сколько точек и кусков ушло (для отчёта; читающий обнуляет)
  uint32_t cRender = 0, cHash = 0, cSend = 0, cWait = 0;   // такты за кадр: рисование, отпечатки, отправка, ожидание DMA
  uint32_t fullDraws = 0;                  // сколько раз кадр рисовался целиком (для отчёта; читающий обнуляет)
  bool stale[STRIPS][TILES];               // плитку меняли мимо отпечатков (дорисовка рисок) — при сравнении слать заново
  uint32_t lastKey = 0;                    // «ключ» сцены, по которой кадр нарисован целиком (rxcolor.h, sceneKey)
  bool anOn = false;                       // на экране анализатор, и an — то, что нарисовано
  bool trOn = false;                       // идёт переход: во втором кадре — снимок прежнего экрана (с 3.09)
  float trK = 0;                           // где переход был в прошлом кадре (меньше — начался новый)
  AnState an;
  G g;

  void begin(uint16_t *frame0, uint16_t *frame1, uint16_t *a0, uint16_t *a1) {
    fb = frame0;
    fb2 = frame1;
    a[0] = a0;
    a[1] = a1;
    valid = false;
  }
  void invalidate() {   // на экране неизвестно что (включили, разбудили): следующий кадр — целиком
    valid = false;
    lastKey = 0;
    anOn = false;
  }
  static uint32_t tileHash(const uint16_t *p, int h, uint32_t seed) {   // p — левый верхний угол плитки в кадре
    uint32_t v = seed * 0x9E3779B1u + 0x7F4A7C15u;
    for (int r = 0; r < h; r++) {
      const uint32_t *q = (const uint32_t *)(p + r * W);
      for (int i = 0; i < TW / 2; i++) {
        v ^= q[i];
        v *= 0x01000193u;
        v ^= v >> 15;
      }
    }
    return v;
  }
  // Сравнить по отпечаткам полосы, задевающие строки y0…y1 и столбцы x0…x1, и послать изменившиеся куски. С 3.09
  // кусков в полосе может быть несколько: две и больше неизменных плиток подряд (640 точек) дороже лишней посылки
  // (~500 точек шины) — тогда кусок кончается (рамка «тиши» по краю экрана уходила почти целыми полосами).
  void hashSend(Out &o, int y0, int y1, int x0, int x1) {
    int s0 = y0 / SH, s1 = (y1 - 1) / SH, tA = x0 / TW, tB = (x1 - 1) / TW;
    if (s0 < 0) s0 = 0;
    if (s1 >= STRIPS) s1 = STRIPS - 1;
    if (tA < 0) tA = 0;
    if (tB >= TILES) tB = TILES - 1;
    for (int st = s0; st <= s1; st++) {
      const int y = st * SH, h = y + SH > H ? H - y : SH;
      uint32_t c1 = RXC_CYC();
      const uint16_t *row0 = fb + y * W;
      int t0 = -1, t1 = -1;
      for (int t = tA; t <= tB; t++) {
        uint32_t hv = tileHash(row0 + t * TW, h, (uint32_t)(st * TILES + t));
        if (!valid || stale[st][t] || hv != hash[st][t]) {
          if (t0 >= 0 && t - t1 > 2) {   // прежний кусок кончился
            cHash += RXC_CYC() - c1;
            sendRect(o, t0 * TW, y, (t1 + 1) * TW - t0 * TW, h);
            c1 = RXC_CYC();
            t0 = -1;
          }
          if (t0 < 0) t0 = t;
          t1 = t;
          hash[st][t] = hv;
          stale[st][t] = false;
        }
      }
      cHash += RXC_CYC() - c1;
      if (t0 >= 0) sendRect(o, t0 * TW, y, (t1 + 1) * TW - t0 * TW, h);
    }
  }
  // Послать прямоугольник кадра как есть (через полосы во внутренней памяти, по SH строк).
  void sendRect(Out &o, int x, int y, int w, int h) {
    if (x < 0) w += x, x = 0;
    if (y < 0) h += y, y = 0;
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    int rowsPer = (W * SH) / w;
    for (int yy = y; yy < y + h; yy += rowsPer) {
      int hh = y + h - yy < rowsPer ? y + h - yy : rowsPer;
      uint32_t c2 = RXC_CYC();
      o.wait(nextBuf);
      uint32_t c3 = RXC_CYC();
      cWait += c3 - c2;
      uint16_t *buf = a[nextBuf];
      for (int r = 0; r < hh; r++) memcpy(buf + r * w, fb + (yy + r) * W + x, w * 2);
      o.send(nextBuf, x, yy, w, hh, buf);
      cSend += RXC_CYC() - c3;
      sentPx += w * hh;
      spans++;
      nextBuf ^= 1;
    }
  }
  void markStale(const m2::Rect &r) {   // плитки под прямоугольником изменены мимо отпечатков
    for (int st = r.y / SH; st <= (r.y + r.h - 1) / SH && st < STRIPS; st++)
      for (int t = r.x / TW; t <= (r.x + r.w - 1) / TW && t < TILES; t++)
        if (st >= 0 && t >= 0) stale[st][t] = true;
  }
  // Кадр по сцене (с 3.07): целиком — только когда сменился «ключ» сцены (или экран движется весь, или переход);
  // иначе — дорисовка рисок анализатора и областей, которые движутся сами (rxcolor.h: sceneKey, sceneDyn).
  void sceneFrame(Out &o, const Scene &cur, const Scene *old, int kind, float k) {
    cRender = cHash = cSend = cWait = 0;
    const bool mainAn = cur.sid == SC_MAIN && cur.view == 0;
    uint32_t key = sceneKey(cur);
    if (old && kind >= 0) {   // переход — кадр целиком
      uint32_t c0 = RXC_CYC();
      float e = k <= 0 ? 0 : k >= 1 ? 1 : k * k * (3 - 2 * k);
      // Прежний экран — снимком того, что на экране сейчас (с 3.09; до того он рисовался заново на каждом кадре
      // перехода — вдвое больше работы, «тиша» входила рывками): снимок — в начале перехода и когда новый переход
      // начался поверх прежнего (тогда снимок — их смесь, и смена идёт без скачка).
      bool fresh = !trOn || k < trK;
      trOn = true;
      trK = k;
      if (fresh && fb2) {
        if (valid) memcpy(fb2, fb, W * H * 2);
        else {   // на экране неизвестно что — прежний экран рисуется (один раз)
          g.target(fb2, 0, 0, W, H);
          drawScene(g, *old);
        }
      }
      g.target(fb, 0, 0, W, H);
      compose([&](G &gg) { drawScene(gg, cur); }, [&](G &gg) { drawScene(gg, *old); }, kind, e);
      cRender = RXC_CYC() - c0;
      hashSend(o, 0, H, 0, W);
      valid = true;
      lastKey = 0;   // после перехода — ещё раз целиком
      anOn = false;
      fullDraws++;
      return;
    }
    trOn = false;
    if (!valid || !key || key != lastKey || (mainAn && !anOn)) {
      uint32_t c0 = RXC_CYC();
      g.target(fb, 0, 0, W, H);
      drawScene(g, cur);
      cRender = RXC_CYC() - c0;
      hashSend(o, 0, H, 0, W);
      valid = true;
      lastKey = key;
      anOn = mainAn && key;
      if (anOn) anRemember(an, cur.spec, cur.peak, cur.airMute);
      fullDraws++;
      return;
    }
    uint32_t c0 = RXC_CYC();
    m2::Rect rr[AN_N + 6];
    int n = 0;
    if (anOn) {   // риски — только разница; соседние куски — одной посылкой, если так дешевле
      g.target(fb, 0, 0, W, H);
      n = mergeRects(rr, analyzerStep(g, an, cur.spec, cur.peak, cur.airMute, rr), 500);
    }
    m2::Rect dr[6];
    int nd = sceneDyn(cur, dr);
    for (int i = 0; i < nd; i++) {   // области, которые движутся сами
      g.target(fb, 0, 0, W, H);
      g.clip(dr[i].x, dr[i].y, dr[i].w, dr[i].h);
      drawScene(g, cur);
    }
    cRender = RXC_CYC() - c0;
    for (int i = 0; i < n; i++) {
      sendRect(o, rr[i].x, rr[i].y, rr[i].w, rr[i].h);
      markStale(rr[i]);
    }
    for (int i = 0; i < nd; i++) hashSend(o, dr[i].y, dr[i].y + dr[i].h, dr[i].x, dr[i].x + dr[i].w);
  }

 private:
  int nextBuf = 0;
  template <class FN, class FO>
  void compose(FN &&drawNew, FO &&drawOld, int kind, float e) {
    if (!fb2) {   // второго кадра нет: сдвиги — рисованием обоих экранов, остальное — новый экран без смешивания
      if (kind == TR_LEFT || kind == TR_RIGHT || kind == TR_UP || kind == TR_DOWN) {
        bool hor = kind == TR_LEFT || kind == TR_RIGHT, fwd = kind == TR_LEFT || kind == TR_UP;
        int L = hor ? W : H, sft = (int)(e * L + 0.5f);
        int oo = fwd ? -sft : sft, on = fwd ? L - sft : sft - L;
        g.origin(hor ? oo : 0, hor ? 0 : oo);
        if (hor) g.clip(fwd ? 0 : sft, 0, W - sft, H);
        else g.clip(0, fwd ? 0 : sft, W, H - sft);
        drawOld(g);
        g.origin(hor ? on : 0, hor ? 0 : on);
        if (hor) g.clip(fwd ? W - sft : 0, 0, sft, H);
        else g.clip(0, fwd ? H - sft : 0, W, sft);
        drawNew(g);
        g.origin(0, 0);
        g.unclip();
      } else drawNew(g);
      return;
    }
    // во втором кадре — снимок прежнего экрана (sceneFrame); новый рисуется только там, где его видно
    switch (kind) {
      case TR_LEFT:    // новый въезжает справа
      case TR_RIGHT: { // …слева
        int sft = (int)(e * W + 0.5f);
        if (sft > W) sft = W;
        for (int y = 0; y < H; y++) {   // прежний уезжает — сдвиг снимка
          if (kind == TR_LEFT) memcpy(fb + y * W, fb2 + y * W + sft, (W - sft) * 2);
          else memcpy(fb + y * W + sft, fb2 + y * W, (W - sft) * 2);
        }
        if (sft > 0) {
          if (kind == TR_LEFT) {
            g.origin(W - sft, 0);
            g.clip(W - sft, 0, sft, H);
          } else {
            g.origin(sft - W, 0);
            g.clip(0, 0, sft, H);
          }
          drawNew(g);
          g.origin(0, 0);
          g.unclip();
        }
        return;
      }
      case TR_UP:      // новый поднимается снизу
      case TR_DOWN: {  // …опускается сверху
        int sft = (int)(e * H + 0.5f);
        if (sft > H) sft = H;
        if (kind == TR_UP) memcpy(fb, fb2 + sft * W, (H - sft) * W * 2);
        else memcpy(fb + sft * W, fb2, (H - sft) * W * 2);
        if (sft > 0) {
          if (kind == TR_UP) {
            g.origin(0, H - sft);
            g.clip(0, H - sft, W, sft);
          } else {
            g.origin(0, sft - H);
            g.clip(0, 0, W, sft);
          }
          drawNew(g);
          g.origin(0, 0);
          g.unclip();
        }
        return;
      }
      case TR_IRIS: {   // новый раскрывается кругом из середины, край мягкий
        const float cx = W / 2.0f, cy = H / 2.0f, r = e * 186, ro = r + 0.5f, ri = r - 1;
        int bx0 = (int)floorf(cx - ro), by0 = (int)floorf(cy - ro), bx1 = (int)ceilf(cx + ro), by1 = (int)ceilf(cy + ro);
        bx0 = bx0 < 0 ? 0 : bx0;
        by0 = by0 < 0 ? 0 : by0;
        bx1 = bx1 > W ? W : bx1;
        by1 = by1 > H ? H : by1;
        if (r > 0 && bx1 > bx0 && by1 > by0) {   // новый — только в квадрате круга
          g.clip(bx0, by0, bx1 - bx0, by1 - by0);
          drawNew(g);
          g.unclip();
        }
        // по строке: вне круга — снимок, внутри (r − 1) — новый, точки на краю — смесь; корень — только для края
        for (int y = 0; y < H; y++) {
          float dy = y + 0.5f - cy, dy2 = dy * dy;
          uint16_t *p = fb + y * W, *q = fb2 + y * W;
          if (r <= 0 || dy2 >= ro * ro) {
            memcpy(p, q, W * 2);
            continue;
          }
          float xo = sqrtf(ro * ro - dy2);
          int o0 = (int)floorf(cx - xo), o1 = (int)ceilf(cx + xo);
          if (o0 > 0) memcpy(p, q, o0 * 2);
          else o0 = 0;
          if (o1 < W - 1) memcpy(p + o1 + 1, q + o1 + 1, (W - 1 - o1) * 2);
          else o1 = W - 1;
          int i0 = W, i1 = -1;
          if (ri > 0 && dy2 < ri * ri) {
            float xi = sqrtf(ri * ri - dy2);
            i0 = (int)ceilf(cx - xi);
            i1 = (int)floorf(cx + xi) - 1;
          }
          for (int x = o0; x <= o1; x++) {
            if (x >= i0 && x <= i1) {
              x = i1;
              continue;
            }
            float dx = x + 0.5f - cx, m = r - sqrtf(dx * dx + dy2) + 0.5f;
            if (m >= 1) continue;
            p[x] = m <= 0 ? q[x] : G::blend(q[x], p[x], (uint8_t)(m * 255));
          }
        }
        return;
      }
      case TR_WIPE: {   // шторка слева направо, край 24 точки
        const int soft = 24;
        int edge = (int)(e * (W + soft)) - soft, nx = edge + soft;
        if (nx > 0) {   // новый — только левее дальнего края шторки
          g.clip(0, 0, nx < W ? nx : W, H);
          drawNew(g);
          g.unclip();
        }
        for (int y = 0; y < H; y++) {
          uint16_t *p = fb + y * W, *q = fb2 + y * W;
          for (int x = edge < 0 ? 0 : edge; x < W; x++) {
            int d = x - edge;
            p[x] = d >= soft ? q[x] : G::blend(p[x], q[x], (uint8_t)(d * 255 / soft));
          }
        }
        return;
      }
      default: {   // растворение
        drawNew(g);
        uint8_t a = (uint8_t)(e * 255 + 0.5f);
        for (int i = 0; i < W * H; i++) fb[i] = G::blend(fb2[i], fb[i], a);
      }
    }
  }
};

}   // namespace rxc
#ifdef ARDUINO
#pragma GCC pop_options
#endif
