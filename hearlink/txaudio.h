// Передатчик: вход звука → пакеты.
//
// Темп задаёт сам вход: встроенный АЦП снимает 64 000 отсчётов в секунду, пары складываются — получается
// 32 000. Звук идёт кусками по 64 отсчёта (2 мс); пакет уходит, когда набран кадр выбранного качества
// (от одного куска до шести). Пробный тон подменяет только содержимое, темп остаётся от АЦП.
#pragma once
#include "radio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_task_wdt.h"
#include "driver/i2s_std.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "soc/gpio_sig_map.h"
#include "esp_rom_gpio.h"
#include "assets.h"
#include "txmp3.h"
#include "nsfx.h"
#include <new>
#include "subc.h"

static adc_continuous_handle_t txAdc;
static i2s_chan_handle_t txI2s;
static volatile int16_t txPeak;            // наибольший отсчёт за последнее время (для индикатора)
static volatile int16_t txPeakUi;          // то же для экрана (обнуляет экран)
static volatile uint32_t txClipMs;         // когда вход последний раз упирался в край шкалы
static portMUX_TYPE txMux = portMUX_INITIALIZER_UNLOCKED;
#define TX_SPEC_N 1024   // было 2048: разбор вдвое короче, шаг по частоте 31 Гц — для 20 рисок с избытком
static int16_t txSpecBuf[TX_SPEC_N];       // последние отсчёты входа — для спектра на экране
static volatile uint32_t txSpecPos;
static volatile bool txSpecOn;             // спектр сейчас на экране (вкладка «Звук») — иначе звук для него не копим
static volatile bool txPause;              // идёт поиск свободного канала: пакеты не слать
static volatile bool txPaused;             // задача передачи подтвердила остановку
// Пауза «налегке»: звук для эфира не готовить вовсе (вход читать, чтобы АЦП не захлебнулся, но не смешивать и не
// сжимать). Нужна, пока передатчик раздаёт прошивку приёмникам (ota.h): подготовка звука занимает половину первого
// ядра, раздача брала остальное, и ядро было занято без единого просвета — через 5 с плату перезапускал сторож
// (07.10, на глазах у владельца: «во время обновления приемников передатчик перезагрузился»).
static volatile bool txPauseLight;
static void (*txOnFrame)(const int16_t *pcm);   // кому ещё отдать кадр (запись на карту)
static void (*txAfterSend)();                  // что сделать после пакета звука (команды приёмникам)
static volatile bool txAuxWant;                // служебное ждёт эфира (команды приёмникам, ответ на подключение, ключ)
static float txSqSum;                      // сумма квадратов отсчётов с прошлого чтения экраном — для плавного индикатора
static uint32_t txSqCnt;
static volatile uint32_t txClips;          // отсчётов АЦП на краю шкалы за секунду
static volatile uint32_t txFrames;         // кадров за секунду
static volatile uint32_t txRawSamples;     // отсчётов входа за секунду — проверка настоящей частоты
static volatile uint32_t txRawMin = 4095, txRawMax;  // края сырых отсчётов АЦП за секунду
static volatile uint32_t txAdcRestarts;    // сколько раз пришлось перезапускать встроенный АЦП
// Сколько задача передачи тратит на кадр (от конца чтения входа до начала следующего чтения): на «найвищій» кадр идёт
// каждые 2000 мкс — дольше нельзя. Печатает Q.
static volatile uint32_t txProcSumUs, txProcN, txProcMaxUs, txProcOver;
static bool txIdleWdtOff;             // сторож холостой задачи ядра радио снят (setup)
static volatile int txWdtErr = -1;    // чем кончилась постановка задачи передачи под сторожа задач (0 — стоит; печатает ?)
static volatile uint32_t txYields;   // сколько раз задаче пришлось самой отдать ядро из-за перегрузки (см. txTask)
// То же по этапам: 0 источник звука, 1 уровни и «тишина», 2 сжатие, 3 сборка пакета, 4 шифр и подпись, 5 отдача радио,
// 6 остальное (команды приёмникам, конец круга).
static volatile uint32_t txSec[7];
#define TXP(i) { uint32_t n_ = (uint32_t)esp_timer_get_time(); txSec[i] = txSec[i] + (n_ - tsec); tsec = n_; }
static volatile uint16_t txRawLast;        // последний сырой отсчёт АЦП (для проверки выводов)

static bool txAdcBegin() {
  adc_unit_t unit;
  adc_channel_t ch;
  int pinAdc = cfg.board == BOARD_4848 ? PIN_ADC_4848 : PIN_ADC_DEVKIT;
  if (adc_continuous_io_to_channel(pinAdc, &unit, &ch) != ESP_OK || unit != ADC_UNIT_1) return false;
  adc_continuous_handle_cfg_t hc = {};
  hc.max_store_buf_size = FRAME * 2 * SOC_ADC_DIGI_RESULT_BYTES * 4;
  hc.conv_frame_size = FRAME * 2 * SOC_ADC_DIGI_RESULT_BYTES;
  if (adc_continuous_new_handle(&hc, &txAdc) != ESP_OK) return false;
  adc_digi_pattern_config_t pat = {};
  pat.atten = ADC_ATTEN_DB_12;
  pat.channel = ch;
  pat.unit = ADC_UNIT_1;
  pat.bit_width = 12;
  adc_continuous_config_t cc = {};
  cc.pattern_num = 1;
  cc.adc_pattern = &pat;
  cc.sample_freq_hz = SRATE * 2;
  cc.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  cc.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
  if (adc_continuous_config(txAdc, &cc) != ESP_OK) return false;
  return adc_continuous_start(txAdc) == ESP_OK;
}

// Встроенный АЦП иногда замолкает после смены канала Wi-Fi (на стенде — после поиска свободного канала:
// отсчёты перестали приходить совсем, и передатчик слал 20 кадров в секунду вместо 500). Радио и АЦП
// делят один узел микросхемы. Лечится перезапуском АЦП — здесь он пересоздаётся целиком.
static void txAdcRestart() {
  if (txAdc) {
    adc_continuous_stop(txAdc);
    adc_continuous_deinit(txAdc);
    txAdc = NULL;
  }
  txAdcBegin();
  txAdcRestarts = txAdcRestarts + 1;
}

// Проба (порт: a2): на ОДИН запуск вход I2S работает без главного такта на выводе 43 (он же TXD порта) — АЦП при этом
// молчит, но весь путь «вход I2S → эфир» идёт как обычно, а передатчик печатает в порт. С настоящим тактом порт нем.
static RTC_NOINIT_ATTR uint32_t txI2sNoMclk;
#define I2S_NO_MCLK 0x6E6F4D43u
static bool txI2sProbe;   // этот запуск — такая проба
// Стык кадров (3.21). Знак первого отсчёта кадра лежит в последнем слове ПРОШЛОГО кадра, а между кадрами I2S иногда
// теряет кусок (на записи без пульта — 10 стыков из 749, внутри кадров — ни одного расхождения из 95 250): тогда знак
// чужой и отсчёт улетает на всю шкалу — щелчок. Это и был «постоянный треск в покое» 3.19. Теперь из двух вариантов
// знака первого отсчёта берётся тот, что ближе к следующему отсчёту того же канала. Потери самого I2S считает txI2sOvf.
static volatile uint32_t txAdcEdgeFix, txI2sOvfN;
static bool IRAM_ATTR txI2sOvf(i2s_chan_handle_t, i2s_event_data_t *, void *) {
  txI2sOvfN = txI2sOvfN + 1;
  return false;
}
// Такты PCM1808 — самыми слабыми выходами (с 3.26). Владелец 16.10: «сигнал однозначно стал хуже… не поменяли мы
// что-то в частотах работы оборудования, что может мешать работе wifi». Частоты не менялись (MCLK 8,192 МГц, BCK 2,048,
// LRCK 32 кГц), но пульт сегодня в эфире почти всё время, а с ним — такт на выводе 43: по журналу карты шум радио у
// передатчика при пульте на 2–3 дБ выше (−87…−88 дБм против −90…−91). Гармоники такта идут каждые 8,192 МГц — в любом
// канале 2,4 ГГц их две-три. Слабый выход (~5 мА) делает фронты мягче, а верхние гармоники — тише; входам PCM1808
// (триггеры Шмитта) короткими проводами этого хватает — шине цветного экрана 20 МГц хватило (3.11).
static void txI2sWeakPins() {
  if (cfg.board != BOARD_4848) return;
  gpio_set_drive_capability((gpio_num_t)PIN_I2S_BCK_4848, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t)PIN_I2S_WS_4848, GPIO_DRIVE_CAP_0);
  if (!txI2sProbe) gpio_set_drive_capability((gpio_num_t)PIN_I2S_MCLK_4848, GPIO_DRIVE_CAP_0);
}
// Внешний АЦП PCM1808 по I2S. Написано по описанию микросхемы; на модуле не проверено.
static bool txI2sBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  // 8 кадров по 2 мс (с 3.21; было 4): задача передачи иногда задерживается дольше 6 мс, и I2S терял кадр — разрыв
  // звука с пульта, щелчок (проверочный звук делается на месте и от этого не страдает). Запас не добавляет задержки:
  // кадр забирается, как только готов; копится только после задержки задачи.
  cc.dma_desc_num = 8;
  cc.dma_frame_num = FRAME;
  if (i2s_new_channel(&cc, NULL, &txI2s) != ESP_OK) return false;
  i2s_std_config_t sc = {};
  sc.clk_cfg.sample_rate_hz = SRATE;
  sc.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
  sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  bool m = cfg.board == BOARD_4848;
  // 4.x («Next Edition»): вход — мост на Pi Pico, такт PCM1808 даёт он сам; вывод 43 всегда отдан порту (по нему же идут
  // команды мосту, txbridge.h). То, что в 3.x было пробой a2 на один запуск, теперь — всегда. Данные моста выровнены
  // (сдвига на бит, как у PCM1808 напрямую, нет): узнавание сдвига не нужно — «як є».
  txI2sProbe = true;
  txI2sNoMclk = 0;
  sc.gpio_cfg.mclk = txI2sProbe ? I2S_GPIO_UNUSED : (gpio_num_t)(m ? PIN_I2S_MCLK_4848 : PIN_I2S_MCLK);
  sc.gpio_cfg.bclk = (gpio_num_t)(m ? PIN_I2S_BCK_4848 : PIN_I2S_BCK);
  sc.gpio_cfg.ws = (gpio_num_t)(m ? PIN_I2S_WS_4848 : PIN_I2S_WS);
  sc.gpio_cfg.dout = I2S_GPIO_UNUSED;
  sc.gpio_cfg.din = (gpio_num_t)(m ? PIN_I2S_DIN_4848 : PIN_I2S_DIN);
  if (i2s_channel_init_std_mode(txI2s, &sc) != ESP_OK) return false;
  txI2sWeakPins();
  i2s_event_callbacks_t cbs = {};
  cbs.on_recv_q_ovf = txI2sOvf;   // потери кадров I2S (задача не успела забрать) — для отчёта e14
  i2s_channel_register_event_callback(txI2s, &cbs, NULL);
  return i2s_channel_enable(txI2s) == ESP_OK;
}

// ---- вывод 43 модуля: главный такт PCM1808 или порт
// Свободных выводов у модуля не хватает, и главный такт внешнего АЦП идёт выводом 43 — это же TXD порта: пока такт
// на выводе, передатчик в порт не печатает (команды принимает). Из-за этого для работы с портом вход приходилось
// переводить на встроенный АЦП, а с ним радио передатчика встаёт (см. radio.h; владелец 09.10: «при включенном pcm
// не будет связи с машиной»). Но такт нужен PCM1808 только тогда, когда звук со входа действительно идёт в эфир.
// С 2.55 вывод делится по надобности: в эфире проверочный звук или файл с карты — вывод отдан порту (передатчик
// печатает как обычно, PCM1808 в это время молчит); в эфире вход — выводу возвращается такт. Сам I2S не
// перезапускается: его такт внутри идёт всегда, меняется только то, что подключено к выводу.
static volatile bool txMclkOnPin = true;   // что сейчас на выводе 43: такт (так его ставит запуск I2S) или порт
static void txMclkPin(bool mclk) {
  if (cfg.input != IN_I2S || txI2sProbe || cfg.board != BOARD_4848 || !txI2s || mclk == txMclkOnPin) return;
  if (mclk) {
    Serial.println("порт замовкає: в ефір іде вхід PCM1808 — вивід порту потрібен його такту (перевірочний звук або файл повернуть порт)");
    vTaskDelay(pdMS_TO_TICKS(30));   // строка успевает уйти
    gpio_reset_pin((gpio_num_t)PIN_I2S_MCLK_4848);
    gpio_set_direction((gpio_num_t)PIN_I2S_MCLK_4848, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(PIN_I2S_MCLK_4848, I2S0_MCLK_OUT_IDX, false, false);
    gpio_set_drive_capability((gpio_num_t)PIN_I2S_MCLK_4848, GPIO_DRIVE_CAP_0);   // такт — слабым выходом (3.26)
    txMclkOnPin = true;
  } else {
    uart_set_pin(UART_NUM_0, PIN_I2S_MCLK_4848, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    gpio_set_drive_capability((gpio_num_t)PIN_I2S_MCLK_4848, GPIO_DRIVE_CAP_DEFAULT);   // порту — обычный выход
    txMclkOnPin = false;
    Serial.println("порт знову працює: в ефірі не вхід PCM1808 (його такт з виводу порту знято)");
  }
}

// Прочитать кадр со встроенного АЦП: 128 отсчётов → 64, без постоянной составляющей, в шкале 16 бит.
static bool txReadAdc(int16_t *pcm) {
  static uint8_t raw[FRAME * 2 * SOC_ADC_DIGI_RESULT_BYTES];
  static float dc = 4095;  // сумма двух отсчётов в покое — около середины шкалы
  uint32_t have = 0;
  while (have < sizeof(raw)) {
    uint32_t got = 0;
    if (adc_continuous_read(txAdc, raw + have, sizeof(raw) - have, &got, 50) != ESP_OK) return false;
    have += got;
  }
  const adc_digi_output_data_t *d = (const adc_digi_output_data_t *)raw;
  uint32_t mn = txRawMin, mx = txRawMax, clips = 0, fmn = 4095, fmx = 0;
  for (int i = 0; i < FRAME; i++) {
    uint32_t a = d[2 * i].type2.data, b = d[2 * i + 1].type2.data;
    if (a < mn) mn = a;
    if (a > mx) mx = a;
    if (a < fmn) fmn = a;
    if (a > fmx) fmx = a;
    if (a <= 8 || a >= 4087) clips++;
    float v = (float)(a + b);
    dc += (v - dc) * 0.0005f;          // медленно следим за серединой (срез около 2,5 Гц)
    float x = (v - dc) * 8.0f;         // 13 бит суммы → шкала 16 бит
    pcm[i] = x > 32767 ? 32767 : x < -32768 ? -32768 : (int16_t)x;
  }
  txRawMin = mn;
  txRawMax = mx;
  txRawLast = d[0].type2.data;
  // перегрузка — это когда звук упирается в край шкалы; вход, который просто лежит на нуле (ничего
  // не подключено), перегрузкой не считается
  if (fmx - fmn > 1000) txClips = txClips + clips;
  txRawSamples = txRawSamples + FRAME * 2;
  return true;
}

// Счёт самого АЦП (с 3.18, для txLimPrint): упирается ли он в край шкалы, нет ли «перескоков» отсчёта через весь
// размах за один шаг (у настоящего звука их не бывает — так выглядит сдвиг данных на бит: старший разряд теряется и
// громкое «переворачивается»), что в младшем байте 32-битного слова (PCM1808 шлёт 24 бита — там должны быть нули).
static volatile uint32_t txAdcFrames, txAdcPeakL, txAdcPeakR, txAdcRailL, txAdcRailR, txAdcJumpL, txAdcJumpR, txAdcLowOr;
static volatile uint32_t txAdcRailUi;    // отсчётов АЦП на краю шкалы — для листа «Піки входу» (с 3.24; обнуляет ui_tx.h)
// Сдвиг данных PCM1808 на бит (с 3.19). Счёт 3.18 на приборе владельца (16.10, вход с пульта в перегрузе): «перескоков
// через весь розмах» Л 774, П 476, на краю шкалы — по 2 отсчёта, в младшем байте слова взводится только бит 0. Так
// бывает, когда данные приходят на такт раньше, чем их ждёт I2S (АЦП в «левом выравнивании» при Philips у ESP32 — или
// ESP32 берёт бит на такт позже): старший разряд отсчёта уходит в бит 0 ПРОШЛОГО слова, в слове остаются D22…D0.
// Отсчёт выходит вдвое громче настоящего, а всё, что выше половины шкалы АЦП, «переворачивается» через весь размах —
// треск на громком (владелец: «на тестовом звуке все идеально, даже при полном перегрузе на индикаторе уровня, а через
// ацп этот же уровень это просто треск на выходе»). Отличить по самим данным: у правильного слова бит 8 — младший
// разряд D0 (шум, взводится в половине слов), а бит 0 — всегда 0; у сдвинутого — бит 8 всегда 0 (бит после D0),
// а бит 0 — знак следующего отсчёта. Сдвинутые слова собираются заново: знак — из бита 0 предыдущего слова потока
// (Л₀ П₀ Л₁ П₁…: знак Л₁ — в П₀, знак П₀ — в Л₀), D22…D0 — из своего. Решение меняется после 4 кадров подряд с
// однозначным признаком; тишина без такта (одни нули) решения не меняет. Порт: e18 — показать, e18=0 — само,
// 1 — читать как есть, 2 — всегда собирать заново (проба на слух; не сохраняется).
// Проверено на записях настоящих слов с прибора владельца (3.20, `build/adcraw/`): без пульта (шум АЦП) знак отсчёта
// совпадает с битом 0 ПРЕДЫДУЩЕГО слова буфера в 100 % слов (с любым другим соседом — 55–59 %); с пультом сборка убирает
// все «перевороты» (188 и 201 за 1,5 с → 0). С 3.21 снова «само» по умолчанию.
static volatile bool txAdcShift = true;    // сейчас данные считаются сдвинутыми (у владельца так; само поправится за 8 мс)
static volatile uint8_t txAdcFmtForce = 1; // 0 — само, 1 — как есть (с 4.00 по умолчанию: данные моста выровнены), 2 — сдвиг
static volatile uint32_t txAdcWords, txAdcBit0, txAdcBit8, txAdcFlips;
// Запись «сырых» 32-битных слов АЦП (с 3.20) — для разбора на компьютере: e19=<мс> — записать, когда вход оживёт;
// e21=<мс> — то же, но вход включить самому (проверочный звук снимается на время записи и возвращается сам);
// e20 — выгрузить в порт (строки «@@ADC …», tools/adcdump.py). Память — внешняя (до 1,5 с = 384 КБ).
static int32_t *txRawCap;
static volatile uint32_t txRawCapN, txRawCapWant;
static volatile uint8_t txRawCapState;           // 0 нет, 1 ждёт живого входа, 2 пишет, 3 готово
static volatile uint8_t txRawCapTone = 0xFF;     // какой проверочный звук вернуть после записи (e21); 0xFF — не трогали
static volatile uint32_t txRawCapT0, txRawCapLive; // когда дана команда; когда вход ожил (0 — ещё нет)
static volatile uint16_t txRawCapDelay;          // начинать через столько мс живого входа (АЦП после такта успокаивается ~1 с)
static volatile uint8_t txRawCapKind;            // 0 — слова АЦП как пришли; 1 — то, что уходит в эфир (середина | разность << 16)
// Начать ли запись сейчас: вход живой уже txRawCapDelay мс.
static bool txRawCapGo(bool live) {
  if (txRawCapState != 1) return false;
  if (!live) {
    txRawCapLive = 0;
    return false;
  }
  uint32_t now = millis();
  if (!txRawCapLive) txRawCapLive = now ? now : 1;
  if (now - txRawCapLive < txRawCapDelay) return false;
  txRawCapState = 2;
  return true;
}
// Те же середина и разность с полной точностью входа (24 бита моста; в шкале 16 бит, с дробной частью): подсиление и
// эквалайзер входа берут их, а не 16-битные pcm/sd. До 22.10 вход сразу становился 16-битным (старшие 16 бит слова,
// округлением вниз, и ещё деление пополам с отбрасыванием), а подсиление входа (+24 дБ) поднимало и эту ступеньку:
// пульт −87 дБ шёл лишь несколькими ступенями шкалы — шипение и треск (владелец: «при низком уровне звука на цифре
// слышно фоновое шипение и немного треска»).
static float txFineM[FRAME], txFineS[FRAME];
static inline int16_t txRound16(float v) {   // к ближайшему (половина — от нуля), с ограничением
  return v >= 32767.0f ? 32767 : v <= -32768.0f ? -32768 : (int16_t)(int32_t)(v + (v < 0 ? -0.5f : 0.5f));
}
// Внешний АЦП даёт два канала: в pcm — середина (Л+П)/2, в sd — разность (Л−П)/2 (округлённые до 16 бит; точные — в
// txFineM / txFineS).
static NSX_HOT bool txReadI2s(int16_t *pcm, int16_t *sd) {
  static int32_t raw[FRAME * 2];
  static int lp, rp;
  static uint32_t carry;   // бит 0 последнего слова прошлого кадра — знак первого отсчёта этого
  static int8_t vote;      // кадров подряд «сдвинуто» (+) или «как есть» (−)
  size_t got = 0;
  if (i2s_channel_read(txI2s, raw, sizeof(raw), &got, 50) != ESP_OK || got != sizeof(raw)) return false;
  txBridgeFeed(raw, FRAME);   // обратный канал моста — биты 7…4 слов (txbridge.h)
  uint32_t b0 = 0, b8 = 0, nz = 0, lo = 0;
  for (int i = 0; i < FRAME * 2; i++) {
    uint32_t w = (uint32_t)raw[i];
    if (w) nz++;
    b0 += w & 1;
    b8 += (w >> 8) & 1;
    lo |= w & 0xFF;
  }
  if (b0 && !b8) vote = vote < 0 ? 1 : vote < 4 ? vote + 1 : 4;
  else if (b8 && !b0) vote = vote > 0 ? -1 : vote > -4 ? vote - 1 : -4;
  bool sh = txAdcFmtForce ? txAdcFmtForce == 2 : txAdcShift;
  if (!txAdcFmtForce && ((vote == 4 && !sh) || (vote == -4 && sh))) {
    sh = !sh;
    txAdcShift = sh;
    txAdcFlips = txAdcFlips + 1;
  }
  txAdcWords = txAdcWords + nz;
  txAdcBit0 = txAdcBit0 + b0;
  txAdcBit8 = txAdcBit8 + b8;
  if (txRawCapKind == 0) txRawCapGo(nz > FRAME);   // вход ожил (АЦП после такта молчит ~0,3 с)
  if (txRawCapKind == 0 && txRawCapState == 2 && txRawCap) {
    uint32_t n = txRawCapN, m = FRAME * 2;
    if (n + m > txRawCapWant) m = txRawCapWant - n;
    memcpy(txRawCap + n, raw, m * sizeof(int32_t));
    n += m;
    txRawCapN = n;
    if (n >= txRawCapWant) txRawCapState = 3;
  }
  if (sh) {   // собрать заново: знак — бит 0 предыдущего слова потока, D22…D0 — биты 31…9 своего
    uint32_t c = carry;
    for (int i = 0; i < FRAME * 2; i++) {
      uint32_t w = (uint32_t)raw[i];
      raw[i] = (int32_t)((c << 31) | ((w >> 1) & 0x7FFFFF00u));
      c = w & 1;
    }
    carry = c;
    int64_t a = raw[0], o = (int32_t)((uint32_t)raw[0] ^ 0x80000000u), nx = raw[2];
    if (llabs(o - nx) < llabs(a - nx)) {
      raw[0] = (int32_t)o;
      txAdcEdgeFix = txAdcEdgeFix + 1;
    }
  } else carry = (uint32_t)raw[FRAME * 2 - 1] & 1;
  uint32_t pl = txAdcPeakL, pr = txAdcPeakR, rl = 0, rr = 0, jl = 0, jr = 0;
  // Причина сдвига у владельца (16.10): вывод FMT модуля — на 3,3 В, это «левое выравнивание» (описание PCM1808,
  // табл. 3), а по нашей схеме FMT — на GND (I2S). В «левом выравнивании» левый канал идёт при ВЫСОКОМ LRCK, а I2S у
  // ESP32 считает левым низкий — каналы переставлены. Поэтому при сдвиге слова кадра берутся наоборот.
  const int iL = sh ? 1 : 0, iR = sh ? 0 : 1;
  for (int i = 0; i < FRAME; i++) {
    int32_t l24 = raw[2 * i + iL] >> 8, r24 = raw[2 * i + iR] >> 8;   // 24 бита; биты 7…4 — обратный канал моста
    float m = (float)(l24 + r24) * (1.0f / 512), d = (float)(l24 - r24) * (1.0f / 512);
    txFineM[i] = m;
    txFineS[i] = d;
    pcm[i] = txRound16(m);
    sd[i] = txRound16(d);
    int l = l24 >> 8, r = r24 >> 8;
    uint32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
    if (al > pl) pl = al;
    if (ar > pr) pr = ar;
    if (al >= 32766) rl++;
    if (ar >= 32766) rr++;
    if (abs(l - lp) > 49152) jl++;
    if (abs(r - rp) > 49152) jr++;
    lp = l;
    rp = r;
  }
  txAdcPeakL = pl;
  txAdcPeakR = pr;
  txAdcRailL = txAdcRailL + rl;
  txAdcRailR = txAdcRailR + rr;
  txAdcJumpL = txAdcJumpL + jl;
  txAdcJumpR = txAdcJumpR + jr;
  txAdcLowOr = txAdcLowOr | lo;
  txAdcFrames = txAdcFrames + 1;
  if (rl || rr) {
    txClips = txClips + rl + rr;   // для отметки «перевантаження» на экране, как у встроенного АЦП
    txAdcRailUi = txAdcRailUi + rl + rr;
  }
  txRawSamples = txRawSamples + FRAME;
  if (txBrMute) {   // идёт обновление моста: на выводе 40 ответы его загрузчика, а не звук (txbridge.h)
    memset(pcm, 0, FRAME * sizeof(int16_t));
    memset(sd, 0, FRAME * sizeof(int16_t));
    memset(txFineM, 0, sizeof(txFineM));
    memset(txFineS, 0, sizeof(txFineS));
  }
  return true;
}

// Пробный тон 1000 Гц на четверти полной шкалы (−12 дБ): ровно 32 отсчёта на период.
static void txTone(int16_t *pcm) {
  static int16_t tab[32];
  static uint32_t ph;
  if (!tab[8])
    for (int i = 0; i < 32; i++) tab[i] = (int16_t)lroundf(8192.0f * sinf(2 * PI * i / 32));
  for (int i = 0; i < FRAME; i++) pcm[i] = tab[ph++ & 31];
}

// Проверка каналов: один долгий гудок слева (−12 дБ), пауза, два коротких справа (−18 дБ), пауза — и по кругу
// за четыре секунды. Разный уровень — чтобы и по числам было видно, какой канал где.
static void txToneLR(int16_t *pcm, int16_t *sd) {
  static uint32_t t, ph;
  static float env;      // гудок начинается и кончается плавно, за 5 мс — без щелчка в наушниках
  for (int i = 0; i < FRAME; i++) {
    uint32_t ms = t / (SRATE / 1000);
    bool left = ms < 1000, right = (ms >= 2000 && ms < 2400) || (ms >= 2600 && ms < 3000);
    static bool wasLeft;
    if (left || right) wasLeft = left;
    env += ((left || right ? 1.0f : 0.0f) - env) * (1.0f / (SRATE * 0.0015f));
    int x = (int)lroundf(env * (wasLeft ? 8192.0f : 4096.0f) * sinf(2 * PI * (ph++ & 31) / 32));
    pcm[i] = x / 2;                    // звук в одном канале: середина — половина, разность — плюс или минус половина
    sd[i] = wasLeft ? x / 2 : -x / 2;
    if (++t >= SRATE * 4) t = 0;
  }
}

// Проверочные звуки из раздела данных: голос и музыка. Список зависит от режима; между записями — пауза.
// Проигрыватель одной записи: несжатую читает с флеша кусками по 2048 отсчётов (64 мс), MP3 берёт из запаса txmp3.h.
struct AssetPlay {
  int16_t buf[2048];
  uint32_t n = 0, at = 0, pos = 0;
  int item = -1;
  bool mp3 = false;
  bool open(const char *name) {
    item = assetFind(name);
    pos = n = at = 0;
    mp3 = item >= 0 && assetIsMp3(item);
    if (mp3) mp3Open(item);
    return item >= 0;
  }
  bool get2(int16_t &l, int16_t &r) {   // оба канала; false — запись кончилась (или её нет)
    if (item < 0) return false;
    if (mp3) {
      int k = mp3Get(l, r);
      if (k < 0) {
        item = -1;
        return false;
      }
      if (!k) l = r = 0;
      return true;
    }
    if (at >= n) {
      uint32_t left = (assetTab[item].size - pos) / 2, k = left > 2048 ? 2048 : left;
      if (!k || !assetRead(assetTab[item].off + pos, buf, k * 2)) {
        item = -1;
        return false;
      }
      pos += k * 2;
      n = k;
      at = 0;
    }
    l = r = buf[at++];
    return true;
  }
  bool get(int16_t &s) {   // в эфир пока идёт один канал: левый и правый складываются
    int16_t l = 0, r = 0;
    if (!get2(l, r)) return false;
    s = (int16_t)((l + r) / 2);
    return true;
  }
};

// Какую мелодию играть следующей: выбранную в настройках или по очереди.
static const char *txNextMusic() {
  int i = cfg.testTrack ? assetMusic(cfg.testTrack) : -1;
  return i >= 0 ? assetTab[i].name : assetNextMusic();
}
static inline float dbGain(int db) {
  return powf(10.0f, db / 20.0f);
}

// «Музыка с объявлением»: музыка идёт без остановки; раз в cfg.duckS секунд она плавно уходит в фон
// (на cfg.duckDb), поверх в полную силу звучит голос, после него музыка так же плавно возвращается.
#define DUCK_LEAD (SRATE * 8 / 10)      // за сколько до голоса музыка начинает уходить (и столько же уходит)
#define DUCK_HOLD (SRATE * 3 / 10)      // пауза после голоса перед возвратом музыки
#define DUCK_BACK (SRATE * 12 / 10)     // за сколько музыка возвращается
// Голос объявления — по языку надписей: при английском — запись «golos-en» (если она залита), иначе украинская.
static const char *txVoiceName() {
  return cfg.lang && assetFind("golos-en") >= 0 ? "golos-en" : "golos";
}

static void txTestDuck(int16_t *pcm, int16_t *sd, AssetPlay &mus, AssetPlay &voc, bool restart) {
  static uint32_t t, hold;
  static float pos, gain = 1;     // pos: 0 — музыка впереди, 1 — в фоне
  static bool voice, low;
  if (restart) {
    t = 0;
    hold = 0;
    pos = 0;
    gain = 1;
    voice = low = false;
    mus.open(txNextMusic());
    voc.item = -1;
  }
  uint32_t period = (uint32_t)cfg.duckS * SRATE;
  // Громкости считаются степенной функцией; до 2.56 — три раза на каждом кадре (500 раз в секунду), хотя меняются
  // они лишь при смене настроек и во время ухода/возврата музыки. Задаче передачи на кадр отведено 2 мс, уходило
  // 1,8–1,9 — с голосом она не успевала (кадров 470–489 в секунду вместо 500).
  static int8_t gmDb = 127, gvDb = 127, dkDb = 127;
  static float gm = 1, gv = 1, smWas = -1, duckK = 1;
  if (gmDb != cfg.musicDb) {
    gmDb = cfg.musicDb;
    gm = dbGain(gmDb);
  }
  if (gvDb != cfg.voiceDb) {
    gvDb = cfg.voiceDb;
    gv = dbGain(gvDb);
  }
  // положение уходит и возвращается равномерно, громкость по нему — в децибелах и со сглаженными краями
  float step = low ? (float)FRAME / DUCK_LEAD : -(float)FRAME / DUCK_BACK;
  pos = pos + step < 0 ? 0 : pos + step > 1 ? 1 : pos + step;
  float sm = pos * pos * (3 - 2 * pos);
  if (sm != smWas || dkDb != cfg.duckDb) {
    smWas = sm;
    dkDb = cfg.duckDb;
    duckK = powf(10.0f, dkDb * sm / 20.0f);
  }
  float g1 = gm * duckK, dg = (g1 - gain) / FRAME;
  for (int i = 0; i < FRAME; i++) {
    int16_t ml = 0, mr = 0, v = 0;
    if (!mus.get2(ml, mr)) {      // мелодия кончилась — следующая
      mus.open(txNextMusic());
      mus.get2(ml, mr);
    }
    t++;
    if (!voice && !hold && t + DUCK_LEAD >= period) low = true;
    if (t >= period) {
      t = 0;
      voice = voc.open(txVoiceName());
      low = voice;
    }
    if (voice && !voc.get(v)) {   // голос договорил
      voice = false;
      hold = DUCK_HOLD;
      v = 0;
    }
    if (hold && !--hold) low = false;
    gain += dg;
    int x = (int)((ml + mr) * 0.5f * gain + v * gv);   // голос — посередине
    pcm[i] = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
    sd[i] = (int16_t)((ml - mr) * 0.5f * gain);
  }
}

// ---- файл с карты памяти в эфире (файловый менеджер, «В ефір»). Отсчёты готовит задача разбора (txmp3.h) — уже на
// 32 кГц, оба канала; здесь они только забираются. Начало и остановка — плавные (20 мс), без щелчка.
static volatile bool txCardOn;       // в эфир идёт файл с карты (вместо входа и проверочного звука)
static volatile bool txCardStop;     // попросили остановить: звук плавно уходит, затем txCardOn снимается
static volatile bool txTestReset;    // проверочный звук начать заново (задачу разбора занимал файл)
static volatile uint32_t cpPlayed;   // сколько отсчётов файла ушло в эфир
// Переход по треку и пауза (с 3.28, плеер fmui.h): звук уходит за 20 мс; когда он в нуле — пауза перестаёт брать звук
// из запаса (разбор встаёт, место в треке держится), а переход перезапускает разбор с нужного байта (cpSeekByte) и
// ставит время; новый звук входит с нуля за те же 20 мс — без щелчка.
static void txCardSound(int16_t *pcm, int16_t *sd) {
  static float g = 0;
  bool seek = cpSeekReqByte != 0xFFFFFFFF, hold = seek || cpPause;
  for (int i = 0; i < FRAME; i++) {
    int16_t l = 0, r = 0;
    int k = hold && g <= 0 ? 0 : mp3Get(l, r);
    if (k < 0) {                     // файл кончился (или не читается)
      for (; i < FRAME; i++) pcm[i] = sd[i] = 0;
      g = 0;
      fm.playEnd = cpErr ? 3 : 1;
      txCardOn = false;
      return;
    }
    if (k > 0) cpPlayed = cpPlayed + 1;
    float d = (txCardStop || hold ? 0.0f : 1.0f) - g;
    g += d > 1.0f / 640 ? 1.0f / 640 : d < -1.0f / 640 ? -1.0f / 640 : d;
    pcm[i] = (int16_t)((l + r) * 0.5f * g);
    sd[i] = (int16_t)((l - r) * 0.5f * g);
  }
  if (seek && g <= 0) {   // звук ушёл — перейти
    cpPlayed = cpSeekReqPlayed;
    cpSeekByte = cpSeekReqByte;
    cpSeekReqByte = 0xFFFFFFFF;
    mpStarted = false;
    mpWantGen = mpWantGen + 1;   // задача разбора откроет файл заново с cpSeekByte
  }
  if (txCardStop && g <= 0) {
    fm.playEnd = 2;
    txCardOn = false;
  }
}

static volatile bool txSoundsOk;     // раздел со звуками найден
static void txTestSound(int16_t *pcm, int16_t *sd) {
  static AssetPlay a, b;
  static uint32_t gap;
  static int step, mode = -1, track = -1;
  if (txTestReset) {   // задачу разбора занимал файл с карты — список начать заново
    txTestReset = false;
    mode = -1;
  }
  bool fresh = mode != cfg.tone || track != cfg.testTrack;
  if (fresh) {   // режим или мелодию сменили — начать список заново
    mode = cfg.tone;
    track = cfg.testTrack;
    step = 0;
    gap = 0;
    a.item = b.item = -1;
    assetMusicAt = -1;   // мелодии — с первой
  }
  if (mode == TEST_DUCK) {
    txTestDuck(pcm, sd, a, b, fresh);
    return;
  }
  static bool isVoice;
  float gm = dbGain(cfg.musicDb), gv = dbGain(cfg.voiceDb);
  for (int i = 0; i < FRAME; i++) {
    pcm[i] = 0;
    if (gap) {
      gap--;
      continue;
    }
    if (a.item < 0) {   // следующая запись списка: голос, мелодии или голос и мелодии через раз
      isVoice = mode == TEST_VOICE || (mode == TEST_ALL && step++ % 2 == 0);
      if (!a.open(isVoice ? txVoiceName() : txNextMusic())) {
        gap = SRATE;
        continue;
      }
    }
    int16_t l = 0, r = 0;
    if (!a.get2(l, r)) {   // запись кончилась — пауза перед следующей
      gap = mode == TEST_VOICE ? (uint32_t)cfg.voiceGapS * SRATE : isVoice ? SRATE * 6 / 10 : SRATE * 12 / 10;
      continue;
    }
    float g = isVoice ? gv : gm;
    pcm[i] = (int16_t)((l + r) * 0.5f * g);
    sd[i] = (int16_t)((l - r) * 0.5f * g);
  }
}

static volatile uint8_t txQNow;            // качество, которое сейчас идёт в эфир (может быть ниже выбранного)
static volatile bool txStNow;              // в эфир сейчас идёт стерео
static volatile bool txMute;               // тишина в эфире: пакеты идут (приёмники держат связь), звука в них нет
// Затор в эфире. Когда канал занят чужими передачами, пакеты ждут в очереди драйвера (до 32 штук = 64 мс звука на
// «найвищій»), а потом уходят пачкой. Приёмнику такой звук уже не нужен: его запас (до 30 мс) кончился, он сыграл
// тишину, а опоздавшая пачка переполняет запас. Мало того: чтобы разослать пачку, нужен эфир, которого и так нет, —
// затор сам себя продлевает. Замер 07.10 (канал 1, «найвища» стерео, 5,5 Мбит/с): в занятые секунды ожидание до
// 56 мс, отказы драйвера, у приёмника провалы до 30 мс.
// Поэтому очередь держим короткой:
//  — очередь длиннее TX_THIN_US — шлём один пакет из (копий + 1): в нём запасные копии пропущенных кадров, и приёмник
//    собирает звук без разрыва (эти кадры — сжатые и в моно); эфира при этом нужно втрое меньше, затор рассасывается;
//  — очередь длиннее TX_HARD_US — не шлём вовсе: этот звук опоздал бы. Приёмник закроет дыру сам и не накопит задержку.
// Команда порта T0 выключает это (для сравнения), T1 включает.
// Пороги — в длине очереди, пересчитанной во время звука. В спокойном эфире очередь на «найвищій» доходит до 3–5
// пакетов (замер), прореживание начинается с шести.
#define TX_THIN_US 12000
#define TX_HARD_US 20000
static volatile bool txThinOn = true;
static volatile uint32_t txThinned, txSkipped;   // за секунду: пакетов пропущено с заменой копиями и кадров потеряно совсем
static volatile uint32_t txSkipMs;                // когда последний раз кадр пропал совсем: эфир был занят, и потери у приёмников — не от слабого сигнала
static volatile uint32_t txLostMsAll;             // сколько миллисекунд звука пропало у играющих приёмников (по их сообщениям), нарастающим итогом — для «мягкого» ухода с канала
// Запись настроек во флеш выключает кэш на 8–120 мс (долго — когда NVS стирает страницу), и звук со входа за это время
// теряется: замер 09.10 — в секунду записи в эфир ушло 438 пакетов из 500, у приёмника «порожньо 105 мс». Поэтому
// несрочную запись откладываем до паузы в звуке: 0,15 с в эфире тише −46 дБ (или передача стоит). Дольше maxMs не
// ждём — запишем как есть: настройка не должна пропасть, если питание выключат.
// шумозаглушение входа (nsfx.h) — для отчёта: на сколько сейчас убавлено (в среднем, дБ), сколько шагов сочтено
// паузой (по ним запоминается шум), сколько времени занимает
static volatile float txNsRedDb;
static volatile uint32_t txNsUsSum, txNsUsMax, txNsCalls, txNsPauseHops;
static volatile bool txNsLearnAsk;   // «запомнить шум сейчас» (порт: e7=1)
static volatile bool txNsFrozen;     // шум запомнен по просьбе и не обновляется (e7=0 — снова сам)
static volatile bool txNsLearning;   // идёт секунда запоминания
// Шумозаглушение входа считает отдельная задача на ядре экрана. На ядре радио (в задаче передачи) оно стоило 0,43 мс
// на двухмиллисекундный кадр, а на кадрах с расчётом спектра — 0,87 мс: треть кадров выходила за свои 2 мс (замер
// 11.10, вход PCM1808, моно: «довших за 2000 мкс 6206 із 19876»). Ядро экрана свободнее, и задача на нём работает
// одновременно с передачей: кадр кладётся в кольцо, а забирается готовым через кадр.
#define TXNS_RING 8
struct TxNsFrame {
  int16_t m[FRAME], s[FRAME];
};
static TxNsFrame *txNsIn, *txNsOut;            // что отдано на обработку и что готово (по номеру кадра)
static volatile uint32_t txNsPut, txNsDone;    // сколько кадров отдано / обработано
static volatile uint32_t txNsLate;             // сколько раз кадр не успел (в эфир пошёл необработанный)
static volatile uint8_t txNsLevel, txNsSensV;
static volatile bool txNsStereo, txNsResetAsk;
static TaskHandle_t txNsTaskH;
static void txNsTask(void *) {
  static float work[NSX_N] __attribute__((aligned(16)));
  NsSpec *ns = (NsSpec *)heap_caps_malloc(sizeof(NsSpec), MALLOC_CAP_SPIRAM);
  if (ns) ns->reset();
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (txNsDone != txNsPut) {
      uint32_t k = txNsDone;
      TxNsFrame f = txNsIn[k & (TXNS_RING - 1)];
      if (ns) {
        if (txNsResetAsk) {
          txNsResetAsk = false;
          ns->reset();
        }
        ns->level = txNsLevel;
        ns->sens = txNsSensV;
        if (txNsLearnAsk) {
          txNsLearnAsk = false;
          ns->learnStart();
        }
        if (!txNsFrozen && !ns->learnLeft) ns->frozen = false;
        txNsFrozen = ns->frozen;
        txNsLearning = ns->learnLeft > 0;
        uint32_t t0 = (uint32_t)esp_timer_get_time();
        ns->run(f.m, f.s, FRAME, txNsStereo, work);
        uint32_t d = (uint32_t)esp_timer_get_time() - t0;
        txNsUsSum = txNsUsSum + d;
        txNsCalls = txNsCalls + 1;
        if (d > txNsUsMax) txNsUsMax = d;
        txNsRedDb = ns->redDb;
        txNsPauseHops = ns->pauseHops;
      }
      txNsOut[k & (TXNS_RING - 1)] = f;
      txNsDone = k + 1;
    }
  }
}
// задача и её память заводятся, когда шумозаглушение включают впервые; false — памяти не нашлось
static bool txNsStart() {
  if (txNsTaskH) return true;
  if (!txNsIn) txNsIn = (TxNsFrame *)heap_caps_calloc(TXNS_RING, sizeof(TxNsFrame), MALLOC_CAP_SPIRAM);
  if (!txNsOut) txNsOut = (TxNsFrame *)heap_caps_calloc(TXNS_RING, sizeof(TxNsFrame), MALLOC_CAP_SPIRAM);
  if (!txNsIn || !txNsOut) return false;
  return xTaskCreatePinnedToCore(txNsTask, "txns", 4096, NULL, 18, &txNsTaskH, 1) == pdPASS;
}
// Шумовой порог входа (с 3.17; настройки cfg.txGate…). Владелец 16.10: «имеется ровный фоновый шум в виде шипения с
// передатчика. Аппаратные решения в виде добавления конденсаторов по питанию результатов не дали», «при проверочном
// звуке шипение полностью отсутствует, а значит причина точно в adc», «функции настраиваемые должны быть». Он предлагал
// простой порог: «если средняя амплитуда в буфере ниже порога — заполнить буфер нулями». Так у слов срезались бы
// окончания и тихие слоги, а у порога звук «дребезжал» бы (то пропадает, то нет) со щелчками на краях кадров. Здесь —
// тот же порог, но как у приёмника «у паузах» (nsfx.h, NsDyn): шум входа меряется сам (самое тихое: вниз быстро, вверх
// на 0,25 дБ/с), порог — на 3…15 дБ над ним (или вручную, в дБ), мягкое колено 6 дБ, после звука порог держится
// открытым 60…300 мс (окончания слов), открывается за кадр (2 мс), закрывается ровно (в децибелах) за 60…400 мс; в
// паузах звук тише на 12 / 20 / 30 дБ или глушится в ноль. Множитель меняется по отсчётам — без щелчков. Только звук с пульта:
// проверочный звук и файлы с карты чистые.
struct TxGate {
  float floorDb = -60, levDb = -96, thrDb = -60, gDb = 0, g = 1;
  int hold = 0;
  bool openNow = true;
  void open() {
    gDb = 0;
    g = 1;
    hold = 0;
    openNow = true;
  }
  // уровень кадра (сумма квадратов отсчётов середины и разности) в дБ от полной шкалы и шум входа
  NSX_HOT void measure(float sumSq) {
    float ms = sumSq * (1.0f / FRAME) * (1.0f / (32768.0f * 32768.0f));
    levDb = 3.01029996f * nsxLog2a(ms + 1e-12f);   // 10·lg
    if (levDb < floorDb) floorDb += (levDb - floorDb) * 0.05f;   // вниз — быстро (за ~40 мс)
    else floorDb += 0.0005f;                                     // вверх — 0,25 дБ в секунду
    if (floorDb > -36) floorDb = -36;                            // громче — уже не шум, а звук
    if (floorDb < -96) floorDb = -96;
  }
  float thrFor(uint8_t thr, int8_t manDb) const {
    return manDb ? (float)manDb : floorDb + 3.0f + 3.0f * (thr > 4 ? 2 : thr);
  }
  // depth 1…4 — в паузах тише на 12 / 20 / 30 дБ / тишина; rel 0…2 — быстро / средне / медленно. Убавление считается в
  // децибелах и гаснет ровно (децибелы за кадр), а не «по экспоненте» множителя: иначе тишина наступала бы через секунду.
  void apply(int16_t *m, int16_t *s, int n, uint8_t depth, uint8_t thr, uint8_t rel, int8_t manDb);
};
NSX_HOT IRAM_ATTR void TxGate::apply(int16_t *m, int16_t *s, int n, uint8_t depth, uint8_t thr, uint8_t rel, int8_t manDb) {
  static const float DEPTH[5] = { 0, 12, 20, 30, 60 };   // «тишина» — до −60 дБ, дальше ровно ноль
  static const int HOLD[3] = { 30, 75, 150 };            // кадров по 2 мс: держится открытым 60 / 150 / 300 мс
  static const int FALL[3] = { 30, 75, 200 };            // и гаснет до конца за 60 / 150 / 400 мс
  if (depth > 4) depth = 4;
  if (rel > 2) rel = 1;
  thrDb = thrFor(thr, manDb);
  float x = (levDb - thrDb) * (1.0f / 6.0f);   // мягкое колено: 0 — у порога, 1 — на 6 дБ громче
  x = x < 0 ? 0 : x > 1 ? 1 : x;
  if (x >= 1) hold = HOLD[rel];
  else if (hold > 0) {
    hold--;
    x = 1;
  }
  float tDb = -DEPTH[depth] * (1 - x), g0 = g;
  if (tDb >= gDb) gDb = tDb;   // открыть — за этот кадр
  else {                       // закрыть — ровно, по настройке
    gDb -= DEPTH[depth] / FALL[rel];
    if (gDb < tDb) gDb = tDb;
  }
  g = depth == 4 && gDb <= -59.9f ? 0.0f : nsxExp2a(gDb * 0.166096405f);   // 10^(дБ/20)
  openNow = gDb > -6;
  if (g0 == 1 && g == 1) return;
  if (g0 == 0 && g == 0) {
    memset(m, 0, n * sizeof(int16_t));
    memset(s, 0, n * sizeof(int16_t));
    return;
  }
  float d = (g - g0) / n;   // множитель — по отсчётам: без щелчка на краю кадра
  for (int i = 0; i < n; i++) {
    float k = g0 + d * (i + 1);
    m[i] = (int16_t)lrintf(m[i] * k);
    s[i] = (int16_t)lrintf(s[i] * k);
  }
}
// Ограничитель пиков входа (с 3.18; настройки cfg.txLim…). Владелец 16.10: «при перегрузах на входе начинается треск
// на выходе. Нужно, чтобы передатчик выполнял компрессию звука в таких пиках, чтобы при завышенном входе, на выходе был
// максимально громкий звук без искажений и треска»; «на тестовом звуке, даже при переусилении, на выходе приемников звук
// чистый. проблема с треском только на ацп». До 3.18 эквалайзер входа и подсиление упирались в край шкалы жёстко
// (срез — тот же треск), а у владельца на входе эквалайзер +2 дБ на верхах. Теперь после эквалайзера и подсиления звук
// идёт через задержку LA отсчётов (1,5 мс), и усиление опускается заранее — к приходу пика ровно настолько, чтобы
// больший из каналов (|Л| или |П| = |середина| + |разность|) не вышел за потолок; ниже потолка, по настройке, пики
// мягко сжимаются (колено ±3 дБ). Устроено как ограничитель приёмника (rxaudio.h, с 2.86): наименьшее «надо убавить»
// по окну — очередью, затем отпускание и среднее за окно; поэтому за потолок отсчёт не выходит (каждое из LA прошлых
// значений не больше нужного выходящему отсчёту — все их окна его видели). Что сам АЦП срезал, ограничитель не вернёт —
// для этого счёт АЦП (txAdc…) и совет в описании: убавить уровень до АЦП.
struct TxLim {
  enum { LA = 48, DQ = 64 };
  float bm[LA], bs[LA], box[LA], dqV[DQ];
  uint32_t dqT[DQ];
  uint32_t t = 0, dqH = 0, dqE = 0;
  int pos = 0, bpos = 0, bN = 0;
  float r1 = 1, boxSum = LA;
  TxLim() {
    for (int i = 0; i < LA; i++) {
      bm[i] = bs[i] = 0;
      box[i] = 1;
    }
  }
  // m, s — середина и разность после эквалайзера и подсиления (в шкале 16 бит, могут быть за краем); выход — с задержкой LA.
  // С 3.21 — во внутренней памяти и с -O2 (см. txInEqGain).
  void run(const float *m, const float *s, int16_t *om, int16_t *os, int n, uint8_t ceil, uint8_t comp, uint8_t rel, float &lowG, uint32_t &busy);
};
NSX_HOT IRAM_ATTR void TxLim::run(const float *m, const float *s, int16_t *om, int16_t *os, int n, uint8_t ceil, uint8_t comp, uint8_t rel, float &lowG, uint32_t &busy) {
  static const float CEIL[4] = { 30934.0f, 29205.0f, 26029.0f, 23198.0f };   // −0,5 / −1 / −2 / −3 дБ от 32768
  static const float KNEE[4] = { 0, -7.0f, -11.0f, -15.0f };                 // колено (дБ от полной шкалы) при потолке −1 дБ
  static const float SLOPE[4] = { 0, 0.5f, 0.6667f, 0.75f };                 // 1 − 1/R: 2:1, 3:1, 4:1
  static const float REL[3] = { 1 - 0.99921907f, 1 - 0.99973961f, 1 - 0.99989584f };   // 40 / 120 / 300 мс при 32 кГц
  const float lim = CEIL[ceil > 3 ? 1 : ceil];
  const uint8_t c = comp > 3 ? 1 : comp;
  const float kneeDb = KNEE[c] + (lim == CEIL[1] ? 0 : 20 * log10f(lim * (1.0f / 29205.0f))), relK = REL[rel > 2 ? 1 : rel];
  const float kneeLin = c ? 32768.0f * powf(10.0f, (kneeDb - 3) / 20) : 1e9f;
  for (int i = 0; i < n; i++) {
    float pk = fabsf(m[i]) + fabsf(s[i]), need = pk > lim ? lim / pk : 1.0f;
    if (pk > kneeLin) {   // мягкое сжатие пиков ниже потолка
      float d = 6.02059991f * nsxLog2a(pk * (1.0f / 32768)), over = d - kneeDb, red;
      if (over < 3) red = (over + 3) * (over + 3) * (1.0f / 12) * SLOPE[c];
      else red = over * SLOPE[c];
      float g = nsxExp2a(-red * 0.166096405f);
      if (g < need) need = g;
    }
    float dm = bm[pos], ds = bs[pos];   // отсчёт, пришедший LA назад, — он сейчас пойдёт на выход
    bm[pos] = m[i];
    bs[pos] = s[i];
    pos = pos + 1 == LA ? 0 : pos + 1;
    t++;
    while (dqE != dqH && dqV[(dqE - 1) & (DQ - 1)] >= need) dqE--;
    dqV[dqE & (DQ - 1)] = need;
    dqT[dqE & (DQ - 1)] = t;
    dqE++;
    while (t - dqT[dqH & (DQ - 1)] >= LA) dqH++;
    float mn = dqV[dqH & (DQ - 1)];
    if (mn < r1) r1 = mn;
    else r1 += (mn - r1) * relK;
    float g = boxSum * (1.0f / LA);
    boxSum += r1 - box[bpos];
    box[bpos] = r1;
    bpos = bpos + 1 == LA ? 0 : bpos + 1;
    if (++bN >= 4096) {
      bN = 0;
      boxSum = 0;
      for (int k = 0; k < LA; k++) boxSum += box[k];
    }
    float vm = dm * g, vs = ds * g;
    om[i] = (int16_t)(vm > 32767 ? 32767 : vm < -32768 ? -32768 : lrintf(vm));
    os[i] = (int16_t)(vs > 32767 ? 32767 : vs < -32768 ? -32768 : lrintf(vs));
    if (g < lowG) lowG = g;
    if (g < 0.999f) busy++;
  }
}
// Эквалайзер и подсиление входа (3.21). Владелец 16.10: «реальный звук на приемники идет на несколько порядков тише от
// тестового… с искажениями и треском». Замер (`Q`): с пультом задача передачи тратила на кадр в среднем 1,76 мс из 2
// (41 % кадров — дольше 2 мс; обработка входа — 465 мкс против 232 у проверочного звука), и I2S терял кадры: 147 за 7 с —
// каждый разрыв звука щелчок. Горячие места входа — эта функция, ограничитель и порог — с 3.21 собираются с -O2 и
// (кроме measure) лежат во внутренней памяти: вся прошивка — -Os из флеша, а ядро радио делит кэш с драйвером Wi-Fi.
// Источник — точные середина и разность входа (fine: txFineM / txFineS, см. txReadI2s) или 16-битные pcm/side.
static NSX_HOT IRAM_ATTR void txInEqGain(Eq6 &eq, bool eqS, float k, const int16_t *pcm, const int16_t *side, bool fine, float *fm, float *fs) {
  const bool any = eq.any;
  for (int i = 0; i < FRAME; i++) {
    float v = fine ? txFineM[i] : pcm[i], u = fine ? txFineS[i] : side[i];
    if (any) v = eq.run(0, v);
    if (eqS) u = eq.run(1, u);
    fm[i] = v * k;
    fs[i] = u * k;
  }
}
static volatile float txLimLowG = 1;     // для отчёта: наименьшее усиление ограничителя (обнуляет читающий)
static volatile float txLimLowUi = 1;    // то же для экрана (лист «Піки входу», с 3.24; обнуляет ui_tx.h раз в секунду)
static volatile uint32_t txLimBusy;      // и сколько отсчётов он убавлял
static volatile float txGateFloorDb = -96, txGateThrDb = -96, txGateLevDb = -96;   // для отчёта и экрана
static volatile bool txGateOpen = true, txGateActive;
static volatile uint32_t txGateFrames, txGateClosed, txGateCloses;
static volatile uint32_t txLoudMs;            // когда в эфир последний раз шёл звук громче −46 дБ (0 — ещё не было)
static bool txQuietToSave(uint32_t dueMs, uint32_t maxMs);
static volatile bool txEqOnTone;              // проба (порт: e9=1): эквалайзер входа действует и на проверочный тон 1 кГц — для замера
static volatile uint32_t txEqGen = 1;         // растёт при смене эквалайзера входа — задача передачи пересчитывает звенья
// Два кадра в пакете (proto.h, Q_DOUBLE). Порт: Qd0 — никогда, Qd1 — когда все приёмники на связи умеют (2.60+),
// Qd2 — всегда (проверка). txDoubleOk считает otaAutoTick по версиям приёмников на связи.
// С 2.77 двойной пакет несёт два прошлых кадра без сжатия («с точными копиями», proto.h, qPktLen3) — когда все
// приёмники из списка сообщали 2.77+ (txDupOk, считает там же). Qd3 — двойные по-старому, только со сжатыми копиями
// (для сравнения на слух); Qd2 — всегда двойные и всегда с точными копиями.
static volatile uint8_t txDoubleMode = 1;
static volatile bool txDoubleOk;
static volatile bool txDupOk;                // все приёмники из списка понимают пакет «с точными копиями» (2.77+)
// Подполосный кодек (subc.h, proto.h — Q_SUB), с 2.79: им идёт «стандартна» в моно, когда все приёмники из списка
// сообщали 2.79+ (txSubOk). Порт: Qc0 — не применять (прежнее сжатие; для сравнения на слух), Qc1 — как положено.
static volatile bool txSubOk;                // все приёмники из списка понимают подполосный кодек
static volatile bool txSubOff;               // проверка (Qc0): кодек не применять
static volatile bool txSubNow;               // сейчас в эфир идёт он
// Качество, которое пойдёт в эфир на этой скорости: выбранное или ближайшее ниже, которое помещается (как qForRate),
// но «стандартна» в моно с новым кодеком легче прежней — пакет 158 байт вместо 223 — и помещается даже в 1 Мбит/с.
// Со стерео: пока стерео в выбранном качестве помещается — идёт оно (прежним сжатием). Не помещается (нижняя
// ступень, 1 Мбит/с) — эфир переходит в моно на новый кодек, а не в «мова» со стерео: у той одна запасная копия
// (при 25 % потерь пропадало 5 % звука), у нового кодека три. Экран при этом пишет «стерео (зараз моно)».
static inline int txQPick(int want, int rateIdx, bool st, bool *sub) {
  int q = want < 0 ? 0 : want >= Q_COUNT ? Q_COUNT - 1 : want;
  for (;; q++) {
    bool subFits = q == Q_STD && txSubOk && !txSubOff && airUs(qPktLenSub(), rateIdx) * 100 <= qFrameUs(Q_STD) * 45;
    bool normal = q >= Q_COUNT - 1 || qFits(q, rateIdx, st);
    if (subFits && (!st || !normal)) {
      if (sub) *sub = true;
      return q;
    }
    if (normal) {
      if (sub) *sub = false;
      return q;
    }
  }
}
static volatile bool txDupNow;               // сейчас в эфир идут такие пакеты
// Стерео «найвищої» с разностью каналов без сжатия (с 2.86, proto.h — qPktLen4; с 2.87 — и в копиях прошлого пакета):
// когда все приёмники из списка сообщали 2.87 или новее (txPcmSOk, считает otaAutoTick). Порт: Qr0 — разность
// по-прежнему 4-битная (сравнение на слух), Qr1 — как положено, Qr2 — всегда (проверка). Не запоминается.
static volatile uint8_t txSideMode = 1;
static volatile bool txPcmSOk;
static volatile bool txPcmSNow;              // сейчас в эфир идёт разность без сжатия
static volatile uint8_t txDoubleWhy;         // почему двойные пакеты нельзя: 0 — можно, 1 — в списке есть приёмник без
static uint8_t txDoubleWho[3];               // подтверждённой версии 2.60+ (его номер — здесь), 2 — открыто окно добавления
static volatile bool txDblNow;               // сейчас в эфир идут двойные пакеты
static volatile bool txSideCopyOn;            // слать копию разности каналов: её понимают все приёмники на связи (см. proto.h)
static volatile uint32_t txThinMs;               // когда последний раз шёл затор (несколько пропусков подряд), а не одиночный пропуск
// Переход на другой канал без паузы (см. txscan.h): главный цикл выбирает канал и ставит txHopCh; задача передачи
// назначает номер пакета, с которого передатчик будет на новом канале, объявляет его приёмникам и в нужный миг
// переставляет радио. Объявление шлёт txHopSend (в txpeers.h — там очередь команд).
static volatile uint8_t txHopCh;                 // на какой канал перейти (0 — перехода нет)
static volatile uint32_t txHopSeq;               // с какого пакета
static volatile bool txHopDone;                  // переход сделан — главному циклу: записать канал и сказать на экране
static void (*txHopSend)(uint8_t ch, uint32_t atSeq);
static volatile uint32_t txSeqNow;               // номер последнего пакета (по нему проверяется свежесть сведений приёмников)
static volatile bool txHold;               // передачу ещё не начинать: на экране заставка
static volatile int16_t txPeakAir;         // наибольший отсчёт того, что реально ушло в эфир, за секунду (для отчёта)
static volatile int16_t txPeakS;           // наибольший отсчёт разности каналов за секунду (для отчёта)

static void txTask(void *) {
  static uint8_t pkt[PKT_MAX];
  static uint8_t hist[1 + DBL_COPIES][3 + Q_MAX_N / 2];   // сжатые прошлые кадры: [0] — N−1, [1] — N−2 … (двойному пакету нужны [1…DBL_COPIES])
  static uint8_t pairMain[Q_MAX_N * 2], pairSide[3 + Q_MAX_N / 2];   // первый кадр пары ждёт второго
  static int16_t pairSideP[FRAME];           // и его разность без сжатия (с 2.86)
  static uint8_t pcmHistS[1 + DUP_PCM][FRAME * 2];   // разность прошлых кадров «найвищої» без сжатия — копии N−1 и N−2 (с 2.87)
  static bool pcmS;                          // пара идёт с разностью без сжатия (proto.h, qPktLen4)
  static uint8_t pcmHist[1 + DUP_PCM][FRAME * 2];   // прошлые кадры «найвищої» без сжатия: [0] — N−1, [1] — N−2 … (как hist)
  static SubcEnc subEnc;                     // подполосный кодек: разбор кадра (с 2.79)
  static uint8_t subF0[2][SUB_F0], subF2[3][SUB_F2], subF3[4][SUB_F3];   // кадр в трёх видах: [0] — новый, [1] — N−1, [2] — N−2, [3] — N−3
  static bool sub;                           // «стандартна» идёт подполосным кодеком
  static bool dup;                           // двойные пакеты идут «с точными копиями» (с 2.77)
  static uint8_t bcn[256];                   // одиночный пакет «для старых приёмников» три раза в секунду
  static uint16_t bcnLen;                    // он собран и ждёт свободного кадра пары (0 — нет)
  static bool dbl, pairHave;                 // идём двойными пакетами; первый кадр пары уже лежит
  static uint32_t pairSeq;
  static uint8_t pairFl;
  static uint8_t blk[3 + Q_MAX_N / 2];
  static uint8_t blkS[3 + Q_MAX_N / 2];
  static Eq6 inEq;                                // эквалайзер входа (см. eq.h)
  uint32_t inEqGen = 0;
  static uint8_t scNow[SC_LEN], scHist[1 + DBL_COPIES][SC_LEN];   // копия разности: этого кадра и прошлых (как hist)
  static int16_t pcm[FRAME], side[FRAME];     // середина (Л+П)/2 и разность (Л−П)/2
  static int16_t acc[FRAME * 6], accS[FRAME * 6];   // набор на пакет, 32 кГц
  static int16_t frm[Q_MAX_N], frmS[Q_MAX_N];       // он же после перехода на 16 кГц
  static Decim2 dec, decS;
  Hdr *h = (Hdr *)pkt;
  int idxCarry = 0, idxCarryS = 0, idxCarrySc = 0, q = -1, accN = 0;
  bool st = false;
  uint8_t accFlags = 0;
  uint32_t seq = 0, boot = secEpoch;   // эпоха: номер этого включения (к запуску задачи уже увеличен и записан)
  txSoundsOk = assetsBegin();
  bool ok = cfg.input == IN_I2S ? txI2sBegin() : txAdcBegin();
  if (!ok) Serial.println("вхід звуку не запустився — передаю тишу");
  TickType_t tick = xTaskGetTickCount();
  const uint8_t inMode = cfg.input;   // вход — тот, с которым задача запущена: команда смены входа меняет cfg.input сразу,
                                      // а перезапуск следует позже — до 2.56 задача в этот промежуток читала вход, которого нет
  uint32_t tpAfter = 0, tsec = 0;
  // Сторож задач (5 с) следит за САМОЙ этой задачей, а не за холостой задачей ядра радио (её сторож снят в setup).
  // Ядро радио при одиночных пакетах занято на 93–100 %, и «холостая задача пять секунд не получала времени» там —
  // не зависание, а обычная работа с попутной фоновой нагрузкой; сторож же перезапускал из-за этого передатчик
  // (10.10: пять раз подряд при раздаче обновления). Настоящее зависание — задача передачи перестала проходить
  // свой круг (в том числе повисла внутри драйвера радио) — он ловит по-прежнему.
  esp_err_t wdtAdd = esp_task_wdt_add(NULL);
  bool wdtMine = wdtAdd == ESP_OK;
  txWdtErr = wdtAdd;
  for (;;) {
    if (wdtMine) esp_task_wdt_reset();
    uint32_t clipsBefore = txClips;
    memset(side, 0, sizeof(side));
    if (tpAfter) {   // сколько длилась обработка прошлого кадра (от конца чтения входа до начала следующего чтения)
      uint32_t p = (uint32_t)esp_timer_get_time() - tpAfter;
      txProcSumUs = txProcSumUs + p;
      txProcN = txProcN + 1;
      if (p > txProcMaxUs) txProcMaxUs = p;
      if (p > 2000) txProcOver = txProcOver + 1;
      txSec[6] = txSec[6] + ((uint32_t)esp_timer_get_time() - tsec);
    }
    uint32_t tRead = (uint32_t)esp_timer_get_time();
    bool got = ok && (inMode == IN_I2S ? txReadI2s(pcm, side) : txReadAdc(pcm));
    tpAfter = (uint32_t)esp_timer_get_time();
    tsec = tpAfter;
    // Перегрузка: если кадры обрабатываются дольше, чем приходят, чтение входа перестаёт ждать, задача крутится без
    // единой паузы, ядру радио не достаётся «холостого» времени — и через 5 с сторож задач перезапускает плату (так
    // было 09.10 во время голосовых объявлений: кадр в среднем 1,8 мс из 2, с голосом — больше; владелец:
    // «периодически на передатчике кратковременно гаснет экран»). Потеря миллисекунды звука лучше перезапуска:
    // после секунды без единого ожидания задача сама отдаёт ядро на один тик.
    {
      static uint16_t noWait;
      if (tpAfter - tRead > 250) noWait = 0;
      else if (++noWait >= 500) {
        noWait = 0;
        txYields = txYields + 1;
        vTaskDelay(1);
      }
    }
    if (!got) {
      static uint8_t fails;
      memset(pcm, 0, sizeof(pcm));
      memset(side, 0, sizeof(side));
      if (ok && inMode == IN_ADC && ++fails >= 2) {   // АЦП замолчал — перезапустить
        fails = 0;
        txAdcRestart();
        tick = xTaskGetTickCount();
      } else {   // входа нет — держим темп по часам. Отметка времени могла отстать (пока вход работал, её не двигали):
        TickType_t nowT = xTaskGetTickCount();   // тогда vTaskDelayUntil возвращается сразу, цикл крутится без пауз и
        if (nowT - tick > 20) tick = nowT;       // сторож роняет плату (так было 09.10, когда опыт отнял у I2S вывод)
        vTaskDelayUntil(&tick, 2);
      }
    }
    txMclkPin(!cfg.tone && !txCardOn);   // вывод 43: такт PCM1808 — только когда в эфир идёт сам вход
    if (txHold) {   // заставка ещё на экране: в эфир ничего не идёт, проверочный звук не начинается
      rLastDoneMs = millis();
      continue;
    }
    if (txPause && txPauseLight) {   // идёт раздача прошивки: звук не готовим (см. txPauseLight)
      txPaused = true;
      rLastDoneMs = millis();
      continue;
    }
    if (inEqGen != txEqGen) {   // эквалайзер входа сменили — пересчитать звенья
      inEqGen = txEqGen;
      inEq.build(cfg.txEq, cfg.txLowCut != 0);
    }
    bool card = txCardOn;   // файл с карты — впереди и входа, и проверочного звука
    bool plainTone = !card && (cfg.tone == TEST_TONE || (cfg.tone >= TEST_VOICE && cfg.tone != TEST_LR && !txSoundsOk));
    if (cfg.tone || card) memset(side, 0, sizeof(side));
    if (card) txCardSound(pcm, side);
    else if (cfg.tone == TEST_LR) txToneLR(pcm, side);
    else if (plainTone) {
      txTone(pcm);
      if (txEqOnTone && inEq.any)
        for (int i = 0; i < FRAME; i++) {
          float v = inEq.run(0, pcm[i]);
          pcm[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
        }
    }
    else if (cfg.tone) txTestSound(pcm, side);
    else if (cfg.gainDb || inEq.any || (cfg.txLim && got)) {   // эквалайзер входа, подсиление, ограничитель пиков (с 3.18)
      static int8_t lastDb = 0;
      static float k = 1;
      static TxLim *lim;
      if (lastDb != cfg.gainDb) {
        lastDb = cfg.gainDb;
        k = powf(10.0f, lastDb / 20.0f);
      }
      bool eqS = inEq.any && cfg.input == IN_I2S;   // разность каналов есть только у внешнего АЦП
      bool fine = got && inMode == IN_I2S;          // вход моста: середина и разность с полной точностью (txFineM/S)
      if (cfg.txLim && got && !lim) lim = new (std::nothrow) TxLim();   // из кучи: прошивка общая, приёмнику он не нужен
      if (lim && cfg.txLim && got) {
        static float fm[FRAME], fs[FRAME];
        txInEqGain(inEq, eqS, k, pcm, side, fine, fm, fs);
        float low = 1;
        uint32_t busy = txLimBusy;
        lim->run(fm, fs, pcm, side, FRAME, cfg.txLimCeil, cfg.txLimComp, cfg.txLimRel, low, busy);
        if (low < txLimLowG) txLimLowG = low;
        if (low < txLimLowUi) txLimLowUi = low;
        txLimBusy = busy;
      } else
        for (int i = 0; i < FRAME; i++) {
          float v = fine ? txFineM[i] : pcm[i], u = fine ? txFineS[i] : side[i];
          if (inEq.any) v = inEq.run(0, v);
          if (eqS) u = inEq.run(1, u);
          v *= k;
          u *= k;
          pcm[i] = txRound16(v);   // округлением: «(int16_t)v» отбрасывал дробь к нулю — мёртвая зона ±1 у нуля
          side[i] = txRound16(u);
        }
    }
    // Шумозаглушение входа (с 2.68): только звук с пульта — проверочный звук и файлы чистые. Считает его отдельная
    // задача на ядре экрана (txNsTask): кадр отдаётся ей, а в эфир идёт кадр, отданный пять кадров назад (10 мс, с 3.23).
    {
      static bool nsWas;
      bool nsOn = cfg.txNs >= 1 && cfg.txNs <= 3 && !cfg.tone && !card && got && txNsStart();
      if (nsOn) {
        if (!nsWas) txNsResetAsk = true;   // включили (или вернулись ко входу): очередь и оценка — с чистого листа
        txNsLevel = cfg.txNs;
        txNsSensV = cfg.txNsSens > 4 ? 2 : cfg.txNsSens;
        txNsStereo = cfg.stereo && cfg.input == IN_I2S;
        uint32_t k = txNsPut;
        TxNsFrame &f = txNsIn[k & (TXNS_RING - 1)];
        memcpy(f.m, pcm, sizeof(f.m));
        memcpy(f.s, side, sizeof(f.s));
        txNsPut = k + 1;
        xTaskNotifyGive(txNsTaskH);
        // В эфир — кадр, отданный на обработку ДВА кадра назад (с 3.23; было — один): у задачи на него 4 мс. Шаг
        // шумодава (БПФ на 256 отсчётов) приходится на каждый второй кадр и стоит ~1,3 мс, а ядро экрана делит время с
        // выводом картинки — с одним кадром запаса задача не успевала тысячи раз (16.10: «не встигло кадрів 51772»), и в
        // эфир шёл кадр без обработки: в тишине — всплеск шума на 2 мс, на 17 дБ громче соседних. Владелец: «частые
        // тихие щелчки и потрескивание на фоне… как только звук очень тихий», «шум похож на цифровые помехи». Задержка
        // пути шумодава — 10 мс (было 8). Не успела и теперь (запись во флеш) — идёт тот же звук без обработки с той же
        // задержкой, но приглушённый на столько, сколько шумодав сейчас убирает: без всплеска.
        if ((int32_t)(txNsDone - (k - 1)) >= 0 && k >= 2) {
          const TxNsFrame &o = txNsOut[(k - 2) & (TXNS_RING - 1)];
          memcpy(pcm, o.m, sizeof(o.m));
          memcpy(side, o.s, sizeof(o.s));
        } else {
          const TxNsFrame &o = txNsIn[(k - 5) & (TXNS_RING - 1)];
          if (k >= 5) {
            float red = txNsRedDb, g = red < 0 ? nsxExp2a(red * 0.166096405f) : 1.0f;   // 10^(дБ/20)
            for (int i = 0; i < FRAME; i++) {
              pcm[i] = (int16_t)lrintf(o.m[i] * g);
              side[i] = (int16_t)lrintf(o.s[i] * g);
            }
            txNsLate = txNsLate + 1;
          } else {
            memset(pcm, 0, sizeof(pcm));
            memset(side, 0, sizeof(side));
          }
        }
      }
      nsWas = nsOn;
    }
    TXP(0)
    int pk = 0, pkS = 0;
    float sq = 0;
    for (int i = 0; i < FRAME; i++) {
      int a = pcm[i] < 0 ? -pcm[i] : pcm[i], b = side[i] < 0 ? -side[i] : side[i];
      if (a > pk) pk = a;
      if (b > pkS) pkS = b;
      sq += (float)pcm[i] * pcm[i];
    }
    if (pkS > txPeakS) txPeakS = pkS > 32767 ? 32767 : pkS;
    portENTER_CRITICAL(&txMux);
    txSqSum += sq;
    txSqCnt += FRAME;
    portEXIT_CRITICAL(&txMux);
    if (txSpecOn) {
      uint32_t sp = txSpecPos;
      for (int i = 0; i < FRAME; i++) txSpecBuf[(sp + i) & (TX_SPEC_N - 1)] = pcm[i];
      txSpecPos = sp + FRAME;
    }
    if (txOnFrame) txOnFrame(pcm);
    if (pk > txPeak) txPeak = pk > 32767 ? 32767 : pk;
    if (pk > txPeakUi) txPeakUi = pk > 32767 ? 32767 : pk;
    if (!cfg.tone && !card && (pk >= 32000 || txClips != clipsBefore)) txClipMs = millis();
    // Шумовой порог входа (с 3.17, TxGate выше): только звук с пульта. Шум входа меряется и при выключенном пороге —
    // чтобы на экране и в порту было видно, насколько шипит вход.
    {
      static TxGate gt;
      static float levShow = -96;
      bool src = !cfg.tone && !card && got;
      bool on = src && cfg.txGate >= 1 && cfg.txGate <= 4;
      if (src) {
        float sqS = 0;
        for (int i = 0; i < FRAME; i++) sqS += (float)side[i] * side[i];
        gt.measure(sq + sqS);
        bool was = gt.openNow;
        if (on) gt.apply(pcm, side, FRAME, cfg.txGate, cfg.txGateThr, cfg.txGateRel, cfg.txGateDb);
        else {
          gt.open();
          gt.thrDb = gt.thrFor(cfg.txGateThr, cfg.txGateDb);
        }
        levShow += (gt.levDb - levShow) * 0.05f;
        txGateFloorDb = gt.floorDb;
        txGateThrDb = gt.thrDb;
        txGateLevDb = levShow;
        if (on) {
          txGateFrames = txGateFrames + 1;
          if (!gt.openNow) txGateClosed = txGateClosed + 1;
          if (was && !gt.openNow) txGateCloses = txGateCloses + 1;
        }
      } else gt.open();
      txGateOpen = gt.openNow;
      txGateActive = on;
    }

    // Тишина в эфире: уровень и спектр выше уже посчитаны по входу — оператор видит, что звук с пульта есть.
    // Звук уходит и возвращается плавно (за 15 мс), без щелчка.
    {
      static float mg = 1;
      float goal = txMute ? 0 : 1;
      if (mg != goal || txMute) {
        for (int i = 0; i < FRAME; i++) {
          mg += goal > mg ? 1.0f / 480 : goal < mg ? -1.0f / 480 : 0;
          mg = mg < 0 ? 0 : mg > 1 ? 1 : mg;
          pcm[i] = (int16_t)(pcm[i] * mg);
          side[i] = (int16_t)(side[i] * mg);
        }
      }
      int pa = 0;
      for (int i = 0; i < FRAME; i++) {
        int a = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (a > pa) pa = a;
      }
      if (pa > txPeakAir) txPeakAir = pa > 32767 ? 32767 : pa;
      if (pa > 160) txLoudMs = millis() ? millis() : 1;
    }
    if (txRawCapKind == 1) {   // запись того, что уходит в эфир (с 3.21, e22): середина и разность после всей обработки
      txRawCapGo(!cfg.tone && !card && got);
      if (txRawCapState == 2 && txRawCap) {
        uint32_t n = txRawCapN, m = FRAME;
        if (n + m > txRawCapWant) m = txRawCapWant - n;
        for (uint32_t i = 0; i < m; i++) txRawCap[n + i] = (int32_t)((uint16_t)pcm[i] | ((uint32_t)(uint16_t)side[i] << 16));
        n += m;
        txRawCapN = n;
        if (n >= txRawCapWant) txRawCapState = 3;
      }
    }
    // Стерео идёт в эфир, когда оно включено и источник стереофонический: мелодии, проверка каналов, внешний АЦП.
    // Голос в списке с мелодиями идёт тем же видом пакета (разность — нули), чтобы приёмники не перестраивались.
    bool srcSt = card ? cpChans == 2 : cfg.tone ? cfg.tone == TEST_LR || ((cfg.tone == TEST_ALL || cfg.tone == TEST_MUSIC || cfg.tone == TEST_DUCK) && txSoundsOk) : cfg.input == IN_I2S;
    bool wantSt = cfg.stereo && srcSt;
    // качество: выбранное, а если на нынешней скорости радио оно не помещается — ближайшее ниже
    bool wantSub = false;
    int wantQ = txQPick(cfg.quality, cfg.rateIdx, wantSt, &wantSub);
    wantSt = wantSt && qStereoOk(wantQ) && !wantSub;   // новый кодек — только моно
    if (wantQ != q || wantSt != st || wantSub != sub) {
      q = wantQ;
      st = wantSt;
      sub = wantSub;
      txSubNow = sub;
      if (sub) {
        subcInit();
        subEnc.reset();
        memset(subF0, 0, sizeof(subF0));
        memset(subF2, 0, sizeof(subF2));
        memset(subF3, 0, sizeof(subF3));
      }
      txQNow = q;
      txStNow = st;
      accN = 0;
      accFlags = 0;
      idxCarry = 0;
      idxCarryS = 0;
      memset(hist, 0, sizeof(hist));
      memset(pcmHist, 0, sizeof(pcmHist));
      memset(pcmHistS, 0, sizeof(pcmHistS));
      pairHave = false;
    }
    const QDef &d = QDEF[q];
    if (!pairHave) {   // упаковка меняется только между парами
      dbl = q == Q_HI && (txDoubleMode >= 2 || (txDoubleMode == 1 && txDoubleOk));
      dup = dbl && (txDoubleMode == 2 || (txDoubleMode == 1 && txDupOk));
      pcmS = dup && st && (txSideMode == 2 || (txSideMode == 1 && txPcmSOk));
    }
    txDblNow = dbl;
    txDupNow = dup;
    txPcmSNow = pcmS;
    if (st) {   // при заторе стерео плавно сводится в моно: кадры из копий несут только середину, и разность «через раз» дребезжала бы
      static float sg = 1;
      // (с 2.43 на «найвищій» копии несут и разность — сводить незачем; осталось для прочих качеств и старых приёмников)
      float goal = !(txSideCopyOn && q == Q_HI) && txThinMs && msSince(txThinMs) < 500 ? 0 : 1;
      if (sg != goal || goal == 0)
        for (int i = 0; i < FRAME; i++) {
          sg += goal > sg ? 1.0f / 6400 : goal < sg ? -1.0f / 640 : 0;   // уходит за 20 мс, возвращается за 200
          sg = sg < 0 ? 0 : sg > 1 ? 1 : sg;
          side[i] = (int16_t)(side[i] * sg);
        }
    }
    TXP(1)
    memcpy(acc + accN * FRAME, pcm, sizeof(pcm));
    memcpy(accS + accN * FRAME, side, sizeof(side));
    accFlags |= (plainTone && !txMute ? FLAG_TONE : 0) | (txMute ? FLAG_MUTE : 0) | (!cfg.tone && !card && (pk >= 32000 || txClips != clipsBefore) ? FLAG_CLIP : 0);
    if (++accN < d.chunks) continue;   // кадр пакета ещё не набран
    accN = 0;
    const int16_t *src = acc, *srcS = accS;
    if (d.sr != SRATE) {
      dec.run(acc, d.n, frm);
      src = frm;
      if (st) {
        decS.run(accS, d.n, frmS);
        srcS = frmS;
      }
    }
    // На «найвищій» сжатая середина идёт только в запасные копии (нужна лишь при потере пакета) — ей хватит
    // простого сжатия; подбор шага оставлен там, где сжатое звучит всегда.
    if (sub) {   // подполосный кодек: один разбор — три вида кадра (для нового места в пакете и для будущих копий)
      for (int k = 1; k >= 1; k--) memcpy(subF0[k], subF0[k - 1], SUB_F0);
      for (int k = 2; k >= 1; k--) memcpy(subF2[k], subF2[k - 1], SUB_F2);
      for (int k = 3; k >= 1; k--) memcpy(subF3[k], subF3[k - 1], SUB_F3);
      subEnc.analyse(src);
      subEnc.pack(subF0[0], SUB_F0, false);
      subEnc.pack(subF2[0], SUB_F2, false);
      subEnc.pack(subF3[0], SUB_F3, true);
      memset(blk, 0, sizeof(blk));
    } else
    if (d.pcm) adpcmEncodeBlock(src, d.n, blk, idxCarry);
    else adpcmEncodeBlockBest(src, d.n, blk, idxCarry);
    if (st) adpcmEncodeBlockBest(srcS, d.n, blkS, idxCarryS);
    if (st && q == Q_HI) scEncode(srcS, scNow, idxCarrySc);
    else memset(scNow, 0, SC_LEN);

    TXP(2)
    {   // переход на другой канал вместе с приёмниками
      static bool armed;
      static uint8_t tick;
      uint8_t hc = txHopCh;
      if (hc && !armed) {   // назначить пакет: через ~0,2 с — чтобы объявление успело дойти до всех несколько раз
        uint32_t lead = 200000 / qFrameUs(q);
        txHopSeq = seq + (lead < 10 ? 10 : lead);
        if (dbl) {   // двойные пакеты: переход — на границе пары (первый кадр пары уйдёт уже с нового канала)
          uint32_t firstPar = pairHave ? ((seq ^ 1) & 1) : (seq & 1);
          if ((txHopSeq & 1) != firstPar) txHopSeq = txHopSeq + 1;
        }
        armed = true;
        tick = 0;
      }
      if (armed && hc) {
        if ((int32_t)(seq - txHopSeq) >= 0) {   // пора: дать уйти пакетам из очереди и переставить радио
          for (int i = 0; i < 30 && (int32_t)(rPut - rGot) > 0; i++) vTaskDelay(1);
          cfg.channel = hc;
          esp_wifi_set_channel(hc, WIFI_SECOND_CHAN_NONE);
          bbMark(BB_HOP, hc);
          armed = false;
          txHopCh = 0;
          txHopDone = true;
        } else if (txHopSend && (tick++ % (uint8_t)(qFrameUs(q) >= 8000 ? 1 : qFrameUs(q) >= 4000 ? 3 : 6)) == 0) {
          txHopSend(hc, txHopSeq);   // объявление — каждые ~12 мс, мимо общей очереди команд
        }
      }
    }
    uint32_t mySeq = seq++;   // у каждого кадра свой номер — и в одиночном пакете, и в двойном
    txSeqNow = mySeq;
    int bl = qBlock(q);
    bool stash = dbl && !pairHave;   // первый кадр пары: запомнить и ждать второго
    if (stash) {
      pairSeq = mySeq;
      pairFl = accFlags;
      memcpy(pairMain, src, d.n * 2);
      if (st) {
        memcpy(pairSide, blkS, bl);
        memcpy(pairSideP, srcS, d.n * 2);
      }
      pairHave = true;
    }
    h->magic = MAGIC;
    h->ver = PROTO_VER;
    h->kit = cfg.kit;
    h->boot = boot;
    h->seq = dbl ? pairSeq : mySeq;
    h->flags = accFlags | (dbl ? pairFl : 0) | (cfg.channel << 4);
    h->q = q | (st ? Q_STEREO : 0) | (cfg.lang ? Q_LANG_EN : 0) | (dbl ? Q_DOUBLE : 0) | (sub ? Q_SUB : 0);
    accFlags = 0;
    uint8_t *w = pkt + sizeof(Hdr);
    if (dbl && !stash) {   // первый кадр пары — из запомненного
      memcpy(w, pairMain, d.n * 2);
      w += d.n * 2;
      if (st && pcmS) {
        memcpy(w, pairSideP, d.n * 2);
        w += d.n * 2;
      } else if (st) {
        memcpy(w, pairSide, bl);
        w += bl;
      }
    }
    if (sub) {   // новый кадр, прошлый той же точности, позапрошлый попроще, трёхпакетной давности — грубо
      memcpy(w, subF0[0], SUB_F0);
      w += SUB_F0;
      memcpy(w, subF0[1], SUB_F0);
      w += SUB_F0;
      memcpy(w, subF2[2], SUB_F2);
      w += SUB_F2;
      memcpy(w, subF3[3], SUB_F3);
      w += SUB_F3;
    } else
    if (d.pcm) {
      memcpy(w, src, d.n * 2);
      w += d.n * 2;
    } else {
      memcpy(w, blk, bl);
      w += bl;
    }
    if (st && pcmS) {   // разность каналов — сразу за серединой; без сжатия (с 2.86)
      memcpy(w, srcS, d.n * 2);
      w += d.n * 2;
    } else if (st) {
      memcpy(w, blkS, bl);
      w += bl;
    }
    if (sub) {
    } else
    if (dbl) {   // копии кадров двух прошлых пакетов, от ближнего (N−2) к дальнему; в стерео — и копии их разности
      // (счёт k — от кадра, собираемого сейчас: [0] — первый кадр этой же пары, он уже лежит в пакете)
      int k = 1;
      if (dup)   // прошлый пакет — ещё раз без сжатия: его пропажа восстановится бит в бит (см. qPktLen3 в proto.h)
        for (; k <= DUP_PCM; k++, w += d.n * 2) memcpy(w, pcmHist[k], d.n * 2);
      for (; k <= DBL_COPIES; k++, w += bl) memcpy(w, hist[k], bl);
      if (st && pcmS)   // разность N−1 и N−2 — без сжатия (с 2.87); у N−3 и N−4 копии разности нет (proto.h, qPktLen4)
        for (int k2 = 1; k2 <= DUP_PCM; k2++, w += d.n * 2) memcpy(w, pcmHistS[k2], d.n * 2);
      else if (st)
        for (int k2 = 1; k2 <= DBL_COPIES; k2++, w += SC_LEN) memcpy(w, scHist[k2], SC_LEN);
    } else {
      for (int k = 0; k < qCopies(q, st); k++, w += bl) memcpy(w, hist[k], bl);
      if (st && q == Q_HI && txSideCopyOn && (seq & 63)) {   // копия разности прошлого кадра; каждый 64-й пакет — прежней длины
        memcpy(w, scHist[0], SC_LEN);
        w += SC_LEN;
      }
    }
    txPaused = txPause;
    if (stash) {   // первый кадр пары: пакета звука в этот кадр нет — здесь место служебным пакетам
      if (!txPause) {
        radioSend(NULL, 0);
        // Служебные пакеты (одиночный пакет для старых приёмников, «кого слышу», команды, строки отладки) идут в
        // этом, свободном, кадре пары и только когда драйвер уже отдал в эфир пакет звука (с 2.63). До этого они
        // клались драйверу сразу следом за пакетом звука, «поверх» него. Со встроенным АЦП остановки радио шли в ногу
        // именно с долей таких отправок (64 % — 7 остановок за 3 минуты, 7–10 % — ни одной за 14), а 10.10 радио
        // впервые встало и на входе PCM1808 — при двойных пакетах, когда «поверх» остались только служебные.
        // Драйвер занят дольше 100 мс — отправляем всё равно: команда приёмнику важнее. С 3.30 / 4.00, когда служебное ждёт
        // (txAuxWant), — уже через 16 мс: на «найвищій» со стерео пакет звука уходит в эфир в среднем 2,75 мс из 4, и
        // в этот кадр драйвер почти всегда ещё занят, а насос (peersPump) брал ещё и каждый пятый раз — копия команды
        // уходила раз в ~0,5 с, настройки приёмникам опаздывали на секунды и терялись. Доля отправок «поверх» при этом
        // мала: команды ждут эфира лишь доли секунды после нажатия.
        static uint8_t auxSkip;
        if (rPut == rGot || ++auxSkip >= (txAuxWant ? 4 : 25)) {
          auxSkip = 0;
          if (bcnLen) {
            radioSend(bcn, bcnLen);
            bcnLen = 0;
          } else if (txAfterSend) txAfterSend();
        }
      } else rLastDoneMs = millis();
    } else if (!txPause) {
      static uint8_t run;   // сколько пакетов подряд пропущено
      // Очередь драйвера. Поправка ofs — на случай, если счёт всё же разойдётся с делом (ответ драйвера потерялся,
      // счётчики обнулили при не ушедшем пакете): настоящая очередь за секунду хоть раз да пустеет, поэтому
      // наименьшее за секунду значение считаем нулём.
      static int32_t ofs, winLow = INT32_MAX;
      static uint16_t winCnt;
      int32_t raw = (int32_t)(rPut - rGot);
      if (raw < ofs) ofs = raw;
      if (raw < winLow) winLow = raw;
      if (++winCnt >= 500) {
        if (winLow > ofs) ofs = winLow;
        winLow = INT32_MAX;
        winCnt = 0;
      }
      // двойной пакет несёт копии двух прошлых пакетов, но нарочно пропускаем не больше одного из двух: запас от
      // потерь остаётся. То же на нижней ступени «самой» скорости (1 Мбит/с): туда передатчик ушёл ради слабого
      // приёмника, а у него пропуск двух пакетов подряд оставляет каждый кадр на единственном пакете (с 2.74).
      int copies = dbl || cfg.rateIdx == R_1M ? 1 : sub ? 2 : qCopies(q, st), pend = raw - ofs;
      if (pend > rPendMax) rPendMax = pend;
      uint32_t fus = qFrameUs(q) * (dbl ? 2 : 1);
      int thinAt = TX_THIN_US / fus < 3 ? 3 : TX_THIN_US / fus, hardAt = TX_HARD_US / fus < 5 ? 5 : TX_HARD_US / fus;
      bool send = true;
      if (txThinOn) {
        if (pend >= hardAt) send = false;                        // этот звук уже опоздал бы
        else if (pend >= thinAt && run < copies) send = false;   // затор: один пакет из (копий + 1)
      }
      if (send) {
        if (run) h->flags |= FLAG_THIN;
        run = 0;
        TXP(3)
        secSeal(SEC_AUDIO, h->boot, h->seq, NULL, pkt, sizeof(Hdr), pkt + sizeof(Hdr), (w - pkt) - sizeof(Hdr), w);   // шифр и подпись
        TXP(4)
        w += SEC_TAG;
        if (secShadow & 1) secShadowRun(pkt + sizeof(Hdr), (w - pkt) - sizeof(Hdr) - SEC_TAG);
        radioSend(pkt, w - pkt);
        TXP(5)
        if (dbl) {   // три раза в секунду — ещё и обычный одиночный пакет со вторым кадром пары (см. Q_DOUBLE в proto.h).
          // Чаще, чем раз в 0,6 с: приёмник, 0,6 с не слышавший «своих» пакетов, уходит искать передатчик по каналам.
          static uint16_t bc;
          if (++bc >= 75) {
            bc = 0;
            Hdr *b = (Hdr *)bcn;
            *b = *h;
            b->seq = mySeq;
            b->q = h->q & ~Q_DOUBLE;
            uint8_t *v = bcn + sizeof(Hdr);
            memcpy(v, src, d.n * 2);
            v += d.n * 2;
            if (st) {
              memcpy(v, blkS, bl);
              v += bl;
            }
            for (int k = 0; k < qCopies(q, st); k++, v += bl) memcpy(v, hist[k], bl);
            bcnLen = secSeal(SEC_AUDIO, b->boot, b->seq, NULL, bcn, sizeof(Hdr), bcn + sizeof(Hdr), (v - bcn) - sizeof(Hdr), v) ? (uint16_t)((v - bcn) + SEC_TAG) : 0;   // уйдёт в следующем свободном кадре
          }
        } else {
          bcnLen = 0;
          if (txAfterSend) txAfterSend();   // одиночные пакеты: служебные — как раньше, следом за пакетом звука
        }
      } else {
        radioSend(NULL, 0);   // пакета нет, но сторож радио должен видеть и это время (см. radioSend)
        if (run < copies) txThinned = txThinned + 1;
        else {
          txSkipped = txSkipped + 1;
          txSkipMs = millis() ? millis() : 1;
        }
        if (run < 255) run++;
        // одиночный пропуск стерео не трогает; четыре за четверть секунды — затор, стерео сводится в моно
        static uint32_t winAt;
        static uint8_t winN;
        uint32_t nowMs = millis();
        if (nowMs - winAt > 250) {
          winAt = nowMs;
          winN = 0;
        }
        if (++winN >= 4) txThinMs = nowMs ? nowMs : 1;
      }
    } else rLastDoneMs = millis();   // сторож радио во время поиска канала молчит
    if (!stash) pairHave = false;     // пара ушла (или пропущена) — следующий кадр снова первый
    for (int k = DBL_COPIES; k >= 1; k--) {   // копии сдвигаются: N−1 становится N−2 и так далее
      memcpy(hist[k], hist[k - 1], bl);
      memcpy(scHist[k], scHist[k - 1], SC_LEN);
    }
    memcpy(hist[0], blk, bl);
    memcpy(scHist[0], scNow, SC_LEN);
    if (d.pcm) {   // и несжатые кадры — тем же порядком (нужны только «найвищій»: у остальных копия и есть сам кадр)
      for (int k = DUP_PCM; k >= 1; k--) memcpy(pcmHist[k], pcmHist[k - 1], FRAME * 2);
      memcpy(pcmHist[0], src, d.n * 2);
      if (st) {   // и разность без сжатия — для её копий (с 2.87)
        for (int k = DUP_PCM; k >= 1; k--) memcpy(pcmHistS[k], pcmHistS[k - 1], FRAME * 2);
        memcpy(pcmHistS[0], srcS, d.n * 2);
      }
    }
    txFrames = txFrames + 1;
  }
}

static bool txQuietToSave(uint32_t dueMs, uint32_t maxMs) {
  uint32_t now = millis();
  if ((int32_t)(now - dueMs) < 0) return false;   // срок ещё не пришёл
  if (now - dueMs >= maxMs) return true;          // ждали паузы слишком долго
  return txPause || !txLoudMs || msSince(txLoudMs, now) > 150;
}
