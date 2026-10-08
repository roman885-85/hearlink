// Экран приёмника: модуль M75 (SH1106 128×64 + ручка EC11 + кнопки CON и BAK).
//
// Обычно на экране спектр звука и строка состояния. Поворот ручки — громкость, нажатие — тишина,
// долгое нажатие — меню. BAK — назад (на главном экране — тишина), CON — погасить экран.
// Экран не обязателен: если он не отвечает, приёмник играет без него и раз в две секунды ищет экран снова.
//
// Заставка, поиск передатчика, «запуск», уход в ожидание — по состоянию приёмника (rxPower, см. rxaudio.h);
// сами картинки с движением — в rxscreens.h. В ожидании экран погашен; нажатие ручки его будит:
// если ждали сигнала — приёмник ещё 10 с ищет передатчик, если выключен с передатчика — подсказка
// «ручку двічі — увімкнути».
#pragma once
#include <Wire.h>
#include <U8g2lib.h>
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "esp_intr_alloc.h"
#include "soc/interrupts.h"
#include "rxaudio.h"
#include "rxlink.h"
#include "rxscreens.h"

void settingsSave();   // в hearlink.ino

static U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
static volatile bool uiHaveOled;
static volatile bool oledFast;         // экран работает на 800 кГц
static volatile uint32_t oledFrames, oledFrameUsMax, oledFrameUsSum;   // для отчёта: кадров и время кадра
static volatile int32_t encCount;      // четверть-шаги ручки
static volatile uint8_t encState;
static volatile uint32_t uiKeys;       // состояние кнопок сейчас: бит 0 — ручка, 1 — CON, 2 — BAK (для проверки сборки)

// Быстрый вывод кадра на экран SH1106. Библиотека шлёт кадр (1 КБ) примерно сорока мелкими посылками, и на них
// уходило 25 мс при любой скорости шины — 33 кадра в секунду, спектр шёл рывками (владелец 07.10: «не вижу плавности
// анализатора спектра на приемнике»). Здесь кадр сравнивается с тем, что уже на экране, и по каждой из восьми строк
// экрана уходит одна посылка — только с изменившимися столбцами. Рисует по-прежнему библиотека, в свой лист.
static uint8_t oledPrev[1024];
static bool oledPrevOk;
static void oledFlush() {
  const uint8_t *buf = oled.getBufferPtr();
  for (int p = 0; p < 8; p++) {
    const uint8_t *row = buf + p * 128;
    uint8_t *old = oledPrev + p * 128;
    int x0 = 0, x1 = 127;
    if (oledPrevOk) {
      while (x0 < 128 && row[x0] == old[x0]) x0++;
      if (x0 == 128) continue;   // строка не изменилась
      while (row[x1] == old[x1]) x1--;
    }
    Wire.beginTransmission(0x3C);
    Wire.write(0x80);            // «дальше одна команда»
    Wire.write(0xB0 | p);        // строка экрана
    Wire.write(0x80);
    Wire.write((x0 + 2) & 0x0F); // столбец (у SH1106 видимая часть сдвинута на 2)
    Wire.write(0x80);
    Wire.write(0x10 | ((x0 + 2) >> 4));
    Wire.write(0x40);            // «дальше данные до конца посылки»
    Wire.write(row + x0, x1 - x0 + 1);
    Wire.endTransmission();
    memcpy(old + x0, row + x0, x1 - x0 + 1);
  }
  oledPrevOk = true;
}

static void IRAM_ATTR encIsr() {
  static const int8_t T[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
  uint8_t s = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  encState = ((encState << 2) | s) & 15;
  encCount = encCount + T[encState];
}

// Прерывания выводов ручки подключаются напрямую, а не через attachInterrupt. Тот ставит общий обработчик выводов
// через служебную задачу ядра (ipc) со стеком в 1 КБ; если в этот миг приходит прерывание звука и задачи
// переключаются, стек переполняется и плата падает при запуске — «Stack canary watchpoint triggered (ipc1)».
// После программного перезапуска (конец обновления по радио!) так выходило больше чем в половине запусков, до трёх
// падений подряд: замер 07.10 — 7 падений на 11 перезапусков; найдено по снимку сбоя (стек задачи ipc1: ipc_task →
// gpio_isr_register_on_core_static → esp_intr_alloc → прерывание → переключение задач). Здесь обработчик ставит
// сама задача экрана (стек 8 КБ), служебная задача не участвует.
static intr_handle_t encIntr;
static void IRAM_ATTR encGpioIsr(void *) {
  uint32_t st;
  gpio_ll_get_intr_status(&GPIO, 0, &st);   // события на выводах 0…31 с включённым прерыванием
  gpio_ll_clear_intr_status(&GPIO, st);
  if (st & ((1u << PIN_ENC_A) | (1u << PIN_ENC_B))) encIsr();
}
static void encAttach() {
  if (!encIntr && esp_intr_alloc(ETS_GPIO_INTR_SOURCE, 0, encGpioIsr, NULL, &encIntr) != ESP_OK) {
    Serial.println("переривання ручки не підключилось");
    return;
  }
  for (int pin : { PIN_ENC_A, PIN_ENC_B }) {
    gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_ANYEDGE);
    gpio_intr_enable((gpio_num_t)pin);
  }
}

// ---- спектр: окно Ханна, преобразование Фурье, 14 полос с логарифмическим шагом от 100 Гц до 8 кГц
#define BANDS 14
static float fftRe[SPEC_N], fftIm[SPEC_N], fftWin[SPEC_N], fftCos[SPEC_N / 2], fftSin[SPEC_N / 2];
static uint16_t fftRev[SPEC_N];
static uint8_t bandLo[BANDS], bandHi[BANDS];
static float barLevel[BANDS];   // 0…1 — что показал последний разбор (цель)
static float barShown[BANDS];   // 0…1 — что нарисовано: идёт к цели плавно, вверх за 45 мс, вниз за 150 мс (как на передатчике)
static float barPeak[BANDS];    // отметка пика: держится 0,45 с, потом плавно опускается
static uint32_t barPeakAt[BANDS];
// Один шаг движения рисок: dt — сколько миллисекунд прошло с прошлого кадра. Скорость не зависит от частоты кадров.
// Мелкая дрожь гасится отдельно (владелец 07.10: «добавить плавности в малых пиках столбцов для избежания
// дерганности»): чем меньше отличие цели от нарисованного, тем медленнее риска к ней идёт (до четырёх раз), а высота
// в точках меняется, только когда сдвиг набрал почти целую точку, — риска не «мигает» на границе двух высот.
static float barDraw[BANDS];    // высота риски, как она рисуется (в долях шкалы, с шагом в одну точку)
static void specAnimate(uint32_t now, float dt) {
  for (int b = 0; b < BANDS; b++) {
    float d = barLevel[b] - barShown[b], a = fabsf(d), tau = d > 0 ? 45.0f : 150.0f;
    if (a < 0.09f) tau *= 1 + (0.09f - a) / 0.09f * 3;
    barShown[b] += d * (1 - expf(-dt / tau));
    float px = barShown[b] * 46;
    if (fabsf(px - barDraw[b] * 46) >= 0.8f) barDraw[b] = roundf(px) / 46;
    if (barShown[b] >= barPeak[b]) {
      barPeak[b] = barShown[b];
      barPeakAt[b] = now;
    } else if (now - barPeakAt[b] > 450) {
      barPeak[b] -= dt * 0.0011f;   // вся шкала — за 0,9 с
      if (barPeak[b] < barShown[b]) barPeak[b] = barShown[b];
    }
  }
}

static void specInit() {
  int bits = 0;
  while ((1 << bits) < SPEC_N) bits++;
  for (int i = 0; i < SPEC_N; i++) {
    int r = 0;
    for (int b = 0; b < bits; b++)
      if (i & (1 << b)) r |= 1 << (bits - 1 - b);
    fftRev[i] = r;
    fftWin[i] = 0.5f - 0.5f * cosf(2 * PI * i / (SPEC_N - 1));
  }
  for (int i = 0; i < SPEC_N / 2; i++) {
    fftCos[i] = cosf(2 * PI * i / SPEC_N);
    fftSin[i] = -sinf(2 * PI * i / SPEC_N);
  }
  float f = 100, k = powf(8000.0f / 100.0f, 1.0f / BANDS), hz = (float)SRATE / SPEC_N;
  for (int b = 0; b < BANDS; b++) {
    int lo = (int)ceilf(f / hz), hi = (int)floorf(f * k / hz);
    if (lo < 1) lo = 1;
    if (hi < lo) hi = lo;
    bandLo[b] = lo;
    bandHi[b] = hi;
    f *= k;
  }
}

static void specUpdate() {
  uint32_t pos = specPos;
  for (int i = 0; i < SPEC_N; i++) {
    fftRe[fftRev[i]] = specBuf[(pos + i) & (SPEC_N - 1)] * fftWin[i] * (1.0f / 32768);
    fftIm[i] = 0;
  }
  for (int len = 2; len <= SPEC_N; len <<= 1) {
    int half = len >> 1, stepW = SPEC_N / len;
    for (int i = 0; i < SPEC_N; i += len)
      for (int j = 0; j < half; j++) {
        float wr = fftCos[j * stepW], wi = fftSin[j * stepW];
        float ur = fftRe[i + j], ui = fftIm[i + j];
        float vr = fftRe[i + j + half] * wr - fftIm[i + j + half] * wi;
        float vi = fftRe[i + j + half] * wi + fftIm[i + j + half] * wr;
        fftRe[i + j] = ur + vr;
        fftIm[i + j] = ui + vi;
        fftRe[i + j + half] = ur - vr;
        fftIm[i + j + half] = ui - vi;
      }
  }
  for (int b = 0; b < BANDS; b++) {
    float p = 0;
    for (int i = bandLo[b]; i <= bandHi[b]; i++) {
      float m = fftRe[i] * fftRe[i] + fftIm[i] * fftIm[i];
      if (m > p) p = m;
    }
    // полная шкала синуса после окна даёт (N/4)²; низ шкалы −60 дБ, верх −6 дБ, подъём 3 дБ на октаву
    float db = 10 * log10f(p / ((SPEC_N / 4.0f) * (SPEC_N / 4.0f)) + 1e-12f) + b * (18.0f / BANDS);
    float lv = (db + 60) / 54;
    lv = lv < 0 ? 0 : lv > 1 ? 1 : lv;
    barLevel[b] = lv;   // сглаживание — в specAnimate, по времени
  }
}

// ---- меню
// Управление — одной ручкой (владелец 07.10: «кнопки, кроме кнопки на энкодере, использоваться не будут — учесть это
// при построении меню»): поворот — громкость или выбор, нажатие — «тиша» или вход/подтверждение, долгое нажатие —
// меню с главного экрана и «назад» из меню. Экран гасится пунктом «Екран»; любое движение ручки включает его снова.
// С версии 2.32 в начале списка — то, что нужно слушающему (владелец 07.10 выбрал из предложенного: вид главного экрана,
// светодиод, язык, чёткость речи, баланс, предел громкости, проверка наушников, качество связи), дальше — служебное.
// Те же шесть настроек меняются и с передатчика (окно приёмника → «Налаштування»); дверь у них одна — rxParamSet.
enum { M_VIEW, M_LED, M_LANG, M_CLARITY, M_BALANCE, M_VOLMAX, M_EARTEST, M_LINK,
       M_KIT, M_CHANNEL, M_DEPTH, M_OUTPUT, M_STEREO, M_SCREEN, M_FORGET, M_ABOUT, M_EXIT, M_COUNT };
static const char *const MENU_NAME[M_COUNT] = { "Вигляд", "Світлодіод", "Мова", "Чіткість", "Баланс", "Межа гучн.", "Навушники", "Зв'язок",
                                                "Набір", "Канал", "Запас", "Вихід звуку", "Виводи", "Екран", "Забути набір", "Про пристрій", "Вийти" };
static const uint8_t MENU_ICON[M_COUNT] = { 9, 10, 11, 12, 13, 14, 15, 16, 0, 1, 2, 3, 4, 5, 6, 7, 8 };   // значки — rxs::menuIcon
// какой пункт меню отвечает за настройку RXP_… (для оповещения «настройку сменили с передатчика»)
static const uint8_t PARAM_ITEM[RXP_COUNT] = { M_CLARITY, M_BALANCE, M_VOLMAX, M_VIEW, M_LED, M_LANG };
// «Забути набір»: стереть ключ набора — приёмник снова попросит доступ (для перехода к другому передатчику или
// после смены ключа на своём). Нарочно в два действия: повернуть на «ТАК» и нажать.
static bool menuForgetArm;

static void menuValue(int item, char *s, size_t n) {
  switch (item) {
    case M_VIEW: snprintf(s, n, "%s", cfg.rxView == 1 ? tr("стрілки") : cfg.rxView == 2 ? tr("гучність") : tr("спектр")); break;
    case M_LED: snprintf(s, n, "%s", cfg.rxLed == 0 ? tr("вимк.") : cfg.rxLed == 1 ? tr("тьмяно") : cfg.rxLed == 3 ? tr("яскраво") : tr("норма")); break;
    case M_LANG: snprintf(s, n, "%s", cfg.rxLang == 1 ? "українська" : cfg.rxLang == 2 ? "English" : tr("як передавач")); break;
    case M_CLARITY:
      snprintf(s, n, "%s", cfg.rxClarity == 0 ? tr("вимк.") : cfg.rxClarity == 1 ? tr("легка") : cfg.rxClarity == 2 ? tr("середня") : tr("сильна"));
      break;
    case M_BALANCE:   // при выходе «протифаза» канал один — баланс не к чему приложить
      if (!cfg.rxStereo) snprintf(s, n, "%s", tr("1 канал"));
      else if (!cfg.rxBalance) snprintf(s, n, "%s", tr("рівно"));
      else snprintf(s, n, cfg.rxBalance < 0 ? tr("ліве +%d") : tr("праве +%d"), cfg.rxBalance < 0 ? -cfg.rxBalance : cfg.rxBalance);
      break;
    case M_VOLMAX: snprintf(s, n, "%u %%", cfg.rxVolMax * 5); break;
    case M_EARTEST: snprintf(s, n, "%s", tr("тест")); break;
    case M_LINK:
      if (rLastRxMs && msSince(rLastRxMs) < 1200) snprintf(s, n, tr("%d дБм"), (int)rRssi);
      else snprintf(s, n, "--");
      break;
    case M_KIT: snprintf(s, n, "%u", cfg.kit); break;
    case M_CHANNEL:
      if (cfg.autoChannel) snprintf(s, n, tr("авто (%u)"), cfg.channel);
      else snprintf(s, n, "%u", cfg.channel);
      break;
    case M_DEPTH:
      if (cfg.depthMs) snprintf(s, n, tr("%u мс"), cfg.depthMs);
      else snprintf(s, n, tr("сам (%.0f)"), (float)rxTargetMs);
      break;
    case M_OUTPUT: snprintf(s, n, "%s", cfg.output == OUT_I2S ? "PCM5102" : "PDM"); break;
    case M_STEREO: snprintf(s, n, "%s", cfg.rxStereo ? tr("2 канали") : tr("протифаза")); break;
    case M_SCREEN: snprintf(s, n, "%s", tr("вимкнути")); break;
    case M_FORGET: snprintf(s, n, "%s", !secHave ? tr("нема ключа") : menuForgetArm ? tr("ТАК") : tr("ні")); break;
    default: s[0] = 0;
  }
}

static bool menuChange(int item, int d) {   // true — нужна перезагрузка
  switch (item) {
    case M_VIEW: rxParamSet(RXP_VIEW, (cfg.rxView + d + 3 * 8) % 3); break;      // по кругу
    case M_LED: rxParamSet(RXP_LED, cfg.rxLed + d); break;
    case M_LANG: rxParamSet(RXP_LANG, (cfg.rxLang + d + 3 * 8) % 3); break;
    case M_CLARITY: rxParamSet(RXP_CLARITY, cfg.rxClarity + d); break;           // слышно сразу: звук в меню идёт
    case M_BALANCE:
      if (cfg.rxStereo) rxParamSet(RXP_BALANCE, cfg.rxBalance + d);
      break;
    case M_VOLMAX: {   // 20…100 %: ниже предел ставить незачем, а выше него громкость не поднимется
      int v = cfg.rxVolMax + d;
      rxParamSet(RXP_VOLMAX, v < 4 ? 4 : v);
      break;
    }
    case M_KIT: break;   // номер набора приёмник получает от передатчика (при подключении и вслед за его сменой) — здесь только показ
    case M_CHANNEL: {   // 0 — авто, 1…13
      int v = cfg.autoChannel ? 0 : cfg.channel;
      v = (v + d + 14 * 4) % 14;
      cfg.autoChannel = v == 0;
      if (v) cfg.channel = v;
      rApply = true;
      break;
    }
    case M_DEPTH: {   // «сам», затем 4…40 мс
      int v = (cfg.depthMs ? cfg.depthMs : 3) + d;
      cfg.depthMs = v < 4 ? 0 : v > 40 ? 40 : v;
      break;
    }
    case M_OUTPUT: cfg.output = cfg.output == OUT_I2S ? OUT_PDM : OUT_I2S; return true;
    case M_STEREO: cfg.rxStereo = !cfg.rxStereo; break;   // действует сразу: усилитель должен быть подключён соответственно
    case M_FORGET: menuForgetArm = !menuForgetArm; break;
  }
  return false;
}

static void drawCentered(int y, const char *s) {
  oled.drawUTF8((128 - oled.getUTF8Width(s)) / 2, y, s);
}

static void uiTask(void *) {
  rxKeyPinsClean();   // до pinMode: иначе он включит на кнопке «унаследованное» прерывание (см. rxlink.h)
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_KEY_CON, INPUT_PULLUP);   // кнопки модуля не используются; выводы подтянуты, чтобы не «висели»
  pinMode(PIN_KEY_BAK, INPUT_PULLUP);
  encState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  encAttach();   // вместо attachInterrupt — см. выше
  Wire.setBufferSize(256);   // строка экрана (128 байт и заголовок) должна помещаться в одну посылку
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  specInit();

  enum { S_MAIN, S_MENU, S_EDIT, S_ABOUT, S_LINK, S_EAR } screen = S_MAIN;
  int32_t encSeen = 0;
  uint8_t keyPrev = 0, keyStable = 0, keyCnt = 0, pwPrev = 255;
  // экран ищем сразу, а не через две секунды: иначе заставку при включении никто не увидит
  uint32_t pressAt = 0, volShownAt = 0, lastProbe = millis() - 2000, saveAt = 0, peekUntil = 0, muteAt = 0;
  bool longDone = false, screenOn = true, needRestart = false, dispOn = true;
  uint32_t specAt = 0;
  int item = 0;
  char buf[40];
  // --- движение на экране (rxfx.h): переходы между экранами, плавные величины, оповещения
  enum { SC_NONE, SC_UPDATE, SC_IDENT, SC_SLEEP, SC_SPLASH, SC_UNPAIRED, SC_GOING, SC_STARTING, SC_SEARCH, SC_MUTE, SC_MAIN, SC_LOST, SC_MENU, SC_ABOUT,
         SC_LINK, SC_EAR };
  static uint8_t trFrom[1024];             // прежний кадр — из него делается переход к новому экрану
  uint8_t sidWas = SC_NONE, trKind = 0;
  uint32_t trAt = 0, trMs = 0, frameAt = millis(), sidAt = 0;   // sidAt — когда на экране появился нынешний вид
  float barsF = 0, volF = cfg.volume, volK = 0, selF = 0, scrollF = 0, chPosF = cfg.channel;
  int chShown = cfg.channel, chPrev = cfg.channel;
  uint32_t chAt = 0;
  static char toastQ[4][44];               // оповещения: очередь из четырёх
  uint8_t toastHead = 0, toastTail = 0;
  uint32_t toastAt = 0;
  auto say = [&](const char *text) {       // показать оповещение (плашка сверху)
    uint8_t nx = (toastHead + 1) & 3;
    if (nx == toastTail) return;           // очередь полна — это оповещение пропустить
    for (uint8_t i = toastTail; i != toastHead; i = (i + 1) & 3)
      if (!strcmp(toastQ[i], text)) return;   // такое уже ждёт
    strlcpy(toastQ[toastHead], text, sizeof(toastQ[0]));
    toastHead = nx;
  };
  // «было — стало» для оповещений
  bool sigWas = false, muteWas = false, stereoWas = cfg.rxStereo;
  uint8_t flagsWas = 0;
  int volWas = cfg.volume;
  uint32_t lostAt = 0, clipSaidAt = 0, weakSince = 0, weakSaidAt = 0, nameSum = 0;
  int parWas[RXP_COUNT];                   // настройки «для слуха и удобства», как были на прошлом кадре
  for (int p = 0; p < RXP_COUNT; p++) parWas[p] = rxParamGet(p);
  uint32_t limitSaidAt = 0;
  // стрелочные индикаторы и полоска уровня: уровень и пик каналов 0…1 с «ходом» стрелки (вверх быстро, вниз медленно)
  float vuLF = 0, vuRF = 0, vuLP = 0, vuRP = 0;
  uint32_t vuLPAt = 0, vuRPAt = 0;

  for (;;) {
    uint32_t now = millis();
    // --- экран: есть ли он
    if (!uiHaveOled && now - lastProbe >= 2000) {
      lastProbe = now;
      Wire.beginTransmission(0x3C);
      if (Wire.endTransmission() == 0) {
        oled.begin();
        // Скорость обмена с экраном — 800 кГц вместо 400: кадр (1 КБ) уходит за 13 мс вместо 26, и спектр идёт
        // плавно (владелец 07.10: «оптимизировать скорость работы дисплея»). Если экран на такой скорости не
        // отвечает — остаёмся на 400.
        Wire.setClock(800000);
        int nak = 0;
        for (int i = 0; i < 12; i++) {
          Wire.beginTransmission(0x3C);
          Wire.write(0x00);
          Wire.write(0xE3);   // пустая команда
          if (Wire.endTransmission() != 0) nak++;
        }
        oledFast = nak == 0;
        oled.setBusClock(oledFast ? 800000 : 400000);
        oled.setContrast(200);
        oledPrevOk = false;
        uiHaveOled = true;
        rxScreenFound = true;
        dispOn = true;
      }
      rxScreenProbed = true;
    }
    // --- ручка: 4 четверть-шага на щелчок
    int32_t e = encCount;
    int turn = (e - encSeen) / 4;
    encSeen += turn * 4;
    // --- кнопки с защитой от дребезга
    uint8_t raw = !digitalRead(PIN_ENC_SW);   // только ручка
    uiKeys = raw;
    if (raw == keyPrev) {
      if (keyCnt < 3) keyCnt++;
    } else keyCnt = 0;
    keyPrev = raw;
    uint8_t pressed = 0, released = 0;
    if (keyCnt == 3 && raw != keyStable) {
      pressed = raw & ~keyStable;
      released = keyStable & ~raw;
      keyStable = raw;
    }
    bool clickKnob = false, longKnob = false;
    if (pressed & 1) {
      pressAt = now;
      longDone = false;
    }
    if ((keyStable & 1) && !longDone && now - pressAt > 700) {
      longDone = true;
      longKnob = true;
    }
    if ((released & 1) && !longDone) clickKnob = true;
    // --- состояние приёмника: поиск, запуск, работа, уход в ожидание, ожидание, пробуждение
    uint8_t pw = rxPower, why = rxPowerWhy;
    uint32_t pt = now - rxPowerAt;   // сколько длится нынешнее состояние
    if (pw != pwPrev) {
      if (pw == PW_WAKING || pw == PW_SEARCH || pw == PW_GOING) {   // проснулся или уходит — экран включить, меню закрыть
        screenOn = true;
        screen = S_MAIN;
      }
      pwPrev = pw;
    }
    if (!screenOn && pw != PW_STANDBY && (turn || (pressed & 1))) {   // экран погашен из меню: любое движение ручки его включает,
      screenOn = true;                                                // и только включает — громкость и «тиша» не трогаются
      turn = 0;
      longDone = true;
      clickKnob = longKnob = false;
    }
    if (pw == PW_STANDBY && (pressed || turn)) {
      if (why == WHY_NOSIGNAL) rxPowerWhy = WHY_KEY;   // ждали сигнала — поискать ещё раз, уже с экраном
      else peekUntil = now + 5000;                     // выключен с передатчика — показать, как включить
    }
    if (cfg.off) {   // выключен с передатчика: ручка не работает, кроме двойного нажатия — включить самому
      static uint32_t lastClick;
      if (clickKnob) {
        if (now - lastClick < 700) {
          cfg.off = 0;
          peekUntil = 0;
          settingsSave();
        }
        lastClick = now;
      }
      screen = S_MAIN;
    }
    // ручка и кнопки работают в работе и пока ищем передатчик (без него тоже надо уметь открыть меню)
    bool live = !cfg.off && (pw == PW_RUN || (pw == PW_SEARCH && (why == WHY_KEY || pt >= RX_SPLASH_MS)));
    if (!live) {
      clickKnob = longKnob = false;
      turn = 0;
    }
    uint8_t otaPh = otaRx.phase;
    bool otaShow = otaPh == OS_RECV || otaPh == OS_CHECK || otaPh == OS_WRITE || otaPh == OS_DONE || (otaPh == OS_FAIL && otaRx.err != OE_CANCEL);
    bool show = (screenOn && pw != PW_STANDBY) || now < peekUntil || now < rxIdentifyUntil || otaShow;   // обновление видно и при выключенном экране
    if (uiHaveOled && show != dispOn) {
      oled.setPowerSave(!show);
      dispOn = show;
      oledPrevOk = false;   // после включения экрана — кадр целиком
    }
    if (rxUiAsk) {   // проверка с порта: рук у проверяющего нет, а частоту кадров экрана «Зв'язок» и меню измерить надо
      screen = rxUiAsk == 1 ? S_MENU : rxUiAsk == 2 ? S_LINK : S_MAIN;
      rxUiAsk = 0;
    }
    if (rxEarTest && screen != S_EAR && (clickKnob || longKnob)) {   // проверку наушников запустили с передатчика: нажатие её прекращает
      rxEarStart(false);
      clickKnob = longKnob = false;
    }
    switch (screen) {
      case S_MAIN:
        if (turn) {
          int v = cfg.volume + turn;
          if (v > 20 && cfg.rxVolMax < 20 && now - limitSaidAt > 2500) {   // шкала кончилась, а звук тише полного — сказать почему
            limitSaidAt = now;
            say(tr("Межа гучності"));
          }
          cfg.volume = v < 0 ? 0 : v > 20 ? 20 : v;
          rxMute = false;
          volShownAt = now;
          saveAt = now + 1000;
        }
        if (clickKnob) {
          rxMute = !rxMute;
          muteAt = now;
          if (!rxMute) say(tr("Звук увімкнено"));
        }
        if (longKnob) {
          screen = S_MENU;
          item = 0;
        }
        break;
      case S_MENU:
        if (turn) item = (item + turn + M_COUNT * 8) % M_COUNT;
        if (longKnob || (clickKnob && item == M_EXIT)) screen = S_MAIN;   // долгое нажатие — назад
        else if (clickKnob && item == M_SCREEN) {                          // погасить экран (включит любое движение ручки)
          screenOn = false;
          screen = S_MAIN;
        } else if (clickKnob && item == M_EARTEST) {                       // проверка наушников: тон по очереди в левое и правое
          rxEarStart(true);
          screen = S_EAR;
        } else if (clickKnob) screen = item == M_ABOUT ? S_ABOUT : item == M_LINK ? S_LINK : item == M_KIT ? S_MENU : S_EDIT;
        break;
      case S_EDIT:
        if (turn) {
          needRestart |= menuChange(item, turn);
          saveAt = now + 1500;
        }
        if (clickKnob || longKnob) {
          screen = S_MENU;
          if (item == M_FORGET) {
            if (clickKnob && menuForgetArm && secHave) {
              secForget();
              screen = S_MAIN;
            }
            menuForgetArm = false;
          }
          if (needRestart) {
            settingsSave();
            delay(100);
            ESP.restart();
          }
        }
        break;
      case S_ABOUT:
      case S_LINK:
        if (clickKnob || longKnob) screen = S_MENU;
        break;
      case S_EAR:   // нажатие — прекратить; сама кончается через четыре круга
        if (clickKnob || longKnob || !rxEarTest) {
          rxEarStart(false);
          screen = S_MENU;
        }
        break;
    }
    if (saveAt && now >= saveAt) {
      saveAt = 0;
      settingsSave();
    }
    rxUiBusy = screen != S_MAIN;
    // спектр виден только на главном экране в работе, при включённом экране и без «тиші» — тогда и копить для него звук
    rxSpecOn = uiHaveOled && show && pw == PW_RUN && screen == S_MAIN && !rxMute && cfg.rxView == 0;
    rxUiAwake = now < peekUntil || now < rxIdentifyUntil || keyStable != 0;   // в ожидании: пока на экране подсказка — не засыпать

    // --- рисование
    if (uiHaveOled && show) {
      u8g2_t *g = oled.getU8g2();
      oled.clearBuffer();
      float dtMs = (float)(now - frameAt > 100 ? 100 : now - frameAt);
      frameAt = now;
      bool signal = msSince(rLastRxMs, now) < 500 && rLastRxMs;
      bool sound = rxSoundMs && msSince(rxSoundMs, now) < 1500;
      // ---- оповещения: что изменилось с прошлого кадра
      if (pw == PW_RUN) {
        if (signal && !sigWas && lostAt && now - lostAt > 1500) say(tr("Сигнал відновлено"));
        if (!signal && sigWas) lostAt = now ? now : 1;
        uint8_t fl = rxFlags;
        if (signal) {
          if ((fl & FLAG_CLIP) && !(flagsWas & FLAG_CLIP) && now - clipSaidAt > 8000) {
            clipSaidAt = now;
            say(tr("Гучно на вході!"));
          }
          if ((fl & FLAG_MUTE) && !(flagsWas & FLAG_MUTE)) say(tr("Тиша в ефірі"));
          if (!(fl & FLAG_MUTE) && (flagsWas & FLAG_MUTE)) say(tr("Звук в ефірі"));
          if (rRssi < -84) {
            if (!weakSince) weakSince = now ? now : 1;
            if (now - weakSince > 4000 && (!weakSaidAt || now - weakSaidAt > 30000)) {
              weakSaidAt = now ? now : 1;
              say(tr("Слабкий сигнал"));
            }
          } else weakSince = 0;
        }
        flagsWas = fl;
        if (cfg.volume != volWas && !turn && !rxMute) volShownAt = now ? now : 1;   // громкость сменили с передатчика — показать
        if ((bool)cfg.rxStereo != stereoWas) say(cfg.rxStereo ? tr("Вихід: стерео") : tr("Вихід: моно"));
        uint32_t ns = 5381;
        for (const char *c = rxName(); *c; c++) ns = ns * 33 + (uint8_t)*c;
        if (nameSum && ns != nameSum) say(tr("Нове ім'я приймача"));
        nameSum = ns;
      }
      for (int p = 0; p < RXP_COUNT; p++) {   // настройку сменили не ручкой (с передатчика) — сказать, что и на что
        int v = rxParamGet(p);
        if (v != parWas[p] && screen != S_EDIT) {
          char val[24], msg[44];
          menuValue(PARAM_ITEM[p], val, sizeof(val));
          snprintf(msg, sizeof(msg), "%s: %s", tr(MENU_NAME[PARAM_ITEM[p]]), val);
          say(msg);
        }
        parWas[p] = v;
      }
      sigWas = signal;
      volWas = cfg.volume;
      stereoWas = cfg.rxStereo;
      // ---- какой экран рисуем
      uint8_t sid;
      if (otaShow) sid = SC_UPDATE;
      else if (now < rxIdentifyUntil) sid = SC_IDENT;
      else if (pw == PW_STANDBY) sid = SC_SLEEP;
      else if (pw == PW_WAKING || (pw == PW_SEARCH && why != WHY_KEY && pt < RX_SPLASH_MS)) sid = SC_SPLASH;
      else if (rxEarTest) sid = SC_EAR;
      else if (!secHave && screen == S_MAIN) sid = SC_UNPAIRED;
      else if (pw == PW_GOING) sid = SC_GOING;
      else if (pw == PW_STARTING) sid = SC_STARTING;
      else if (pw == PW_SEARCH && screen == S_MAIN) sid = SC_SEARCH;
      else if (screen == S_MAIN && rxMute) sid = SC_MUTE;
      else if (screen == S_MAIN) sid = signal ? SC_MAIN : SC_LOST;
      else if (screen == S_ABOUT) sid = SC_ABOUT;
      else if (screen == S_LINK) sid = SC_LINK;
      else sid = SC_MENU;
      if (sid != sidWas) {   // экран сменился — переход: прежний кадр растворяется, съезжает или раскрывается кругом
        sidAt = now;
        trKind = sidWas == SC_MAIN && sid == SC_MENU ? rxs::TR_UP : sidWas == SC_MENU && (sid == SC_MAIN || sid == SC_MUTE || sid == SC_LOST) ? rxs::TR_DOWN
                 : sidWas == SC_MENU && (sid == SC_ABOUT || sid == SC_LINK) ? rxs::TR_LEFT : (sidWas == SC_ABOUT || sidWas == SC_LINK) && sid == SC_MENU ? rxs::TR_RIGHT
                 : sid == SC_MUTE || sidWas == SC_MUTE || sid == SC_EAR || sidWas == SC_EAR ? rxs::TR_IRIS : sidWas == SC_SPLASH ? rxs::TR_WIPE : rxs::TR_DISSOLVE;
        trMs = trKind == rxs::TR_WIPE ? 320 : trKind == rxs::TR_IRIS ? 300 : trKind == rxs::TR_DISSOLVE ? 240 : 260;
        trAt = oledPrevOk && sidWas != SC_NONE && sidWas != SC_SLEEP && sid != SC_SLEEP && sid != SC_SPLASH ? (now ? now : 1) : 0;
        if (trAt) memcpy(trFrom, oledPrev, sizeof(trFrom));
        if (sid == SC_MENU && sidWas != SC_ABOUT && sidWas != SC_LINK && sidWas != SC_EAR) {   // меню открыли: плашка сразу на выбранной строке
          selF = item;
          scrollF = 0;
        }
        if (sid == SC_MAIN && sidWas == SC_STARTING) say(tr("Передавач на зв'язку"));
        sidWas = sid;
      }
      // ---- плавные величины
      {
        float a = 1 - expf(-dtMs / 180.0f);
        int bt = !signal ? 0 : rRssi > -55 ? 4 : rRssi > -67 ? 3 : rRssi > -78 ? 2 : rRssi > -88 ? 1 : 0;
        barsF += (bt - barsF) * a;
        volF += (cfg.volume - volF) * (1 - expf(-dtMs / 70.0f));
        bool volOn = sid == SC_MAIN && volShownAt && now - volShownAt < 1500;
        volK += volOn ? dtMs / 160.0f : -dtMs / 220.0f;
        volK = volK < 0 ? 0 : volK > 1 ? 1 : volK;
        if (cfg.channel != chShown && now - chAt >= 110) {   // цифра канала сменяется не чаще девяти раз в секунду — иначе её не прочесть
          chPrev = chShown;
          chShown = cfg.channel;
          chAt = now;
        }
        if (fabsf(chShown - chPosF) > 6) chPosF = chShown;   // с 13-го на 1-й — отметка не едет через всю шкалу
        chPosF += (chShown - chPosF) * (1 - expf(-dtMs / 60.0f));
        // уровень каналов для стрелок: шкала от −48 дБ до полной; вверх за 45 мс, вниз за 300 мс; пик держится 0,6 с
        int pl = rxVuL, pr = rxVuR;
        rxVuL = 0;
        rxVuR = 0;
        auto lvl = [](int pk) {
          if (pk < 130 || rxMute) return 0.0f;
          float v = (20 * log10f(pk / 32768.0f) + 48) / 48;
          return v < 0 ? 0.0f : v > 1 ? 1.0f : v;
        };
        float tl = signal ? lvl(pl) : 0, trg = signal ? lvl(pr) : 0;
        vuLF += (tl - vuLF) * (1 - expf(-dtMs / (tl > vuLF ? 45.0f : 300.0f)));
        vuRF += (trg - vuRF) * (1 - expf(-dtMs / (trg > vuRF ? 45.0f : 300.0f)));
        if (vuLF >= vuLP) {
          vuLP = vuLF;
          vuLPAt = now;
        } else if (now - vuLPAt > 600) vuLP = vuLP - dtMs * 0.0012f < vuLF ? vuLF : vuLP - dtMs * 0.0012f;
        if (vuRF >= vuRP) {
          vuRP = vuRF;
          vuRPAt = now;
        } else if (now - vuRPAt > 600) vuRP = vuRP - dtMs * 0.0012f < vuRF ? vuRF : vuRP - dtMs * 0.0012f;
      }
      rxs::ScanInfo si;
      si.label = cfg.autoChannel ? tr("шукаю: канал") : tr("канал");
      si.ch = chShown;
      si.prevCh = chPrev;
      si.roll = (now - chAt) / 170.0f;
      si.pos = chPosF;
      // ---- сам экран
      switch (sid) {
        case SC_UPDATE: {   // обновление прошивки по радио — поверх всего остального
          char pc[24];
          uint8_t e = otaRx.err;
          snprintf(buf, sizeof(buf), tr("прошивка %s"), otaRx.version);
          snprintf(pc, sizeof(pc), "%d %%", otaRx.percent);
          if (otaPh == OS_RECV) rxs::update(g, now, tr("приймаю з передавача"), otaRx.percent, pc);
          else if (otaPh == OS_CHECK) rxs::update(g, now, tr("перевіряю"), 100, buf);
          else if (otaPh == OS_WRITE) rxs::update(g, now, tr("записую, не вимикайте"), otaRx.percent, pc);
          else if (otaPh == OS_DONE) rxs::update(g, now, tr("готово, перезапуск"), 100, buf);
          else rxs::update(g, now, tr("не вдалося"), -1,
                           e == OE_NOSLOT ? tr("потрібен USB (раз)") : e == OE_LOST ? tr("передавач зник") : e == OE_HASH ? tr("помилка в даних")
                           : e == OE_NOMEM ? tr("забракло пам'яті") : tr("помилка запису"));
          break;
        }
        case SC_IDENT: rxs::identify(g, now, rxName()); break;   // «покажи себя»: кольца, имя, вспышки
        case SC_SLEEP:   // экран разбужен ручкой
          if (why != WHY_NOSIGNAL) rxs::sleeping(g, now - (peekUntil - 5000));
          break;
        case SC_SPLASH: rxs::splash(g, pt); break;
        case SC_UNPAIRED:   // не подключён к набору: просит доступ, на экране код для сверки
          rxs::unpaired(g, now, rxPairCode, rLastAnyMs && msSince(rLastAnyMs, now) < 1500);
          break;
        case SC_GOING: rxs::going(g, pt, RX_GOING_MS, why == WHY_OFF); break;
        case SC_STARTING:
          snprintf(buf, sizeof(buf), tr("набір %u  версія %s"), cfg.kit, FW_VERSION);
          rxs::starting(g, pt, RX_START_MS, rxName(), buf);
          break;
        case SC_SEARCH: {
          uint32_t from = why == WHY_KEY ? 0 : RX_SPLASH_MS;
          si.title = tr("ПОШУК ПЕРЕДАВАЧА");
          si.left = 1 - (float)(pt > from ? pt - from : 0) / (RX_WAIT_MS - from);
          rxs::search(g, pt, si);
          break;
        }
        case SC_MUTE: rxs::muted(g, now - muteAt); break;
        case SC_LOST:   // сигнал пропал: полоса внизу — сколько осталось до ухода в ожидание
          si.title = tr("НЕМАЄ СИГНАЛУ");
          si.left = 1 - (float)msSince(rLastRxMs, now) / RX_WAIT_MS;
          si.lost = true;
          rxs::search(g, now - sidAt, si);
          break;
        case SC_MAIN:   // вид — по настройке «Вигляд»: спектр, стрелочные индикаторы или крупная громкость
          rxs::statusBar(g, now, barsF, rxName(), cfg.rxView == 2 ? -1 : cfg.volume * 5, sound);
          if (cfg.rxView == 1) rxs::needles(g, now, vuLF, vuRF, vuLP, vuRP, cfg.rxStereo && rxAirStereo);
          else if (cfg.rxView == 2) rxs::bigVolume(g, now, volF, 20, vuLF > vuRF ? vuLF : vuRF);
          else {
            specUpdate();                       // разбор — на каждом кадре
            specAnimate(now, (float)(now - specAt > 100 ? 100 : now - specAt));
            specAt = now;
            rxs::spectrum(g, barDraw, barPeak, BANDS, now);
          }
          if (cfg.rxView != 2) rxs::volumeCard(g, now, volF, volK);   // громкость — карточкой поверх (в виде «гучність» она и так крупно)
          break;
        case SC_MENU: {
          static char vals[M_COUNT][24];
          rxs::MenuRow rows[M_COUNT];
          for (int it = 0; it < M_COUNT; it++) {
            menuValue(it, vals[it], sizeof(vals[it]));
            rows[it] = rxs::MenuRow{ tr(MENU_NAME[it]), vals[it], MENU_ICON[it] };
          }
          int first = item < 2 ? 0 : item > M_COUNT - 3 ? M_COUNT - 4 : item - 1;
          float a = 1 - expf(-dtMs / 55.0f);
          if (fabsf(item - selF) > 4) selF = item;   // с последней строки на первую — без проезда через весь список
          selF += (item - selF) * a;
          scrollF += (first * 16 - scrollF) * a;
          rxs::menuList(g, now, rows, M_COUNT, selF, scrollF, screen == S_EDIT);
          break;
        }
        case SC_ABOUT:
          oled.setFont(u8g2_font_6x13_t_cyrillic);
          rxs::ticker(g, 0, 128, 12, rxName(), now, 0, 14);
          snprintf(buf, sizeof(buf), tr("в.%s  К%u Н%u  %02X%02X%02X"), FW_VERSION, cfg.channel, cfg.kit, rxId[0], rxId[1], rxId[2]);
          oled.drawUTF8(0, 27, buf);
          snprintf(buf, sizeof(buf), tr("запас %.1f мс"), rxFillSm * 1000.0f / SRATE);
          oled.drawUTF8(0, 42, buf);
          snprintf(buf, sizeof(buf), tr("сигнал %d дБм"), (int)rRssi);
          oled.drawUTF8(0, 57, buf);
          break;
        case SC_LINK: {   // качество связи: сейчас и за последнюю минуту
          rxs::LinkInfo li;
          li.signal = signal;
          li.rssi = rRssi;
          li.bars = barsF;
          li.reserveMs = rxFillSm * 1000.0f / SRATE;
          li.hr = lnkRssi;
          li.hl = lnkLoss;
          li.pos = lnkPos;
          li.n = LNK_N;
          li.lossPct = li.pos ? lnkLoss[(li.pos - 1) % LNK_N] / 10.0f : 0;
          li.frac = msSince(lnkAt, now) / 1000.0f;
          if (li.frac > 1) li.frac = 1;
          rxs::linkScreen(g, now, li);
          break;
        }
        case SC_EAR: rxs::earTest(g, now, rxEarSide(now), cfg.rxStereo); break;
      }
      // ---- оповещение: плашка сверху (выезжает за четверть секунды, держится полторы, уезжает)
      if (toastTail != toastHead) {
        bool fits = sid == SC_MAIN || sid == SC_LOST || sid == SC_MENU || sid == SC_ABOUT || sid == SC_MUTE || sid == SC_SEARCH || sid == SC_LINK;
        if (!toastAt) toastAt = now ? now : 1;
        uint32_t te = now - toastAt;
        if (!fits || te > 1960) {
          toastTail = (toastTail + 1) & 3;
          toastAt = 0;
        } else rxs::toast(g, toastQ[toastTail], te < 260 ? te / 260.0f : te < 1760 ? 1.0f : 1 - (te - 1760) / 200.0f);
      }
      // ---- переход от прежнего экрана
      if (trAt) {
        float k = (float)(now - trAt) / trMs;
        if (k >= 1) trAt = 0;
        else rxs::blend(g, trFrom, k, trKind);
      }
      uint32_t f0 = micros();
      Wire.setClock(oledFast ? 800000 : 400000);
      oledFlush();
      uint32_t fu = micros() - f0;
      oledFrames = oledFrames + 1;
      oledFrameUsSum = oledFrameUsSum + fu;
      if (fu > oledFrameUsMax) oledFrameUsMax = fu;
      // 60 кадров в секунду: ровно по часам, а не «сколько успеем» (успевает и 150, но это лишняя работа процессору)
      static TickType_t tick;
      if ((int32_t)(xTaskGetTickCount() - tick) > 40) tick = xTaskGetTickCount();
      vTaskDelayUntil(&tick, pdMS_TO_TICKS(16));
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}
