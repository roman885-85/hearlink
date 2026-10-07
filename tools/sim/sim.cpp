#include <chrono>
// Рисует страницы меню передатчика на компьютере: tools/sim/run.sh → tools/sim/out/*.png
#include "txui.h"
#include <vector>
#include <string>

static uint32_t fakeMs = 100000;
uint32_t millis() { return fakeMs; }
namespace ui { void uiDelay(int) {} void uiWaitFrame() {} bool uiSlide(const uint16_t *, int, int, int, int) { return false; } void uiSlideSync() {} void uiQuiet(bool) {} void uiBreath() {}
// Карта памяти понарошку: один и тот же список в любой папке, работа «кончается» сразу.
static FmEntry simEnt[] = {
  { "ZAPYS", 0, 1, 0 }, { "Проповіді 2026", 0, 1, 0 }, { "Хор", 0, 1, 0 },
  { "Проповідь Іван Петренко — Про любов до ближнього 2026-05-12.mp3", 48211045, 0, FK_MP3 },
  { "zapys-0001.wav", 778752, 0, FK_WAV }, { "zapys-0002.wav", 153600044, 0, FK_WAV }, { "Оголошення.txt", 412, 0, FK_TEXT },
  { "Псалом 22.mp3", 3811200, 0, FK_MP3 }, { "foto.jpg", 2211000, 0, FK_OTHER },
};
bool uiFm(int op, const char *a, const char *) {
  fm.ent = simEnt;
  fm.freeMb = 196;
  fm.totalMb = 3796;
  fm.lastOp = op;
  fm.result = FM_OK;
  if (op == FM_LIST) {
    strlcpy(fm.path, a, sizeof(fm.path));
    fm.n = strcmp(a, "/Хор") ? 9 : 0;
    fm.listOk = true;
    fm.listSeq = fm.listSeq + 1;
  } else if (op == FM_INFO) {
    fm.info = FmInfo();
    strlcpy(fm.info.name, fmBase(a), sizeof(fm.info.name));
    fm.info.kind = fmKind(a);
    fm.info.playable = fm.info.kind == FK_MP3 || fm.info.kind == FK_WAV;
    fm.info.why = fm.info.playable ? 0 : 1;
    fm.info.rate = fm.info.kind == FK_MP3 ? 44100 : 32000;
    fm.info.chans = fm.info.kind == FK_MP3 ? 2 : 1;
    fm.info.bits = 16;
    fm.info.kbps = 128;
    fm.info.durS = fm.info.kind == FK_MP3 ? 3013 : 12;
    strlcpy(fm.info.text, "Неділя, 10:00 — богослужіння. Середа, 18:30 — молитовне зібрання. У суботу о 16:00 — спів хору. Запрошуємо всіх!", sizeof(fm.info.text));
    fm.infoSeq = fm.infoSeq + 1;
  } else if (op == FM_PLAY) {
    fm.playing = true;
    strlcpy(fm.playName, fmBase(a), sizeof(fm.playName));
    fm.playS = 83;
    fm.playTotalS = 3013;
    return true;
  } else if (op == FM_STOP) {
    fm.playing = false;
    return true;
  }
  fm.doneSeq = fm.doneSeq + 1;
  return true;
} }

static uint16_t fb[480 * 480], scratch[480 * 480];

static void save(const char *name) {
  std::string path = std::string("tools/sim/out/") + name + ".rgb";
  FILE *f = fopen(path.c_str(), "wb");
  for (int i = 0; i < 480 * 480; i++) {
    uint16_t v = fb[i];
    uint8_t rgb[3] = { (uint8_t)((v >> 11) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31) };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static void tap(int x, int y) {
  ui::frame(true, x, y);
  fakeMs += 60;
  ui::frame(false, x, y);
  fakeMs += 20;
  ui::frame(false, 0, 0);
}

int main() {
  using namespace ui;
  animate = false;
  begin(fb, scratch);
  view.version = "0.2";
  snprintf(view.mac, sizeof(view.mac), "28:84:85:9E:6A:84");
  view.freeHeapK = 212;
  view.uptimeS = 4521;
  view.pktPerS = 500;
  view.airAvgMs = 0.8f;
  view.airMaxMs = 9;
  view.levelDb = -17;
  view.holdDb = -9;
  view.peakDb = -9;
  for (int i = 0; i < SPEC_BARS; i++) view.spec[i] = (uint8_t)(30 + 150 * fabsf(sinf(i * 0.37f)) * (1.0f - i / 48.0f));
  static const uint8_t busy[13] = { 55, 30, 12, 8, 20, 70, 62, 25, 10, 6, 34, 4, 2 };
  memcpy(view.chanBusy, busy, 13);

  {   // приёмники для примера
    static const char *const NM[] = { "Зал, 3 ряд", "Бабуся Ганна", "Приймач 4A94", "Балкон", "Микола Іванович" };
    static const int8_t RS[] = { -52, -66, -74, -58, -80 };
    view.rxN = 5;
    for (int i = 0; i < 5; i++) {
      View::Rx &r = view.rx[i];
      r.id[0] = 0x88; r.id[1] = 0x4A; r.id[2] = 0x90 + i;
      snprintf(r.name, sizeof(r.name), "%s", NM[i]);
      r.online = i != 4; r.off = i == 3; r.sleep = i == 3; r.mute = false;
      r.volume = 10 + i * 2; r.rssi = RS[i]; r.depthMs = 8; r.lossPm = 4 + i * 3; r.fw = 4; r.uptimeMin = 75 + i * 11;
    }
  }
  {   // кадры появления эмблемы
    static uint8_t ang[LOGO_BIG_W * LOGO_BIG_H];
    const float cx = LOGO_BIG_W * 0.5f, cy = LOGO_BIG_H * 0.40f;
    for (int y = 0; y < LOGO_BIG_H; y++)
      for (int x = 0; x < LOGO_BIG_W; x++) {
        float a = atan2f(x - cx, cy - y);
        if (a < 0) a += 2 * PI;
        ang[y * LOGO_BIG_W + x] = (uint8_t)(a * (255.0f / (2 * PI)));
      }
    logoAngle = ang;
    static const int FR[] = { 9, 17, 25, 33, 44, 52, 62, 80 };
    for (int k = 0; k < 8; k++) {
      splashPrepare();
      for (int f = 0; f <= FR[k]; f++) logoAnimFrame(f, (W - LOGO_BIG_W) / 2, 56);   // как на плате: кадр за кадром
      flush();
      char nm[32];
      snprintf(nm, sizeof(nm), "z%d-kadr-%02d", k, FR[k]);
      save(nm);
    }
    logoAngle = nullptr;
  }
  splashPrepare();
  splash(view.version);
  flush();
  save("00-zastavka");

  needFull = true;
  frame(false, 0, 0);
  save("01-golovna");
  { Box b = itemBox(ID_SRC_TEST); tap(b.x + 40, b.y + 20); }
  save("01a-golovna-perevirka-pytannya");
  { Box b = mItem(ID_M_OK); tap(b.x + 30, b.y + 20); }
  save("01b-golovna-perevirka-yde");
  { Box b = itemBox(ID_SRC_LIVE); tap(b.x + 40, b.y + 20); }
  save("01c-golovna-znovu-z-pulta");
  { Box b = itemBox(ID_MUTE); tap(b.x + 40, b.y + 20); }
  for (int k = 0; k < 12; k++) { fakeMs += 20; frame(false, 0, 0); }
  save("01d-golovna-tysha");
  { Box b = itemBox(ID_MUTE); tap(b.x + 40, b.y + 20); }
  for (int k = 0; k < 12; k++) { fakeMs += 20; frame(false, 0, 0); }
  view.sd = true;
  view.sdFreeMb = 14200;
  view.rec = true;
  view.recS = 754;
  view.clip = true;
  view.levelDb = -2;
  view.holdDb = -0.5f;
  view.peakDb = -0.5f;
  needFull = true;
  frame(false, 0, 0);
  save("02-golovna-zapys-perevant");
  view.clip = false;
  view.rec = false;
  view.levelDb = -60;
  view.holdDb = -60;
  view.peakDb = -60;
  view.silent = true;

  {   // передатчик сам ушёл с занятого канала: сообщение на главной и строка на странице «Ефір»
    view.hopSeq = 1; view.hopResult = 3; view.hopFrom = 1; view.hopTo = 1; view.hopWaitMs = 155; view.hopSecs = 6;
    for (int k = 0; k < 3; k++) frame(false, 0, 0);
    save("21a-zator-shukayu");
    view.hopSeq = 2; view.hopResult = 1; view.hopTo = 7; view.val[P_CHANNEL] = 7; view.hopAgoS = 0;
    for (int k = 0; k < 3; k++) frame(false, 0, 0);
    save("21b-zator-kanal-zmineno");
    view.hopAgoS = 200;
    view.val[P_HOP] = 1;
    tap(40 + 240, 450);
    save("21c-efir-pislya-perekhodu");
    toastUntil = 0;
    tap(40, 450);
    for (int k = 0; k < 3; k++) frame(false, 0, 0);
  }
  tap(40 + 80, 450);
  save("17-pryimachi");
  {   // окно «Оновлення приймачів»: до начала, передача, запись на приёмниках, итог
    Box b = itemBox(ID_OTA_OPEN);
    tap(b.x + 40, b.y + 20);
    view.ota.card = 3;
    for (int k = 0; k < 6; k++) frame(false, 0, 0);
    save("17d-onovlennya-pochatok");
    view.ota.card = 5; snprintf(view.ota.cardVer, sizeof(view.ota.cardVer), "2.22");
    for (int k = 0; k < 6; k++) frame(false, 0, 0);
    save("17d2-onovlennya-ye-na-kartci");
    view.ota.card = 6; snprintf(view.ota.cardFile, sizeof(view.ota.cardFile), "photo.bin");
    for (int k = 0; k < 6; k++) frame(false, 0, 0);
    save("17d3-onovlennya-fayl-ne-godytsya");
    view.ota.card = 5;
    view.ota.stage = 3; view.ota.percent = 43; view.ota.n = 3;
    snprintf(view.ota.it[0].name, sizeof(view.ota.it[0].name), "Зал, другий ряд, біля проходу ліворуч");
    view.ota.it[0].phase = 2; view.ota.it[0].percent = 43; view.ota.it[0].lost = false;
    snprintf(view.ota.it[1].name, sizeof(view.ota.it[1].name), "Приймач 3B10");
    view.ota.it[1].phase = 2; view.ota.it[1].percent = 61; view.ota.it[1].lost = false;
    snprintf(view.ota.it[2].name, sizeof(view.ota.it[2].name), "Балкон");
    view.ota.it[2].phase = 1; view.ota.it[2].lost = false;
    for (int k = 0; k < 10; k++) frame(false, 0, 0);
    save("17e-onovlennya-nadsylayu");
    {   // ход передачи от 0 до 100 %: каждый кадр должен рисоваться (07.10 передатчик с открытым окном перезапустился сторожем)
      int drawn = 0;
      for (int p = 0; p <= 100; p++) {
        view.ota.percent = p; view.ota.it[0].percent = p; view.ota.it[1].percent = p < 80 ? p + 20 : 100;
        for (int k = 0; k < 3; k++) frame(false, 0, 0);
        drawn++;
      }
      view.ota.n = 2;
      for (int p = 0; p <= 100; p += 7) { view.ota.percent = p; for (int k = 0; k < 3; k++) frame(false, 0, 0); }
      view.ota.n = 3;
      printf("вікно оновлення: намальовано %d станів передачі\n", drawn);
    }
    view.ota.stage = 5; view.ota.it[0].phase = 4; view.ota.it[0].percent = 72; view.ota.it[1].phase = 5;
    for (int k = 0; k < 10; k++) frame(false, 0, 0);
    save("17f-onovlennya-zapys");
    view.ota.stage = 6; view.ota.it[0].phase = 5; view.ota.done = 2; view.ota.same = 1;
    for (int k = 0; k < 10; k++) frame(false, 0, 0);
    save("17g-onovlennya-gotovo");
    view.ota.it[0].phase = 6; view.ota.it[0].err = 1; view.ota.fail = 1; view.ota.done = 1;
    for (int k = 0; k < 10; k++) frame(false, 0, 0);
    save("17h-onovlennya-potriben-usb");
    view.ota.stage = 10; view.ota.selfWait = 6; view.ota.selfPct = 0; view.ota.fromCard = true; snprintf(view.ota.cardVer, sizeof(view.ota.cardVer), "2.26");
    needFull = true;
    for (int k = 0; k < 4; k++) frame(false, 0, 0);
    save("17i0-onovlennya-poperedzhennya");
    view.ota.selfWait = 0;
    view.ota.stage = 10; view.ota.selfPct = 37; view.ota.fromCard = true; view.ota.it[0].phase = 4; view.ota.it[0].percent = 20; view.ota.it[0].err = 0;
    for (int k = 0; k < 10; k++) frame(false, 0, 0);
    needFull = true;
    for (int k = 0; k < 4; k++) frame(false, 0, 0);
    save("17i-onovlennya-peredavach-zapysuye");
    view.ota.stage = 11;
    for (int k = 0; k < 10; k++) frame(false, 0, 0);
    save("17j-onovlennya-perezapusk");
    view.ota.stage = 6; view.ota.fromCard = false;
    { Box c = otaBtn(ID_OTA_CLOSE); tap(c.x + 30, c.y + 20); }
    view.ota = View::Ota();
    out.otaStart = out.otaCancel = false;
    needFull = true;
    for (int k = 0; k < 3; k++) frame(false, 0, 0);
  }
  {   // окно «Додати приймач»: сначала пусто, потом две просьбы, потом одну дозволили
    Box b = itemBox(ID_PAIR_OPEN);
    tap(b.x + 40, b.y + 20);
    save("17a-dodaty-chekayu");
    view.askN = 2;
    snprintf(view.ask[0].name, sizeof(view.ask[0].name), "Приймач 4A94");
    view.ask[0].code = 714; view.ask[0].slot = 0;
    snprintf(view.ask[1].name, sizeof(view.ask[1].name), "Приймач 3B10");
    view.ask[1].code = 4821; view.ask[1].slot = 1;
    for (int k = 0; k < 10; k++) frame(false, 0, 0);
    save("17b-dodaty-zapyty");
    { Box o = pairBtn(ID_PAIR_OK); tap(o.x + 30, o.y + 20); }
    printf("дозвіл: місце %d, вікно %s\n", out.pairApprove, modal == M_PAIR ? "відкрите" : "закрите");
    save("17c-dodaty-dozvoleno");
    view.askN = 0;
    out.pairApprove = -1;
    { Box c = pairBtn(ID_PAIR_CLOSE); tap(c.x + 30, c.y + 20); }
    printf("закрито: прийом запитів %d, вікно %s\n", out.pairOpen, modal == M_NONE ? "закрите" : "відкрите");
    out.pairOpen = -1;
    needFull = true;
    frame(false, 0, 0);
  }
  tap(150, 100);
  save("18-pryimach-vikno");
  for (int k = 0; k < view.rxN; k++) view.rx[k].fw = 11;     // приёмники с версии 1.1 умеют стерео
  needFull = true;
  frame(false, 0, 0);
  save("18a-pryimach-vikno-stereo");
  { Box b = rxBtn(ID_RX_STEREO); tap(b.x + 30, b.y + 20); }
  save("18b-pryimach-stereo-uvimkneno");
  // приёмник с версии 2.32 сообщает свои настройки: на месте переключателя выхода — «Налаштування»
  {
    View::Rx &r = view.rx[0];
    r.hasInfo = true;
    r.stereo = true;
    const uint8_t par[6] = { 2, 5 + 2, 14, 1, 2, 0 };   // чёткость «середня», баланс «праве +2», предел 70 %, стрелки, норма, как у передатчика
    memcpy(r.par, par, 6);
    r.online = true;
    r.fw = 23;
    view.ota.stage = 0;
    memcpy(rxSelId, r.id, 3);   // окно приёмника открываем прямо: касания выше попадают в «Оновлення» (кнопка появилась в 2.19)
    modal = M_RX;
    needFull = true;
    frame(false, 0, 0);
    save("18c-pryimach-vikno-2.32");
    { Box b = rxBtn(ID_RX_SET); tap(b.x + 30, b.y + 20); }
    save("18d-pryimach-nalashtuvannya");
    { Box b = rxsBox(ID_RXS_PLUS + 1); tap(b.x + 20, b.y + 20); }     // чёткость: середня → сильна
    { Box b = rxsBox(ID_RXS_MINUS + 0); tap(b.x + 20, b.y + 20); }    // выход: два канали → протифаза (баланс гаснет)
    { Box b = rxsBox(ID_RXS_EAR); tap(b.x + 20, b.y + 20); }          // проверка наушников
    printf("налаштування приймача: команда %d, аргумент %d\n", out.rxCmd, out.rxArg);
    save("18e-nalashtuvannya-pislya-natyskan");
    { Box b = rxsBox(ID_RXS_OK); tap(b.x + 20, b.y + 20); }
    save("18f-nazad-u-vikno-pryimacha");
    r.hasInfo = false;
  }
  tap(340, 290);   // «Перейменувати»
  save("19-klaviatura");
  modal = M_NONE;
  needFull = true;
  frame(false, 0, 0);
  tap(40 + 160, 450);
  save("03-zvuk");
  tap(120, 245);
  save("04-zvuk-pidsylennya");
  tap(120, 400);   // «Скасувати» — где бы ни стояла, после закроем принудительно
  modal = M_NONE;
  needFull = true;
  frame(false, 0, 0);
  tap(360, 245);
  save("05-zvuk-dzherelo");
  modal = M_NONE;
  needFull = true;
  frame(false, 0, 0);
  OPT_TRACK[1] = "Morning in the Conservatory";
  OPT_TRACK[2] = "Ранкова прогулянка";
  OPT_TRACK[3] = nullptr;
  view.val[P_TONE] = 5;
  needFull = true;
  frame(false, 0, 0);
  tap(240, 307);
  save("05c-zvuk-perevirochnyi-vikno");
  { Box b = mItem(ID_SHEET + 0); tap(b.x + 100, b.y + 20); }
  save("05d-perevirochnyi-rezhym");
  { Box b = mItem(ID_M_OPT + 2); tap(b.x + 100, b.y + 20); }      // «Голос»
  save("05e-vikno-pislya-vyboru-golos");
  { Box b = mItem(ID_SHEET + 1); tap(b.x + 100, b.y + 20); }
  save("05f-perevirochnyi-melodiya");
  { Box b = mItem(ID_M_OPT + 2); tap(b.x + 100, b.y + 20); }
  { Box b = mItem(ID_SHEET + 2); tap(b.x + 100, b.y + 20); }
  save("05g-perevirochnyi-period");
  { Box b = mItem(ID_M_CANCEL); tap(b.x + 30, b.y + 20); }
  { Box b = mItem(ID_SHEET + 3); tap(b.x + 100, b.y + 20); }
  save("05h-perevirochnyi-fon");
  { Box b = mItem(ID_M_OK); tap(b.x + 30, b.y + 20); }
  save("05i-vikno-znovu");
  { Box b = mItem(ID_M_OK); tap(b.x + 30, b.y + 20); }
  save("05j-zvuk-pislya-vikna");
  view.val[P_TONE] = 0;
  needFull = true;
  frame(false, 0, 0);
  view.val[P_STEREO] = 1;
  stereoNow = true;
  needFull = true;
  frame(false, 0, 0);
  save("05k-zvuk-stereo");
  tap(390, 369);
  save("05l-efir-mono-stereo");
  modal = M_NONE;
  view.val[P_STEREO] = 0;
  stereoNow = false;
  needFull = true;
  frame(false, 0, 0);
  tap(150, 369);
  save("05a-zvuk-yakist");
  modal = M_NONE;
  view.val[P_QUALITY] = 0;
  qualNow = 0;
  needFull = true;
  frame(false, 0, 0);   // страница нарисована с «найвища»
  qualNow = 2;          // затем скорость стала мала — в эфир идёт «мова»
  for (int k = 0; k < 10; k++) { fakeMs += 20; frame(false, 0, 0); }   // без полной перерисовки и без нажатия: строка обязана обновиться сама
  save("05b-zvuk-yakist-znyzhena");
  qualNow = -1;
  needFull = true;
  frame(false, 0, 0);

  tap(40 + 240, 450);
  save("06-efir-ne-miryaly");
  view.scanned = true;
  needFull = true;
  frame(false, 0, 0);
  save("07-efir");
  tap(240, 245);
  save("08-efir-poshuk-pytannya");
  modal = M_NONE;
  needFull = true;
  frame(false, 0, 0);
  tap(100, 310);
  save("09-efir-kanal");
  modal = M_NONE;
  needFull = true;
  frame(false, 0, 0);
  tap(100, 370);
  save("10-efir-shvydkist");
  modal = M_NONE;
  needFull = true;
  frame(false, 0, 0);

  tap(40 + 320, 450);
  save("11-nalashtuvannya");
  tap(240, 220);
  save("12-pro-prystriy");
  modal = M_NONE;
  needFull = true;
  frame(false, 0, 0);
  tap(350, 300);
  save("13-skynuty-pytannya");
  modal = M_NONE;
  needFull = true;
  frame(false, 0, 0);

  tap(40 + 400, 450);
  save("14-dovidka");
  tap(240, 150);
  save("15-dovidka-stattya");
  tap(430, 375);
  save("16-dovidka-stattya-2");
  {   // движение значков: шесть кадров на трёх вкладках через 0,12 с
    view.silent = false;
    view.levelDb = -20;
    static const int PG[] = { PG_RX, PG_SOUND, PG_AIR };
    for (int p = 0; p < 3; p++) {
      tap(40 + PG[p] * 80, 450);
      for (int k = 0; k < 6; k++) {
        for (int f = 0; f < 6; f++) {
          fakeMs += 20;
          frame(false, 0, 0);
        }
        char nm[32];
        snprintf(nm, sizeof(nm), "an-%d-%d", p, k);
        save(nm);
      }
    }
  }
  drawStandby(0.45f, true);
  {   // сколько точек переносится на экран за кадр, когда никто ничего не трогает
    modal = M_NONE;
    static const char *const PN[] = { "Головна", "Приймачі", "Звук", "Ефір", "Налашт.", "Довідка" };
    for (int pg = 0; pg < PG_N; pg++) {
      page = pg;
      article = -1;
      needFull = true;
      frame(false, 0, 0);
      flushPx = 0;
      fullDraws = 0;
      view.rxN = 1;
      view.rx[0].online = true;
      view.rx[0].off = false;
      view.radioOk = true;
      auto c0 = std::chrono::steady_clock::now();
      for (int k = 0; k < 250; k++) {     // пять секунд
        view.rx[0].rssi = -40 - (k / 25) % 3 * 7;   // сигнал приёмника гуляет, как в жизни
        fakeMs += 20;
        view.uptimeS = fakeMs / 1000;
        view.levelDb = -20 + 8 * sinf(k * 0.21f);
        view.holdDb = view.levelDb + 3;
        for (int b = 0; b < SPEC_BARS; b++) view.spec[b] = 90 + 60 * sinf(k * 0.3f + b);
        frame(false, 0, 0);
      }
      double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - c0).count() / 250;
      printf("покой, «%s»: %u точек за кадр, целиком страница рисовалась %u раз за 250 кадров, на этом компьютере %.0f мкс на кадр\n", PN[pg],
             (unsigned)(flushPx / 250), (unsigned)fullDraws, us);
    }
    page = PG_HOME;
    needFull = true;
    frame(false, 0, 0);
  }
  save("20-vymkneno");
  {   // настройки: шесть строк, переименование источника звука
    modal = M_NONE;
    toastUntil = 0;
    page = PG_SETUP;
    needFull = true;
    for (int k = 0; k < 3; k++) frame(false, 0, 0);
    save("22a-nalashtuvannya");
    { Box b = itemBox(ID_SRCNAME); tap(b.x + 40, b.y + 20); }
    save("22b-nazva-dzherela-klaviatura");
    {   // случай владельца 07.10: стереть всё и набрать буквы верхнего ряда — они должны появляться в поле
      snprintf(kbText, sizeof(kbText), "Зал");
      kbShift = false;
      needFull = true;
      for (int k = 0; k < 3; k++) frame(false, 0, 0);
      auto key = [&](const char *label, int kind) {
        for (int i = 0; i < keyN; i++)
          if (keys[i].kind == kind && (kind != 0 || !strcmp(keys[i].label, label))) {
            tap(keys[i].b.x + keys[i].b.w / 2, keys[i].b.y + keys[i].b.h / 2);
            for (int k = 0; k < 8; k++) frame(false, 0, 0);   // отложенная перерисовка подписей
            return;
          }
        printf("НЕТ КЛАВИШИ %s\n", label);
      };
      for (int n = 0; n < 3; n++) key("", 2);
      printf("після стирання: «%s», заголовні %d\n", kbText, (int)kbShift);
      save("22c-klaviatura-sterto");
      key("з", 0);
      save("22d-klaviatura-Z");
      for (const char *l : { "у", "ш", "х", "ї", "й", "ц" }) key(l, 0);
      printf("набрано: «%s»\n", kbText);
      save("22e-klaviatura-verkhniy-ryad");
      // палец лежит на клавише: подсветка и увеличенная буква
      for (int i = 0; i < keyN; i++)
        if (keys[i].kind == 0 && !strcmp(keys[i].label, "к")) {
          ui::frame(true, keys[i].b.x + 19, keys[i].b.y + 26);
          save("22f-klaviatura-palets-na-k");
          ui::frame(false, keys[i].b.x + 19, keys[i].b.y + 26);
          ui::frame(false, 0, 0);
        }
      save("22g-klaviatura-pislya-k");
    }
    snprintf(kbText, sizeof(kbText), "Мікрофон");
    kbLayout();
    for (int i = 0; i < keyN; i++)
      if (keys[i].kind == 6) { tap(keys[i].b.x + 20, keys[i].b.y + 20); break; }
    printf("джерело: «%s», прохання %d\n", out.srcName, (int)out.setSrcName);
    out.setSrcName = false;
    page = PG_HOME;
    needFull = true;
    for (int k = 0; k < 3; k++) frame(false, 0, 0);
    save("22c-golovna-z-nazvoyu");
    view.srcName[0] = 0;
  }
  {   // английский язык: все страницы и главные окна — проверить, что надписи помещаются
    uiLang = 1;
    view.val[P_LANG] = 1;
    modal = M_NONE;
    toastUntil = 0;
    view.rxN = 5;
    for (int i = 0; i < 5; i++) { view.rx[i].online = i != 4; view.rx[i].off = i == 3; view.rx[i].sleep = i == 3; view.rx[i].fw = 21; }
    static const char *const EN[] = { "en-1-home", "en-2-receivers", "en-3-sound", "en-4-air", "en-5-settings", "en-6-help" };
    for (int pg = 0; pg < PG_N; pg++) {
      page = pg;
      article = -1;
      needFull = true;
      for (int k = 0; k < 3; k++) frame(false, 0, 0);
      save(EN[pg]);
    }
    page = PG_HELP;
    article = 3;
    articlePage = 0;
    needFull = true;
    frame(false, 0, 0);
    save("en-7-help-article");
    article = -1;
    page = PG_RX;
    needFull = true;
    frame(false, 0, 0);
    tap(150, 100);
    save("en-8-receiver-window");
    { Box b = rxBtn(ID_RX_FORGET); tap(b.x + 30, b.y + 20); }
    save("en-9-remove-confirm");
    modal = M_NONE;
    needFull = true;
    frame(false, 0, 0);
    { Box b = itemBox(ID_PAIR_OPEN); tap(b.x + 40, b.y + 20); }
    view.askN = 1;
    snprintf(view.ask[0].name, sizeof(view.ask[0].name), "Receiver 4A94");
    view.ask[0].code = 714;
    for (int k = 0; k < 10; k++) frame(false, 0, 0);
    save("en-10-add-receiver");
    view.askN = 0;
    modal = M_NONE;
    page = PG_SETUP;
    needFull = true;
    frame(false, 0, 0);
    { Box b = itemBox(ID_ROW + P_LANG); tap(b.x + 40, b.y + 20); }
    save("en-11-language");
    modal = M_NONE;
    page = PG_SOUND;
    needFull = true;
    frame(false, 0, 0);
    { Box b = itemBox(ID_ROW + P_QUALITY); tap(b.x + 40, b.y + 20); }
    save("en-12-quality");
    modal = M_NONE;
    page = PG_HOME;
    view.hopSeq = 9; view.hopResult = 1; view.hopFrom = 1; view.hopTo = 6; view.hopWaitMs = 155;
    needFull = true;
    for (int k = 0; k < 4; k++) frame(false, 0, 0);
    save("en-13-channel-changed");
    uiLang = 0;
  }
  {   // файловый менеджер карты памяти
    auto run = [&](int n) { for (int k = 0; k < n; k++) { fakeMs += 20; frame(false, 0, 0); } };
    auto hit = [&](int id) { Box b = modal == M_FILES ? fmBox(id) : itemBox(id); tap(b.x + b.w / 2, b.y + b.h / 2); run(3); };
    modal = M_NONE; page = PG_HOME; view.sd = true; view.rec = false; view.sdFreeMb = 196; view.clip = false; view.val[P_TONE] = 0;
    fakeMs += 20000; needFull = true; run(4);
    save("fm-00-golovna");
    hit(ID_HOME_FILES); save("fm-01-spysok");
    hit(ID_FM_ROW + 3); save("fm-02-vikno-fayla");
    hit(ID_FM_PLAY); save("fm-03-pishov-v-efir");
    fakeMs += 5000; run(3); save("fm-04-fayl-v-efiri");
    hit(ID_FM_NEXT); save("fm-05-druha-storinka");
    hit(ID_FM_ROW + 0); save("fm-06-vikno-tekstu");
    hit(ID_FM_VIEW); save("fm-07-tekst");
    hit(ID_FM_X); hit(ID_FM_X); hit(ID_FM_PREV);
    hit(ID_FM_ROW + 4); hit(ID_FM_COPY); save("fm-08-kopiyuvannya-vybrano");
    hit(ID_FM_ROW + 1); save("fm-09-vstavyty-syudy");
    fmSt = FS_BUSY; fmBusyOp = FM_COPY; fm.progress = 37; fmDraw(); flush(); save("fm-10-kopiyuyu");
    fmSt = FS_LIST; fmBusyOp = 0; fmClip[0] = 0; fmDraw(); flush();
    hit(ID_FM_MORE); save("fm-11-inshe");
    hit(ID_FM_RMDIR); save("fm-12-vydalyty-teku");
    hit(ID_FM_NO); hit(ID_FM_MORE); hit(ID_FM_FORMAT); save("fm-13-format-1");
    hit(ID_FM_YES); save("fm-14-format-2");
    hit(ID_FM_NO); hit(ID_FM_BACK);
    hit(ID_FM_ROW + 8 - 6 + 6 > 5 ? ID_FM_ROW + 5 : ID_FM_ROW + 5); save("fm-15-vikno-wav");
    hit(ID_FM_DEL); save("fm-16-vydalyty-fayl");
    hit(ID_FM_NO); hit(ID_FM_ROW + 2); save("fm-17-porozhnya-teka");
    hit(ID_FM_NEWDIR); save("fm-18-nova-teka");
    kbForDir = false; fmNewDir(NULL); run(2);
    hit(ID_FM_CLOSE);
    view.fileOn = true; needFull = true; run(3); save("fm-19-golovna-fayl-v-efiri");
    view.fileOn = false; fm.playing = false;
    view.val[P_TONE] = 5; page = PG_SOUND; needFull = true; run(3); save("zz-zvuk-perevirka-vognyk");
    page = PG_HOME; needFull = true; run(3); save("zz-golovna-perevirka-vognyk");
    // живые огоньки: четыре кадра с шагом 0,18 с — проверочный звук, затем тишина, затем обычный вход
    for (int k = 0; k < 4; k++) { char nm[32]; snprintf(nm, sizeof(nm), "zy-vognyk-test-%d", k); for (int j = 0; j < 9; j++) { fakeMs += 20; frame(false, 0, 0); } save(nm); }
    view.muted = true; needFull = true; run(2);
    for (int k = 0; k < 4; k++) { char nm[32]; snprintf(nm, sizeof(nm), "zy-vognyk-tysha-%d", k); for (int j = 0; j < 9; j++) { fakeMs += 20; frame(false, 0, 0); } save(nm); }
    view.muted = false; view.val[P_TONE] = 0; view.rec = true; view.recS = 75; needFull = true; run(2);
    for (int k = 0; k < 4; k++) { char nm[32]; snprintf(nm, sizeof(nm), "zy-vognyk-vhid-%d", k); for (int j = 0; j < 9; j++) { fakeMs += 20; frame(false, 0, 0); } save(nm); }
    view.rec = false;
    view.val[P_TONE] = 0;
  }
  return 0;
}
