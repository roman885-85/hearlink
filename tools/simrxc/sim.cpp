/*  Цветной экран приёмника на компьютере (с 3.03): тот же hearlink/rxcolor.h и тот же вывод полосами (rxstrip.h),
    что идут в плату, — только куски кадра кладутся не в экран, а в картинку. Кадры каждые 40 мс складываются в
    out/ — из них run.sh собирает анимацию и лист ключевых кадров. Ещё печатается, сколько точек за кадр ушло бы
    на экран (шина SPI: 40 МГц — 0,4 мкс на точку).   Запуск: tools/simrxc/run.sh  */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <string>
#include <chrono>
#include "rxstrip.h"

uint32_t millis() { return 0; }
#ifndef SIM_VERSION
#define SIM_VERSION "?"   // run.sh подставляет FW_VERSION из config.h
#endif

using namespace rxc;
static uint16_t fb[W * H];
struct HostOut : Out {
  void wait(int) override {}
  void send(int, int x, int y, int w, int h, uint16_t *px) override {
    for (int r = 0; r < h; r++) memcpy(fb + (y + r) * W + x, px + r * w, w * 2);
  }
};
static HostOut out;
static uint16_t check[W * H];   // кадр, нарисованный целиком, — сверка с тем, что «на экране» после дорисовки
static uint32_t badFrames, badPx;
// сверка переходов (с 3.09): эталон — прежним способом: оба экрана целиком и формула для каждой точки; прежний экран —
// снимок того, что было на «экране» в начале перехода
static uint16_t refOld[W * H], refNew[W * H], refOut[W * H];
static uint32_t trBadFrames, trBadPx, trFrames;
static float trWas = 2;
static void transitionRef(const Scene &sc, int kind, float k) {
  float e = k <= 0 ? 0 : k >= 1 ? 1 : k * k * (3 - 2 * k);
  G rg;
  rg.target(refNew, 0, 0, W, H);
  drawScene(rg, sc);
  const uint16_t *q = refOld, *p = refNew;
  if (kind == TR_LEFT || kind == TR_RIGHT || kind == TR_UP || kind == TR_DOWN) {
    // прежний — сдвиг снимка; новый — рисуется со смещением и обрезкой (как рисовался всегда: растровый фон зависит от
    // места на экране, поэтому сдвиг готовой картинки нового экрана с ним точка в точку не совпадёт)
    bool hor = kind == TR_LEFT || kind == TR_RIGHT, fwd = kind == TR_LEFT || kind == TR_UP;
    int L = hor ? W : H, sft = (int)(e * L + 0.5f);
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        int c = hor ? x : y, src = fwd ? c + sft : c - sft;
        if (src >= 0 && src < L) refOut[y * W + x] = hor ? q[y * W + src] : q[src * W + x];
      }
    int on = fwd ? L - sft : sft - L;
    G ng;
    ng.target(refOut, 0, 0, W, H);
    ng.origin(hor ? on : 0, hor ? 0 : on);
    if (hor) ng.clip(fwd ? W - sft : 0, 0, sft, H);
    else ng.clip(0, fwd ? H - sft : 0, W, sft);
    if (sft > 0) drawScene(ng, sc);
    return;
  }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int i = y * W + x;
      uint16_t o;
      if (kind == TR_IRIS) {
        const float cx = W / 2.0f, cy = H / 2.0f, r = e * 186;
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy, m = r - sqrtf(dx * dx + dy * dy) + 0.5f;
        o = m >= 1 ? p[i] : m <= 0 ? q[i] : G::blend(q[i], p[i], (uint8_t)(m * 255));
      } else if (kind == TR_WIPE) {
        const int soft = 24;
        int edge = (int)(e * (W + soft)) - soft, d = x - edge;
        o = x < edge ? p[i] : d >= soft ? q[i] : G::blend(p[i], q[i], (uint8_t)(d * 255 / soft));
      } else
        o = G::blend(q[i], p[i], (uint8_t)(e * 255 + 0.5f));
      refOut[i] = o;
    }
}
static Strips strips;
static uint16_t frame0[W * H], frame1[W * H], bufA[W * SH], bufB[W * SH];
static FILE *idx;
static int frameNo;

static void save(const char *scene, uint32_t t, bool key, uint32_t sent, double us) {
  char path[96];
  snprintf(path, sizeof(path), "out/f%04d.rgb", frameNo);
  FILE *f = fopen(path, "wb");
  for (int i = 0; i < W * H; i++) {
    uint16_t c = fb[i];
    uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  fprintf(idx, "%d\t%s\t%u\t%d\t%u\t%.0f\n", frameNo, scene, (unsigned)t, key ? 1 : 0, (unsigned)sent, us);
  frameNo++;
}

// ---- «режиссёр»: как ui_rx.h — сцена, переходы между видами, оповещения
static Scene cur, old;
static uint8_t sidWas = SC_NONE;
static int trKind = -1;
static uint32_t trAt = 0, trMs = 0, simClock = 0;
static const char *sceneName = "";
static void setTransition(uint8_t from, uint8_t to) {
  trKind = from == SC_MAIN && to == SC_MENU ? TR_UP : from == SC_MENU && (to == SC_MAIN || to == SC_MUTE || to == SC_LOST) ? TR_DOWN
           : from == SC_MENU && (to == SC_ABOUT || to == SC_LINK) ? TR_LEFT : (from == SC_ABOUT || from == SC_LINK) && to == SC_MENU ? TR_RIGHT
           : to == SC_MUTE || from == SC_MUTE || to == SC_EAR || from == SC_EAR ? TR_IRIS : from == SC_SPLASH ? TR_WIPE : TR_DISSOLVE;
  trMs = trKind == TR_WIPE ? 320 : trKind == TR_IRIS ? 300 : trKind == TR_DISSOLVE ? 240 : 260;
  trAt = from != SC_NONE ? simClock : 0;
}
static void render(bool key) {
  if (cur.sid != sidWas) {
    old = cur;
    old.sid = sidWas;
    setTransition(sidWas, cur.sid);
    sidWas = cur.sid;
  }
  auto t0 = std::chrono::steady_clock::now();
  strips.sentPx = 0;
  bool tr = trAt && simClock - trAt < trMs;
  if (tr) {
    float k = (float)(simClock - trAt) / trMs;
    if (k < trWas) memcpy(refOld, fb, sizeof(fb));   // начало перехода: на «экране» — прежний экран
    trWas = k;
    strips.sceneFrame(out, cur, &old, trKind, k);
    transitionRef(cur, trKind, k);
    uint32_t bad = 0;
    for (int i = 0; i < W * H; i++)
      if (refOut[i] != fb[i]) bad++;
    trFrames++;
    if (bad) {
      trBadFrames++;
      trBadPx += bad;
      if (trBadFrames <= 5) printf("ПЕРЕХІД НЕ ЗБІГСЯ: %s, вид %d, k=%.2f, точок %u\n", sceneName, trKind, k, (unsigned)bad);
    }
  } else {
    trWas = 2;
    trAt = 0;
    strips.sceneFrame(out, cur, nullptr, -1, 1);
    old = cur;   // прежний экран для следующего перехода — каким он был в последнем кадре
    // сверка: «экран» должен быть точно таким, как кадр, нарисованный по той же сцене целиком
    G cg;
    cg.target(check, 0, 0, W, H);
    drawScene(cg, cur);
    uint32_t bad = 0;
    for (int i = 0; i < W * H; i++)
      if (check[i] != fb[i]) bad++;
    if (bad) {
      badFrames++;
      badPx += bad;
      if (badFrames <= 5) {
        printf("РАСХОЖДЕНИЕ: %s, t=%u, точек %u\n", sceneName, (unsigned)cur.t, (unsigned)bad);
        if (getenv("SIM_DIFF")) {   // где именно: координаты, «экран» и кадр целиком
          int n = 0;
          for (int i = 0; i < W * H && n < 24; i++)
            if (check[i] != fb[i]) {
              printf("   (%d,%d) экран %04x, целиком %04x\n", i % W, i / W, fb[i], check[i]);
              n++;
            }
        }
      }
    }
  }
  double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
  save(sceneName, cur.t, key, strips.sentPx, us);
}

// главный экран: движение спектра и стрелок — как в приборе
static float specShown[14], specPeak[14];
static uint32_t peakAt[14], specLast;
static void animSpectrum(uint32_t t, bool sound) {
  float dt = t > specLast && t - specLast < 200 ? (float)(t - specLast) : 40;
  specLast = t;
  for (int b = 0; b < AN_N; b++) {
    float beat = 0.5f + 0.5f * sinf(t * 0.006f + b * 0.9f), hit = (t / 380 + b * 3) % 5 == 0 ? 1.0f : 0.35f;
    float target = sound ? (0.25f + 0.7f * beat * hit) * (1.0f - b * 0.03f) : 0;
    float d = target - specShown[b];
    specShown[b] += d * (d > 0 ? 1 - expf(-dt / 45.0f) : 1 - expf(-dt / 150.0f));
    if (specShown[b] >= specPeak[b]) {
      specPeak[b] = specShown[b];
      peakAt[b] = t;
    } else if (t - peakAt[b] > 450) {
      specPeak[b] -= dt * 0.0011f;
      if (specPeak[b] < specShown[b]) specPeak[b] = specShown[b];
    }
  }
}

// громкость — как в приборе (ui_rx.h): шаг ручки 0…20 сразу, плавная громкость догоняет его (70 мс), карточка видна
// полторы секунды после поворота
static int volStepNow = 13;
static float volSmooth = 13, volCard = 0;
static uint32_t volTurnAt = 0;
static void volFrame(int step, uint32_t clock) {
  if (step != volStepNow) {
    volStepNow = step;
    volTurnAt = clock ? clock : 1;
  }
  volSmooth += (volStepNow - volSmooth) * (1 - expf(-40 / 70.0f));
  bool on = volTurnAt && clock - volTurnAt < 1500;
  volCard += on ? 40 / 160.0f : -40 / 220.0f;
  volCard = volCard < 0 ? 0 : volCard > 1 ? 1 : volCard;
  cur.vol = volSmooth;
  cur.volPct = volStepNow * 5;
  cur.volK = volCard;
}

static const char *const MN[] = { "Вигляд", "Світлодіод", "Мова", "Чіткість", "Баланс", "Межа гучн.", "Підсилення", "Стискання", "У паузах",
                                  "Поріг пауз", "Закриття", "Шумозагл.", "Чутл. шуму", "Зразок шуму", "Навушники", "Зв'язок", "Набір", "Канал", "Запас",
                                  "Вихід звуку", "Виводи", "Екран", "Яскравість", "Забути набір", "Перезапуск", "Про пристрій", "Вийти" };
static const char *const MV[] = { "спектр", "норма", "як передавач", "вимк.", "рівно", "100 %", "+20 дБ", "сильне", "ще -6 дБ", "+6 дБ", "середньо", "середнє",
                                  "2 з 5", "сам", "тест", "-64 дБм", "1", "авто (5)", "сам (14)", "PCM5102", "2 канали", "вимкнути", "3 з 5",
                                  "ні", "ні", "", "" };
static const uint8_t MI[] = { 9, 10, 11, 12, 13, 14, 17, 19, 20, 20, 20, 18, 18, 18, 15, 16, 0, 1, 2, 3, 4, 5, 23, 6, 21, 7, 8 };

static int8_t lnkR[60];
static uint8_t lnkL[60];

int main() {
  if (getenv("SIM_EN")) uiLang = 1;   // надписи по-английски (проверка ширины перевода)
  idx = fopen("out/frames.tsv", "w");
  strips.begin(frame0, frame1, bufA, bufB);
  strcpy(cur.name, "Зал, 2 ряд. 4 місце");
  for (int i = 0; i < 60; i++) {
    lnkR[i] = (int8_t)(-60 - 6 * sinf(i / 6.0f) - (i % 17 == 0 ? 9 : 0));
    lnkL[i] = (uint8_t)(i % 13 == 0 ? 25 : i % 29 == 0 ? 70 : 0);
  }
  lnkR[44] = -128;
  const uint32_t STEP = 40;
  auto run = [&](const char *name, uint8_t sid, uint32_t dur, auto &&fill, std::initializer_list<uint32_t> keys) {
    sceneName = name;
    cur.sid = sid;
    for (uint32_t t = 0; t < dur; t += STEP, simClock += STEP) {
      cur.t = t;
      fill(t);
      bool key = false;
      for (uint32_t k : keys)
        if (t <= k && k < t + STEP) key = true;
      render(key);
    }
  };
  auto none = [](uint32_t) {};
  run("заставка", SC_SPLASH, 4000, none, { 700, 1500, 2300, 3900 });
  run("пошук", SC_SEARCH, 2600, [&](uint32_t t) {
    strcpy(cur.siTitle, tr("ПОШУК ПЕРЕДАВАЧА"));
    strcpy(cur.siLabel, tr("шукаю: канал"));
    int ch = 1 + (int)(t / 400) % 13;
    cur.si.prevCh = cur.si.ch;
    cur.si.ch = ch;
    cur.si.roll = (t % 400) / 170.0f;
    cur.si.pos = 1 + (t / 400.0f);
    cur.si.left = 1 - t / 10000.0f;
    cur.si.lost = false;
  }, { 1300 });
  run("запуск", SC_STARTING, 3000, [&](uint32_t) {
    cur.total = 4400;
    snprintf(cur.info, sizeof(cur.info), "набір 1  версія %s", SIM_VERSION);
  }, { 2500 });
  run("головний: спектр", SC_MAIN, 3200, [&](uint32_t t) {
    animSpectrum(t, true);
    cur.spec = specShown;
    cur.peak = specPeak;
    cur.nb = AN_N;
    cur.bars = 3.2f + 0.6f * sinf(t / 900.0f);
    volFrame(t < 1600 ? 13 : t < 1720 ? 14 : t < 1840 ? 15 : 16, simClock);   // три щелчка ручкой
    cur.sound = true;
    cur.view = 0;
    cur.signal = true;
    cur.rssi = -58;
    cur.stereo = true;
    cur.ns = true;
    strcpy(cur.toast, tr("Передавач на зв'язку"));
    cur.toastK = t < 260 ? t / 260.0f : t < 1760 ? 1 : t < 1960 ? 1 - (t - 1760) / 200.0f : 0;
  }, { 1000, 2400 });
  cur.toastK = 0;
  // тот же экран без оповещений и карточки громкости — то, что слушатель видит почти всё время: здесь дорисовываются
  // только столбики; громкость и уровень связи меняются — экран перерисовывается целиком
  run("головний: спектр, рівно", SC_MAIN, 3200, [&](uint32_t t) {
    animSpectrum(3200 + t, true);
    volFrame(t < 1800 ? 16 : 14, simClock);
    cur.bars = t < 1000 ? 3.2f : t < 2400 ? 2.6f : 3.4f;
    cur.ns = t < 2600;
  }, { 600, 2900 });
  run("меню", SC_MENU, 3600, [&](uint32_t t) {
    cur.nrows = sizeof(MN) / sizeof(MN[0]);
    for (int i = 0; i < cur.nrows; i++) {
      cur.rows[i].name = tr(MN[i]);
      strcpy(cur.rows[i].value, MV[i]);
      cur.rows[i].icon = MI[i];
    }
    int item = t < 2400 ? (int)(t / 300) : 8;
    float target = item, first = item < 2 ? 0 : item > cur.nrows - 3 ? cur.nrows - 5 : item - 2;
    cur.sel += (target - cur.sel) * 0.5f;
    cur.scroll += (first * MENU_ROW_H - cur.scroll) * 0.5f;
    cur.edit = t > 2700;
    // правка: на 3,0 с значение меняется («въезжает» справа за 0,2 с) — как ui_rx.h при повороте ручки
    if (t >= 3000) {
      strcpy(cur.rows[item].value, "ще -12 дБ");
      strcpy(cur.editOld, "ще -6 дБ");
      cur.editK = (t - 3000) / 200.0f > 1 ? 1 : (t - 3000) / 200.0f;
      cur.editDir = 1;
    } else cur.editK = 1;
  }, { 600, 2200, 3080, 3300 });
  cur.edit = false;
  run("про пристрій", SC_ABOUT, 1200, [&](uint32_t) {
    strcpy(cur.about.name, cur.name);
    strcpy(cur.about.version, SIM_VERSION);
    cur.about.channel = 5;
    cur.about.kit = 1;
    strcpy(cur.about.id, "4C3B10");
    cur.about.reserveMs = 14.2f;
    cur.about.rssi = -64;
    cur.about.signal = true;
  }, { 900 });
  run("меню", SC_MENU, 600, none, {});
  run("зв'язок", SC_LINK, 2000, [&](uint32_t t) {
    cur.li.signal = true;
    cur.li.rssi = -64;
    cur.li.bars = 2.4f;
    cur.li.lossPct = 0.4f;
    cur.li.reserveMs = 14;
    cur.li.hr = lnkR;
    cur.li.hl = lnkL;
    cur.li.pos = 200;
    cur.li.n = 60;
    cur.li.frac = (t % 1000) / 1000.0f;
  }, { 1500 });
  run("меню", SC_MENU, 600, none, {});
  run("головний: стрілки", SC_MAIN, 2400, [&](uint32_t t) {
    cur.view = 1;
    cur.stereo = true;
    cur.vuL = 0.55f + 0.3f * sinf(t / 170.0f);
    cur.vuR = 0.5f + 0.35f * sinf(t / 210.0f + 1);
    cur.vuLP = 0.86f;
    cur.vuRP = 0.92f;
    cur.volK = 0;
  }, { 1200 });
  run("головний: гучність", SC_MAIN, 1600, [&](uint32_t t) {
    cur.view = 2;
    cur.vol = 13;
    cur.vuL = 0.5f + 0.4f * sinf(t / 150.0f);
  }, { 1000 });
  cur.view = 0;
  run("тиша", SC_MUTE, 2200, none, { 1600 });
  run("немає сигналу", SC_LOST, 3200, [&](uint32_t t) {
    strcpy(cur.siTitle, tr("НЕМАЄ СИГНАЛУ"));
    strcpy(cur.siLabel, tr("канал"));
    cur.si.ch = cur.si.prevCh = 5;
    cur.si.roll = 1;
    cur.si.pos = 5;
    cur.si.left = 1 - t / 10000.0f;
    cur.si.lost = true;
  }, { 1100, 2400 });
  run("перехід в очікування", SC_GOING, 6000, [&](uint32_t) {
    cur.total = 6000;
    cur.off = false;
  }, { 1700, 5800 });
  run("це я", SC_IDENT, 2100, none, { 600, 1000 });
  run("оновлення", SC_UPDATE, 2000, [&](uint32_t t) {
    snprintf(cur.line, sizeof(cur.line), "%s", tr("приймаю з передавача"));
    cur.percent = 20 + (int)(t / 40);
    snprintf(cur.foot, sizeof(cur.foot), "%d %%", cur.percent);
  }, { 1500 });
  run("не підключено", SC_UNPAIRED, 2000, [&](uint32_t) {
    cur.code = 4821;
    cur.heard = true;
  }, { 1500 });
  run("навушники", SC_EAR, 2200, [&](uint32_t t) {
    cur.earSide = t < 1100 ? 1 : 2;
    cur.stereo = true;
  }, { 700, 1700 });
  run("вимкнено (сон)", SC_SLEEP, 1600, none, { 1200 });
  fclose(idx);
  printf("кадрів %d; кадрів, де «екран» не збігся з кадром, намальованим цілком: %u (точок %u)\n", frameNo, (unsigned)badFrames, (unsigned)badPx);
  printf("кадрів переходу %u; не збіглися з еталоном: %u (точок %u)\n", (unsigned)trFrames, (unsigned)trBadFrames, (unsigned)trBadPx);
  return 0;
}
