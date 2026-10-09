// Экран передатчика на модуле ESP32-4848S040: связь меню (txui.h) с железом.
//
// Меню само ничего не знает о плате: здесь ему 50 раз в секунду передаётся, что показывать (уровень, спектр,
// состояние радио, карта памяти), а его просьбы (сменить настройку, найти канал, начать запись) выполняются.
#pragma once
#include "esp_sleep.h"
#include "panel4848.h"
#include "txaudio.h"
#include "txscan.h"
#include "txrec.h"
#include "upd.h"      // обновление с карты памяти: файл прошивки в папке UPDATE (нужны и карта, и ota.h)
#include "txpeers.h"
#include "txui.h"
#include "esp_dsp.h"

void settingsSave();      // в hearlink.ino
void settingsFactory();

namespace ui {
void uiWaitFrame() {
  lcdWaitFrame();
}
bool uiSlide(const uint16_t *b, int off, int dir, int y0, int y1) {
  return lcdSlide(b, off, dir, y0, y1);
}
void uiQuiet(bool on) {
  mpQuiet = on;
}
void uiBreath() {   // передышка в большой копии между листами экрана: дать шине памяти подгрузить порцию кадра
  delayMicroseconds(70);
}
void uiSlideSync() {
  for (int i = 0; i < 4 && lcdSlideSeen != lcdSlideReq; i++) lcdWaitFrame();
}
bool uiFm(int op, const char *a, const char *b) {
  return fmAsk(op, a, b);
}
void uiDelay(int ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}
}

namespace txscreen {
static volatile bool lcdOk, touchOk, touchDebug;
static volatile int lastX = -1, lastY = -1;
static volatile uint32_t frameSumUs, frameN, frameMaxUs;
static volatile uint32_t specSumUs, specN;   // сколько времени уходит на разбор звука для спектра
static volatile uint32_t loopSumUs, loopN;   // и на весь круг экрана (сбор показаний + разбор + рисование)
static volatile uint32_t splashShownMs;   // сколько заставка была на экране при горящей подсветке
static volatile int pageAsk = -1;         // с порта попросили открыть вкладку (для замеров); 9 — файловый менеджер карты
static volatile int tapAsk = -1;          // с порта «нажали» кнопку с этим номером (Ut<номер>) — проверка экрана без рук
// клавиатура: что набрано и как быстро разбираются касания (порт: Uk)
static void kbReport() {
  Serial.printf("клавіатура %s; набрано «%s»; уведено %u; найдовша обробка натискання %u мкс, відпускання %u мкс; подій сенсора втрачено %u\n",
                ui::modal == ui::M_KEYS ? "open" : "closed", ui::kbText, (unsigned)ui::kbTaps, (unsigned)ui::kbPressUs, (unsigned)ui::kbReleaseUs,
                (unsigned)touchLost);
  ui::kbPressUs = 0;
  ui::kbReleaseUs = 0;
}
static volatile bool fmStateAsk;          // с порта спросили, что на экране файлового менеджера (Uq)
static volatile bool offAsk;              // с порта попросили выключить передатчик (проверка без рук)
static const uint8_t RATE_IDX[9] = { 8, 7, 6, 4, 3, 2, 1, 0, 12 };   // 24, 18, 12, 6, 11, 5,5, 2, 1 и 0,5 Мбит/с в таблице RATES
static const uint8_t DIM_MIN[4] = { 0, 1, 5, 15 };

// Спектр входа — алгоритм из «ПОТУЖНОГО РАДІО» (yoSpectrum): 2048 отсчётов, окно Ханна, преобразование Фурье,
// 32 полосы с логарифмическим шагом 50 Гц … 16 кГц (узкие нижние берутся между соседними отсчётами),
// наклон 4 дБ на октаву, шкала идёт за громкостью (верх — недавний пик, низ — на 30 дБ ниже), уровень
// усредняется по времени и риска едет к нему мягко: вверх быстрее, вниз медленнее.
// Само преобразование — из esp-dsp, на векторных инструкциях ESP32-S3.
static float *fftBuf, *fftWin;
static float spAvg[ui::SPEC_BARS], spSmooth[ui::SPEC_BARS], spRef = -30;
static bool spRefOk;
static uint32_t spLastT;

static void specInit() {
  // Во внутренней памяти (12 КБ): во внешней, где раньше лежали эти листы, разбор шёл в разы медленнее, и вкладка
  // «Звук» давала 26 кадров в секунду вместо 50 (замер 07.10).
  fftBuf = (float *)heap_caps_aligned_alloc(16, TX_SPEC_N * 2 * sizeof(float), MALLOC_CAP_INTERNAL);
  fftWin = (float *)heap_caps_malloc(TX_SPEC_N * sizeof(float), MALLOC_CAP_INTERNAL);
  dsps_fft2r_init_fc32(NULL, TX_SPEC_N);
  for (int i = 0; i < TX_SPEC_N; i++) fftWin[i] = (0.5f - 0.5f * cosf(2 * PI * i / (TX_SPEC_N - 1))) / 32768.0f;
}

// Спектр считается в два приёма. specUpdate — разбор звука (теперь на каждом кадре) — даёт, куда рискам идти;
// specAnimate — каждый кадр подвигает риски к этим значениям: вверх за 45 мс, вниз за 150 мс.
// Раньше риски переставлялись сразу на новое место раз в два кадра (12–15 раз в секунду) — владелец: «рывкообразная
// работа». После первой правки (45 / 150 мс): «не хватает плавности столбцов, они всё ещё дёргаются слишком при
// небольшом изменении гармоник» (07.10). Вторая правка (90 / 400 мс и усреднение разбора за 0,2 с) сделала спектр
// вялым: «скорость анализатора верни назад. и увеличь количество опросов — это даст динамичность и плавность».
// Итог: скорость рисок прежняя (45 / 150 мс); разбор — на каждом кадре (вдвое чаще), берётся вся мощность полосы
// (один наибольший отсчёт скачет от кадра к кадру) с коротким усреднением (0,06 с); рисование не трогает риску,
// пока она не сдвинулась больше чем на точку.
static void specAnimate(uint8_t *out) {
  static uint32_t last;
  uint32_t now = millis();
  float dt = last ? (now - last) / 1000.0f : 0.03f;
  last = now;
  if (dt > 0.2f) dt = 0.2f;
  float up = 1 - expf(-dt / 0.045f), dn = 1 - expf(-dt / 0.15f);
  for (int b = 0; b < ui::SPEC_BARS; b++) {
    spSmooth[b] += (spAvg[b] - spSmooth[b]) * (spAvg[b] > spSmooth[b] ? up : dn);
    if (spSmooth[b] < 0.01f && spAvg[b] < 0.01f) spSmooth[b] = 0;
    out[b] = (uint8_t)(spSmooth[b] * 200);
  }
}

// Полосы спектра посчитаны заранее: с какого отсчёта по какой, поправка наклона, и доля для узких полос.
struct SpecBand {
  uint16_t i0, i1;   // отсчёты [i0, i1) — широкая полоса; i1 == 0 — узкая: между отсчётами i0 и i0 + 1
  float frac, tilt;  // доля между двумя отсчётами; поправка +4 дБ на октаву
};
static SpecBand spBand[ui::SPEC_BARS];
static bool spBandOk;
static void specBands() {
  const int N = TX_SPEC_N, n = ui::SPEC_BARS;
  const float fmin = 50, fmax = 16000, sr = SRATE;
  for (int b = 0; b < n; b++) {
    float f0 = fmin * powf(fmax / fmin, (float)b / n), f1 = fmin * powf(fmax / fmin, (float)(b + 1) / n);
    float fc = sqrtf(f0 * f1), b0 = f0 * N / sr, b1 = f1 * N / sr;
    SpecBand &s = spBand[b];
    s.tilt = 4 * log2f(fc / 1000.0f);
    if (b1 - b0 < 1.5f) {
      float fb = fc * N / sr;
      int i = (int)fb;
      s.frac = fb - i;
      s.i0 = i < 1 ? 1 : i >= N / 2 - 1 ? N / 2 - 2 : i;
      s.i1 = 0;
    } else {
      int i0 = (int)b0, i1 = (int)b1;
      s.i0 = i0 < 1 ? 1 : i0;
      s.i1 = i1 > N / 2 ? N / 2 : i1;
      s.frac = 0;
    }
  }
  spBandOk = true;
}

static void specUpdate() {
  const int N = TX_SPEC_N, n = ui::SPEC_BARS;
  const float range = 30, head = 6, decay = 5, avgK = 0.7f;
  uint32_t tSpec = micros();
  if (!spBandOk) specBands();
  uint32_t now = millis();
  float dt = spLastT ? (now - spLastT) / 1000.0f : 0.05f;
  spLastT = now;
  if (dt > 0.3f) dt = 0.3f;
  uint32_t pos = txSpecPos;
  for (int i = 0; i < N; i++) {
    fftBuf[2 * i] = txSpecBuf[(pos + i) & (N - 1)] * fftWin[i];
    fftBuf[2 * i + 1] = 0;
  }
  dsps_fft2r_fc32(fftBuf, N);
  dsps_bit_rev_fc32(fftBuf, N);
  auto mag2 = [&](int i) {
    return fftBuf[2 * i] * fftBuf[2 * i] + fftBuf[2 * i + 1] * fftBuf[2 * i + 1];
  };
  float lv[ui::SPEC_BARS], dbs[ui::SPEC_BARS], fm = -200;
  float aAvg = 1 - expf(-dt / 0.06f);   // короткое усреднение по времени, 0,06 с
  for (int b = 0; b < n; b++) {
    const SpecBand &sb = spBand[b];
    float p = 0;
    if (!sb.i1) {
      float q0 = sqrtf(mag2(sb.i0)), q1 = sqrtf(mag2(sb.i0 + 1)), m = q0 + (q1 - q0) * sb.frac;
      p = m * m;
    } else {
      for (int i = sb.i0; i < sb.i1; i++) p += mag2(i);   // вся мощность полосы: один наибольший отсчёт скачет от кадра к кадру
      p /= 1.5f;                                           // окно Ханна размазывает тон на полтора отсчёта
    }
    {
      static float pAvg[ui::SPEC_BARS];
      pAvg[b] += (p - pAvg[b]) * aAvg;
      p = pAvg[b];
    }
    float amp = sqrtf(p) / (N / 4.0f);
    dbs[b] = 20 * log10f(amp + 1e-9f) + sb.tilt;
    if (dbs[b] > fm) fm = dbs[b];
  }
  if (!spRefOk) {
    spRef = fm;
    spRefOk = true;
  } else if (fm > spRef) {
    float a = dt / 0.25f;
    spRef += (fm - spRef) * (a > 1 ? 1 : a);
  } else spRef -= decay * dt;
  if (spRef < -50) spRef = -50;   // тихое не раздуваем до шума
  for (int b = 0; b < n; b++) {
    float l = (dbs[b] - (spRef - range)) / (range + head);
    lv[b] = l < 0 ? 0 : l > 1 ? 1 : l;
    float k = dt / 0.045f, av = avgK * k;
    spAvg[b] += (lv[b] - spAvg[b]) * (av > 1 ? 1 : av);
  }
  specSumUs = specSumUs + (micros() - tSpec);
  specN = specN + 1;
}

static void report() {
  // (печать касаний больше не переключается самой командой u: её включают и выключают «ud» — иначе после нечётного
  // числа отчётов она оставалась включённой и на каждое касание в порт шло по 125 строк в секунду)
  Serial.printf("екран: %s; сенсор: %s (адреса 0x%02X); останній дотик x=%d y=%d; друк дотиків %s\n", lcdOk ? "запущено" : "НЕ ЗАПУЩЕНО",
                touchOk ? tr("знайдено") : tr("НЕ ЗНАЙДЕНО"), gtAddr, (int)lastX, (int)lastY, touchDebug ? tr("увімкнено") : tr("вимкнено"));
  uint32_t n = frameN, sum = frameSumUs, mx = frameMaxUs;
  frameN = 0;
  frameSumUs = 0;
  frameMaxUs = 0;
  Serial.printf("заставка була на екрані %.1f с (кадр емблеми: сер. %u мс, найдовший %u мс); ", splashShownMs / 1000.0f,
                (unsigned)ui::splashFrameAvgMs, (unsigned)ui::splashFrameMaxMs);
  {
    uint32_t sn = specN, ss = specSumUs, ln = loopN, ls = loopSumUs;
    specN = 0;
    specSumUs = 0;
    loopN = 0;
    loopSumUs = 0;
    Serial.printf("коло екрана: сер. %.2f мс (усе разом: показники, розбір спектра, малювання); розбір спектра: сер. %.2f мс, %u разів\n", ln ? ls / 1000.0f / ln : 0.0f,
                  sn ? ss / 1000.0f / sn : 0.0f, (unsigned)sn);
  }
  {
    static uint32_t lf, lt;
    uint32_t f = lcdFrames, t = millis();
    Serial.printf("екран: %.1f кадрів за секунду%s; останнє перегортання сторінки: %u кроків за %u мс, найдовший крок %.1f мс\n",
                  lt && t != lt ? (f - lf) * 1000.0f / (t - lt) : 0.0f, lcdVsyncOk ? "" : " (ЗНАКУ ПОЧАТКУ КАДРУ НЕМАЄ)", (unsigned)ui::slideFrames,
                  (unsigned)ui::slideMs, ui::slideMaxUs / 1000.0f);
    uint32_t fm = lcdFillMaxUs;
    lcdFillMaxUs = 0;
    {
      static uint32_t lastSum, lastN, lastMs;
      uint32_t sum = lcdFillSumUs, cnt = lcdFills, nowMs = millis();
      if (lastMs && nowMs != lastMs)
        Serial.printf("вивід кадру забирає %.0f %% другого ядра (порцій %u за секунду, сер. %u мкс)\n", (sum - lastSum) / 10.0f / (nowMs - lastMs),
                      (unsigned)((cnt - lastN) * 1000 / (nowMs - lastMs)), cnt != lastN ? (unsigned)((sum - lastSum) / (cnt - lastN)) : 0);
      lastSum = sum;
      lastN = cnt;
      lastMs = nowMs;
    }
    Serial.printf("вивід кадру: %s; найдовша порція %u мкс (межа 457); за останнє перегортання кадрів у русі %u, з них повторених %u\n",
                  lcdOwnFill ? "свій (сторінки зсуває екран)" : "ДРАЙВЕРА (без руху сторінок)", (unsigned)fm, (unsigned)lcdSlideShown, (unsigned)lcdSlideRepeat);
    Serial.printf("  у русі: порцій %u, сер. %u мкс, найдовша %u мкс, довших за крок розгортки %u, навздогін %u; у спокої з минулого звіту: довших за крок %u, навздогін %u; спосіб %u\n",
                  (unsigned)lcdSlFills, (unsigned)(lcdSlFills ? lcdSlSumUs / lcdSlFills : 0), (unsigned)lcdSlMaxUs, (unsigned)lcdSlOver, (unsigned)lcdSlChase,
                  (unsigned)lcdIdleOver, (unsigned)lcdIdleChase, lcdSlideMode);
    Serial.printf("  черга буферів: драйвер збивався %u разів (зараз %s), порцій він велів покласти не в той буфер %u; покладено в буфер, що саме передається, %u; буфер вибирає %s\n",
                  (unsigned)lcdBbFlips, lcdBbFlipped ? "ЗБИТИЙ" : "у черзі", (unsigned)lcdBbWrong, (unsigned)lcdBbHit,
                  lcdBbTrustDriver ? "ДРАЙВЕР (дослід Y10)" : !lcdBb[1] ? "драйвер (свій вивід не запущено)" : "прошивка — за місцем порції");
    lcdIdleOver = 0;
    lcdIdleChase = 0;
    lf = f;
    lt = t;
  }
  Serial.printf("затримки радіо: обробка прийнятого пакета до %u мкс (вид 0x%04X), переналаштувань радіо %u (до %u мкс), сам виклик надсилання до %u мкс\n",
                (unsigned)dgRecvMaxUs, (unsigned)dgRecvMagic, (unsigned)dgApplyN, (unsigned)dgApplyMaxUs, (unsigned)dgSendMaxUs);
  dgRecvMaxUs = 0;
  dgApplyMaxUs = 0;
  dgApplyN = 0;
  dgSendMaxUs = 0;
  Serial.printf("вікно: затемнення сторінки до %.1f мс, усе малювання вікна до %.1f мс; розбір звуку піднімався понад екран %u разів\n",
                ui::dgDimUs / 1000.0f, ui::dgModalUs / 1000.0f, (unsigned)mpBoosts);
  ui::dgDimUs = 0;
  ui::dgModalUs = 0;
  mpBoosts = 0;
  Serial.printf("кадр меню: сер. %.2f мс, найдовший %.1f мс (за %u кадрів); картка пам'яті: %s%s; вільно пам'яті %u КБ, PSRAM %u КБ\n",
                n ? sum / 1000.0f / n : 0.0f, mx / 1000.0f, (unsigned)n, recSd ? tr("є") : recCard == CARD_BAD_FS ? tr("не та розмітка") : recCard == CARD_NO_INIT ? tr("не запускається") : tr("не відповідає"), recOn ? tr(", іде запис") : "",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));
  static const char *const PN[] = { "Головна", "Приймачі", "Звук", "Ефір", "Налаштування", "Довідка" };
  Serial.printf("відкрито: сторінка «%s», вікно %d; на екран переноситься %u точок за кадр, сторінка цілком малювалась %u разів\n",
                PN[ui::page < ui::PG_N ? ui::page : 0], ui::modal, (unsigned)(n ? ui::flushPx / n : 0), (unsigned)ui::fullDraws);
  ui::flushPx = 0;
  ui::fullDraws = 0;
  if (scanDone) {
    Serial.print("зайнятість каналів 1…13, %:");
    for (int i = 0; i < 13; i++) Serial.printf(" %u", scanBusy[i]);
    Serial.printf(" → канал %u\n", cfg.channel);
  }
}

static void task(void *) {
  uint32_t m0 = ESP.getFreeHeap();
  lcdOk = lcdBegin();
  uint32_t m1 = ESP.getFreeHeap();
  touchOk = touchBegin();
  if (touchOk) touchTaskBegin();
  if (!lcdOk) {
    Serial.println("екран не запустився (потрібна збірка з PSRAM)");
    vTaskDelete(NULL);
  }
  uint16_t *scratch = (uint16_t *)heap_caps_aligned_alloc(16, ui::W * ui::H * 2, MALLOC_CAP_SPIRAM);   // теневой лист экрана
  ui::begin(lcdFb, scratch);
  uint32_t m2 = ESP.getFreeHeap();
  specInit();
  uint32_t m3 = ESP.getFreeHeap();
  recBegin();
  Serial.printf("внутрішня пам'ять, КБ: до екрана %u, після екрана %u, після меню %u, після спектра %u\n", (unsigned)(m0 / 1024), (unsigned)(m1 / 1024),
                (unsigned)(m2 / 1024), (unsigned)(m3 / 1024));
  ui::View &v = ui::view;
  v.version = FW_VERSION;
  snprintf(v.mac, sizeof(v.mac), "%s", WiFi.macAddress().c_str());
  // Заставка: экран залит фоном → включается подсветка → эмблема и надписи проявляются при горящем экране.
  // (В версии 0.2 подсветка включалась после заставки, и её было видно одно мгновение.)
  uint32_t splashMs = 0;
  if (!quickBoot) {   // быстрый перезапуск после вставшего радио — без заставки: эфир уже идёт, меню сразу
    ui::splashPrepare();
    for (int b = 0; b <= cfg.brightness; b += 4) {
      lcdBacklight(b);
      vTaskDelay(pdMS_TO_TICKS(6));
    }
    lcdBacklight(cfg.brightness);
    uint32_t splashStart = millis();
    ui::splash(FW_VERSION);
    splashMs = millis() - splashStart;
    for (int b = cfg.brightness; b >= 0; b -= 8) {   // к главной странице — через короткое затемнение
      lcdBacklight(b);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  lcdBacklight(0);
  ui::frame(false, 0, 0);
  for (int b = 0; b <= cfg.brightness; b += 6) {
    lcdBacklight(b);
    vTaskDelay(pdMS_TO_TICKS(8));
  }
  lcdBacklight(cfg.brightness);
  splashShownMs = splashMs;
  txHold = false;   // заставку сменило меню — теперь передача
  if (updatedBoot) {
    static char ut[64];
    snprintf(ut, sizeof(ut), tr("Передавач оновлено: версія %s"), FW_VERSION);
    ui::toast(ut);
  } else if (quickBoot) ui::toast(tr("Радіо перезапущено — ефір відновлено"));
  else if (txMute) ui::toast(tr("Тиша ввімкнена з минулого разу — звук в ефір не йде"));

  float shown = -60, hold = -60, light = cfg.brightness;
  uint32_t holdUntil = 0, lastLoud = millis(), lastTouch = millis(), saveAt = 0, lastSec = 0;
  uint32_t sentPrev = 0;
  bool asleep = false, swallow = false;
  TickType_t tick = xTaskGetTickCount();
  for (;;) {
    uint32_t now = millis(), loopT0 = micros();
    // --- что показывать
    v.val[ui::P_CHANNEL] = cfg.channel;
    v.val[ui::P_KIT] = cfg.kit;
    v.val[ui::P_RATE] = 0;
    if (!cfg.rateAuto)
      for (int i = 0; i < 9; i++)
        if (RATE_IDX[i] == cfg.rateIdx) v.val[ui::P_RATE] = i + 1;
    v.val[ui::P_QUALITY] = cfg.quality;
    v.val[ui::P_STEREO] = cfg.stereo;
    v.val[ui::P_HOP] = cfg.autoHop;
    v.val[ui::P_DEBUG] = cfg.dbgAir;
    v.val[ui::P_LANG] = cfg.lang;
    strlcpy(v.srcName, cfg.srcName, sizeof(v.srcName));
    uiLang = cfg.lang;
    v.hopSeq = hopSeq;
    v.hopResult = hopResult;
    v.hopFrom = hopFrom;
    v.hopTo = hopTo;
    v.hopSecs = hopSecs;
    v.hopWaitMs = hopWaitMs;
    v.hopAgoS = hopAtMs ? (now - hopAtMs) / 1000 : 0;
    ui::stereoNow = txStNow;
    {   // названия мелодий для списка — один раз, когда раздел со звуками прочитан
      static bool tracksSet;
      static char tt[8][56];
      if (!tracksSet && txSoundsOk) {
        tracksSet = true;
        int k = 1;
        for (; k <= 7; k++) {
          int i = assetMusic(k);
          if (i < 0) break;
          if (assetTitles[i][0]) strlcpy(tt[k], assetTitles[i], sizeof(tt[k]));
          else snprintf(tt[k], sizeof(tt[k]), tr("Мелодія %d"), k);
          ui::OPT_TRACK[k] = tt[k];
        }
        ui::OPT_TRACK[k] = nullptr;
      }
    }
    v.val[ui::P_TRACK] = cfg.testTrack;
    v.val[ui::P_DUCK_S] = cfg.duckS;
    v.val[ui::P_DUCK_DB] = cfg.duckDb;
    v.val[ui::P_VOICE_GAP] = cfg.voiceGapS;
    v.val[ui::P_MUSIC_DB] = cfg.musicDb;
    v.val[ui::P_VOICE_DB] = cfg.voiceDb;
    ui::qualNow = txQNow;
    ui::rateNowText = RATES[cfg.rateIdx].name;
    v.val[ui::P_POWER] = cfg.powerDbm;
    v.val[ui::P_GAIN] = cfg.gainDb;
    if (ui::out.eqBand < 0) {   // пока нажатие не выполнено, на экране — то, что нажали
      for (int k = 0; k < 5; k++) v.txEq[k] = (int8_t)((int)cfg.txEq[k] - 6);
      v.txLowCut = cfg.txLowCut;
    }
    v.val[ui::P_INPUT] = cfg.input;
    v.val[ui::P_TONE] = cfg.tone;
    v.lastTone = cfg.lastTone;
    v.muted = txMute;
    if (offAsk) {
      offAsk = false;
      ui::out.powerOff = true;
    }
    if (pageAsk >= 0) {   // с порта: открыть вкладку — так же, как пальцем, с перелистыванием (чтобы его можно было замерить)
      int pg = pageAsk;
      pageAsk = -1;
      ui::modal = ui::M_NONE;
      if (pg == 9) {
        if (ui::view.sd) ui::fmOpen();
      } else if (pg != ui::page) ui::slideTo(pg);
      else ui::needFull = true;
    }
    if (tapAsk >= 0) {
      int id = tapAsk;
      tapAsk = -1;
      ui::activate(id);
    }
    if (fmStateAsk) {
      fmStateAsk = false;
      Serial.printf("екран картки: вікно %d (0 список, 1 файл, 2 питання, 3 робота, 4 текст, 5 інше), тека «%s», у списку %d (готовий: %s), сторінка %d з %d, кнопок %d, вибрано «%s», копіюється «%s»\n",
                    ui::modal == ui::M_FILES ? ui::fmSt : -1, ui::fmCur, fm.n, ui::fmListReady() ? "так" : "ні", ui::fmPage + 1, ui::fmPages(), ui::hotN, ui::fmSelName, ui::fmClip);
    }
    v.val[ui::P_BRIGHT] = cfg.brightness;
    v.val[ui::P_DIM] = cfg.dimIdx;
    // уровень: среднеквадратичный за кадр (+3 дБ — синус показывает свою вершину); вверх быстро, вниз плавно
    portENTER_CRITICAL(&txMux);
    float sq = txSqSum;
    uint32_t cnt = txSqCnt;
    txSqSum = 0;
    txSqCnt = 0;
    portEXIT_CRITICAL(&txMux);
    int pk = txPeakUi;
    txPeakUi = 0;
    float db = cnt && sq > 0 ? 10 * log10f(sq / cnt / (32768.0f * 32768.0f)) + 3 : -60;
    float pkDb = pk > 0 ? 20 * log10f(pk / 32768.0f) : -60;
    db = db < -60 ? -60 : db > 0 ? 0 : db;
    pkDb = pkDb < -60 ? -60 : pkDb;
    // плавность — как у рисок спектра: сначала усреднение по времени, потом риска едет к среднему
    // (вверх 0,6, вниз 0,25 расстояния за 45 мс); кадр здесь 20 мс
    {
      static float avg = -60;
      const float k = 20.0f / 45.0f;
      avg += (db - avg) * 0.7f * k;
      shown += (avg - shown) * (avg > shown ? 0.6f * k : 0.25f * k);
    }
    // настоящий пик отсчётов — для подсказки «гучно» и цвета числа (держится секунду, потом спадает)
    if (pkDb >= hold) {
      hold = pkDb;
      holdUntil = now + 1000;
    } else if (now > holdUntil) hold -= 0.5f;
    if (hold < shown) hold = shown;
    // «шапка» на шкале: докуда риска недавно доходила. Едет вверх вместе с риской, держится 0,8 с и плавно,
    // с разгоном, опускается — как шапки в анализаторе спектра. Раньше на шкале стоял сам пик отсчётов:
    // у музыки он на 10–15 дБ правее риски и скачет, а при пустом входе прыгал от шума АЦП.
    {
      static float cap = -60, fall = 0;
      static uint32_t capUntil = 0;
      if (shown >= cap) {
        cap = shown;
        capUntil = now + 800;
        fall = 0;
      } else if (now > capUntil) {
        fall += 0.03f;
        cap -= fall;
        if (cap < shown) cap = shown;
      }
      v.holdDb = cap;
    }
    v.levelDb = shown;
    v.peakDb = hold;
    if (db > -50) lastLoud = now;
    v.silent = now - lastLoud > 5000;
    v.clip = txClipMs && msSince(txClipMs, now) < 1500;
    v.radioOk = scanRunning || msSince(rLastDoneMs, now) < 500;
    v.uptimeS = now / 1000;
    if (now - lastSec >= 1000) {   // раз в секунду — счётчики радио
      lastSec = now;
      uint32_t sent = rSentTotal, n = rAirN, sum = rAirSumUs, mx = rAirMaxUs;
      rAirN = 0;
      rAirSumUs = 0;
      rAirMaxUs = 0;
      v.pktPerS = sent - sentPrev;
      sentPrev = sent;
      v.airAvgMs = n ? sum / 1000.0f / n : 0;
      v.airMaxMs = mx / 1000.0f;
      v.refused = rRefusedTotal > 65535 ? 65535 : rRefusedTotal;
      v.restarts = rRestarts;
      v.freeHeapK = ESP.getFreeHeap() / 1024;
    }
    // спектр считается через кадр — с той же частотой, с какой рисуется
    // Спектр считается, только пока он на экране: вкладка «Звук», поверх неё нет окна, экран не пригашен.
    // Тогда же задача передачи копит для него звук (txSpecOn) — в остальное время не копит.
    bool specShown = ui::page == ui::PG_SOUND && ui::modal == ui::M_NONE && !asleep;
    txSpecOn = specShown;
    if (specShown) {
      specUpdate();   // на каждом кадре (было через кадр): владелец — «увеличь количество опросов»
      specAnimate(v.spec);
    }
    v.sd = recSd;
    v.sdFull = recFull;
    v.fileOn = fm.playing;
    v.card = recCard;
    strlcpy(v.cardFs, recCardFs, sizeof(v.cardFs));
    v.rec = recOn;
    v.recS = recSamples / SRATE;
    v.sdFreeMb = recFreeMb;
    v.scanning = scanRunning;
    v.scanStep = scanStep;
    v.scanned = scanDone;
    memcpy(v.chanBusy, scanBusy, 13);
    {   // приёмники: копия таблицы
      int n = 0;
      portENTER_CRITICAL(&peerMux);
      for (int i = 0; i < PEERS_MAX && n < ui::RX_MAX; i++) {
        const Peer &p = peers[i];
        if (!p.used) continue;
        ui::View::Rx &r = v.rx[n++];
        memcpy(r.id, p.id, 3);
        memcpy(r.name, p.name, ui::RX_NAME);
        r.name[ui::RX_NAME - 1] = 0;
        r.wide = p.wide;
        r.online = p.seenMs && msSince(p.seenMs, now) < 7000;
        r.off = p.flags & ST_OFF;
        r.sleep = p.flags & ST_SLEEP;
        r.mute = p.flags & ST_MUTE;
        r.stereo = p.flags & ST_STEREO;
        r.volume = p.volume;
        r.depthMs = p.depthMs;
        r.lossPm = p.lossPm;
        r.lostFrames = p.lostFrames;
        r.fw = p.fw;
        r.rssi = p.rssi;
        r.uptimeMin = p.uptimeMin;
        r.hasInfo = p.hasInfo;
        memcpy(r.par, p.par, sizeof(r.par));
        memcpy(r.eq, p.eq, sizeof(r.eq));
        r.lowCut = p.lowCut;
        r.hasEq = p.hasEq;
        r.lock = p.lock;
        r.hasLock = p.hasInfo && (p.fwMaj > 2 || (p.fwMaj == 2 && p.fwMin >= 46));
        r.boost = p.boost > 12 ? 0 : p.boost;
        r.hasBoost = p.hasInfo && (p.fwMaj > 2 || (p.fwMaj == 2 && p.fwMin >= 48));
        r.earTest = p.earTest;
      }
      portEXIT_CRITICAL(&peerMux);
      v.rxN = n;
      {   // обновление приёмников по радио — для окна
        ui::View::Ota &ov = v.ota;
        if (ui::out.otaAuto < 0) ov.autoUpd = cfg.autoUpd;
        uint8_t st = otaTx.stage;
        if (st != OT_IDLE || ov.stage != 1) ov.stage = st;   // «1» ставит сама кнопка «Почати» — до ответа задачи
        ov.secs = otaTxActive() ? (now - otaTx.startMs) / 1000 : (otaTx.endMs - otaTx.startMs) / 1000;
        int k = 0, more = 0, minPct = 101;
        OtaCount c = otaTxCount(st == OT_WRITE || st >= OT_DONE ? 12000 : 8000);
        for (auto &op : otaTx.peer) {
          if (!op.used) continue;
          bool lost = op.phase != OS_DONE && op.phase != OS_SAME && op.phase != OS_FAIL && msSince(op.seenMs, now) > 8000;
          if (op.phase == OS_RECV && !lost && op.percent < minPct) minPct = op.percent;
          if (k >= 4) {
            more++;
            continue;
          }
          ui::View::Ota::It &it = ov.it[k++];
          snprintf(it.name, sizeof(it.name), "%02X%02X%02X", op.id[0], op.id[1], op.id[2]);
          for (int i = 0; i < PEERS_MAX; i++)
            if (peers[i].used && !memcmp(peers[i].id, op.id, 3) && peers[i].name[0]) strlcpy(it.name, peers[i].name, sizeof(it.name));
          it.phase = op.phase;
          it.percent = op.percent;
          it.err = op.err;
          it.lost = lost && st != OT_IDLE;
        }
        ov.n = st == OT_IDLE ? 0 : k;
        ov.more = more;
        ov.percent = minPct > 100 ? (st == OT_FINISH ? 100 : 0) : minPct;
        ov.done = c.done;
        ov.fail = c.fail;
        ov.same = c.same;
        ov.lost = c.lost;
        ov.card = upd.state;
        ov.cardWhy = upd.why;
        ov.selfPct = upd.selfPercent;
        ov.selfWait = upd.selfWait;
        ov.fromCard = otaTx.fromCard;
        ov.demo = otaTx.demo;
        strlcpy(ov.cardVer, upd.version, sizeof(ov.cardVer));
        strlcpy(ov.cardFile, upd.file, sizeof(ov.cardFile));
        // на карте нашлась новая прошивка — предложить: один раз за включение открыть окно «Оновлення» само
        static uint32_t offered;
        if (upd.state == UPD_READY && offered != upd.seq && ui::modal == ui::M_NONE && now > 4000 && !asleep && st == OT_IDLE) {
          offered = upd.seq;
          ui::modal = ui::M_OTA;
          ui::drawModal();
        }
        static uint8_t stWas = OT_IDLE;   // закончилось, а окно закрыто — сказать итог сообщением
        if (st != stWas) {
          if (st >= OT_DONE && st <= OT_ERROR && stWas >= OT_PREP && stWas <= OT_WRITE && ui::modal != ui::M_OTA) {
            char t[96];
            if (st == OT_DONE) snprintf(t, sizeof(t), tr("Оновлення приймачів: оновлено %d, не вдалося %d"), c.done, c.fail + c.lost);
            else snprintf(t, sizeof(t), "%s", st == OT_NOBODY ? tr("Оновлення приймачів: оновлювати нікого") : tr("Оновлення приймачів не завершено"));
            ui::toast(t);
          }
          stWas = st;
        }
      }
      // просьбы о доступе (пока открыто окно «Додати приймач»; оно же не даёт приёму просьб закрыться по времени)
      int an = 0;
      if (ui::modal == ui::M_PAIR) {
        pairOpenUntil = now + 20000;
        for (int i = 0; i < PAIR_ASKS && an < 3; i++)
          if (pairAsk[i].used) {
            ui::View::Ask &a = v.ask[an++];
            strlcpy(a.name, pairAsk[i].name, ui::RX_NAME);
            a.code = pairAsk[i].code;
            a.slot = i;
          }
      }
      v.askN = an;
      // кто появился и кто пропал — сказать на экране
      static uint8_t was[PEERS_MAX];   // 0 не видели, 1 на связи, 2 пропал
      for (int i = 0; i < PEERS_MAX; i++) {
        const Peer &p = peers[i];
        if (!p.used) {
          was[i] = 0;
          continue;
        }
        bool on = p.seenMs && msSince(p.seenMs, now) < 7000;
        char t[128];
        if (on && was[i] != 1) {
          snprintf(t, sizeof(t), was[i] == 2 ? tr("«%s» знову на зв'язку") : tr("«%s» на зв'язку"), p.name);
          ui::toast(t);
          was[i] = 1;
        } else if (!on && was[i] == 1) {
          snprintf(t, sizeof(t), tr("«%s» зник зі зв'язку"), p.name);
          ui::toast(t);
          was[i] = 2;
        }
      }
    }

    // --- палец: касания копит задача сенсора (panel4848.h), здесь разбирается всё, что накопилось.
    // Клавиатуре отдаётся каждая точка (ей важна дорожка пальца со временем), остальным экранам — «нажал» и «отпустил»;
    // на каждое такое событие — свой шаг меню, поэтому быстрое нажатие, целиком уместившееся в один долгий кадр,
    // не теряется.
    static bool down = false;
    static int tx = 0, ty = 0;
    int steps = 0;
    uint32_t t0 = micros();
    {
      TouchEv ev;
      while (touchQ && steps < 24 && xQueueReceive(touchQ, &ev, 0) == pdTRUE) {
        bool was = down;
        down = ev.down;
        tx = ev.x;
        ty = ev.y;
        if (down) {
          if (asleep && !was) swallow = true;   // первое касание только будит экран
          lastTouch = now;
          if (lastX != tx || lastY != ty) {
            lastX = tx;
            lastY = ty;
            if (touchDebug) Serial.printf("дотик x=%d y=%d пляма=%u\n", tx, ty, ev.size);
          }
        }
        if (down != was || ui::modal == ui::M_KEYS) {
          ui::touchMs = ev.ms;
          ui::frame(down && !swallow, tx, ty);
          steps++;
        }
        if (!down) swallow = false;
      }
    }
    asleep = DIM_MIN[cfg.dimIdx] && now - lastTouch > DIM_MIN[cfg.dimIdx] * 60000u;
    float want = (asleep ? 6 : cfg.brightness) * lcdDim / 100.0f;
    if (fabsf(want - light) > 0.5f) {
      light += (want - light) * (otaTx.stage == OT_SELF ? 0.3f : 0.12f);   // при записи прошивки — гаснет и загорается быстрее
      lcdBacklight((uint8_t)light);
    } else if (!lcdDim && light > 0) {
      light = 0;
      lcdBacklight(0);
    }

    if (!steps) {   // касаний не было — обычный кадр (движение на экране, живые показатели)
      ui::touchMs = millis();
      ui::frame(down && !swallow, tx, ty);
    }
    uint32_t dt = micros() - t0;
    frameSumUs = frameSumUs + dt;
    frameN = frameN + 1;
    if (dt > frameMaxUs) frameMaxUs = dt;

    // --- что попросили
    ui::Out &o = ui::out;
    if (o.mute >= 0) {
      txMute = o.mute;
      cfg.txMute = o.mute;
      o.mute = -1;
      saveAt = now + 600;
    }
    if (o.setParam >= 0) {
      int val = o.setValue;
      switch (o.setParam) {
        case ui::P_CHANNEL: cfg.channel = val; rApply = true; break;
        case ui::P_KIT: cfg.kit = val; break;
        case ui::P_RATE:
          cfg.rateAuto = val == 0;
          if (val >= 1 && val <= 9) {
            cfg.rateIdx = RATE_IDX[val - 1];
            rApply = true;
            int q = qForRate(cfg.quality, cfg.rateIdx);
            if (q != cfg.quality) {   // выбранное качество в эту скорость не помещается — сказать, что пойдёт на деле
              static char msg[128];
              snprintf(msg, sizeof(msg), tr("На цій швидкості якість звуку — «%s»"), tr(Q_NAME[q]));
              ui::toast(msg);
            }
          }
          break;
        case ui::P_QUALITY: {
          cfg.quality = val;
          int q = qForRate(cfg.quality, cfg.rateIdx);
          if (q != cfg.quality && !cfg.rateAuto) {
            static char msg[128];
            snprintf(msg, sizeof(msg), tr("Швидкість %s Мбіт/с: якість буде «%s»"), RATES[cfg.rateIdx].name, tr(Q_NAME[q]));
            ui::toast(msg);
          }
          break;
        }
        case ui::P_POWER: cfg.powerDbm = val; rApply = true; break;
        case ui::P_GAIN: cfg.gainDb = val; break;
        case ui::P_TONE:
          cfg.tone = val;
          if (val) cfg.lastTone = val;
          break;
        case ui::P_STEREO: cfg.stereo = val ? 1 : 0; break;
        case ui::P_HOP: cfg.autoHop = val ? 1 : 0; break;
        case ui::P_DEBUG: cfg.dbgAir = val ? 1 : 0; break;
        case ui::P_LANG:
          cfg.lang = val ? 1 : 0;
          uiLang = cfg.lang;
          ui::needFull = true;   // всё на экране — заново, на новом языке
          break;
        case ui::P_TRACK: cfg.testTrack = val; break;
        case ui::P_DUCK_S: cfg.duckS = val; break;
        case ui::P_DUCK_DB: cfg.duckDb = val; break;
        case ui::P_VOICE_GAP: cfg.voiceGapS = val; break;
        case ui::P_MUSIC_DB: cfg.musicDb = val; break;
        case ui::P_VOICE_DB: cfg.voiceDb = val; break;
        case ui::P_BRIGHT: cfg.brightness = val; break;
        case ui::P_DIM: cfg.dimIdx = val; break;
        case ui::P_INPUT:
          if (cfg.input != val) {   // вход запускается при старте — нужен перезапуск
            cfg.input = val;
            settingsSave();
            vTaskDelay(pdMS_TO_TICKS(200));
            ESP.restart();
          }
          break;
      }
      o.setParam = -1;
      saveAt = now + 600;   // запись почти сразу: выключили питание через секунду после изменения — оно не должно пропасть
    }
    if (o.otaAuto >= 0) {   // автообновление приёмников — включить / выключить
      cfg.autoUpd = o.otaAuto ? 1 : 0;
      o.otaAuto = -1;
      saveAt = now + 600;
    }
    if (o.eqBand >= 0) {   // эквалайзер входа
      if (o.eqBand < 5) cfg.txEq[o.eqBand] = (uint8_t)constrain(o.eqVal + 6, 0, 12);
      else cfg.txLowCut = o.eqVal ? 1 : 0;
      txEqGen = txEqGen + 1;
      o.eqBand = -1;
      saveAt = now + 600;
    }
    if (o.setSrcName) {   // источник звука переименован
      o.setSrcName = false;
      strlcpy(cfg.srcName, o.srcName, sizeof(cfg.srcName));
      saveAt = now + 600;
    }
    if (o.otaStart || o.otaDemo || o.otaCard) {   // обновление: своей прошивкой / пробное / файлом с карты (приёмники, потом сам передатчик)
      bool ok = o.otaCard ? (upd.state == UPD_READY && otaTxStart(false, false, upd.img, upd.len, upd.version, upd.sha)) : otaTxStart(o.otaDemo);
      o.otaStart = o.otaDemo = o.otaCard = false;
      if (!ok) ui::toast(tr("Оновлення зараз неможливе"));
    }
    if (o.updScan) {
      o.updScan = false;
      if (upd.state != UPD_READY && upd.state != UPD_OLDER && !otaTxActive()) upd.scanAsk = true;
    }
    if (o.otaCancel) {
      o.otaCancel = false;
      otaTx.cancel = true;
    }
    if (o.pairOpen >= 0) {
      pairOpen(o.pairOpen != 0);
      o.pairOpen = -1;
    }
    if (o.pairApprove >= 0) {
      pairApproveAsk = o.pairApprove;
      o.pairApprove = -1;
    }
    if (o.rxCmd) {   // команда приёмнику; таблицу правим сразу, приёмник подтвердит следующим сообщением
      int i = peerFind(o.rxId);
      if (i >= 0) {
        Peer &p = peers[i];
        switch (o.rxCmd) {
          case ui::RXC_IDENTIFY: peerCommand(p.id, CMD_IDENTIFY, 0); break;
          case ui::RXC_NAME:
            peerCommand(p.id, CMD_NAME, 0, o.rxName);
            utf8Copy(p.name, o.rxName, p.wide ? NAME_LONG : NAME_LEN);   // приёмник со старой прошивкой запомнит 13 букв
            peersDirty = true;
            break;
          case ui::RXC_ENABLE:
            peerCommand(p.id, CMD_ENABLE, o.rxArg);
            p.flags = o.rxArg ? p.flags & ~ST_OFF : p.flags | ST_OFF;
            p.wantOff = !o.rxArg;
            p.enforceUntil = now + 15000;   // спящий приёмник слушает эфир урывками — повторять, пока не подтвердит
            peersDirty = true;
            break;
          case ui::RXC_VOLUME:
            peerCommand(p.id, CMD_VOLUME, o.rxArg);
            p.volume = o.rxArg;
            p.flags &= ~ST_MUTE;
            break;
          case ui::RXC_STEREO:
            peerCommand(p.id, CMD_STEREO, o.rxArg);
            p.flags = o.rxArg ? p.flags | ST_STEREO : p.flags & ~ST_STEREO;
            break;
          case ui::RXC_SET:   // настройка «для слуха и удобства»: номер × 32 + значение
            peerCommand(p.id, CMD_SET, (uint8_t)o.rxArg);
            if ((o.rxArg >> 5) < RXP_COUNT) p.par[o.rxArg >> 5] = o.rxArg & 31;
            else if ((o.rxArg >> 5) == RXP_X_LOCK) p.lock = o.rxArg & 3;
            else if ((o.rxArg >> 5) == RXP_X_BOOST) p.boost = o.rxArg & 31;
            p.parSetMs = now ? now : 1;
            break;
          case ui::RXC_EQ:   // эквалайзер: полоса × 16 + значение
            peerCommand(p.id, CMD_EQ, (uint8_t)o.rxArg);
            if ((o.rxArg >> 4) < 5) p.eq[o.rxArg >> 4] = o.rxArg & 15;
            else p.lowCut = o.rxArg & 1;
            p.parSetMs = now ? now : 1;
            break;
          case ui::RXC_EARTEST:
            peerCommand(p.id, CMD_EARTEST, o.rxArg ? 1 : 0);
            p.earTest = o.rxArg ? 1 : 0;
            p.parSetMs = now ? now : 1;
            break;
          case ui::RXC_FORGET: peerRemove(i); break;   // удалить из набора: приёмник теряет доступ, ключ набора меняется
        }
      }
      o.rxCmd = 0;
    }
    if (o.powerOff) {   // выключить передатчик: эфир замолкает, экран гаснет, всё спит; включение — коснуться экрана
      o.powerOff = false;
      settingsSave();
      if (recOn) recWantStop = true;
      cardStop();   // файл с карты в эфире — тоже остановить
      txPause = true;
      for (int b = (int)light; b >= 0; b -= 4) {
        lcdBacklight(b);
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      lcdBacklight(0);
      vTaskDelay(pdMS_TO_TICKS(300));
      esp_wifi_stop();
      ui::drawStandby(0, true);
      for (int i = 0; i < 200 && recOn; i++) vTaskDelay(pdMS_TO_TICKS(10));   // запись на карту успеет закрыть файл
      // Выключено: радио стоит, подсветка погашена, процессор спит и раз в четверть секунды просыпается на пару
      // миллисекунд спросить сенсор. Прикосновение включает передатчик (владелец 07.10: «после активации все
      // отключается, только ожидает прикосновения на сенсор, которое повторно включает устройство»).
      // Чтобы помеха на сенсоре не включила его сама, касание должно продержаться 0,15 с.
      Serial.flush();
      for (;;) {
        esp_sleep_enable_timer_wakeup(250000);
        esp_light_sleep_start();
        int sx, sy;
        touchHold = true;   // здесь сенсор читаем сами: между кругами сна задача сенсора не успевает
        if (!touchRead(sx, sy)) continue;
        vTaskDelay(pdMS_TO_TICKS(150));
        if (!touchRead(sx, sy)) continue;
        lcdBacklight(40);
        ui::drawStandby(1, false);
        vTaskDelay(pdMS_TO_TICKS(150));
        ESP.restart();
      }
    }
    if (o.scan) {
      o.scan = false;
      scanStart();
    }
    if (o.recToggle) {
      o.recToggle = false;
      if (recOn) recWantStop = true;
      else recWantStart = true;
    }
    if (o.factory) {
      settingsFactory();
      vTaskDelay(pdMS_TO_TICKS(200));
      ESP.restart();
    }
    if (o.restart) {
      settingsSave();
      vTaskDelay(pdMS_TO_TICKS(200));
      ESP.restart();
    }
    if (saveAt && txQuietToSave(saveAt, 8000)) {   // в паузе звука: запись на миг останавливает приём со входа
      saveAt = 0;
      settingsSave();
    }
    // Кадр не уложился в 20 мс (вкладка со спектром при играющей музыке) — всё равно уступить время:
    // иначе главный цикл с отчётами, командами порта и выбором скорости не получает ядро вовсе (06.10).
    loopSumUs = loopSumUs + (micros() - loopT0);
    loopN = loopN + 1;
    if (asleep) {   // экран пригашен — смотреть на него некому: шесть кадров в секунду вместо пятидесяти (касание будит сразу же)
      vTaskDelay(pdMS_TO_TICKS(150));
      tick = xTaskGetTickCount();
    } else {
      // Следующий кадр меню — с началом следующего кадра экрана (42 раза в секунду). Раньше меню рисовало по своим
      // часам, 50 раз в секунду, и любое движение на экране шло то с повтором кадра, то с пропуском.
      // Перед ожиданием — уступить время главному циклу (отчёты, команды порта), даже если кадр затянулся.
      vTaskDelay(pdMS_TO_TICKS(2));
      lcdWaitFrame();
      tick = xTaskGetTickCount();
    }
  }
}
}  // namespace txscreen
