// Набор для слабослышащих: передатчик звука и приёмники с наушниками.
//
// Одна прошивка на все платы. Роль (передатчик или приёмник) и настройки хранятся в плате.
// Порт 115200, команды строкой:
//   ?            — что сейчас настроено
//   t / r        — сделать плату передатчиком / приёмником (перезапуск)
//   k<0–255>     — номер набора           c<1–13> — канал        c0 — приёмник ищет канал сам
//   v<скорость>  — 0.5 1 2 5.5 11 6 9 12 18 24 36 48 54 Мбит/с, v0 — сама      p<2–20> — мощность, дБм
//   g<0–6>       — передатчик: проверочный звук вместо входа (1 тон, 2 голос, 3 голос и музыка, 4 музыка,
//                  5 — музыка, раз в N с уходит в фон под голос, 6 — проверка каналов: гудок слева, два справа)
//   q<0–3>       — передатчик: качество звука (0 найвища, 1 стандартна, 2 мова, 3 дальня)
//   w<0|1>       — передатчик: стерео в эфир (идёт, когда источник стереофонический); приёмник: выход стерео
//                  (левый и правый на двух выводах) вместо моно в противофазе
//   W<0|1>       — передатчик: первому приёмнику на связи — выход «протифаза» / «два канали»
//   V<0–20>      — передатчик: первому приёмнику на связи — громкость
//   X            — передатчик: первому приёмнику на связи — проверка выхода на слух (пять состояний по 6 с,
//                  перед каждым гудки с его номером; то же — «Показати себе» дважды подряд)
//   y<№> <знач.> — передатчик: настройки проверочного звука: 1 — мелодия (0 по очереди), 2 — объявление раз в N с,
//                  3 — музыка под голосом, дБ, 4 — пауза между повторами голоса, с, 5 — громкость музыки, дБ,
//                  6 — громкость голоса, дБ
//   a<0|1>       — передатчик: вход — встроенный АЦП / внешний PCM1808 (перезапуск)
//   o<0|1>       — приёмник: выход — PDM / внешний PCM5102 (перезапуск)
//   d<0|4–40>    — приёмник: запас, мс (0 — сам) l<0–20> — громкость    m — тишина вкл/выкл
//   m            — передатчик: тишина в эфире вкл/выкл (пакеты идут, звука в них нет)
//   U<0–5>       — передатчик: открыть вкладку меню (для замеров времени кадра на каждой)
//   u            — проверка экрана: приёмник — экран, ручка, кнопки; передатчик — экран, сенсор, время кадра, карта
//   s            — передатчик: найти свободный канал (звук прервётся на две секунды)
//   H            — передатчик на модуле 4848S040: проверка свободных выводов IO1, IO2, IO40 (25 с)
//   P            — передатчик: список приёмников;  I — первому из них «покажи себя»;  E<0|1> — выключить/включить его;
//                  N<имя> — дать ему имя
//   b<0|1>       — плата: 0 — обычная ESP32-S3, 1 — модуль ESP32-4848S040 (перезапуск)
//   K            — защита: есть ли ключ набора, счётчики отвергнутых пакетов;  K0 — забыть ключ (приёмник попросит
//                  доступ заново; передатчик заведёт новый ключ — все приёмники придётся подключить снова)
//   J / J1 / J0  — передатчик: просьбы приёмников о доступе / открыть окно добавления на 3 минуты / закрыть
//   j<№>         — передатчик: дозволить доступ приёмнику из списка просьб
//   R<номер>     — передатчик: удалить приёмник из набора (шесть знаков номера, например R4C3B10): он теряет доступ,
//                  ключ набора меняется и сам уходит оставшимся приёмникам
//   O            — передатчик: выключить (то же, что кнопка в шапке); включает касание экрана
//   h<0|1|2|3>   — передатчик: сам уходит с занятого канала — выключить / включить; h2 — искать и перейти сейчас;
//                  h3 — шесть ближайших секунд считать «плохими» (проверка самого счёта затора)
//   L<0|1>       — язык надписей: 0 — українська, 1 — English (ставится на передатчике, приёмники берут его сами)
//   S<имя>       — передатчик: назвать источник звука (надпись на кнопке вместо «З пульта»); S без имени — вернуть
//   D            — передатчик: что с картой памяти (отвечает ли, объём, разметка)
//   B            — самопроверка шифра и замер его времени на плате
//   M / M1 / M0  — передатчик: обновление прошивки приёмников по радио (ota.h) — состояние / начать / отменить
//   Q            — передатчик: «чёрный ящик» радио (bbox.h) — счётчики, последний слепок, события этой минуты;
//                  Q0…Q3 — до какой ступени оживлять вставшее радио, Q7/Q8/Q9/Q6 — проверить ступени на здоровом
//   Ut<№> / Uk   — передатчик: «нажать» кнопку с этим номером (49 — клавиатура имени источника) / что набрано на клавиатуре
//   Ux…          — передатчик: «касание» экрана без пальца: Ux<x>,<y>,<мс>[,<сдвиг>] или серия
//                  Ux<пауза мс>:<x>,<y>,<мс>;<x>,<y>,<мс>;…  (проверка клавиатуры: скорость, уход пальца при отрыве)
//   G1 / G0 / G  — передатчик: отладка по радио; «@884A94 ?» — команда другому устройству набора
#include "soc/gpio_struct.h"   // для проверки настройки вывода кнопки (команда n8)
#include "hal/gpio_ll.h"       // сброс прерываний выводов при запуске
#include "driver/gpio.h"
#include <Preferences.h>
#include "config.h"

Settings cfg;
static Preferences prefs;
void settingsSave();
void settingsFactory();

#include "dbgtee.h"   // с этого места Serial — порт «с ответвлением» (отладка по радио)
#include "radio.h"
#include "eq.h"       // эквалайзер — общий для входа передатчика и для приёмника
#include "txaudio.h"
#include "txpeers.h"
#include "rxaudio.h"
#include "rxlink.h"
#include "ota.h"      // обновление прошивки приёмников по радио
#include "ui_rx.h"
#include "dbgair.h"
#include "ui_tx.h"

void settingsSave() {
  uint32_t t0 = millis();
  prefs.putBytes("cfg", &cfg, sizeof(cfg));
  bbMark(BB_NVS, 0, millis() - t0);
}

// Радио не оживает (см. radioSend в radio.h): перезапустить плату без заставки и замера каналов — эфир вернётся
// через секунды. Признак «быстрый запуск» переживает перезапуск в памяти часов.
#define QUICK_BOOT 0x51424F4Fu
RTC_NOINIT_ATTR static uint32_t quickBootMagic;
RTC_NOINIT_ATTR static uint32_t quickBootCount;   // сколько таких перезапусков подряд (с включения питания)
// Метка прошивки набора внутри образа: по ней передатчик узнаёт «свой» файл на карте и его версию (upd.h).
static const char FW_TAG[] = FW_TAG_KEY FW_VERSION;
// Передатчик обновил сам себя с карты: перезапуск без заставки, после запуска — сообщение на экране.
#define UPDATED_BOOT 0x55504454u
RTC_NOINIT_ATTR static uint32_t updatedMagic;
static void updRestart() {
  settingsSave();
  quickBootMagic = QUICK_BOOT;
  updatedMagic = UPDATED_BOOT;
  ESP.restart();
}
static void radioDead() {
  bbRefresh();
  quickBootMagic = QUICK_BOOT;
  if (quickBoot && millis() < 15000) {   // только что так перезапускались, а радио опять стоит — сброс поглубже:
    esp_sleep_enable_timer_wakeup(100000);   // сон на 0,1 с снимает питание со всей цифровой части, кроме часов
    esp_deep_sleep_start();
  }
  ESP.restart();
}

void settingsFactory() {   // всё стереть и начать с начальных настроек (роль и вид платы сохраняются)
  bool isTx = cfg.isTx;
  uint8_t board = cfg.board;
  cfg = Settings();
  cfg.isTx = isTx;
  cfg.board = board;
  settingsSave();
}

static void settingsLoad() {
  prefs.begin("hearlink", false);
  size_t len = prefs.getBytesLength("cfg");   // запись от старой версии короче — новые поля остаются по умолчанию
  bool oldVolScale = len && len <= offsetof(Settings, volScale);   // записана до 2.40: шкала громкости была другой
  if (len && len <= sizeof(cfg)) prefs.getBytes("cfg", &cfg, len);
  else if (len && len <= 1024) {   // запись длиннее — её оставила более новая прошивка (вернулись на прежнюю): взять своё начало.
    uint8_t *tmp = (uint8_t *)malloc(len);   // До 2.32 такая запись не читалась вовсе, и возврат стирал все настройки
    if (tmp) {
      if (prefs.getBytes("cfg", tmp, len) == len) memcpy((void *)&cfg, tmp, sizeof(cfg));
      free(tmp);
    }
  }
  if (cfg.channel < 1 || cfg.channel > 13) cfg.channel = 6;
  cfg.name[sizeof(cfg.name) - 1] = 0;   // имена, записанные до 2.19, могли кончаться половиной буквы (ошибка стирания
  cfg.nameLong[sizeof(cfg.nameLong) - 1] = 0;   // на клавиатуре передатчика) — обрывки убрать
  cfg.srcName[sizeof(cfg.srcName) - 1] = 0;
  utf8Clean(cfg.name);
  utf8Clean(cfg.nameLong);
  utf8Clean(cfg.srcName);
  if (cfg.rateIdx >= N_RATES) cfg.rateIdx = 2;
  if ((cfg.depthMs && cfg.depthMs < 4) || cfg.depthMs > 40) cfg.depthMs = 0;
  if (cfg.volume > 20) cfg.volume = 16;
  if (cfg.tone >= TEST_COUNT) cfg.tone = 0;
  if (cfg.gainDb < -12 || cfg.gainDb > 24) cfg.gainDb = 0;
  if (cfg.brightness < 10 || cfg.brightness > 100) cfg.brightness = 80;
  if (cfg.dimIdx > 3) cfg.dimIdx = 0;
  if (cfg.quality >= Q_COUNT) cfg.quality = Q_HI;
  if (cfg.testTrack > 7) cfg.testTrack = 0;
  if (cfg.duckS < 10 || cfg.duckS > 240) cfg.duckS = 30;
  if (cfg.duckDb < -40 || cfg.duckDb > -6) cfg.duckDb = -16;
  if (cfg.voiceGapS < 1 || cfg.voiceGapS > 60) cfg.voiceGapS = 2;
  if (cfg.musicDb < -30 || cfg.musicDb > 0) cfg.musicDb = 0;
  if (cfg.voiceDb < -30 || cfg.voiceDb > 0) cfg.voiceDb = 0;
  if (cfg.lastTone < 1 || cfg.lastTone >= TEST_COUNT) cfg.lastTone = TEST_DUCK;
  if (cfg.txMute > 1) cfg.txMute = 0;
  if (cfg.stereo > 1) cfg.stereo = 0;
  if (cfg.rxStereo > 1) cfg.rxStereo = 0;
  if (cfg.rxClarity > 3) cfg.rxClarity = 0;
  if (cfg.rxBalance < -5 || cfg.rxBalance > 5) cfg.rxBalance = 0;
  if (cfg.rxVolMax < 1 || cfg.rxVolMax > 20) cfg.rxVolMax = 20;
  if (cfg.rxView > 2) cfg.rxView = 0;
  if (cfg.rxLed > 3) cfg.rxLed = 2;
  if (cfg.rxLang > 2) cfg.rxLang = 0;
  if (cfg.autoUpd > 1) cfg.autoUpd = 1;
  for (uint8_t &v : cfg.rxEq) if (v > 12) v = 6;
  if (cfg.rxLowCut > 1) cfg.rxLowCut = 1;
  for (uint8_t &v : cfg.txEq) if (v > 12) v = 6;
  if (cfg.txLowCut > 1) cfg.txLowCut = 0;
  if (cfg.rxLock > 3) cfg.rxLock = 0;
  if (cfg.rxBoost > 12) cfg.rxBoost = 0;
  // До 2.40 «звук как есть» был шаг 12 (60 %), с 2.40 — шаг 20 (100 %). Чтобы после обновления приёмник звучал как до
  // него, шаг и предел сдвигаются на 8; всё, что стояло выше 60 % (там было усиление с ограничителем), становится 100 %.
  if (oldVolScale) {
    if (cfg.volume) cfg.volume = cfg.volume + 8 > 20 ? 20 : cfg.volume + 8;
    cfg.rxVolMax = cfg.rxVolMax + 8 > 20 ? 20 : cfg.rxVolMax + 8;
    cfg.volScale = 1;
    prefs.putBytes("cfg", &cfg, sizeof(cfg));
  }
}

// Почему плата запустилась в этот раз: важно отличать включение питания от провала напряжения и сбоя программы.
static const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "увімкнення живлення";
    case ESP_RST_BROWNOUT: return "ПРОСІДАННЯ ЖИВЛЕННЯ";
    case ESP_RST_SW: return "перезапуск програмою";
    case ESP_RST_PANIC: return "ЗБІЙ ПРОГРАМИ";
    case ESP_RST_INT_WDT: return "ЗАВИСАННЯ (сторож переривань)";
    case ESP_RST_TASK_WDT: return "ЗАВИСАННЯ (сторож задач)";
    case ESP_RST_WDT: return "ЗАВИСАННЯ (сторож)";
    case ESP_RST_EXT: return "кнопка або порт";
    case ESP_RST_DEEPSLEEP: return "глибокий перезапуск (радіо не оживало)";
    default: return "інше";
  }
}

// Проверка свободных выводов модуля ESP32-4848S040 (IO1, IO2, IO40 — левый ряд разъёма H1).
// Сначала печатает, не притянуты ли выводы чем-то на самой плате; затем 25 секунд следит за ними:
// кто коснётся вывода проводом, идущим на GND разъёма, — в порт пишется, какой это вывод. Так находится
// порядок выводов L1–L3 без схемы платы. Вывод IO2 занят входом звука — он проверяется по отсчётам АЦП.
static void pinProbe() {
  if (cfg.board != BOARD_4848 || !cfg.isTx) {
    Serial.println("перевірка виводів — лише для передавача на модулі 4848S040");
    return;
  }
  auto adcAvg = []() {
    uint32_t s = 0;
    for (int i = 0; i < 50; i++) {
      s += txRawLast;
      delay(4);
    }
    return s / 50;
  };
  for (int pin : { 1, 40 }) {
    pinMode(pin, INPUT_PULLUP);
    delay(30);
    int up = digitalRead(pin);
    pinMode(pin, INPUT_PULLDOWN);
    delay(30);
    int dn = digitalRead(pin);
    pinMode(pin, INPUT_PULLUP);
    Serial.printf("IO%d: з підтяжкою вгору %d, вниз %d — %s\n", pin, up, dn,
                  up && !dn ? "вільний" : !up ? "щось тягне до землі" : "щось тягне до живлення");
  }
  uint32_t free = adcAvg();
  gpio_pullup_en(GPIO_NUM_2);
  delay(300);
  uint32_t up = adcAvg();
  float v = up * 3.1f / 4095;
  Serial.printf("IO2 (вхід звуку): без підтяжки %u з 4095, з внутрішньою підтяжкою %u (≈%.2f В)", (unsigned)free, (unsigned)up, v);
  if (v > 2.9f) Serial.println(" — вільний");
  else Serial.printf(" — на платі опір до землі ≈ %.0f кОм\n", 45.0f * v / (3.3f - v));
  Serial.println("тепер 25 с: торкайтеся дротом від GND до виводів L1–L3 по черзі…");
  int was1 = 1, was40 = 1, was2 = up > 600;
  for (uint32_t t0 = millis(); millis() - t0 < 25000;) {
    int a = digitalRead(1), b = digitalRead(40), c = adcAvg() > up / 2 && up > 600;
    if (a != was1) Serial.printf("  IO1: %s\n", a ? "відпустили" : "ЗАМКНУЛИ НА ЗЕМЛЮ");
    if (b != was40) Serial.printf("  IO40: %s\n", b ? "відпустили" : "ЗАМКНУЛИ НА ЗЕМЛЮ");
    if (up > 600 && c != was2) Serial.printf("  IO2: %s\n", c ? "відпустили" : "ЗАМКНУЛИ НА ЗЕМЛЮ");
    was1 = a;
    was40 = b;
    was2 = c;
  }
  gpio_pullup_dis(GPIO_NUM_2);
  pinMode(1, INPUT);
  pinMode(40, INPUT);
  Serial.println("перевірку виводів закінчено");
}

static void help() {
  Serial.printf("запуск через: %s; працює %u с\n", resetReason(), (unsigned)(millis() / 1000));
  Serial.printf("версія %s, %s, набір %u, канал %u%s, швидкість %s Мбіт/с%s, потужність %d дБм\n", FW_VERSION,
                cfg.isTx ? "ПЕРЕДАВАЧ" : "ПРИЙМАЧ", cfg.kit, cfg.channel, !cfg.isTx && cfg.autoChannel ? " (пошук сам)" : "",
                RATES[cfg.rateIdx].name, cfg.isTx && cfg.rateAuto ? " (обирається сама)" : "", cfg.powerDbm);
  Serial.printf("плата: %s\n", cfg.board == BOARD_4848 ? "ESP32-4848S040" : "ESP32-S3");
  Serial.printf("захист: ключ набору %s%s\n", secHave ? "є" : "НЕМАЄ", cfg.isTx || secHave ? "" : " — приймач не підключено до набору, просить доступ");
  if (cfg.isTx) {
    static const char *const T[TEST_COUNT] = { "вимкнено", "тон 1000 Гц", "голос", "голос і музика", "музика", "музика з оголошенням", "перевірка каналів (лівий — один гудок, правий — два)" };
    Serial.printf("вхід: %s, перевірочний звук: %s%s\n", cfg.input == IN_I2S ? (txI2sProbe ? "PCM1808 (I2S) — ПРОБА без головного такту" : "PCM1808 (I2S)") : "вбудований АЦП", T[cfg.tone < TEST_COUNT ? cfg.tone : 0],
                  txSoundsOk ? "" : " (звуків у пам'яті немає — замість них тон)");
    Serial.printf("  мелодія: %s; оголошення кожні %u с; музика під голосом %d дБ; повтор голосу через %u с; гучність музики %d дБ, голосу %d дБ\n",
                  cfg.testTrack ? String(cfg.testTrack).c_str() : "по черзі", cfg.duckS, cfg.duckDb, cfg.voiceGapS, cfg.musicDb, cfg.voiceDb);
    if (mpFrames && mpItem >= 0)
      Serial.printf("  зараз мелодія «%s» (%.15s)\n", assetTitles[mpItem][0] ? assetTitles[mpItem] : "без назви", assetTab[mpItem].name);
    if (mpFrames) {
      Serial.printf("мелодії MP3: %u Гц, каналів %u, %u кбіт/с; розбір кадру сер. %u, макс. %u мкс (кадр — 36 мс звуку); недобір %u, помилок %u\n", (unsigned)mpRate,
                    (unsigned)mpChans, (unsigned)mpKbps, (unsigned)(mpUsSum / mpFrames), (unsigned)mpUsMax, (unsigned)mpUnder, (unsigned)mpErrors);
      Serial.printf("  запас розібраного звуку %u мс, пріоритет розбору %u (1 — не заважає екрану, 3 — понад екран)\n", (unsigned)((mpHead - mpTail) * 1000 / SRATE), mpPrio);
      mpUsSum = 0;      // счёт — с прошлого вызова «?»: так видно, что было именно в этот промежуток
      mpFrames = 0;
      mpUsMax = 0;
      mpUnder = 0;
    }
    int q = txQNow < Q_COUNT ? (int)txQNow : qForRate(cfg.quality, cfg.rateIdx);
    bool st = txStNow;
    Serial.printf("якість звуку: %s%s — %u Гц, кадр %u мс, пакет %d байт, в ефірі %u мкс із %u (%u %%)\n", Q_NAME[q],
                  q != cfg.quality ? " (знижено: обрана не вміщається в цю швидкість)" : "", QDEF[q].sr, (unsigned)(qFrameUs(q) / 1000), qPktLen(q, st),
                  (unsigned)airUs(qPktLen(q, st), cfg.rateIdx), (unsigned)qFrameUs(q), (unsigned)(airUs(qPktLen(q, st), cfg.rateIdx) * 100 / qFrameUs(q)));
    Serial.printf("стерео в ефір: %s; зараз в ефірі %s%s\n", cfg.stereo ? "увімкнено" : "вимкнено", st ? "СТЕРЕО" : "моно",
                  cfg.stereo && !st ? " (джерело моно або якість «дальня»)" : "");
  }
  else Serial.printf("ім'я: «%s»%s; вихід: %s, %s, запас %s, гучність %u з 20\n", rxName(), cfg.off ? " (ВИМКНЕНИЙ з передавача)" : "", cfg.output == OUT_I2S ? "PCM5102 (I2S)" : "PDM",
                     cfg.rxStereo ? "ДВА КАНАЛИ (навушники напряму або стерео)" : cfg.output == OUT_I2S ? "моно" : "ПРОТИФАЗА (підсилювач з різницевим входом)",
                     cfg.depthMs ? (String(cfg.depthMs) + " мс").c_str() : "підбирається сам", cfg.volume);
  if (!cfg.isTx) {   // чем громкость оборачивается на деле: шкала 0…100 % растянута на 0…«межа гучності»
    float g = rxVolumeGain(rxVolumeScaled(cfg.volume));
    Serial.printf("гучність %u %% при межі %u %% — звук %s%.1f дБ від повної шкали\n", cfg.volume * 5, cfg.rxVolMax * 5, g > 0 ? "" : "вимкнено, ", g > 0 ? 20 * log10f(g) : 0.0f);
  }
}

// Свободное время ядер: на каждом ядре секунду крутится пустой счётчик с самым низким приоритетом. Чем меньше насчитал,
// тем больше ядро занято остальным (в том числе прерываниями). Числа сравнивать между собой и между режимами.
static volatile uint32_t spinCount[2];
static volatile bool spinRun;
static void spinTask(void *arg) {
  int core = (int)(intptr_t)arg;
  uint32_t n = 0;
  while (spinRun) {
    n = n + 1;
    __asm__ __volatile__("nop");
  }
  spinCount[core] = n;
  vTaskDelete(NULL);
}
static void cpuProbe() {
  spinCount[0] = spinCount[1] = 0;
  spinRun = true;
  xTaskCreatePinnedToCore(spinTask, "spin0", 2048, (void *)0, 1, NULL, 0);
  xTaskCreatePinnedToCore(spinTask, "spin1", 2048, (void *)1, 1, NULL, 1);
  delay(1000);
  spinRun = false;
  delay(60);
  Serial.printf("вільний час ядер за секунду (умовні одиниці, більше — вільніше): перше (радіо) %u, друге (екран) %u\n", (unsigned)(spinCount[0] / 1000),
                (unsigned)(spinCount[1] / 1000));
}

// Сверка записи проверочного узора (Z2): в файле должен идти счётчик без единого разрыва. Разрыв — потерянные,
// повторённые или не туда записанные секторы; печатается, где он и на сколько отсчётов.
static void recVerify(const char *path) {
  static int16_t *buf;   // во внешней памяти
  char full[300];
  if (!buf) buf = (int16_t *)heap_caps_malloc(1024 * 2, MALLOC_CAP_SPIRAM);
  if (!buf) return;
  snprintf(full, sizeof(full), "/sd%s", path);
  FILE *f = fopen(full, "rb");
  WavFmt w;
  if (!f || wavParse(f, w)) {
    Serial.println("звірка: файл не відкрився або це не запис");
    if (f) fclose(f);
    return;
  }
  fseek(f, w.dataPos, SEEK_SET);
  uint32_t total = w.dataLen / 2, n = 0, breaks = 0, at[5], jump[5];
  int16_t prev = 0;
  bool have = false;
  while (n < total) {
    size_t k = fread(buf, 2, total - n > 1024 ? 1024 : total - n, f);
    if (!k) break;
    for (size_t i = 0; i < k; i++) {
      if (have && (int16_t)(prev + 1) != buf[i]) {
        if (breaks < 5) {
          at[breaks] = n + i;
          jump[breaks] = (uint16_t)(buf[i] - prev - 1);
        }
        breaks++;
      }
      prev = buf[i];
      have = true;
    }
    n += k;
  }
  fclose(f);
  Serial.printf("звірка «%s»: дані з %u-го байта, відліків %u з %u (%.1f с), розривів лічильника %u", path, (unsigned)w.dataPos, (unsigned)n, (unsigned)total, n / (float)SRATE,
                (unsigned)breaks);
  for (uint32_t i = 0; i < breaks && i < 5; i++) Serial.printf("; на %u-му відліку стрибок %u", (unsigned)at[i], (unsigned)jump[i]);
  Serial.println();
}

// Приём файла с компьютера на карту через порт (tools/sendfile.py): Fu<путь>|<размер>|<скорость порта>.
// Карта стоит в передатчике, другого пути положить на неё файл без вынимания нет (владелец 07.10: «файл в загрузках …
// скопируй на карту и проверь»). Файл идёт кусками по 2048 байт; у каждого куска заголовок (A5 5A, длина) и
// контрольная сумма; на каждый кусок ответ: 06 'K' — принят, 15 'N' — повторить, 18 'E' — карта не пишет, конец.
// На время передачи порт переходит на заданную скорость (230400: на 460800 мост этого модуля сбоит), главный цикл
// стоит — отчётов в порт нет, журнал на карту и уход с занятого канала на эти минуты не работают.
static uint16_t upCrc(const uint8_t *d, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= (uint16_t)d[i] << 8;
    for (int k = 0; k < 8; k++) c = c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1;
  }
  return c;
}
static void fmUpload(const String &path, uint32_t size, uint32_t baud) {
  const uint32_t BLK = 2048;
  static uint8_t *buf;
  char full[300];
  snprintf(full, sizeof(full), "/sd%s", path.c_str());
  if (!buf) buf = (uint8_t *)heap_caps_malloc(BLK + 8, MALLOC_CAP_SPIRAM);
  if (!recSd || recOn || fm.op || fmWant || !buf || !size) {
    hwSerial.println("UPLOAD FAIL busy");
    return;
  }
  FILE *f = fopen(full, "wb");
  if (!f) {
    hwSerial.println("UPLOAD FAIL open");
    return;
  }
  hwSerial.printf("UPLOAD READY %u\n", (unsigned)BLK);
  hwSerial.flush();
  delay(60);
  hwSerial.end();                     // большой приёмный буфер порта (4 КБ) держим только на время передачи файла
  hwSerial.setRxBufferSize(4096);
  hwSerial.begin(baud);
  uint32_t got = 0, bad = 0;
  bool ok = true;
  sdwOn = true;   // секторы копятся и уходят на карту крупно — как при записи
  hwSerial.setTimeout(6000);
  while (got < size && ok) {
    uint32_t want = size - got > BLK ? BLK : size - got;
    uint8_t h[4];
    bool good = hwSerial.readBytes(h, 4) == 4 && h[0] == 0xA5 && h[1] == 0x5A && (uint32_t)(h[2] | h[3] << 8) == want &&
                hwSerial.readBytes(buf, want + 2) == want + 2 && upCrc(buf, want) == (uint16_t)(buf[want] << 8 | buf[want + 1]);
    if (!good) {
      if (++bad > 20) {
        ok = false;
        break;
      }
      delay(150);   // переждать остаток испорченного куска и выбросить его
      while (hwSerial.available()) hwSerial.read();
      hwSerial.write((uint8_t)0x15);
      hwSerial.write('N');
      continue;
    }
    if (fwrite(buf, 1, want, f) != want) {
      ok = false;
      break;
    }
    got += want;
    hwSerial.write((uint8_t)0x06);
    hwSerial.write('K');
  }
  if (fclose(f)) ok = false;
  sdwOn = false;
  if (!ok) {
    hwSerial.write((uint8_t)0x18);
    hwSerial.write('E');
    unlink(full);
  }
  hwSerial.flush();
  delay(300);
  hwSerial.end();
  hwSerial.setRxBufferSize(256);
  hwSerial.begin(115200);
  hwSerial.setTimeout(1000);
  delay(300);
  hwSerial.printf("\nUPLOAD %s %u з %u байт, повторів %u\n", ok ? "DONE" : "FAIL", (unsigned)got, (unsigned)size, (unsigned)bad);
}

// Файловый менеджер карты — с порта (для проверки без рук). Ждёт конца работы и печатает итог.
static void fmSerial(const String &arg) {
  char c = arg.length() ? arg[0] : 0;
  if (!c) {
    Serial.printf("файл в ефірі: %s «%s» %u / %u с; кінець %u, помилка читання %u; розбір: %u Гц, %u кан., кадрів %u, помилок %u, недоборів %u, сер. %.1f мс, найдовше %.1f мс; читання картки %u мс, перерахунок частоти %u мс, запас %u мс\n",
                  fm.playing ? "ТАК" : "ні", fm.playName, (unsigned)fm.playS, (unsigned)fm.playTotalS, (unsigned)fm.playEnd, (unsigned)cpErr, (unsigned)mpRate, (unsigned)cpChans,
                  (unsigned)mpFrames, (unsigned)mpErrors, (unsigned)mpUnder, mpFrames ? mpUsSum / 1000.0f / mpFrames : 0.0f, mpUsMax / 1000.0f,
                  (unsigned)(cpReadUs / 1000), (unsigned)(cpRsUs / 1000), (unsigned)((mpHead - mpTail) / 32));
    cpReadUs = 0;
    cpRsUs = 0;
    mpFrames = 0;
    mpUsSum = 0;
    mpUsMax = 0;
    return;
  }
  String p = arg.substring(1), q;
  int bar = p.indexOf('|');
  if (bar >= 0) {
    q = p.substring(bar + 1);
    p = p.substring(0, bar);
  }
  if (c == 'u') {   // Fu<путь>|<размер>|<скорость> — принять файл с компьютера (tools/sendfile.py)
    int b2 = q.indexOf('|');
    fmUpload(p, (uint32_t)q.toInt(), b2 >= 0 ? (uint32_t)q.substring(b2 + 1).toInt() : 115200);
    return;
  }
  if (c == 't') {   // Ft<файл> — напечатать текстовый файл с карты целиком (журнал работы после зала)
    char full[300];
    snprintf(full, sizeof(full), "/sd%s", p.c_str());
    FILE *f = fopen(full, "rb");
    if (!f) {
      Serial.println("файл не відкрився");
      return;
    }
    static char buf[513];
    size_t n, total = 0;
    while ((n = fread(buf, 1, 512, f)) > 0) {
      Serial.write((const uint8_t *)buf, n);
      total += n;
    }
    fclose(f);
    Serial.printf("\n— кінець файла, %u байт; рядків журналу записано %u, не записано %u —\n", (unsigned)total, (unsigned)logLines, (unsigned)logFails);
    return;
  }
  int op = c == 'l' ? FM_LIST : c == 'm' ? FM_MKDIR : c == 'd' ? FM_DELETE : c == 'c' ? FM_COPY : c == 'i' ? FM_INFO : c == 'p' ? FM_PLAY : c == 's' ? FM_STOP : 0;
  if (!op) return;
  uint32_t seq = fm.doneSeq, t0 = millis(), shown = 0;
  if (!fmAsk(op, p.c_str(), q.c_str())) {
    Serial.println("картка зайнята або її немає");
    return;
  }
  if (op == FM_PLAY || op == FM_STOP) {
    Serial.println(op == FM_PLAY ? "файл пішов в ефір" : "зупиняю файл");
    return;
  }
  while (fm.doneSeq == seq && millis() - t0 < 300000) {
    delay(20);
    if (op == FM_COPY && millis() - shown > 2000) {
      shown = millis();
      Serial.printf("  копіюю: %u %%\n", (unsigned)fm.progress);
    }
  }
  Serial.printf("картка: дія %d, код %u, %u мс; вільно %u МБ з %u\n", op, (unsigned)fm.result, (unsigned)(millis() - t0), (unsigned)fm.freeMb, (unsigned)fm.totalMb);
  if (op == FM_LIST && !fm.result) {
    for (int i = 0; i < fm.n; i++) Serial.printf("  %s %s  %u\n", fm.ent[i].dir ? "[тека]" : "      ", fm.ent[i].name, (unsigned)fm.ent[i].size);
    Serial.printf("  записів %d, не показано %d\n", fm.n, fm.more);
  }
  if (op == FM_INFO && !fm.result)
    Serial.printf("  «%s»: вид %u, в ефір %s (причина %u), %u Гц, %u кан., %u біт, %u кбіт/с, %u с, %u байт\n", fm.info.name, fm.info.kind, fm.info.playable ? "можна" : "НЕ МОЖНА",
                  fm.info.why, (unsigned)fm.info.rate, fm.info.chans, fm.info.bits, fm.info.kbps, (unsigned)fm.info.durS, (unsigned)fm.info.size);
}

// «Чёрный ящик» радио (bbox.h): последний слепок и — по просьбе — события этой минуты.
static void bbPrint(bool live) {
  char *t = logExtra ? logExtra + 2048 : nullptr;   // вторая половина той же памяти: первая может ждать записи на карту
  if (!t) return;
  Serial.printf("радіо: ставало %u разів, оживлено без перезапуску плати %u, швидких перезапусків плати %u; оживляти до ступеня %u, починати з %u\n",
                (unsigned)bbDeaths, (unsigned)rRevived, (unsigned)quickBootCount, rReviveMax, rReviveFrom);
  if (bbSnapMagic == BB_MAGIC) {
    bbFormat(bbSnap, t, 2048, false);
    Serial.print(t);
  }
  if (live) {
    bbFormat(bbSnap, t, 2048, true);
    Serial.print("останні події:");
    Serial.print(t);
  }
}

static void command(String s) {
  s.trim();
  if (!s.length()) return;
  if (s[0] == '@') {   // команда другому устройству набора — по радио (dbgair.h)
    dbgRemote(s);
    return;
  }
  char c = s[0];
  String arg = s.substring(1);
  arg.trim();
  long v = arg.toInt();
  bool restart = false;
  if (cfg.isTx) bbMark(BB_CMD, (uint8_t)c, (uint32_t)(v < 0 ? 0 : v));
  switch (c) {
    case '?': help(); return;
    case 't':
      if (!cfg.isTx && cfg.autoChannel) cfg.channel = 6;   // канал, на котором приёмник застал поиск, передатчику не нужен
      cfg.isTx = true;                                     // (уже передатчик — канал не трогать: 07.10 так сбил канал владельца)
      restart = true;
      break;
    case 'r': cfg.isTx = false; restart = true; break;
    case 'k':
      if (v < 0 || v > 255) return;
      cfg.kit = v;
      rxNeedPrime = true;
      break;
    case 'c':
      if (v < 0 || v > 13 || (v == 0 && cfg.isTx)) return;
      cfg.autoChannel = v == 0;
      if (v) cfg.channel = v;
      rApply = true;
      break;
    case 'v': {   // v0 — скорость выбирается сама
      int i = 0;
      while (i < N_RATES && arg != RATES[i].name) i++;
      if (arg == "0") cfg.rateAuto = 1;
      else if (i == N_RATES) return;
      else {
        cfg.rateAuto = 0;
        cfg.rateIdx = i;
        rApply = true;
      }
      break;
    }
    case 'w':
      if (cfg.isTx) cfg.stereo = v ? 1 : 0;
      else cfg.rxStereo = v ? 1 : 0;
      break;
    case 'y': {
      int sp = arg.indexOf(' ');
      if (sp < 0) return;
      long x = arg.substring(sp + 1).toInt();
      switch (v) {
        case 1: cfg.testTrack = constrain(x, 0, 7); break;
        case 2: cfg.duckS = constrain(x, 10, 240); break;
        case 3: cfg.duckDb = constrain(x, -40, -6); break;
        case 4: cfg.voiceGapS = constrain(x, 1, 60); break;
        case 5: cfg.musicDb = constrain(x, -30, 0); break;
        case 6: cfg.voiceDb = constrain(x, -30, 0); break;
        default: return;
      }
      break;
    }
    case 'q':   // якість звуку: 0 найвища … 3 дальня
      if (v < 0 || v >= Q_COUNT) return;
      cfg.quality = v;
      break;
    case 'p':
      if (v < 2 || v > 20) return;
      cfg.powerDbm = v;
      rApply = true;
      break;
    case 'g':   // 0 вход, 1 тон, 2 голос, 3 голос и музыка, 4 музыка, 5 музыка с объявлением
      if (v < 0 || v >= TEST_COUNT) return;
      cfg.tone = v;
      if (v) cfg.lastTone = v;
      break;
    case 'U':   // передатчик: открыть вкладку меню (0 головна … 5 довідка) — для замеров без рук
      if (cfg.isTx && arg.length() && arg[0] == 't') txscreen::tapAsk = arg.substring(1).toInt();   // Ut<номер> — «нажать» кнопку
      else if (cfg.isTx && arg.length() && arg[0] == 'q') txscreen::fmStateAsk = true;                // Uq — что на экране карты
      else if (cfg.isTx && arg.length() && arg[0] == 'k') txscreen::kbReport();                       // Uk — что набрано на клавиатуре
      else if (cfg.isTx && arg.length() && arg[0] == 'x' && !fakeAsk) {   // «касания» экрана без пальца (проверка клавиатуры):
        // Ux<x>,<y>,<мс>[,<сдвиг>] — одно; Ux<пауза>:<x>,<y>,<мс>[,<сдвиг>];<x>,<y>,<мс>;… — несколько подряд
        const char *c = arg.c_str() + 1;
        const char *colon = strchr(c, ':');
        fakeGap = colon ? atoi(c) : 50;
        if (colon) c = colon + 1;
        int n = 0;
        while (*c && n < 24) {
          int x = 0, y = 0, ms = 80, dx = 0;
          if (sscanf(c, "%d,%d,%d,%d", &x, &y, &ms, &dx) < 2) break;
          fakeTap[n++] = FakeTap{ (int16_t)x, (int16_t)y, (int16_t)dx, (uint16_t)(ms < 8 ? 8 : ms > 5000 ? 5000 : ms) };
          c = strchr(c, ';');
          if (!c) break;
          c++;
        }
        fakeN = n;
        fakeAsk = n > 0;
      } else if (cfg.isTx && v >= 0 && (v < ui::PG_N || v == 9)) txscreen::pageAsk = v;   // U<0–5> — открыть вкладку, U9 — карту
      return;
    case 'b': cfg.board = v ? BOARD_4848 : BOARD_DEVKIT; restart = true; break;
    case 'a':   // a0 — встроенный вход, a1 — PCM1808; a2 — проба: PCM1808 на один запуск без главного такта (порт работает)
      cfg.input = v ? IN_I2S : IN_ADC;
      txI2sNoMclk = v == 2 ? I2S_NO_MCLK : 0;
      restart = true;
      break;
    case 'o': cfg.output = v ? OUT_I2S : OUT_PDM; restart = true; break;
    case 'd':
      if (v != 0 && (v < 4 || v > 40)) return;
      cfg.depthMs = v;
      break;
    case 'l':
      if (v < 0 || v > 20) return;
      cfg.volume = v;
      break;
    case 'm':   // приёмник: тишина в наушниках; передатчик: тишина в эфире
      if (cfg.isTx) {
        txMute = !txMute;
        cfg.txMute = txMute;
        settingsSave();
        Serial.printf("тиша в ефірі: %s\n", txMute ? "УВІМКНЕНО" : "вимкнено");
      } else rxMute = !rxMute;
      return;
    case 's':
      if (cfg.isTx) scanStart();
      return;
    case 'z':   // передатчик: замолчать в эфире совсем (z1) и вернуться (z0) — проверка ожидания приёмников; не запоминается
      if (cfg.isTx) {
        txHold = arg.length() ? v != 0 : !txHold;
        Serial.printf("передача %s\n", txHold ? "ЗУПИНЕНА (z0 — повернути)" : "йде");
      }
      return;
    case 'K':   // защита: состояние; K0 — забыть ключ
      if (arg == "0") {
        if (cfg.isTx) {
          secNewKey();
          Serial.println("створено НОВИЙ ключ набору: усі приймачі треба додати заново");
        } else {
          secForget();
          Serial.println("ключ набору забуто: приймач проситиме доступ");
        }
      }
      Serial.printf("захист: ключ набору %s (покоління %u), епоха %u; відкинуто: з чужим підписом %u, старих (повторів) %u; шифр апаратний AES-128\n",
                    secHave ? "Є" : "НЕМАЄ", (unsigned)secGen, (unsigned)secEpoch, (unsigned)secBadTag, (unsigned)secReplay);
      if (!cfg.isTx) Serial.printf("  особистий ключ приймача %s; новий ключ набору просив %u, отримав %u разів\n", secDev.ok ? "є" : "НЕМАЄ", (unsigned)rxKeyAsked,
                                   (unsigned)rxKeyTaken);
      Serial.printf("пам'ять: вільно %u байт (найменше з запуску %u), найбільший шматок %u; апаратний AES додатково: звернень %u, помилок %u\n",
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)secHwRuns, (unsigned)secHwFails);
      if (cfg.isTx) pairPrint();
      else if (!secHave) Serial.printf("  прошу доступ: код %04u, запитів надіслано %u, передавач %s\n", (unsigned)rxPairCode, (unsigned)rxPairAsked,
                                       msSince(rLastAnyMs) < 1000 && rLastAnyMs ? "чути" : "не чути");
      return;
    case 'J':
      if (!cfg.isTx) return;
      if (arg.length()) pairOpen(v != 0);
      pairPrint();
      return;
    case 'j':
      if (cfg.isTx && v >= 1 && v <= PAIR_ASKS) pairApproveAsk = v - 1;
      return;
    case 'R':   // передатчик: удалить приёмник из набора — R<шесть знаков номера>, например R4C3B10
      if (cfg.isTx && arg.length() == 6) {
        uint8_t id[3];
        for (int i = 0; i < 3; i++) id[i] = (uint8_t)strtol(arg.substring(i * 2, i * 2 + 2).c_str(), NULL, 16);
        int i = peerFind(id);
        if (i >= 0) peerRemove(i);
        else Serial.println("такого приймача в наборі немає");
      }
      return;
    case 'B': secSelfTest(); return;
    case 'D':   // передатчик: что с картой памяти; D<число> — скорость обмена с картой, МГц (D4, D8, D16, D20), карта подключается заново
      if (cfg.isTx && cfg.board == BOARD_4848 && arg.length() && v >= 1 && v <= 40) {
        recSpiHz = (uint32_t)v * 1000000;
        recRemount = true;
        Serial.printf("картка: обмін тепер %ld МГц\n", v);
        return;
      }
      if (cfg.isTx && cfg.board == BOARD_4848) {
        Serial.printf("запис: обмін %u МГц; записів куска %u, найдовший %u мс; втрачено відліків %u; збоїв запису %u (останній: на %u-му байті, errno %d, тривав %u мс)\n",
                      (unsigned)(recSpiHz / 1000000), (unsigned)recWrN, (unsigned)recWrMaxMs, (unsigned)recLost, (unsigned)recFails, (unsigned)recFailAt, recFailErrno,
                      (unsigned)recFailMs);
        Serial.printf("картка: кластер %u КБ; звернень на запис %u, скидань накопичувача %u, повторів після заминки %u, найдовше чекання %u мс, здався %u разів\n",
                      (unsigned)recClusterKb, (unsigned)sdOps, (unsigned)sdwFlushes, (unsigned)sdRetries, (unsigned)sdSettleMaxMs, (unsigned)sdGiveUps);
        Serial.printf("  по одному сектору: %u разів, сер. %.1f мс; по кілька: %u разів, секторів %u, сер. %.1f мс на звернення; найдовше %.0f мс\n", (unsigned)sdN1,
                      sdN1 ? sdUs1 / 1000.0f / sdN1 : 0.0f, (unsigned)sdNm, (unsigned)sdSecM, sdNm ? sdUsM / 1000.0f / sdNm : 0.0f, sdMaxUs / 1000.0f);
        sdN1 = sdUs1 = sdNm = sdSecM = sdUsM = sdMaxUs = 0;
        static const char *const ST[] = { "картка не відповідає (її немає або немає контакту)", "підключена", "відповідає, але розмітка не та",
                                          "відповідає, але не запускається" };
        if (recCard == CARD_OK)
          Serial.printf("  %s; внутрішньої пам'яті до підключення картки %u КБ, після %u КБ\n", recOn ? "іде запис" : recFull ? "місця немає" : "запису немає",
                        (unsigned)(recMemBefore / 1024), (unsigned)(recMemAfter / 1024));
        Serial.printf("картка пам'яті: %s; обсяг %u МБ%s%s; вільно %u МБ%s\n", ST[recCard & 3], (unsigned)recCardMb, recCardFs[0] ? ", розмітка " : "", recCardFs,
                      (unsigned)recFreeMb, recCard == CARD_BAD_FS ? " — потрібна FAT32 (exFAT ця прошивка не читає)" : "");
        if (recCard != CARD_OK)
          Serial.printf("  відповідь на скидання: 0x%02X (0x01 — картка є, 0xFF — мовчить); на другий запит: 0x%02X [%02X %02X %02X %02X]; рівні виводів зараз: 41=%d 42=%d 47=%d 48=%d\n",
                        recRaw0, recRaw8, recRaw7[0], recRaw7[1], recRaw7[2], recRaw7[3], digitalRead(41), digitalRead(42), digitalRead(47), digitalRead(48));
        if (recCard != CARD_OK)
          Serial.printf("  вручну (без блока SPI): відповідь 0x%02X [%02X %02X %02X %02X %02X %02X %02X %02X]; такт 48: вгору %s, вниз %s; дані 47: вгору %s, вниз %s; лінія від картки 41 з підтяжкою вниз: у спокої %d, при вибраній картці %d; блок SPI %s\n",
                        recBbR1, recBbResp[0], recBbResp[1], recBbResp[2], recBbResp[3], recBbResp[4], recBbResp[5], recBbResp[6], recBbResp[7],
                        (recBbPins & 1) ? "так" : "НІ", (recBbPins & 2) ? "так" : "НІ", (recBbPins & 4) ? "так" : "НІ", (recBbPins & 8) ? "так" : "НІ",
                        (recBbPins >> 4) & 1, (recBbPins >> 5) & 1, recSpiOk ? "запущено" : "НЕ ЗАПУСТИВСЯ");
        recBbAsk = true;
        recProbeAsk = true;
      }
      return;
    case 'G':   // передатчик: отладка по радио — G1 включить, G0 выключить (то же — «Налашт.» › «Налагодження ефіром»); G — состояние
      if (cfg.isTx && arg.length()) {
        cfg.dbgAir = v ? 1 : 0;
        settingsSave();
        if (cfg.board == BOARD_4848) ui::needFull = true;
      }
      Serial.printf("налагодження ефіром: %s; надіслано рядків %u, почуто чужих %u, не вмістилось %u\n", dbgAirOn() ? "УВІМКНЕНО" : "вимкнено", (unsigned)dbgSent,
                    (unsigned)dbgHeard, (unsigned)dbgDropped);
      return;
    case 'Y':   // передатчик: Y1 — вывод кадра экрана «по-старому» (для сравнения), Y0 — свой; плата перезапускается
      if (cfg.isTx && v >= 5 && v <= 8) {   // Y5…Y8 — опыт: способ подготовки порции кадра в движении (0…3)
        lcdSlideMode = v - 5;
        Serial.printf("вивід кадру в русі: спосіб %u\n", lcdSlideMode);
        return;
      }
      if (cfg.isTx && v == 2) {   // Y2 — сколько свободного времени на каждом ядре: секунду крутим пустой счётчик на обоих
        cpuProbe();
        return;
      }
      if (cfg.isTx && (v == 3 || v == 4)) {   // Y3 — выключить подгрузку порций кадра в кэш (для сравнения), Y4 — включить; затем замер ядер
        lcdNoPreload = v == 3;
        delay(300);
        cpuProbe();
        return;
      }
      if (cfg.isTx && cfg.board == BOARD_4848 && v == 9) {   // Y9 — опыт: нарочно сбить драйвер экрана с очереди буферов (panel4848.h)
        lcdProvokeFlip();
        return;
      }
      if (cfg.isTx && cfg.board == BOARD_4848 && (v == 10 || v == 11)) {   // Y10 — класть порции, куда велит драйвер (как до 2.49), Y11 — по-своему
        lcdBbTrustDriver = v == 10;
        Serial.printf("буфер для порції кадру: %s\n", lcdBbTrustDriver ? "той, що називає драйвер (як до 2.49)" : "свій — за місцем порції в кадрі");
        return;
      }
      if (cfg.isTx && cfg.board == BOARD_4848 && v <= 1 && arg.length()) {
        lcdClassicMagic = v ? LCD_CLASSIC : 0;
        Serial.println("перезапуск із іншим виводом кадру");
        Serial.flush();
        ESP.restart();
      }
      return;
    case 'F':   // передатчик: файловый менеджер карты с порта — Fl<папка> список, Fi<файл> сведения, Fp<файл> в эфир, Fs стоп,
                // Fm<папка> создать, Fd<путь> удалить, Fc<файл>|<папка> копировать; F — что сейчас в эфире. Форматирования с порта нет.
      if (cfg.isTx && cfg.board == BOARD_4848) fmSerial(arg);
      return;
    case 'Z':   // передатчик: запись на карту — Z1 начать, Z0 остановить, Z — список записей
      if (cfg.isTx && cfg.board == BOARD_4848) {
        if (!recSd) Serial.println("картки немає — запис неможливий");
        else if (arg.length() == 0) recListAsk = true;
        else if (arg[0] == 'v') recVerify(arg.c_str() + 1);   // Zv<файл> — сверить запись проверочного узора
        else if (v) {   // Z1 — запись звука; Z2 — проверочный узор (счётчик); Z3 — узор без накопителя секторов
          recPattern = v >= 2;
          recNoCache = v == 3;
          recWantStart = true;
        } else recWantStop = true;
      }
      return;
    case 'S':   // передатчик: имя источника звука на кнопке (S без имени — вернуть «З пульта»)
      if (cfg.isTx) {
        strlcpy(cfg.srcName, arg.c_str(), sizeof(cfg.srcName));
        settingsSave();
        if (cfg.board == BOARD_4848) ui::needFull = true;
        Serial.printf("назва джерела звуку: «%s»\n", cfg.srcName[0] ? cfg.srcName : "З пульта");
      }
      return;
    case 'L':   // язык надписей: L0 — українська, L1 — English (на передатчике; приёмники берут его у передатчика)
      if (arg.length()) {
        cfg.lang = v ? 1 : 0;
        uiLang = cfg.lang;
        settingsSave();
        if (cfg.isTx && cfg.board == BOARD_4848) ui::needFull = true;
      }
      Serial.printf("мова написів: %s", cfg.lang ? "English" : "українська");
      if (cfg.isTx) Serial.printf("; голос оголошення: запис «%s»%s", txVoiceName(), cfg.lang && assetFind("golos-en") < 0 ? " (англійського голосу в пам'яті немає)" : "");
      Serial.println();
      return;
    case 'h':   // передатчик: сам уходит с занятого канала — h1 / h0; h2 — перейти сейчас (проверка без настоящего затора)
      if (cfg.isTx) {
        if (v == 2) hopForce = true;
        else if (v == 3) {            // шесть «плохих» секунд подряд — проверить счёт затора (паузу после прошлой попытки снять)
          hopFake = 6;
          hopQuietUntil = 0;
        } else if (arg.length()) cfg.autoHop = v ? 1 : 0;
        Serial.printf("канал при заторі: %s%s\n", cfg.autoHop ? "міняю сам" : "не міняю", v == 2 ? "; пошук почнеться за секунду" : v == 3 ? "; шість секунд вважаю поганими" : "");
        if (v < 2 && arg.length()) settingsSave();
      }
      return;
    case 'O':   // передатчик: выключить (как кнопкой в шапке); включает касание экрана — или сброс
      if (cfg.isTx && cfg.board == BOARD_4848) txscreen::offAsk = true;
      return;
    case 'x':   // отладка: шифр вхолостую вдобавок к рабочему (1 — в задаче передачи, 2 — в задаче Wi-Fi, 3 — в обеих, 0 — выключить)
      secShadow = v & 3;
      Serial.printf("апаратний AES додатково: %u\n", (unsigned)secShadow);
      return;
    case 'T':   // передатчик: короткая очередь и прореживание пакетов при занятом эфире — T1 (обычно), T0 — как до версии 1.7; не запоминается
      if (cfg.isTx) {
        txThinOn = v != 0;
        Serial.printf("прорідження при зайнятому ефірі %s\n", txThinOn ? "увімкнено" : "ВИМКНЕНО");
      }
      return;
    case 'A':   // передатчик: считать чужие передачи на своём канале (A1) — в ежесекундном отчёте; A0 — выключить
      if (cfg.isTx) {
        monSet(v != 0);
        Serial.printf("спостереження за ефіром %s\n", monOn ? "увімкнено" : "вимкнено");
      }
      return;
    case 'P': peersPrint(); return;
    case 'n':   // приёмник: настройки «для слуха и удобства» — n показать; n<номер>=<значение> сменить; n9=1 / n9=0 — проверка наушников
      if (!cfg.isTx) {
        int p = -1, val = 0;
        if (sscanf(arg.c_str(), "%d=%d", &p, &val) == 2) {
          if (p == 9) rxEarStart(val != 0);
          else if (p == 10) {   // n10=<дБ 0…24> — усиление (с 2.48)
            if (rxParamSet(RXP_X_BOOST, val / 2)) settingsSave();
          }
          else if (p == 7) rxUiAsk = (uint8_t)val;   // n7=1 меню, n7=2 «Зв'язок», n7=3 главный экран — для замеров
          else if (p == 8) {   // n8=1 — как настроен вывод кнопки ручки; n8=<мс> — «нажать» её программно на столько миллисекунд
            if (val == 9) {   // n8=9 — «логический анализатор»: что на самом деле идёт на ЦАП по выводам BCK, LRCK и данных
              if (cfg.output != OUT_I2S) {
                Serial.println("вихід не PCM5102 — знімати нічого");
                return;
              }
              const int N = 16384;
              uint8_t *b = (uint8_t *)malloc(N);
              if (!b) return;
              for (int pin : { PIN_I2S_MCLK, PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DOUT }) gpio_input_enable((gpio_num_t)pin);
              digitalWrite(PIN_DAC_XSMT, LOW);   // ЦАП заглушить: в наушники постоянные числа не пойдут
              delay(40);
              rxPattern = true;
              delay(30);
              portDISABLE_INTERRUPTS();
              for (int i = 0; i < N; i++) b[i] = (uint8_t)(REG_READ(GPIO_IN_REG) >> 10);   // бит 0 — вывод 10, 1 — BCK, 2 — LRCK, 3 — данные
              portENABLE_INTERRUPTS();
              rxPattern = false;
              delay(30);
              digitalWrite(PIN_DAC_XSMT, HIGH);
              static int idx[420];
              int n = 0;
              for (int i = 1; i < N - 4 && n < 420; i++)
                if (!((b[i - 1] >> 1) & 1) && ((b[i] >> 1) & 1)) idx[n++] = i;   // подъёмы BCK
              if (n < 80) {
                Serial.printf("BCK не видно (підйомів %d)\n", n);
                free(b);
                return;
              }
              float per = (float)(idx[n - 1] - idx[0]) / (n - 1);
              Serial.printf("знято %d відліків; на один такт BCK %.1f відліку; вивід 10 (SCK): %d\n", N, per, b[N / 2] & 1);
              int k0 = -1;   // первый подъём BCK, на котором LRCK уже 0, а на предыдущем был 1
              for (int k = 3; k < n - 40; k++)
                if (((b[idx[k - 1]] >> 2) & 1) && !((b[idx[k]] >> 2) & 1)) {
                  k0 = k;
                  break;
                }
              if (k0 < 0) {
                Serial.println("LRCK не змінюється");
                free(b);
                return;
              }
              char ws[40], d0[40], d1[40];
              int m = 0;
              for (; m < 36 && k0 + m < n; m++) {
                int i = idx[k0 + m];
                ws[m] = '0' + ((b[i] >> 2) & 1);
                d0[m] = '0' + ((b[i - 3 < 0 ? 0 : i - 3] >> 3) & 1);   // данные чуть раньше подъёма BCK
                d1[m] = '0' + ((b[i + 3] >> 3) & 1);                   // и чуть позже: должны совпадать
              }
              ws[m] = d0[m] = d1[m] = 0;
              // когда меняется LRCK: при каком уровне BCK и за сколько отсчётов до ближайшего подъёма
              int j = idx[k0 - 1];
              while (j < idx[k0] && ((b[j] >> 2) & 1)) j++;
              Serial.printf("LRCK:   %s\n", ws);
              Serial.printf("DATA до: %s\n", d0);
              Serial.printf("DATA по: %s\n", d1);
              Serial.printf("LRCK упав при BCK=%d, за %d відліків до підйому BCK (такт — %.0f відліків)\n", (b[j] >> 1) & 1, idx[k0] - j, per);
              free(b);
              return;
            }
            if (val == 8) {   // n8=8 — настоящие частоты за время с прошлого такого вызова: звук из эфира (часы передатчика) и выход (свои часы)
              static int64_t t0;
              static uint32_t in0, out0, st0, sn0;
              int64_t t = esp_timer_get_time();
              uint32_t in = rxInSamples, out = rxOutSamples;
              if (t0) {
                double sec = (t - t0) / 1e6, fin = (in - in0) / sec, fout = (out - out0) / sec;
                Serial.printf("за %.1f с: з ефіру %.2f відліків/с (%+.0f ppm від 32000), на вихід %.2f відліків/с (%+.0f ppm), вихід відносно ефіру %+.0f ppm; темп %+.0f ppm\n",
                              sec, fin, (fin / 32000 - 1) * 1e6, fout, (fout / 32000 - 1) * 1e6, (fout / fin - 1) * 1e6, (double)rxRatioPpm);
              }
              if (t0) {
                Serial.printf("вихід лишався без даних %u раз із %u порцій по 1 мс\n", (unsigned)(rxOutStarved - st0), (unsigned)(rxOutSent - sn0));
                float low = rxLimLow;
                Serial.printf("обмежувач: працював %.1f %% часу, найбільше −%.1f дБ; гучність %u, чіткість %u\n", (out - out0) ? 100.0 * rxLimBusy / (out - out0) : 0.0,
                              low < 1 ? -20 * log10f(low) : 0.0f, cfg.volume, cfg.rxClarity);
                Serial.printf("кадрів із запасних копій: зі стерео %u, у моно %u\n", (unsigned)rxScCount, (unsigned)rxMonoRec);
                rxScCount = 0;
                rxMonoRec = 0;
              }
              rxLimBusy = 0;
              rxLimLow = 1;
              st0 = rxOutStarved;
              sn0 = rxOutSent;
              t0 = t;
              in0 = in;
              out0 = out;
              return;
            }
            if (val == 7) {   // n8=7 — что на выводах выхода звука: доля единиц и число перепадов за 20 мс (BCK, LRCK, данные; PDM «+», «−»)
              for (int pin : { PIN_I2S_MCLK, PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_OUT_P, PIN_OUT_N }) {
                gpio_input_enable((gpio_num_t)pin);
                uint32_t ones = 0, n = 0, edges = 0, t0 = micros();
                int prev = gpio_get_level((gpio_num_t)pin);
                while (micros() - t0 < 20000) {
                  int v = gpio_get_level((gpio_num_t)pin);
                  ones += v;
                  edges += v != prev;
                  prev = v;
                  n++;
                }
                Serial.printf("вивід %d: одиниць %u %%, перепадів за 20 мс %u (≈%u Гц), опитувань %u\n", pin, (unsigned)(n ? ones * 100 / n : 0), (unsigned)edges,
                              (unsigned)(edges * 25), (unsigned)n);
              }
              Serial.printf("вихід: %s, %s; XSMT (вивід %d): %d\n", cfg.output == OUT_I2S ? "PCM5102 (I2S): BCK 11, LRCK 12, DIN 13; вивід 10 — нуль для SCK" : "PDM: 17, 18",
                            rxOutLive() ? "живий" : "СТОЇТЬ", PIN_DAC_XSMT, gpio_get_level((gpio_num_t)PIN_DAC_XSMT));
              return;
            }
            if (val == 2) gpio_wakeup_enable((gpio_num_t)PIN_ENC_SW, GPIO_INTR_LOW_LEVEL);   // n8=2 — оставить вывод таким, каким его оставлял сон до 2.32
            if (val == 5 || val == 6) {   // n8=5 — «нажать» ручку на секунду, не задерживая главный цикл; n8=6 — то же через 30 с
              // (во сне порт не слушает, поэтому пробуждение кнопкой проверяется нажатием, назначенным заранее)
              static esp_timer_handle_t rel;
              static volatile bool down;
              if (!rel) {
                esp_timer_create_args_t ta = {};
                ta.callback = [](void *) {
                  if (!down) {   // пора нажать
                    down = true;
                    gpio_set_direction((gpio_num_t)PIN_ENC_SW, GPIO_MODE_INPUT_OUTPUT_OD);
                    gpio_set_level((gpio_num_t)PIN_ENC_SW, 0);
                    esp_timer_start_once(rel, 1200000);
                  } else {       // пора отпустить
                    down = false;
                    gpio_set_level((gpio_num_t)PIN_ENC_SW, 1);
                    gpio_set_direction((gpio_num_t)PIN_ENC_SW, GPIO_MODE_INPUT);
                  }
                };
                ta.name = "key";
                esp_timer_create(&ta, &rel);
              }
              esp_timer_stop(rel);
              down = false;
              esp_timer_start_once(rel, val == 6 ? 30000000 : 1000);
              Serial.printf("ручку буде натиснуто на секунду %s\n", val == 6 ? "через 30 с" : "зараз");
              return;
            }
            if (val == 3 || val == 4) {   // n8=3 / n8=4 — «повернуть» ручку на щелчок в одну и в другую сторону (выводы сами замыкаются на «землю»)
              int a = val == 3 ? PIN_ENC_A : PIN_ENC_B, b = val == 3 ? PIN_ENC_B : PIN_ENC_A;
              for (int pin : { a, b }) gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT_OUTPUT_OD);
              const int seq[4][2] = { { a, 0 }, { b, 0 }, { a, 1 }, { b, 1 } };
              for (auto &st : seq) {
                gpio_set_level((gpio_num_t)st[0], st[1]);
                delay(3);
              }
              for (int pin : { a, b }) gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
              Serial.printf("ручку повернуто на крок: лічильник %d, гучність %u\n", (int)encCount, cfg.volume);
              return;
            }
            if (val >= 50) {   // вывод на это время сам тянет себя к «земле» — ровно то, что делает кнопка
              Serial.printf("натискаю ручку на %d мс\n", val);
              Serial.flush();
              gpio_set_direction((gpio_num_t)PIN_ENC_SW, GPIO_MODE_INPUT_OUTPUT_OD);
              gpio_set_level((gpio_num_t)PIN_ENC_SW, 0);
              delay(val > 5000 ? 5000 : val);
              gpio_set_level((gpio_num_t)PIN_ENC_SW, 1);
              gpio_set_direction((gpio_num_t)PIN_ENC_SW, GPIO_MODE_INPUT);
            }
            Serial.printf("вивід кнопки ручки (%d): рівень %d, вид переривання %u, переривання дозволено %u, будить зі сну %u; виходів з очікування %u, кіл сну %u\n",
                          PIN_ENC_SW, gpio_get_level((gpio_num_t)PIN_ENC_SW), (unsigned)GPIO.pin[PIN_ENC_SW].int_type, (unsigned)GPIO.pin[PIN_ENC_SW].int_ena,
                          (unsigned)GPIO.pin[PIN_ENC_SW].wakeup_enable, (unsigned)rxWakes, (unsigned)rxSleeps);
            return;
          }
          else if (rxParamSet((uint8_t)p, val)) settingsSave();
        }
        Serial.printf("налаштування: 0 чіткість %u, 1 баланс %d, 2 межа гучності %u (гучність %u, звук %.1f дБ), 3 вигляд %u, 4 світлодіод %u, 5 мова %u (на екрані %s)%s; підсилення +%u дБ (зараз %+.1f); блокування %u; еквалайзер %d %d %d %d %d дБ, зріз низів %u; рівень до нього %.1f дБ, після %.1f дБ\n",
                      cfg.rxClarity, cfg.rxBalance, cfg.rxVolMax, cfg.volume, cfg.volume ? 20 * log10f(rxVolumeGain(rxVolumeScaled(cfg.volume))) : -99.0f, cfg.rxView, cfg.rxLed, cfg.rxLang, uiLang ? "English" : "українська",
                      rxEarTest ? "; ІДЕ ПЕРЕВІРКА НАВУШНИКІВ" : "", cfg.rxBoost * 2, 20 * log10f(rxBoostNow > 0.01f ? rxBoostNow : 0.01f), cfg.rxLock, (cfg.rxEq[0] - 6) * 2, (cfg.rxEq[1] - 6) * 2, (cfg.rxEq[2] - 6) * 2,
                      (cfg.rxEq[3] - 6) * 2, (cfg.rxEq[4] - 6) * 2, cfg.rxLowCut,
                      20 * log10f((rxEqPkIn + 1) / 32768.0f), 20 * log10f((rxEqPkOut + 1) / 32768.0f));
        rxEqPkIn = 0;
        rxEqPkOut = 0;
      }
      return;
    case 'e': {   // передатчик: эквалайзер входа — e<полоса 0…4>=<дБ −12…12>, e5=<0/1> — срез низов; e — показать
      int b = -1, val = 0;
      if (cfg.isTx && sscanf(arg.c_str(), "%d=%d", &b, &val) == 2 && b == 9) {   // e9=1 — проба: эквалайзер и на тоне 1 кГц
        txEqOnTone = val != 0;
        Serial.printf("еквалайзер входу на перевірочному тоні: %u\n", (unsigned)txEqOnTone);
        return;
      }
      bool set = cfg.isTx && sscanf(arg.c_str(), "%d=%d", &b, &val) == 2 && b >= 0 && b <= 5;
      if (set) {
        if (b < 5) cfg.txEq[b] = (uint8_t)constrain(val / 2 + 6, 0, 12);
        else cfg.txLowCut = val ? 1 : 0;
        txEqGen = txEqGen + 1;
      }
      Serial.printf("еквалайзер входу: %d %d %d %d %d дБ (125 Гц, 400 Гц, 1 кГц, 2,5 кГц, 6 кГц), зріз низів %u\n", (cfg.txEq[0] - 6) * 2,
                    (cfg.txEq[1] - 6) * 2, (cfg.txEq[2] - 6) * 2, (cfg.txEq[3] - 6) * 2, (cfg.txEq[4] - 6) * 2, cfg.txLowCut);
      if (set) break;
      return;
    }
    case 'i':   // передатчик: настройка приёмнику — i<номер>=<значение>[@номер приёмника]; i9=1 / i9=0 — проверка наушников
      if (cfg.isTx) {
        int p = -1, val = 0;
        char who[8] = "";
        if (sscanf(arg.c_str(), "%d=%d@%6s", &p, &val, who) < 2) return;
        for (int i = 0; i < PEERS_MAX; i++) {
          if (!peers[i].used || !peerOnline(peers[i])) continue;
          char idx[8];
          snprintf(idx, sizeof(idx), "%02X%02X%02X", peers[i].id[0], peers[i].id[1], peers[i].id[2]);
          if (who[0] && strcasecmp(who, idx)) continue;
          if (!peers[i].hasInfo) continue;   // понимают приёмники с версии 2.32 — они сообщают свои настройки
          if (p == 7) {   // i7=<дБ 0…24> — усиление приёмника (с 2.48)
            peerCommand(peers[i].id, CMD_SET, (uint8_t)((RXP_X_BOOST << 5) | constrain(val / 2, 0, 12)));
          } else
          if (p == 6) {   // i6=<0…3> — блокировка ручки: 0 нет, 1 меню, 2 громкость, 3 всё (приёмники с 2.46)
            peerCommand(peers[i].id, CMD_SET, (uint8_t)((RXP_X_LOCK << 5) | (val & 3)));
          } else
          if (p >= 20 && p <= 25) {   // i20…i24=<−12…12 дБ> — полосы эквалайзера, i25=<0/1> — срез низов
            if (!peers[i].hasEq) continue;
            int v = p == 25 ? (val ? 1 : 0) : constrain(val / 2 + 6, 0, 12);
            peerCommand(peers[i].id, CMD_EQ, (uint8_t)(((p - 20) << 4) | v));
          } else
          if (p == 9) peerCommand(peers[i].id, CMD_EARTEST, val ? 1 : 0);
          else if (p >= 0 && p < RXP_COUNT) peerCommand(peers[i].id, CMD_SET, (uint8_t)((p << 5) | ((val + (p == RXP_BALANCE ? 5 : 0)) & 31)));
          Serial.printf("команду надіслано приймачу %s\n", idx);
          if (!who[0]) break;
        }
      }
      return;
    case 'H': pinProbe(); return;
    case 'I':
    case 'E':
    case 'W':
    case 'V':
    case 'X':
    case 'N':
      for (int i = 0; i < PEERS_MAX; i++)
        if (peers[i].used && peerOnline(peers[i])) {
          if (c == 'W' && peers[i].fw < 11) continue;   // стерео понимают приёмники с версии 1.1
          if (c == 'X' && peers[i].fw < 13) continue;   // проверку выхода — с версии 1.3
          if (c == 'X') peerCommand(peers[i].id, CMD_DACTEST, 0);
          if (c == 'I') peerCommand(peers[i].id, CMD_IDENTIFY, 0);
          if (c == 'E') {   // как выключатель на экране: команда повторяется, пока приёмник (он может спать) не подтвердит
            peerCommand(peers[i].id, CMD_ENABLE, v ? 1 : 0);
            peers[i].wantOff = !v;
            peers[i].enforceUntil = millis() + 15000;
            peersDirty = true;
          }
          if (c == 'W') peerCommand(peers[i].id, CMD_STEREO, v ? 1 : 0);
          if (c == 'V') peerCommand(peers[i].id, CMD_VOLUME, v < 0 ? 0 : v > 20 ? 20 : v);
          if (c == 'N') peerCommand(peers[i].id, CMD_NAME, 0, arg.c_str());
          Serial.printf("команду надіслано приймачу %02X%02X%02X\n", peers[i].id[0], peers[i].id[1], peers[i].id[2]);
          break;
        }
      return;
    case 'M':   // передатчик: обновление прошивки приёмников по радио — M1 начать, M0 отменить, M — состояние
      if (cfg.isTx) {
        // M1 — начать; M4 — пробное (приёмники принимают и сверяют, но не записывают); M9 — холостая передача всей
        // прошивки, даже если её никто не ждёт (проверка самого передатчика)
        // M2 — обновить всё файлом с карты (приёмники, затем сам передатчик); M3 — просмотреть папку UPDATE заново
        if (arg.length() && (v == 1 || v == 4 || v == 9))
          Serial.println(otaTxStart(v == 4, v == 9) ? "оновлення приймачів розпочато" : "оновлення зараз неможливе (уже йде або немає ключа набору)");
        else if (arg.length() && v == 5 && upd.state == UPD_OLDER && !upd.img) {   // M5: старую прошивку сперва прочитать с карты
          upd.keepOlder = true;
          upd.scanAsk = true;
          Serial.println("читаю з картки старішу прошивку — повторіть M5 за пів хвилини");
        } else if (arg.length() && (v == 2 || v == 5))   // M5 — то же, но и когда прошивка в файле старше работающей (возврат на прежнюю)
          Serial.println((upd.state == UPD_READY || (v == 5 && upd.state == UPD_OLDER)) && otaTxStart(false, false, upd.img, upd.len, upd.version, upd.sha) ? "оновлення з картки розпочато" : "на картці немає придатної нової прошивки");
        else if (arg.length() && v == 3) upd.scanAsk = true;
        else if (arg.length() && (v == 6 || v == 7)) {   // M6 / M7 — автообновление приёмников включить / выключить
          cfg.autoUpd = v == 6;
          settingsSave();
        }
        else if (arg.length() && v == 0) otaTx.cancel = true;
        Serial.printf("картка: стан %u, файл «%s», версія «%s», причина %u; мітка цієї прошивки %s\n", upd.state, upd.file, upd.version, upd.why, FW_TAG);
        otaTxPrint();
        Serial.printf("автооновлення приймачів: %s (M6 — увімкнути, M7 — вимкнути); запусків у це ввімкнення %u\n", cfg.autoUpd ? "увімкнено" : "вимкнено", (unsigned)otaAutoRuns);
      } else Serial.printf("оновлення: стан %u, %u %%, блоків %u з %u, причина %u\n", otaRx.phase, otaRx.percent, (unsigned)otaRx.got, (unsigned)otaRx.blocks, otaRx.err);
      return;
    case 'Q':   // передатчик: «чёрный ящик» радио. Q — показать; Q0…Q3 — до какой ступени оживлять радио (обычно 3);
                // Q5 — начинать оживление со 2-й ступени, Q4 — с 1-й; Q7/Q8/Q9 — проверить ступень 1/2/3 на здоровом радио, Q6 — сброс поглубже (через сон)
      if (cfg.isTx) {
        if (arg.length() && v >= 0 && v <= 3) rReviveMax = v;
        else if (v == 4 || v == 5) rReviveFrom = v - 3;
        else if (v >= 7 && v <= 9) bbTestStep = v - 6;
        else if (v == 6) {   // проверить «сброс поглубже»
          quickBootMagic = QUICK_BOOT;
          esp_sleep_enable_timer_wakeup(100000);
          esp_deep_sleep_start();
        }
        bbPrint(true);
      }
      return;
    case 'u':
      if (cfg.isTx) {
        if (arg == "d") {   // ud — печатать в порт каждое касание экрана (включить/выключить)
          txscreen::touchDebug = !txscreen::touchDebug;
          Serial.printf("друк дотиків %s\n", txscreen::touchDebug ? "увімкнено" : "вимкнено");
          return;
        }
        txscreen::report();
        return;
      }
      Serial.printf("екран: %s; ручка: %d чверть-кроків; кнопки зараз: ручка %d, CON %d, BAK %d\n", uiHaveOled ? "знайдено" : "НЕ ЗНАЙДЕНО",
                    (int)encCount, (int)(uiKeys & 1), (int)(uiKeys >> 1 & 1), (int)(uiKeys >> 2 & 1));
      return;
    default: return;
  }
  settingsSave();
  if (restart) {
    Serial.println("перезапуск…");
    delay(100);
    ESP.restart();
  }
  help();
}

// Журнал работы на карту (см. txrec.h): показатели копятся десять секунд и уходят одной строкой.
struct LogAcc {
  uint32_t n, frames, refused, air, thinned, skipped, airSumUs, airN, airMaxUs;
  int pendMax, pk;
};
static LogAcc logAcc;
static void logEmit() {
  if (!recSd || logSeq != logDone) return;   // карты нет или прежняя строка ещё не записана — эту пропускаем
  static bool head;
  int o = 0;
  char *t = logText;
  const int cap = sizeof(logText);
  if (!head) {   // первая строка файла: что за прибор и почему запустился
    head = true;
    o += snprintf(t + o, cap - o, "# hearlink %s; запуск: %s; рядки раз на 10 с: час с, канал, швидкість, якість, пакетів, відмов, не пішло, перезапусків радіо, "
                  "до ефіру сер./макс. мс, черга, проріджено, пропущено, рівень дБ, джерело, запис | приймач: зв'язок, сон, сигнал там/тут дБм, запас мс, втрати %%, "
                  "пропусків, провалів мс\n", FW_VERSION, resetReason());
  }
  const LogAcc &a = logAcc;
  o += snprintf(t + o, cap - o, "t=%u к=%u шв=%s як=%u пак=%u відм=%u неп=%u рест=%u еф=%.1f/%.1f черга=%d прор=%u проп=%u рів=%.0f дж=%s зап=%u", (unsigned)(millis() / 1000),
                cfg.channel, RATES[cfg.rateIdx].name, (unsigned)txQNow, (unsigned)a.frames, (unsigned)a.refused, (unsigned)a.air, (unsigned)rRestarts,
                a.airN ? a.airSumUs / 1000.0f / a.airN : 0.0f, a.airMaxUs / 1000.0f, a.pendMax, (unsigned)a.thinned, (unsigned)a.skipped,
                a.pk > 0 ? 20 * log10f(a.pk / 32768.0f) : -99.0f, fm.playing ? "файл" : cfg.tone ? "перевірка" : txMute ? "тиша" : "вхід", recOn ? 1u : 0u);
  for (int i = 0; i < PEERS_MAX && o < cap - 90; i++) {
    const Peer &p = peers[i];
    if (!p.used) continue;
    o += snprintf(t + o, cap - o, " | %02X%02X%02X зв=%u сон=%u с=%d/%d зап=%u втр=%.1f проп=%u пров=%u", p.id[0], p.id[1], p.id[2], peerOnline(p) ? 1u : 0u,
                  p.flags & ST_SLEEP ? 1u : p.flags & ST_OFF ? 2u : 0u, p.rssi, p.rssiHere, p.depthMs, p.lossPm / 10.0f, p.lostFrames, p.underMs);
  }
  if (o > cap - 2) o = cap - 2;
  t[o++] = '\n';
  t[o] = 0;
  logSeq = logSeq + 1;
}

static void reportTx() {
  uint32_t frames = txFrames, refused = rRefused, air = rAirFail, raw = txRawSamples, clips = txClips;
  uint32_t mn = txRawMin, mx = txRawMax;
  int pk = txPeak;
  txFrames = 0;
  rRefused = 0;
  rAirFail = 0;
  rSent = 0;
  txRawSamples = 0;
  txClips = 0;
  txRawMin = 4095;
  txRawMax = 0;
  txPeak = 0;
  uint32_t airN = rAir2N, airSum = rAir2SumUs, airMax = rAir2MaxUs, thinned = txThinned, skipped = txSkipped;
  int32_t pendMax = rPendMax;
  rAir2N = 0;
  rAir2SumUs = 0;
  rAir2MaxUs = 0;
  rPendMax = 0;
  txThinned = 0;
  txSkipped = 0;
  Serial.printf("ПЕРЕДАВАЧ к=%u шв=%s як=%s набір %u | пакетів %u, відмов %u, не пішло в ефір %u, перезапусків радіо %u, до ефіру сер. %u макс. %u мкс | ",
                cfg.channel, RATES[cfg.rateIdx].name, Q_NAME[txQNow < Q_COUNT ? txQNow : 0], cfg.kit, (unsigned)frames, (unsigned)refused, (unsigned)air,
                (unsigned)rRestarts, (unsigned)(airN ? airSum / airN : 0), (unsigned)airMax);
  if (cfg.input == IN_ADC)
    Serial.printf("АЦП %u відл/с, сирі %u…%u, перевантажень %u, перезапусків АЦП %u | ", (unsigned)raw, (unsigned)mn, (unsigned)mx, (unsigned)clips,
                  (unsigned)txAdcRestarts);
  else Serial.printf("I2S %u відл/с | ", (unsigned)raw);
  int pkS = txPeakS, pkAir = txPeakAir;
  txPeakS = 0;
  txPeakAir = 0;
  Serial.printf("рівень %.1f дБ%s", pk > 0 ? 20 * log10f(pk / 32768.0f) : -99.0f, cfg.tone ? " (перевірочний звук)" : "");
  if (txHold) Serial.print(" | ПЕРЕДАЧА СТОЇТЬ (заставка або команда z)");
  else if (txMute) Serial.printf(" | ТИША В ЕФІРІ: в ефір іде %.1f дБ", pkAir > 0 ? 20 * log10f(pkAir / 32768.0f) : -99.0f);
  if (txStNow) Serial.printf(" | СТЕРЕО, різниця каналів %.1f дБ", pkS > 0 ? 20 * log10f(pkS / 32768.0f) : -99.0f);
  Serial.printf(" | черга макс. %d, проріджено %u, пропущено %u%s", (int)pendMax, (unsigned)thinned, (unsigned)skipped, txThinOn ? "" : " (прорідження вимкнено)");
  if (refused) Serial.printf(" | відмова драйвера: 0x%X", (unsigned)rLastErr);
  if (monOn) {   // чужие передачи Wi-Fi на нашем канале за эту секунду
    uint32_t fr = monFrames, us = monAirUs, mxu = monMaxUs, slow = monSlowUs;
    monFrames = 0;
    monAirUs = 0;
    monMaxUs = 0;
    monSlowUs = 0;
    Serial.printf(" | чужі: кадрів %u, зайнято %.1f %% часу (повільних 1–2 Мбіт/с %.1f %%), найдовший %u мкс", (unsigned)fr, us / 10000.0f, slow / 10000.0f,
                  (unsigned)mxu);
  }
  Serial.println();
  {   // журнал работы на карту: копим десять секунд
    LogAcc &a = logAcc;
    a.n++;
    a.frames += frames;
    a.refused += refused;
    a.air += air;
    a.thinned += thinned;
    a.skipped += skipped;
    a.airSumUs += airSum;
    a.airN += airN;
    if (airMax > a.airMaxUs) a.airMaxUs = airMax;
    if (pendMax > a.pendMax) a.pendMax = pendMax;
    if (pk > a.pk) a.pk = pk;
    if (a.n >= 10) {
      logEmit();
      a = LogAcc();
    }
  }
  hopTick(airMax, thinned, skipped, refused);   // затяжной затор — самому уйти на более свободный канал (своё сообщение — отдельной строкой)
}

static void reportRx() {
  uint32_t frames = sFrames, rec = sRecovered, lost = sLostFrames, under = sUnder, drops = sDrops, resync = sResync, thinned = sThinned;
  sThinned = 0;
  uint32_t fmin = rxFillMin, fmax = rxFillMax;
  uint32_t tz = tZero, tb = tBreaks, tc = tCount;
  float ts = tSumSq;
  int pk = rxPeak, pkL = rxPeakL, pkR = rxPeakR;
  rxPeakL = 0;
  rxPeakR = 0;
  rxLinkCount(frames, rec, lost, under);
  sFrames = 0;
  sRecovered = 0;
  sLostFrames = 0;
  sUnder = 0;
  sDrops = 0;
  sResync = 0;
  rxFillMin = UINT32_MAX;
  rxFillMax = 0;
  tZero = 0;
  tBreaks = 0;
  tCount = 0;
  tSumSq = 0;
  rxPeak = 0;
  if (!consoleLive() && !dbgAirOn()) {   // никто не смотрит ни в порт, ни по радио — молчим (счётчики выше всё равно учтены)
    rForeign = 0;
    return;
  }
  if (uiHaveOled) {   // экран приёмника: сколько кадров в секунду и сколько идёт один кадр (плавность спектра)
    uint32_t n = oledFrames, sum = oledFrameUsSum, mx = oledFrameUsMax;
    oledFrames = 0;
    oledFrameUsSum = 0;
    oledFrameUsMax = 0;
    Serial.printf("екран: %u кадрів за секунду, передача кадру сер. %.1f мс, найдовша %.1f мс, обмін %s кГц\n", (unsigned)n, n ? sum / 1000.0f / n : 0.0f, mx / 1000.0f,
                  oledFast ? "800" : "400");
  }
  {
    static uint8_t ledShown = 255;
    static uint8_t ledEvery;
    uint32_t lw = ledWrites;
    ledWrites = 0;
    if (ledState != ledShown || ++ledEvery >= 5) {   // светодиод сменил цвет (и раз в 5 с) — строкой в порт: глазами его вижу не я
      ledShown = ledState;
      ledEvery = 0;
      Serial.printf("світлодіод: %s, оновлень за секунду %u (зараз %u/%u/%u)\n", ledName(), (unsigned)lw, ledR, ledG, ledB);
    }
  }
  static const char *const PWN[] = { "пошук передавача", "запуск", "робота", "переходжу в очікування", "ОЧІКУВАННЯ", "прокидаюсь" };
  if (rxPower != PW_RUN)
    Serial.printf("стан: %s%s, %u с; виходів з очікування %u; кіл сну %u\n", PWN[rxPower],
                  rxPower == PW_STANDBY || rxPower == PW_GOING ? (rxPowerWhy == WHY_OFF ? " (вимкнено з передавача)" : " (немає сигналу)") : "",
                  (unsigned)((millis() - rxPowerAt) / 1000), (unsigned)rxWakes, (unsigned)rxSleeps);
  if (cfg.output == OUT_PDM && (rxPower != PW_RUN || !rxOutLive()))
    Serial.printf("виводи виходу: %u і %u %% одиниць (%s)\n", rxOutDuty[0], rxOutDuty[1], rxOutLive() ? "сигнал іде" : "стоять");
  if (!secHave) {
    Serial.printf("ПРИЙМАЧ к=%u | НЕ ПІДКЛЮЧЕНО до набору: прошу доступ, код %04u, запитів %u, передавач %s\n", cfg.channel, (unsigned)rxPairCode,
                  (unsigned)rxPairAsked, msSince(rLastAnyMs) < 1000 && rLastAnyMs ? "чути" : "не чути");
    rForeign = 0;
    return;
  }
  if (!frames) {
    Serial.printf("ПРИЙМАЧ к=%u набір %u | сигналу немає%s%s\n", cfg.channel, cfg.kit, rForeign ? " (є чужі пакети)" : "",
                  secBadTag ? " (є пакети з чужим підписом)" : "");
    rForeign = 0;
    return;
  }
  rForeign = 0;
  Serial.printf("ПРИЙМАЧ к=%u набір %u | кадрів %u, відновлено %u, втрачено %u, порожньо %.1f мс, зсувів %u, перезапусків %u, з копій %u | ",
                cfg.channel, cfg.kit, (unsigned)frames, (unsigned)rec, (unsigned)lost, under * 1000.0f / SRATE, (unsigned)drops, (unsigned)resync, (unsigned)thinned);
  Serial.printf("запас %.1f мс (%.1f…%.1f, ціль %.1f%s), темп %+.0f ppm | сигнал %d дБм | ", rxFillSm * 1000.0f / SRATE,
                fmin == UINT32_MAX ? 0.0f : fmin * 1000.0f / SRATE, fmax * 1000.0f / SRATE, (float)rxTargetMs, cfg.depthMs ? "" : " сам",
                (float)rxRatioPpm, (int)rRssi);
  if ((rxFlags & FLAG_TONE) && tc > SRATE / 2)
    Serial.printf("тон %.1f Гц %.1f дБ, розривів %u\n", tz * (float)SRATE / tc, 10 * log10f(ts / tc / (32768.0f * 32768.0f) * 2), (unsigned)tb);
  else Serial.printf("рівень %.1f дБ%s", pk > 0 ? 20 * log10f(pk / 32768.0f) : -99.0f, rxFlags & FLAG_CLIP ? ", вхід передавача перевантажено" : "");
  if (!((rxFlags & FLAG_TONE) && tc > SRATE / 2)) {
    Serial.printf(" | ефір %s, вихід %s", rxAirStereo ? "СТЕРЕО" : "моно", cfg.rxStereo ? "СТЕРЕО" : "моно");
    if (cfg.rxStereo) Serial.printf(": лівий %.1f дБ, правий %.1f дБ", pkL > 0 ? 20 * log10f(pkL / 32768.0f) : -99.0f, pkR > 0 ? 20 * log10f(pkR / 32768.0f) : -99.0f);
    Serial.println();
  }
}

// Программный перезапуск платы (обновление по радио, «быстрый запуск» передатчика, команда порта, меню) настройку
// выводов НЕ сбрасывает: на них остаются включённые прерывания прежнего запуска и уже «случившиеся» события. Отсюда
// два сбоя, найденные 07.10 после жалобы владельца на зависание от кнопки ручки:
//  — pinMode включал на кнопке прерывание «пока уровень низкий», оставшееся от сна (см. rxKeyPinsClean в rxlink.h);
//  — приёмник после программного перезапуска нередко падал ещё при запуске («Stack canary watchpoint triggered (ipc1)»,
//    до трёх раз подряд): прерывание выводов ручки срабатывало в тот самый миг, когда его обработчик только ставился.
// Поэтому первым делом при запуске — снять прерывания со всех выводов и забыть все случившиеся события.
static void gpioIntrReset() {
  for (int pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++) {
    if (!GPIO_IS_VALID_GPIO(pin)) continue;
    gpio_intr_disable((gpio_num_t)pin);
    gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_DISABLE);
  }
  gpio_ll_clear_intr_status(&GPIO, 0xFFFFFFFFu);
  gpio_ll_clear_intr_status_high(&GPIO, 0xFFFFFFFFu);
}

void setup() {
  gpioIntrReset();
  Serial.begin(115200);
  settingsLoad();
  if (cfg.board == BOARD_DEVKIT && cfg.isTx) {
    // Цветной светодиод платы S3 (вывод 48, у новых плат — 38) передатчику на такой плате не нужен: гасим и держим
    // вывод в нуле, чтобы не вспыхивал от наводок. У приёмника им управляет rxled.h. На модуле с экраном (плата b1)
    // эти выводы заняты другим — там не трогаем.
    for (int pin : { 48, 38 }) {
      rgbLedWrite(pin, 0, 0, 0);
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    }
  }
  uiLang = cfg.lang;
  if (!cfg.isTx) rxLangApply();   // у приёмника язык может быть выбран свой
  Serial.println();
  if (!cfg.isTx) {
    if (!rxRingAlloc()) Serial.println("не вистачило пам'яті під запас звуку");   // до запуска радио: пакеты пойдут сразу
    else rOnAudio = rxOnPacket;
  }
  if (!radioBegin()) Serial.println("радіо не запустилось");
  esp_register_shutdown_handler([]() { rRebooting = true; });   // после запуска радио: вызовется раньше, чем его остановят
  secBegin();
  if (cfg.isTx) {
    // Ключ набора рождается в передатчике (радио уже включено — случайность настоящая). Эпоха — номер этого включения:
    // записываем до первого пакета, чтобы ни одно одноразовое число шифра не повторилось и после перезапуска.
    if (!secHave) {
      secNewKey();
      Serial.println("захист: створено новий ключ набору — приймачі треба додати («Приймачі» → «Додати приймач»)");
    }
    secEpoch = secEpoch + 1;
    secSaveEpoch();
    help();
    peersBegin();
    // На ядре экрана, с приоритетом выше экрана. На ядре радио он не успевал: в стерео «найвищої» якості то ядро
    // занято радио и сжатием, и кадр в 36 мс звука разбирался 23–33 мс (здесь — около 8). Экран при этом обязан
    // уступать время главному циклу — см. конец его цикла.
    txMute = cfg.txMute;                // как было при выключении
    quickBoot = quickBootMagic == QUICK_BOOT && (esp_reset_reason() == ESP_RST_SW || esp_reset_reason() == ESP_RST_DEEPSLEEP);
    quickBootMagic = 0;
    if (esp_reset_reason() == ESP_RST_POWERON) {   // после включения питания в памяти часов мусор
      quickBootCount = 0;
      bbSnapMagic = 0;
    }
    updatedBoot = quickBoot && updatedMagic == UPDATED_BOOT;
    updatedMagic = 0;
    if (quickBoot && !updatedBoot) quickBootCount = quickBootCount + 1;
    cardExtra = updService;          // задача карты смотрит папку UPDATE
    otaTx.selfWrite = updSelfWrite;
    otaTx.selfDone = updRestart;
    logExtra = (char *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    rOnDead = radioDead;
    txHold = cfg.board == BOARD_4848 && !quickBoot;   // на модуле с экраном передача начинается, когда заставку сменит меню
    // Разбор MP3 остаётся на втором ядре. Пробовал первое (07.10): там кадр MP3 разбирался 25–32 мс вместо 17 —
    // первое ядро занято передачей звука и радио, — и недоборы пошли даже у проверочной мелодии. Не переносить.
    xTaskCreatePinnedToCore(mp3Task, "mp3", 8192, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(txTask, "tx", 6144, NULL, 20, NULL, 0);
    if (cfg.board == BOARD_4848) {
      xTaskCreatePinnedToCore(txscreen::task, "ui", 12288, NULL, 2, NULL, 1);
      if (!quickBoot) {
      scanSurvey = true;   // пока на экране заставка и передачи ещё нет — замерить занятость каналов (канал не меняется):
      scanStart();         // по этому замеру передатчик при заторе уйдёт на свободный канал без паузы
      }
    }
  } else {
    // При включении приёмник печатает одну короткую строку, а не весь список настроек: каждая строка — вспышка
    // зелёного светодиода моста USB. Настройки покажет команда «?».
    Serial.printf("ПРИЙМАЧ, прошивка %s («?» — налаштування)\n", FW_VERSION);
    rLastRxMs = 0;
    rxLinkBegin();
    xTaskCreatePinnedToCore(rxTask, "rx", 6144, NULL, 20, NULL, 1);
    if (cfg.board == BOARD_DEVKIT) xTaskCreatePinnedToCore(uiTask, "ui", 8192, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(ledTask, "led", 3072, NULL, 3, NULL, 0);   // светодиод — отдельно от экрана (см. rxled.h)
  }
  if (cfg.isTx) rOnOta = otaTxOnPacket;
  else {
    memcpy(otaRxId, rxId, 3);
    otaRxLive = [] { rLastRxMs = millis(); };   // пакеты обновления — тоже «передатчик слышен»: приёмник не уходит искать канал и не засыпает
    rOnOta = otaRxOnPacket;
  }
  dbgBegin();   // отладка по радио (после того как у передатчика назначена отправка команд)
}

void loop() {
  static uint32_t lastReport;
  static String line;
  dbgPrintHeard();          // строки других устройств набора, пришедшие по радио
  if (dbgCmdAsk) {          // команда, пришедшая по радио: выполнить как набранную в порту
    String c = dbgCmdBuf;
    dbgCmdAsk = false;
    Serial.printf("> %s\n", c.c_str());
    if (c.length() && c[0] != '@') command(c);
  }
  if (!cfg.isTx) dbgPumpRx();
  otaAutoTick();            // передатчик: приёмник со старой прошивкой на связи — обновить его самому
  if (cfg.isTx && cfg.board == BOARD_4848) {   // драйвер экрана сбился с очереди буферов (panel4848.h): отметить, когда
    static uint32_t seenFlips;
    uint32_t f = lcdBbFlips;
    if (f != seenFlips) {
      seenFlips = f;
      bbMark(BB_LCDFLIP, f > 255 ? 255 : f);
      Serial.printf("екран: драйвер збився з черги буферів (%u-й раз від запуску) — порції кладемо по-своєму, картинка ціла\n", (unsigned)f);
    }
  }
  if (!cfg.isTx) {   // обновление прошивки по радио: приём, сверка, запись (см. ota.h)
    otaRxTick();
    bool busy = otaRxBusy();
    rxOtaMute = busy;
    ledOta = busy;
  }
  while (Serial.available()) {
    char ch = Serial.read();
    consoleMs = millis() ? millis() : 1;
    if (ch == '\n' || ch == '\r') {
      command(line);
      line = "";
    } else if (line.length() < 600) line += ch;   // было 32: путь к файлу на карте бывает длинным
  }
  if (!cfg.isTx) {
    if (rxPower != PW_STANDBY) radioScanTick();   // в ожидании каналы обходит сам круг сна
    rxLinkTick();
    rxPairTick();
    rxPowerTick();
  } else {
    rateAutoTick();
    pairTick();
  }
  if (cfg.isTx && logExtra) {   // радио вставало: через две секунды (видно, чем кончилось оживление) — слепок в порт и в журнал на карте
    static uint32_t seen, at;
    static bool prev = true;    // слепок прежнего запуска (плату пришлось перезапустить) — один раз, как только карта готова
    if (prev && millis() > 4000) {
      prev = false;
      if (bbSnapMagic == BB_MAGIC && !bbSnapSeq) {
        int o = snprintf(logExtra, 2048, "# з минулого запуску (швидких перезапусків плати %u): ", (unsigned)quickBootCount);
        bbFormat(bbSnap, logExtra + o, 2048 - o, false);
        Serial.print(logExtra);
        logExtraSeq = logExtraSeq + 1;
        bbSnapMagic = 0;
      }
    }
    if (bbSnapSeq != seen && !at) at = millis() + 2000;
    if (at && millis() > at && logExtraSeq == logExtraDone) {
      at = 0;
      seen = bbSnapSeq;
      bbRefresh();
      bbFormat(bbSnap, logExtra, 2048, false);
      Serial.print(logExtra);
      if (recSd) logExtraSeq = logExtraSeq + 1;
    }
  }
  if (cfg.isTx && peersDirty) {
    static uint32_t saveAt;
    if (!saveAt) saveAt = millis() + 3000;       // имена меняются редко — сохранить чуть погодя, одной записью
    else if (txQuietToSave(saveAt, 8000)) {      // и в паузе звука (см. txQuietToSave)
      saveAt = 0;
      peersSave();
    }
  }
  uint32_t now = millis();
  if (now - lastReport >= 1000) {
    lastReport += 1000;
    if (now - lastReport >= 1000) lastReport = now;
    if (cfg.isTx) reportTx();
    else reportRx();
  }
  delay(5);
}
