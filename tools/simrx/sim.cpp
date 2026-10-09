/*  Просмотр экранов приёмника на компьютере: тот же hearlink/rxscreens.h, что идёт в плату, собирается с настоящей
    библиотекой U8g2 (экран SH1106 128×64, полный лист в памяти). Кадры каждые 40 мс складываются в out/ —
    из них run.sh собирает анимацию и лист ключевых кадров.   Запуск: tools/simrx/run.sh  */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <string>
extern "C" {
#include "u8g2.h"
}
#include "../../hearlink/rxscreens.h"
#define RX_START_MS 4400   // как в hearlink/rxaudio.h: полоска «ЗАПУСК» идёт 4 с

static u8g2_t g;
static uint8_t cbByte(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }
static uint8_t cbGpio(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }
static int frameNo;
static FILE *idx;

static void save(const char *scene, uint32_t t, bool key) {
  char path[96];
  snprintf(path, sizeof(path), "out/f%04d.pgm", frameNo);
  FILE *f = fopen(path, "wb");
  fprintf(f, "P5\n128 64\n255\n");
  const uint8_t *b = u8g2_GetBufferPtr(&g);
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 128; x++) fputc((b[(y / 8) * 128 + x] >> (y & 7)) & 1 ? 255 : 0, f);
  fclose(f);
  fprintf(idx, "%d\t%s\t%u\t%d\n", frameNo, scene, (unsigned)t, key ? 1 : 0);
  frameNo++;
}

// главный экран работы: строка состояния и спектр с пиками (движение — как в приборе)
static float simShown[14], simPeak[14];
static void mainScreen(uint32_t t, bool sound = true, int vol = 60, const char *name = "Зал, 3 ряд") {
  static uint32_t peakAt[14], last;
  float dt = t > last && t - last < 200 ? (float)(t - last) : 20;
  last = t;
  for (int b = 0; b < 14; b++) {
    float beat = 0.5f + 0.5f * sinf(t * 0.006f + b * 0.9f), hit = (t / 380 + b * 3) % 5 == 0 ? 1.0f : 0.35f;
    float target = sound ? (0.25f + 0.7f * beat * hit) * (1.0f - b * 0.03f) : 0;
    float d = target - simShown[b];
    simShown[b] += d * (d > 0 ? 1 - expf(-dt / 45.0f) : 1 - expf(-dt / 150.0f));
    if (simShown[b] >= simPeak[b]) { simPeak[b] = simShown[b]; peakAt[b] = t; }
    else if (t - peakAt[b] > 450) { simPeak[b] -= dt * 0.0011f; if (simPeak[b] < simShown[b]) simPeak[b] = simShown[b]; }
  }
  rxs::statusBar(&g, t, 3.0f + 0.9f * sinf(t / 900.0f), name, vol, sound);
  rxs::spectrum(&g, simShown, simPeak, 14, t);
}
static uint8_t simFrom[1024];   // прежний кадр — для переходов
static void keepFrame() { memcpy(simFrom, u8g2_GetBufferPtr(&g), 1024); }

// сцена поиска: канал обходится по кругу, цифра «прокручивается», отметка на шкале едет
static void searchScene(uint32_t t, const char *title, const char *label, float left, bool lost) {
  rxs::ScanInfo si;
  si.title = title;
  si.label = label;
  uint32_t step = t / 420;
  si.ch = 1 + (int)(step % 13);
  si.prevCh = 1 + (int)((step + 12) % 13);
  si.roll = (t % 420) / 170.0f;
  if (si.roll > 1) si.roll = 1;
  static float pos = 1;
  float want = si.ch;
  if (fabsf(want - pos) > 6) pos = want;
  pos += (want - pos) * 0.28f;
  si.pos = pos;
  si.left = left;
  si.lost = lost;
  rxs::search(&g, t, si);
}

template <class F> static void scene(const char *name, uint32_t ms, F draw, std::initializer_list<uint32_t> keys) {
  for (uint32_t t = 0; t < ms; t += 40) {
    u8g2_ClearBuffer(&g);
    draw(t);
    bool key = false;
    for (uint32_t k : keys) key |= t <= k && k < t + 40;
    save(name, t, key);
  }
}

int main() {
  u8g2_Setup_sh1106_i2c_128x64_noname_f(&g, U8G2_R0, cbByte, cbGpio);
  u8g2_InitDisplay(&g);
  u8g2_SetPowerSave(&g, 0);
  idx = fopen("out/frames.tsv", "w");
  // новый приёмник: не подключён к набору
  scene("0а не підключено, передавача не чути", 2000, [](uint32_t t) { rxs::unpaired(&g, t, 714, false); }, { 500, 900 });
  scene("0б не підключено, прошу доступ", 2400, [](uint32_t t) { rxs::unpaired(&g, t, 714, true); }, { 900, 1500 });
  // включение без передатчика
  scene("1 заставка", 4000, [](uint32_t t) { rxs::splash(&g, t); }, { 300, 600, 900, 1200, 1400, 1700, 2400, 3900 });
  scene("2 пошук", 6000, [](uint32_t t) { searchScene(t, "ПОШУК ПЕРЕДАВАЧА", "шукаю: канал", 1 - t / 6000.0f, false); }, { 300, 700, 1100, 1500, 3000, 5800 });
  scene("3 сигналу немає", 3000, [](uint32_t t) { rxs::going(&g, t, 3000, false); }, { 250, 700, 1600, 2700, 2900 });
  scene("4 очікування", 1200, [](uint32_t) {}, { 500 });
  // передатчик появился
  scene("5 пробудження", 4000, [](uint32_t t) { rxs::splash(&g, t); }, { 3900 });
  scene("6а запуск, довге ім'я", RX_START_MS, [](uint32_t t) { rxs::starting(&g, t, RX_START_MS, "Зал, другий ряд, біля проходу ліворуч", "набір 1  версія 2.19"); },
        { 1000, 2400, 3600 });
  scene("9 оновлення: приймаю", 3000, [](uint32_t t) { rxs::update(&g, t, "приймаю з передавача", (int)(t / 30), "43 %"); }, { 1300, 2700 });
  scene("9а оновлення: записую", 2000, [](uint32_t t) { rxs::update(&g, t, "записую, не вимикайте", 72, "72 %"); }, { 800 });
  scene("9б оновлення: готово", 1000, [](uint32_t t) { rxs::update(&g, t, "готово, перезапуск", 100, "прошивка 2.19"); }, { 500 });
  scene("9в оновлення: потрібен USB", 1000, [](uint32_t t) { rxs::update(&g, t, "не вдалося", -1, "потрібен USB (раз)"); }, { 500 });
  scene("6 запуск", RX_START_MS, [](uint32_t t) { rxs::starting(&g, t, RX_START_MS, "Зал, 3 ряд", "набір 1  версія 2.19"); }, { 200, 1200, 2200, 3200, 4250 });
  scene("7 робота", 2600, [](uint32_t t) { mainScreen(t); }, { 400, 1200, 2000 });
  scene("7а робота: в ефірі тихо", 2000, [](uint32_t t) { mainScreen(t + 2600, false); }, { 700, 1500 });
  scene("7б робота: довге ім'я", 4200, [](uint32_t t) { mainScreen(t, true, 60, "Зал, другий ряд, біля проходу"); }, { 800, 2600, 3800 });
  scene("7в гучність", 2200, [](uint32_t t) {   // покрутили ручку: карточка раскрывается, число едет 60 → 75, потом уходит
    mainScreen(t);
    float k = t < 1700 ? rxs::phase(t, 0, 160) : 1 - rxs::phase(t, 1700, 220);
    float vol = 12 + 3 * rxs::easeOut(rxs::phase(t, 250, 500));
    rxs::volumeCard(&g, t, vol, k);
  }, { 400, 900 });
  scene("7г оповіщення", 2400, [](uint32_t t) {   // плашка сверху: выехала, постояла, уехала
    mainScreen(t);
    float k = t < 1900 ? rxs::phase(t, 0, 260) : 1 - rxs::phase(t, 1900, 200);
    rxs::toast(&g, "Сигнал відновлено", k);
  }, { 80, 160, 600, 2000 });
  scene("7г2 заблоковано", 1200, [](uint32_t t) {   // ручку заблокировали с передатчика
    mainScreen(t);
    rxs::toast(&g, "Заблоковано", 1);
  }, { 600 });
  scene("7г3 розблоковано", 1200, [](uint32_t t) {
    mainScreen(t);
    rxs::toast(&g, "Розблоковано", 1);
  }, { 600 });
  scene("7д перехід: робота → меню", 400, [](uint32_t t) {   // меню поднимается снизу
    static const rxs::MenuRow R[] = { { "Набір", "1", 0 }, { "Канал", "сам (6)", 1 }, { "Запас", "сам", 2 }, { "Вихід звуку", "PDM", 3 }, { "Виводи", "стерео", 4 },
                                      { "Екран", "вимкнути", 5 }, { "Забути набір", "", 6 }, { "Про пристрій", "", 7 }, { "Назад", "", 8 } };
    if (t == 0) { mainScreen(2000); keepFrame(); u8g2_ClearBuffer(&g); }
    rxs::menuList(&g, t, R, 9, 0, 0, false);
    rxs::blend(&g, simFrom, t / 280.0f, rxs::TR_UP);
  }, { 80, 160, 240 });
  scene("7е меню", 3600, [](uint32_t t) {   // ручку крутят: плашка переезжает, список прокручивается; на «Канал» — правка
    static const rxs::MenuRow R[] = { { "Набір", "1", 0 }, { "Канал", "сам (6)", 1 }, { "Запас", "сам", 2 }, { "Вихід звуку", "PDM", 3 }, { "Виводи", "стерео", 4 },
                                      { "Екран", "вимкнути", 5 }, { "Забути набір", "", 6 }, { "Про пристрій", "", 7 }, { "Назад", "", 8 } };
    static float sel = 0, scroll = 0;
    int target = t < 500 ? 0 : t < 900 ? 1 : t < 1900 ? 1 : t < 2200 ? 2 : t < 2500 ? 3 : t < 2800 ? 4 : 5;
    float want = target < 2 ? 0 : target > 6 ? 5 * 16 : (target - 1) * 16;
    sel += (target - sel) * 0.3f;
    scroll += (want - scroll) * 0.25f;
    rxs::menuList(&g, t, R, 9, sel, scroll, t >= 1200 && t < 1900);
  }, { 200, 600, 700, 1400, 2300, 2700, 3400 });
  scene("7ж це я", 2000, [](uint32_t t) { rxs::identify(&g, t, "Зал, 3 ряд"); }, { 200, 720, 1100 });
  scene("7з перехід: робота → тиша", 400, [](uint32_t t) {   // тишина раскрывается кругом
    if (t == 0) { mainScreen(2000); keepFrame(); u8g2_ClearBuffer(&g); }
    rxs::muted(&g, t);
    rxs::blend(&g, simFrom, t / 300.0f, rxs::TR_IRIS);
  }, { 80, 160, 240 });
  scene("7и перехід: заставка → пошук", 400, [](uint32_t t) {   // шторка с мягким краем
    if (t == 0) { rxs::splash(&g, 3900); keepFrame(); u8g2_ClearBuffer(&g); }
    searchScene(t, "ПОШУК ПЕРЕДАВАЧА", "шукаю: канал", 1, false);
    rxs::blend(&g, simFrom, t / 320.0f, rxs::TR_WIPE);
  }, { 80, 160, 240 });
  scene("7к перехід: розчинення", 320, [](uint32_t t) {
    if (t == 0) { rxs::starting(&g, 4300, 4400, "Зал, 3 ряд", "набір 1  версія 2.29"); keepFrame(); u8g2_ClearBuffer(&g); }
    mainScreen(t);
    rxs::blend(&g, simFrom, t / 260.0f, rxs::TR_DISSOLVE);
  }, { 80, 160 });
  // ---- 2.32: виды главного экрана, меню «для слуха и удобства», связь, проверка наушников
  scene("13 вигляд: стрілки, стерео", 3000, [](uint32_t t) {
    static float l, r, pl, pr;
    float tl = 0.45f + 0.4f * sinf(t / 260.0f) * sinf(t / 90.0f), tr_ = 0.5f + 0.42f * sinf(t / 310.0f + 1) * sinf(t / 70.0f);
    l += (tl - l) * (tl > l ? 0.3f : 0.12f); r += (tr_ - r) * (tr_ > r ? 0.3f : 0.12f);
    pl = l > pl ? l : pl - 0.01f; pr = r > pr ? r : pr - 0.01f;
    rxs::statusBar(&g, t, 3.4f, "Зал, 3 ряд", 60, true);
    rxs::needles(&g, t, l, r, pl, pr, true);
  }, { 300, 900, 1500, 2200 });
  scene("13а вигляд: стрілки, один канал", 3000, [](uint32_t t) {
    static float l, pl;
    float tl = t > 2200 ? 1.0f : 0.5f + 0.45f * sinf(t / 240.0f) * sinf(t / 80.0f);
    l += (tl - l) * (tl > l ? 0.3f : 0.12f);
    pl = l > pl ? l : pl - 0.01f;
    rxs::statusBar(&g, t, 3.4f, "Зал, 3 ряд", 60, true);
    rxs::needles(&g, t, l, l, pl, pl, false);
  }, { 300, 900, 1500, 2600 });
  scene("14 вигляд: гучність", 3200, [](uint32_t t) {
    float vol = t < 800 ? 12 : t < 1300 ? 12 + (t - 800) / 500.0f * 4 : 16;
    rxs::statusBar(&g, t, 3.4f, "Зал, 3 ряд", -1, true);
    rxs::bigVolume(&g, t, vol, 20, 0.5f + 0.4f * sinf(t / 200.0f));
  }, { 300, 1000, 1800 });
  scene("14а гучність: межа 70 %, тиша 0", 2400, [](uint32_t t) {
    rxs::statusBar(&g, t, 2.0f, "Зал, 3 ряд", -1, t < 1200);
    rxs::bigVolume(&g, t, t < 1200 ? 14 : 0, 14, t < 1200 ? 0.6f : 0);
  }, { 600, 1800 });
  scene("14б картка гучності з межею 70 %", 1200, [](uint32_t t) {
    mainScreen(t);
    rxs::volumeCard(&g, t, 14, 1, 14);
  }, { 600 });
  scene("15 меню: нові пункти", 4200, [](uint32_t t) {
    static const char *const N[] = { "Вигляд", "Світлодіод", "Мова", "Чіткість", "Баланс", "Межа гучн.", "Навушники", "Зв'язок", "Набір", "Канал" };
    static const char *const V[] = { "стрілки", "яскраво", "як передавач", "середня", "праве +2", "70 %", "тест", "-48 дБм", "1", "авто (6)" };
    static const uint8_t IC[] = { 9, 10, 11, 12, 13, 14, 15, 16, 0, 1 };
    rxs::MenuRow rows[10];
    for (int i = 0; i < 10; i++) rows[i] = rxs::MenuRow{ N[i], V[i], IC[i] };
    int item = (int)(t / 500) % 8;
    static float sel, scroll;
    int first = item < 2 ? 0 : item > 7 ? 6 : item - 1;
    sel += (item - sel) * 0.3f;
    scroll += (first * 16 - scroll) * 0.3f;
    rxs::menuList(&g, t, rows, 10, sel, scroll, item == 3);
  }, { 300, 1300, 1800, 2800, 3800 });
  scene("16 зв'язок", 4000, [](uint32_t t) {
    static int8_t hr[60];
    static uint8_t hl[60];
    uint32_t pos = 40 + t / 1000;
    for (int i = 0; i < 60; i++) {
      hr[i] = (int8_t)(-52 + 9 * sinf(i * 0.35f) - (i % 60 > 22 && i % 60 < 27 ? 30 : 0));
      hl[i] = (i % 60 > 22 && i % 60 < 27) ? (i % 2 ? 60 : 8) : 0;
      if (i == 30 || i == 31) hr[i] = -128;
    }
    rxs::LinkInfo li;
    li.signal = true; li.rssi = -48; li.bars = 3.6f; li.lossPct = 0.3f; li.reserveMs = 12; li.hr = hr; li.hl = hl; li.pos = pos; li.n = 60;
    li.frac = (t % 1000) / 1000.0f;
    rxs::linkScreen(&g, t, li);
  }, { 300, 1500, 3500 });
  scene("17 перевірка навушників", 5200, [](uint32_t t) {
    uint32_t e = t % 2600;
    rxs::earTest(&g, t, e < 1000 ? 1 : e < 1300 ? 0 : e < 2300 ? 2 : 0, true);
  }, { 300, 700, 1150, 1600, 2000 });
  scene("17а перевірка: вихід «протифаза»", 1200, [](uint32_t t) { rxs::earTest(&g, t, 1, false); }, { 500 });
  scene("8 тиша", 2600, [](uint32_t t) { rxs::muted(&g, t); }, { 60, 200, 380, 520, 700, 1600 });
  scene("9 немає сигналу", 2400, [](uint32_t t) { searchScene(t, "НЕМАЄ СИГНАЛУ", "шукаю: канал", 1 - t / 10000.0f, true); }, { 120, 280, 440, 900, 1150, 1700 });
  // выключили с передатчика
  scene("10 вимкнено", 3000, [](uint32_t t) { rxs::going(&g, t, 3000, true); }, { 250, 1600 });
  scene("11 сон", 1000, [](uint32_t) {}, {});
  scene("12 ручка уві сні", 2400, [](uint32_t t) { rxs::sleeping(&g, t); }, { 600, 1400 });
  // то же по-английски (ключевые кадры — проверить, что надписи помещаются)
  uiLang = 1;
  scene("EN not connected", 1600, [](uint32_t t) { rxs::unpaired(&g, t, 714, true); }, { 900 });
  scene("EN no transmitter", 1200, [](uint32_t t) { rxs::unpaired(&g, t, 714, false); }, { 900 });
  scene("EN searching", 1200, [](uint32_t t) { searchScene(t, tr("ПОШУК ПЕРЕДАВАЧА"), tr("шукаю: канал"), 0.6f, false); }, { 900 });
  scene("EN signal lost", 3000, [](uint32_t t) { rxs::going(&g, t, 3000, false); }, { 1700 });
  scene("EN turned off", 3000, [](uint32_t t) { rxs::going(&g, t, 3000, true); }, { 1700 });
  scene("EN starting", RX_START_MS, [](uint32_t t) { rxs::starting(&g, t, RX_START_MS, "Hall, row 3", "kit 1  version 2.19"); }, { 2200 });
  scene("EN mute", 1200, [](uint32_t t) { rxs::muted(&g, t); }, { 600 });
  scene("EN asleep", 1200, [](uint32_t t) { rxs::sleeping(&g, t); }, { 1000 });
  uiLang = 0;
  fclose(idx);
  printf("кадров: %d\n", frameNo);
  return 0;
}
