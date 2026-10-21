// Экран приёмника: модуль M75 (SH1106 128×64 + ручка EC11 + кнопки CON и BAK).
//
// Обычно на экране спектр звука и строка состояния. Поворот ручки — громкость, нажатие — тишина,
// долгое нажатие — меню. BAK — назад (на главном экране — тишина), CON — погасить экран.
// Экран не обязателен: если он не отвечает, приёмник играет без него и раз в две секунды ищет экран снова.
//
// Заставка, поиск передатчика, «запуск», уход в ожидание — по состоянию приёмника (rxPower, см. rxaudio.h);
// сами картинки с движением — в rxscreens.h. В ожидании экран погашен; нажатие ручки его будит:
// если ждали сигнала — приёмник ещё 10 с ищет передатчик, если выключен с передатчика — надпись, что включает его
// только передатчик (до 2.64 приёмник можно было включить самому двойным нажатием ручки).
#pragma once
#include <Wire.h>
#include <U8g2lib.h>
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "esp_intr_alloc.h"
#include "soc/interrupts.h"
#include "esp_timer.h"   // опрос кнопки ручки (с 3.09)
#include "dsps_fft2r.h"  // esp-dsp: БПФ на векторных командах ESP32-S3 (разбор спектра, с 3.12)
#include "rxaudio.h"
#include "rxlink.h"
#include "rxscreens.h"
#include "rxlcd.h"   // цветной экран IPS 1,9″ (с 3.03): рисование — rxcolor.h, вывод полосами — rxstrip.h

void settingsSave();   // в hearlink.ino

static U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
static volatile bool uiHaveOled;
static volatile bool oledFast;         // экран работает на 800 кГц
static volatile uint32_t oledFrames, oledFrameUsMax, oledFrameUsSum;   // для отчёта: кадров и время кадра
static volatile uint32_t oledDrawUsSum, oledDrawUsMax;                 // и рисования кадра (с 2.94; с разбором спектра)
static volatile uint32_t specUsSum;    // из них — разбор спектра и движение рисок (с 3.09)
static volatile bool oledFlushHigh = true;   // кадр уходит на экран с приоритетом выше звука (с 2.93; Y12 — как до 2.93, Y13 — снова так)
static volatile int32_t encCount;      // четверть-шаги ручки
static volatile uint8_t encState;
static volatile uint32_t uiKeys;       // состояние кнопок сейчас: бит 0 — ручка, 1 — CON, 2 — BAK (для проверки сборки)
static volatile bool uiHaveColor;      // выбран и запущен цветной экран (с 3.03; выбор — перемычкой PIN_RX_COLOR с 3.13)
static bool rxColorJumper;             // перемычка вывода 21 на «землю» стоит (прочитано при запуске)
static volatile uint32_t lcdSentSum;   // для отчёта: сколько точек ушло на цветной экран
static volatile uint32_t lcdCycR, lcdCycH, lcdCycS, lcdCycW;   // и такты кадра: рисование, отпечатки, отправка, ожидание DMA (с 3.05)
static rxc::Scene *scn, *scnOld;       // сцена нынешнего кадра и прежнего экрана (для перехода) — во внешней памяти
// Яркость 1…5 (с 3.03): у цветного экрана — подсветка, у OLED — контраст (3 — 200, как было до 3.03).
static void dispBright() {
  static const uint8_t OLED_CONTRAST[6] = { 0, 30, 100, 200, 230, 255 };
  uint8_t b = cfg.rxBright < 1 ? 1 : cfg.rxBright > 5 ? 5 : cfg.rxBright;
  if (uiHaveColor) rxLcdBright(b);
  else if (uiHaveOled) oled.setContrast(OLED_CONTRAST[b]);
}

// Быстрый вывод кадра на экран SH1106. Библиотека шлёт кадр (1 КБ) примерно сорока мелкими посылками, и на них
// уходило 25 мс при любой скорости шины — 33 кадра в секунду, спектр шёл рывками (владелец 07.10: «не вижу плавности
// анализатора спектра на приемнике»). Здесь кадр сравнивается с тем, что уже на экране, и по каждой из восьми строк
// экрана уходит одна посылка — только с изменившимися столбцами. Рисует по-прежнему библиотека, в свой лист.
static uint8_t *oledPrev;   // с 3.03 — из кучи при запуске экрана (только у приёмника; см. specBuf в rxaudio.h)
static uint8_t *oledStatic;            // строка состояния главного экрана, как нарисована на чистом листе (с 3.12)
static uint32_t oledStaticKey;
static volatile bool oledCache = true;   // Y18 — строка из запаса (с 3.12), Y17 — каждый кадр заново
static bool oledPrevOk;
static void oledFlush() {
  const uint8_t *buf = oled.getBufferPtr();
  if (!oledPrev) oledPrevOk = false;   // памяти под прежний кадр нет — строки целиком
  for (int p = 0; p < 8; p++) {
    const uint8_t *row = buf + p * 128;
    uint8_t *old = oledPrev ? oledPrev + p * 128 : nullptr;
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
    if (oledPrev) memcpy(old + x0, row + x0, x1 - x0 + 1);
  }
  oledPrevOk = oledPrev != nullptr;
}

// Кнопка ручки — опрос раз в 5 мс отдельным таймером (с 3.09), а не раз в кадр. До 3.09 защита от дребезга ждала
// 4 одинаковых отсчёта подряд по одному на кадр: при медленном кадре (цветной экран, «тиша» — 30–40 мс) кнопку надо
// было держать 120–160 мс, и короткое нажатие пропадало (владелец 15.10: «выйти из режима тишины не получается с
// первого раза»). Теперь 4 одинаковых отсчёта — это 15–20 мс при любом кадре, и нажатие с отпусканием между двумя
// кадрами не теряется: счётчики только растут, кадр смотрит, выросли ли они.
static volatile uint8_t knobDown;                     // ручка нажата (после защиты от дребезга)
static volatile uint32_t knobPresses, knobReleases;   // сколько раз нажали и отпустили
static void knobPoll(void *) {
  static uint8_t last, same;
  uint8_t raw = !gpio_get_level((gpio_num_t)PIN_ENC_SW);
  if (raw != last) {
    last = raw;
    same = 0;
    return;
  }
  if (same < 3) same++;
  if (same == 3 && raw != knobDown) {
    knobDown = raw;
    if (raw) knobPresses = knobPresses + 1;
    else knobReleases = knobReleases + 1;
  }
}
static void knobStart() {
  static esp_timer_handle_t tm;
  if (tm) return;
  esp_timer_create_args_t a = {};
  a.callback = knobPoll;
  a.name = "knob";
  if (esp_timer_create(&a, &tm) == ESP_OK) esp_timer_start_periodic(tm, 5000);
}

// Проверка помехи от шины цветного экрана радио (с 3.09, команда Y22). Владелец 15.10: «обновления картинки отдаются
// легкими щелчками в звуке… при статической картинке звук чистый… особенно при слабом сигнале; на олед такого не
// слышно». Подозрение — гармоники шины SPI (40 МГц: 2440 МГц — 61-я, внутри канала 5) у антенны. Фазы по 5 с: экран
// стоит / шина гонит кадр целиком без остановки — при разной силе выводов шины. Шум радио (как Qn у передатчика)
// считается здесь, каждый кадр; потери пакетов — в ежесекундном отчёте (hearlink.ino, reportRx) по фазе, в которой
// прошла секунда.
struct LcdEmiPhase {
  bool stress;
  int8_t cap;   // сила выводов: −1 — как обычно (2)
  const char *name;
};
static const LcdEmiPhase LCD_EMI[] = { { false, -1, "екран стоїть" }, { true, -1, "шина без упину, виводи за налаштуванням" }, { false, -1, "екран стоїть" },
                                       { true, 0, "шина без упину, виводи 0 (найслабші)" }, { true, 1, "шина без упину, виводи 1" },
                                       { true, 2, "шина без упину, виводи 2 (до 3.11)" }, { false, -1, "екран стоїть" } };
constexpr int LCD_EMI_N = sizeof(LCD_EMI) / sizeof(LCD_EMI[0]);
constexpr uint32_t LCD_EMI_MS = 5000;
static volatile int8_t lcdEmiPh = -1;   // идущая фаза; −1 — проверки нет
static volatile bool lcdEmiDone;        // проверка кончилась — отчёт напечатает reportRx
static uint32_t lcdEmiAt, lcdEmiN[LCD_EMI_N], lcdEmiFr[LCD_EMI_N], lcdEmiRec[LCD_EMI_N], lcdEmiLost[LCD_EMI_N], lcdEmiUnder[LCD_EMI_N],
  lcdEmiSec[LCD_EMI_N], lcdEmiFrames[LCD_EMI_N];
static int32_t lcdEmiNf[LCD_EMI_N], lcdEmiNfHw[LCD_EMI_N], lcdEmiNfMax[LCD_EMI_N];
static float lcdEmiBusy[LCD_EMI_N];
static void lcdEmiStart() {
  for (int i = 0; i < LCD_EMI_N; i++) {
    lcdEmiN[i] = lcdEmiFr[i] = lcdEmiRec[i] = lcdEmiLost[i] = lcdEmiUnder[i] = lcdEmiSec[i] = lcdEmiFrames[i] = 0;
    lcdEmiNf[i] = lcdEmiNfHw[i] = 0;
    lcdEmiNfMax[i] = -32768;
    lcdEmiBusy[i] = 0;
  }
  lcdEmiDone = false;
  lcdEmiAt = millis();
  rxLcdDrive(LCD_EMI[0].cap);
  lcdEmiPh = 0;
}
// Y23[=сила] — 8 с кадр целиком без остановки, послушать наводку в звуке (с 3.11); сила выводов на это время — по
// команде (0…3) или по настройке.
static volatile uint32_t lcdStressUntil;
static void lcdEmiFrame(uint32_t now) {   // зовёт задача экрана перед каждым кадром цветного экрана
  if (lcdStressUntil) {
    if ((int32_t)(now - lcdStressUntil) >= 0) {
      lcdStressUntil = 0;
      rxLcdDrive(-1);
    }
    rxLcdStrips->invalidate();
  }
  int ph = lcdEmiPh;
  if (ph < 0) return;
  if (now - lcdEmiAt >= LCD_EMI_MS) {
    if (++ph >= LCD_EMI_N) {
      rxLcdDrive(-1);
      lcdEmiPh = -1;
      lcdEmiDone = true;
      rxLcdStrips->invalidate();
      return;
    }
    lcdEmiAt = now;
    rxLcdDrive(LCD_EMI[ph].cap);
    lcdEmiPh = ph;
  }
  int nf = phy_get_noise_floor(), nh = read_hw_noisefloor();
  uint32_t c[2] = { 0, 0 };
  phy_get_cca_cnt(c);
  lcdEmiNf[ph] += nf;
  lcdEmiNfHw[ph] += nh;
  if (nf > lcdEmiNfMax[ph]) lcdEmiNfMax[ph] = nf;
  lcdEmiBusy[ph] += c[1] ? (float)c[0] / c[1] : 0;
  lcdEmiN[ph]++;
  lcdEmiFrames[ph]++;
  if (LCD_EMI[ph].stress) rxLcdStrips->invalidate();   // кадр целиком: на экран — все 54 400 точек
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

// ---- спектр: окно Ханна, преобразование Фурье, полосы с логарифмическим шагом от 100 Гц до 8 кГц: у OLED — 14, у
// цветного экрана — 10 (с 3.08, владелец 15.10: «столбцов оставить 10»)
#define BANDS 14                        // наибольшее число полос
static uint8_t specBands = BANDS;       // сколько считается сейчас (specSetBands)
// массивы разбора — из кучи при запуске экрана (с 3.03, только у приёмника; см. specBuf в rxaudio.h)
static float *fftRe, *fftIm, *fftWin, *fftCos, *fftSin;
static uint16_t *fftRev;
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
  for (int b = 0; b < specBands; b++) {
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

// С 3.12 разбор спектра — esp-dsp (dsps_fft2r_fc32: у ESP32-S3 — на векторных командах): замер 15.10 — свой разбор
// стоил 1,1–1,25 мс на кадр, самая большая часть кадра обоих экранов (цветной: 3,6–5,9 мс, OLED: 4,4). Свой оставлен
// для сравнения: Y19 — свой, Y20 — esp-dsp, Y21 — оба на одном окне звука, разница по полосам.
static float *fftBuf;              // esp-dsp: пары (re, im), выровнено на 16 байт
static bool fftDsp;                // esp-dsp готов
static volatile bool fftUseDsp = true;
static volatile bool fftCmpAsk, fftCmpReady;   // Y21: сравнить оба разбора на одном окне
static float fftCmpA[BANDS], fftCmpB[BANDS];
static uint32_t fftCmpUsA, fftCmpUsB;
static void specInit() {
  fftBuf = (float *)heap_caps_aligned_alloc(16, SPEC_N * 2 * sizeof(float), MALLOC_CAP_INTERNAL);
  fftDsp = fftBuf && dsps_fft2r_init_fc32(nullptr, SPEC_N) == ESP_OK;
  fftRe = (float *)heap_caps_malloc(SPEC_N * sizeof(float), MALLOC_CAP_INTERNAL);
  fftIm = (float *)heap_caps_malloc(SPEC_N * sizeof(float), MALLOC_CAP_INTERNAL);
  fftWin = (float *)heap_caps_malloc(SPEC_N * sizeof(float), MALLOC_CAP_INTERNAL);
  fftCos = (float *)heap_caps_malloc(SPEC_N / 2 * sizeof(float), MALLOC_CAP_INTERNAL);
  fftSin = (float *)heap_caps_malloc(SPEC_N / 2 * sizeof(float), MALLOC_CAP_INTERNAL);
  fftRev = (uint16_t *)heap_caps_malloc(SPEC_N * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
  if (!fftRe || !fftIm || !fftWin || !fftCos || !fftSin || !fftRev) {
    Serial.println("спектр: немає пам'яті — на екрані його не буде");
    fftRe = nullptr;
    return;
  }
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
}
static void specSetBands(int n) {   // границы полос: n полос от 100 Гц до 8 кГц
  specBands = n < 1 ? 1 : n > BANDS ? BANDS : n;
  float f = 100, k = powf(8000.0f / 100.0f, 1.0f / specBands), hz = (float)SRATE / SPEC_N;
  for (int b = 0; b < specBands; b++) {
    int lo = (int)ceilf(f / hz), hi = (int)floorf(f * k / hz);
    if (lo < 1) lo = 1;
    if (hi < lo) hi = lo;
    bandLo[b] = lo;
    bandHi[b] = hi;
    f *= k;
  }
}

// Мощность полос (наибольшая по столбцам БПФ полосы) окна звука src с места pos (src — кольцо из SPEC_N отсчётов) —
// своим разбором (как до 3.12) …
static void specPowOwn(const int16_t *src, uint32_t pos, float *pw) {
  for (int i = 0; i < SPEC_N; i++) {
    fftRe[fftRev[i]] = src[(pos + i) & (SPEC_N - 1)] * fftWin[i] * (1.0f / 32768);
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
  for (int b = 0; b < specBands; b++) {
    float p = 0;
    for (int i = bandLo[b]; i <= bandHi[b]; i++) {
      float m = fftRe[i] * fftRe[i] + fftIm[i] * fftIm[i];
      p += m;   // мощность полосы (с 4.00; было — наибольший столбец)
    }
    pw[b] = p;
  }
}
// … и esp-dsp: результат у него — с переставленными битами номера столбца (X[k] — на месте fftRev[k]); переставлять
// весь массив незачем — нужны только столбцы до 8 кГц.
static void specPowDsp(const int16_t *src, uint32_t pos, float *pw) {
  for (int i = 0; i < SPEC_N; i++) {
    fftBuf[2 * i] = src[(pos + i) & (SPEC_N - 1)] * fftWin[i] * (1.0f / 32768);
    fftBuf[2 * i + 1] = 0;
  }
  dsps_fft2r_fc32(fftBuf, SPEC_N);
  for (int b = 0; b < specBands; b++) {
    float p = 0;
    for (int i = bandLo[b]; i <= bandHi[b]; i++) {
      const float *c = fftBuf + 2 * fftRev[i];
      float m = c[0] * c[0] + c[1] * c[1];
      p += m;   // мощность полосы (с 4.00; было — наибольший столбец)
    }
    pw[b] = p;
  }
}
// Полная шкала синуса после окна даёт (N/4)². С 4.00 — мощность полосы без наклона, как у анализатора пульта (розовый
// шум — ровно); до 4.00 брался наибольший столбец полосы и прибавлялось ~3 дБ на октаву — рядом с пультом верх
// выглядел на 15–20 дБ громче, чем есть (владелец 21.10: «чуть ли не зеркальный анализатор вижу на пульте»).
static float specDb(float p, int b) {
  (void)b;
  return 10 * log10f(p / ((SPEC_N / 4.0f) * (SPEC_N / 4.0f)) + 1e-12f);
}
static void specUpdate() {
  if (!fftRe || !specBuf) return;
  uint32_t pos = specPos;
  float pw[BANDS];
  if (fftCmpAsk) {   // Y21: оба разбора на одной копии окна (кольцо, пока считаем, дописывает задача звука)
    static int16_t *win;   // из кучи при первой сверке: статический массив занял бы 1 КБ и у передатчика
    if (!win) win = (int16_t *)heap_caps_malloc(SPEC_N * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    if (!win) {
      fftCmpAsk = false;
      return;
    }
    for (int i = 0; i < SPEC_N; i++) win[i] = specBuf[(pos + i) & (SPEC_N - 1)];
    uint32_t t0 = micros();
    specPowOwn(win, 0, pw);
    uint32_t t1 = micros();
    for (int b = 0; b < specBands; b++) fftCmpA[b] = specDb(pw[b], b);
    if (fftDsp) specPowDsp(win, 0, pw);
    uint32_t t2 = micros();
    for (int b = 0; b < specBands; b++) fftCmpB[b] = fftDsp ? specDb(pw[b], b) : 0;
    fftCmpUsA = t1 - t0;
    fftCmpUsB = t2 - t1;
    fftCmpAsk = false;
    fftCmpReady = true;
  }
  if (fftDsp && fftUseDsp) specPowDsp(specBuf, pos, pw);
  else specPowOwn(specBuf, pos, pw);
  for (int b = 0; b < specBands; b++) {
    float lv = (specDb(pw[b], b) + 66) / 60;   // низ шкалы −66 дБ, верх −6 дБ (с 4.00 шире: без наклона верх ниже)
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
// С 2.68 за «Підсилення» — обработка «для слуха» (nsfx.h): как оно ведёт себя у края шкалы, что делать в паузах и
// шумозаглушение. Те же три настройки меняются с передатчика (окно приёмника → «Обробка звуку»).
// С 2.72 — и их настройки (владелец 11.10: «добавить настройки для шумодавов»): порог и закрытие пауз, чувствительность
// шумозаглушения и образец шума.
enum { M_VIEW, M_LED, M_LANG, M_CLARITY, M_BALANCE, M_VOLMAX, M_BOOST, M_COMP, M_GATE, M_GATETHR, M_GATEREL, M_NS, M_NSSENS, M_NSLEARN, M_EARTEST, M_LINK,
       M_KIT, M_CHANNEL, M_DEPTH, M_OUTPUT, M_STEREO, M_SCREEN, M_BRIGHT, M_FORGET, M_RESTART, M_ABOUT, M_EXIT, M_COUNT };
static const char *const MENU_NAME[M_COUNT] = { "Вигляд", "Світлодіод", "Мова", "Чіткість", "Баланс", "Межа гучн.", "Підсилення", "Стискання", "У паузах",
                                                "Поріг пауз", "Закриття", "Шумозагл.", "Чутл. шуму", "Зразок шуму", "Навушники", "Зв'язок",
                                                "Набір", "Канал", "Запас", "Вихід звуку", "Виводи", "Екран", "Яскравість", "Забути набір", "Перезапуск",
                                                "Про пристрій", "Вийти" };
static const uint8_t MENU_ICON[M_COUNT] = { 9, 10, 11, 12, 13, 14, 17, 19, 20, 20, 20, 18, 18, 18, 15, 16, 0, 1, 2, 3, 4, 5, 23, 6, 21, 7, 8 };   // значки — rxs::menuIcon / rxc::icon
// какой пункт меню отвечает за настройку RXP_… (для оповещения «настройку сменили с передатчика»)
static const uint8_t PARAM_ITEM[RXP_COUNT] = { M_CLARITY, M_BALANCE, M_VOLMAX, M_VIEW, M_LED, M_LANG };
// «Забути набір»: стереть ключ набора — приёмник снова попросит доступ (для перехода к другому передатчику или
// после смены ключа на своём). Нарочно в два действия: повернуть на «ТАК» и нажать.
static bool menuForgetArm;
static bool menuRestartArm;   // в пункте «Перезапуск» выбрано «ТАК»

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
    case M_BOOST:
      if (cfg.rxBoost) snprintf(s, n, tr("%s%d дБ"), "+", cfg.rxBoost * 2);
      else snprintf(s, n, "%s", tr("вимк."));
      break;
    case M_COMP:
      snprintf(s, n, "%s", cfg.rxComp == 0 ? tr("вимк.") : cfg.rxComp == 1 ? tr("легке") : cfg.rxComp == 2 ? tr("середнє") : tr("сильне"));
      break;
    case M_GATE:
      snprintf(s, n, "%s", cfg.rxGate == 0 ? tr("вимк.") : cfg.rxGate == 1 ? tr("без підс.") : cfg.rxGate == 2 ? tr("ще -6 дБ") : tr("ще -12 дБ"));
      break;
    case M_NS:
      snprintf(s, n, "%s", cfg.rxNs == 0 ? tr("вимк.") : cfg.rxNs == 1 ? tr("слабке") : cfg.rxNs == 2 ? tr("середнє") : tr("сильне"));
      break;
    case M_GATETHR: snprintf(s, n, tr("%s%d дБ"), "+", 3 + 3 * cfg.rxGateThr); break;
    case M_GATEREL: snprintf(s, n, "%s", cfg.rxGateRel == 0 ? tr("швидко") : cfg.rxGateRel == 2 ? tr("повільно") : tr("середньо")); break;
    case M_NSSENS: snprintf(s, n, tr("%d з 5"), cfg.rxNsSens + 1); break;
    case M_NSLEARN: snprintf(s, n, "%s", rxNsFrozen ? tr("взято") : tr("сам")); break;
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
    case M_OUTPUT: snprintf(s, n, "%s", cfg.output == OUT_CS4344 ? "CS4344" : cfg.output == OUT_I2S ? "PCM5102" : "PDM"); break;
    case M_STEREO: snprintf(s, n, "%s", cfg.rxStereo ? tr("2 канали") : tr("протифаза")); break;
    case M_SCREEN: snprintf(s, n, "%s", tr("вимкнути")); break;
    case M_BRIGHT: snprintf(s, n, tr("%d з 5"), cfg.rxBright); break;
    case M_FORGET: snprintf(s, n, "%s", !secHave ? tr("нема ключа") : menuForgetArm ? tr("ТАК") : tr("ні")); break;
    case M_RESTART: snprintf(s, n, "%s", menuRestartArm ? tr("ТАК") : tr("ні")); break;
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
    case M_BOOST: rxParamSet(RXP_X_BOOST, cfg.rxBoost + d); break;   // слышно сразу
    case M_COMP: rxParamSet(RXP_Y_COMP, cfg.rxComp + d); break;
    case M_GATE: rxParamSet(RXP_Y_GATE, cfg.rxGate + d); break;
    case M_NS: rxParamSet(RXP_Y_NS, cfg.rxNs + d); break;
    case M_GATETHR: rxParamSet(RXP_Y_GTHR, cfg.rxGateThr + d); break;
    case M_GATEREL: rxParamSet(RXP_Y_GREL, cfg.rxGateRel + d); break;
    case M_NSSENS: rxParamSet(RXP_Y_SENS, cfg.rxNsSens + d); break;
    case M_NSLEARN: rxParamSet(RXP_Y_LEARN, d > 0 ? 2 : 0); break;   // вправо — запомнить шум сейчас, влево — снова сам
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
    case M_OUTPUT: cfg.output = (uint8_t)((cfg.output + (d > 0 ? 1 : 2)) % 3); return true;   // PDM → PCM5102 → CS4344 → PDM
    case M_STEREO: cfg.rxStereo = !cfg.rxStereo; break;   // действует сразу: усилитель должен быть подключён соответственно
    case M_BRIGHT: {   // яркость 1…5: у цветного экрана — подсветка, у OLED — контраст; действует сразу
      int v = cfg.rxBright + d;
      cfg.rxBright = (uint8_t)(v < 1 ? 1 : v > 5 ? 5 : v);
      dispBright();
      break;
    }
    case M_FORGET: menuForgetArm = !menuForgetArm; break;
    case M_RESTART: menuRestartArm = !menuRestartArm; break;   // «ТАК» и нажатие — перезапуск (звук пропал, а до кнопки сброса не добраться)
  }
  return false;
}

static void drawCentered(int y, const char *s) {
  oled.drawUTF8((128 - oled.getUTF8Width(s)) / 2, y, s);
}

static void uiTask(void *) {
  const UBaseType_t uiPrio = uxTaskPriorityGet(NULL);   // свой приоритет (2): к нему возвращаемся после передачи кадра
  rxKeyPinsClean();   // до pinMode: иначе он включит на кнопке «унаследованное» прерывание (см. rxlink.h)
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  knobStart();
  pinMode(PIN_KEY_CON, INPUT_PULLUP);   // кнопки модуля не используются; выводы подтянуты, чтобы не «висели»
  pinMode(PIN_KEY_BAK, INPUT_PULLUP);
  encState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  encAttach();   // вместо attachInterrupt — см. выше
  Wire.setBufferSize(256);   // строка экрана (128 байт и заголовок) должна помещаться в одну посылку
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  specInit();
  // Тип экрана — перемычкой (с 3.13): вывод 21 на «землю» — цветной, свободен — OLED. Пять отсчётов по миллисекунде:
  // подтяжка внутри слабая, вывод «в воздухе» должен успеть подняться.
  pinMode(PIN_RX_COLOR, INPUT_PULLUP);
  delay(2);
  rxColorJumper = true;
  for (int i = 0; i < 5; i++, delay(1))
    if (digitalRead(PIN_RX_COLOR)) rxColorJumper = false;
  if (rxColorJumper) {   // цветной экран (с 3.03): у модуля нет линии чтения — определить его нельзя, только перемычкой
    scn = (rxc::Scene *)heap_caps_calloc(1, sizeof(rxc::Scene), MALLOC_CAP_SPIRAM);
    scnOld = (rxc::Scene *)heap_caps_calloc(1, sizeof(rxc::Scene), MALLOC_CAP_SPIRAM);
    if (scn && scnOld) {
      new (scn) rxc::Scene();
      new (scnOld) rxc::Scene();
      uiHaveColor = rxLcdBegin(cfg.rxDispOpt);
    }
    if (uiHaveColor) {
      rxScreenFound = true;
      rxScreenProbed = true;
    } else Serial.println("кольоровий екран не запустився (пам'ять або шина SPI) — працюю з OLED");
  }
  specSetBands(uiHaveColor ? rxc::AN_N : BANDS);

  enum { S_MAIN, S_MENU, S_EDIT, S_ABOUT, S_LINK, S_EAR } screen = S_MAIN;
  int32_t encSeen = 0;
  uint8_t keyStable = 0, pwPrev = 255;
  uint32_t kpSeen = knobPresses, krSeen = knobReleases;   // счётчики кнопки, уже учтённые
  // экран ищем сразу, а не через две секунды: иначе заставку при включении никто не увидит
  uint32_t pressAt = 0, volShownAt = 0, lastProbe = millis() - 2000, saveAt = 0, peekUntil = 0, muteAt = 0;
  bool longDone = false, screenOn = true, needRestart = false, dispOn = true;
  uint32_t specAt = 0;
  int item = 0;
  char buf[40];
  // --- движение на экране (rxfx.h): переходы между экранами, плавные величины, оповещения
  enum { SC_NONE, SC_UPDATE, SC_IDENT, SC_SLEEP, SC_SPLASH, SC_UNPAIRED, SC_GOING, SC_STARTING, SC_SEARCH, SC_MUTE, SC_MAIN, SC_LOST, SC_MENU, SC_ABOUT,
         SC_LINK, SC_EAR };
  static_assert((int)SC_UPDATE == (int)rxc::SC_UPDATE && (int)SC_MAIN == (int)rxc::SC_MAIN && (int)SC_EAR == (int)rxc::SC_EAR, "виды экрана — как в rxcolor.h");
  static uint8_t *trFrom;                  // прежний кадр — из него делается переход к новому экрану (с 3.03 — из кучи)
  if (!trFrom) trFrom = (uint8_t *)heap_caps_malloc(1024, MALLOC_CAP_INTERNAL);
  if (!oledPrev) oledPrev = (uint8_t *)heap_caps_calloc(1, 1024, MALLOC_CAP_INTERNAL);
  if (!oledStatic) oledStatic = (uint8_t *)heap_caps_calloc(1, 1024, MALLOC_CAP_INTERNAL);
  uint8_t sidWas = SC_NONE, trKind = 0;
  uint32_t trAt = 0, trMs = 0, frameAt = millis(), sidAt = 0;   // sidAt — когда на экране появился нынешний вид
  float barsF = 0, volF = cfg.volume, volK = 0, selF = 0, scrollF = 0, chPosF = cfg.channel;
  char editOld[24] = "";              // правка: прежнее значение — «перекатывается» (с 3.14)
  uint32_t editAt = 0;
  int8_t editDir = 1;
  float selC = 0, scrollC = 0;   // то же для меню цветного экрана (строки по 34 точки, видно пять)
  uint32_t oledNoticeUntil = 0;  // цветной экран выбран, а OLED тоже стоит: на нём подсказка, как его вернуть
  bool lcdShown = false;
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
  uint32_t limitSaidAt = 0, lockSaidAt = 0;
  uint8_t lockWas = cfg.rxLock, boostWas = cfg.rxBoost;
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
        uiHaveOled = true;
        dispBright();
        oledPrevOk = false;
        rxScreenFound = true;
        dispOn = true;
        if (uiHaveColor) {   // выбран цветной экран, а на модуле с ручкой стоит и OLED: сказать, как вернуть его
          oled.clearBuffer();
          oled.setFont(u8g2_font_6x13_t_cyrillic);
          drawCentered(12, tr("Кольоровий екран"));
          drawCentered(30, tr("OLED — зніміть"));
          drawCentered(44, tr("перемичку 21-GND"));
          oled.sendBuffer();
          oledNoticeUntil = now + 10000;
        }
      }
      rxScreenProbed = true;
    }
    // --- ручка: 4 четверть-шага на щелчок
    int32_t e = encCount;
    int turn = (e - encSeen) / 4;
    encSeen += turn * 4;
    // --- кнопка ручки: нажатия и отпускания считает таймер (knobPoll), здесь — что изменилось с прошлого кадра
    uiKeys = !digitalRead(PIN_ENC_SW);   // как есть сейчас — для проверки сборки (u)
    uint32_t kp = knobPresses, kr = knobReleases;
    uint8_t pressed = kp != kpSeen, released = kr != krSeen;
    kpSeen = kp;
    krSeen = kr;
    keyStable = knobDown;
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
    // Ручка работает, только пока есть сигнал своего передатчика и приёмник не выключен с него (с 3.14; владелец 15.10:
    // «приемники при отсутствии сигнала или выключенные с передатчика не должны реагировать на енкодер никак, а тем
    // более при нажатии включать режим тишины»). До 3.14 без сигнала ручка открывала меню (пока ищем передатчик) и
    // включала «тишу» (на экране «немає сигналу» — она оставалась и после возвращения сигнала), а во сне нажатие будило
    // приёмник искать передатчик и показывало «вимкнено з передавача». Сигнал — пакеты не старше 0,5 с: столько же
    // считает и экран (нет — «НЕМАЄ СИГНАЛУ»).
    bool live = !cfg.off && pw == PW_RUN && rLastRxMs && msSince(rLastRxMs, now) < 500;
    if (!live) {
      clickKnob = longKnob = false;
      turn = 0;
      if (screen != S_MAIN && screen != S_EAR) screen = S_MAIN;   // меню без сигнала — закрыть: на экране «немає сигналу»
    }
    if (!screenOn && pw != PW_STANDBY && live && (turn || (pressed & 1))) {   // экран погашен из меню: любое движение ручки его
      screenOn = true;                                                       // включает, и только включает — громкость и «тиша» не трогаются
      turn = 0;
      longDone = true;
      clickKnob = longKnob = false;
    }
    // Выключен с передатчика: ручка не работает вовсе, включить приёмник может только передатчик (владелец 10.10:
    // «функция приемника самостоятельно включить должна быть заблокирована… удаленное управление с передатчика
    // является приоритетным»). До 2.64 двойное нажатие включало приёмник самому. Служебный выход, если передатчика
    // больше нет, — кабель и команда K0 (забыть набор): приёмник попросит доступ заново.
    // И выход с самой ручки (владелец 11.10: «делай»): её держат 10 с, и за всё это время приёмник ни разу не услышал
    // свой передатчик — набор забывается, приёмник просит доступ заново. Звук это не включает: доступ даёт оператор
    // передатчика. Пока передатчик в эфире, удержание не делает ничего — включает и выключает только он. На экране об
    // этом выходе ничего нет и отсчёт не показывается (владелец 11.10: «информация о десяти секундах не отображать.
    // это знать пользователю не нужно») — он описан только в руководстве для того, кто обслуживает набор.
    if (cfg.off) {
      screen = S_MAIN;
      if ((keyStable & 1) && secHave && now - pressAt >= 10000 && (!rLastRxMs || msSince(rLastRxMs, now) >= (int32_t)(now - pressAt))) {
        secForget();
        cfg.off = 0;
        settingsSave();
        peekUntil = 0;
      }
    }
    uint8_t otaPh = otaRx.phase;
    bool otaShow = otaPh == OS_RECV || otaPh == OS_CHECK || otaPh == OS_WRITE || otaPh == OS_DONE || (otaPh == OS_FAIL && otaRx.err != OE_CANCEL);
    bool show = (screenOn && pw != PW_STANDBY) || now < peekUntil || now < rxIdentifyUntil || otaShow;   // обновление видно и при выключенном экране
    if (uiHaveOled && !uiHaveColor && show != dispOn) {
      oled.setPowerSave(!show);
      dispOn = show;
      oledPrevOk = false;   // после включения экрана — кадр целиком
    }
    if (uiHaveColor) {
      if (show != lcdShown) {   // цветной экран: погашен — подсветка выключена и контроллер спит
        lcdShown = show;
        rxLcdSleep(!show);
      }
      if (uiHaveOled && oledNoticeUntil) {   // подсказка на OLED — 10 с, потом он гаснет
        if ((int32_t)(now - oledNoticeUntil) >= 0) {
          oled.setPowerSave(1);
          oledNoticeUntil = 0;
        }
      }
    }
    if (rxUiAsk) {   // проверка с порта: рук у проверяющего нет, а частоту кадров экрана «Зв'язок» и меню измерить надо
      screen = rxUiAsk == 1 ? S_MENU : rxUiAsk == 2 ? S_LINK : S_MAIN;
      rxUiAsk = 0;
    }
    if (rxEarTest && screen != S_EAR && (clickKnob || longKnob)) {   // проверку наушников запустили с передатчика: нажатие её прекращает
      rxEarStart(false);
      clickKnob = longKnob = false;
    }
    // Блокировка ручки с передатчика (с 2.46): 1 — меню, 2 — громкость (поворот и «тиша» нажатием), 3 — всё.
    if ((cfg.rxLock & 1) && (screen == S_MENU || screen == S_EDIT || screen == S_ABOUT || screen == S_LINK)) screen = S_MAIN;
    if (screen == S_MAIN && cfg.rxLock) {
      bool hit = ((cfg.rxLock & 2) && (turn || clickKnob)) || ((cfg.rxLock & 1) && longKnob);
      if (cfg.rxLock & 2) {
        turn = 0;
        clickKnob = false;
      }
      if (cfg.rxLock & 1) longKnob = false;
      if (hit && now - lockSaidAt > 2500) {   // человек крутит, а ничего не меняется — сказать почему
        lockSaidAt = now;
        say(tr("Заблоковано"));
      }
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
          char was[24], now2[24];
          menuValue(item, was, sizeof(was));
          needRestart |= menuChange(item, turn);
          menuValue(item, now2, sizeof(now2));
          if (strcmp(was, now2)) {   // значение сменилось — «перекат»: больше — новое снизу, меньше — сверху
            strlcpy(editOld, was, sizeof(editOld));
            editAt = now ? now : 1;
            editDir = turn > 0 ? 1 : -1;
          }
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
          if (item == M_RESTART) {
            if (clickKnob && menuRestartArm) rxRestartAsk = 2;   // исполнит главный цикл (запишет настройки и перезапустит)
            menuRestartArm = false;
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
    if (saveAt && rxQuietToSave(saveAt, 8000)) {   // в паузе звука: запись слышна как щелчок
      saveAt = 0;
      settingsSave();
    }
    rxUiBusy = screen != S_MAIN;
    // спектр виден только на главном экране в работе, при включённом экране и без «тиші» — тогда и копить для него звук
    rxSpecOn = (uiHaveOled || uiHaveColor) && show && pw == PW_RUN && screen == S_MAIN && !rxMute && cfg.rxView == 0;
    // в ожидании не засыпать, пока на экране подсказка или (выключен с передатчика) держат ручку — служебный выход 10 с
    rxUiAwake = now < peekUntil || now < rxIdentifyUntil || (cfg.off && keyStable != 0);

    // --- рисование: общая часть (оповещения, какой экран, плавные величины) — для обоих экранов, дальше каждый рисует своё
    if ((uiHaveOled || uiHaveColor) && show) {
      uint32_t d0 = micros();
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
      if (cfg.rxBoost != boostWas) {   // усиление сменили (с передатчика — сказать; своей ручкой — и так видно)
        if (screen != S_EDIT) {
          char val[24], msg[44];
          menuValue(M_BOOST, val, sizeof(val));
          snprintf(msg, sizeof(msg), "%s: %s", tr(MENU_NAME[M_BOOST]), val);
          say(msg);
        }
        boostWas = cfg.rxBoost;
      }
      {   // обработку «для слуха» сменили (с передатчика — сказать; своей ручкой — и так видно)
        static uint8_t fxWas[3] = { 255, 255, 255 };
        const uint8_t fxNow[3] = { cfg.rxNs, cfg.rxComp, cfg.rxGate };
        static const uint8_t FX_ITEM[3] = { M_NS, M_COMP, M_GATE };
        for (int k = 0; k < 3; k++) {
          if (fxWas[k] != 255 && fxWas[k] != fxNow[k] && screen != S_EDIT) {
            char val[24], msg[44];
            menuValue(FX_ITEM[k], val, sizeof(val));
            snprintf(msg, sizeof(msg), "%s: %s", tr(MENU_NAME[FX_ITEM[k]]), val);
            say(msg);
          }
          fxWas[k] = fxNow[k];
        }
      }
      if (cfg.rxLock != lockWas) {   // ручку заблокировали или освободили с передатчика
        say(cfg.rxLock ? tr("Заблоковано") : tr("Розблоковано"));
        lockWas = cfg.rxLock;
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
      else if (screen == S_MAIN && !signal) sid = SC_LOST;   // сигнала нет — так и сказать, и при «тиші» (ручка без сигнала не работает)
      else if (screen == S_MAIN && rxMute) sid = SC_MUTE;
      else if (screen == S_MAIN) sid = SC_MAIN;
      else if (screen == S_ABOUT) sid = SC_ABOUT;
      else if (screen == S_LINK) sid = SC_LINK;
      else sid = SC_MENU;
      if (sid != sidWas) {   // экран сменился — переход: прежний кадр растворяется, съезжает или раскрывается кругом
        sidAt = now;
        trKind = sidWas == SC_MAIN && sid == SC_MENU ? rxs::TR_UP : sidWas == SC_MENU && (sid == SC_MAIN || sid == SC_MUTE || sid == SC_LOST) ? rxs::TR_DOWN
                 : sidWas == SC_MENU && (sid == SC_ABOUT || sid == SC_LINK) ? rxs::TR_LEFT : (sidWas == SC_ABOUT || sidWas == SC_LINK) && sid == SC_MENU ? rxs::TR_RIGHT
                 : sid == SC_MUTE || sidWas == SC_MUTE || sid == SC_EAR || sidWas == SC_EAR ? rxs::TR_IRIS : sidWas == SC_SPLASH ? rxs::TR_WIPE : rxs::TR_DISSOLVE;
        trMs = trKind == rxs::TR_WIPE ? 320 : trKind == rxs::TR_IRIS ? 300 : trKind == rxs::TR_DISSOLVE ? 240 : 260;
        bool prevOk = uiHaveColor ? rxLcdStrips->valid : oledPrevOk;   // на экране уже есть прежний кадр
        trAt = prevOk && (uiHaveColor || trFrom) && sidWas != SC_NONE && sidWas != SC_SLEEP && sid != SC_SLEEP && sid != SC_SPLASH ? (now ? now : 1) : 0;
        if (trAt) {
          if (uiHaveColor) *scnOld = *scn;   // прежний экран — каким был в последнем кадре: переход нарисует его ещё раз
          else memcpy(trFrom, oledPrev, 1024);
        }
        if (sid == SC_MENU && sidWas != SC_ABOUT && sidWas != SC_LINK && sidWas != SC_EAR) {   // меню открыли: плашка сразу на выбранной строке
          selF = item;
          scrollF = 0;
          int fc = item < 2 ? 0 : item > M_COUNT - 3 ? M_COUNT - 5 : item - 2;
          selC = item;
          scrollC = fc * rxc::MENU_ROW_H;
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
      // ---- оповещение: плашка сверху (выезжает за четверть секунды, держится полторы, уезжает) — какое и насколько
      const char *toastNow = nullptr;
      float toastK = 0;
      if (toastTail != toastHead) {
        bool fits = sid == SC_MAIN || sid == SC_LOST || sid == SC_MENU || sid == SC_ABOUT || sid == SC_MUTE || sid == SC_SEARCH || sid == SC_LINK;
        if (!toastAt) toastAt = now ? now : 1;
        uint32_t te = now - toastAt;
        if (!fits || te > 1960) {
          toastTail = (toastTail + 1) & 3;
          toastAt = 0;
        } else {
          toastNow = toastQ[toastTail];
          toastK = te < 260 ? te / 260.0f : te < 1760 ? 1.0f : 1 - (te - 1760) / 200.0f;
        }
      }
      if (sid == SC_MAIN && cfg.rxView == 0) {   // спектр: разбор — на каждом кадре
        uint32_t s0 = micros();
        specUpdate();
        specAnimate(now, (float)(now - specAt > 100 ? 100 : now - specAt));
        specAt = now;
        specUsSum = specUsSum + (micros() - s0);
      }
      if (uiHaveColor) {   // ---- цветной экран (с 3.03): сцена по тем же данным, кадр — полосами (rxstrip.h)
        rxc::Scene &c = *scn;
        c.sid = sid;
        c.t = now;
        c.toastK = toastNow ? toastK : 0;
        if (toastNow) strlcpy(c.toast, toastNow, sizeof(c.toast));
        switch (sid) {
          case SC_UPDATE: {
            uint8_t e = otaRx.err;
            if (otaPh == OS_RECV || otaPh == OS_WRITE) snprintf(c.foot, sizeof(c.foot), "%d %%", otaRx.percent);
            else if (otaPh == OS_FAIL)
              strlcpy(c.foot, e == OE_NOSLOT ? tr("потрібен USB (раз)") : e == OE_LOST ? tr("передавач зник") : e == OE_HASH ? tr("помилка в даних")
                              : e == OE_NOMEM ? tr("забракло пам'яті") : tr("помилка запису"), sizeof(c.foot));
            else snprintf(c.foot, sizeof(c.foot), tr("прошивка %s"), otaRx.version);
            strlcpy(c.line, otaPh == OS_RECV ? tr("приймаю з передавача") : otaPh == OS_CHECK ? tr("перевіряю") : otaPh == OS_WRITE ? tr("записую, не вимикайте")
                            : otaPh == OS_DONE ? tr("готово, перезапуск") : tr("не вдалося"), sizeof(c.line));
            c.percent = otaPh == OS_FAIL ? -1 : otaPh == OS_CHECK || otaPh == OS_DONE ? 100 : otaRx.percent;
            break;
          }
          case SC_IDENT: strlcpy(c.name, rxName(), sizeof(c.name)); break;
          case SC_SLEEP:
            if (why == WHY_NOSIGNAL) c.sid = rxc::SC_NONE;   // разбужен ручкой, ждали сигнала: картинки нет (как у OLED)
            else c.t = now - (peekUntil - 5000);
            break;
          case SC_SPLASH: c.t = pt; break;
          case SC_UNPAIRED:
            c.code = rxPairCode;
            c.heard = rLastAnyMs && msSince(rLastAnyMs, now) < 1500;
            break;
          case SC_GOING:
            c.t = pt;
            c.total = RX_GOING_MS;
            c.off = why == WHY_OFF;
            break;
          case SC_STARTING:
            c.t = pt;
            c.total = RX_START_MS;
            strlcpy(c.name, rxName(), sizeof(c.name));
            snprintf(c.info, sizeof(c.info), tr("набір %u  версія %s"), cfg.kit, FW_VERSION);
            break;
          case SC_SEARCH:
          case SC_LOST: {
            uint32_t from = why == WHY_KEY ? 0 : RX_SPLASH_MS;
            c.t = sid == SC_SEARCH ? pt : now - sidAt;
            strlcpy(c.siTitle, sid == SC_SEARCH ? tr("ПОШУК ПЕРЕДАВАЧА") : tr("НЕМАЄ СИГНАЛУ"), sizeof(c.siTitle));
            strlcpy(c.siLabel, si.label, sizeof(c.siLabel));
            c.si.ch = si.ch;
            c.si.prevCh = si.prevCh;
            c.si.roll = si.roll;
            c.si.pos = si.pos;
            c.si.lost = sid == SC_LOST;
            c.si.left = sid == SC_SEARCH ? 1 - (float)(pt > from ? pt - from : 0) / (RX_WAIT_MS - from) : 1 - (float)msSince(rLastRxMs, now) / RX_WAIT_MS;
            break;
          }
          case SC_MUTE: c.t = now - muteAt; break;
          case SC_MAIN:
            strlcpy(c.name, rxName(), sizeof(c.name));
            c.bars = barsF;
            c.volPct = cfg.volume * 5;
            c.sound = sound;
            c.stereo = cfg.rxStereo && rxAirStereo;
            c.view = cfg.rxView;
            c.spec = barShown;   // цветному — плавная высота (у OLED — по точкам, barDraw)
            c.peak = barPeak;
            c.nb = specBands;
            c.vuL = vuLF;
            c.vuR = vuRF;
            c.vuLP = vuLP;
            c.vuRP = vuRP;
            c.vol = volF;
            c.volK = volK;
            c.limit = 20;
            c.ns = cfg.rxNs >= 1 && cfg.rxNs <= 3;
            c.airMute = rxFlags & FLAG_MUTE;
            c.rssi = rRssi;
            c.signal = signal;
            break;
          case SC_MENU: {
            for (int it = 0; it < M_COUNT && it < rxc::MENU_MAX; it++) {
              c.rows[it].name = tr(MENU_NAME[it]);
              menuValue(it, c.rows[it].value, sizeof(c.rows[it].value));
              c.rows[it].icon = MENU_ICON[it];
            }
            c.nrows = M_COUNT < rxc::MENU_MAX ? M_COUNT : rxc::MENU_MAX;
            int fc = item < 2 ? 0 : item > M_COUNT - 3 ? M_COUNT - 5 : item - 2;   // видно пять строк
            float a = 1 - expf(-dtMs / 55.0f);
            if (fabsf(item - selC) > 5) selC = item;
            selC += (item - selC) * a;
            scrollC += (fc * rxc::MENU_ROW_H - scrollC) * a;
            c.sel = selC;
            c.scroll = scrollC;
            c.edit = screen == S_EDIT;
            c.editK = c.edit && editAt ? fminf(1.0f, (now - editAt) / 200.0f) : 1;
            c.editDir = editDir;
            strlcpy(c.editOld, editOld, sizeof(c.editOld));
            break;
          }
          case SC_ABOUT:
            strlcpy(c.about.name, rxName(), sizeof(c.about.name));
            strlcpy(c.about.version, FW_VERSION, sizeof(c.about.version));
            c.about.channel = cfg.channel;
            c.about.kit = cfg.kit;
            snprintf(c.about.id, sizeof(c.about.id), "%02X%02X%02X", rxId[0], rxId[1], rxId[2]);
            c.about.reserveMs = rxFillSm * 1000.0f / SRATE;
            c.about.rssi = rRssi;
            c.about.signal = signal;
            break;
          case SC_LINK:
            c.li.signal = signal;
            c.li.rssi = rRssi;
            c.li.bars = barsF;
            c.li.reserveMs = rxFillSm * 1000.0f / SRATE;
            c.li.hr = lnkRssi;
            c.li.hl = lnkLoss;
            c.li.pos = lnkPos;
            c.li.n = LNK_N;
            c.li.lossPct = c.li.pos ? lnkLoss[(c.li.pos - 1) % LNK_N] / 10.0f : 0;
            c.li.frac = msSince(lnkAt, now) / 1000.0f;
            if (c.li.frac > 1) c.li.frac = 1;
            break;
          case SC_EAR:
            c.earSide = rxEarSide(now);
            c.stereo = cfg.rxStereo;
            break;
        }
        rxLcdStrips->sentPx = 0;
        lcdEmiFrame(now);
        if (trAt && now - trAt < trMs) rxLcdStrips->sceneFrame(rxLcdOut, *scn, scnOld, trKind, (float)(now - trAt) / trMs);
        else {
          trAt = 0;
          rxLcdStrips->sceneFrame(rxLcdOut, *scn, nullptr, -1, 1);
        }
        lcdSentSum = lcdSentSum + rxLcdStrips->sentPx;
        lcdCycR = lcdCycR + rxLcdStrips->cRender;
        lcdCycH = lcdCycH + rxLcdStrips->cHash;
        lcdCycS = lcdCycS + rxLcdStrips->cSend;
        lcdCycW = lcdCycW + rxLcdStrips->cWait;
        if (lcdShown) dispBright();   // первый кадр ушёл — подсветка (и смена яркости с порта)
        uint32_t fu = micros() - d0;
        oledFrames = oledFrames + 1;
        oledDrawUsSum = oledDrawUsSum + fu;
        if (fu > oledDrawUsMax) oledDrawUsMax = fu;
      } else {   // ---- OLED
        u8g2_t *g = oled.getU8g2();
        oled.clearBuffer();
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
          case SC_MAIN: {   // вид — по настройке «Вигляд»: спектр, стрелочные индикаторы или крупная громкость
            // Строка состояния (с 3.12; владелец 15.10: «можно такой же принцип отрисовки применить и для oled»):
            // рисуется, только когда в ней что-то сменилось (rxs::statusKey), иначе — из запаса; каждый кадр — только
            // черта под ней и то, что ниже. Y17 — каждый кадр заново (как до 3.12), Y18 — из запаса.
            int vp = cfg.rxView == 2 ? -1 : cfg.volume * 5;
            if (oledStatic && oledCache) {
              uint32_t key = rxs::statusKey(g, now, barsF, rxName(), vp);
              if (key != oledStaticKey) {
                rxs::statusBar(g, now, barsF, rxName(), vp, sound, false);
                memcpy(oledStatic, oled.getBufferPtr(), 1024);
                oledStaticKey = key;
              } else memcpy(oled.getBufferPtr(), oledStatic, 1024);
              rxs::statusLine(g, now, sound);
            } else rxs::statusBar(g, now, barsF, rxName(), vp, sound);
            if (cfg.rxView == 1) rxs::needles(g, now, vuLF, vuRF, vuLP, vuRP, cfg.rxStereo && rxAirStereo);
            else if (cfg.rxView == 2) rxs::bigVolume(g, now, volF, 20, vuLF > vuRF ? vuLF : vuRF);
            else rxs::spectrum(g, barDraw, barPeak, specBands, now);   // разбор и движение — выше, общие с цветным экраном
            if (cfg.rxView != 2) rxs::volumeCard(g, now, volF, volK);   // громкость — карточкой поверх (в виде «гучність» она и так крупно)
            break;
          }
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
            rxs::menuList(g, now, rows, M_COUNT, selF, scrollF, screen == S_EDIT, editOld,
                          screen == S_EDIT && editAt ? fminf(1.0f, (now - editAt) / 200.0f) : 1, editDir);
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
        if (toastNow) rxs::toast(g, toastNow, toastK);   // оповещение поверх
        // ---- переход от прежнего экрана
        if (trAt) {
          float k = (float)(now - trAt) / trMs;
          if (k >= 1) trAt = 0;
          else rxs::blend(g, trFrom, k, trKind);
        }
        uint32_t f0 = micros();
        oledDrawUsSum = oledDrawUsSum + (f0 - d0);
        if (f0 - d0 > oledDrawUsMax) oledDrawUsMax = f0 - d0;
        Wire.setClock(oledFast ? 800000 : 400000);
        // Шина I2C берёт за раз 32 байта, и каждую следующую порцию кладёт эта задача: драйвер будит её, когда ушла
        // предыдущая. До 2.96 задача экрана была на одном ядре с задачей звука (приоритет 20), и пока та считала блок,
        // порция ждала: с шумозаглушением в стерео (2.89) передача кадра выросла с 4–6 до ~20 мс, экран — ~20 кадров в
        // секунду вместо 60 (владелец 14.10: «при включенном шумодаве очень тормозит меню и анализатор на главной
        // странице»). С 2.93 на время передачи задача экрана выше звука (21): почти всё это время она ждёт шину, а на
        // порцию забирает микросекунды. С 2.96 экран на первом ядре; там выше него только служебные задачи и радио (23 —
        // его порция по-прежнему ждёт), а подъём опережает светодиод и прочие ниже 21.
        if (oledFlushHigh) vTaskPrioritySet(NULL, 21);
        oledFlush();
        if (oledFlushHigh) vTaskPrioritySet(NULL, uiPrio);
        uint32_t fu = micros() - f0;
        oledFrames = oledFrames + 1;
        oledFrameUsSum = oledFrameUsSum + fu;
        if (fu > oledFrameUsMax) oledFrameUsMax = fu;
      }
      // 60 кадров в секунду: ровно по часам, а не «сколько успеем» (успевает и 150, но это лишняя работа процессору)
      // Опоздали (кадр считался дольше 14 мс) — счёт с этого места, но отдых между кадрами не меньше 2 мс всегда: иначе на
      // первом ядре не успевает задача простоя, и сторож задач перезапускает плату (с 3.06; 884A94 15.10 с цветным
      // экраном перезапускался так во время обновления по радио). На время обновления цветному экрану хватит 10 кадров
      // в секунду — меньше работы рядом с записью во флеш.
      static TickType_t tick;
      TickType_t nowT = xTaskGetTickCount();
      if ((int32_t)(nowT - tick) > 14) tick = nowT - 14;
      vTaskDelayUntil(&tick, pdMS_TO_TICKS(uiHaveColor && otaShow ? 100 : 16));
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}
