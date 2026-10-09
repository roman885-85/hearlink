// Передатчик: вход звука → пакеты.
//
// Темп задаёт сам вход: встроенный АЦП снимает 64 000 отсчётов в секунду, пары складываются — получается
// 32 000. Звук идёт кусками по 64 отсчёта (2 мс); пакет уходит, когда набран кадр выбранного качества
// (от одного куска до шести). Пробный тон подменяет только содержимое, темп остаётся от АЦП.
#pragma once
#include "radio.h"
#include "esp_adc/adc_continuous.h"
#include "driver/i2s_std.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "soc/gpio_sig_map.h"
#include "esp_rom_gpio.h"
#include "assets.h"
#include "txmp3.h"

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
// Внешний АЦП PCM1808 по I2S. Написано по описанию микросхемы; на модуле не проверено.
static bool txI2sBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 4;
  cc.dma_frame_num = FRAME;
  if (i2s_new_channel(&cc, NULL, &txI2s) != ESP_OK) return false;
  i2s_std_config_t sc = {};
  sc.clk_cfg.sample_rate_hz = SRATE;
  sc.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
  sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  bool m = cfg.board == BOARD_4848;
  txI2sProbe = txI2sNoMclk == I2S_NO_MCLK;
  txI2sNoMclk = 0;
  sc.gpio_cfg.mclk = txI2sProbe ? I2S_GPIO_UNUSED : (gpio_num_t)(m ? PIN_I2S_MCLK_4848 : PIN_I2S_MCLK);
  sc.gpio_cfg.bclk = (gpio_num_t)(m ? PIN_I2S_BCK_4848 : PIN_I2S_BCK);
  sc.gpio_cfg.ws = (gpio_num_t)(m ? PIN_I2S_WS_4848 : PIN_I2S_WS);
  sc.gpio_cfg.dout = I2S_GPIO_UNUSED;
  sc.gpio_cfg.din = (gpio_num_t)(m ? PIN_I2S_DIN_4848 : PIN_I2S_DIN);
  if (i2s_channel_init_std_mode(txI2s, &sc) != ESP_OK) return false;
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
    txMclkOnPin = true;
  } else {
    uart_set_pin(UART_NUM_0, PIN_I2S_MCLK_4848, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
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

// Внешний АЦП даёт два канала: в pcm — середина (Л+П)/2, в sd — разность (Л−П)/2.
static bool txReadI2s(int16_t *pcm, int16_t *sd) {
  static int32_t raw[FRAME * 2];
  size_t got = 0;
  if (i2s_channel_read(txI2s, raw, sizeof(raw), &got, 50) != ESP_OK || got != sizeof(raw)) return false;
  for (int i = 0; i < FRAME; i++) {
    int l = raw[2 * i] >> 16, r = raw[2 * i + 1] >> 16;
    pcm[i] = (int16_t)((l + r) / 2);
    sd[i] = (int16_t)((l - r) / 2);
  }
  txRawSamples = txRawSamples + FRAME;
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
static void txCardSound(int16_t *pcm, int16_t *sd) {
  static float g = 0;
  for (int i = 0; i < FRAME; i++) {
    int16_t l = 0, r = 0;
    int k = mp3Get(l, r);
    if (k < 0) {                     // файл кончился (или не читается)
      for (; i < FRAME; i++) pcm[i] = sd[i] = 0;
      g = 0;
      fm.playEnd = cpErr ? 3 : 1;
      txCardOn = false;
      return;
    }
    if (k > 0) cpPlayed = cpPlayed + 1;
    float d = (txCardStop ? 0.0f : 1.0f) - g;
    g += d > 1.0f / 640 ? 1.0f / 640 : d < -1.0f / 640 ? -1.0f / 640 : d;
    pcm[i] = (int16_t)((l + r) * 0.5f * g);
    sd[i] = (int16_t)((l - r) * 0.5f * g);
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
// Запись настроек во флеш выключает кэш на 8–120 мс (долго — когда NVS стирает страницу), и звук со входа за это время
// теряется: замер 09.10 — в секунду записи в эфир ушло 438 пакетов из 500, у приёмника «порожньо 105 мс». Поэтому
// несрочную запись откладываем до паузы в звуке: 0,15 с в эфире тише −46 дБ (или передача стоит). Дольше maxMs не
// ждём — запишем как есть: настройка не должна пропасть, если питание выключат.
static volatile uint32_t txLoudMs;            // когда в эфир последний раз шёл звук громче −46 дБ (0 — ещё не было)
static bool txQuietToSave(uint32_t dueMs, uint32_t maxMs);
static volatile bool txEqOnTone;              // проба (порт: e9=1): эквалайзер входа действует и на проверочный тон 1 кГц — для замера
static volatile uint32_t txEqGen = 1;         // растёт при смене эквалайзера входа — задача передачи пересчитывает звенья
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
  static uint8_t hist[Q_MAX_COPIES][3 + Q_MAX_N / 2];   // сжатые кадры N−1 и N−2
  static uint8_t blk[3 + Q_MAX_N / 2];
  static uint8_t blkS[3 + Q_MAX_N / 2];
  static Eq6 inEq;                                // эквалайзер входа (см. eq.h)
  uint32_t inEqGen = 0;
  static uint8_t scNow[SC_LEN], scHist[SC_LEN];   // копия разности: этого кадра и прошлого
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
  for (;;) {
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
    else if (cfg.gainDb || inEq.any) {   // эквалайзер входа, затем подсиление — с упором в край шкалы
      static int8_t lastDb = 0;
      static float k = 1;
      if (lastDb != cfg.gainDb) {
        lastDb = cfg.gainDb;
        k = powf(10.0f, lastDb / 20.0f);
      }
      bool eqS = inEq.any && cfg.input == IN_I2S;   // разность каналов есть только у внешнего АЦП
      for (int i = 0; i < FRAME; i++) {
        float v = pcm[i], u = side[i];
        if (inEq.any) v = inEq.run(0, v);
        if (eqS) u = inEq.run(1, u);
        v *= k;
        u *= k;
        pcm[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
        side[i] = u > 32767 ? 32767 : u < -32768 ? -32768 : (int16_t)u;
      }
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
    // Стерео идёт в эфир, когда оно включено и источник стереофонический: мелодии, проверка каналов, внешний АЦП.
    // Голос в списке с мелодиями идёт тем же видом пакета (разность — нули), чтобы приёмники не перестраивались.
    bool srcSt = card ? cpChans == 2 : cfg.tone ? cfg.tone == TEST_LR || ((cfg.tone == TEST_ALL || cfg.tone == TEST_MUSIC || cfg.tone == TEST_DUCK) && txSoundsOk) : cfg.input == IN_I2S;
    bool wantSt = cfg.stereo && srcSt;
    // качество: выбранное, а если на нынешней скорости радио оно не помещается — ближайшее ниже
    int wantQ = qForRate(cfg.quality, cfg.rateIdx, wantSt);
    wantSt = wantSt && qStereoOk(wantQ);
    if (wantQ != q || wantSt != st) {
      q = wantQ;
      st = wantSt;
      txQNow = q;
      txStNow = st;
      accN = 0;
      accFlags = 0;
      idxCarry = 0;
      idxCarryS = 0;
      memset(hist, 0, sizeof(hist));
    }
    const QDef &d = QDEF[q];
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
    h->magic = MAGIC;
    h->ver = PROTO_VER;
    h->kit = cfg.kit;
    h->boot = boot;
    h->seq = seq++;
    txSeqNow = h->seq;
    h->flags = accFlags | (cfg.channel << 4);
    h->q = q | (st ? Q_STEREO : 0) | (cfg.lang ? Q_LANG_EN : 0);
    accFlags = 0;
    uint8_t *w = pkt + sizeof(Hdr);
    int bl = qBlock(q);
    if (d.pcm) {
      memcpy(w, src, d.n * 2);
      w += d.n * 2;
    } else {
      memcpy(w, blk, bl);
      w += bl;
    }
    if (st) {   // разность каналов — сразу за серединой
      memcpy(w, blkS, bl);
      w += bl;
    }
    for (int k = 0; k < qCopies(q, st); k++, w += bl) memcpy(w, hist[k], bl);
    if (st && q == Q_HI && txSideCopyOn && (seq & 63)) {   // копия разности прошлого кадра; каждый 64-й пакет — прежней длины
      memcpy(w, scHist, SC_LEN);
      w += SC_LEN;
    }
    txPaused = txPause;
    if (!txPause) {
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
      int copies = qCopies(q, st), pend = raw - ofs;
      if (pend > rPendMax) rPendMax = pend;
      uint32_t fus = qFrameUs(q);
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
        if (txAfterSend) txAfterSend();
      } else {
        radioSend(NULL, 0);   // пакета нет, но сторож радио должен видеть и это время (см. radioSend)
        if (run < copies) txThinned = txThinned + 1;
        else txSkipped = txSkipped + 1;
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
    memcpy(hist[1], hist[0], bl);     // копии сдвигаются: N−1 становится N−2
    memcpy(hist[0], blk, bl);
    memcpy(scHist, scNow, SC_LEN);
    txFrames = txFrames + 1;
  }
}

static bool txQuietToSave(uint32_t dueMs, uint32_t maxMs) {
  uint32_t now = millis();
  if ((int32_t)(now - dueMs) < 0) return false;   // срок ещё не пришёл
  if (now - dueMs >= maxMs) return true;          // ждали паузы слишком долго
  return txPause || !txLoudMs || msSince(txLoudMs, now) > 150;
}
