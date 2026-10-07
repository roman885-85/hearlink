// Меню передатчика (экран 480×480, сенсорный) — только рисование и логика нажатий, без обращения к железу.
//
// Этот файл собирается и в прошивке, и на компьютере (tools/sim): так вид страниц проверяется по картинкам.
// Что показывать, меню получает в структуре View; что человек изменил — отдаёт в структуре Out.
//
// Вид — как у панелей сайта, роутера и коммутатора в тёмной теме. Рисование — библиотекой m2gfx
// из «ПОТУЖНОГО РАДІО»: заливки идут векторными инструкциями ESP32-S3 прямо в память экрана.
// Целиком страница рисуется только при переходе; дальше обновляются лишь живые части (уровень, спектр, время).
#pragma once
#include "utf8.h"
#include <Arduino.h>
#include <initializer_list>
#include "m2gfx.h"
#include "lang.h"
#include "m2Title.h"
#include "m2Big.h"
#include "m2Mid.h"
#include "m2Row.h"
#include "m2RowB.h"
#include "fTxt.h"
#include "fTxtB.h"
#include "fNum.h"
#include "logo.h"
#include "fm.h"

namespace ui {
// ширина надписи с учётом языка
static int trW(const char *s, const GFXfont *f) {
  return m2::Gfx::textW(tr(s), f);
}
using m2::AL_C;
using m2::AL_L;
using m2::AL_R;

void uiDelay(int ms);   // даёт устройство (пауза между кадрами анимации)
// даёт устройство: просьба файлового менеджера карты (FM_… из fm.h); false — карта занята другой работой или её нет
bool uiFm(int op, const char *a, const char *b);
void uiWaitFrame();     // даёт устройство: дождаться начала следующего кадра экрана (в симуляторе — ничего)
// даёт устройство: показывать строки y0…y1 сдвинутыми на off точек (0…480) — прежняя страница с основного листа уезжает,
// новая с листа b въезжает (dir > 0 — справа); остальные строки сразу с листа b. b == NULL — снова основной лист.
// false — экран этого не умеет. Новое значение попадает на экран со следующего кадра.
void uiQuiet(bool on);   // просьба к остальным задачам ядра экрана не мешать (перелистывание страницы)
void uiBreath();         // короткая передышка в большой копии между листами (см. copyRows)
bool uiSlide(const uint16_t *b, int off, int dir, int y0, int y1);
void uiSlideSync();     // даёт устройство: дождаться, пока заданное uiSlide окажется на экране

constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
// тёмная тема панелей (style.css роутера и коммутатора, значения oklch переведены в RGB)
const uint16_t C_BG = RGB(16, 12, 10), C_FG = RGB(245, 241, 236), C_CARD = RGB(29, 24, 21);
const uint16_t C_PRIMARY = RGB(202, 153, 113), C_PRIMARY_FG = RGB(25, 15, 10), C_SECONDARY = RGB(47, 39, 34);
const uint16_t C_MUTED = RGB(41, 35, 30), C_MUTED_FG = RGB(173, 163, 151), C_BORDER = RGB(62, 54, 48);
const uint16_t C_DANGER = RGB(197, 55, 47), C_OK = RGB(52, 143, 79), C_WARN = RGB(215, 150, 40);
const uint16_t C_LAMP_RED = RGB(235, 30, 24);   // огонёк «в эфире проверочный звук»: ярче, чем C_DANGER, и с тёмным ободком
const uint16_t C_LAMP_GREEN = RGB(40, 210, 84);   // огонёк «в эфире звук источника / файл»: той же яркости и с тем же ободком
const uint8_t RADIUS = 8;
#define F_CAP (&m2Row)     // подписи, «H» 10 точек
#define F_CAPB (&m2RowB)
#define F_TXT (&fTxt)      // основной текст, «H» 13
#define F_TXTB (&fTxtB)
#define F_H2 (&m2Title)    // заголовки
#define F_H (&m2Mid)
#define F_BIG (&m2Big)
#define F_NUM (&fNum)      // крупные числа

const int W = 480, H = 480, HDR = 64, TAB = 72, CY = HDR, CH = H - HDR - TAB;
const int SPEC_BARS = 20;   // было 32; владелец 07.10: «сделай 20 столбцов… это должно облегчить страницу и убрать торможение»

enum Page { PG_HOME, PG_RX, PG_SOUND, PG_AIR, PG_SETUP, PG_HELP, PG_N };
const int TABW = W / PG_N;   // ширина вкладки
const int RX_MAX = 16, RX_ROWS = 5, RX_NAME = 66;   // имя приёмника — до 32 букв (NAME_LONG в proto.h)
enum Param { P_CHANNEL, P_KIT, P_RATE, P_POWER, P_GAIN, P_INPUT, P_TONE, P_BRIGHT, P_DIM, P_QUALITY,
             P_TRACK, P_DUCK_S, P_DUCK_DB, P_VOICE_GAP, P_MUSIC_DB, P_VOICE_DB, P_STEREO, P_HOP, P_LANG, P_DEBUG, P_COUNT };

// Что показывать. Заполняет устройство перед каждым кадром.
struct View {
  int val[P_COUNT] = { 6, 1, 0, 15, 0, 0, 0, 80, 0, 0, 0, 30, -16, 2, 0, 0, 0 };
  // уровень входа (сглаженный); «шапка» — докуда риска недавно доходила; настоящий пик отсчётов (для подсказок)
  float levelDb = -60, holdDb = -60, peakDb = -60;
  int lastTone = 5;                    // какой проверочный звук включится по кнопке «Перевірка» (последний выбранный)
  bool muted = false;                  // тишина в эфире
  bool clip = false, silent = false, radioOk = true;
  uint32_t uptimeS = 0;
  uint16_t pktPerS = 0, refused = 0, restarts = 0;
  float airAvgMs = 0, airMaxMs = 0;
  uint8_t spec[SPEC_BARS] = {};        // риски спектра, 0…200 (уже сглаженные)
  bool sd = false, rec = false;
  bool sdFull = false;                 // на карте не осталось места для записи
  bool fileOn = false;                 // в эфир идёт файл с карты (вместо входа и проверочного звука)
  uint8_t card = 0;                    // почему карты нет: 0 — не отвечает, 2 — не та разметка, 3 — отвечает, но не запускается
  char cardFs[12] = "";                // какая на ней разметка, если не та
  uint32_t recS = 0, sdFreeMb = 0;
  uint8_t chanBusy[13] = {};           // занятость каналов, 0…100
  bool scanned = false, scanning = false;
  uint8_t scanStep = 0;
  char mac[20] = "";
  uint32_t freeHeapK = 0;
  const char *version = "";
  // приёмники, которые сообщали о себе (и те, что запомнены с прошлых включений)
  struct Rx {
    uint8_t id[3];
    char name[RX_NAME];
    bool wide = false;    // прошивка приёмника понимает длинное имя (2.19+); иначе — 13 букв
    bool online, off, mute, stereo;
    bool sleep = false;   // спит: выключен с передатчика (или только что проснулся и ещё не сообщил об этом)
    uint8_t volume, depthMs, lossPm, lostFrames, fw;
    int8_t rssi;
    uint16_t uptimeMin;
    // настройки «для слуха и удобства» (приёмники с 2.32): чёткость, баланс (+5), предел громкости, вид, светодиод, язык
    bool hasInfo = false;
    uint8_t par[6] = {};
    bool earTest = false;   // у приёмника идёт проверка наушников
  } rx[RX_MAX];
  int rxN = 0;
  // приёмники без ключа, которые просят доступ к набору (видны, пока открыто окно «Додати приймач»)
  // сам ушёл с занятого канала (или хотел, да некуда): для сообщения на экране и строки на странице «Ефір»
  char srcName[RX_NAME] = "";          // как назван источник звука (пусто — «З пульта»)
  uint32_t hopSeq = 0;                 // номер события: сменился — показать сообщение
  uint8_t hopResult = 0;               // 1 — перешёл, 2 — свободнее не нашлось, 3 — ищу
  uint8_t hopFrom = 0, hopTo = 0, hopSecs = 0;
  uint16_t hopWaitMs = 0;
  uint32_t hopAgoS = 0;                // сколько секунд назад
  // обновление прошивки приёмников по радио (ota.h): что делает передатчик и каждый приёмник
  struct Ota {
    uint8_t stage = 0;      // OT_… (0 — не идёт)
    uint8_t percent = 0;    // сколько образа уже у самого отстающего приёмника
    uint16_t secs = 0;      // сколько секунд идёт
    uint8_t n = 0, more = 0;
    uint8_t done = 0, fail = 0, same = 0, lost = 0;
    // обновление с карты (upd.h): 0 не смотрели, 1 карты нет, 2 смотрю, 3 файлов нет, 4 та же прошивка, 5 ЕСТЬ, 6 файл не годится,
    // 7 — в файле прошивка старше работающей
    uint8_t card = 0, cardWhy = 0, selfPct = 0;
    uint8_t selfWait = 0;   // сколько секунд до начала записи в передатчик (идёт предупреждение); 0 — запись идёт
    bool fromCard = false, demo = false;
    char cardVer[12] = "", cardFile[40] = "", sendVer[12] = "";
    struct It {
      char name[RX_NAME];
      uint8_t phase, percent, err;   // OS_…, проценты, OE_…
      bool lost;
    } it[4];
  } ota;
  struct Ask {
    char name[RX_NAME];
    uint16_t code;     // четыре цифры — те же, что на экране приёмника
    uint8_t slot;      // место просьбы у устройства
  } ask[3];
  int askN = 0;
};

// Что человек попросил. Устройство выполняет и обнуляет.
struct Out {
  int setParam = -1, setValue = 0;
  bool scan = false, recToggle = false, restart = false, factory = false, touched = false, powerOff = false;
  int mute = -1;                       // попросили включить (1) или выключить (0) тишину в эфире
  // команда приёмнику: 0 — нет; иначе RXC_…
  int rxCmd = 0, rxArg = 0;
  uint8_t rxId[3] = {};
  char rxName[RX_NAME] = "";
  bool setSrcName = false;             // источник звука переименован: новое имя — в srcName (пусто — вернуть «З пульта»)
  char srcName[RX_NAME] = "";
  bool otaStart = false, otaCancel = false;   // обновление приёмников по радио: начать / отменить
  bool otaCard = false, otaDemo = false, updScan = false;   // обновить всё файлом с карты / пробное / просмотреть папку на карте
  int pairOpen = -1;                   // открыть (1) или закрыть (0) приём просьб о доступе
  int pairApprove = -1;                // дозволить доступ: место просьбы
};
enum { RXC_IDENTIFY = 1, RXC_NAME, RXC_ENABLE, RXC_VOLUME, RXC_FORGET, RXC_STEREO,
       RXC_SET,       // настройка приёмника: rxArg = номер настройки × 32 + значение (как в команде CMD_SET)
       RXC_EARTEST }; // проверка наушников: rxArg 1 — начать, 0 — прекратить

// ---------------------------------------------------------------- настройки и пояснения к ним
enum { K_STEP, K_LIST, K_TOGGLE };
struct ParamDef {
  const char *name, *desc;
  uint8_t kind;
  int16_t lo, hi, step;
  const char *unit;
  const char *const *opts;
};
// список скоростей длинный — в окне он стоит в два столбца, поэтому подписи короткие
static const char *const OPT_RATE[] = { "Сама", "24 Мбіт/с", "18 Мбіт/с", "12 Мбіт/с", "6 Мбіт/с", "11 Мбіт/с", "5,5 Мбіт/с", "2 Мбіт/с", "1 Мбіт/с", "0,5 Мбіт/с", nullptr };
static const char *const OPT_QUAL[] = { "Найвища — без стиснення", "Стандартна — зі стисненням", "Мова — лише смуга голосу", "Дальня — для слабкого сигналу", nullptr };
static const char *const SHORT_QUAL[] = { "найвища", "стандартна", "мова", "дальня" };
// список мелодий заполняет устройство: «По черзі», затем названия из памяти; конец списка — nullptr
static const char *OPT_TRACK[9] = { "По черзі", "Мелодія 1", "Мелодія 2", nullptr };
static const char *const OPT_STEREO[] = { "Моно", "Стерео", nullptr };
static const char *const OPT_INPUT[] = { "Вбудований вхід", "Модуль PCM1808", nullptr };
static const char *const OPT_LANG[] = { "Українська", "English", nullptr };
static const char *const OPT_HOP[] = { "Не міняти — лишатись на своєму каналі", "Міняти самому на вільніший", nullptr };
static const char *const OPT_TEST[] = { "Вимкнено — іде звук із входу", "Тон 1000 Гц", "Голос: «Увага! Йде перевірка звуку»", "Голос і музика по колу",
                                        "Музика", "Музика, голос поверх", "Канали: лівий — гудок, правий — два", nullptr };
// коротко — для узкой кнопки «Який звук» рядом с переключателем источника
static const char *const TEST_COMPACT[] = { "тон", "тон", "голос", "голос+музика", "музика", "музика+голос", "канали" };
static const char *const SHORT_TEST[] = { "вимкнено", "тон", "голос", "голос і музика", "музика", "музика з голосом", "перевірка каналів" };
static const char *const OPT_DIM[] = { "Ніколи", "Через 1 хвилину", "Через 5 хвилин", "Через 15 хвилин", nullptr };
static const char *const SHORT_INPUT[] = { "вбудований", "PCM1808" };
static const char *const SHORT_DIM[] = { "ніколи", "1 хв", "5 хв", "15 хв" };
static const ParamDef PARAM[P_COUNT] = {
  { "Канал", "Радіоканал, на якому йде передача. Приймачі знаходять його самі. Якщо звук переривається — спробуйте інший або «Знайти вільний канал».", K_STEP, 1, 13, 1, "", nullptr },
  { "Набір", "Номер вашого комплекту. Приймачі отримують його від передавача самі. Міняйте, якщо поруч працює ще один такий набір.", K_STEP, 1, 99, 1, "", nullptr },
  { "Швидкість радіо", "Найнадійніше — 5,5 Мбіт/с і нижче: що менша швидкість, то далі чути. На 6–24 Мбіт/с пакети губляться навіть поруч із приймачем — не радимо. Для 2 Мбіт/с і нижче передавач сам знизить якість звуку. «Сама» — обирає від 5,5 униз за сигналом приймачів.", K_LIST, 0, 9, 1, "", OPT_RATE },
  { "Потужність", "Сила сигналу передавача. У малому залі можна зменшити — менше завад сусідам.", K_STEP, 2, 20, 1, " дБм", nullptr },
  { "Підсилення входу", "Додає гучності тихому джерелу. Краще виставити рівень на пульті, а тут лишити 0.", K_STEP, -12, 24, 3, " дБ", nullptr },
  { "Вхід з пульта", "Яким входом передавач приймає звук з пульта. Вбудований вхід працює одразу; модуль PCM1808 дає чистіший звук і стерео, але його треба під'єднати.", K_LIST, 0, 1, 1, "", OPT_INPUT },
  { "Перевірочний звук", "Замість входу в ефір іде голос, музика або тон — щоб перевірити приймачі без пульта. «Канали» — перевірка стерео: один гудок ліворуч, два праворуч.", K_LIST, 0, 6, 1, "", OPT_TEST },
  { "Яскравість екрана", "Яскравість підсвітки цього екрана.", K_STEP, 10, 100, 10, " %", nullptr },
  { "Гасити екран", "Через який час без дотиків екран пригасне. Передача при цьому триває. Дотик повертає яскравість.", K_LIST, 0, 3, 1, "", OPT_DIM },
  { "Якість звуку", "Що нижча якість, то менший потік: пакети рідші, запасні копії перекривають довшу заваду, і працюють повільні, далекобійні швидкості радіо. Плата — затримка: пакет триває 2, 4, 8 або 12 мс.", K_LIST, 0, 3, 1, "", OPT_QUAL },
  { "Мелодія", "Яку мелодію грати в режимах із музикою. «По черзі» — одна за одною.", K_LIST, 0, 7, 1, "", OPT_TRACK },
  { "Оголошення кожні", "Як часто в режимі «Музика, голос поверх» музика стихає і звучить голос.", K_STEP, 10, 240, 10, " с", nullptr },
  { "Музика під голосом", "Наскільки тихішою стає музика, поки говорить голос. −16 дБ — добре чути і голос, і музику; −40 дБ — музики майже не чути.", K_STEP, -40, -6, 2, " дБ", nullptr },
  { "Повтор голосу через", "Пауза між повторами оголошення в режимі «Голос».", K_STEP, 1, 60, 1, " с", nullptr },
  { "Гучність музики", "Гучність мелодій у перевірочному звуці. 0 дБ — як записано.", K_STEP, -30, 0, 3, " дБ", nullptr },
  { "Гучність голосу", "Гучність оголошення в перевірочному звуці. 0 дБ — як записано.", K_STEP, -30, 0, 3, " дБ", nullptr },
  { "Ефір", "Стерео йде в ефір, коли джерело стереофонічне: мелодії перевірки або модуль PCM1808. Приймач грає стерео, якщо його вихід — «два канали» (навушники напряму); у «протифазі» (для підсилювача) — моно. У якості «дальня» завжди моно.", K_LIST, 0, 1, 1, "", OPT_STEREO },
  { "Канал при заторі", "Коли ефір на каналі зайнятий так, що пакети довго чекають (звук рветься), передавач сам замовкає на дві секунди, міряє всі канали й переходить на вільніший. Приймачі знаходять його самі. Про перехід і причину буде повідомлення.", K_LIST, 0, 1, 1, "", OPT_HOP },
  { "Мова / Language", "Мова написів на передавачі й на приймачах та голос оголошення в перевірочному звуці. Language of the transmitter and receivers and the voice of the test announcement.", K_LIST, 0, 1, 1, "", OPT_LANG },
  { "Налагодження ефіром", "Рядки порту всіх пристроїв набору видно в порту того, що підключений до комп'ютера; команди іншим пристроям — теж ефіром. Для налагодження без дротів до приймачів.", K_TOGGLE, 0, 1, 1, "", nullptr },
};

static const char *rateNowText = "";   // на какой скорости передатчик работает сейчас (заполняет устройство)
static int qualNow = -1;               // какое качество сейчас идёт в эфир (ниже выбранного, если оно не помещается в скорость)
static bool stereoNow = false;         // в эфир сейчас идёт стерео (источник может быть и моно)
static void paramText(int p, int v, char *s, size_t n) {
  switch (p) {
    case P_RATE:
      if (v == 0 && rateNowText[0]) snprintf(s, n, tr("сама (%s)"), rateNowText);
      else if (rateNowText[0]) snprintf(s, n, tr("%s Мбіт/с"), rateNowText);
      else snprintf(s, n, "%s", v == 0 ? tr("сама") : OPT_RATE[v < 0 || v > 9 ? 0 : v]);
      break;
    case P_QUALITY:
      if (qualNow >= 0 && qualNow <= 3 && qualNow != v) snprintf(s, n, tr("%s — за швидкістю"), tr(SHORT_QUAL[qualNow]));
      else snprintf(s, n, "%s", SHORT_QUAL[v < 0 || v > 3 ? 0 : v]);
      break;
    case P_INPUT: snprintf(s, n, "%s", SHORT_INPUT[v ? 1 : 0]); break;
    case P_DIM: snprintf(s, n, "%s", SHORT_DIM[v < 0 || v > 3 ? 0 : v]); break;
    case P_TONE: snprintf(s, n, "%s", SHORT_TEST[v < 0 || v > 6 ? 0 : v]); break;
    case P_HOP: snprintf(s, n, "%s", v ? tr("сам") : tr("ні")); break;
    case P_LANG: snprintf(s, n, "%s", v ? "English" : "Українська"); break;
    case P_STEREO:
      if (v && !stereoNow) snprintf(s, n, tr("стерео (зараз моно)"));
      else snprintf(s, n, "%s", v ? tr("стерео") : tr("моно"));
      break;
    case P_TRACK: {
      int k = 0;
      while (k < 8 && OPT_TRACK[k]) k++;
      snprintf(s, n, "%s", v <= 0 || v >= k ? tr("по черзі") : OPT_TRACK[v]);
      break;
    }
    case P_GAIN: snprintf(s, n, tr("%s%d дБ"), v > 0 ? "+" : "", v); break;
    default: snprintf(s, n, "%d%s", v, tr(PARAM[p].unit));
  }
}

// ---------------------------------------------------------------- справка
struct Topic {
  const char *title, *text;
};
static const Topic HELP[] = {
  { "Як підключити звук",
    "Звук беруть із лінійного виходу пульта (AUX, MONITOR або REC OUT) — не з виходу на колонки.\n"
    "Між пультом і передавачем ставте розв'язку (ізолятор «земляної петлі»): без неї може гудіти.\n"
    "Після підключення дивіться на сторінку «Головна»: смужка рівня має рухатись у такт звуку." },
  { "Рівень входу",
    "Смужка на головній сторінці показує гучність на вході.\n"
    "Зелена зона — добре. Жовта — гучно, але ще чисто. Червона — перевантаження: звук у навушниках хрипить.\n"
    "Рівень виставляють на пульті так, щоб найгучніші місця доходили до жовтої зони, але не до червоної.\n"
    "Якщо джерело тихе і на пульті додати нічим — додайте «Підсилення входу» на сторінці «Звук»." },
  { "Канал і набір",
    "Канал — це «доріжка» в ефірі Wi-Fi. Передавач сидить на одному каналі, приймачі знаходять його самі.\n"
    "Якщо звук у навушниках переривається, на сторінці «Ефір» натисніть «Знайти вільний канал»: передавач за дві секунди перевірить усі канали й стане на найвільніший.\n"
    "Коли ефір зайнятий надовго, передавач сам переходить на вільніший канал і пише про це на екрані (вимикається там само: «Канал при заторі»).\n"
    "Набір — номер вашого комплекту; приймачі отримують його від передавача самі. Якщо поруч працює другий такий комплект, дайте їм різні номери." },
  { "Приймачі",
    "Новий приймач треба додати до набору: «Приймачі» › «Додати приймач», звірити код і дозволити. Далі він сам знаходить передавач. Чужий приймач звуку не почує.\n"
    "Ручка — гучність. Натиск на ручку — тиша. Довгий натиск — меню приймача, у меню довгий натиск — назад.\n"
    "Без сигналу приймач за 10 секунд засинає і прокидається сам, коли передавач з'явиться. Видалити приймач — у його вікні «Видалити з набору»." },
  { "Запис на картку",
    "Якщо в передавачі є картка пам'яті, усе, що йде в ефір, можна записувати у файл WAV.\n"
    "Кнопка запису — на сторінці «Головна». Година запису займає близько 230 МБ.\n"
    "Картка — microSD з розміткою FAT32; exFAT передавач не читає. «Картка не відповідає» — її немає або немає контакту.\n"
    "Перед тим як виймати картку, зупиніть запис.\n"
    "«Файли» поруч із кнопкою запису відкривають картку: теки й файли, пуск файлу в ефір (WAV або MP3), копіювання, видалення, нова тека, форматування." },
  { "Якщо щось не так",
    "Тиша в навушниках: перевірте смужку рівня на «Головній». Якщо вона стоїть — звук не доходить від пульта.\n"
    "Для перевірки без пульта ввімкніть «Перевірочний звук» на сторінці «Звук» — у навушниках буде голос і музика.\n"
    "Звук переривається: «Ефір» › «Знайти вільний канал»; поставте передавач вище, ближче до людей.\n"
    "Не допомогло — на сторінці «Звук» знизьте «Якість звуку»: передача стане стійкішою, а на «Ефірі» можна буде взяти меншу швидкість радіо.\n"
    "Хрипить: рівень у червоній зоні — зменшіть на пульті." },
};
const int HELP_N = sizeof(HELP) / sizeof(HELP[0]);

// ---------------------------------------------------------------- состояние меню
struct Box {
  int16_t x, y, w, h;
  bool has(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};
// номера элементов, на которые можно нажать
constexpr int ID_NONE = -1;
constexpr int ID_TAB = 0;                      // +номер страницы
constexpr int ID_HOME_CH = 10, ID_HOME_KIT = 11, ID_HOME_REC = 12, ID_HOME_RX = 13;
constexpr int ID_HOME_FILES = 14;                   // «Файли» на плитке карты — открывает файловый менеджер
constexpr int ID_ROW = 20;                     // +номер настройки
constexpr int ID_SCAN = 40, ID_ABOUT = 41, ID_RESTART = 42, ID_FACTORY = 43, ID_POWEROFF = 44;
constexpr int ID_SRC_LIVE = 45, ID_SRC_TEST = 46;   // что идёт в эфир: звук с пульта / проверочный звук
constexpr int ID_MUTE = 47;                         // тишина в эфире
constexpr int ID_SRCNAME = 49;                      // «Назва джерела звуку» на странице настроек — открывает клавиатуру
constexpr int ID_POWER = 48;                        // выключение передатчика — кнопка в шапке, посередине (на всех страницах)
constexpr int ID_HELP = 50;                    // +номер темы
constexpr int ID_ART_BACK = 60, ID_ART_PREV = 61, ID_ART_NEXT = 62;
constexpr int ID_M_MINUS = 70, ID_M_PLUS = 71, ID_M_OK = 72, ID_M_CANCEL = 73;
constexpr int ID_M_OPT = 80;                   // +номер варианта
constexpr int ID_RXROW = 90;                   // +строка списка приёмников
constexpr int ID_RX_PREV = 96, ID_RX_NEXT = 97;
constexpr int ID_RX_IDENT = 100, ID_RX_RENAME = 101, ID_RX_VOLDN = 102, ID_RX_VOLUP = 103, ID_RX_FORGET = 104, ID_RX_CLOSE = 105, ID_RX_ONOFF = 106;
constexpr int ID_RX_STEREO = 107;
constexpr int ID_RX_SET = 108;                 // «Налаштування» в окне приёмника — открывает окно его настроек
constexpr int ID_RXS_MINUS = 140, ID_RXS_PLUS = 150;   // + строка окна настроек приёмника (0…6)
constexpr int ID_RXS_EAR = 160, ID_RXS_OK = 161;
constexpr int ID_SHEET = 110;                  // +строка окна «Перевірочний звук»
constexpr int ID_OTA_DEMO = 133;   // «Пробне» — пробное обновление: приёмники принимают и сверяют, но не записывают
constexpr int ID_OTA_OPEN = 130, ID_OTA_START = 131, ID_OTA_CLOSE = 132;   // «Оновлення» приёмников по радио: открыть окно, начать, закрыть/отменить
constexpr int ID_PAIR_OPEN = 120, ID_PAIR_CLOSE = 121, ID_PAIR_OK = 122;   // «Додати приймач»; ID_PAIR_OK + номер просьбы — «Дозволити»
enum Modal { M_NONE, M_PARAM, M_ABOUT, M_CONFIRM, M_RX, M_KEYS, M_SHEET, M_PAIR, M_FILES, M_OTA, M_RXSET };
// окно «Перевірочний звук»: все настройки проверочного звука одним списком
static const int SHEET[] = { P_TONE, P_TRACK, P_DUCK_S, P_DUCK_DB, P_VOICE_GAP, P_MUSIC_DB, P_VOICE_DB };
const int SHEET_N = sizeof(SHEET) / sizeof(SHEET[0]);
enum Confirm { CF_SCAN, CF_RESTART, CF_FACTORY, CF_REC_STOP, CF_POWEROFF, CF_FORGET, CF_TEST };

// Всё рисование идёт не на экран, а в «теневой» лист — точную копию экрана в памяти. Рисовалка сама запоминает,
// какой прямоугольник тронули; в конце кадра (и перед каждой паузой анимации) он одним махом переносится
// на экран. Поэтому на экране никогда не видно промежуточных состояний («залили фон — потом нарисовали»):
// точки, которые не изменились, переписываются тем же цветом, и глаз видит только настоящие изменения.
struct DGfx : m2::Gfx {
  // Тронутые места запоминаются несколькими прямоугольниками: если держать один общий, огонёк в шапке
  // и значок вкладки внизу давали бы прямоугольник почти во весь экран (так и было: кадр 16 мс вместо 0,4).
  struct R {
    int x0, y0, x1, y1;
  } rc[48];
  int rn = 0;
  void mark(float x, float y, float w, float h) {
    R n = { (int)x - 1, (int)y - 1, (int)(x + w) + 2, (int)(y + h) + 2 };
    // Вплотную к уже тронутому — расширить его. Склеивать «рядом» (было: ближе 12 точек) нельзя: 32 риски спектра
    // стоят через 13 точек, сливались в одну область во всю карточку, и на экран каждый раз переносилось 40 тысяч
    // точек — на вкладке «Звук» это и тормозило меню (06.10).
    for (int i = 0; i < rn; i++) {
      R &r = rc[i];
      if (n.x0 <= r.x1 + 1 && n.x1 >= r.x0 - 1 && n.y0 <= r.y1 + 1 && n.y1 >= r.y0 - 1) {
        if (n.x0 < r.x0) r.x0 = n.x0;
        if (n.y0 < r.y0) r.y0 = n.y0;
        if (n.x1 > r.x1) r.x1 = n.x1;
        if (n.y1 > r.y1) r.y1 = n.y1;
        return;
      }
    }
    if (rn < 48) {
      rc[rn++] = n;
      return;
    }
    R &r = rc[47];   // мест нет — влить в последний
    if (n.x0 < r.x0) r.x0 = n.x0;
    if (n.y0 < r.y0) r.y0 = n.y0;
    if (n.x1 > r.x1) r.x1 = n.x1;
    if (n.y1 > r.y1) r.y1 = n.y1;
  }
  void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    m2::Gfx::fill(x, y, w, h, c);
    mark(x, y, w, h);
  }
  void fillA(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c, uint8_t a) {
    m2::Gfx::fillA(x, y, w, h, c, a);
    mark(x, y, w, h);
  }
  void box(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t r, uint16_t c) {
    m2::Gfx::box(x, y, w, h, r, c);
    mark(x, y, w, h);
  }
  void circle(float cx, float cy, float r, uint16_t c) {
    m2::Gfx::circle(cx, cy, r, c);
    mark(cx - r, cy - r, 2 * r, 2 * r);
  }
  void line(float x0, float y0, float x1, float y1, float wd, uint16_t c) {
    m2::Gfx::line(x0, y0, x1, y1, wd, c);
    float ax = x0 < x1 ? x0 : x1, ay = y0 < y1 ? y0 : y1, bx = x0 < x1 ? x1 : x0, by = y0 < y1 ? y1 : y0;
    mark(ax - wd, ay - wd, bx - ax + 2 * wd, by - ay + 2 * wd);
  }
  void arc(float cx, float cy, float r, float wd, uint16_t c, float a0 = 0, float a1 = 360) {
    m2::Gfx::arc(cx, cy, r, wd, c, a0, a1);
    mark(cx - r - wd, cy - r - wd, 2 * (r + wd), 2 * (r + wd));
  }
  void poly(const float *xy, uint8_t n, uint16_t c) {
    m2::Gfx::poly(xy, n, c);
    float ax = xy[0], ay = xy[1], bx = xy[0], by = xy[1];
    for (int i = 1; i < n; i++) {
      if (xy[2 * i] < ax) ax = xy[2 * i];
      if (xy[2 * i] > bx) bx = xy[2 * i];
      if (xy[2 * i + 1] < ay) ay = xy[2 * i + 1];
      if (xy[2 * i + 1] > by) by = xy[2 * i + 1];
    }
    mark(ax, ay, bx - ax, by - ay);
  }
  void blit(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *px) {
    m2::Gfx::blit(x, y, w, h, px);
    mark(x, y, w, h);
  }
  int16_t text(int16_t x, int16_t baseline, const char *utf8, const GFXfont *f, uint16_t c, uint8_t align = m2::AL_L, int16_t maxw = 0) {
    utf8 = tr(utf8);   // все надписи идут через перевод (см. lang.h): при украинском — как есть
    int16_t w = m2::Gfx::text(x, baseline, utf8, f, c, align, maxw);
    int ww = maxw > w ? maxw : w;
    mark(align == m2::AL_L ? x : align == m2::AL_C ? x - ww / 2 : x - ww, baseline - f->yAdvance, ww + 2, f->yAdvance + f->yAdvance / 2);
    return w;
  }
};
static DGfx g;
static uint16_t *fbMain, *fbShadow;      // экран и его теневая копия (если её нет — рисуем прямо на экран)
static uint16_t *drawBuf() {
  return fbShadow ? fbShadow : fbMain;
}
// перенести на экран то, что изменилось с прошлого раза
static uint32_t flushPx, fullDraws;     // сколько точек перенесено и сколько раз страница рисовалась целиком (для проверки)
// Копия прямоугольника из теневого листа в основной. Оба листа — во внешней памяти, и та же память с той же шиной
// кормит экран: порцию кадра процессор заранее подгружает в кэш (panel4848.h). Большая копия одним махом забивает шину
// и вытесняет подгруженное — несколько порций кадра подряд готовятся втрое дольше, и картинка может дёрнуться. Поэтому
// копия больше ~8 КБ идёт с передышками: строка-две — и 70 мкс шине на подгрузку. Вся страница так копируется ~45 мс
// вместо ~20; на глаз это ничего не меняет (копия идёт в лист, которого в этот миг либо не видно, либо он совпадает).
static void copyRows(int x0, int y0, int x1, int y1) {
  int w = x1 - x0, every = w * (y1 - y0) > 4096 ? (w > 240 ? 1 : 2) : 0;
  for (int y = y0; y < y1; y++) {
    memcpy(fbMain + y * 480 + x0, fbShadow + y * 480 + x0, w * 2);
    if (every && ((y - y0) % every) == every - 1) uiBreath();
  }
}
static void flush() {
  for (int i = 0; i < g.rn && fbShadow; i++) {
    int x0 = g.rc[i].x0 < 0 ? 0 : g.rc[i].x0, y0 = g.rc[i].y0 < 0 ? 0 : g.rc[i].y0;
    int x1 = g.rc[i].x1 > 480 ? 480 : g.rc[i].x1, y1 = g.rc[i].y1 > 480 ? 480 : g.rc[i].y1;
    if (x1 <= x0 || y1 <= y0) continue;
    copyRows(x0, y0, x1, y1);
    flushPx += (x1 - x0) * (y1 - y0);
  }
  g.rn = 0;
}
static void pause(int ms) {   // пауза между кадрами анимации: сначала показать нарисованное
  flush();
  uiDelay(ms);
}
static View view;
static Out out;
static int page = PG_HOME, modal = M_NONE, modalArg = 0, article = -1, articlePage = 0, articlePages = 1;
static int modalValue = 0;
static bool sheetBack = false;     // окно настройки открыто из списка «Перевірочний звук» — вернуться в него
static bool needFull = true, animate = true;
// нажатие
static bool wasDown = false;
static uint32_t touchMs = 0;   // время события сенсора, которое сейчас разбирает кадр (ставит задача экрана)
static int pressId = ID_NONE, pressX = 0, pressY = 0;
static uint32_t pressMs = 0, repeatMs = 0;
// что уже нарисовано из живого (чтобы не перерисовывать без нужды)
static int shownPill = -1, shownHint = -1, shownNum = -999, shownUp = -1, shownRec = -1, shownScan = -1, shownStat = -1, shownRxN = -1;
static uint32_t shownRxSum = 0;
static int rxPage = 0, rxSel = 0;                 // страница списка приёмников и выбранный приёмник
static uint8_t rxSelId[3];
// экранная клавиатура
static char kbText[RX_NAME] = "";
static bool kbShift = true, kbDigits = false;
static bool kbForSource = false;   // клавиатура открыта для имени источника звука (иначе — для имени приёмника)
static bool kbForDir = false;      // …или для имени новой папки на карте (файловый менеджер)
static void fmNewDir(const char *name);
static void fmOpen();
static int kbPressed = -1;
static float toggleAnim = 0;
static uint32_t frameNo = 0;
static bool toastOn = false;                    // всплывающее сообщение сейчас на экране

struct Hot {
  Box b;
  int id;
};
static Hot hot[32];
static int hotN = 0;

// ---------------------------------------------------------------- живые огоньки
// Правило владельца (07.10): «все индикации работы функций должны показывать активность (ненавязчивая анимация
// работы)». Огонёк, который означает «это сейчас работает», не стоит неподвижно, а медленно «дышит»: чуть меньше и
// бледнее — чуть больше и ярче, полтора вдоха в секунду. Кто рисует такой огонёк, тот записывает его сюда (lampSet);
// дальше его оживляет общий кадр (lampsAnim). Кнопку нажали или состояние ушло — lampDrop. Полная перерисовка
// страницы начинается с чистого списка. В окнах поверх страницы огоньки не трогаются.
// Уже живые без этого: огонёк «В ЕФІРІ» в шапке, точка записи, приёмники на связи, уровень, спектр, значки вкладок.
struct Lamp {
  int16_t id;
  float x, y, r;
  uint16_t c, ring;   // цвет огонька и того, что под ним (или тёмного ободка)
};
static Lamp lamps[8];
static int lampN = 0;
enum { LAMP_HINT = 900, LAMP_MUTE = 901 };
static void lampDrop(int id) {
  for (int i = 0; i < lampN; i++)
    if (lamps[i].id == id) lamps[i--] = lamps[--lampN];
}
static void lampSet(int id, float x, float y, float r, uint16_t c, uint16_t ring) {
  lampDrop(id);
  if (lampN < 8) lamps[lampN++] = Lamp{ (int16_t)id, x, y, r, c, ring };
}
static void lampDraw(const Lamp &l, float k);   // k: 0 — пригас, 1 — горит в полную силу
static void lampsAnim() {
  float t = millis() / 1000.0f;
  for (int i = 0; i < lampN; i++) lampDraw(lamps[i], 0.5f + 0.5f * sinf(t * 4.2f + i * 1.7f));
}
// Три точки «идёт работа»: по ним пробегает свет. Для ожидания, у которого нет хода в процентах.
static void waitDots(int x, int y, uint16_t bg);

// ---------------------------------------------------------------- мелкие помощники
static void card(const Box &b, uint16_t fill = C_CARD, uint16_t edge = C_BORDER) {
  g.box(b.x, b.y, b.w, b.h, RADIUS, edge);
  g.box(b.x + 1, b.y + 1, b.w - 2, b.h - 2, RADIUS - 1, fill);
}

// Текст с переносом по словам. Рисует строки с first по first+maxLines−1, возвращает общее число строк.
static int wrap(int x, int y, int w, int lineH, const char *text, const GFXfont *f, uint16_t c, int maxLines = 99, int first = 0, bool draw = true) {
  char line[160];
  text = tr(text);
  int n = 0, lines = 0;
  const char *p = text;
  line[0] = 0;
  auto flush = [&]() {
    if (draw && lines >= first && lines < first + maxLines) g.text(x, y + (lines - first) * lineH, line, f, c);
    lines++;
    n = 0;
    line[0] = 0;
  };
  while (*p) {
    if (*p == '\n') {
      flush();
      p++;
      continue;
    }
    const char *e = p;
    while (*e && *e != ' ' && *e != '\n') e++;
    int wl = (int)(e - p);
    if (n + wl + 1 >= (int)sizeof(line)) flush();
    int keep = n;
    if (n) line[n++] = ' ';
    memcpy(line + n, p, wl);
    n += wl;
    line[n] = 0;
    if (keep && m2::Gfx::textW(line, f) > w) {   // слово не влезло — на новую строку
      line[keep] = 0;
      flush();
      memcpy(line, p, wl);
      n = wl;
      line[n] = 0;
    }
    p = e;
    while (*p == ' ') p++;
  }
  if (n) flush();
  return lines;
}

static void fmtTime(uint32_t s, char *t, size_t n) {
  snprintf(t, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
}

// ---------------------------------------------------------------- шапка и вкладки
static int pillState() {
  return !view.radioOk ? 4 : view.muted ? 5 : view.val[P_TONE] ? 3 : view.clip ? 1 : view.silent ? 2 : 0;
}

// Плашка состояния в шапке. Слева в ней — огонёк: «в эфире» он ровно дышит, при проверочном звуке бьётся
// чаще, при перегрузке мигает.
static int pillX = 0;                 // левый край плашки (для огонька)
static const uint16_t PILL_COL[] = { C_OK, C_DANGER, C_SECONDARY, C_WARN, C_DANGER, C_DANGER };
static void drawPill(int st) {
  static const char *const TXT[] = { "В ЕФІРІ", "ПЕРЕВАНТАЖЕННЯ", "ТИХО НА ВХОДІ", "ПЕРЕВІРКА ЗВУКУ", "РАДІО МОВЧИТЬ", "ТИША В ЕФІРІ" };
  g.fill(258, 12, 222, 40, C_BG);   // левее — кнопка выключения
  int w = trW(TXT[st], F_CAPB) + 48;
  pillX = 464 - w;
  g.box(pillX, 15, w, 34, 17, PILL_COL[st]);
  g.text(pillX + 30 + (w - 44) / 2, 38, TXT[st], F_CAPB, st == 2 ? C_MUTED_FG : st == 3 ? C_PRIMARY_FG : C_FG, AL_C);
  shownPill = st;
}
static void pillLamp(int st, float t) {
  float k = st == 0 ? 0.5f + 0.5f * sinf(t * 2.6f) : st == 3 ? 0.5f + 0.5f * sinf(t * 7) : st == 1 || st == 4 ? ((int)(t * 4) & 1) : st == 5 ? ((int)(t * 2) & 1) : 0.25f;
  uint16_t ink = st == 3 ? C_PRIMARY_FG : st == 2 ? C_MUTED_FG : C_FG;
  g.circle(pillX + 17, 32, 6.5f, PILL_COL[st]);                                   // стереть прежний огонёк
  g.circle(pillX + 17, 32, 3.2f + 2.3f * k, m2::Gfx::blend(PILL_COL[st], ink, (uint8_t)(110 + 145 * k)));
}

// Кнопка выключения — в шапке посередине (владелец 07.10): красный кружок с белым значком «питание».
// Красный — чтобы её было видно сразу и не спутать ни с чем рядом («значок выключения должен отличаться от
// остального рядом по цвету»): больше в шапке красного нет, пока всё в порядке.
static void drawPower(bool pressed) {
  const float cx = 238, cy = 32;
  const uint16_t WHITE = RGB(255, 255, 255);
  g.circle(cx, cy, 17.5f, C_BG);
  g.circle(cx, cy, 17, pressed ? WHITE : C_DANGER);
  uint16_t ink = pressed ? C_DANGER : WHITE;
  g.arc(cx, cy + 1, 8, 2.6f, ink, 35, 325);              // кольцо с разрывом сверху
  g.box(cx - 1.3f, cy - 11, 2.6f, 10.5f, 1.3f, ink);     // чёрточка в разрыве
}

static void drawHeader() {
  static const char *const SUB[PG_N] = { "передавач звуку", "приймачі", "звук на вході", "радіоефір", "налаштування", "довідка" };
  g.fill(0, 0, W, HDR - 1, C_BG);
  g.fill(0, HDR - 1, W, 1, C_BORDER);
  g.blit(14, 6, LOGO_SM_W, LOGO_SM_H, LOGO_SM);
  g.text(82, 30, tr("ВІДРОДЖЕННЯ"), F_H2, C_FG);
  g.text(82, 51, SUB[page], F_CAP, C_MUTED_FG);
  drawPill(pillState());
  drawPower(false);
}

// Значки вкладок нарисованы фигурами, поэтому могут двигаться. sc — размер (1 — обычный; при выборе вкладки
// значок на треть секунды «подпрыгивает»), t — секунды с момента выбора (меньше нуля — значок неподвижен):
//   наушники — от чашек расходятся волны, если приёмники на связи; «Звук» — риски пляшут;
//   «Ефір» — дуги загораются одна за другой; «Налашт.» — бегунки переезжают; дом и вопрос — качаются при выборе.
static bool tabLive = false;          // есть что показывать движением (приёмники на связи / идёт передача)
static void tabIcon(int i, int cx, int cy, uint16_t c, float sc = 1, float t = -1) {
  auto X = [&](float dx) { return cx + dx * sc; };
  auto Y = [&](float dy) { return cy + dy * sc; };
  uint16_t dim = m2::Gfx::blend(C_CARD, c, 90);
  switch (i) {
    case PG_HOME: {
      float sway = t >= 0 && t < 0.6f ? sinf(t * 21) * (0.6f - t) * 3 : 0;   // крыша качнулась
      const float roof[] = { X(-13), Y(-1), X(sway), Y(-13), X(13), Y(-1) };
      g.poly(roof, 3, c);
      g.box(X(-9), Y(-2), 18 * sc, 13 * sc, 2, c);
      g.fill(X(-3), Y(3), 6 * sc, 8 * sc, C_CARD);
      break;
    }
    case PG_RX:   // наушники
      g.arc(cx, Y(2), 11 * sc, 2.5f * sc, c, 270, 90);
      g.box(X(-14), Y(1), 7 * sc, 12 * sc, 3, c);
      g.box(X(7), Y(1), 7 * sc, 12 * sc, 3, c);
      if (t >= 0 && tabLive) {   // волны от чашек
        float ph = fmodf(t * 1.4f, 1.0f);
        uint16_t w = m2::Gfx::blend(C_CARD, c, (uint8_t)(255 * (1 - ph)));
        g.arc(X(-11), Y(7), (5 + 6 * ph) * sc, 1.6f, w, 215, 325);
        g.arc(X(11), Y(7), (5 + 6 * ph) * sc, 1.6f, w, 35, 145);
      }
      break;
    case PG_SOUND: {
      static const int8_t hgt[] = { 8, 18, 26, 14, 22, 10 };
      for (int k = 0; k < 6; k++) {
        float h = hgt[k];
        if (t >= 0) h = 6 + 20 * (0.5f + 0.5f * sinf(t * (4.3f + k * 0.9f) + k * 1.7f));
        g.box(X(-14 + k * 5), Y(12 - h), 3 * sc, h * sc, 1, c);
      }
      break;
    }
    case PG_AIR: {
      float ph = t >= 0 && tabLive ? fmodf(t * 1.1f, 1.0f) : -1;
      g.circle(cx, Y(4), 3.5f * sc, c);
      g.fill(X(-1), Y(4), 3 * sc, 9 * sc, c);
      g.arc(cx, Y(4), 9 * sc, 2.5f * sc, ph < 0 || ph > 0.25f ? c : dim, 300, 60);
      g.arc(cx, Y(4), 15 * sc, 2.5f * sc, ph < 0 || ph > 0.5f ? c : dim, 305, 55);
      break;
    }
    case PG_SETUP:
      for (int k = 0; k < 3; k++) {
        static const int8_t knob[] = { 6, -5, 2 };
        float mv = t >= 0 && t < 1.2f ? sinf(t * 9 + k * 2.1f) * (1.2f - t) * 6 : 0;   // бегунки переехали и встали
        g.box(X(-13), Y(-9 + k * 9), 26 * sc, 3 * sc, 1, c);
        g.circle(X(knob[k] + mv), Y(-7.5f + k * 9), 4.5f * sc, c);
      }
      break;
    case PG_HELP: {
      float tilt = t >= 0 && t < 0.7f ? sinf(t * 18) * (0.7f - t) * 4 : 0;
      g.arc(cx, cy, 12 * sc, 2.5f * sc, c);
      g.text(cx + tilt, cy + 5, "?", F_CAPB, c, AL_C);
      break;
    }
  }
}

// Значок выбранной вкладки — живой: перерисовывается 25 раз в секунду на своём пятачке.
static uint32_t tabSince = 0;         // когда выбрали текущую вкладку
static void tabAnim() {
  float t = (millis() - tabSince) / 1000.0f;
  float sc = t < 0.33f ? 1 + 0.28f * sinf(t / 0.33f * PI) : 1;   // «подпрыгнул» и вернулся
  int cx = page * TABW + TABW / 2, cy = H - TAB + 28;
  g.fill(cx - 26, cy - 22, 52, 42, C_CARD);
  tabIcon(page, cx, cy, C_PRIMARY, sc, t);
}

static void drawTabs() {
  static const char *const NAME[PG_N] = { "Головна", "Приймачі", "Звук", "Ефір", "Налашт.", "Довідка" };
  g.fill(0, H - TAB, W, TAB, C_CARD);
  g.fill(0, H - TAB, W, 1, C_BORDER);
  for (int i = 0; i < PG_N; i++) {
    int x = i * TABW;
    uint16_t c = i == page ? C_PRIMARY : C_MUTED_FG;
    if (i == page) g.box(x + TABW / 2 - 22, H - TAB, 44, 3, 1, C_PRIMARY);
    tabIcon(i, x + TABW / 2, H - TAB + 28, c);
    g.text(x + TABW / 2, H - 10, NAME[i], F_CAP, c, AL_C);
  }
}

// ---------------------------------------------------------------- размеры и места
// Главная страница сверху вниз: переключатель источника, уровень, три плитки, запись.
const int SW_Y = 72, SW_H = 48, LV_Y = 128, LV_H = 118, TL_Y = 254, TL_H = 72, RC_Y = 334, RC_H = 66;
static const Box METER = { 34, LV_Y + 38, 412, 28 };
static Box rowBox(int k, int y0 = 76, int h = 52, int gap = 8) {
  return Box{ 16, (int16_t)(y0 + k * (h + gap)), 448, (int16_t)h };
}
static Box itemBox(int id) {
  if (id >= ID_TAB && id < ID_TAB + PG_N) return Box{ (int16_t)((id - ID_TAB) * TABW), (int16_t)(H - TAB), (int16_t)TABW, TAB };
  if (id >= ID_RXROW && id < ID_RXROW + RX_ROWS) return rowBox(id - ID_RXROW, 72, 54, 4);
  switch (id) {
    case ID_HOME_CH: return Box{ 16, TL_Y, 141, TL_H };
    case ID_HOME_KIT: return Box{ 169, TL_Y, 142, TL_H };
    case ID_HOME_REC: return Box{ 304, RC_Y + 8, 148, RC_H - 16 };
    case ID_HOME_FILES: return Box{ 198, RC_Y + 8, 98, RC_H - 16 };
    case ID_HOME_RX: return Box{ 323, TL_Y, 141, TL_H };
    // переключатель источника: на главной — во всю ширину сверху, на вкладке «Звук» — в строке проверочного звука
    case ID_SRC_LIVE: return page == PG_HOME ? Box{ 16, SW_Y, 150, SW_H } : Box{ 16, 280, 150, 54 };
    case ID_SRC_TEST: return page == PG_HOME ? Box{ 174, SW_Y, 150, SW_H } : Box{ 174, 280, 150, 54 };
    case ID_MUTE: return Box{ 332, SW_Y, 132, SW_H };
    case ID_POWER: return Box{ 218, 12, 40, 40 };
    case ID_RX_PREV: return Box{ 320, 364, 48, 40 };
    case ID_RX_NEXT: return Box{ 416, 364, 48, 40 };
    case ID_PAIR_OPEN: return Box{ 16, 364, 144, 40 };
    case ID_OTA_OPEN: return Box{ 168, 364, 144, 40 };
    case ID_POWEROFF: return Box{ 16, 332, 448, 52 };
    case ID_ROW + P_LANG: return rowBox(3, 76, 46, 6);
    case ID_ROW + P_GAIN: return Box{ 16, 218, 218, 54 };
    case ID_ROW + P_INPUT: return Box{ 246, 218, 218, 54 };
    case ID_ROW + P_TONE: return Box{ 332, 280, 132, 54 };   // «який звук» — открывает окно настроек проверки
    case ID_ROW + P_QUALITY: return Box{ 16, 342, 288, 54 };
    case ID_ROW + P_STEREO: return Box{ 312, 342, 152, 54 };
    case ID_SCAN: return Box{ 16, 222, 448, 50 };
    case ID_ROW + P_CHANNEL: return Box{ 16, 282, 141, 56 };
    case ID_ROW + P_KIT: return Box{ 169, 282, 142, 56 };
    case ID_ROW + P_HOP: return Box{ 323, 282, 141, 56 };   // рядом с каналом: сам ли передатчик уходит с занятого канала
    case ID_ROW + P_RATE: return Box{ 16, 346, 218, 56 };
    case ID_ROW + P_POWER: return Box{ 246, 346, 218, 56 };
    // страница настроек: шесть строк по 46 точек
    case ID_ROW + P_BRIGHT: return rowBox(0, 76, 46, 6);
    case ID_ROW + P_DIM: return rowBox(1, 76, 46, 6);
    case ID_SRCNAME: return rowBox(2, 76, 46, 6);
    case ID_ABOUT: return Box{ 16, 284, 218, 46 };              // пятая строка настроек поделена: слева «Про пристрій»,
    case ID_ROW + P_DEBUG: return Box{ 246, 284, 218, 46 };     // справа — отладка по радио
    case ID_RESTART: return Box{ 16, 336, 218, 46 };
    case ID_FACTORY: return Box{ 246, 336, 218, 46 };
    case ID_ART_BACK: return Box{ 16, 350, 150, 50 };
    case ID_ART_PREV: return Box{ 330, 350, 62, 50 };
    case ID_ART_NEXT: return Box{ 402, 350, 62, 50 };
  }
  if (id >= ID_HELP && id < ID_HELP + HELP_N) return rowBox(id - ID_HELP, 72, 48, 7);
  return Box{ 0, 0, 0, 0 };
}

// ---------------------------------------------------------------- отдельные элементы
static void drawToggle(int x, int y, float on) {   // on: 0…1 (положение бегунка)
  uint16_t c = m2::Gfx::blend(C_SECONDARY, C_PRIMARY, (uint8_t)(on * 255));
  g.box(x, y, 52, 28, 14, c);
  g.circle(x + 14 + on * 24, y + 14, 10.5f, on > 0.5f ? C_PRIMARY_FG : C_MUTED_FG);
}

// Что сейчас написано в каждой строке настройки: значение может смениться и без нажатия (скорость «сама»,
// качество «за швидкістю», «стерео (зараз моно)») — тогда строку надо перерисовать самим.
static char rowShown[P_COUNT][64];
// Текст строки: у проверочного звука — какой звук включится по кнопке «Перевірка», у остальных — значение.
static void rowText(int p, char *v, size_t n) {
  if (p == P_TONE) snprintf(v, n, "%s", TEST_COMPACT[view.lastTone >= 1 && view.lastTone <= 6 ? view.lastTone : 5]);
  else if (PARAM[p].kind == K_TOGGLE) snprintf(v, n, "%d", view.val[p] ? 1 : 0);
  else paramText(p, view.val[p], v, n);
}
static void drawRow(int p, bool pressed) {
  char v[64];
  Box b = itemBox(ID_ROW + p);
  card(b, pressed ? C_SECONDARY : C_CARD);
  if (p == P_TONE) {   // узкая кнопка рядом с переключателем: открывает окно настроек проверочного звука
    rowText(p, v, sizeof(v));
    strlcpy(rowShown[p], v, sizeof(rowShown[p]));
    g.text(b.x + 12, b.y + 22, tr("Який звук"), F_CAP, C_MUTED_FG);
    g.text(b.x + 12, b.y + 44, v, F_CAPB, C_FG, AL_L, b.w - 30);
    g.text(b.x + b.w - 10, b.y + b.h / 2 + 6, "»", F_TXT, C_BORDER, AL_R);
    return;
  }
  bool half = b.w < 300;
  g.text(b.x + 16, b.y + (half ? 22 : b.h / 2 + 6), PARAM[p].name, half ? F_CAP : F_TXT, half ? C_MUTED_FG : C_FG);
  if (PARAM[p].kind == K_TOGGLE) {
    drawToggle(b.x + b.w - 68, b.y + (b.h - 28) / 2, view.val[p] ? 1 : 0);
    snprintf(rowShown[p], sizeof(rowShown[p]), "%d", view.val[p] ? 1 : 0);
    return;
  }
  paramText(p, view.val[p], v, sizeof(v));
  strlcpy(rowShown[p], v, sizeof(rowShown[p]));
  if (half) g.text(b.x + 16, b.y + 45, v, F_TXTB, C_FG, AL_L, b.w - 44);
  else g.text(b.x + b.w - 34, b.y + b.h / 2 + 6, v, F_TXTB, C_PRIMARY, AL_R);
  g.text(b.x + b.w - 14, b.y + b.h / 2 + 6, "»", F_TXT, C_BORDER, AL_R);
}

static void lampDraw(const Lamp &l, float k) {
  g.circle(l.x, l.y, l.r + 2, l.ring);
  g.circle(l.x, l.y, l.r * (0.66f + 0.34f * k), m2::Gfx::blend(l.ring, l.c, (uint8_t)(105 + 150 * k)));
}
static void waitDots(int x, int y, uint16_t bg) {
  float t = millis() / 1000.0f;
  g.fill(x - 4, y - 6, 44, 12, bg);
  for (int i = 0; i < 3; i++) {
    float k = sinf(t * 5.0f - i * 0.9f);
    k = k < 0 ? 0 : k;
    g.circle(x + 4 + i * 14, y, 2.4f + 1.6f * k, m2::Gfx::blend(bg, C_PRIMARY, (uint8_t)(110 + 145 * k)));
  }
}

static void drawButton(const Box &b, const char *label, int style, bool pressed) {   // 0 обычная, 1 главная, 2 опасная
  uint16_t fill = style == 1 ? C_PRIMARY : style == 2 ? C_CARD : C_SECONDARY;
  uint16_t ink = style == 1 ? C_PRIMARY_FG : style == 2 ? C_DANGER : C_FG;
  if (pressed) fill = m2::Gfx::blend(fill, C_FG, 40);
  card(b, fill, style == 1 ? C_PRIMARY : style == 2 ? C_DANGER : C_BORDER);
  g.text(b.x + b.w / 2, b.y + b.h / 2 + 6, label, F_TXTB, ink, AL_C, b.w - 16);
}

static void drawRecButton(bool pressed) {
  Box b = itemBox(ID_HOME_REC);
  if (!view.sd) return;
  // Значок и надпись ставятся порознь, с промежутком в 10 точек. Раньше место под значок давали три пробела в
  // начале надписи, и точка стояла вплотную к букве (владелец 07.10: «находится слишком близко к надписи»).
  const char *label = view.rec ? tr("Зупинити") : tr("Записати");
  drawButton(b, "", view.rec ? 2 : 0, pressed);
  int tw = m2::Gfx::textW(label, F_TXTB), x0 = b.x + (b.w - (14 + 10 + tw)) / 2, iy = b.y + b.h / 2;
  if (view.rec) g.box(x0 + 1, iy - 6, 12, 12, 2, C_DANGER);
  else g.circle(x0 + 7, iy, 7, C_DANGER);
  g.text(x0 + 24, iy + 6, label, F_TXTB, view.rec ? C_DANGER : C_FG);
}

static int rxPerPage() {
  return RX_ROWS;
}
static const char *signalWord(int rssi) {
  return rssi > -60 ? tr("відмінний") : rssi > -72 ? tr("добрий") : rssi > -82 ? tr("слабкий") : tr("дуже слабкий");
}
static int rxOnline() {
  int n = 0;
  for (int i = 0; i < view.rxN; i++) n += view.rx[i].online;
  return n;
}
static int rxFind(const uint8_t *id) {
  for (int i = 0; i < view.rxN; i++)
    if (!memcmp(view.rx[i].id, id, 3)) return i;
  return -1;
}

static void drawRxRow(int k, bool pressed) {
  int idx = rxPage * rxPerPage() + k;
  if (idx >= view.rxN || k >= rxPerPage()) return;
  const View::Rx &r = view.rx[idx];
  Box b = itemBox(ID_RXROW + k);
  char t[96];
  card(b, pressed ? C_SECONDARY : C_CARD);
  g.circle(b.x + 20, b.y + 19, 6, !r.online ? C_BORDER : r.off ? C_DANGER : C_OK);
  g.text(b.x + 36, b.y + 24, r.name, F_TXTB, r.online ? C_FG : C_MUTED_FG, AL_L, 300);
  if (!r.online) snprintf(t, sizeof(t), tr("не на зв'язку"));
  else if (r.off) snprintf(t, sizeof(t), r.sleep ? tr("спить · вимкнений з передавача") : tr("вимкнений з передавача"));
  else if (r.sleep) snprintf(t, sizeof(t), tr("прокидається…"));
  else snprintf(t, sizeof(t), tr("сигнал %s · гучність %d %%%s"), signalWord(r.rssi), r.volume * 5, r.mute ? tr(" · тиша") : "");
  g.text(b.x + 36, b.y + 45, t, F_CAP, C_MUTED_FG, AL_L, 320);
  if (r.online) drawToggle(b.x + b.w - 68, b.y + 13, r.off ? 0 : 1);
}

static int srcShown = -1;      // какая половина переключателя нарисована выбранной
static void drawSource(int id, bool pressed) {
  Box b = itemBox(id);
  bool file = view.fileOn, test = view.val[P_TONE] != 0, mine = file ? id == ID_SRC_TEST : (id == ID_SRC_TEST) == test;
  uint16_t fill = pressed ? C_SECONDARY : mine ? C_PRIMARY : C_CARD, ink = mine && !pressed ? C_PRIMARY_FG : C_FG;
  g.fill(b.x, b.y, b.w, b.h, C_BG);
  g.box(b.x, b.y, b.w, b.h, RADIUS, mine || pressed ? fill : C_BORDER);
  if (!mine && !pressed) g.box(b.x + 1, b.y + 1, b.w - 2, b.h - 2, RADIUS - 1, C_CARD);
  // имя источника задаёт владелец («Налашт.» › «Назва джерела звуку»); не задано — «З пульта»
  // пока в эфире файл с карты, правая кнопка — он («Файл»): нажать — открыть карту; левая — вернуть звук источника
  const char *label = id == ID_SRC_LIVE ? (view.srcName[0] ? view.srcName : tr("З пульта")) : file ? tr("Файл") : tr("Перевірка");
  int cx = b.x + b.w / 2 + (mine ? 8 : 0);
  int w = g.text(cx, b.y + b.h / 2 + 6, label, F_TXTB, ink, AL_C, b.w - (mine ? 44 : 16));
  if (w > b.w - 44) w = b.w - 44;
  lampDrop(id);
  if (mine) {   // огонёк у выбранного: зелёный — звук источника или файл; красный — проверочный звук
    float dx = cx - w / 2 - 14, dy = b.y + b.h / 2;
    // Красный был янтарным и на песочной кнопке терялся (владелец 07.10: «почти не видно красной точки, она
    // сливается с кнопкой») — теперь ярко-красный с тёмным ободком. И оба огонька живые (lampSet).
    // Зелёный (звук источника, файл) — того же вида, что красный: яркий, с тёмным ободком
    // (владелец 07.10: «для индикации с пульта сделать индикатор такой же как и в проверке, но зелёный»).
    bool red = test && !file;
    Lamp l = { (int16_t)id, dx, dy, 5.5f, red ? C_LAMP_RED : C_LAMP_GREEN, C_PRIMARY_FG };
    lampDraw(l, 1);
    if (!pressed) lampSet(id, l.x, l.y, l.r, l.c, l.ring);
  }
  srcShown = file ? 2 : test;
}

// Кнопка «Тиша»: пока тишина включена — красная, с перечёркнутым динамиком.
static int muteShown = -1;
static void drawMute(bool pressed) {
  Box b = itemBox(ID_MUTE);
  bool on = view.muted;
  g.fill(b.x, b.y, b.w, b.h, C_BG);
  g.box(b.x, b.y, b.w, b.h, RADIUS, pressed ? C_SECONDARY : on ? C_DANGER : C_BORDER);
  if (!on && !pressed) g.box(b.x + 1, b.y + 1, b.w - 2, b.h - 2, RADIUS - 1, C_CARD);
  int ix = b.x + 26, iy = b.y + b.h / 2;
  const float horn[] = { ix - 9.0f, iy - 4.0f, ix - 3.0f, iy - 4.0f, ix + 5.0f, iy - 11.0f, ix + 5.0f, iy + 11.0f, ix - 3.0f, iy + 4.0f, ix - 9.0f, iy + 4.0f };
  g.poly(horn, 6, C_FG);                                         // динамик
  if (on) g.line(ix - 11, iy + 11, ix + 11, iy - 11, 3, C_FG);   // перечёркнут
  else g.arc(ix + 5, iy, 8, 2.2f, C_FG, 40, 140);                // звуковая волна
  g.text(b.x + 48, iy + 6, on ? tr("ТИША") : tr("Тиша"), F_TXTB, C_FG);
  lampDrop(LAMP_MUTE);
  if (on && !pressed) lampSet(LAMP_MUTE, b.x + b.w - 15, iy, 3.5f, C_FG, C_DANGER);   // тишина включена — белый огонёк на красной кнопке
  muteShown = on;
}

static void drawItem(int id, bool pressed) {
  char t[40];
  Box b = itemBox(id);
  if (id == ID_SRC_LIVE || id == ID_SRC_TEST) return drawSource(id, pressed);
  if (id == ID_MUTE) return drawMute(pressed);
  if (id == ID_POWER) return drawPower(pressed);
  if (id >= ID_RXROW && id < ID_RXROW + RX_ROWS) return drawRxRow(id - ID_RXROW, pressed);
  if (id >= ID_ROW && id < ID_ROW + P_COUNT) return drawRow(id - ID_ROW, pressed);
  if (id >= ID_HELP && id < ID_HELP + HELP_N) {
    card(b, pressed ? C_SECONDARY : C_CARD);
    g.text(b.x + 16, b.y + b.h / 2 + 6, HELP[id - ID_HELP].title, F_TXT, C_FG);
    g.text(b.x + b.w - 14, b.y + b.h / 2 + 6, "»", F_TXT, C_BORDER, AL_R);
    return;
  }
  switch (id) {
    case ID_HOME_CH:
    case ID_HOME_KIT:
      card(b, pressed ? C_SECONDARY : C_CARD);
      g.text(b.x + 14, b.y + 22, id == ID_HOME_CH ? tr("Канал") : tr("Набір"), F_CAP, C_MUTED_FG);
      snprintf(t, sizeof(t), "%d", view.val[id == ID_HOME_CH ? P_CHANNEL : P_KIT]);
      g.text(b.x + 14, b.y + 62, t, F_BIG, C_FG);
      break;
    case ID_HOME_REC: drawRecButton(pressed); break;
    case ID_HOME_FILES:
      if (view.sd) drawButton(b, tr("Файли"), 0, pressed);
      break;
    case ID_HOME_RX: {
      card(b, pressed ? C_SECONDARY : C_CARD);
      g.text(b.x + 14, b.y + 22, tr("Приймачі"), F_CAP, C_MUTED_FG);
      snprintf(t, sizeof(t), "%d", rxOnline());
      int w = g.text(b.x + 14, b.y + 62, t, F_BIG, rxOnline() ? C_FG : C_MUTED_FG);
      snprintf(t, sizeof(t), tr("з %d"), view.rxN);
      if (view.rxN) g.text(b.x + 22 + w, b.y + 62, t, F_CAP, C_MUTED_FG);
      break;
    }
    case ID_RX_PREV: drawButton(b, "«", 0, pressed); break;
    case ID_RX_NEXT: drawButton(b, "»", 0, pressed); break;
    case ID_PAIR_OPEN: drawButton(b, tr("+ Додати"), 1, pressed); break;
    case ID_OTA_OPEN: drawButton(b, tr("Оновлення"), 0, pressed); break;
    case ID_POWEROFF: drawButton(b, tr("Вимкнути передавач"), 2, pressed); break;
    case ID_SCAN: drawButton(b, view.scanning ? tr("Шукаю…") : tr("Знайти вільний канал"), 1, pressed); break;
    case ID_SRCNAME:
      card(b, pressed ? C_SECONDARY : C_CARD);
      g.text(b.x + 16, b.y + b.h / 2 + 6, tr("Назва джерела звуку"), F_TXT, C_FG);
      g.text(b.x + b.w - 34, b.y + b.h / 2 + 6, view.srcName[0] ? view.srcName : tr("З пульта"), F_TXTB, C_PRIMARY, AL_R, 200);
      g.text(b.x + b.w - 14, b.y + b.h / 2 + 6, "»", F_TXT, C_BORDER, AL_R);
      break;
    case ID_ABOUT:
      card(b, pressed ? C_SECONDARY : C_CARD);
      g.text(b.x + 16, b.y + b.h / 2 + 6, tr("Про пристрій"), F_TXT, C_FG);
      g.text(b.x + b.w - 14, b.y + b.h / 2 + 6, "»", F_TXT, C_BORDER, AL_R);
      break;
    case ID_RESTART: drawButton(b, tr("Перезапустити"), 0, pressed); break;
    case ID_FACTORY: drawButton(b, tr("Скинути все"), 2, pressed); break;
    case ID_ART_BACK: drawButton(b, tr("« Теми"), 0, pressed); break;
    case ID_ART_PREV: drawButton(b, "«", 0, pressed); break;
    case ID_ART_NEXT: drawButton(b, "»", 0, pressed); break;
  }
}

// ---------------------------------------------------------------- живые части страниц
// Уровень входа — в том же виде, что спектр: ряд рисок со скруглёнными концами. Риски загораются слева
// направо, последняя — частично (поэтому движение плавное, без ступенек); цвет — по зоне: зелёная, жёлтая
// от −12 дБ, красная от −3 дБ. Светлая риска — недавний наибольший уровень.
// Перерисовываются только те риски, что изменились.
const int METER_N = 46;
static uint16_t meterShown[METER_N];
static void drawMeter(bool force = false) {
  float pos = (view.levelDb + 60) / 60 * METER_N;
  int holdTick = view.holdDb <= -59 ? -1 : (int)((view.holdDb + 60) / 60 * METER_N);
  holdTick = holdTick >= METER_N ? METER_N - 1 : holdTick;
  for (int i = 0; i < METER_N; i++) {
    float f = pos - i;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    uint16_t key = (uint16_t)(f * 255) | (i == holdTick && i > (int)pos ? 0x100 : 0);
    if (!force && key == meterShown[i]) continue;
    meterShown[i] = key;
    float db = -60 + (i + 0.5f) * 60 / METER_N;
    uint16_t zone = db > -3 ? C_DANGER : db > -12 ? C_WARN : C_OK;
    uint16_t c = i == holdTick && i > (int)pos ? C_FG : m2::Gfx::blend(C_SECONDARY, zone, (uint8_t)(f * 255));
    int x = METER.x + i * METER.w / METER_N;
    g.fill(x, METER.y, 8, METER.h, C_CARD);
    g.box(x + 1, METER.y + 1, 6, METER.h - 2, 3, c);
  }
}

static void drawHomeLive(bool force) {
  char t[48];
  drawMeter(force);
  int num = (int)lroundf(view.levelDb);
  if (force || (frameNo % 8 == 0 && num != shownNum)) {
    g.fill(330, LV_Y + 10, 122, 26, C_CARD);
    if (num <= -59) snprintf(t, sizeof(t), tr("тихо"));
    else snprintf(t, sizeof(t), tr("%d дБ"), num);
    g.text(448, LV_Y + 30, t, F_TXTB, view.peakDb > -3 ? C_DANGER : C_FG, AL_R);
    shownNum = num;
  }
  int hint = view.muted ? 5 : view.val[P_TONE] ? 4 : view.clip ? 3 : view.silent ? 0 : view.peakDb > -12 ? 2 : 1;
  if (force || hint != shownHint) {
    static const char *const TXT[] = { "Звуку на вході немає", "Рівень добрий", "Гучно, але ще чисто",
                                       "Перевантаження — зменшіть рівень на пульті", "Іде перевірочний звук, вхід вимкнено",
                                       "Тиша в ефірі: приймачі нічого не чують" };
    static const uint16_t COL[] = { C_MUTED_FG, C_OK, C_WARN, C_DANGER, C_WARN, C_DANGER };
    g.fill(30, LV_Y + 92, 420, 22, C_CARD);
    g.circle(38, LV_Y + 103, 4, COL[hint]);
    lampDrop(LAMP_HINT);
    if (hint) lampSet(LAMP_HINT, 38, LV_Y + 103, 4, COL[hint], C_CARD);   // звук есть — огонёк живой; звука нет — серая точка стоит
    g.text(50, LV_Y + 109, TXT[hint], F_CAP, hint == 0 ? C_MUTED_FG : C_FG);
    shownHint = hint;
  }
  if (!force && rxOnline() * 100 + view.rxN != shownRxN) {
    shownRxN = rxOnline() * 100 + view.rxN;
    drawItem(ID_HOME_RX, false);
  }
  if (view.sd && view.rec && !force && (frameNo & 1)) {   // идёт запись — точка дышит
    float k = 0.5f + 0.5f * sinf(millis() / 1000.0f * 4);
    g.circle(40, RC_Y + 44, 7, C_CARD);
    g.circle(40, RC_Y + 44, 4 + 2 * k, m2::Gfx::blend(C_CARD, C_DANGER, (uint8_t)(140 + 115 * k)));
  }
  int rec = !view.sd ? -2 - view.card : view.rec ? (int)view.recS : -100000 - (int)(view.sdFreeMb & 0xFFFF) - (view.sdFull ? 70000 : 0);
  if (force || rec != shownRec) {
    bool both = force || (shownRec >= 0) != (rec >= 0) || (shownRec > -100000) != (rec > -100000);   // карта появилась/пропала, запись пошла/кончилась
    g.fill(30, RC_Y + 6, both || !view.sd ? 268 : 164, RC_H - 12, C_CARD);   // при карте справа от надписи — кнопки «Файли» и «Записати»
    g.text(32, RC_Y + 24, tr("Запис на картку"), F_CAP, C_MUTED_FG);
    if (!view.sd) {   // настоящая причина, а не «карты нет»: владелец вставил карту, а экран уверял, что её нет
      if (view.card == 2) {
        snprintf(t, sizeof(t), tr("%s не читається: треба FAT32"), view.cardFs[0] ? view.cardFs : tr("розмітка"));
        g.text(32, RC_Y + 50, t, F_TXT, C_WARN);
      } else if (view.card == 3) g.text(32, RC_Y + 50, tr("картка не запускається"), F_TXT, C_WARN);
      else g.text(32, RC_Y + 50, tr("картка не відповідає"), F_TXT, C_MUTED_FG);
    } else if (view.rec) {
      fmtTime(view.recS, t, sizeof(t));
      g.circle(40, RC_Y + 44, 6, C_DANGER);
      g.text(54, RC_Y + 50, t, F_TXTB, C_FG);
    } else if (view.sdFull) g.text(32, RC_Y + 50, tr("місця немає"), F_TXT, C_WARN);
    else {   // час запису, що лишився: година — близько 230 МБ; менше двох годин — хвилинами (було «вільно 0 год» при 196 МБ)
      if (view.sdFreeMb >= 460) snprintf(t, sizeof(t), tr("вільно %u год"), (unsigned)(view.sdFreeMb / 230));
      else snprintf(t, sizeof(t), tr("вільно %u хв"), (unsigned)(view.sdFreeMb * 60 / 230));
      g.text(32, RC_Y + 50, t, F_TXT, view.sdFreeMb < 40 ? C_WARN : C_FG);
    }
    if (both) {
      drawRecButton(pressId == ID_HOME_REC);
      drawItem(ID_HOME_FILES, pressId == ID_HOME_FILES);
    }
    shownRec = rec;
  }
}

// Спектр — как в «ПОТУЖНОМУ РАДІО»: 32 риски со скруглёнными концами, которые «дышат» со звуком; чем выше
// риска, тем она ярче. Уровни (0…200) приходят уже сглаженными. Перерисовывается только верх риски
// и она сама — фон под телом не трогается, поэтому ничего не мигает.
// Риска перерисовывается не вся, а только то, что изменилось: выросла — дорисовать верх, опустилась — стереть
// лишнее и поставить новый скруглённый верх. Целиком — лишь когда сменилась ступень яркости (их восемь).
static int16_t specTop[SPEC_BARS];
static uint8_t specCol[SPEC_BARS];
static void drawSpectrum(bool full = false) {
  const int x0 = 30, y1 = 198, hmax = 84, STEP = 21, BW = 15;   // 20 рисок по 15 точек с шагом 21
  for (int i = 0; i < SPEC_BARS; i++) {
    float lv = view.spec[i] / 200.0f;
    int h = lv > 0 ? (int)(6 + lv * (hmax - 6)) : 5;
    int top = y1 - h, bx = x0 + i * STEP + 1;
    uint8_t ck = lv > 0 ? 1 + (uint8_t)(lv * 7.99f) : 0;
    uint16_t c = ck ? m2::Gfx::blend(C_SECONDARY, C_PRIMARY, (uint8_t)(90 + (ck - 0.5f) / 8 * 165)) : C_SECONDARY;
    int old = specTop[i];
    if (!full && old) {   // дрожь в одну точку не рисуем вовсе (и цвет из-за неё не меняем)
      int d = top - old;
      if (d >= -1 && d <= 1 && (h > 5) == (y1 - old > 5)) continue;
    }
    if (!full && old && ck == specCol[i]) {
      if (top == old) continue;
      if (top < old) g.box(bx, top, BW, old - top + 8 > h ? h : old - top + 8, 4, c);
      else {
        g.fill(bx - 1, old, BW + 2, top - old + 4, C_CARD);
        g.box(bx, top, BW, h < 12 ? h : 12, 4, c);
      }
    } else {
      g.fill(x0 + i * STEP, y1 - hmax - 2, STEP - 1, hmax + 2, C_CARD);
      g.box(bx, top, BW, h, 4, c);
    }
    specTop[i] = top;
    specCol[i] = ck;
  }
}

static void drawChannels() {
  const int x0 = 32, y1 = 184, hmax = 64;
  for (int i = 0; i < 13; i++) {
    int x = x0 + i * 32;
    bool cur = i + 1 == view.val[P_CHANNEL];
    bool known = view.scanned || (view.scanning && i < view.scanStep);
    int h = known ? 4 + view.chanBusy[i] * (hmax - 4) / 100 : 4;
    g.fill(x, y1 - hmax, 24, hmax - h, C_CARD);
    uint16_t c = !known ? C_BORDER : view.chanBusy[i] > 60 ? C_DANGER : view.chanBusy[i] > 25 ? C_WARN : C_OK;
    g.box(x, y1 - h, 24, h, 3, c);
    char t[4];
    snprintf(t, sizeof(t), "%d", i + 1);
    g.fill(x - 2, y1 + 4, 28, 22, C_CARD);
    if (cur) g.box(x - 2, y1 + 5, 28, 20, 6, C_PRIMARY);
    g.text(x + 12, y1 + 20, t, F_CAPB, cur ? C_PRIMARY_FG : C_MUTED_FG, AL_C);
  }
}

// ---------------------------------------------------------------- страницы целиком
static void addHot(int id) {
  if (hotN < (int)(sizeof(hot) / sizeof(hot[0]))) hot[hotN++] = Hot{ itemBox(id), id };
}
static void item(int id) {
  drawItem(id, false);
  addHot(id);
}

static void drawArticle() {
  const Topic &tp = HELP[article];
  g.text(18, 96, tp.title, F_H, C_FG);
  int total = wrap(18, 130, 444, 24, tp.text, F_TXT, C_FG, 9, articlePage * 9);
  articlePages = (total + 8) / 9;
  item(ID_ART_BACK);
  if (articlePages > 1) {
    char t[16];
    snprintf(t, sizeof(t), tr("%d з %d"), articlePage + 1, articlePages);
    g.text(318, 381, t, F_CAP, C_MUTED_FG, AL_R);
    item(ID_ART_PREV);
    item(ID_ART_NEXT);
  }
}

// Рисует область между шапкой и вкладками. Куда — задаёт g.target (экран или запасной лист).
static void drawContent() {
  g.fill(0, CY, W, CH, C_BG);
  hotN = 0;
  switch (page) {
    case PG_HOME:
      item(ID_SRC_LIVE);
      item(ID_SRC_TEST);
      item(ID_MUTE);
      card(Box{ 16, LV_Y, 448, LV_H });
      g.text(32, LV_Y + 28, view.fileOn ? tr("Рівень звуку з файлу") : view.val[P_TONE] ? tr("Рівень перевірочного звуку") : tr("Рівень входу"), F_CAP, C_MUTED_FG);
      for (int m : { -48, -36, -24, -12, -6, 0 }) {
        char t[8];
        snprintf(t, sizeof(t), "%d", m);
        g.text(m == 0 ? METER.x + METER.w : METER.x + (m + 60) * METER.w / 60, LV_Y + 86, t, F_CAP, C_MUTED_FG, m == 0 ? AL_R : AL_C);
      }
      item(ID_HOME_CH);
      item(ID_HOME_KIT);
      item(ID_HOME_RX);
      shownRxN = rxOnline() * 100 + view.rxN;
      card(Box{ 16, RC_Y, 448, RC_H });
      addHot(ID_HOME_REC);
      addHot(ID_HOME_FILES);
      drawHomeLive(true);
      break;
    case PG_RX:
      if (!view.rxN) {
        tabIcon(PG_RX, 240, 160, C_BORDER);
        g.text(240, 216, tr("Приймачів у наборі ще немає"), F_TXTB, C_FG, AL_C);
        g.text(240, 246, tr("Натисніть «+ Додати» і ввімкніть приймач —"), F_CAP, C_MUTED_FG, AL_C);
        g.text(240, 268, tr("він попросить доступ. Чужий приймач звуку не почує."), F_CAP, C_MUTED_FG, AL_C);
      } else {
        if (rxPage * rxPerPage() >= view.rxN) rxPage = 0;
        for (int k = 0; k < rxPerPage() && rxPage * rxPerPage() + k < view.rxN; k++) item(ID_RXROW + k);
        if (view.rxN > RX_ROWS) {
          char t[24];
          snprintf(t, sizeof(t), tr("%d з %d"), rxPage + 1, (view.rxN + rxPerPage() - 1) / rxPerPage());
          g.text(392, 390, t, F_CAP, C_MUTED_FG, AL_C);   // между стрелками
          item(ID_RX_PREV);
          item(ID_RX_NEXT);
        }
      }
      item(ID_PAIR_OPEN);   // всегда на виду: без неё приёмник к набору не подключить
      item(ID_OTA_OPEN);    // обновление прошивки приёмников по радио — рядом со списком приёмников
      break;
    case PG_SOUND:
      card(Box{ 16, 76, 448, 134 });
      g.text(32, 100, tr("Спектр входу"), F_CAP, C_MUTED_FG);
      g.text(448, 100, tr("100 Гц … 12 кГц"), F_CAP, C_MUTED_FG, AL_R);
      drawSpectrum(true);
      item(ID_ROW + P_GAIN);
      item(ID_ROW + P_INPUT);
      item(ID_SRC_LIVE);
      item(ID_SRC_TEST);
      item(ID_ROW + P_TONE);
      item(ID_ROW + P_QUALITY);
      item(ID_ROW + P_STEREO);
      break;
    case PG_AIR:
      card(Box{ 16, 76, 448, 138 });
      g.text(32, 100, tr("Зайнятість каналів"), F_CAP, C_MUTED_FG);
      if (view.hopResult == 1 || view.hopResult == 2) {   // последний самостоятельный уход с канала — что и когда
        char t[80];
        uint32_t m = view.hopAgoS / 60;
        if (view.hopResult == 1) snprintf(t, sizeof(t), tr("сам змінив канал з %u на %u, %u хв тому"), view.hopFrom, view.hopTo, (unsigned)m);
        else snprintf(t, sizeof(t), tr("затор, вільнішого не знайшов, %u хв тому"), (unsigned)m);
        g.text(448, 100, t, F_CAP, C_WARN, AL_R);
      } else g.text(448, 100, view.scanned ? tr("нижче — вільніше") : tr("ще не вимірювалось"), F_CAP, C_MUTED_FG, AL_R);
      drawChannels();
      item(ID_SCAN);
      item(ID_ROW + P_CHANNEL);
      item(ID_ROW + P_KIT);
      item(ID_ROW + P_HOP);
      item(ID_ROW + P_RATE);
      item(ID_ROW + P_POWER);
      break;
    case PG_SETUP:
      item(ID_ROW + P_BRIGHT);
      item(ID_ROW + P_DIM);
      item(ID_SRCNAME);
      item(ID_ABOUT);
      item(ID_ROW + P_DEBUG);
      item(ID_RESTART);
      item(ID_FACTORY);
      item(ID_ROW + P_LANG);
      break;
    case PG_HELP:
      if (article >= 0) drawArticle();
      else
        for (int i = 0; i < HELP_N; i++) item(ID_HELP + i);
      break;
  }
  addHot(ID_POWER);   // кнопка выключения в шапке — на каждой странице
}

// ---------------------------------------------------------------- окна поверх страницы
static Box mBox;
static int mOptY = 150, mOptStep = 56;   // где в окне начинаются варианты списка и шаг между ними
static int mCols = 1;                    // столбцов в списке (длинный список — два)
static Box mItem(int id) {
  int by = mBox.y + mBox.h - 76;
  switch (id) {
    case ID_M_MINUS: return Box{ (int16_t)(mBox.x + 20), (int16_t)(by - 96), 120, 78 };
    case ID_M_PLUS: return Box{ (int16_t)(mBox.x + mBox.w - 140), (int16_t)(by - 96), 120, 78 };
    case ID_M_OK:
      if (modal == M_SHEET) return Box{ (int16_t)(mBox.x + 16), (int16_t)(mBox.y + mBox.h - 64), (int16_t)(mBox.w - 32), 50 };
      return Box{ (int16_t)(mBox.x + mBox.w / 2 + 6), (int16_t)by, (int16_t)(mBox.w / 2 - 26), 56 };
    case ID_M_CANCEL: return Box{ (int16_t)(mBox.x + 20), (int16_t)by, (int16_t)(mBox.w / 2 - 26), 56 };
  }
  if (id >= ID_SHEET && id < ID_SHEET + SHEET_N) return Box{ (int16_t)(mBox.x + 16), (int16_t)(mBox.y + 58 + (id - ID_SHEET) * 48), (int16_t)(mBox.w - 32), 42 };
  if (id >= ID_M_OPT) {
    int k = id - ID_M_OPT, cw = (mBox.w - 40 - (mCols - 1) * 8) / mCols;
    return Box{ (int16_t)(mBox.x + 20 + (k % mCols) * (cw + 8)), (int16_t)(mBox.y + mOptY + (k / mCols) * mOptStep), (int16_t)cw, (int16_t)(mOptStep - 8) };
  }
  return Box{ 0, 0, 0, 0 };
}

static void drawModalValue() {
  char v[32];
  Box m = mItem(ID_M_MINUS);
  g.fill(mBox.x + 150, m.y, mBox.w - 300, m.h, C_CARD);
  if (modalArg == P_GAIN && modalValue > 0) snprintf(v, sizeof(v), "+%d", modalValue);
  else snprintf(v, sizeof(v), "%d", modalValue);
  g.text(mBox.x + mBox.w / 2, m.y + 60, v, F_NUM, C_FG, AL_C);
}

// Нужна ли настройка в выбранном режиме проверочного звука (ненужные в списке приглушены).
static bool sheetUsed(int p) {
  int m = view.val[P_TONE];   // 1 тон, 2 голос, 3 голос и музыка, 4 музыка, 5 музыка с голосом
  switch (p) {
    case P_TRACK:
    case P_MUSIC_DB: return m >= 3 && m <= 5;
    case P_DUCK_S:
    case P_DUCK_DB: return m == 5;
    case P_VOICE_GAP: return m == 2;
    case P_VOICE_DB: return m == 2 || m == 3 || m == 5;
  }
  return true;
}

static void drawModalItem(int id, bool pressed) {
  Box b = mItem(id);
  if (id >= ID_SHEET && id < ID_SHEET + SHEET_N) {
    char v[64];
    int p = SHEET[id - ID_SHEET];
    bool on = sheetUsed(p);
    card(b, pressed ? C_SECONDARY : C_BG);
    g.text(b.x + 14, b.y + b.h / 2 + 6, p == P_TONE ? tr("Режим") : PARAM[p].name, F_TXT, on ? C_FG : C_MUTED_FG);
    paramText(p, view.val[p], v, sizeof(v));
    g.text(b.x + b.w - 30, b.y + b.h / 2 + 6, v, F_TXTB, on ? C_PRIMARY : C_MUTED_FG, AL_R, 230);
    g.text(b.x + b.w - 12, b.y + b.h / 2 + 6, "»", F_TXT, C_BORDER, AL_R);
    return;
  }
  if (id >= ID_M_OPT) {
    int k = id - ID_M_OPT;
    bool sel = k == modalValue;
    card(b, pressed ? C_SECONDARY : sel ? C_MUTED : C_CARD, sel ? C_PRIMARY : C_BORDER);
    g.arc(b.x + 24, b.y + b.h / 2, 9, 2, sel ? C_PRIMARY : C_MUTED_FG);
    if (sel) g.circle(b.x + 24, b.y + b.h / 2, 4.5f, C_PRIMARY);
    g.text(b.x + 46, b.y + b.h / 2 + 6, PARAM[modalArg].opts[k], F_TXT, C_FG, AL_L, b.w - 56);
    return;
  }
  switch (id) {
    case ID_M_MINUS:
    case ID_M_PLUS:   // знаки рисуем фигурами — крупно и ровно по центру
      drawButton(b, "", 0, pressed);
      g.box(b.x + b.w / 2 - 15, b.y + b.h / 2 - 3, 30, 6, 2, C_FG);
      if (id == ID_M_PLUS) g.box(b.x + b.w / 2 - 3, b.y + b.h / 2 - 15, 6, 30, 2, C_FG);
      break;
    case ID_M_CANCEL: drawButton(b, modal == M_CONFIRM ? tr("Ні") : tr("Скасувати"), 0, pressed); break;
    case ID_M_OK: drawButton(b, modal == M_CONFIRM ? tr("Так") : modal == M_ABOUT ? tr("Закрити") : tr("Готово"), 1, pressed); break;
  }
}

static void mHot(int id) {
  drawModalItem(id, false);
  if (hotN < (int)(sizeof(hot) / sizeof(hot[0]))) hot[hotN++] = Hot{ mItem(id), id };
}

static void drawAboutLines() {
  char t[64];
  int y = mBox.y + 84;
  auto line = [&](const char *k, const char *v) {
    g.fill(mBox.x + 20, y - 20, mBox.w - 40, 26, C_CARD);
    g.text(mBox.x + 22, y, k, F_CAP, C_MUTED_FG);
    g.text(mBox.x + mBox.w - 22, y, v, F_CAPB, C_FG, AL_R);
    y += 28;
  };
  snprintf(t, sizeof(t), "%s", view.version);
  line(tr("Версія програми"), t);
  line(tr("Адреса пристрою"), view.mac);
  fmtTime(view.uptimeS, t, sizeof(t));
  line(tr("Працює"), t);
  snprintf(t, sizeof(t), tr("%u за секунду"), view.pktPerS);
  line(tr("Пакетів в ефір"), t);
  snprintf(t, sizeof(t), tr("%.1f мс, найдовше %.0f мс"), view.airAvgMs, view.airMaxMs);
  line(tr("Очікування ефіру"), t);
  snprintf(t, sizeof(t), "%u / %u", view.refused, view.restarts);
  line(tr("Відмов / перезапусків радіо"), t);
  snprintf(t, sizeof(t), tr("%u КБ"), (unsigned)view.freeHeapK);
  line(tr("Вільної пам'яті"), t);
}

// ---- окно одного приёмника
// Кнопки окна приёмника: четыре ряда по две. «Видалити з набору» есть всегда — и у приёмника на связи (нижний ряд,
// слева от «Закрити»), и у выключенного (во всю ширину): добавить приёмник можно — значит, и удалить можно.
static Box rxBtn(int id) {
  int i = rxFind(rxSelId);
  bool online = i >= 0 && view.rx[i].online;
  int col = (id == ID_RX_RENAME || id == ID_RX_VOLUP || id == ID_RX_ONOFF || (id == ID_RX_CLOSE && online)) ? 1 : 0;
  int row = (id == ID_RX_IDENT || id == ID_RX_RENAME) ? 0 : (id == ID_RX_VOLDN || id == ID_RX_VOLUP) ? 1 : (id == ID_RX_CLOSE || id == ID_RX_FORGET) ? 3 : 2;
  if (id == ID_RX_FORGET && !online) row = 0;
  Box b = { (int16_t)(mBox.x + 20 + col * 202), (int16_t)(mBox.y + 246 + row * 50), 190, 44 };
  if (!online && (id == ID_RX_FORGET || id == ID_RX_CLOSE)) b.w = mBox.w - 40;
  return b;
}

static void drawRxLines() {
  int i = rxFind(rxSelId);
  if (i < 0) return;
  const View::Rx &r = view.rx[i];
  char t[64];
  int y = mBox.y + 98;
  auto line = [&](const char *k, const char *v) {
    g.fill(mBox.x + 20, y - 19, mBox.w - 40, 24, C_CARD);
    g.text(mBox.x + 22, y, k, F_CAP, C_MUTED_FG);
    g.text(mBox.x + mBox.w - 22, y, v, F_CAPB, C_FG, AL_R);
    y += 25;
  };
  g.fill(mBox.x + 20, mBox.y + 48, mBox.w - 40, 24, C_CARD);
  g.circle(mBox.x + 28, mBox.y + 60, 5, !r.online ? C_BORDER : r.off ? C_DANGER : C_OK);
  g.text(mBox.x + 42, mBox.y + 66,
         !r.online ? tr("не на зв'язку") : r.off ? (r.sleep ? tr("спить · вимкнений з передавача") : tr("вимкнений з передавача")) : r.sleep ? tr("прокидається…") : tr("на зв'язку"),
         F_CAP, C_MUTED_FG);
  if (!r.online) {
    line(tr("Сигнал"), "—");
    return;
  }
  snprintf(t, sizeof(t), tr("%d дБм, %s"), r.rssi, signalWord(r.rssi));
  line(tr("Сигнал"), t);
  snprintf(t, sizeof(t), "%d %%%s", r.volume * 5, r.mute ? tr(", тиша") : "");
  line(tr("Гучність"), t);
  snprintf(t, sizeof(t), tr("%.1f %% пакетів"), r.lossPm / 10.0f);
  line(tr("Втрати"), t);
  snprintf(t, sizeof(t), tr("%u мс"), r.depthMs);
  line(tr("Запас (затримка)"), t);
  line(tr("Вихід звуку"), r.fw < 11 ? tr("протифаза (стара версія)") : r.stereo ? tr("два канали (навушники)") : tr("протифаза (підсилювач)"));
  snprintf(t, sizeof(t), tr("%u год %02u хв, версія %.1f"), r.uptimeMin / 60, r.uptimeMin % 60, r.fw / 10.0f);
  line(tr("Працює"), t);
}

static void drawRxButton(int id, bool pressed) {
  int i = rxFind(rxSelId);
  bool off = i >= 0 && view.rx[i].off;
  Box b = rxBtn(id);
  switch (id) {
    case ID_RX_IDENT: drawButton(b, tr("Показати себе"), 0, pressed); break;
    case ID_RX_RENAME: drawButton(b, tr("Перейменувати"), 0, pressed); break;
    case ID_RX_VOLDN: drawButton(b, tr("Тихіше"), 0, pressed); break;
    case ID_RX_VOLUP: drawButton(b, tr("Гучніше"), 0, pressed); break;
    case ID_RX_ONOFF: drawButton(b, off ? tr("Увімкнути") : tr("Вимкнути"), off ? 0 : 2, pressed); break;
    case ID_RX_STEREO: drawButton(b, i >= 0 && view.rx[i].stereo ? tr("Вихід: протифаза") : tr("Вихід: два канали"), 0, pressed); break;
    case ID_RX_SET: drawButton(b, tr("Налаштування"), 0, pressed); break;
    case ID_RX_FORGET: drawButton(b, tr("Видалити з набору"), 2, pressed); break;
    case ID_RX_CLOSE: drawButton(b, tr("Закрити"), 1, pressed); break;
  }
}

// Окно «Додати приймач». Пока оно открыто, передатчик слушает просьбы приёмников без ключа; оператор сверяет код
// с экраном приёмника и нажимает «Дозволити» — только тогда приёмник получает ключ набора.
static Box pairBtn(int id) {
  if (id == ID_PAIR_CLOSE) return Box{ (int16_t)(mBox.x + 20), (int16_t)(mBox.y + mBox.h - 64), (int16_t)(mBox.w - 40), 48 };
  int k = id - ID_PAIR_OK;
  return Box{ (int16_t)(mBox.x + mBox.w - 162), (int16_t)(mBox.y + 147 + k * 62), 136, 46 };
}
static uint32_t pairShown = 0;   // что нарисовано в списке просьб
static uint32_t pairSum() {
  uint32_t h = 7919u + view.askN;
  for (int i = 0; i < view.askN; i++) {
    h = h * 31 + view.ask[i].code + view.ask[i].slot * 65536u;
    for (const char *c = view.ask[i].name; *c; c++) h = h * 31 + (uint8_t)*c;
  }
  return h;
}
static void drawPairButton(int id, bool pressed) {
  if (id == ID_PAIR_CLOSE) drawButton(pairBtn(id), tr("Закрити"), 0, pressed);
  else if (id >= ID_PAIR_OK && id < ID_PAIR_OK + view.askN) drawButton(pairBtn(id), tr("Дозволити"), 1, pressed);
}
static void drawPairModal() {
  char t[48];
  mBox = Box{ 24, 30, 432, 420 };
  card(mBox, C_CARD, C_BORDER);
  g.text(mBox.x + 20, mBox.y + 38, tr("Додати приймач"), F_H, C_FG);
  g.text(mBox.x + 20, mBox.y + 70, tr("Увімкніть новий приймач поруч — він попросить доступ."), F_CAP, C_MUTED_FG);
  g.text(mBox.x + 20, mBox.y + 92, tr("Звірте код із кодом на екрані приймача і дозвольте."), F_CAP, C_MUTED_FG);
  g.text(mBox.x + 20, mBox.y + 114, tr("Без дозволу приймач звуку не почує."), F_CAP, C_MUTED_FG);
  if (!view.askN) {
    card(Box{ (int16_t)(mBox.x + 20), (int16_t)(mBox.y + 142), (int16_t)(mBox.w - 40), 62 }, C_SECONDARY);
    g.text(mBox.x + mBox.w / 2, mBox.y + 180, tr("Чекаю на приймач…"), F_TXT, C_MUTED_FG, AL_C);
  }
  for (int i = 0; i < view.askN; i++) {
    int y = mBox.y + 142 + i * 62;
    card(Box{ (int16_t)(mBox.x + 20), (int16_t)y, (int16_t)(mBox.w - 40), 56 }, C_SECONDARY);
    g.text(mBox.x + 34, y + 24, view.ask[i].name, F_TXTB, C_FG, AL_L, 230);
    snprintf(t, sizeof(t), tr("код %04u"), view.ask[i].code);
    g.text(mBox.x + 34, y + 46, t, F_CAP, C_PRIMARY);
    drawPairButton(ID_PAIR_OK + i, false);
    hot[hotN++] = Hot{ pairBtn(ID_PAIR_OK + i), ID_PAIR_OK + i };
  }
  drawPairButton(ID_PAIR_CLOSE, false);
  hot[hotN++] = Hot{ pairBtn(ID_PAIR_CLOSE), ID_PAIR_CLOSE };
  pairShown = pairSum();
}

// ---- окно «Оновлення приймачів» (ota.h). Владелец 07.10: «во время обновлений приемников по воздуху, на экране
// соответствующее сообщение с ходом выполнения обновления».
static bool otaBusy() {   // идёт передача: закрыть окно нельзя, только отменить
  return view.ota.stage >= 1 && view.ota.stage <= 4;
}
static bool otaSelf() {   // передатчик пишет прошивку в себя или перезапускается: кнопок нет
  return view.ota.stage == 10 || view.ota.stage == 11;
}
static Box otaBtn(int id) {
  int y = mBox.y + mBox.h - 64;
  if (view.ota.stage == 0) {   // три кнопки в ряд: начать, пробное, закрыть
    int k = id == ID_OTA_START ? 0 : id == ID_OTA_DEMO ? 1 : 2;
    return Box{ (int16_t)(mBox.x + 20 + k * 133), (int16_t)y, 126, 48 };
  }
  return Box{ (int16_t)(mBox.x + 20), (int16_t)y, (int16_t)(mBox.w - 40), 48 };
}
static void drawOtaButton(int id, bool pressed) {
  if (id == ID_OTA_START) drawButton(otaBtn(id), view.ota.card == 5 ? tr("Оновити") : tr("Почати"), 1, pressed);
  else if (id == ID_OTA_DEMO) drawButton(otaBtn(id), tr("Пробне"), 0, pressed);
  else if (id == ID_OTA_CLOSE) drawButton(otaBtn(id), otaBusy() ? tr("Скасувати") : tr("Закрити"), otaBusy() ? 2 : 0, pressed);
}
static uint32_t otaShown = 0;
static uint32_t otaSum() {   // всё, от чего зависит вид окна
  const View::Ota &o = view.ota;
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(o.stage); mix(o.percent); mix(o.n); mix(o.more); mix(o.done); mix(o.fail); mix(o.same); mix(o.lost);
  mix(o.card); mix(o.cardWhy); mix(o.selfPct); mix(o.fromCard); mix(o.demo); mix(o.selfWait);
  for (const char *c = o.cardVer; *c; c++) mix((uint8_t)*c);
  for (int i = 0; i < o.n; i++) {
    mix(o.it[i].phase); mix(o.it[i].percent); mix(o.it[i].err); mix(o.it[i].lost);
    for (const char *c = o.it[i].name; *c; c++) mix((uint8_t)*c);
  }
  return h;
}
static void otaItemText(const View::Ota::It &it, char *t, size_t cap) {
  if (it.phase == 6) {   // OS_FAIL
    snprintf(t, cap, "%s", it.err == 1 ? tr("потрібна одна заливка по USB") : it.err == 5 ? tr("не почув передавача") : it.err == 7 ? tr("скасовано") : tr("не вдалося"));
  } else if (it.phase == 5) snprintf(t, cap, "%s", view.ota.demo ? tr("прийняв і перевірив — не записував") : view.ota.stage >= 6 ? tr("оновлено") : tr("оновлено — перезапускається"));
  else if (it.phase == 1) snprintf(t, cap, "%s", tr("уже має цю прошивку"));
  else if (it.lost) snprintf(t, cap, "%s", tr("не відповідає"));
  else if (it.phase == 2) snprintf(t, cap, tr("приймає: %d %%"), it.percent);
  else if (it.phase == 3) snprintf(t, cap, "%s", tr("перевіряє"));
  else if (it.phase == 4) snprintf(t, cap, tr("записує: %d %%"), it.percent);
  else snprintf(t, cap, "—");
}
// Передатчик обновляет сам себя: на весь экран, крупно (владелец 07.10: «нужно чтобы во время обновления передатчика
// на экране была крупная надпись ОНОВЛЕННЯ ПРОШИВКИ НЕ ВИМИКАЙТЕ ЖИВЛЕННЯ!!!, чтобы было очевидно, что происходит»).
// Экран при этом «дышит»: запись во флеш идёт при погашенной подсветке (upd.h), между отрезками записи он загорается.
// Строка рисуется самым крупным шрифтом, какой помещается по ширине.
static void bigLine(int y, const char *s, uint16_t ink) {
  auto f = m2::Gfx::textW(s, F_BIG) <= W - 24 ? F_BIG : m2::Gfx::textW(s, F_H2) <= W - 24 ? F_H2 : F_H;
  g.text(W / 2, y, s, f, ink, AL_C);
}
static void drawSelfUpdate() {
  const View::Ota &o = view.ota;
  char t[96];
  g.fill(0, 0, W, H, C_BG);
  if (o.stage == 10 && o.selfWait) {
    // Перед записью — предупреждение (владелец 07.10: «перед обновлением написать крупное сообщение о том, что экран
    // будет мигать, и сообщить другие попутные данные»): что сейчас будет, сколько займёт, что со звуком и приёмниками.
    bigLine(64, tr("ЗАРАЗ ОНОВИТЬСЯ"), C_FG);
    bigLine(116, tr("ПЕРЕДАВАЧ"), C_FG);
    g.fill(60, 140, W - 120, 3, C_PRIMARY);
    bigLine(200, tr("ЕКРАН БЛИМАТИМЕ"), C_WARN);
    bigLine(252, tr("ТАК МАЄ БУТИ"), C_WARN);
    snprintf(t, sizeof(t), tr("Прошивка: з %s на %s"), view.version, o.cardVer);
    g.text(W / 2, 302, t, F_TXTB, C_FG, AL_C);
    g.text(W / 2, 330, tr("Запис триває близько хвилини. Не вимикайте живлення."), F_CAP, C_MUTED_FG, AL_C);
    g.text(W / 2, 354, tr("Звуку в ефірі не буде до перезапуску передавача."), F_CAP, C_MUTED_FG, AL_C);
    if (o.n) snprintf(t, sizeof(t), tr("Приймачі прошивку отримали (%d) і записують її самі."), o.n - o.same);
    else snprintf(t, sizeof(t), "%s", tr("Приймачі не відповіли — оновляться пізніше, від передавача."));
    g.text(W / 2, 378, t, F_CAP, C_MUTED_FG, AL_C);
    snprintf(t, sizeof(t), tr("початок через %d с"), o.selfWait);
    g.text(W / 2, 440, t, F_H2, C_PRIMARY, AL_C);
    return;
  }
  bool done = o.stage == 11;
  bigLine(96, tr("ОНОВЛЕННЯ"), C_FG);
  bigLine(152, tr("ПРОШИВКИ"), C_FG);
  g.fill(60, 180, W - 120, 3, C_PRIMARY);
  if (done) {
    bigLine(250, tr("ГОТОВО"), C_OK);
    bigLine(306, tr("ПЕРЕЗАПУСК"), C_OK);
  } else {
    bigLine(250, tr("НЕ ВИМИКАЙТЕ"), C_LAMP_RED);
    bigLine(306, tr("ЖИВЛЕННЯ!!!"), C_LAMP_RED);
  }
  int pct = done ? 100 : o.selfPct;
  g.box(40, 366, W - 80, 22, 11, C_SECONDARY);
  int fw = (W - 80) * pct / 100;
  if (fw >= 22) g.box(40, 366, fw, 22, 11, done ? C_OK : C_PRIMARY);
  snprintf(t, sizeof(t), "%d %%", pct);
  g.text(W / 2, 430, t, F_H2, C_FG, AL_C);
  if (!done) g.text(W / 2, 462, tr("екран на час запису пригасає — так має бути"), F_CAP, C_MUTED_FG, AL_C);
}

static void drawOtaModal() {
  const View::Ota &o = view.ota;
  if (otaSelf()) {   // передатчик пишет прошивку в себя — не окно, а весь экран
    drawSelfUpdate();
    otaShown = otaSum();
    return;
  }
  char t[96];
  mBox = Box{ 24, 30, 432, 420 };
  card(mBox, C_CARD, C_BORDER);
  g.text(mBox.x + 20, mBox.y + 38, tr("Оновлення приймачів"), F_H, C_FG);
  int x = mBox.x + 20, w = mBox.w - 40;
  if (o.stage == 0) {   // ещё не начато: что есть на карте и что будет
    // строка о карте: передатчик сам смотрит папку UPDATE
    uint16_t cink = C_MUTED_FG;
    switch (o.card) {
      case 5: snprintf(t, sizeof(t), tr("На картці є прошивка %s."), o.cardVer); cink = C_OK; break;
      case 4: snprintf(t, sizeof(t), tr("На картці та сама прошивка, що вже працює (%s)."), o.cardVer); break;
      case 3: snprintf(t, sizeof(t), "%s", tr("У теці UPDATE на картці прошивки немає.")); break;
      case 6: snprintf(t, sizeof(t), tr("Файл %s не годиться."), o.cardFile); cink = C_WARN; break;
      case 7: snprintf(t, sizeof(t), tr("На картці старіша прошивка (%s) — не пропоную."), o.cardVer); break;
      case 1: snprintf(t, sizeof(t), "%s", tr("Картки пам'яті немає.")); break;
      default: snprintf(t, sizeof(t), "%s", tr("Дивлюсь, що на картці…")); break;
    }
    card(Box{ (int16_t)x, (int16_t)(mBox.y + 56), (int16_t)w, 40 }, C_SECONDARY);
    g.text(x + 14, mBox.y + 82, t, F_CAPB, cink, AL_L, w - 28);
    if (o.card == 5) {
      g.text(x, mBox.y + 124, tr("«Оновити»: спершу приймачі — по радіо, потім передавач."), F_CAP, C_MUTED_FG, AL_L, w);
      g.text(x, mBox.y + 146, tr("Під час запису передавача екран блиматиме."), F_CAP, C_WARN, AL_L, w);
    } else {
      snprintf(t, sizeof(t), tr("«Почати»: приймачам піде прошивка передавача (%s)."), view.version);
      g.text(x, mBox.y + 124, t, F_CAP, C_MUTED_FG, AL_L, w);
      g.text(x, mBox.y + 146, tr("Нова прошивка — файл .bin у теці UPDATE на картці."), F_CAP, C_MUTED_FG, AL_L, w);
    }
    g.text(x, mBox.y + 178, tr("На цей час, близько хвилини, звук в ефірі зупиниться."), F_CAP, C_MUTED_FG, AL_L, w);
    g.text(x, mBox.y + 200, tr("Приймачі мають бути ввімкнені: сплячий не оновиться."), F_CAP, C_MUTED_FG, AL_L, w);
    g.text(x, mBox.y + 222, tr("Приймач, який уже має цю прошивку, нічого не робитиме."), F_CAP, C_MUTED_FG, AL_L, w);
    card(Box{ (int16_t)x, (int16_t)(mBox.y + 244), (int16_t)w, 62 }, C_SECONDARY);
    g.text(mBox.x + mBox.w / 2, mBox.y + 272, tr("«Пробне» — показати, як це виглядає:"), F_CAP, C_MUTED_FG, AL_C);
    g.text(mBox.x + mBox.w / 2, mBox.y + 294, tr("приймачі приймуть прошивку, але не запишуть."), F_CAP, C_MUTED_FG, AL_C);
    drawOtaButton(ID_OTA_START, false);
    hot[hotN++] = Hot{ otaBtn(ID_OTA_START), ID_OTA_START };
    drawOtaButton(ID_OTA_DEMO, false);
    hot[hotN++] = Hot{ otaBtn(ID_OTA_DEMO), ID_OTA_DEMO };
  } else {
    const char *head = "";
    uint16_t ink = C_FG;
    switch (o.stage) {
      case 1: head = tr("Готую прошивку"); break;
      case 2: head = tr("Шукаю приймачі"); break;
      case 3:
      case 4: head = tr("Надсилаю прошивку"); break;
      case 5: head = tr("Приймачі записують прошивку"); break;
      case 6: head = o.fail || o.lost ? tr("Завершено, але не всі оновились") : tr("Готово — приймачі оновлено"); ink = o.fail || o.lost ? C_WARN : C_OK; break;
      case 7: head = o.n ? tr("Оновлювати нікого") : tr("Жоден приймач не відповів"); ink = o.n ? C_OK : C_WARN; break;
      case 8: head = tr("Скасовано"); ink = C_WARN; break;
      case 10: head = tr("Передавач записує прошивку"); break;
      case 11: head = tr("Готово — передавач перезапускається"); ink = C_OK; break;
      case 12: head = tr("Не вдалося записати прошивку в передавач"); ink = C_DANGER; break;
      default: head = tr("Не вдалося прочитати прошивку"); ink = C_DANGER; break;
    }
    if (o.demo && o.stage == 6) head = tr("Пробне оновлення пройшло — нічого не записано");
    g.text(x, mBox.y + 74, head, F_TXTB, ink, AL_L, w - 60);
    bool bar = o.stage == 3 || o.stage == 4 || o.stage == 10;
    if (bar) {
      int pct = o.stage == 10 ? o.selfPct : o.percent;
      snprintf(t, sizeof(t), "%d %%", pct);
      g.text(x + w, mBox.y + 74, t, F_TXTB, C_PRIMARY, AL_R);
      g.box(x, mBox.y + 86, w, 12, 6, C_SECONDARY);
      int fw = w * pct / 100;
      if (fw >= 12) g.box(x, mBox.y + 86, fw, 12, 6, C_PRIMARY);
    }
    const char *sub = o.stage >= 1 && o.stage <= 4 ? (o.demo ? tr("Пробне: приймачі нічого не запишуть. Звук зупинено.") : tr("Звук в ефірі зупинено до кінця передачі."))
                      : o.stage == 5 ? (o.demo ? tr("Звук уже в ефірі.") : tr("Звук уже в ефірі. Не вимикайте приймачі."))
                      : o.stage == 10 ? tr("Не вимикайте. Екран на час запису пригасає — так має бути.")
                      : o.stage == 11 ? tr("Не вимикайте передавач. Звук повернеться за хвилину.") : "";
    g.text(x, mBox.y + (bar ? 118 : 98), sub, F_CAP, C_MUTED_FG, AL_L, w);
    int y0 = mBox.y + 132;
    for (int i = 0; i < o.n; i++) {
      const View::Ota::It &it = o.it[i];
      int y = y0 + i * 50;
      card(Box{ (int16_t)x, (int16_t)y, (int16_t)w, 44 }, C_SECONDARY);
      bool ok = it.phase == 5 || it.phase == 1, bad = it.phase == 6 || it.lost;
      g.circle(x + 16, y + 22, 6, ok ? C_OK : bad ? C_DANGER : C_PRIMARY);
      g.text(x + 32, y + 19, it.name, F_CAPB, C_FG, AL_L, w - 44);
      otaItemText(it, t, sizeof(t));
      g.text(x + 32, y + 37, t, F_CAP, bad ? C_WARN : C_MUTED_FG, AL_L, w - 44);
    }
    if (o.more) {
      snprintf(t, sizeof(t), tr("і ще %d"), o.more);
      g.text(x + w, y0 + o.n * 50 + 12, t, F_CAP, C_MUTED_FG, AL_R);
    }
    if (!o.n && o.stage >= 2 && o.stage <= 4) g.text(mBox.x + mBox.w / 2, y0 + 30, tr("Чекаю відповіді приймачів"), F_CAP, C_MUTED_FG, AL_C);
  }
  if (!otaSelf()) {
    drawOtaButton(ID_OTA_CLOSE, false);
    hot[hotN++] = Hot{ otaBtn(ID_OTA_CLOSE), ID_OTA_CLOSE };
  }
  otaShown = otaSum();
}

static void drawRxModal() {
  int i = rxFind(rxSelId);
  if (i < 0) return;
  const View::Rx &r = view.rx[i];
  mBox = Box{ 24, 14, 432, 452 };
  card(mBox, C_CARD, C_BORDER);
  g.text(mBox.x + 20, mBox.y + 36, r.name, F_H, C_FG, AL_L, 392);
  drawRxLines();
  auto btn = [&](int id) {
    drawRxButton(id, false);
    hot[hotN++] = Hot{ rxBtn(id), id };
  };
  if (r.online) {
    btn(ID_RX_IDENT);
    btn(ID_RX_RENAME);
    btn(ID_RX_VOLDN);
    btn(ID_RX_VOLUP);
    // Приёмник с версии 2.32 сообщает свои настройки — у него на этом месте кнопка «Налаштування» (выход «два канали /
    // протифаза» переехал в то окно первой строкой); у приёмника постарше остаётся прежний переключатель выхода.
    if (r.hasInfo) btn(ID_RX_SET);
    else if (r.fw >= 11) btn(ID_RX_STEREO);   // стерео понимают приёмники с версии 1.1
    btn(ID_RX_ONOFF);
  }
  btn(ID_RX_FORGET);
  btn(ID_RX_CLOSE);
}

// ---- окно «Налаштування приймача»: то же, что в меню самого приёмника, — выход звука, чёткость речи, баланс, предел
// громкости, вид главного экрана, светодиод, язык — и проверка наушников. Каждая строка: название, нынешнее значение
// и кнопки «меньше / больше»; нажатие сразу уходит приёмнику командой, он подтверждает следующим сообщением о себе.
// (Владелец 06.10: функции должны быть парами — что меняется на приёмнике, меняется и отсюда.)
constexpr int RXS_N = 7;
static const char *const RXS_NAME[RXS_N] = { "Вихід звуку", "Чіткість мови", "Баланс", "Межа гучності", "Вигляд екрана", "Світлодіод", "Мова написів" };
static int rxsGet(const View::Rx &r, int row) {   // значение строки числом
  return row == 0 ? (r.stereo ? 1 : 0) : row == 2 ? (int)r.par[1] - 5 : r.par[row - 1];
}
static void rxsText(const View::Rx &r, int row, char *t, size_t n) {
  int v = rxsGet(r, row);
  switch (row) {
    case 0: snprintf(t, n, "%s", v ? tr("два канали") : tr("протифаза")); break;
    case 1: snprintf(t, n, "%s", v == 0 ? tr("вимкнено") : v == 1 ? tr("легка") : v == 2 ? tr("середня") : tr("сильна")); break;
    case 2:
      if (!r.stereo) snprintf(t, n, "%s", tr("1 канал"));
      else if (!v) snprintf(t, n, "%s", tr("рівно"));
      else snprintf(t, n, v < 0 ? tr("ліве +%d") : tr("праве +%d"), v < 0 ? -v : v);
      break;
    case 3: snprintf(t, n, "%d %%", v * 5); break;
    case 4: snprintf(t, n, "%s", v == 1 ? tr("стрілки") : v == 2 ? tr("гучність") : tr("спектр")); break;
    case 5: snprintf(t, n, "%s", v == 0 ? tr("вимкнено") : v == 1 ? tr("тьмяно") : v == 3 ? tr("яскраво") : tr("норма")); break;
    default: snprintf(t, n, "%s", v == 1 ? "українська" : v == 2 ? "English" : tr("як у передавача"));
  }
}
// новое значение строки после нажатия «меньше» (d = −1) или «больше» (+1); вид экрана и язык идут по кругу
static int rxsStep(const View::Rx &r, int row, int d) {
  static const int8_t LO[RXS_N] = { 0, 0, -5, 4, 0, 0, 0 }, HI[RXS_N] = { 1, 3, 5, 20, 2, 3, 2 };
  int v = rxsGet(r, row) + d;
  if (row == 0 || row == 4 || row == 6) return v < LO[row] ? HI[row] : v > HI[row] ? LO[row] : v;
  return v < LO[row] ? LO[row] : v > HI[row] ? HI[row] : v;
}
static Box rxsBox(int id) {
  if (id == ID_RXS_EAR) return Box{ (int16_t)(mBox.x + 16), (int16_t)(mBox.y + mBox.h - 62), 258, 48 };
  if (id == ID_RXS_OK) return Box{ (int16_t)(mBox.x + 282), (int16_t)(mBox.y + mBox.h - 62), 134, 48 };
  bool plus = id >= ID_RXS_PLUS;
  int row = id - (plus ? ID_RXS_PLUS : ID_RXS_MINUS);
  return Box{ (int16_t)(mBox.x + mBox.w - 16 - (plus ? 50 : 106)), (int16_t)(mBox.y + 56 + row * 46), 50, 40 };
}
static void drawRxSetButton(int id, bool pressed) {
  int i = rxFind(rxSelId);
  Box b = rxsBox(id);
  if (id == ID_RXS_EAR) return drawButton(b, i >= 0 && view.rx[i].earTest ? tr("Зупинити перевірку") : tr("Перевірка навушників"), 0, pressed);
  if (id == ID_RXS_OK) return drawButton(b, tr("Готово"), 1, pressed);
  bool plus = id >= ID_RXS_PLUS;
  int row = id - (plus ? ID_RXS_PLUS : ID_RXS_MINUS);
  bool dead = i >= 0 && row == 2 && !view.rx[i].stereo;   // баланс при одном канале не к чему приложить
  card(b, pressed && !dead ? m2::Gfx::blend(C_SECONDARY, C_FG, 40) : C_SECONDARY, C_BORDER);
  uint16_t ink = dead ? C_BORDER : C_FG;
  g.box(b.x + b.w / 2 - 9, b.y + b.h / 2 - 2, 18, 4, 1, ink);   // знаки — фигурами
  if (plus) g.box(b.x + b.w / 2 - 2, b.y + b.h / 2 - 9, 4, 18, 1, ink);
}
static void drawRxSetRow(int row) {
  int i = rxFind(rxSelId);
  if (i < 0) return;
  char t[40];
  int y = mBox.y + 56 + row * 46;
  g.fill(mBox.x + 16, y, mBox.w - 32 - 112, 40, C_CARD);
  g.text(mBox.x + 18, y + 26, tr(RXS_NAME[row]), F_CAP, C_MUTED_FG);
  rxsText(view.rx[i], row, t, sizeof(t));
  g.text(mBox.x + mBox.w - 16 - 116, y + 26, t, F_CAPB, C_FG, AL_R, 150);
}
static uint32_t rxsShown;
static uint32_t rxsSum() {   // что сейчас показано: изменилось (приёмник сообщил другое) — перерисовать
  int i = rxFind(rxSelId);
  if (i < 0) return 0;
  const View::Rx &r = view.rx[i];
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  for (int k = 0; k < 6; k++) mix(r.par[k]);
  mix(r.stereo);
  mix(r.earTest);
  mix(r.online);
  return h;
}
static void drawRxSetModal() {
  int i = rxFind(rxSelId);
  if (i < 0) return;
  mBox = Box{ 24, 14, 432, 452 };
  card(mBox, C_CARD, C_BORDER);
  g.text(mBox.x + 20, mBox.y + 36, view.rx[i].name, F_H, C_FG, AL_L, 392);
  for (int row = 0; row < RXS_N; row++) {
    drawRxSetRow(row);
    for (int id : { ID_RXS_MINUS + row, ID_RXS_PLUS + row }) {
      drawRxSetButton(id, false);
      hot[hotN++] = Hot{ rxsBox(id), id };
    }
  }
  for (int id : { ID_RXS_EAR, ID_RXS_OK }) {
    drawRxSetButton(id, false);
    hot[hotN++] = Hot{ rxsBox(id), id };
  }
  rxsShown = rxsSum();
}

// ---- экранная клавиатура (имя приёмника)
struct Key {
  Box b;
  const char *label;
  uint8_t kind;   // 0 буква, 1 регистр, 2 стереть, 3 цифры/буквы, 4 пробел, 5 отмена, 6 готово
};
static Key keys[52];
static int keyN = 0;

static void upperUtf8(const char *in, char *out) {   // строчная буква → заглавная (кириллица и латиница)
  uint8_t a = in[0], b = in[1];
  out[0] = a;
  out[1] = b;
  out[2] = 0;
  if (a < 0x80) {
    if (a >= 'a' && a <= 'z') out[0] = a - 32;
    out[1] = 0;
  } else if (a == 0xD0 && b >= 0xB0 && b <= 0xBF) out[1] = b - 0x20;          // а…п
  else if (a == 0xD1 && b >= 0x80 && b <= 0x8F) out[0] = 0xD0, out[1] = b + 0x20;   // р…я
  else if (a == 0xD1 && (b == 0x94 || b == 0x96 || b == 0x97)) out[0] = 0xD0, out[1] = b - 0x10;   // є і ї
  else if (a == 0xD2 && b == 0x91) out[1] = 0x90;                              // ґ
}

static void kbLayout() {
  static const char *const L1[] = { "й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х", "ї" };
  static const char *const L2[] = { "ф", "і", "в", "а", "п", "р", "о", "л", "д", "ж", "є" };
  static const char *const L3[] = { "я", "ч", "с", "м", "и", "т", "ь", "б", "ю", "ґ" };
  static const char *const E1[] = { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "(", ")" };   // английская раскладка
  static const char *const E2[] = { "a", "s", "d", "f", "g", "h", "j", "k", "l", "/", ":" };
  static const char *const E3[] = { "z", "x", "c", "v", "b", "n", "m", "&", "+", "#" };
  static const char *const D1[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "№", "-" };
  static const char *const D2[] = { "!", "?", "(", ")", "/", ":", ";", "+", "=", "«", "»" };
  static const char *const D3[] = { "a", "b", "c", "d", "e", "f", "g", "h", "i", "j" };
  keyN = 0;
  auto add = [&](int x, int y, int w, const char *label, uint8_t kind) {
    keys[keyN++] = Key{ Box{ (int16_t)x, (int16_t)y, (int16_t)w, 52 }, label, kind };
  };
  const int y1 = 150, y2 = 208, y3 = 266, y4 = 324;
  for (int i = 0; i < 12; i++) add(1 + i * 40, y1, 38, kbDigits ? D1[i] : uiLang ? E1[i] : L1[i], 0);
  for (int i = 0; i < 11; i++) add(21 + i * 40, y2, 38, kbDigits ? D2[i] : uiLang ? E2[i] : L2[i], 0);
  add(1, y3, 56, "", 1);
  for (int i = 0; i < 10; i++) add(60 + i * 36, y3, 34, kbDigits ? D3[i] : uiLang ? E3[i] : L3[i], 0);
  add(422, y3, 57, "", 2);
  add(1, y4, 78, kbDigits ? tr("абв") : "123", 3);
  add(82, y4, 44, ",", 0);
  add(129, y4, 222, "", 4);
  add(354, y4, 40, ".", 0);
  add(397, y4, 40, "'", 0);
  add(440, y4, 39, "-", 0);
  add(16, 392, 216, tr("Скасувати"), 5);
  keys[keyN - 1].b.h = 62;
  add(248, 392, 216, tr("Готово"), 6);
  keys[keyN - 1].b.h = 62;
}

static void drawKey(int i, bool pressed) {
  const Key &k = keys[i];
  char up[4];
  if (k.kind >= 5) {
    drawButton(k.b, k.label, k.kind == 6 ? 1 : 0, pressed);
    return;
  }
  uint16_t fill = pressed ? C_PRIMARY : (k.kind == 0 || k.kind == 4) ? C_SECONDARY : C_MUTED;
  if (k.kind == 1 && kbShift) fill = C_PRIMARY;
  uint16_t ink = pressed || (k.kind == 1 && kbShift) ? C_PRIMARY_FG : C_FG;
  g.box(k.b.x, k.b.y, k.b.w, k.b.h, 6, fill);
  int cx = k.b.x + k.b.w / 2, cy = k.b.y + k.b.h / 2;
  switch (k.kind) {
    case 0:
      if (kbShift && !kbDigits) {
        upperUtf8(k.label, up);
        g.text(cx, cy + 7, up, F_TXT, ink, AL_C);
      } else g.text(cx, cy + 7, k.label, F_TXT, ink, AL_C);
      break;
    case 1: {   // стрелка вверх
      const float a[] = { (float)cx - 11, (float)cy + 1, (float)cx, (float)cy - 11, (float)cx + 11, (float)cy + 1 };
      g.poly(a, 3, ink);
      g.fill(cx - 5, cy + 1, 10, 9, ink);
      break;
    }
    case 2: {   // стереть
      const float a[] = { (float)cx - 16, (float)cy, (float)cx - 6, (float)cy - 10, (float)cx - 6, (float)cy + 10 };
      g.poly(a, 3, ink);
      g.box(cx - 7, cy - 10, 22, 20, 3, ink);
      g.line(cx, cy - 4, cx + 8, cy + 4, 2.2f, fill);
      g.line(cx + 8, cy - 4, cx, cy + 4, 2.2f, fill);
      break;
    }
    case 3: g.text(cx, cy + 6, k.label, F_CAPB, ink, AL_C); break;
    case 4: g.box(cx - 40, cy + 8, 80, 3, 1, C_MUTED_FG); break;
  }
}

// Сколько букв можно набрать: имя источника — 13 (место на кнопке), имя приёмника — 32, а приёмнику с прошивкой
// до 2.19 — 13 (длиннее он не запомнит); название папки — 32.
static int kbWide = 1;   // имя для приёмника, который понимает длинные имена
static int kbLimit() {
  return kbForSource ? 13 : kbForDir ? 32 : kbWide ? 32 : 13;
}
static uint8_t kbRelabel = 255;   // с какой клавиши продолжать отложенную перерисовку подписей (255 — нечего)

static void drawKbField() {
  card(Box{ 16, 56, 448, 62 }, C_CARD, C_PRIMARY);
  const char *v = kbText;   // длинное имя — показываем конец
  while (*v && m2::Gfx::textW(v, F_H) > 400) {
    v++;
    while (((uint8_t)*v & 0xC0) == 0x80) v++;
  }
  int w = g.text(34, 96, v, F_H, C_FG);
  g.fill(36 + w, 72, 3, 30, C_PRIMARY);
}

static void drawKeys() {   // только клавиши — фон и поле не трогаются, экран не мигает
  for (int i = 0; i < keyN; i++) drawKey(i, i == kbPressed);
}

static void drawKeyboard() {
  char lim[40];
  g.fill(0, 0, W, H, C_BG);
  g.text(18, 38, kbForDir ? tr("Назва нової теки") : kbForSource ? tr("Назва джерела звуку") : tr("Ім'я приймача"), F_H2, C_FG);
  snprintf(lim, sizeof(lim), tr("до %d літер"), kbLimit());
  g.text(462, 38, lim, F_CAP, C_MUTED_FG, AL_R);
  drawKbField();
  kbLayout();
  drawKeys();
}

// Какая клавиша под точкой. Как в радио: промежутков между клавишами нет — берётся ближайшая в ряду.
static int kbKeyAt(int x, int y) {
  if (y < 142) return -1;
  int best = -1, bestD = 1 << 30;
  for (int i = 0; i < keyN; i++) {
    const Box &b = keys[i].b;
    if (y < b.y - 4 || y >= b.y + b.h + 4) continue;   // ряд — по высоте (с запасом на промежуток)
    int d = x < b.x ? b.x - x : x >= b.x + b.w ? x - (b.x + b.w - 1) : 0;
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

// увеличенная клавиша над пальцем
static Box kbPopBox(int i) {
  const Box &b = keys[i].b;
  int x = b.x + b.w / 2 - 30;
  x = x < 2 ? 2 : x > W - 62 ? W - 62 : x;
  return Box{ (int16_t)x, (int16_t)(b.y - 70), 60, 66 };
}
static void kbPopDraw(int i) {
  if (i < 0 || keys[i].kind != 0) return;
  Box p = kbPopBox(i);
  char up[4];
  const char *t = keys[i].label;
  if (kbShift && !kbDigits) {
    upperUtf8(t, up);
    t = up;
  }
  g.box(p.x, p.y, p.w, p.h, 10, C_PRIMARY);
  g.box(p.x + 2, p.y + 2, p.w - 4, p.h - 4, 8, C_CARD);
  g.text(p.x + p.w / 2, p.y + 46, t, F_BIG, C_PRIMARY, AL_C);
}
// вернуть то, что было под увеличенной клавишей: только в её прямоугольнике
static void kbPopErase(int i) {
  if (i < 0 || keys[i].kind != 0) return;
  Box p = kbPopBox(i);
  m2::Rect old = g.narrow(p.x, p.y, p.w, p.h);
  g.fill(p.x, p.y, p.w, p.h, C_BG);
  if (p.y < 120) drawKbField();
  for (int k = 0; k < keyN; k++) {
    const Box &b = keys[k].b;
    if (b.x < p.x + p.w && b.x + b.w > p.x && b.y < p.y + p.h && b.y + b.h > p.y) drawKey(k, k == kbPressed);
  }
  g.restore(old);
}

static void closeModal();
static void toast(const char *text);
static uint32_t kbDownMs = 0, kbRepMs = 0;
static bool kbRepeated = false;

static void kbBackspace() {
  size_t len = strlen(kbText);
  if (!len) return;
  // Стереть букву целиком. До 2.19 стирался один байт: проверялся уже обнулённый байт, и от кириллической буквы
  // (два байта) в тексте оставалась половина — на её месте потом виден «не тот знак», а следующие буквы портились
  // (владелец 07.10: «клавиатура не всегда записывает верные символы»; имя «Зал, 2 ряд 4?» в списке приёмников).
  uint8_t c;
  do {
    c = (uint8_t)kbText[--len];
    kbText[len] = 0;
  } while (len && (c & 0xC0) == 0x80);
  drawKbField();
  if (!len && !kbShift) {   // пусто — следующая буква снова заглавная
    kbShift = true;
    kbRelabel = 0;
  }
}

static void kbAct(int i) {
  const Key &k = keys[i];
  size_t len = strlen(kbText);
  char up[4];
  switch (k.kind) {
    case 0:
    case 4: {
      const char *add = k.kind == 4 ? " " : k.label;
      if (k.kind == 0 && kbShift && !kbDigits) {
        upperUtf8(k.label, up);
        add = up;
      }
      if (len + strlen(add) < sizeof(kbText) && utf8Letters(kbText) < kbLimit()) {
        strcat(kbText, add);
        drawKbField();
        if (kbShift && k.kind == 0) {   // после первой буквы — строчные
          kbShift = false;
          kbRelabel = 0;
        }
      }
      break;
    }
    case 1:
      kbShift = !kbShift;
      kbRelabel = 0;
      break;
    case 2: kbBackspace(); break;
    case 3:
      kbDigits = !kbDigits;
      kbLayout();
      kbRelabel = 0;
      break;
    case 5:
      if (kbForDir) {   // передумали создавать папку — назад к списку файлов
        kbForDir = false;
        fmNewDir(NULL);
        break;
      }
      modal = kbForSource ? M_NONE : M_RX;
      kbForSource = false;
      needFull = true;
      break;
    case 6: {
      while (len && kbText[len - 1] == ' ') kbText[--len] = 0;
      if (kbForDir) {
        kbForDir = false;
        fmNewDir(kbText);
        break;
      }
      if (kbForSource) {   // имя источника звука; пустое — вернуть обычное «З пульта»
        out.setSrcName = true;
        strlcpy(out.srcName, kbText, sizeof(out.srcName));
        strlcpy(view.srcName, kbText, sizeof(view.srcName));
        kbForSource = false;
        modal = M_NONE;
        needFull = true;
        break;
      }
      if (len) {
        out.rxCmd = RXC_NAME;
        memcpy(out.rxId, rxSelId, 3);
        strlcpy(out.rxName, kbText, sizeof(out.rxName));
        int r = rxFind(rxSelId);
        if (r >= 0) strlcpy(view.rx[r].name, kbText, RX_NAME);
      }
      modal = M_RX;
      needFull = true;
      break;
    }
  }
}

// Касания клавиатуры: клавиша под пальцем подсвечена и показана крупно; палец можно вести; буква вводится, когда
// палец отпустили. События приходят из очереди сенсора (panel4848.h) — каждое со своим временем ms.
//
// Какая клавиша считается нажатой (владелец 07.10: «клавиатура не всегда записывает верные символы»):
//  — первая точка касания даёт клавишу сразу (подсветка без задержки);
//  — на соседнюю клавиша меняется, только если палец пробыл над ней 35 мс подряд (и вышел за край прежней на
//    6 точек). Отрыв пальца даёт одну-три «уехавшие» точки за 10–30 мс — они букву больше не меняют;
//  — если самой первой точкой сенсор промахнулся (палец ложится краем, пятно ещё мало), а дальше палец стоял на
//    другой клавише, но отпустили его раньше 35 мс, — берётся та, на которой он стоял.
static int kbCand = -1;                 // клавиша, на которую палец, похоже, переходит
static uint32_t kbCandMs = 0, kbSinceMs = 0;
static volatile uint32_t kbPressUs, kbReleaseUs, kbTaps;   // самая долгая обработка нажатия и отпускания, мкс; сколько букв введено
static void kbShow(int k, uint32_t ms) {   // подсветить другую клавишу (или никакую)
  int old = kbPressed;
  kbPressed = k;
  if (old >= 0) {
    kbPopErase(old);
    drawKey(old, false);
  }
  if (k >= 0) {
    drawKey(k, true);
    kbPopDraw(k);
  }
  kbDownMs = ms;
  kbSinceMs = ms;
  kbRepeated = false;
  kbCand = -1;
}
static void kbTouch(bool down, int tx, int ty, uint32_t ms) {
  uint32_t t0 = micros();
  if (down) {
    int k = kbKeyAt(tx, ty);
    if (kbPressed < 0 && !wasDown) kbShow(k, ms);   // палец только что лёг
    else if (kbPressed >= 0) {
      const Box &b = keys[kbPressed].b;
      if (tx >= b.x - 6 && tx < b.x + b.w + 6 && ty >= b.y - 6 && ty < b.y + b.h + 6) k = kbPressed;
      if (k == kbPressed) kbCand = -1;
      else if (k != kbCand) {
        kbCand = k;
        kbCandMs = ms;
      } else if (ms - kbCandMs >= 35) kbShow(k, ms);
      if (kbPressed >= 0 && keys[kbPressed].kind == 2 && kbCand < 0 && ms - kbDownMs > 500 && ms - kbRepMs > 110) {   // «стереть» держат
        kbRepMs = ms;
        kbRepeated = true;
        kbBackspace();
      }
    } else if (k >= 0 && k != kbCand) {   // палец лёг мимо клавиш и пришёл на клавишу
      kbCand = k;
      kbCandMs = ms;
    } else if (k >= 0 && ms - kbCandMs >= 35) kbShow(k, ms);
    uint32_t d = micros() - t0;
    if (d > kbPressUs) kbPressUs = d;
  } else if (wasDown) {
    int k = kbPressed;
    if (k >= 0 && kbCand >= 0 && ms - kbCandMs >= 15 && kbCandMs - kbSinceMs < 15) k = kbCand;   // первая точка была промахом
    kbCand = -1;
    if (k >= 0) {
      int shown = kbPressed;
      kbPressed = -1;
      // Сначала убрать увеличенную клавишу, потом ввести букву. Пробовал наоборот — чтобы поле ввода рисовалось один
      // раз, силами стирания; но стирание рисует только в прямоугольнике увеличенной клавиши, и буква от клавиш верхнего
      // ряда в поле не появлялась (владелец 07.10: «удалил все символы, после чего символ З ввести не могу, и так со
      // многими»). Проверять такие правки картинкой экрана (tools/sim), а не только набранным текстом.
      if (shown >= 0) {
        kbPopErase(shown);
        drawKey(shown, false);
      }
      if (!(keys[k].kind == 2 && kbRepeated)) kbAct(k);
      kbRepeated = false;
      kbTaps = kbTaps + 1;
    }
    uint32_t d = micros() - t0;
    if (d > kbReleaseUs) kbReleaseUs = d;
  }
  wasDown = down;
}
// Подписи клавиш после смены регистра или раскладки перерисовываются не разом (это десятки миллисекунд, и следующее
// нажатие ждало бы), а по несколько клавиш за кадр; клавиши под увеличенной буквой пропускаются — их вернёт её стирание.
static void kbLive() {
  if (kbRelabel >= keyN) return;
  Box pop = kbPressed >= 0 && keys[kbPressed].kind == 0 ? kbPopBox(kbPressed) : Box{ 0, 0, 0, 0 };
  for (int n = 0; n < 7 && kbRelabel < keyN; kbRelabel++) {
    const Box &b = keys[kbRelabel].b;
    if (keys[kbRelabel].kind > 3) continue;
    if (pop.w && b.x < pop.x + pop.w && b.x + b.w > pop.x && b.y < pop.y + pop.h && b.y + b.h > pop.y) continue;
    drawKey(kbRelabel, kbRelabel == kbPressed);
    n++;
  }
  if (kbRelabel >= keyN) kbRelabel = 255;
}

#include "fmui.h"   // экран «Картка пам'яті» — файловый менеджер

static bool modalAgain;   // окно перерисовывается поверх себя же (живые окна: ход обновления, просьбы о доступе)
static void drawModal() {
  hotN = 0;
  if (modal == M_KEYS) {
    drawKeyboard();
    return;
  }
  if (modal == M_FILES) {
    fmDraw();
    return;
  }
  if (!modalAgain) g.fillA(0, 0, W, H, 0x0000, 150);   // притемнить страницу (при перерисовке того же окна — уже притемнена)
  if (modal == M_RX) {
    drawRxModal();
    return;
  }
  if (modal == M_RXSET) {
    drawRxSetModal();
    return;
  }
  if (modal == M_PAIR) {
    drawPairModal();
    return;
  }
  if (modal == M_OTA) {
    drawOtaModal();
    return;
  }
  if (modal == M_SHEET) {
    int h = 58 + SHEET_N * 48 + 70;
    mBox = Box{ 24, (int16_t)((H - h) / 2), 432, (int16_t)h };
    card(mBox, C_CARD, C_BORDER);
    g.text(mBox.x + 20, mBox.y + 38, tr("Перевірочний звук"), F_H, C_FG);
    for (int k = 0; k < SHEET_N; k++) mHot(ID_SHEET + k);
    Box b = Box{ (int16_t)(mBox.x + 16), (int16_t)(mBox.y + h - 64), (int16_t)(mBox.w - 32), 50 };
    drawButton(b, tr("Готово"), 1, false);
    hot[hotN++] = Hot{ b, ID_M_OK };
    return;
  }
  if (modal == M_PARAM) {
    const ParamDef &d = PARAM[modalArg];
    int lines = wrap(0, 0, 392, 22, d.desc, F_CAP, 0, 99, 0, false);
    int nopt = 0;
    if (d.kind == K_LIST)
      while (d.opts[nopt]) nopt++;
    mOptY = 62 + lines * 22 + 8;
    mCols = nopt > 7 ? 2 : 1;
    mOptStep = mCols > 1 ? 48 : nopt > 6 ? 44 : nopt > 5 ? 48 : 56;
    int h = d.kind == K_LIST ? mOptY + (nopt + mCols - 1) / mCols * mOptStep + 12 : 72 + lines * 22 + 96 + 96;
    mBox = Box{ 24, (int16_t)((H - h) / 2), 432, (int16_t)h };
    card(mBox, C_CARD, C_BORDER);
    g.text(mBox.x + 20, mBox.y + 38, d.name, F_H, C_FG);
    wrap(mBox.x + 20, mBox.y + 68, 392, 22, d.desc, F_CAP, C_MUTED_FG);
    if (d.kind == K_LIST) {
      for (int k = 0; k < nopt; k++) mHot(ID_M_OPT + k);
    } else {
      mHot(ID_M_MINUS);
      mHot(ID_M_PLUS);
      drawModalValue();
      if (d.unit[0]) g.text(mBox.x + mBox.w / 2, mItem(ID_M_MINUS).y + 82, tr(d.unit) + 1, F_CAP, C_MUTED_FG, AL_C);
      mHot(ID_M_CANCEL);
      mHot(ID_M_OK);
    }
  } else if (modal == M_ABOUT) {
    mBox = Box{ 24, 60, 432, 360 };
    card(mBox, C_CARD, C_BORDER);
    g.text(mBox.x + 20, mBox.y + 38, tr("Стан і про пристрій"), F_H, C_FG);
    drawAboutLines();
    Box b = mItem(ID_M_OK);
    b.x = mBox.x + 20;
    b.w = mBox.w - 40;
    drawButton(b, tr("Закрити"), 1, false);
    hot[hotN++] = Hot{ b, ID_M_OK };
  } else if (modal == M_CONFIRM) {
    static const char *const TITLE[] = { "Знайти вільний канал?", "Перезапустити?", "Скинути всі налаштування?", "Зупинити запис?",
                                         "Вимкнути передавач?", "Видалити приймач з набору?", "Увімкнути перевірочний звук?" };
    static const char *const TEXT[] = {
      "Передавач за дві секунди перевірить усі канали й стане на найвільніший. На цей час звук у навушниках зникне, потім приймачі самі знайдуть новий канал.",
      "Передача перерветься на кілька секунд.",
      "Канал, набір, гучність входу та інші налаштування повернуться до початкових. Пристрій перезапуститься.",
      "Файл буде збережено на картці.",
      "Передача припиниться, екран згасне, усе засне. Щоб увімкнути знову — торкніться екрана.",
      "Приймач втратить доступ до звуку: ключ набору зміниться й сам дійде до інших приймачів. Повернути його можна через «Додати приймач».",
      "Замість звуку з джерела в ефір піде перевірочний звук. Повернути — ліва кнопка у цьому ряду.",
    };
    int lines = wrap(0, 0, 392, 24, TEXT[modalArg], F_TXT, 0, 99, 0, false);
    int h = 64 + lines * 24 + 100;
    mBox = Box{ 24, (int16_t)((H - h) / 2), 432, (int16_t)h };
    card(mBox, C_CARD, C_BORDER);
    g.text(mBox.x + 20, mBox.y + 38, TITLE[modalArg], F_H, C_FG);
    wrap(mBox.x + 20, mBox.y + 72, 392, 24, TEXT[modalArg], F_TXT, C_MUTED_FG);
    mHot(ID_M_CANCEL);
    mHot(ID_M_OK);
  }
}

// ---------------------------------------------------------------- переходы и перерисовка
static void drawAll() {
  g.target(drawBuf(), 0, 0, W, H);
  needFull = false;
  lampN = 0;   // огоньки страницы записываются заново теми, кто их рисует
  fullDraws++;
  if (modal == M_KEYS) {
    hotN = 0;
    drawKeyboard();
    return;
  }
  if (modal == M_FILES) {   // окно во весь экран: страница под ним не рисуется
    fmDraw();
    return;
  }
  drawHeader();
  drawContent();
  drawTabs();
  if (modal != M_NONE) drawModal();
  needFull = false;
}

// Перелистывание страниц: новая страница въезжает сбоку и выталкивает прежнюю — быстро пошла, мягко встала.
// История (всё по словам владельца):
//  · до 2.3 страницу двигали переносом памяти, 7 шагов по 150…8 точек; каждый шаг (330 КБ во внешней памяти) шёл дольше
//    кадра экрана и поперёк развёртки — «плавности нет, страница перелистывается с рывками кадров»;
//  · в 2.3 сделал «шторку» (новая страница открывается за кромкой) — плавно, но «старое перелистывание было красивее»;
//  · с 2.4 — прежний вид, а плавность даёт сам вывод кадра: экран складывает каждую строку из двух листов со сдвигом
//    (panel4848.h, lcdFill). Память не переносится, сдвиг меняется строго между кадрами — 42 шага в секунду без разрывов.
// Новая страница целиком рисуется в теневой лист; основной лист (прежняя страница) до конца не трогается.
static uint32_t slideFrames, slideMs, slideMaxUs;   // последнее перелистывание: шагов, длительность движения, самый долгий шаг (для отчёта)
static void slideTo(int newPage) {
  int dir = newPage > page ? 1 : -1;
  page = newPage;
  article = -1;
  tabSince = millis();
  if (!animate || !fbShadow) {
    needFull = true;
    return;
  }
  g.target(fbShadow, 0, 0, W, H);
  lampN = 0;
  drawHeader();
  drawContent();
  drawTabs();
  uiQuiet(true);    // разбор мелодии на время движения стоит: ядро нужно выводу кадра (см. mpQuiet в txmp3.h)
  uiWaitFrame();
  uint32_t t0 = millis(), u0 = micros();
  slideMaxUs = 0;
  slideFrames = 0;
  if (uiSlide(fbShadow, 0, dir, CY, CY + CH)) {   // шапка и вкладки — сразу новые, страница ещё на месте
    const int N = 12;
    for (int i = 1; i <= N; i++) {
      float k = 1 - (float)i / N;
      k = 1 - k * k * k;                           // 110, 92, 75, 61, 47, 35, 25, 17, 10, 6, 2 точек за шаг
      uiWaitFrame();
      uiSlide(fbShadow, i == N ? W : (int)(W * k + 0.5f), dir, CY, CY + CH);
      uint32_t u = micros();
      if (u - u0 > slideMaxUs) slideMaxUs = u - u0;
      u0 = u;
    }
    slideFrames = N;
    slideMs = millis() - t0;
    uiSlideSync();                                 // новая страница целиком на экране — основной лист больше не читается
    copyRows(0, 0, W, H);
    uiSlide(NULL, 0, 0, 0, 0);                     // экран снова показывает основной лист (он уже такой же)
    uiSlideSync();
  } else {                                         // экран сдвигать не умеет — страница просто сменяется
    copyRows(0, 0, W, H);
    slideMs = millis() - t0;
  }
  uiQuiet(false);
  g.rn = 0;   // экран уже совпадает с теневым листом
  toastOn = false;
  tabSince = millis();
}

static void openParam(int p) {
  if (PARAM[p].kind == K_TOGGLE) {
    out.setParam = p;
    out.setValue = !view.val[p];
    view.val[p] = out.setValue;
    for (int k = 1; k <= 6; k++) {   // бегунок переезжает
      Box b = itemBox(ID_ROW + p);
      float t = k / 6.0f;
      drawToggle(b.x + b.w - 68, b.y + (b.h - 28) / 2, view.val[p] ? t : 1 - t);
      pause(14);
    }
    return;
  }
  modal = M_PARAM;
  modalArg = p;
  modalValue = view.val[p];
  drawModal();
}

static void closeModal() {
  modal = sheetBack ? M_SHEET : M_NONE;   // настройку открывали из списка — вернуться в список
  sheetBack = false;
  needFull = true;
}

static void applyStep(int d) {
  const ParamDef &pd = PARAM[modalArg];
  int v = modalValue + d * pd.step;
  if (modalArg == P_CHANNEL) v = v < pd.lo ? pd.hi : v > pd.hi ? pd.lo : v;
  v = v < pd.lo ? pd.lo : v > pd.hi ? pd.hi : v;
  if (v == modalValue) return;
  modalValue = v;
  drawModalValue();
  if (modalArg == P_BRIGHT) {   // яркость видно сразу
    out.setParam = P_BRIGHT;
    out.setValue = v;
  }
}

static void activate(int id) {
  if (modal == M_FILES) return fmActivate(id);
  if (modal == M_PAIR) {
    if (id == ID_PAIR_CLOSE) {
      out.pairOpen = 0;
      closeModal();
    } else if (id >= ID_PAIR_OK && id < ID_PAIR_OK + view.askN) {
      out.pairApprove = view.ask[id - ID_PAIR_OK].slot;
      toast(tr("Доступ дозволено — приймач з'явиться у списку"));
    }
    return;
  }
  if (modal == M_OTA) {
    if (id == ID_OTA_START || id == ID_OTA_DEMO) {
      if (id == ID_OTA_DEMO) out.otaDemo = true;
      else if (view.ota.card == 5) out.otaCard = true;   // на карте есть прошивка — обновить всё ею
      else out.otaStart = true;
      view.ota.stage = 1;   // сразу показать «готую» (настоящее состояние придёт со следующим кадром)
      drawModal();
    } else if (id == ID_OTA_CLOSE) {
      if (otaBusy()) out.otaCancel = true;
      else closeModal();
    }
    return;
  }
  if (modal == M_RX) {
    int i = rxFind(rxSelId);
    memcpy(out.rxId, rxSelId, 3);
    switch (id) {
      case ID_RX_CLOSE: closeModal(); break;
      case ID_RX_IDENT: out.rxCmd = RXC_IDENTIFY; break;
      case ID_RX_RENAME:
        if (i >= 0) {
          strlcpy(kbText, view.rx[i].name, sizeof(kbText));
          utf8Clean(kbText);   // обрывки букв от прежней ошибки стирания — убрать
          kbShift = !kbText[0];
          kbWide = view.rx[i].wide;
          kbDigits = false;
          modal = M_KEYS;
          drawModal();
        }
        break;
      case ID_RX_VOLDN:
      case ID_RX_VOLUP:
        if (i >= 0) {
          int v = view.rx[i].volume + (id == ID_RX_VOLUP ? 1 : -1);
          v = v < 0 ? 0 : v > 20 ? 20 : v;
          view.rx[i].volume = v;
          view.rx[i].mute = false;
          out.rxCmd = RXC_VOLUME;
          out.rxArg = v;
          drawRxLines();
        }
        break;
      case ID_RX_ONOFF:
        if (i >= 0) {
          out.rxCmd = RXC_ENABLE;
          out.rxArg = view.rx[i].off ? 1 : 0;
          view.rx[i].off = !view.rx[i].off;
          drawRxLines();
          drawRxButton(ID_RX_ONOFF, false);
        }
        break;
      case ID_RX_STEREO:
        if (i >= 0) {
          out.rxCmd = RXC_STEREO;
          out.rxArg = view.rx[i].stereo ? 0 : 1;
          view.rx[i].stereo = !view.rx[i].stereo;
          drawRxLines();
          drawRxButton(ID_RX_STEREO, false);
        }
        break;
      case ID_RX_FORGET:
        modal = M_CONFIRM;
        modalArg = CF_FORGET;
        drawModal();
        break;
      case ID_RX_SET:   // настройки приёмника — отдельным окном поверх этого
        modal = M_RXSET;
        modalAgain = true;
        drawModal();
        modalAgain = false;
        break;
    }
    return;
  }
  if (modal == M_RXSET) {
    int i = rxFind(rxSelId);
    memcpy(out.rxId, rxSelId, 3);
    if (id == ID_RXS_OK || i < 0) {   // назад в окно приёмника
      modal = M_RX;
      needFull = true;
    } else if (id == ID_RXS_EAR) {
      View::Rx &r = view.rx[i];
      r.earTest = !r.earTest;
      out.rxCmd = RXC_EARTEST;
      out.rxArg = r.earTest ? 1 : 0;
      drawRxSetButton(ID_RXS_EAR, false);
      rxsShown = rxsSum();
    } else if (id >= ID_RXS_MINUS && id < ID_RXS_PLUS + RXS_N) {
      View::Rx &r = view.rx[i];
      bool plus = id >= ID_RXS_PLUS;
      int row = id - (plus ? ID_RXS_PLUS : ID_RXS_MINUS);
      if (row == 2 && !r.stereo) return;   // баланс при одном канале
      int v = rxsStep(r, row, plus ? 1 : -1);
      if (v == rxsGet(r, row)) return;
      if (row == 0) {
        r.stereo = v;
        out.rxCmd = RXC_STEREO;
        out.rxArg = v;
        drawRxSetRow(2);                   // баланс зависит от выхода: строка и её кнопки
        drawRxSetButton(ID_RXS_MINUS + 2, false);
        drawRxSetButton(ID_RXS_PLUS + 2, false);
      } else {
        r.par[row - 1] = (uint8_t)(row == 2 ? v + 5 : v);
        out.rxCmd = RXC_SET;
        out.rxArg = ((row - 1) << 5) | r.par[row - 1];
      }
      drawRxSetRow(row);
      rxsShown = rxsSum();
    }
    return;
  }
  if (modal == M_SHEET) {
    if (id >= ID_SHEET && id < ID_SHEET + SHEET_N) {   // настройка открывается поверх страницы, список на это время убран
      sheetBack = true;
      modal = M_PARAM;
      modalArg = SHEET[id - ID_SHEET];
      modalValue = view.val[modalArg];
      needFull = true;
    } else if (id == ID_M_OK) closeModal();
    return;
  }
  if (modal != M_NONE) {
    if (id >= ID_M_OPT) {
      out.setParam = modalArg;
      out.setValue = id - ID_M_OPT;
      view.val[modalArg] = out.setValue;
      closeModal();
    } else if (id == ID_M_OK) {
      if (modal == M_PARAM) {
        out.setParam = modalArg;
        out.setValue = modalValue;
        view.val[modalArg] = modalValue;
      } else if (modal == M_CONFIRM) {
        if (modalArg == CF_SCAN) out.scan = true;
        if (modalArg == CF_RESTART) out.restart = true;
        if (modalArg == CF_FACTORY) out.factory = true;
        if (modalArg == CF_REC_STOP) out.recToggle = true;
        if (modalArg == CF_POWEROFF) out.powerOff = true;
        if (modalArg == CF_TEST) {
          out.setParam = P_TONE;
          out.setValue = view.lastTone >= 1 && view.lastTone <= 6 ? view.lastTone : 5;
          view.val[P_TONE] = out.setValue;
        }
        if (modalArg == CF_FORGET) {
          out.rxCmd = RXC_FORGET;
          memcpy(out.rxId, rxSelId, 3);
        }
      }
      closeModal();
    } else if (id == ID_M_CANCEL) {
      if (modal == M_PARAM && modalArg == P_BRIGHT) {   // вернуть прежнюю яркость
        out.setParam = P_BRIGHT;
        out.setValue = view.val[P_BRIGHT];
      }
      closeModal();
    }
    return;
  }
  if (id >= ID_TAB && id < ID_TAB + PG_N) {
    if (id - ID_TAB != page) slideTo(id - ID_TAB);
    else if (article >= 0) {
      article = -1;
      needFull = true;
    }
    return;
  }
  if (id == ID_ROW + P_TONE) {   // у проверочного звука настроек много — они в отдельном окне
    modal = M_SHEET;
    drawModal();
    return;
  }
  if (id >= ID_ROW && id < ID_ROW + P_COUNT) return openParam(id - ID_ROW);
  if (id >= ID_RXROW && id < ID_RXROW + RX_ROWS) {
    int idx = rxPage * rxPerPage() + (id - ID_RXROW);
    if (idx >= view.rxN) return;
    View::Rx &r = view.rx[idx];
    Box b = itemBox(id);
    memcpy(rxSelId, r.id, 3);
    if (r.online && pressX >= b.x + b.w - 96) {   // выключатель справа: включить или выключить приёмник
      out.rxCmd = RXC_ENABLE;
      out.rxArg = r.off ? 1 : 0;
      memcpy(out.rxId, r.id, 3);
      r.off = !r.off;
      for (int k = 1; k <= 6; k++) {
        drawToggle(b.x + b.w - 68, b.y + 13, r.off ? 1 - k / 6.0f : k / 6.0f);
        pause(14);
      }
      drawRxRow(id - ID_RXROW, false);
    } else {
      modal = M_RX;
      drawModal();
    }
    return;
  }
  if (id >= ID_HELP && id < ID_HELP + HELP_N) {
    article = id - ID_HELP;
    articlePage = 0;
    needFull = true;
    return;
  }
  auto confirm = [](int what) {
    modal = M_CONFIRM;
    modalArg = what;
    drawModal();
  };
  switch (id) {
    case ID_MUTE:   // тишина включается и выключается сразу: её и нажимают, когда нужно немедленно
      view.muted = !view.muted;
      out.mute = view.muted;
      drawItem(ID_MUTE, false);
      break;
    case ID_SRC_LIVE:
      if (view.fileOn) {        // в эфире файл с карты — остановить его: вернётся звук источника
        uiFm(FM_STOP, NULL, NULL);
        break;
      }
      if (view.val[P_TONE]) {   // вернуть звук с пульта — сразу, без вопросов
        out.setParam = P_TONE;
        out.setValue = 0;
        view.val[P_TONE] = 0;
        needFull = true;
      }
      break;
    case ID_SRC_TEST:
      if (view.fileOn) {        // в эфире файл — показать карту (там его можно остановить)
        fmOpen();
        break;
      }
      if (!view.val[P_TONE]) confirm(CF_TEST);   // случайное касание посреди служения не должно пустить в эфир проверку
      else {                                     // проверка уже идёт — показать её настройки
        modal = M_SHEET;
        drawModal();
      }
      break;
    case ID_HOME_RX: slideTo(PG_RX); break;
    case ID_HOME_FILES:
      if (view.sd) fmOpen();
      break;
    case ID_RX_PREV:
    case ID_RX_NEXT: {
      int pages = (view.rxN + rxPerPage() - 1) / rxPerPage();
      rxPage = (rxPage + (id == ID_RX_NEXT ? 1 : pages - 1)) % (pages ? pages : 1);
      needFull = true;
      break;
    }
    case ID_POWEROFF:
    case ID_POWER: confirm(CF_POWEROFF); break;
    case ID_SRCNAME:   // переименовать источник звука
      strlcpy(kbText, view.srcName, sizeof(kbText));
      utf8Clean(kbText);   // обрывки букв от прежней ошибки стирания — убрать
      kbShift = !kbText[0];
      kbDigits = false;
      kbForSource = true;
      modal = M_KEYS;
      drawModal();
      break;
    case ID_PAIR_OPEN:
      out.pairOpen = 1;
      modal = M_PAIR;
      drawModal();
      break;
    case ID_OTA_OPEN:
      out.updScan = true;   // заодно заглянуть в папку UPDATE на карте
      modal = M_OTA;
      drawModal();
      break;
    case ID_HOME_CH: slideTo(PG_AIR); break;
    case ID_HOME_KIT: slideTo(PG_AIR); break;
    case ID_HOME_REC:
      if (view.rec) confirm(CF_REC_STOP);
      else out.recToggle = true;
      break;
    case ID_SCAN:
      if (!view.scanning) confirm(CF_SCAN);
      break;
    case ID_ABOUT:
      modal = M_ABOUT;
      drawModal();
      break;
    case ID_RESTART: confirm(CF_RESTART); break;
    case ID_FACTORY: confirm(CF_FACTORY); break;
    case ID_ART_BACK:
      article = -1;
      needFull = true;
      break;
    case ID_ART_PREV:
      if (articlePage > 0) {
        articlePage--;
        needFull = true;
      }
      break;
    case ID_ART_NEXT:
      if (articlePage + 1 < articlePages) {
        articlePage++;
        needFull = true;
      }
      break;
  }
}

static void redrawItem(int id, bool pressed) {
  if (id == ID_NONE) return;
  if (modal == M_FILES) return fmItem(id, pressed);
  if (modal == M_RX) drawRxButton(id, pressed);
  else if (modal == M_RXSET) drawRxSetButton(id, pressed);
  else if (modal == M_PAIR) drawPairButton(id, pressed);
  else if (modal == M_OTA) drawOtaButton(id, pressed);
  else if (modal != M_NONE) drawModalItem(id, pressed);
  else if (id >= ID_TAB && id < ID_TAB + PG_N) return;   // вкладки при нажатии не меняются
  else drawItem(id, pressed);
  if (modal == M_PARAM && (id == ID_M_MINUS || id == ID_M_PLUS)) return;
}

// ---------------------------------------------------------------- заставка
// Сначала экран заливается фоном (подсветку включают после этого), затем эмблема проявляется из темноты,
// за ней по очереди проступают название и подпись, и бежит полоска запуска. Всего около четырёх секунд;
// передача звука идёт с первой секунды и заставку не ждёт.
static void splashPrepare() {
  g.target(drawBuf(), 0, 0, W, H);
  g.fill(0, 0, W, H, C_BG);
  flush();
}

static void fadeText(int y, const char *s, const GFXfont *f, uint16_t c, int steps) {
  int w = trW(s, f) + 8, h = f->yAdvance + 6;
  for (int k = 1; k <= steps; k++) {
    g.fill(240 - w / 2, y - h + 8, w, h, C_BG);
    g.text(240, y, s, f, m2::Gfx::blend(C_BG, c, (uint8_t)(255 * k / steps)), AL_C);
    pause(28);
  }
}

// Эмблема собирается из трёх слоёв (logo.h), каждый — со своим приёмом:
//   глобус   — раскрывается по кругу, как стрелка часов, и при этом подрастает;
//   книга    — распахивается от корешка в стороны и приподнимается;
//   крест    — опускается сверху и «садится» с лёгким отскоком;
// затем от креста расходится кольцо света, и по всей эмблеме пробегает блик.
struct LogoFx {
  float gS = 1, gWipe = 1;      // глобус: масштаб и доля раскрытого круга
  float bSx = 1;                // книга: раскрытие по ширине, 0…1
  int bDy = 0, cDy = 0;         // сдвиги книги и креста по высоте
  uint8_t gA = 255, bA = 255, cA = 255, cGlow = 0;
  float hR = 0;                 // кольцо света: радиус и яркость
  uint8_t hA = 0;
  float sPos = -9;              // блик: положение по диагонали, 0…1
};
static uint8_t *logoAngle;      // угол каждой точки от центра глобуса (0…255, по часовой от верха)
static uint32_t splashFrameAvgMs, splashFrameMaxMs;   // сколько считался кадр заставки (для проверки на плате)
const int LOGO_FRAMES = 92;

// Рисует прямоугольник [x0,x1)×[y0,y1) эмблемы. flat — эмблема уже собрана: берётся готовая картинка
// (один слой вместо трёх), поверх — только кольцо света и блик. Точки, где слой непрозрачен целиком,
// кладутся без смешивания: так кадр укладывается в полтора десятка миллисекунд.
static void logoFrame(int lx, int ly, const LogoFx &fx, int x0, int y0, int x1, int y1, bool flat) {
  static uint16_t rowBuf[LOGO_BIG_W];
  const int LW = LOGO_BIG_W, LH = LOGO_BIG_H;
  x0 = x0 < 0 ? 0 : x0;
  y0 = y0 < 0 ? 0 : y0;
  x1 = x1 > LW ? LW : x1;
  y1 = y1 > LH ? LH : y1;
  const float cxg = LW * 0.5f, cyg = LH * 0.40f, invG = 1.0f / fx.gS;
  const float cxb = LW * 0.5f, invB = fx.bSx > 0.02f ? 1.0f / fx.bSx : 0;
  const float hx = LW * 0.5f, hy = LH * 0.36f;
  const float r0 = fx.hR - 14, r1 = fx.hR + 14;
  const int wipe = (int)(fx.gWipe * 262);   // с запасом на мягкий край
  const bool gScale = fx.gS < 0.999f, bScale = fx.bSx < 0.999f || fx.bSx > 1.001f;
  for (int y = y0; y < y1; y++) {
    int gy = gScale ? (int)(cyg + (y - cyg) * invG + 0.5f) : y;
    int by = y - fx.bDy, cy = y - fx.cDy;
    bool gRow = !flat && fx.gA && gy >= 0 && gy < LH, bRow = !flat && fx.bA && invB && by >= 0 && by < LH;
    bool cRow = !flat && fx.cA && cy >= 0 && cy < LH;
    float dy2 = (y - hy) * (y - hy);
    bool hRow = fx.hA && dy2 < r1 * r1;
    for (int x = x0; x < x1; x++) {
      uint16_t c = C_BG;
      bool solid = false;
      if (flat) {
        c = LOGO_BIG[y * LW + x];
        solid = c != C_BG;
      }
      if (gRow) {
        int gx = gScale ? (int)(cxg + (x - cxg) * invG + 0.5f) : x;
        if (gx >= 0 && gx < LW) {
          int a = LOGO_GLOBE_A[gy * LW + gx];
          if (a && fx.gWipe < 1 && logoAngle) {
            int d = wipe - logoAngle[gy * LW + gx];
            a = d <= 0 ? 0 : d < 8 ? a * d / 8 : a;
          }
          if (a) {
            a = a * fx.gA / 255;
            c = a >= 250 ? LOGO_GLOBE[gy * LW + gx] : m2::Gfx::blend(c, LOGO_GLOBE[gy * LW + gx], (uint8_t)a);
            solid = true;
          }
        }
      }
      if (bRow) {
        int bx = bScale ? (int)(cxb + (x - cxb) * invB + 0.5f) : x;
        if (bx >= 0 && bx < LW) {
          int a = LOGO_BOOK_A[by * LW + bx];
          if (a) {
            a = a * fx.bA / 255;
            c = a >= 250 ? LOGO_BOOK[by * LW + bx] : m2::Gfx::blend(c, LOGO_BOOK[by * LW + bx], (uint8_t)a);
            solid = true;
          }
        }
      }
      if (cRow) {
        int a = LOGO_CROSS_A[cy * LW + x];
        if (a) {
          uint16_t k = LOGO_CROSS[cy * LW + x];
          a = a * fx.cA / 255;
          c = a >= 250 ? k : m2::Gfx::blend(c, k, (uint8_t)a);
          solid = true;
        }
      }
      if (hRow) {   // кольцо света от креста
        float d2 = (x - hx) * (x - hx) + dy2;
        if (d2 > r0 * r0 && d2 < r1 * r1) {
          float d = fabsf(sqrtf(d2) - fx.hR) / 14;
          c = m2::Gfx::blend(c, RGB(255, 236, 190), (uint8_t)((1 - d) * fx.hA));
        }
      }
      if (solid && fx.sPos > -1) {   // блик по диагонали — только по самой эмблеме
        float u = (x + y * 0.6f) / (LW + LH * 0.6f), d = fabsf(u - fx.sPos);
        if (d < 0.09f) c = m2::Gfx::blend(c, 0xFFFF, (uint8_t)((1 - d / 0.09f) * 130));
      }
      rowBuf[x] = c;
    }
    g.blit(lx + x0, ly + y, x1 - x0, 1, rowBuf + x0);
  }
}

static float easeOut(float t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return 1 - (1 - t) * (1 - t) * (1 - t);
}
static float easeBack(float t) {   // с лёгким перелётом и возвратом
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const float c1 = 1.70158f, c3 = c1 + 1;
  return 1 + c3 * (t - 1) * (t - 1) * (t - 1) + c1 * (t - 1) * (t - 1);
}

// Кадр f заставки (0…LOGO_FRAMES−1). Перерисовывается только та часть эмблемы, где сейчас что-то движется.
static void logoAnimFrame(int f, int lx, int ly, bool whole = false) {
  const int LW = LOGO_BIG_W, LH = LOGO_BIG_H;
  LogoFx fx;
  float tg = f / 23.0f, tb = (f - 18) / 22.0f, tc = (f - 38) / 20.0f, th = (f - 58) / 15.0f, ts = (f - 72) / 18.0f;
  fx.gWipe = tg >= 1 ? 1 : tg * tg * (3 - 2 * tg);
  fx.gS = 0.82f + 0.18f * easeOut(tg);
  fx.bA = tb <= 0 ? 0 : 255;
  fx.bSx = tb <= 0 ? 0 : easeBack(tb);
  if (fx.bSx > 1.06f) fx.bSx = 1.06f;
  fx.bDy = (int)(18 * (1 - easeOut(tb)));
  fx.cA = tc <= 0 ? 0 : (uint8_t)(255 * (tc * 2.5f > 1 ? 1 : tc * 2.5f));
  fx.cDy = (int)(-70 * (1 - easeBack(tc)));
  if (th > 0 && th < 1) {
    fx.hR = 8 + 150 * easeOut(th);
    fx.hA = (uint8_t)(200 * (1 - th));
  }
  if (ts > 0 && ts < 1) fx.sPos = -0.1f + 1.2f * ts;
  if (whole || f <= 24) logoFrame(lx, ly, fx, 0, 0, LW, LH, false);              // глобус раскрывается — вся эмблема
  else if (f <= 41) {                                                             // книга: её полоса
    logoFrame(lx, ly, fx, 0, 74, LW, LH, false);
    if (f >= 38) logoFrame(lx, ly, fx, 86, 0, 162, 74, false);                    // и крест уже показался сверху
  } else if (f <= 59) logoFrame(lx, ly, fx, 86, 0, 162, 214, false);              // крест: его столбец
  else logoFrame(lx, ly, fx, 0, 0, LW, LH, true);                                 // свет и блик — по готовой эмблеме
}

static void wipeText(int y, const char *s, const GFXfont *f, uint16_t c, int steps) {   // надпись выезжает слева направо
  int w = trW(s, f), x0 = 240 - w / 2;
  for (int k = 1; k <= steps; k++) {
    m2::Rect old = g.narrow(x0 - 2, y - f->yAdvance, (w + 4) * k / steps, f->yAdvance + 10);
    g.text(240, y, s, f, c, AL_C);
    g.restore(old);
    pause(22);
  }
}

static void splash(const char *version) {
  const int lx = (W - LOGO_BIG_W) / 2, ly = 56;
  const int LW = LOGO_BIG_W, LH = LOGO_BIG_H;
  if (animate && !logoAngle) logoAngle = (uint8_t *)malloc(LW * LH);
  if (logoAngle) {
    const float cx = LW * 0.5f, cy = LH * 0.40f;
    for (int y = 0; y < LH; y++)
      for (int x = 0; x < LW; x++) {
        float a = atan2f(x - cx, cy - y);   // 0 — вверх, по часовой стрелке
        if (a < 0) a += 2 * PI;
        logoAngle[y * LW + x] = (uint8_t)(a * (255.0f / (2 * PI)));
      }
  }
  uint32_t sum = 0, worst = 0, frames = 0;
  for (int f = animate ? 0 : LOGO_FRAMES - 1; f < LOGO_FRAMES; f++) {
    uint32_t t0 = millis();
    logoAnimFrame(f, lx, ly, !animate);
    uint32_t dt = millis() - t0;
    sum += dt;
    frames++;
    if (dt > worst) worst = dt;
    pause(dt >= 28 ? 2 : 30 - (int)dt);   // около 33 кадров в секунду
  }
  splashFrameAvgMs = frames ? sum / frames : 0;
  splashFrameMaxMs = worst;
  if (logoAngle) {
    free(logoAngle);
    logoAngle = nullptr;
  }
  wipeText(344, tr("Церква «Відродження»"), F_H, C_FG, animate ? 14 : 1);
  fadeText(376, tr("передавач звуку для слабочуючих"), F_TXT, C_MUTED_FG, animate ? 9 : 1);
  char t[32];
  snprintf(t, sizeof(t), tr("версія %s"), version);
  g.text(240, 462, t, F_CAP, C_BORDER, AL_C);
  // полоска «запускаюсь»: бежит с разгоном и замедлением ровно четыре секунды — по часам, а не по числу шагов
  // (владелец 07.10: «анимация загрузки на передатчике 4 секунды (полоска загрузки внизу заставки)»; было около секунды)
  g.box(140, 408, 200, 6, 3, C_MUTED);
  if (animate) {
    const uint32_t BAR_MS = 4000;
    uint32_t b0 = millis();
    for (;;) {
      uint32_t e = millis() - b0;
      float t2 = e >= BAR_MS ? 1.0f : (float)e / BAR_MS, k = t2 * t2 * (3 - 2 * t2);
      int w = (int)(200 * k);
      if (w >= 6) g.box(140, 408, w, 6, 3, C_PRIMARY);
      pause(20);
      if (e >= BAR_MS) break;
    }
    pause(250);
  } else g.box(140, 408, 200, 6, 3, C_PRIMARY);
}

// ---------------------------------------------------------------- передатчик выключен
// hold: 0…1 — сколько уже держат палец (при 1 передатчик включается)
static void drawStandby(float hold, bool first) {
  g.target(drawBuf(), 0, 0, W, H);
  if (first) {
    g.fill(0, 0, W, H, C_BG);
    g.blit((W - LOGO_SM_W) / 2, 150, LOGO_SM_W, LOGO_SM_H, LOGO_SM);
    g.text(240, 244, tr("Передавач вимкнено"), F_H, C_FG, AL_C);
    g.text(240, 276, tr("Торкніться екрана, щоб увімкнути"), F_CAP, C_MUTED_FG, AL_C);
    g.box(140, 312, 200, 8, 4, C_MUTED);
  }
  int w = (int)(200 * (hold < 0 ? 0 : hold > 1 ? 1 : hold));
  if (w < 200) g.box(140 + (w > 8 ? w - 8 : 0), 312, 200 - (w > 8 ? w - 8 : 0), 8, 4, C_MUTED);
  if (w >= 8) g.box(140, 312, w, 8, 4, C_PRIMARY);
  flush();
}

// ---------------------------------------------------------------- всплывающее сообщение
static char toastText[128], toastText2[128];
static uint32_t toastUntil = 0;
static void toast(const char *text) {
  strlcpy(toastText, text, sizeof(toastText));
  toastText2[0] = 0;
  toastUntil = millis() + 3200;
}
// важное сообщение в две строки, держится дольше (передатчик сам сменил канал: что сделал и почему)
static void toast2(const char *line1, const char *line2, uint32_t ms) {
  strlcpy(toastText, line1, sizeof(toastText));
  strlcpy(toastText2, line2, sizeof(toastText2));
  toastUntil = millis() + ms;
  toastOn = false;   // если на экране прежнее сообщение — нарисовать поверх новое
}
static uint32_t hopShown = 0;   // о каком самостоятельном уходе с канала уже сказано
static void hopNotice() {
  if (view.hopSeq == hopShown) return;
  hopShown = view.hopSeq;
  char a[96], b[120];
  if (view.hopResult == 3) {
    snprintf(a, sizeof(a), tr("Ефір на каналі %u зайнятий"), view.hopFrom);
    snprintf(b, sizeof(b), tr("Пакети чекали до %u мс. Шукаю вільніший канал — 2 с тиші"), view.hopWaitMs);
    toast2(a, b, 5000);
  } else if (view.hopResult == 1) {
    snprintf(a, sizeof(a), tr("Канал змінено: з %u на %u"), view.hopFrom, view.hopTo);
    if (view.hopWaitMs) snprintf(b, sizeof(b), tr("Причина: ефір був зайнятий, пакети чекали до %u мс"), view.hopWaitMs);
    else snprintf(b, sizeof(b), tr("Причина: перевірка з порту"));
    toast2(a, b, 12000);
    if (page == PG_AIR || page == PG_HOME) needFull = true;   // номер канала на странице — новый
  } else if (view.hopResult == 2) {
    snprintf(a, sizeof(a), tr("Ефір зайнятий, вільнішого каналу немає"));
    snprintf(b, sizeof(b), tr("Лишаюсь на каналі %u (пакети чекали до %u мс)"), view.hopFrom, view.hopWaitMs);
    toast2(a, b, 12000);
  }
}

static void toastFrame() {
  hopNotice();
  bool want = toastUntil && millis() < toastUntil;
  if (modal != M_NONE) return;
  // Страница под сообщением живёт (уровень, спектр, плитки) и может закрасить его — поэтому сообщение, пока оно
  // на экране, пять раз в секунду рисуется заново.
  if (want && (!toastOn || frameNo % 10 == 0)) {
    if (toastText2[0]) {   // две строки
      int w1 = m2::Gfx::textW(toastText, F_TXTB), w2 = m2::Gfx::textW(toastText2, F_CAP), w = (w1 > w2 ? w1 : w2) + 44;
      w = w > 456 ? 456 : w;
      g.box(240 - w / 2, H - TAB - 76, w, 64, 18, C_PRIMARY);
      g.text(240, H - TAB - 50, toastText, F_TXTB, C_PRIMARY_FG, AL_C, w - 24);
      g.text(240, H - TAB - 26, toastText2, F_CAP, C_PRIMARY_FG, AL_C, w - 24);
    } else {
      int w = m2::Gfx::textW(toastText, F_CAPB) + 40;
      w = w > 440 ? 440 : w;
      g.box(240 - w / 2, H - TAB - 50, w, 38, 19, C_PRIMARY);
      g.text(240, H - TAB - 25, toastText, F_CAPB, C_PRIMARY_FG, AL_C, w - 24);
    }
    toastOn = true;
  } else if (!want && toastOn) {
    toastOn = false;
    toastUntil = 0;
    needFull = true;   // убрать и вернуть то, что было под сообщением
  }
}

// ---------------------------------------------------------------- один кадр
// Вызывать около 50 раз в секунду: down, tx, ty — палец на экране и где.
static void frameBody(bool down, int tx, int ty) {
  frameNo++;
  if (down) out.touched = true;
  if (modal == M_KEYS) {   // клавиатура: своя обработка касаний (каждое событие сенсора — со своим временем, см. touchMs)
    kbTouch(down, tx, ty, touchMs);
    if (modal == M_KEYS) kbLive();
    if (needFull) drawAll();
    return;
  }
  // --- нажатия: действие — когда палец отпустили на том же элементе
  if (down && !wasDown) {
    pressId = ID_NONE;
    for (int i = hotN - 1; i >= 0; i--)
      if (hot[i].b.has(tx, ty)) {
        pressId = hot[i].id;
        break;
      }
    if (pressId == ID_NONE && modal == M_NONE && ty >= H - TAB) pressId = ID_TAB + tx / TABW;
    pressX = tx;
    pressY = ty;
    pressMs = millis();
    repeatMs = pressMs + 450;
    redrawItem(pressId, true);
    if (modal == M_PARAM && (pressId == ID_M_MINUS || pressId == ID_M_PLUS)) applyStep(pressId == ID_M_PLUS ? 1 : -1);
  } else if (down && pressId != ID_NONE) {
    Box pb = { 0, 0, 0, 0 };
    for (int i = 0; i < hotN; i++)
      if (hot[i].id == pressId) pb = hot[i].b;
    if (pb.w && (tx < pb.x - 14 || tx >= pb.x + pb.w + 14 || ty < pb.y - 14 || ty >= pb.y + pb.h + 14)) {
      redrawItem(pressId, false);   // палец увели с кнопки — ничего не нажато
      pressId = ID_NONE;
    } else if (modal == M_PARAM && (pressId == ID_M_MINUS || pressId == ID_M_PLUS) && millis() >= repeatMs) {   // держат — значение бежит
      applyStep(pressId == ID_M_PLUS ? 1 : -1);
      repeatMs = millis() + 110;
    }
  } else if (!down && wasDown && pressId != ID_NONE) {
    int id = pressId;
    pressId = ID_NONE;
    bool isStep = modal == M_PARAM && (id == ID_M_MINUS || id == ID_M_PLUS);
    redrawItem(id, false);
    if (!isStep) activate(id);
  }
  wasDown = down;

  if (needFull) {
    drawAll();
    toastOn = false;
    return;
  }
  fmEndNotice();   // файл с карты доиграл — сказать (на любой странице)
  toastFrame();
  if (modal == M_FILES) return fmLive();
  // --- живые части
  int st = pillState();
  if (st != shownPill && modal == M_NONE) drawPill(st);
  if (modal == M_NONE && (frameNo & 1)) {   // движение значков — 25 раз в секунду
    tabLive = page == PG_RX ? rxOnline() > 0 : view.radioOk;
    pillLamp(st, millis() / 1000.0f);
    tabAnim();
    lampsAnim();
  }
  if (modal == M_ABOUT) {
    if ((int)view.uptimeS != shownStat) {
      shownStat = view.uptimeS;
      drawAboutLines();
    }
    return;
  }
  if (modal == M_RX) {
    if (rxFind(rxSelId) < 0) closeModal();
    else if ((int)view.uptimeS != shownStat) {
      shownStat = view.uptimeS;
      drawRxLines();
    }
    return;
  }
  if (modal == M_RXSET) {   // настройки приёмника живые: он мог сменить их своей ручкой — перерисовать строки
    int i = rxFind(rxSelId);
    if (i < 0 || !view.rx[i].online) {
      modal = M_RX;
      needFull = true;
    } else if (pressId == ID_NONE && rxsSum() != rxsShown) {
      for (int row = 0; row < RXS_N; row++) drawRxSetRow(row);
      drawRxSetButton(ID_RXS_MINUS + 2, false);
      drawRxSetButton(ID_RXS_PLUS + 2, false);
      drawRxSetButton(ID_RXS_EAR, false);
      rxsShown = rxsSum();
    }
    return;
  }
  if (modal == M_OTA) {   // ход обновления: что-то изменилось — перерисовать окно; пока идёт работа — точки «жду»
    if (pressId == ID_NONE && otaSum() != otaShown) {
      modalAgain = true;
      drawModal();
      modalAgain = false;
    }
    else if (otaSelf() && (frameNo & 1)) waitDots(W / 2 - 18, 406, C_BG);   // на крупном экране — под полосой хода
    else if (((view.ota.stage >= 1 && view.ota.stage <= 5) || (view.ota.stage == 0 && (view.ota.card == 0 || view.ota.card == 2))) && (frameNo & 1))
      waitDots(mBox.x + mBox.w - 56, mBox.y + 30, C_CARD);
    return;
  }
  if (modal == M_PAIR) {   // список просьб живой: приёмник появился или исчез — перерисовать окно
    if (pressId == ID_NONE && pairSum() != pairShown) {
      modalAgain = true;
      drawModal();
      modalAgain = false;
    }
    else if (!view.askN && (frameNo & 1)) waitDots(mBox.x + mBox.w / 2 - 18, mBox.y + 194, C_SECONDARY);   // ждём приёмник
    return;
  }
  if (modal != M_NONE) return;
  if (frameNo % 8 == 0 && pressId == ID_NONE) {   // строки настроек: значение сменилось без нажатия — показать новое
    char v[64];
    for (int i = 0; i < hotN; i++) {
      int p = hot[i].id - ID_ROW;
      if (p < 0 || p >= P_COUNT) continue;
      rowText(p, v, sizeof(v));
      if (strcmp(v, rowShown[p])) drawItem(hot[i].id, false);
    }
    if (page == PG_HOME && muteShown != (int)view.muted) drawItem(ID_MUTE, false);        // тишину переключили не отсюда
    if ((page == PG_HOME || page == PG_SOUND) && srcShown != (view.fileOn ? 2 : view.val[P_TONE] != 0)) {   // источник сменили не отсюда
      if (page == PG_HOME) needFull = true;   // на главной меняется и подпись уровня
      else {
        drawItem(ID_SRC_LIVE, false);
        drawItem(ID_SRC_TEST, false);
      }
    }
  }
  switch (page) {
    case PG_HOME: drawHomeLive(false); break;
    case PG_RX:
      if ((frameNo & 3) == 0 && pressId == ID_NONE)   // приёмники на связи — их точки дышат
        for (int k = 0; k < rxPerPage() && rxPage * rxPerPage() + k < view.rxN; k++) {
          const View::Rx &r = view.rx[rxPage * rxPerPage() + k];
          if (!r.online || r.off) continue;
          Box b = itemBox(ID_RXROW + k);
          float a = 0.5f + 0.5f * sinf(millis() / 1000.0f * 2.4f + k);
          g.circle(b.x + 20, b.y + 19, 7.5f, C_CARD);
          g.circle(b.x + 20, b.y + 19, 4.8f + 1.4f * a, m2::Gfx::blend(C_CARD, C_OK, (uint8_t)(150 + 105 * a)));
        }
      if (frameNo % 25 == 0) {   // список живой: имена, сигнал, громкость
        uint32_t sum = view.rxN * 7919u;
        for (int i = 0; i < view.rxN; i++) {
          const View::Rx &r = view.rx[i];
          sum = sum * 31 + r.online + r.off * 2 + r.mute * 4 + r.volume * 8 + (uint32_t)(r.rssi / 6) * 256;
          sum = sum * 31 + r.sleep;
          for (const char *c = r.name; *c; c++) sum = sum * 31 + (uint8_t)*c;
        }
        if (sum != shownRxSum) {
          if (shownRxSum && pressId == ID_NONE) needFull = true;
          shownRxSum = sum;
        }
      }
      break;
    case PG_SOUND:
      drawSpectrum();   // каждый кадр: риски двигаются плавно (перерисовывается только изменившееся)
      break;
    case PG_AIR: {
      int sc = view.scanning ? view.scanStep : view.scanned ? 100 : -1;
      if (sc != shownScan) {
        shownScan = sc;
        needFull = !view.scanning;   // поиск кончился — перерисовать с новым каналом
        drawChannels();
        drawItem(ID_SCAN, false);
      }
      break;
    }
  }
}

static void frame(bool down, int tx, int ty) {
  frameBody(down, tx, ty);
  flush();
}

// fb — память экрана; shadow — лист того же размера (480×480) для рисования
static void begin(uint16_t *fb, uint16_t *shadow) {
  fbMain = fb;
  fbShadow = shadow;
  g.target(drawBuf(), 0, 0, W, H);
}
}  // namespace ui
